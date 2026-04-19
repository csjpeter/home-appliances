#include "core/config.h"
#include "core/logger.h"
#include "domain/appliance_service.h"
#include "infrastructure/brother_client.h"
#include "infrastructure/gree_client.h"
#include "infrastructure/roborock_client.h"
#include "infrastructure/samsung_tv_client.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── Generic helpers ─────────────────────────────────────────────────── */

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

/* Returns 1 if the string looks like an IPv4 address (digits and dots only). */
static int looks_like_ip(const char *s)
{
    if (!s || !isdigit((unsigned char)s[0]))
        return 0;
    int dots = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '.') { dots++; }
        else if (!isdigit((unsigned char)*p)) return 0;
    }
    return dots == 3;
}

/* ── Gree helpers ────────────────────────────────────────────────────── */

static GreeDevice *gree_find_device(GreeDeviceList *list, const char *ip)
{
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->devices[i].ip, ip) == 0 && list->devices[i].bound)
            return &list->devices[i];
    }
    return NULL;
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

/* ── AC (Gree) commands ───────────────────────────────────────────────── */

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
        fprintf(stderr, "Device not found at %s. Is the AC on and on the same network?\n", ip);
        gree_device_list_free(&scanned);
        return -1;
    }

    if (gree_client_bind(target) != 0) {
        fprintf(stderr, "Bind failed for %s\n", ip);
        gree_device_list_free(&scanned);
        return -1;
    }

    GreeDevice bound = *target;
    gree_device_list_free(&scanned);

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

static void print_ac_status(const GreeStatus *st)
{
    static const char *modes[] = {"auto","cool","dry","fan","heat"};
    static const char *fans[]  = {"auto","low","med-low","medium","med-high","high"};
    const char unit = (st->temp_unit == GREE_TEMUN_FAHRENHEIT) ? 'F' : 'C';

    printf("Power:       %s\n",    st->power ? "on" : "off");
    printf("Target temp: %d°%c\n", st->set_temp, unit);
    printf("Room temp:   %d°C\n",  st->room_temp);
    printf("Mode:        %s\n",
           (st->mode >= 0 && st->mode <= 4) ? modes[st->mode] : "?");
    printf("Fan speed:   %s\n",
           (st->fan >= 0 && st->fan <= 5) ? fans[st->fan] : "?");
    printf("Swing V:     %s\n",    st->swing_v  ? "swing" : "fixed");
    printf("Swing H:     %s\n",    st->swing_h == 1 ? "swing" :
                                   st->swing_h == 0 ? "fixed" : "fixed-pos");
    printf("Quiet:       %s\n",    st->quiet       ? "on" : "off");
    printf("Turbo:       %s\n",    st->turbo       ? "on" : "off");
    printf("Sleep:       %s\n",    st->sleep       ? "on" : "off");
    printf("X-Fan:       %s\n",    st->xfan        ? "on" : "off");
    printf("Health:      %s\n",    st->health      ? "on" : "off");
    printf("Air:         %s\n",    st->air         ? "on" : "off");
    printf("Anti-frost:  %s\n",    st->steady_heat ? "on" : "off");
    printf("Lights:      %s\n",    st->lights      ? "on" : "off");
    printf("Unit:        %s\n",    unit == 'F' ? "Fahrenheit" : "Celsius");
    printf("Type:        %s\n",    st->heat_cool_type ? "heat+cool" : "cool-only");
}

static int cmd_ac_status_one(const char *ip)
{
    GreeDeviceList saved = {0};
    if (gree_client_load_bound(&saved) != 0) {
        fprintf(stderr, "Cannot load device store.\n");
        return -1;
    }
    GreeDevice *dev = gree_find_device(&saved, ip);
    if (!dev) {
        fprintf(stderr, "Device %s not bound. Run: ac bind %s\n", ip, ip);
        gree_device_list_free(&saved);
        return -1;
    }

    GreeStatus st = {0};
    int rc = gree_client_get_status(dev, &st);
    gree_device_list_free(&saved);
    if (rc != 0) {
        fprintf(stderr, "%s: failed to get status\n", ip);
        return -1;
    }
    print_ac_status(&st);
    return 0;
}

static int cmd_ac_temp_one(const char *ip)
{
    GreeDeviceList saved = {0};
    if (gree_client_load_bound(&saved) != 0)
        return -1;
    GreeDevice *dev = gree_find_device(&saved, ip);
    if (!dev) {
        fprintf(stderr, "Device %s not bound. Run: ac bind %s\n", ip, ip);
        gree_device_list_free(&saved);
        return -1;
    }
    GreeStatus st = {0};
    int rc = gree_client_get_status(dev, &st);
    gree_device_list_free(&saved);
    if (rc != 0) {
        fprintf(stderr, "room temperature unavailable\n");
        return -1;
    }
    printf("%d\n", st.room_temp);
    return 0;
}

/* Parse "key=value" into Gree protocol key + int value. */
static int parse_ac_param(const char *arg, const char **gree_key, int *gree_val)
{
#define KEY_EQ(pat) \
    (key_len == sizeof(pat) - 1 && strncmp(key, (pat), key_len) == 0)
#define VAL_EQ(s) (strcmp(val, (s)) == 0)

    const char *eq = strchr(arg, '=');
    if (!eq) { fprintf(stderr, "Expected key=value, got: %s\n", arg); return -1; }
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
            fprintf(stderr, "temp must be 16-30\n"); return -1;
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
        *gree_key = "WdSpd";
        if (VAL_EQ("auto"))     { *gree_val = 0; return 0; }
        if (VAL_EQ("low"))      { *gree_val = 1; return 0; }
        if (VAL_EQ("med-low"))  { *gree_val = 2; return 0; }
        if (VAL_EQ("medium") || VAL_EQ("med")) { *gree_val = 3; return 0; }
        if (VAL_EQ("med-high")) { *gree_val = 4; return 0; }
        if (VAL_EQ("high"))     { *gree_val = 5; return 0; }
        *gree_val = atoi(val);
        if (*gree_val >= 0 && *gree_val <= 5) return 0;
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
        *gree_key = "SwhSlp";
        if (VAL_EQ("on"))  { *gree_val = 1; return 0; }
        if (VAL_EQ("off")) { *gree_val = 0; return 0; }
    } else if (KEY_EQ("swing_v")) {
        *gree_key = "SwUpDn";
        if (VAL_EQ("on")  || VAL_EQ("1")) { *gree_val = 1; return 0; }
        if (VAL_EQ("off") || VAL_EQ("0")) { *gree_val = 0; return 0; }
    } else if (KEY_EQ("swing_h")) {
        *gree_key = "SwingLfRig";
        if (VAL_EQ("off"))  { *gree_val = 0; return 0; }
        if (VAL_EQ("full")) { *gree_val = 1; return 0; }
        *gree_val = atoi(val);
        if (*gree_val >= 0 && *gree_val <= 6) return 0;
    } else if (KEY_EQ("xfan")) {
        *gree_key = "Blo";
        if (VAL_EQ("on"))  { *gree_val = 1; return 0; }
        if (VAL_EQ("off")) { *gree_val = 0; return 0; }
    } else if (KEY_EQ("health")) {
        *gree_key = "Health";
        if (VAL_EQ("on"))  { *gree_val = 1; return 0; }
        if (VAL_EQ("off")) { *gree_val = 0; return 0; }
    } else if (KEY_EQ("air")) {
        *gree_key = "Air";
        if (VAL_EQ("on"))  { *gree_val = 1; return 0; }
        if (VAL_EQ("off")) { *gree_val = 0; return 0; }
    } else if (KEY_EQ("antifrost")) {
        *gree_key = "StHt";
        if (VAL_EQ("on"))  { *gree_val = 1; return 0; }
        if (VAL_EQ("off")) { *gree_val = 0; return 0; }
    } else if (KEY_EQ("unit")) {
        *gree_key = "TemUn";
        if (VAL_EQ("c") || VAL_EQ("celsius"))    { *gree_val = 0; return 0; }
        if (VAL_EQ("f") || VAL_EQ("fahrenheit")) { *gree_val = 1; return 0; }
    }

#undef KEY_EQ
#undef VAL_EQ
    fprintf(stderr, "Unknown parameter or value: %s\n", arg);
    return -1;
}

/* Collect leading IP addresses from args, remaining are key=value params.
 * Returns count of IPs found; sets *params_start to first non-IP index. */
static int collect_ips(char **args, int nargs, const char **ips, int max_ips,
                       int *params_start)
{
    int n = 0;
    int i = 0;
    for (; i < nargs && n < max_ips && looks_like_ip(args[i]); i++)
        ips[n++] = args[i];
    *params_start = i;
    return n;
}

/* Send a single key=value pair to one device. */
static int ac_set_single(const char *ip, const char *gree_key, int gree_val)
{
    GreeDeviceList saved = {0};
    if (gree_client_load_bound(&saved) != 0) return -1;
    GreeDevice *dev = gree_find_device(&saved, ip);
    if (!dev) {
        fprintf(stderr, "Device %s not bound. Run: ac bind %s\n", ip, ip);
        gree_device_list_free(&saved);
        return -1;
    }
    const char *keys[1] = { gree_key };
    int         vals[1] = { gree_val };
    int rc = gree_client_set(dev, keys, vals, 1);
    gree_device_list_free(&saved);
    return rc;
}

/* After setting power, query status for room temperature feedback. */
static int cmd_ac_power(const char *ip, int on_val)
{
    if (ac_set_single(ip, "Pow", on_val) != 0)
        return -1;
    printf("AC %s.\n", on_val ? "on" : "off");

    GreeDeviceList saved = {0};
    if (gree_client_load_bound(&saved) == 0) {
        GreeDevice *dev = gree_find_device(&saved, ip);
        if (dev) {
            GreeStatus st = {0};
            if (gree_client_get_status(dev, &st) == 0)
                printf("Room temp: %d°C\n", st.room_temp);
        }
        gree_device_list_free(&saved);
    }
    return 0;
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
    GreeDevice *dev = gree_find_device(&saved, ip);
    if (!dev) {
        fprintf(stderr, "Device %s not bound. Run: ac bind %s\n", ip, ip);
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
    else
        printf("Set.\n");
    return rc;
}

/* ── Vacuum (Roborock) commands ───────────────────────────────────────── */

static int get_vacuum(const char *ip, RoborockDevice *dev)
{
    memset(dev, 0, sizeof(*dev));
    strncpy(dev->ip, ip, sizeof(dev->ip) - 1);

    int rc = roborock_load(ip, dev);
    if (rc != 0) {
        /* not found or error — do hello */
        if (roborock_hello(dev) != 0) {
            fprintf(stderr, "Cannot connect to vacuum at %s\n", ip);
            return -1;
        }
        roborock_save(dev);
    }
    return 0;
}

static const char *vacuum_state_str(int state)
{
    switch (state) {
        case 1:  return "initializing";
        case 2:  return "sleep";
        case 3:  return "idle";
        case 5:  return "cleaning";
        case 6:  return "returning";
        case 8:  return "charging";
        case 11: return "error";
        case 16: return "shutdown";
        default: return "unknown";
    }
}

static const char *fan_speed_label(int fan)
{
    switch (fan) {
        case 101: return "silent";
        case 102: return "balanced";
        case 103: return "turbo";
        case 104: return "max";
        case 105: return "gentle";
        default:  return "unknown";
    }
}

static int parse_fan_level(const char *s)
{
    if (strcmp(s, "silent")   == 0) return 101;
    if (strcmp(s, "balanced") == 0) return 102;
    if (strcmp(s, "turbo")    == 0) return 103;
    if (strcmp(s, "max")      == 0) return 104;
    if (strcmp(s, "gentle")   == 0) return 105;
    int n = atoi(s);
    return (n >= 101 && n <= 105) ? n : -1;
}

static int consumable_pct(int seconds, int threshold_seconds)
{
    int used_pct = seconds * 100 / threshold_seconds;
    return 100 - (used_pct > 100 ? 100 : used_pct);
}

static int cmd_vacuum_status(const char *ip)
{
    RoborockDevice dev = {0};
    if (get_vacuum(ip, &dev) != 0) return -1;

    RoborockStatus st = {0};
    if (roborock_get_status(&dev, &st) != 0) {
        fprintf(stderr, "Failed to get vacuum status from %s\n", ip);
        return -1;
    }

    printf("State:    %s\n", vacuum_state_str(st.state));
    printf("Battery:  %d%%\n", st.battery);
    printf("Fan:      %s\n", fan_speed_label(st.fan_power));
    if (st.in_cleaning) {
        printf("Run time: %d min\n", st.clean_time / 60);
        printf("Area:     %.1f m²\n", st.clean_area / 10000.0);
    }
    if (st.error_code != 0)
        printf("Error:    code %d\n", st.error_code);
    else
        printf("Error:    none\n");
    return 0;
}

static int cmd_vacuum_consumables(const char *ip)
{
    RoborockDevice dev = {0};
    if (get_vacuum(ip, &dev) != 0) return -1;

    RoborockConsumables c = {0};
    if (roborock_get_consumable(&dev, &c) != 0) {
        fprintf(stderr, "Failed to get consumables from %s\n", ip);
        return -1;
    }

    printf("Main brush:  %d%% (%d h used)\n",
           consumable_pct(c.main_brush, 1080000), c.main_brush / 3600);
    printf("Side brush:  %d%% (%d h used)\n",
           consumable_pct(c.side_brush, 720000),  c.side_brush / 3600);
    printf("Filter:      %d%% (%d h used)\n",
           consumable_pct(c.filter,     540000),  c.filter     / 3600);
    printf("Sensors:     %d%% (%d h used)\n",
           consumable_pct(c.sensor,     108000),  c.sensor     / 3600);
    return 0;
}

/* ── TV (Samsung) commands ───────────────────────────────────────────── */

static const char *const KNOWN_TV_KEYS[] = {
    "KEY_POWER","KEY_POWEROFF","KEY_POWERON",
    "KEY_VOLUP","KEY_VOLDOWN","KEY_MUTE",
    "KEY_CHUP","KEY_CHDOWN","KEY_PRECH",
    "KEY_SOURCE","KEY_HDMI","KEY_TV","KEY_AV1","KEY_AV2",
    "KEY_0","KEY_1","KEY_2","KEY_3","KEY_4",
    "KEY_5","KEY_6","KEY_7","KEY_8","KEY_9",
    "KEY_ENTER","KEY_RETURN","KEY_EXIT",
    "KEY_UP","KEY_DOWN","KEY_LEFT","KEY_RIGHT",
    "KEY_MENU","KEY_HOME","KEY_TOOLS","KEY_INFO","KEY_GUIDE",
    "KEY_PLAY","KEY_PAUSE","KEY_STOP","KEY_FF","KEY_REWIND",
    "KEY_RED","KEY_GREEN","KEY_YELLOW","KEY_CYAN",
    "KEY_PIP_ONOFF","KEY_PMODE","KEY_ASPECT",
    NULL
};

static int is_known_key(const char *key)
{
    for (int i = 0; KNOWN_TV_KEYS[i]; i++)
        if (strcmp(KNOWN_TV_KEYS[i], key) == 0) return 1;
    return 0;
}

static int cmd_tv_probe(const char *ip)
{
    int r = samsung_tv_probe(ip);
    if (r > 0)       printf("%s: online\n",  ip);
    else if (r == 0) printf("%s: offline\n", ip);
    else             fprintf(stderr, "%s: probe error\n", ip);
    return (r >= 0) ? 0 : -1;
}

static int cmd_tv_key(const char *ip, const char *key)
{
    if (!is_known_key(key))
        fprintf(stderr, "Warning: '%s' not in known key list\n", key);
    if (samsung_tv_send_key(ip, key) != 0) {
        fprintf(stderr, "%s: failed to send %s\n", ip, key);
        return -1;
    }
    printf("%s: sent %s\n", ip, key);
    return 0;
}

/* ── Printer (Brother) commands ──────────────────────────────────────── */

static const char *printer_state_str(int state)
{
    switch (state) {
        case 3: return "idle";
        case 4: return "printing";
        case 5: return "stopped";
        default: return "unknown";
    }
}

static int cmd_printer_probe(const char *ip)
{
    char model[64] = {0};
    int r = brother_probe(ip, model, sizeof(model));
    if (r > 0)       printf("%s: online — %s\n", ip, model[0] ? model : "Brother printer");
    else if (r == 0) printf("%s: offline (unreachable)\n", ip);
    else             fprintf(stderr, "%s: probe error\n", ip);
    return (r >= 0) ? 0 : -1;
}

static int cmd_printer_status(const char *ip)
{
    BrotherStatus st = {0};
    if (brother_get_status(ip, &st) != 0) {
        fprintf(stderr, "%s: failed to query printer status\n", ip);
        return -1;
    }
    printf("State:  %s\n", printer_state_str(st.state));
    if (st.toner_pct >= 0)
        printf("Toner:  %d%%%s\n", st.toner_pct,
               st.toner_low > 0 ? " [LOW]" : "");
    else
        printf("Toner:  n/a\n");
    printf("Pages:  %d\n", st.page_count >= 0 ? st.page_count : 0);
    return 0;
}

static int cmd_printer_toner(const char *ip, int raw_mode)
{
    BrotherStatus st = {0};
    if (brother_get_status(ip, &st) != 0) {
        fprintf(stderr, "%s: failed to query printer\n", ip);
        return -1;
    }
    if (st.toner_pct < 0) {
        if (!raw_mode)
            fprintf(stderr, "%s: toner unavailable\n", ip);
        return -1;
    }
    if (raw_mode)
        printf("%d\n", st.toner_pct);
    else
        printf("Toner: %d%%\n", st.toner_pct);
    return 0;
}

static int cmd_printer_pages(const char *ip)
{
    BrotherStatus st = {0};
    if (brother_get_status(ip, &st) != 0) {
        fprintf(stderr, "%s: failed to query printer\n", ip);
        return -1;
    }
    if (st.page_count < 0) {
        fprintf(stderr, "%s: page count unavailable\n", ip);
        return -1;
    }
    printf("Pages: %d\n", st.page_count);
    return 0;
}

static int cmd_printer_consumables(const char *ip)
{
    BrotherConsumables c = {0};
    if (brother_get_consumables(ip, &c) != 0) {
        fprintf(stderr, "%s: failed to query consumables\n", ip);
        return -1;
    }
    if (c.toner_pct >= 0)
        printf("Toner:       %d%%%s\n", c.toner_pct, c.toner_pct < 20 ? " [!]" : "");
    else
        printf("Toner:       n/a\n");
    if (c.drum_pct >= 0)
        printf("Drum:        %d%%%s\n", c.drum_pct, c.drum_pct < 20 ? " [!]" : "");
    else
        printf("Drum:        n/a\n");
    if (c.pages_until_maint >= 0)
        printf("Maintenance: %d pages remaining\n", c.pages_until_maint);
    else
        printf("Maintenance: n/a\n");
    return 0;
}

/* ── Usage ───────────────────────────────────────────────────────────── */

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <command> [options]\n"
        "\n"
        "Options:\n"
        "  -v, --verbose    Enable verbose/debug output\n"
        "  --version        Print version and exit\n"
        "  --help           Show this help and exit\n"
        "\n"
        "General:\n"
        "  list                      Scan for Gree AC units on the local network\n"
        "  version\n"
        "  help\n"
        "\n"
        "  Note: vacuum, tv and printer have no network discovery — supply the IP\n"
        "  directly (find it in your router's DHCP table or use 'nmap -sn <net>').\n"
        "\n"
        "AC (Gree) — ac <sub> <ip> [params]:\n"
        "  ac list                   Broadcast-scan for Gree AC units (auto-discover)\n"
        "  ac bind <ip>              Pair with AC and save device key\n"
        "  ac status <ip...>         Show status\n"
        "  ac temp <ip...>           Room temperature (integer °C)\n"
        "  ac on  <ip...>            Power on\n"
        "  ac off <ip...>            Power off\n"
        "  ac set <ip> key=val ...   Set parameters:\n"
        "    power=on|off  temp=<16-30>  mode=auto|cool|dry|fan|heat\n"
        "    fan=auto|low|med-low|medium|med-high|high\n"
        "    sleep=on|off  quiet=on|off  turbo=on|off\n"
        "    swing_v=on|off  swing_h=off|full|1-6\n"
        "    xfan=on|off  health=on|off  air=on|off\n"
        "    antifrost=on|off  lights=on|off  unit=c|f\n"
        "\n"
        "Vacuum (Roborock) — vacuum <sub> <ip>  [IP required, no auto-discover]:\n"
        "  vacuum status <ip>        Show status\n"
        "  vacuum consumables <ip>   Show consumable life\n"
        "  vacuum start|stop|pause|dock|spot|find <ip>\n"
        "  vacuum fan <ip> silent|balanced|turbo|max|gentle\n"
        "  vacuum reset-brush|reset-side-brush|reset-filter|reset-sensor <ip>\n"
        "\n"
        "TV (Samsung) — tv <sub> <ip>  [IP required, no auto-discover]:\n"
        "  tv probe <ip>             Check if TV is on\n"
        "  tv key <ip> KEY1 [KEY2..] Send one or more key codes (300ms gap)\n"
        "  tv volup|voldown <ip> [N] Send N times (default 1, 200ms gap)\n"
        "  tv mute|power|off <ip>\n"
        "  tv source|hdmi <ip>\n"
        "  tv channel <ip> up|down\n"
        "\n"
        "Printer (Brother) — printer <sub> <ip>  [IP required, no auto-discover]:\n"
        "  printer probe <ip>            Check reachability\n"
        "  printer status <ip> [--quiet] State, toner, page count\n"
        "  printer toner <ip> [--raw]    Toner level (--raw = integer only)\n"
        "  printer pages <ip>            Page count\n"
        "  printer consumables <ip>      Toner, drum, maintenance\n"
        "\n"
        "Configuration (~/.config/home-appliances/config.ini):\n"
        "  network=192.168.1.0/24        Subnet used for AC broadcast discovery\n"
        "  iface=wlan0                   Network interface (informational)\n"
        "  discovery_timeout_ms=5000     UDP scan timeout\n",
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

    int log_level = LOG_INFO;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0 ||
            strcmp(argv[i], "--debug")   == 0 || strcmp(argv[i], "-d") == 0) {
            log_level = LOG_DEBUG;
            /* Remove this flag by shifting argv */
            for (int j = i; j < argc - 1; j++) argv[j] = argv[j+1];
            argc--;
            i--;
        }
    }
    logger_init(NULL, log_level);

    Config cfg = {0};
    if (config_load(&cfg) != 0) {
        fprintf(stderr, "Failed to load configuration.\n");
        logger_close();
        return 1;
    }

    int ret = 0;

    /* ── list ── */
    if (strcmp(argv[1], "list") == 0) {
        ret = cmd_list(&cfg);

    /* ── ac ── */
    } else if (strcmp(argv[1], "ac") == 0) {
        if (argc < 3) { print_usage(argv[0]); ret = 1; goto done; }
        const char *sub = argv[2];

        if (strcmp(sub, "list") == 0) {
            ret = cmd_ac_list(&cfg);

        } else if (strcmp(sub, "bind") == 0) {
            if (argc < 4) { fprintf(stderr, "ac bind: missing <ip>\n"); ret = 1; }
            else          ret = cmd_ac_bind(argv[3], &cfg);

        } else if (strcmp(sub, "status") == 0 || strcmp(sub, "temp") == 0
                   || strcmp(sub, "on") == 0  || strcmp(sub, "off") == 0) {
            /* Multi-IP: ac status ip1 ip2 ... */
            const char *ips[32];
            int dummy;
            int n = collect_ips(argv + 3, argc - 3, ips, 32, &dummy);
            if (n == 0) { fprintf(stderr, "ac %s: missing <ip>\n", sub); ret = 1; goto done; }
            for (int i = 0; i < n; i++) {
                if (n > 1) printf("[%s]\n", ips[i]);
                int r = 0;
                if (strcmp(sub, "status") == 0)
                    r = cmd_ac_status_one(ips[i]);
                else if (strcmp(sub, "temp") == 0)
                    r = cmd_ac_temp_one(ips[i]);
                else if (strcmp(sub, "on") == 0)
                    r = cmd_ac_power(ips[i], 1);
                else
                    r = cmd_ac_power(ips[i], 0);
                if (r != 0) ret = 1;
            }

        } else if (strcmp(sub, "set") == 0) {
            if (argc < 5) { fprintf(stderr, "ac set: missing <ip> and params\n"); ret = 1; }
            else          ret = cmd_ac_set(argv[3], argv + 4, argc - 4);

        } else {
            /* Shortcut subcommands with single param: swing-v, xfan, etc. */
            if (argc < 4) { fprintf(stderr, "ac %s: missing <ip>\n", sub); ret = 1; goto done; }
            const char *ip  = argv[3];
            const char *val = argc > 4 ? argv[4] : "";
            int on = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0);
            int off= (strcmp(val, "off") == 0|| strcmp(val, "0") == 0);

            if (strcmp(sub, "swing-v") == 0) {
                if (!on && !off) { fprintf(stderr, "swing-v: on|off required\n"); ret = 1; }
                else ret = ac_set_single(ip, "SwUpDn", on ? 1 : 0);
            } else if (strcmp(sub, "xfan") == 0) {
                if (!on && !off) { fprintf(stderr, "xfan: on|off required\n"); ret = 1; }
                else {
                    if (on) {
                        GreeDeviceList saved2 = {0};
                        if (gree_client_load_bound(&saved2) == 0) {
                            GreeDevice *dev2 = gree_find_device(&saved2, ip);
                            if (dev2) {
                                GreeStatus st2 = {0};
                                if (gree_client_get_status(dev2, &st2) == 0
                                    && st2.mode != GREE_MODE_COOL
                                    && st2.mode != GREE_MODE_DRY) {
                                    static const char *m[] =
                                        {"auto","cool","dry","fan","heat"};
                                    const char *ms = (st2.mode >= 0 && st2.mode <= 4)
                                                     ? m[st2.mode] : "unknown";
                                    fprintf(stderr,
                                        "Warning: X-Fan is only effective in"
                                        " cool/dry mode (current: %s)\n", ms);
                                }
                            }
                            gree_device_list_free(&saved2);
                        }
                    }
                    ret = ac_set_single(ip, "Blo", on ? 1 : 0);
                }
            } else if (strcmp(sub, "health") == 0) {
                if (!on && !off) { fprintf(stderr, "health: on|off required\n"); ret = 1; }
                else ret = ac_set_single(ip, "Health", on ? 1 : 0);
            } else if (strcmp(sub, "air") == 0) {
                if (!on && !off) { fprintf(stderr, "air: on|off required\n"); ret = 1; }
                else ret = ac_set_single(ip, "Air", on ? 1 : 0);
            } else if (strcmp(sub, "antifrost") == 0) {
                if (!on && !off) { fprintf(stderr, "antifrost: on|off required\n"); ret = 1; }
                else ret = ac_set_single(ip, "StHt", on ? 1 : 0);
            } else if (strcmp(sub, "swing-h") == 0) {
                int h = 0;
                if (strcmp(val, "off") == 0) h = 0;
                else if (strcmp(val, "full") == 0) h = 1;
                else { h = atoi(val); if (h < 0 || h > 6) { fprintf(stderr, "swing-h: off|full|0-6\n"); ret = 1; goto done; } }
                ret = ac_set_single(ip, "SwingLfRig", h);
            } else if (strcmp(sub, "unit") == 0) {
                int u = -1;
                if (strcmp(val,"c")==0||strcmp(val,"celsius")==0)    u = 0;
                if (strcmp(val,"f")==0||strcmp(val,"fahrenheit")==0) u = 1;
                if (u < 0) { fprintf(stderr, "unit: c|f required\n"); ret = 1; }
                else ret = ac_set_single(ip, "TemUn", u);
            } else {
                fprintf(stderr, "Unknown ac subcommand: %s\n", sub);
                print_usage(argv[0]);
                ret = 1;
            }
        }

    /* ── vacuum ── */
    } else if (strcmp(argv[1], "vacuum") == 0) {
        if (argc < 4) { print_usage(argv[0]); ret = 1; goto done; }
        const char *sub = argv[2];
        const char *ip  = argv[3];

        RoborockDevice dev = {0};

        if (strcmp(sub, "status") == 0) {
            ret = cmd_vacuum_status(ip);
        } else if (strcmp(sub, "consumables") == 0) {
            ret = cmd_vacuum_consumables(ip);
        } else if (strcmp(sub, "start") == 0) {
            if (get_vacuum(ip, &dev) == 0) {
                ret = roborock_start(&dev);
                if (ret == 0) printf("Cleaning started.\n");
            } else ret = -1;
        } else if (strcmp(sub, "stop") == 0) {
            if (get_vacuum(ip, &dev) == 0) {
                ret = roborock_stop(&dev);
                if (ret == 0) printf("Cleaning stopped.\n");
            } else ret = -1;
        } else if (strcmp(sub, "pause") == 0) {
            if (get_vacuum(ip, &dev) == 0) {
                ret = roborock_pause(&dev);
                if (ret == 0) printf("Cleaning paused.\n");
            } else ret = -1;
        } else if (strcmp(sub, "dock") == 0) {
            if (get_vacuum(ip, &dev) == 0) {
                ret = roborock_dock(&dev);
                if (ret == 0) {
                    printf("Returning to dock...\n");
                    sleep(3);
                    RoborockStatus dock_st = {0};
                    if (roborock_get_status(&dev, &dock_st) == 0)
                        printf("State: %s\n", vacuum_state_str(dock_st.state));
                    else
                        printf("Command sent.\n");
                }
            } else ret = -1;
        } else if (strcmp(sub, "spot") == 0) {
            if (get_vacuum(ip, &dev) == 0) {
                ret = roborock_spot(&dev);
                if (ret == 0) printf("Spot cleaning started.\n");
            } else ret = -1;
        } else if (strcmp(sub, "find") == 0) {
            if (get_vacuum(ip, &dev) == 0) {
                ret = roborock_find(&dev);
                if (ret == 0) printf("Locating robot...\n");
            } else ret = -1;
        } else if (strcmp(sub, "fan") == 0) {
            if (argc < 5) { fprintf(stderr, "vacuum fan: missing level\n"); ret = 1; goto done; }
            int level = parse_fan_level(argv[4]);
            if (level < 0) { fprintf(stderr, "vacuum fan: silent|balanced|turbo|max|gentle\n"); ret = 1; goto done; }
            if (get_vacuum(ip, &dev) == 0) {
                ret = roborock_set_fan(&dev, level);
                if (ret == 0) printf("Fan speed set.\n");
            } else ret = -1;
        } else if (strcmp(sub, "reset-brush") == 0) {
            if (get_vacuum(ip, &dev) == 0) {
                ret = roborock_reset_consumable(&dev, "main_brush_work_time");
                if (ret == 0) printf("Main brush reset.\n");
            } else ret = -1;
        } else if (strcmp(sub, "reset-side-brush") == 0) {
            if (get_vacuum(ip, &dev) == 0) {
                ret = roborock_reset_consumable(&dev, "side_brush_work_time");
                if (ret == 0) printf("Side brush reset.\n");
            } else ret = -1;
        } else if (strcmp(sub, "reset-filter") == 0) {
            if (get_vacuum(ip, &dev) == 0) {
                ret = roborock_reset_consumable(&dev, "filter_work_time");
                if (ret == 0) printf("Filter reset.\n");
            } else ret = -1;
        } else if (strcmp(sub, "reset-sensor") == 0) {
            if (get_vacuum(ip, &dev) == 0) {
                ret = roborock_reset_consumable(&dev, "sensor_dirty_time");
                if (ret == 0) printf("Sensor reset.\n");
            } else ret = -1;
        } else {
            fprintf(stderr, "Unknown vacuum subcommand: %s\n", sub);
            print_usage(argv[0]);
            ret = 1;
        }

        if (ret != 0 && ret != 1)
            fprintf(stderr, "vacuum %s %s: failed\n", sub, ip);

    /* ── tv ── */
    } else if (strcmp(argv[1], "tv") == 0) {
        if (argc < 4) { print_usage(argv[0]); ret = 1; goto done; }
        const char *sub = argv[2];
        const char *ip  = argv[3];

        if (strcmp(sub, "probe") == 0) {
            ret = cmd_tv_probe(ip);
        } else if (strcmp(sub, "key") == 0) {
            if (argc < 5) { fprintf(stderr, "tv key: missing KEY_*\n"); ret = 1; }
            else {
                int nk = argc - 4;
                if (nk > 64) nk = 64;
                const char *keys[65];
                for (int i = 0; i < nk; i++) {
                    keys[i] = argv[4 + i];
                    if (!is_known_key(keys[i]))
                        fprintf(stderr, "Warning: '%s' not in known key list\n",
                                keys[i]);
                }
                keys[nk] = NULL;
                if (nk == 1) {
                    ret = cmd_tv_key(ip, keys[0]);
                } else {
                    if (samsung_tv_send_keys(ip, keys, 300) != 0) {
                        fprintf(stderr, "%s: failed to send keys\n", ip);
                        ret = 1;
                    } else {
                        for (int i = 0; i < nk; i++)
                            printf("%s: sent %s\n", ip, keys[i]);
                    }
                }
            }
        } else if (strcmp(sub, "volup") == 0 || strcmp(sub, "voldown") == 0) {
            const char *vkey = (strcmp(sub, "volup") == 0) ? "KEY_VOLUP" : "KEY_VOLDOWN";
            int vn = (argc > 4) ? atoi(argv[4]) : 1;
            if (vn <= 0 || vn > 64) vn = 1;
            if (vn == 1) {
                ret = cmd_tv_key(ip, vkey);
            } else {
                const char *vkeys[65];
                for (int i = 0; i < vn; i++) vkeys[i] = vkey;
                vkeys[vn] = NULL;
                if (samsung_tv_send_keys(ip, vkeys, 200) != 0) {
                    fprintf(stderr, "%s: failed to send %s x%d\n", ip, vkey, vn);
                    ret = 1;
                } else {
                    printf("%s: sent %s x%d\n", ip, vkey, vn);
                }
            }
        } else if (strcmp(sub, "mute") == 0) { ret = cmd_tv_key(ip, "KEY_MUTE"); }
        else if (strcmp(sub, "power")   == 0) { ret = cmd_tv_key(ip, "KEY_POWER");   }
        else if (strcmp(sub, "off")     == 0) { ret = cmd_tv_key(ip, "KEY_POWEROFF");}
        else if (strcmp(sub, "source")  == 0) { ret = cmd_tv_key(ip, "KEY_SOURCE");  }
        else if (strcmp(sub, "hdmi")    == 0) { ret = cmd_tv_key(ip, "KEY_HDMI");    }
        else if (strcmp(sub, "channel") == 0) {
            if (argc < 5) { fprintf(stderr, "tv channel: up|down required\n"); ret = 1; }
            else if (strcmp(argv[4], "up") == 0)   ret = cmd_tv_key(ip, "KEY_CHUP");
            else if (strcmp(argv[4], "down") == 0)  ret = cmd_tv_key(ip, "KEY_CHDOWN");
            else { fprintf(stderr, "tv channel: up|down\n"); ret = 1; }
        } else {
            fprintf(stderr, "Unknown tv subcommand: %s\n", sub);
            print_usage(argv[0]);
            ret = 1;
        }

    /* ── printer ── */
    } else if (strcmp(argv[1], "printer") == 0) {
        if (argc < 4) { print_usage(argv[0]); ret = 1; goto done; }
        const char *sub = argv[2];
        const char *ip  = argv[3];

        if (strcmp(sub, "probe") == 0) {
            ret = cmd_printer_probe(ip);
        } else if (strcmp(sub, "status") == 0) {
            int quiet = (argc > 4 && strcmp(argv[4], "--quiet") == 0);
            if (quiet) {
                BrotherStatus qst = {0};
                int ec;
                if (brother_get_status(ip, &qst) != 0)
                    ec = 1;
                else if (qst.state == 3 && qst.toner_low <= 0)
                    ec = 0;
                else if (qst.state == 5 || qst.toner_low >= 1)
                    ec = 2;
                else
                    ec = 1;
                logger_close();
                return ec;
            }
            ret = cmd_printer_status(ip);
        } else if (strcmp(sub, "toner") == 0) {
            int raw = (argc > 4 && strcmp(argv[4], "--raw") == 0);
            ret = cmd_printer_toner(ip, raw);
        } else if (strcmp(sub, "pages") == 0) {
            ret = cmd_printer_pages(ip);
        } else if (strcmp(sub, "consumables") == 0) {
            ret = cmd_printer_consumables(ip);
        } else {
            fprintf(stderr, "Unknown printer subcommand: %s\n", sub);
            print_usage(argv[0]);
            ret = 1;
        }

    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        print_usage(argv[0]);
        ret = 1;
    }

done:
    logger_close();
    return ret == 0 ? 0 : 1;
}
