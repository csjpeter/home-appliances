#include "config.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CONFIG_PATH_FMT "%s/.config/home-appliances/config.ini"

static void set_defaults(Config *cfg)
{
    strncpy(cfg->network, "192.168.1.0/24", MAX_NETWORK_LEN - 1);
    strncpy(cfg->iface, "wlan0", MAX_IFACE_LEN - 1);
    cfg->discovery_timeout_ms = 5000;
}

static int config_path(char *buf, size_t len)
{
    const char *home = getenv("HOME");
    if (!home) {
        LOG_ERROR_MSG("HOME environment variable not set");
        return -1;
    }
    snprintf(buf, len, CONFIG_PATH_FMT, home);
    return 0;
}

int config_load(Config *cfg)
{
    set_defaults(cfg);

    char path[512] = {0};
    if (config_path(path, sizeof(path)) != 0)
        return -1;

    FILE *f = fopen(path, "r");
    if (!f)
        return 0; /* first run: defaults apply */

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char numval[32] = {0};
        if (sscanf(line, "network=%63[^\n]", cfg->network) == 1)
            continue;
        if (sscanf(line, "iface=%31[^\n]", cfg->iface) == 1)
            continue;
        if (sscanf(line, "discovery_timeout_ms=%31[^\n]", numval) == 1)
            cfg->discovery_timeout_ms = atoi(numval);
    }
    fclose(f);
    return 0;
}

int config_save(const Config *cfg)
{
    const char *home = getenv("HOME");
    if (!home)
        return -1;

    char dir[768];
    snprintf(dir, sizeof(dir), "%s/.config/home-appliances", home);
    mkdir(dir, 0700);

    char path[800];
    snprintf(path, sizeof(path), "%s/config.ini", dir);
    FILE *f = fopen(path, "w");
    if (!f) {
        LOG_ERROR_MSG("Cannot write config: %s", path);
        return -1;
    }
    fprintf(f, "network=%s\n", cfg->network);
    fprintf(f, "iface=%s\n", cfg->iface);
    fprintf(f, "discovery_timeout_ms=%d\n", cfg->discovery_timeout_ms);
    fclose(f);
    return 0;
}
