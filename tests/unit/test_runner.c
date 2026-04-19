#include "../common/test_helpers.h"

#include <stdio.h>
#include <stdlib.h>

int g_tests_run    = 0;
int g_tests_failed = 0;

void run_config_tests(void);

int main(void)
{
    printf("=== home-appliances unit tesztek ===\n");

    run_config_tests();

    printf("\nEredmény: %d/%d teszt sikeres\n",
           g_tests_run - g_tests_failed, g_tests_run);

    return g_tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
