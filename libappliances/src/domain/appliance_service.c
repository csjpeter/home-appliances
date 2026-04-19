#include "appliance_service.h"
#include "../core/logger.h"

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
