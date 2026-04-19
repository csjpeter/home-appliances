#include "core/config.h"
#include "core/logger.h"
#include "domain/appliance_service.h"
#include "infrastructure/gree_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Helpers ─────────────────────────────────────────────────────────── */

static int broadcast_from_cfg(const Config *cfg, char *out, size_t out_len)
{
    const char *slash = strchr(cfg->network, '/');
    if (!slash) {
        fprintf(stderr, "Invalid network address: %s\n", cfg->network);
        return -1;
    }
    int a, b, c;
    if (sscanf(cfg->network, "%d.%d.%d.", &a, &b, &c) != 3) {
        fprintf(stderr, "Cannot parse network: %s\n", cfg->network);
        return -1;
    }
    snprintf(out, out_len, "%d.%d.%d.255", a, b, c);
    return 0;
}

/* ── list command ────────────────────────────────────────────────────── */

static int cmd_list(const Config *cfg)
{
    char broadcast[32];
    if (broadcast_from_cfg(cfg, broadcast, sizeof(broadcast)) != 0)
        return -1;

    ApplianceList list = {0};
    if (appliance_service_discover(broadcast, cfg->discovery_timeout_ms,
                                   &list) != 0) {
        fprintf(stderr, "Discovery failed.\n");
        return -1;
    }
    appliance_service_print(&list);
    appliance_list_free(&list);
    return 0;
}

/* ── ac subcommands ──────────────────────────────────────────────────── */

static int cmd_ac_list(const Config *cfg)
{
    char broadcast[32];
    if (broadcast_from_cfg(cfg, broadcast, sizeof(broadcast)) != 0)
        return -1;

    GreeDeviceList scanned = {0};
    if (gree_client_scan(broadcast, cfg->discovery_timeout_ms, &scanned) != 0) {
        fprintf(stderr, "Scan failed.\n");
        return -1;
    }

    GreeDeviceList saved = {0};
    gree_client_load_bound(&saved);
    gree_client_merge_bound(&scanned, &saved);
    gree_device_list_free(&saved);

    if (scanned.count == 0) {
        printf("No Gree AC units found.\n");
    } else {
        printf("%-20s %-14s %-10s %-8s %s\n",
               "IP", "MAC", "Firmware", "Bound", "Name");
        printf("%-20s %-14s %-10s %-8s %s\n",
               "--------------------", "--------------",
               "----------", "--------", "----");
        for (int i = 0; i < scanned.count; i++) {
            const GreeDevice *dev = &scanned.devices[i];
            printf("%-20s %-14s %-10s %-8s %s\n",
                   dev->ip, dev->mac, dev->firmware,
                   dev->bound ? "yes" : "no", dev->name);
        }
    }

    gree_device_list_free(&scanned);
    return 0;
}

static int cmd_ac_bind(const char *ip, const Config *cfg)
{
    char broadcast[32];
    if (broadcast_from_cfg(cfg, broadcast, sizeof(broadcast)) != 0)
        return -1;

    /* Scan to discover the device and obtain its MAC */
    GreeDeviceList scanned = {0};
    if (gree_client_scan(broadcast, cfg->discovery_timeout_ms, &scanned) != 0) {
        fprintf(stderr, "Scan failed.\n");
        return -1;
    }

    GreeDevice *target = NULL;
    for (int i = 0; i < scanned.count; i++) {
        if (strcmp(scanned.devices[i].ip, ip) == 0) {
            target = &scanned.devices[i];
            break;
        }
    }
    if (!target) {
        fprintf(stderr, "Device not found at %s\n", ip);
        gree_device_list_free(&scanned);
        return -1;
    }

    if (gree_client_bind(target) != 0) {
        fprintf(stderr, "Bind failed for %s\n", ip);
        gree_device_list_free(&scanned);
        return -1;
    }

    /* Copy bound device before freeing scanned list */
    GreeDevice bound = *target;
    gree_device_list_free(&scanned);

    /* Merge with existing saved devices and save */
    GreeDeviceList saved = {0};
    gree_client_load_bound(&saved);

    int found = 0;
    for (int i = 0; i < saved.count; i++) {
        if (strncmp(saved.devices[i].mac, bound.mac, GREE_MAC_LEN - 1) == 0) {
            memcpy(saved.devices[i].device_key, bound.device_key,
                   sizeof(bound.device_key));
            memcpy(saved.devices[i].ip, bound.ip, sizeof(bound.ip));
            saved.devices[i].bound = 1;
            found = 1;
            break;
        }
    }
    if (!found) {
        GreeDevice *tmp = realloc(saved.devices,
                                  (size_t)(saved.count + 1) * sizeof(GreeDevice));
        if (tmp) {
            saved.devices = tmp;
            saved.devices[saved.count++] = bound;
        }
    }

    int rc = gree_client_save_bound(&saved);
    gree_device_list_free(&saved);

    if (rc == 0)
        printf("Bound %s (MAC: %s)\n", bound.ip, bound.mac);
    return rc;
}

static int cmd_ac_status(const char *ip)
{
    GreeDeviceList saved = {0};
    if (gree_client_load_bound(&saved) != 0) {
        fprintf(stderr, "Cannot load device store.\n");
        return -1;
    }

    GreeDevice *dev = NULL;
    for (int i = 0; i < saved.count; i++) {
        if (strcmp(saved.devices[i].ip, ip) == 0) {
            dev = &saved.devices[i];
            break;
        }
    }
    if (!dev || !dev->bound) {
        fprintf(stderr, "Device %s not bound. Run: %s ac bind %s\n",
                ip, "home-appliances", ip);
        gree_device_list_free(&saved);
        return -1;
    }

    GreeStatus st = {0};
    int rc = gree_client_get_status(dev, &st);
    gree_device_list_free(&saved);
    if (rc != 0) {
        fprintf(stderr, "Failed to get status from %s\n", ip);
        return -1;
    }

    static const char *modes[] = {"auto", "cool", "dry", "fan", "heat"};
    static const char *fans[]  = {"auto", "low",  "med", "high", "turbo"};
    const char unit = (st.temp_unit == GREE_TEMUN_FAHRENHEIT) ? 'F' : 'C';

    printf("Power:       %s\n",  st.power ? "on" : "off");
    printf("Temperature: %d°%c\n", st.set_temp, unit);
    printf("Room temp:   %d°%c\n", st.room_temp, unit);
    printf("Mode:        %s\n",
           (st.mode >= 0 && st.mode < 5) ? modes[st.mode] : "?");
    printf("Fan:         %s\n",
           (st.fan >= 0 && st.fan < 5) ? fans[st.fan] : "?");
    printf("Lights:      %s\n",  st.lights ? "on" : "off");
    printf("Quiet:       %s\n",  st.quiet  ? "on" : "off");
    printf("Turbo:       %s\n",  st.turbo  ? "on" : "off");
    printf("Sleep:       %s\n",  st.sleep  ? "on" : "off");
    return 0;
}

/* Parse a user-supplied "key=value" string into a Gree protocol key and int.
 * Returns 0 on success, -1 on unknown key or bad value. */
static int parse_ac_param(const char *arg, const char **gree_key, int *gree_val)
{
#define KEY_EQ(pat) \
    (key_len == sizeof(pat) - 1 && strncmp(key, (pat), key_len) == 0)
#define VAL_EQ(s) (strcmp(val, (s)) == 0)

    const char *eq = strchr(arg, '=');
    if (!eq) {
        fprintf(stderr, "Expected key=value, got: %s\n", arg);
        return -1;
    }
    const char *key = arg;
    const char *val = eq + 1;
    size_t key_len  = (size_t)(eq - arg);

    if (KEY_EQ("power")) {
        *gree_key = "Pow";
        if (VAL_EQ("on"))  { *gree_val = 1; return 0; }
        if (VAL_EQ("off")) { *gree_val = 0; return 0; }
    } else if (KEY_EQ("temp")) {
        *gree_key = "SetTem";
        *gree_val = atoi(val);
        if (*gree_val < 16 || *gree_val > 30) {
            fprintf(stderr, "temp must be 16-30\n");
            return -1;
        }
        return 0;
    } else if (KEY_EQ("mode")) {
        *gree_key = "Mod";
        if (VAL_EQ("auto")) { *gree_val = 0; return 0; }
        if (VAL_EQ("cool")) { *gree_val = 1; return 0; }
        if (VAL_EQ("dry"))  { *gree_val = 2; return 0; }
        if (VAL_EQ("fan"))  { *gree_val = 3; return 0; }
        if (VAL_EQ("heat")) { *gree_val = 4; return 0; }
    } else if (KEY_EQ("fan")) {
        *gree_key = "Wnd";
        if (VAL_EQ("auto")) { *gree_val = 0; return 0; }
        if (VAL_EQ("low"))  { *gree_val = 1; return 0; }
        if (VAL_EQ("med"))  { *gree_val = 2; return 0; }
        if (VAL_EQ("high")) { *gree_val = 3; return 0; }
    } else if (KEY_EQ("lights")) {
        *gree_key = "Lig";
        if (VAL_EQ("on"))  { *gree_val = 1; return 0; }
        if (VAL_EQ("off")) { *gree_val = 0; return 0; }
    } else if (KEY_EQ("quiet")) {
        *gree_key = "Quiet";
        if (VAL_EQ("on"))  { *gree_val = 1; return 0; }
        if (VAL_EQ("off")) { *gree_val = 0; return 0; }
    } else if (KEY_EQ("turbo")) {
        *gree_key = "Tur";
        if (VAL_EQ("on"))  { *gree_val = 1; return 0; }
        if (VAL_EQ("off")) { *gree_val = 0; return 0; }
    } else if (KEY_EQ("sleep")) {
        *gree_key = "SvSt";
        if (VAL_EQ("on"))  { *gree_val = 1; return 0; }
        if (VAL_EQ("off")) { *gree_val = 0; return 0; }
    }

#undef KEY_EQ
#undef VAL_EQ

    fprintf(stderr, "Unknown parameter or value: %s\n", arg);
    return -1;
}

static int cmd_ac_set(const char *ip, char **args, int nargs)
{
    if (nargs <= 0) {
        fprintf(stderr, "ac set: at least one key=value required\n");
        return -1;
    }

    GreeDeviceList saved = {0};
    if (gree_client_load_bound(&saved) != 0) {
        fprintf(stderr, "Cannot load device store.\n");
        return -1;
    }

    GreeDevice *dev = NULL;
    for (int i = 0; i < saved.count; i++) {
        if (strcmp(saved.devices[i].ip, ip) == 0) {
            dev = &saved.devices[i];
            break;
        }
    }
    if (!dev || !dev->bound) {
        fprintf(stderr, "Device %s not bound. Run: %s ac bind %s\n",
                ip, "home-appliances", ip);
        gree_device_list_free(&saved);
        return -1;
    }

    const char *keys[16];
    int         vals[16];
    int         count = 0;

    for (int i = 0; i < nargs && count < 16; i++) {
        if (parse_ac_param(args[i], &keys[count], &vals[count]) != 0) {
            gree_device_list_free(&saved);
            return -1;
        }
        count++;
    }

    int rc = gree_client_set(dev, keys, vals, count);
    gree_device_list_free(&saved);
    if (rc != 0)
        fprintf(stderr, "Set command failed for %s\n", ip);
    return rc;
}

/* ── Usage ───────────────────────────────────────────────────────────── */

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <command> [options]\n"
        "\n"
        "Commands:\n"
        "  list                    Scan and list all appliances\n"
        "  ac list                 List Gree AC units (with bind status)\n"
        "  ac bind <ip>            Bind to AC at <ip>\n"
        "  ac status <ip>          Show AC status\n"
        "  ac set <ip> key=val ... Set AC parameters:\n"
        "    power=on|off          Power on or off\n"
        "    temp=<16-30>          Target temperature (Celsius)\n"
        "    mode=auto|cool|heat|dry|fan\n"
        "    fan=auto|low|med|high Fan speed\n"
        "    lights=on|off         Display lights\n"
        "    quiet=on|off          Quiet mode\n"
        "    turbo=on|off          Turbo mode\n"
        "    sleep=on|off          Sleep mode\n"
        "  version                 Print version\n"
        "  help                    Show this help\n",
        prog);
}

/* ── Entry point ─────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    if (argc < 2
        || strcmp(argv[1], "help")    == 0
        || strcmp(argv[1], "--help")  == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "version") == 0
        || strcmp(argv[1], "--version") == 0) {
        printf("home-appliances %s\n", HOME_APPLIANCES_VERSION);
        return 0;
    }

    logger_init(NULL, LOG_INFO);

    Config cfg = {0};
    if (config_load(&cfg) != 0) {
        fprintf(stderr, "Failed to load configuration.\n");
        logger_close();
        return 1;
    }

    int ret = 0;

    if (strcmp(argv[1], "list") == 0) {
        ret = cmd_list(&cfg);

    } else if (strcmp(argv[1], "ac") == 0) {
        if (argc < 3) {
            print_usage(argv[0]);
            ret = 1;
        } else if (strcmp(argv[2], "list") == 0) {
            ret = cmd_ac_list(&cfg);
        } else if (strcmp(argv[2], "bind") == 0) {
            if (argc < 4) {
                fprintf(stderr, "ac bind: missing <ip>\n");
                ret = 1;
            } else {
                ret = cmd_ac_bind(argv[3], &cfg);
            }
        } else if (strcmp(argv[2], "status") == 0) {
            if (argc < 4) {
                fprintf(stderr, "ac status: missing <ip>\n");
                ret = 1;
            } else {
                ret = cmd_ac_status(argv[3]);
            }
        } else if (strcmp(argv[2], "set") == 0) {
            if (argc < 5) {
                fprintf(stderr, "ac set: missing <ip> and parameters\n");
                ret = 1;
            } else {
                ret = cmd_ac_set(argv[3], argv + 4, argc - 4);
            }
        } else {
            fprintf(stderr, "Unknown ac subcommand: %s\n", argv[2]);
            print_usage(argv[0]);
            ret = 1;
        }

    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        print_usage(argv[0]);
        ret = 1;
    }

    logger_close();
    return ret == 0 ? 0 : 1;
}
