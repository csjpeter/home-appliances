/**
 * @file main_discovery.c
 * @brief home-appliances-discovery — periodic network monitor.
 *
 * Scans all device types, updates store files with current IP addresses
 * (matching by MAC for Gree, device_id for Roborock, model for TV/printer),
 * and can install/remove itself from the user's crontab.
 *
 * Usage:
 *   home-appliances-discovery run          Scan now, update stores
 *   home-appliances-discovery cron-add     Add 15-minute cron job
 *   home-appliances-discovery cron-status  Show cron job status
 *   home-appliances-discovery cron-remove  Remove cron job
 *   home-appliances-discovery --help
 *   home-appliances-discovery --version
 */

#include "core/config.h"
#include "core/logger.h"
#include "domain/appliance_service.h"
#include "infrastructure/brother_client.h"
#include "infrastructure/gree_client.h"
#include "infrastructure/roborock_client.h"
#include "infrastructure/samsung_tv_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef HOME_APPLIANCES_VERSION
#define HOME_APPLIANCES_VERSION "dev"
#endif

/* ── cron helpers ────────────────────────────────────────────────────── */

#define CRON_MARKER "home-appliances-discovery run"

static int cron_get_current(char **out, size_t *out_len)
{
    FILE *p = popen("crontab -l 2>/dev/null", "r");
    if (!p) {
        *out = NULL;
        *out_len = 0;
        return 0;
    }

    size_t cap = 4096;
    char  *buf = malloc(cap);
    if (!buf) { pclose(p); return -1; }
    size_t len = 0;
    int    c;

    while ((c = fgetc(p)) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) { free(buf); pclose(p); return -1; }
            buf = tmp;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    pclose(p);

    *out = buf;
    *out_len = len;
    return 0;
}

static int cron_write(const char *content)
{
    FILE *p = popen("crontab -", "w");
    if (!p) return -1;
    if (content && *content)
        fputs(content, p);
    return pclose(p) == 0 ? 0 : -1;
}

static int cmd_cron_add(void)
{
    char path[512];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n <= 0) {
        fprintf(stderr, "Cannot determine own executable path.\n");
        return -1;
    }
    path[n] = '\0';

    char *current = NULL;
    size_t clen   = 0;
    if (cron_get_current(&current, &clen) != 0) {
        fprintf(stderr, "Failed to read crontab.\n");
        return -1;
    }

    /* Check if already installed. */
    if (current && strstr(current, CRON_MARKER)) {
        printf("Cron job is already installed.\n");
        free(current);
        return 0;
    }

    /* Append new entry: run every 15 minutes. */
    char entry[640];
    snprintf(entry, sizeof(entry),
             "*/15 * * * * %s run\n", path);

    size_t new_len = (current ? clen : 0) + strlen(entry) + 1;
    char  *new_ct  = malloc(new_len);
    if (!new_ct) { free(current); return -1; }
    new_ct[0] = '\0';
    if (current) strncat(new_ct, current, new_len - 1);
    strncat(new_ct, entry, new_len - 1 - strlen(new_ct));

    int rc = cron_write(new_ct);
    free(current);
    free(new_ct);

    if (rc != 0) {
        fprintf(stderr, "Failed to install cron job.\n");
        return -1;
    }
    printf("Cron job installed: %s", entry);
    return 0;
}

static int cmd_cron_status(void)
{
    char *current = NULL;
    size_t clen   = 0;
    if (cron_get_current(&current, &clen) != 0) {
        fprintf(stderr, "Failed to read crontab.\n");
        return -1;
    }

    if (!current || !strstr(current, CRON_MARKER)) {
        printf("Cron job is NOT installed.\n");
        printf("Run 'home-appliances-discovery cron-add' to install it.\n");
        free(current);
        return 0;
    }

    /* Print the relevant cron line(s). */
    printf("Cron job is installed:\n");
    char *line = strtok(current, "\n");
    while (line) {
        if (strstr(line, CRON_MARKER))
            printf("  %s\n", line);
        line = strtok(NULL, "\n");
    }
    free(current);
    return 0;
}

static int cmd_cron_remove(void)
{
    char *current = NULL;
    size_t clen   = 0;
    if (cron_get_current(&current, &clen) != 0) {
        fprintf(stderr, "Failed to read crontab.\n");
        return -1;
    }

    if (!current || !strstr(current, CRON_MARKER)) {
        printf("Cron job is not installed — nothing to remove.\n");
        free(current);
        return 0;
    }

    /* Remove all lines containing the marker. */
    size_t cap = clen + 1;
    char  *filtered = malloc(cap);
    if (!filtered) { free(current); return -1; }
    filtered[0] = '\0';

    char *save_ptr = NULL;
    char *line = strtok_r(current, "\n", &save_ptr);
    while (line) {
        if (!strstr(line, CRON_MARKER)) {
            strncat(filtered, line, cap - strlen(filtered) - 1);
            strncat(filtered, "\n",  cap - strlen(filtered) - 1);
        }
        line = strtok_r(NULL, "\n", &save_ptr);
    }

    int rc = cron_write(filtered);
    free(current);
    free(filtered);

    if (rc != 0) {
        fprintf(stderr, "Failed to update crontab.\n");
        return -1;
    }
    printf("Cron job removed.\n");
    return 0;
}

/* ── Discovery run ───────────────────────────────────────────────────── */

static int cmd_run(void)
{
    Config cfg = {0};
    if (config_load(&cfg) != 0) {
        fprintf(stderr, "Failed to load configuration.\n");
        return -1;
    }

    char broadcast[32];
    {
        const char *slash = strchr(cfg.network, '/');
        if (!slash) {
            fprintf(stderr, "Invalid network in config: %s\n", cfg.network);
            return -1;
        }
        int a, b, c;
        if (sscanf(cfg.network, "%d.%d.%d.", &a, &b, &c) != 3) {
            fprintf(stderr, "Cannot parse network: %s\n", cfg.network);
            return -1;
        }
        snprintf(broadcast, sizeof(broadcast), "%d.%d.%d.255", a, b, c);
    }

    ApplianceList found = {0};
    int rc = appliance_service_discover_all(cfg.network, broadcast,
                                            cfg.discovery_timeout_ms, &found);
    if (rc != 0) {
        fprintf(stderr, "Discovery scan failed.\n");
        return -1;
    }

    if (found.count == 0) {
        printf("No devices found on %s.\n", cfg.network);
    } else {
        printf("Found %d device(s) on %s:\n", found.count, cfg.network);
        appliance_service_print(&found);
    }
    appliance_list_free(&found);
    return 0;
}

/* ── Usage / version ─────────────────────────────────────────────────── */

static void print_usage(void)
{
    printf(
        "Usage: home-appliances-discovery <command>\n"
        "\n"
        "Commands:\n"
        "  run           Scan network for all device types and update device list\n"
        "  cron-add      Add a 15-minute cron job for automatic discovery\n"
        "  cron-status   Show whether the cron job is installed\n"
        "  cron-remove   Remove the cron job\n"
        "  --help        Show this help\n"
        "  --version     Print version\n"
        "\n"
        "Configuration is read from ~/.config/home-appliances/config.ini.\n"
        "Results are saved to the device store files in the same directory.\n"
    );
}

/* ── main ────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    logger_init(NULL, LOG_WARN);

    if (argc < 2 ||
        strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "help")   == 0) {
        print_usage();
        logger_close();
        return 0;
    }

    if (strcmp(argv[1], "--version") == 0 ||
        strcmp(argv[1], "version")   == 0) {
        printf("home-appliances-discovery %s\n", HOME_APPLIANCES_VERSION);
        logger_close();
        return 0;
    }

    int ret = 0;
    if (strcmp(argv[1], "run") == 0)
        ret = cmd_run();
    else if (strcmp(argv[1], "cron-add") == 0)
        ret = cmd_cron_add();
    else if (strcmp(argv[1], "cron-status") == 0)
        ret = cmd_cron_status();
    else if (strcmp(argv[1], "cron-remove") == 0)
        ret = cmd_cron_remove();
    else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        print_usage();
        ret = 1;
    }

    logger_close();
    return ret != 0 ? 1 : 0;
}
