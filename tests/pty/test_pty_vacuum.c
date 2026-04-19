/**
 * @file test_pty_vacuum.c
 * @brief PTY-based CLI tests for vacuum (Roborock) commands.
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

static void test_vacuum_no_subcommand_shows_usage(void)
{
    const char *argv[] = { HOME_APPLIANCES_BIN, "vacuum", NULL };
    PtySession *s = launch(argv);
    ASSERT(s != NULL, "pty_open+run vacuum succeeds");
    if (!s) return;

    int found = pty_wait_for(s, "vacuum start", 2000);
    pty_close(s);
    ASSERT(found == 0, "vacuum: 'vacuum start' shown in usage");
}

static void test_vacuum_usage_contains_stop(void)
{
    const char *argv[] = { HOME_APPLIANCES_BIN, "vacuum", NULL };
    PtySession *s = launch(argv);
    ASSERT(s != NULL, "pty_open+run vacuum succeeds");
    if (!s) return;

    int found = pty_wait_for(s, "start|stop", 2000);
    pty_close(s);
    ASSERT(found == 0, "vacuum: 'start|stop' commands shown in usage");
}

static void test_vacuum_usage_contains_status(void)
{
    const char *argv[] = { HOME_APPLIANCES_BIN, "vacuum", NULL };
    PtySession *s = launch(argv);
    ASSERT(s != NULL, "pty_open+run vacuum succeeds");
    if (!s) return;

    int found = pty_wait_for(s, "vacuum status", 2000);
    pty_close(s);
    ASSERT(found == 0, "vacuum: 'vacuum status' shown in usage");
}

static void test_vacuum_start_no_ip_shows_usage(void)
{
    const char *argv[] = { HOME_APPLIANCES_BIN, "vacuum", "start", NULL };
    PtySession *s = launch(argv);
    ASSERT(s != NULL, "pty_open+run vacuum start succeeds");
    if (!s) return;

    int found = pty_wait_for(s, "Usage", 2000);
    pty_close(s);
    ASSERT(found == 0, "vacuum start (no ip): Usage shown");
}

static void test_vacuum_fan_no_args_shows_usage(void)
{
    const char *argv[] = { HOME_APPLIANCES_BIN, "vacuum", "fan", NULL };
    PtySession *s = launch(argv);
    ASSERT(s != NULL, "pty_open+run vacuum fan succeeds");
    if (!s) return;

    int found = pty_wait_for(s, "Usage", 2000);
    pty_close(s);
    ASSERT(found == 0, "vacuum fan (no args): Usage shown");
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== PTY Vacuum CLI Tests ===\n");

    RUN_TEST(test_vacuum_no_subcommand_shows_usage);
    RUN_TEST(test_vacuum_usage_contains_stop);
    RUN_TEST(test_vacuum_usage_contains_status);
    RUN_TEST(test_vacuum_start_no_ip_shows_usage);
    RUN_TEST(test_vacuum_fan_no_args_shows_usage);

    printf("\nResults: %d/%d passed", g_tests_run - g_tests_failed, g_tests_run);
    if (g_tests_failed)
        printf(" (%d FAILED)", g_tests_failed);
    printf("\n");

    return g_tests_failed ? 1 : 0;
}
