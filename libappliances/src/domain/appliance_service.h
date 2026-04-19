#ifndef APPLIANCE_SERVICE_H
#define APPLIANCE_SERVICE_H

/**
 * @file appliance_service.h
 * @brief Otthoni készülékek egységes vezérlési felülete (domain réteg).
 */

#include "../infrastructure/gree_client.h"

typedef enum {
    APPLIANCE_TYPE_GREE_AC,
    APPLIANCE_TYPE_ROBOROCK,
    APPLIANCE_TYPE_SAMSUNG_TV,
    APPLIANCE_TYPE_BROTHER_PRINTER,
} ApplianceType;

typedef struct {
    ApplianceType type;
    char name[64];
    char ip[16];
} Appliance;

typedef struct {
    Appliance *items;
    int count;
} ApplianceList;

/**
 * @brief Hálózati scan — megkeresi az összes ismert készüléket.
 * @param broadcast Broadcast cím.
 * @param timeout_ms Scan timeout milliszekundumban.
 * @param out Kimeneti lista (hívónak kell felszabadítani: appliance_list_free).
 * @return 0 siker, -1 hiba esetén.
 */
int appliance_service_discover(const char *broadcast, int timeout_ms,
                               ApplianceList *out);

/**
 * @brief Készüléklista kiírása stdout-ra.
 */
void appliance_service_print(const ApplianceList *list);

/**
 * @brief Készüléklista memóriájának felszabadítása.
 */
void appliance_list_free(ApplianceList *list);

/**
 * @brief Read all known devices from store files without live scanning.
 *        Combines gree_devices, roborock_devices, tv_devices, printer_devices.
 * @param out Caller must free with appliance_list_free().
 * @return 0 on success, -1 on allocation error.
 */
int appliance_service_list_known(ApplianceList *out);

/**
 * @brief Scan all device types in parallel, merge results into store files,
 *        and return the unified list of found devices.
 * @param cidr      CIDR subnet for unicast probing, e.g. "192.168.1.0/24".
 * @param broadcast Broadcast address for Gree AC, e.g. "192.168.1.255".
 * @param timeout_ms Timeout for Gree broadcast scan (ms).
 * @param out       Caller must free with appliance_list_free().
 * @return 0 on success (empty list is OK), -1 on allocation error.
 */
int appliance_service_discover_all(const char *cidr, const char *broadcast,
                                   int timeout_ms, ApplianceList *out);

#endif /* APPLIANCE_SERVICE_H */
