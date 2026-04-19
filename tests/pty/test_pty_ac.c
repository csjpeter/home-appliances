/**
 * @file test_pty_ac.c
 * @brief PTY-based CLI tests for AC (Gree) commands.
 *
 * Runs the home-appliances binary in a PTY and verifies that usage text and
 * error messages appear correctly without any network connection.
 */

#include "../../tests/common/test_helpers.h"
#include "ptytest.h"

#include <stdio.h>
#include <stdlib.h>

int g_tests_run    = 0;
int g_tests_failed = 0;

#ifndef HOME_APPLIANCES_BIN
#define HOME_APPLIANCES_BIN "bin/home-appliances"
#endif

/* ── Helpers ──────────────────────────────────────────────────────────── */

/* 200 rows to avoid any scrolling for long help output. */
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

static void test_no_args_shows_usage(void)
{
    const char *argv[] = { HOME_APPLIANCES_BIN, NULL };
    PtySession *s = launch(argv);
    ASSERT(s != NULL, "pty_open+run succeeds");
    if (!s) return;

    int found = pty_wait_for(s, "Usage", 2000);
    pty_close(s);
    ASSERT(found == 0, "no-args: 'Usage' shown on screen");
}

static void test_help_flag(void)
{
    const char *argv[] = { HOME_APPLIANCES_BIN, "--help", NULL };
    PtySession *s = launch(argv);
    ASSERT(s != NULL, "pty_open+run --help succeeds");
    if (!s) return;

    int found = pty_wait_for(s, "home-appliances", 2000);
    pty_close(s);
    ASSERT(found == 0, "--help: binary name shown on screen");
}

static void test_ac_no_subcommand_shows_usage(void)
{
    const char *argv[] = { HOME_APPLIANCES_BIN, "ac", NULL };
    PtySession *s = launch(argv);
    ASSERT(s != NULL, "pty_open+run ac succeeds");
    if (!s) return;

    /* 'ac list' is the scan subcommand shown in help */
    int found = pty_wait_for(s, "ac list", 2000);
    pty_close(s);
    ASSERT(found == 0, "ac: 'ac list' shown in usage");
}

static void test_ac_usage_contains_bind(void)
{
    const char *argv[] = { HOME_APPLIANCES_BIN, "ac", NULL };
    PtySession *s = launch(argv);
    ASSERT(s != NULL, "pty_open+run ac succeeds");
    if (!s) return;

    int found = pty_wait_for(s, "ac bind", 2000);
    pty_close(s);
    ASSERT(found == 0, "ac: 'ac bind' shown in usage");
}

static void test_ac_usage_contains_status(void)
{
    const char *argv[] = { HOME_APPLIANCES_BIN, "ac", NULL };
    PtySession *s = launch(argv);
    ASSERT(s != NULL, "pty_open+run ac succeeds");
    if (!s) return;

    int found = pty_wait_for(s, "ac status", 2000);
    pty_close(s);
    ASSERT(found == 0, "ac: 'ac status' shown in usage");
}

static void test_ac_set_no_args_error(void)
{
    const char *argv[] = { HOME_APPLIANCES_BIN, "ac", "set", NULL };
    PtySession *s = launch(argv);
    ASSERT(s != NULL, "pty_open+run ac set succeeds");
    if (!s) return;

    int found = pty_wait_for(s, "missing", 2000);
    pty_close(s);
    ASSERT(found == 0, "ac set (no args): 'missing' error shown");
}

static void test_ac_bind_no_ip_error(void)
{
    const char *argv[] = { HOME_APPLIANCES_BIN, "ac", "bind", NULL };
    PtySession *s = launch(argv);
    ASSERT(s != NULL, "pty_open+run ac bind succeeds");
    if (!s) return;

    int found = pty_wait_for(s, "missing", 2000);
    pty_close(s);
    ASSERT(found == 0, "ac bind (no ip): 'missing' error shown");
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== PTY AC CLI Tests ===\n");

    RUN_TEST(test_no_args_shows_usage);
    RUN_TEST(test_help_flag);
    RUN_TEST(test_ac_no_subcommand_shows_usage);
    RUN_TEST(test_ac_usage_contains_bind);
    RUN_TEST(test_ac_usage_contains_status);
    RUN_TEST(test_ac_set_no_args_error);
    RUN_TEST(test_ac_bind_no_ip_error);

    printf("\nResults: %d/%d passed", g_tests_run - g_tests_failed, g_tests_run);
    if (g_tests_failed)
        printf(" (%d FAILED)", g_tests_failed);
    printf("\n");

    return g_tests_failed ? 1 : 0;
}
