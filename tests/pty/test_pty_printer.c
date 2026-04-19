/**
 * @file test_pty_printer.c
 * @brief PTY-based CLI tests for printer (Brother) commands.
 */

#include "../../tests/common/test_helpers.h"
#include "ptytest.h"

#include <stdio.h>

int g_tests_run    = 0;
int g_tests_failed = 0;

#ifndef HOME_APPLIANCES_BIN
#define HOME_APPLIANCES_BIN "bin/home-appliances"
#endif

/* 200 rows to avoid scrolling for 48-line help output. */
static PtySession *launch(const char *argv[])
{
    PtySession *s = pty_open(120, 200);
    if (!s)
        return NULL;
    if (pty_run(s, argv) != 0) {
        pty_close(s);
        return NULL;
    }
    return s;
}

/* ── Tests ────────────────────────────────────────────────────────────── */

static void test_printer_no_subcommand_shows_usage(void)
{
    const char *argv[] = { HOME_APPLIANCES_BIN, "printer", NULL };
    PtySession *s = launch(argv);
    ASSERT(s != NULL, "pty_open+run printer succeeds");
    if (!s) return;

    int found = pty_wait_for(s, "printer status", 2000);
    pty_close(s);
    ASSERT(found == 0, "printer: 'printer status' subcommand shown in usage");
}

static void test_printer_status_no_ip_shows_usage(void)
{
    const char *argv[] = { HOME_APPLIANCES_BIN, "printer", "status", NULL };
    PtySession *s = launch(argv);
    ASSERT(s != NULL, "pty_open+run printer status succeeds");
    if (!s) return;

    int found = pty_wait_for(s, "Usage", 2000);
    pty_close(s);
    ASSERT(found == 0, "printer status (no ip): Usage shown");
}

static void test_printer_usage_shown_in_global_help(void)
{
    const char *argv[] = { HOME_APPLIANCES_BIN, NULL };
    PtySession *s = launch(argv);
    ASSERT(s != NULL, "pty_open+run (no args) succeeds");
    if (!s) return;

    int found = pty_wait_for(s, "printer probe", 2000);
    pty_close(s);
    ASSERT(found == 0, "global help: 'printer probe' command mentioned");
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== PTY Printer CLI Tests ===\n");

    RUN_TEST(test_printer_no_subcommand_shows_usage);
    RUN_TEST(test_printer_status_no_ip_shows_usage);
    RUN_TEST(test_printer_usage_shown_in_global_help);

    printf("\nResults: %d/%d passed", g_tests_run - g_tests_failed, g_tests_run);
    if (g_tests_failed)
        printf(" (%d FAILED)", g_tests_failed);
    printf("\n");

    return g_tests_failed ? 1 : 0;
}
