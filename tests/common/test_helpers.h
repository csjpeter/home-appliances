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
