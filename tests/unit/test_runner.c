#include "../common/test_helpers.h"

#include <stdio.h>
#include <stdlib.h>

int g_tests_run    = 0;
int g_tests_failed = 0;

void run_config_tests(void);
void run_base64_tests(void);
void run_device_store_tests(void);

int main(void)
{
    printf("=== home-appliances unit tests ===\n");

    run_config_tests();
    run_base64_tests();
    run_device_store_tests();

    printf("\nResult: %d/%d tests passed\n",
           g_tests_run - g_tests_failed, g_tests_run);

    return g_tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
