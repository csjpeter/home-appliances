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

#endif /* BROTHER_CLIENT_H */
