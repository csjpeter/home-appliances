#include "appliance_service.h"
#include "../core/logger.h"
#include "../infrastructure/roborock_client.h"
#include "../infrastructure/samsung_tv_client.h"
#include "../infrastructure/brother_client.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *type_name(ApplianceType type)
{
    switch (type) {
        case APPLIANCE_TYPE_GREE_AC:         return "Gree AC";
        case APPLIANCE_TYPE_ROBOROCK:        return "robot vacuum";
        case APPLIANCE_TYPE_SAMSUNG_TV:      return "smart TV";
        case APPLIANCE_TYPE_BROTHER_PRINTER: return "printer";
    }
    return "Unknown";
}

int appliance_service_discover(const char *broadcast, int timeout_ms,
                               ApplianceList *out)
{
    out->items = NULL;
    out->count = 0;

    GreeDeviceList gree = {0};
    if (gree_client_scan(broadcast, timeout_ms, &gree) != 0)
        LOG_WARN_MSG("Gree scan failed");

    int total = gree.count;
    if (total == 0) {
        gree_device_list_free(&gree);
        return 0;
    }

    out->items = malloc(total * sizeof(Appliance));
    if (!out->items) {
        gree_device_list_free(&gree);
        return -1;
    }

    for (int i = 0; i < gree.count; i++) {
        Appliance *a = &out->items[out->count++];
        memset(a, 0, sizeof(*a));
        a->type = APPLIANCE_TYPE_GREE_AC;
        snprintf(a->name, sizeof(a->name), "Gree AC (%s)", gree.devices[i].mac);
        snprintf(a->ip, sizeof(a->ip), "%s", gree.devices[i].ip);
    }

    gree_device_list_free(&gree);
    return 0;
}

void appliance_service_print(const ApplianceList *list)
{
    if (list->count == 0) {
        printf("No appliances found.\n");
        return;
    }
    printf("%-4s %-24s %-20s %s\n", "#", "Type", "IP", "Name");
    printf("%-4s %-24s %-20s %s\n", "---", "------------------------",
           "--------------------", "----");
    for (int i = 0; i < list->count; i++) {
        const Appliance *a = &list->items[i];
        printf("%-4d %-24s %-20s %s\n",
               i + 1, type_name(a->type), a->ip, a->name);
    }
}

void appliance_list_free(ApplianceList *list)
{
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

int appliance_service_list_known(ApplianceList *out)
{
    out->items = NULL;
    out->count = 0;

    GreeDeviceList gree = {0};
    gree_client_load_bound(&gree);

    RoborockDevice *vacuums = NULL;
    int vcount = 0;
    roborock_load_all(&vacuums, &vcount);

    SamsungTvDeviceList tv = {0};
    samsung_tv_load(&tv);

    BrotherDeviceList printer = {0};
    brother_load(&printer);

    int total = gree.count + vcount + tv.count + printer.count;
    if (total == 0) {
        gree_device_list_free(&gree);
        free(vacuums);
        samsung_tv_device_list_free(&tv);
        brother_device_list_free(&printer);
        return 0;
    }

    out->items = malloc((size_t)total * sizeof(Appliance));
    if (!out->items) {
        gree_device_list_free(&gree);
        free(vacuums);
        samsung_tv_device_list_free(&tv);
        brother_device_list_free(&printer);
        return -1;
    }

    for (int i = 0; i < gree.count; i++) {
        Appliance *a = &out->items[out->count++];
        memset(a, 0, sizeof(*a));
        a->type = APPLIANCE_TYPE_GREE_AC;
        snprintf(a->name, sizeof(a->name), "Gree AC (%s)", gree.devices[i].mac);
        snprintf(a->ip, sizeof(a->ip), "%s", gree.devices[i].ip);
    }

    for (int i = 0; i < vcount; i++) {
        Appliance *a = &out->items[out->count++];
        memset(a, 0, sizeof(*a));
        a->type = APPLIANCE_TYPE_ROBOROCK;
        snprintf(a->name, sizeof(a->name), "Roborock (%08x)", vacuums[i].device_id);
        snprintf(a->ip, sizeof(a->ip), "%s", vacuums[i].ip);
    }

    for (int i = 0; i < tv.count; i++) {
        Appliance *a = &out->items[out->count++];
        memset(a, 0, sizeof(*a));
        a->type = APPLIANCE_TYPE_SAMSUNG_TV;
        if (tv.devices[i].model[0] != '\0')
            snprintf(a->name, sizeof(a->name), "%s", tv.devices[i].model);
        else
            snprintf(a->name, sizeof(a->name), "Samsung TV");
        snprintf(a->ip, sizeof(a->ip), "%s", tv.devices[i].ip);
    }

    for (int i = 0; i < printer.count; i++) {
        Appliance *a = &out->items[out->count++];
        memset(a, 0, sizeof(*a));
        a->type = APPLIANCE_TYPE_BROTHER_PRINTER;
        if (printer.devices[i].model[0] != '\0')
            snprintf(a->name, sizeof(a->name), "%s", printer.devices[i].model);
        else
            snprintf(a->name, sizeof(a->name), "Brother printer");
        snprintf(a->ip, sizeof(a->ip), "%s", printer.devices[i].ip);
    }

    gree_device_list_free(&gree);
    free(vacuums);
    samsung_tv_device_list_free(&tv);
    brother_device_list_free(&printer);
    return 0;
}

/* ── Parallel discovery worker types ────────────────────────────────── */

typedef struct { const char *broadcast; int timeout_ms; GreeDeviceList result; int rc; } GreeWorker;
typedef struct { const char *cidr; RoborockDevice *result; int count; int rc; } RoborockWorker;
typedef struct { const char *cidr; SamsungTvDeviceList result; int rc; } TvWorker;
typedef struct { const char *cidr; BrotherDeviceList result; int rc; } PrinterWorker;

static void *gree_worker_fn(void *arg)
{
    GreeWorker *w = arg;
    w->rc = gree_client_scan(w->broadcast, w->timeout_ms, &w->result);
    return NULL;
}

static void *roborock_worker_fn(void *arg)
{
    RoborockWorker *w = arg;
    w->rc = roborock_scan(w->cidr, &w->result, &w->count);
    return NULL;
}

static void *tv_worker_fn(void *arg)
{
    TvWorker *w = arg;
    w->rc = samsung_tv_scan(w->cidr, &w->result);
    return NULL;
}

static void *printer_worker_fn(void *arg)
{
    PrinterWorker *w = arg;
    w->rc = brother_scan(w->cidr, &w->result);
    return NULL;
}

int appliance_service_discover_all(const char *cidr, const char *broadcast,
                                   int timeout_ms, ApplianceList *out)
{
    out->items = NULL;
    out->count = 0;

    GreeWorker    gw = {.broadcast = broadcast, .timeout_ms = timeout_ms};
    RoborockWorker rw = {.cidr = cidr};
    TvWorker       tw = {.cidr = cidr};
    PrinterWorker  pw = {.cidr = cidr};

    pthread_t tg, tr, tt, tp;
    pthread_create(&tg, NULL, gree_worker_fn,     &gw);
    pthread_create(&tr, NULL, roborock_worker_fn, &rw);
    pthread_create(&tt, NULL, tv_worker_fn,       &tw);
    pthread_create(&tp, NULL, printer_worker_fn,  &pw);

    pthread_join(tg, NULL);
    pthread_join(tr, NULL);
    pthread_join(tt, NULL);
    pthread_join(tp, NULL);

    /* Merge Gree results into the saved store */
    GreeDeviceList saved_gree = {0};
    gree_client_load_bound(&saved_gree);
    gree_client_merge_bound(&gw.result, &saved_gree);
    gree_client_save_bound(&gw.result);
    gree_device_list_free(&saved_gree);

    /* Save Roborock results */
    for (int i = 0; i < rw.count; i++)
        roborock_save(&rw.result[i]);

    /* Merge TV results into the saved store */
    SamsungTvDeviceList existing_tv = {0};
    samsung_tv_load(&existing_tv);
    for (int i = 0; i < tw.result.count; i++) {
        int found = 0;
        for (int j = 0; j < existing_tv.count; j++) {
            if (strcmp(tw.result.devices[i].ip, existing_tv.devices[j].ip) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            SamsungTvDevice *tmp = realloc(existing_tv.devices,
                (size_t)(existing_tv.count + 1) * sizeof(SamsungTvDevice));
            if (tmp) {
                existing_tv.devices = tmp;
                existing_tv.devices[existing_tv.count++] = tw.result.devices[i];
            }
        }
    }
    samsung_tv_save(&existing_tv);
    samsung_tv_device_list_free(&existing_tv);

    /* Merge Printer results into the saved store */
    BrotherDeviceList existing_pr = {0};
    brother_load(&existing_pr);
    for (int i = 0; i < pw.result.count; i++) {
        int found = 0;
        for (int j = 0; j < existing_pr.count; j++) {
            if (strcmp(pw.result.devices[i].ip, existing_pr.devices[j].ip) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            BrotherDevice *tmp = realloc(existing_pr.devices,
                (size_t)(existing_pr.count + 1) * sizeof(BrotherDevice));
            if (tmp) {
                existing_pr.devices = tmp;
                existing_pr.devices[existing_pr.count++] = pw.result.devices[i];
            }
        }
    }
    brother_save(&existing_pr);
    brother_device_list_free(&existing_pr);

    /* Build output ApplianceList from scan results */
    int total = gw.result.count + rw.count + tw.result.count + pw.result.count;
    if (total > 0) {
        out->items = malloc((size_t)total * sizeof(Appliance));
        if (!out->items) {
            gree_device_list_free(&gw.result);
            roborock_scan_free(rw.result);
            samsung_tv_device_list_free(&tw.result);
            brother_device_list_free(&pw.result);
            return -1;
        }

        for (int i = 0; i < gw.result.count; i++) {
            Appliance *a = &out->items[out->count++];
            memset(a, 0, sizeof(*a));
            a->type = APPLIANCE_TYPE_GREE_AC;
            snprintf(a->name, sizeof(a->name), "Gree AC (%s)", gw.result.devices[i].mac);
            snprintf(a->ip, sizeof(a->ip), "%s", gw.result.devices[i].ip);
        }

        for (int i = 0; i < rw.count; i++) {
            Appliance *a = &out->items[out->count++];
            memset(a, 0, sizeof(*a));
            a->type = APPLIANCE_TYPE_ROBOROCK;
            snprintf(a->name, sizeof(a->name), "Roborock (%08x)", rw.result[i].device_id);
            snprintf(a->ip, sizeof(a->ip), "%s", rw.result[i].ip);
        }

        for (int i = 0; i < tw.result.count; i++) {
            Appliance *a = &out->items[out->count++];
            memset(a, 0, sizeof(*a));
            a->type = APPLIANCE_TYPE_SAMSUNG_TV;
            if (tw.result.devices[i].model[0] != '\0')
                snprintf(a->name, sizeof(a->name), "%s", tw.result.devices[i].model);
            else
                snprintf(a->name, sizeof(a->name), "Samsung TV");
            snprintf(a->ip, sizeof(a->ip), "%s", tw.result.devices[i].ip);
        }

        for (int i = 0; i < pw.result.count; i++) {
            Appliance *a = &out->items[out->count++];
            memset(a, 0, sizeof(*a));
            a->type = APPLIANCE_TYPE_BROTHER_PRINTER;
            if (pw.result.devices[i].model[0] != '\0')
                snprintf(a->name, sizeof(a->name), "%s", pw.result.devices[i].model);
            else
                snprintf(a->name, sizeof(a->name), "Brother printer");
            snprintf(a->ip, sizeof(a->ip), "%s", pw.result.devices[i].ip);
        }
    }

    gree_device_list_free(&gw.result);
    roborock_scan_free(rw.result);
    samsung_tv_device_list_free(&tw.result);
    brother_device_list_free(&pw.result);
    return 0;
}
