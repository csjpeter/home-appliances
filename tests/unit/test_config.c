#include "../common/test_helpers.h"
#include "core/config.h"

#include <string.h>

static void test_config_defaults(void)
{
    Config cfg = {0};
    config_load(&cfg);

    ASSERT(strlen(cfg.network) > 0, "network nem üres");
    ASSERT(strlen(cfg.iface) > 0, "iface nem üres");
    ASSERT(cfg.discovery_timeout_ms > 0, "timeout pozitív");
}

void run_config_tests(void)
{
    printf("config tesztek:\n");
    RUN_TEST(test_config_defaults);
}
