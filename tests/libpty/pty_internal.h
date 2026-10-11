/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-19 */
/* Internal structures shared between the pty test library modules. */

#ifndef PTY_INTERNAL_H
#define PTY_INTERNAL_H

/**
 * @file pty_internal.h
 * @brief Internal data structures shared between libpty modules.
 */

#include "ptytest.h"
#include <sys/types.h>

/* ── Screen cell ─────────────────────────────────────────────────────── */

typedef struct {
    char ch[8];   /**< UTF-8 character (up to 4 bytes + NUL) */
    int  attr;    /**< PTY_ATTR_* bitmask */
} PtyCell;

/* ── Virtual screen buffer ───────────────────────────────────────────── */

typedef struct PtyScreen {
    int      cols, rows;
    int      cur_row, cur_col;
    int      cur_attr;        /**< Current SGR attribute state */
    int      pending_wrap;    /**< 1 = cursor past last column, wrap on next char */
    PtyCell *cells;           /**< rows × cols cell array */
} PtyScreen;

PtyScreen  *pty_screen_new(int cols, int rows);
void        pty_screen_free(PtyScreen *scr);
void        pty_screen_feed(PtyScreen *scr, const char *data, size_t len);

/* ── PTY session ─────────────────────────────────────────────────────── */

struct PtySession {
    int        master_fd;
    pid_t      child_pid;
    int        cols, rows;
    PtyScreen *screen;
};

#endif /* PTY_INTERNAL_H */
