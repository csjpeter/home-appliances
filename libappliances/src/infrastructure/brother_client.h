#ifndef BROTHER_CLIENT_H
#define BROTHER_CLIENT_H

#include <stddef.h>

#ifndef BROTHER_SNMP_PORT
#define BROTHER_SNMP_PORT 161
#endif

typedef struct
{
    int  state;       /* 3=idle, 4=printing, 5=stopped, 0=unknown */
    int  toner_pct;   /* 0-100, -1 if unavailable */
    int  toner_low;   /* 0=ok, 1=low, 2=absent, 3=empty, -1=unknown */
    int  page_count;  /* total pages, -1 if unavailable */
    char model[64];   /* sysDescr string, empty if unavailable */
} BrotherStatus;

typedef struct
{
    int toner_pct;          /* 0-100, -1=unavailable */
    int drum_pct;           /* 0-100, -1=unavailable */
    int pages_until_maint;  /* pages remaining, -1=unavailable */
} BrotherConsumables;

/**
 * @brief Probe printer reachability via SNMP sysDescr.
 * @param ip        Printer IPv4 address.
 * @param model     Output buffer for model string, or NULL.
 * @param model_len Size of model buffer.
 * @return 1 if reachable, 0 if unreachable, -1 on socket error.
 */
int brother_probe(const char *ip, char *model, size_t model_len);

/**
 * @brief Query full printer status via SNMP.
 * @param ip  Printer IPv4 address.
 * @param out Output status struct.
 * @return 0 on success, -1 on error.
 */
int brother_get_status(const char *ip, BrotherStatus *out);

/**
 * @brief Query consumable levels via Brother proprietary SNMP OIDs.
 *        Falls back to -1 for drum/maintenance if OIDs not supported by model.
 * @param ip  Printer IPv4 address.
 * @param out Output consumables struct.
 * @return 0 on success, -1 on error.
 */
int brother_get_consumables(const char *ip, BrotherConsumables *out);

#define PRINTER_MODEL_LEN 64

/** Known Brother printer entry. */
typedef struct
{
    char ip[16];
    char model[PRINTER_MODEL_LEN];
} BrotherDevice;

/** List of known Brother printers (from printer_devices store). */
typedef struct
{
    BrotherDevice *devices;
    int            count;
} BrotherDeviceList;

/**
 * @brief Load known printers from ~/.config/home-appliances/printer_devices.
 *        Missing file is not an error.
 * @return 0 on success, -1 on I/O error.
 */
int brother_load(BrotherDeviceList *out);

/**
 * @brief Save printer list to ~/.config/home-appliances/printer_devices (mode 0600).
 * @return 0 on success, -1 on error.
 */
int brother_save(const BrotherDeviceList *list);

/** @brief Free a BrotherDeviceList. */
void brother_device_list_free(BrotherDeviceList *list);

/**
 * @brief Check whether ip appears in the printer_devices store.
 * @return 1 if found, 0 if not found, -1 on I/O error.
 */
int brother_is_known(const char *ip);

/**
 * @brief Scan a subnet for Brother printers via SNMP probe.
 *        Results are NOT saved — caller must call brother_save().
 * @param cidr CIDR subnet, e.g. "192.168.1.0/24".
 * @param out  Output list; caller must free with brother_device_list_free().
 * @return 0 on success (empty list OK), -1 on fatal error.
 */
int brother_scan(const char *cidr, BrotherDeviceList *out);

#endif /* BROTHER_CLIENT_H */
