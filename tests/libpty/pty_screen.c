/**
 * @file pty_screen.c
 * @brief Virtual screen buffer with VT100 escape sequence parser.
 *        Ported from email-cli/libs/libptytest — no external dependencies.
 *
 * Minimal VT100 subset:
 *   Cursor: CSI H/f, CSI A/B/C/D
 *   Erase:  CSI 2J (full), CSI K/2K (line)
 *   SGR:    0 (reset), 1 (bold), 2 (dim), 7 (reverse), colour (parsed, ignored)
 *   Control: \\n \\r \\b \\t
 *   UTF-8:  1-4 byte sequences stored per cell
 */

#include "pty_internal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Screen buffer management ────────────────────────────────────────── */

PtyScreen *pty_screen_new(int cols, int rows)
{
    PtyScreen *scr = calloc(1, sizeof(*scr));
    if (!scr) return NULL;
    scr->cols  = cols;
    scr->rows  = rows;
    scr->cells = calloc((size_t)(cols * rows), sizeof(PtyCell));
    if (!scr->cells) { free(scr); return NULL; }
    for (int i = 0; i < cols * rows; i++) {
        scr->cells[i].ch[0] = ' ';
        scr->cells[i].ch[1] = '\0';
    }
    return scr;
}

void pty_screen_free(PtyScreen *scr)
{
    if (!scr) return;
    free(scr->cells);
    free(scr);
}

static PtyCell *cell_at(PtyScreen *scr, int row, int col)
{
    if (row < 0 || row >= scr->rows || col < 0 || col >= scr->cols)
        return NULL;
    return &scr->cells[row * scr->cols + col];
}

static void scroll_up(PtyScreen *scr)
{
    memmove(&scr->cells[0],
            &scr->cells[scr->cols],
            (size_t)((scr->rows - 1) * scr->cols) * sizeof(PtyCell));
    for (int c = 0; c < scr->cols; c++) {
        PtyCell *cl = cell_at(scr, scr->rows - 1, c);
        cl->ch[0] = ' '; cl->ch[1] = '\0';
        cl->attr  = PTY_ATTR_NONE;
    }
}

/* ── VT100 CSI sequence handler ──────────────────────────────────────── */

static void apply_csi(PtyScreen *scr, const char *params, int param_len, char final)
{
    int args[16] = {0};
    int argc = 0;
    const char *p   = params;
    const char *end = params + param_len;

    while (p < end && argc < 16) {
        int val = 0;
        while (p < end && *p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        args[argc++] = val;
        if (p < end && *p == ';') p++;
    }

    scr->pending_wrap = 0;

    switch (final) {
    case 'H': case 'f':
        scr->cur_row = (argc >= 1 && args[0] > 0) ? args[0] - 1 : 0;
        scr->cur_col = (argc >= 2 && args[1] > 0) ? args[1] - 1 : 0;
        if (scr->cur_row >= scr->rows) scr->cur_row = scr->rows - 1;
        if (scr->cur_col >= scr->cols) scr->cur_col = scr->cols - 1;
        break;
    case 'A': { int n = (argc >= 1 && args[0] > 0) ? args[0] : 1;
                scr->cur_row -= n; if (scr->cur_row < 0) scr->cur_row = 0; } break;
    case 'B': { int n = (argc >= 1 && args[0] > 0) ? args[0] : 1;
                scr->cur_row += n; if (scr->cur_row >= scr->rows) scr->cur_row = scr->rows - 1; } break;
    case 'C': { int n = (argc >= 1 && args[0] > 0) ? args[0] : 1;
                scr->cur_col += n; if (scr->cur_col >= scr->cols) scr->cur_col = scr->cols - 1; } break;
    case 'D': { int n = (argc >= 1 && args[0] > 0) ? args[0] : 1;
                scr->cur_col -= n; if (scr->cur_col < 0) scr->cur_col = 0; } break;
    case 'J':
        if (args[0] == 2) {
            for (int i = 0; i < scr->cols * scr->rows; i++) {
                scr->cells[i].ch[0] = ' '; scr->cells[i].ch[1] = '\0';
                scr->cells[i].attr  = PTY_ATTR_NONE;
            }
        }
        break;
    case 'K': {
        int mode  = (argc >= 1) ? args[0] : 0;
        int start = (mode == 1 || mode == 2) ? 0         : scr->cur_col;
        int stop  = (mode == 0 || mode == 2) ? scr->cols : scr->cur_col + 1;
        for (int c = start; c < stop; c++) {
            PtyCell *cl = cell_at(scr, scr->cur_row, c);
            if (cl) { cl->ch[0] = ' '; cl->ch[1] = '\0'; cl->attr = PTY_ATTR_NONE; }
        } } break;
    case 'm':
        for (int i = 0; i < argc; i++) {
            if      (args[i] == 0) scr->cur_attr  = PTY_ATTR_NONE;
            else if (args[i] == 1) scr->cur_attr |= PTY_ATTR_BOLD;
            else if (args[i] == 2) scr->cur_attr |= PTY_ATTR_DIM;
            else if (args[i] == 7) scr->cur_attr |= PTY_ATTR_REVERSE;
            else if (args[i] == 38 || args[i] == 48) {
                if (i + 1 < argc && args[i+1] == 2) i += 4;
                else if (i + 1 < argc && args[i+1] == 5) i += 2;
            }
        }
        break;
    default: break;
    }
}

/* ── Main feed function ──────────────────────────────────────────────── */

void pty_screen_feed(PtyScreen *scr, const char *data, size_t len)
{
    const char *end = data + len;
    const char *p   = data;

    while (p < end) {
        unsigned char ch = (unsigned char)*p;

        if (ch == '\033') {
            if (p + 1 < end && p[1] == '[') {
                const char *csi_start = p + 2;
                const char *q = csi_start;
                while (q < end && ((*q >= '0' && *q <= '9') || *q == ';' || *q == '?')) q++;
                if (q < end) { apply_csi(scr, csi_start, (int)(q - csi_start), *q); p = q + 1; }
                else break;
            } else if (p + 1 < end && p[1] == ']') {
                const char *q = p + 2;
                while (q < end) {
                    if (*q == '\007') { q++; break; }
                    if (*q == '\033' && q + 1 < end && q[1] == '\\') { q += 2; break; }
                    q++;
                }
                p = q;
            } else { p++; }
            continue;
        }

        if (ch == '\n') {
            scr->pending_wrap = 0;
            scr->cur_row++;
            if (scr->cur_row >= scr->rows) { scr->cur_row = scr->rows - 1; scroll_up(scr); }
            p++; continue;
        }
        if (ch == '\r') { scr->pending_wrap = 0; scr->cur_col = 0; p++; continue; }
        if (ch == '\b') { scr->pending_wrap = 0; if (scr->cur_col > 0) scr->cur_col--; p++; continue; }
        if (ch == '\t') {
            scr->pending_wrap = 0;
            scr->cur_col = (scr->cur_col + 8) & ~7;
            if (scr->cur_col >= scr->cols) scr->cur_col = scr->cols - 1;
            p++; continue;
        }
        if (ch < 0x20) { p++; continue; }

        /* Printable char / UTF-8 */
        {
            if (scr->pending_wrap) {
                scr->pending_wrap = 0;
                scr->cur_col = 0;
                scr->cur_row++;
                if (scr->cur_row >= scr->rows) { scr->cur_row = scr->rows - 1; scroll_up(scr); }
            }

            PtyCell *cl = cell_at(scr, scr->cur_row, scr->cur_col);
            if (!cl) { p++; continue; }

            int bytes = 1;
            if      (ch >= 0xF0) bytes = 4;
            else if (ch >= 0xE0) bytes = 3;
            else if (ch >= 0xC0) bytes = 2;

            if (p + bytes > end) break;

            int copy = bytes < (int)sizeof(cl->ch) ? bytes : (int)sizeof(cl->ch) - 1;
            memcpy(cl->ch, p, (size_t)copy);
            cl->ch[copy] = '\0';
            cl->attr = scr->cur_attr;

            scr->cur_col++;
            if (scr->cur_col >= scr->cols) {
                scr->cur_col    = scr->cols - 1;
                scr->pending_wrap = 1;
            }
            p += bytes;
        }
    }
}
