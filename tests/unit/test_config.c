#include "../common/test_helpers.h"
#include "core/config.h"

#include <stdlib.h>
#include <string.h>

static void test_config_defaults(void)
{
    Config cfg = {0};
    config_load(&cfg);

    ASSERT(strlen(cfg.network) > 0, "network is not empty");
    ASSERT(strlen(cfg.iface) > 0, "iface is not empty");
    ASSERT(cfg.discovery_timeout_ms > 0, "timeout is positive");
}

static void test_config_env_override(void)
{
    /* Point HOME at /tmp so no real config file is found. */
    const char *saved_home = getenv("HOME");
    setenv("HOME", "/tmp", 1);

    Config cfg = {0};
    int rc = config_load(&cfg);

    /* Restore HOME before any assertion so the env is clean on failure. */
    if (saved_home)
        setenv("HOME", saved_home, 1);
    else
        unsetenv("HOME");

    ASSERT(rc == 0, "config_load succeeds even without a config file");
    ASSERT(strlen(cfg.network) > 0, "network default is set when no config file");
    ASSERT(strlen(cfg.iface) > 0, "iface default is set when no config file");
    ASSERT(cfg.discovery_timeout_ms > 0, "timeout default is positive when no config file");
}

static void test_config_network_format(void)
{
    Config cfg = {0};
    config_load(&cfg);

    /* Default is CIDR ("192.168.1.0/24"). Accept either '/' or '.' as valid. */
    int has_slash = strchr(cfg.network, '/') != NULL;
    int has_dot   = strchr(cfg.network, '.') != NULL;
    ASSERT(has_slash || has_dot, "network contains '/' (CIDR) or '.' (IP-like)");
}

static void test_config_timeout_range(void)
{
    Config cfg = {0};
    config_load(&cfg);

    ASSERT(cfg.discovery_timeout_ms >= 100,
           "discovery_timeout_ms is at least 100 ms");
    ASSERT(cfg.discovery_timeout_ms <= 30000,
           "discovery_timeout_ms is at most 30000 ms");
}

static void test_config_iface_not_empty(void)
{
    Config cfg = {0};
    config_load(&cfg);

    ASSERT(cfg.iface[0] != '\0', "iface is not an empty string");
}

void run_config_tests(void)
{
    printf("config tests:\n");
    RUN_TEST(test_config_defaults);
    RUN_TEST(test_config_env_override);
    RUN_TEST(test_config_network_format);
    RUN_TEST(test_config_timeout_range);
    RUN_TEST(test_config_iface_not_empty);
}
