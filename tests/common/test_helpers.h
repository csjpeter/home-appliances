/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-19 */
/* Minimal test framework: ASSERT and RUN_TEST. */

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <stdio.h>

extern int g_tests_run;
extern int g_tests_failed;

#define ASSERT(cond, msg) \
    do { \
        g_tests_run++; \
        if (!(cond)) { \
            fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, (msg)); \
            g_tests_failed++; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        printf("  %s ... ", #fn); \
        fflush(stdout); \
        fn(); \
        printf("OK\n"); \
    } while (0)

#endif /* TEST_HELPERS_H */
