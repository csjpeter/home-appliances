#ifndef SAMSUNG_TV_CLIENT_H
#define SAMSUNG_TV_CLIENT_H

/**
 * @file samsung_tv_client.h
 * @brief Samsung TV local remote control — legacy TCP/55000 protocol.
 *        Supported on pre-2016 Samsung Smart TV models.
 */

#ifndef SAMSUNG_TV_PORT
#define SAMSUNG_TV_PORT 55000
#endif

/**
 * @brief Check whether the TV is reachable on port 55000.
 * @return 1 if port is open (TV on/standby), 0 if unreachable.
 */
int samsung_tv_probe(const char *ip);

/**
 * @brief Send a remote key code to the TV.
 *        Connects, authenticates, sends the key, then disconnects.
 *        On first call from a new client, the TV may ask the user to accept
 *        on-screen; subsequent calls from the same client name are auto-accepted.
 * @param ip       TV IPv4 address.
 * @param key_code Key code string, e.g. "KEY_POWER", "KEY_VOLUP".
 * @return 0 on success, -1 on error.
 */
int samsung_tv_send_key(const char *ip, const char *key_code);

/**
 * @brief Send multiple key codes in one TCP session.
 *        Authenticates once, sends each key with delay_ms between them.
 * @param ip       TV IPv4 address.
 * @param keys     NULL-terminated array of KEY_* strings.
 * @param delay_ms Delay between keys in milliseconds (0 = no delay).
 * @return 0 on success, -1 on error.
 */
int samsung_tv_send_keys(const char *ip, const char **keys, int delay_ms);

#define TV_MODEL_LEN 64

/** Known Samsung TV entry. */
typedef struct
{
    char ip[16];
    char model[TV_MODEL_LEN];
} SamsungTvDevice;

/** List of known Samsung TVs (from tv_devices store). */
typedef struct
{
    SamsungTvDevice *devices;
    int              count;
} SamsungTvDeviceList;

/**
 * @brief Load known TVs from ~/.config/home-appliances/tv_devices.
 *        Missing file is not an error (returns 0, empty list).
 * @return 0 on success, -1 on I/O error.
 */
int samsung_tv_load(SamsungTvDeviceList *out);

/**
 * @brief Save TV list to ~/.config/home-appliances/tv_devices (mode 0600).
 *        Overwrites the file completely.
 * @return 0 on success, -1 on error.
 */
int samsung_tv_save(const SamsungTvDeviceList *list);

/** @brief Free a SamsungTvDeviceList allocated by samsung_tv_load or samsung_tv_scan. */
void samsung_tv_device_list_free(SamsungTvDeviceList *list);

/**
 * @brief Check whether ip appears in the tv_devices store.
 * @return 1 if found, 0 if not found, -1 on I/O error.
 */
int samsung_tv_is_known(const char *ip);

/**
 * @brief Scan a subnet for Samsung TVs by probing TCP port 55000.
 *        Results are NOT saved to the store — caller must call samsung_tv_save().
 * @param cidr CIDR subnet, e.g. "192.168.1.0/24".
 * @param out  Output list; caller must free with samsung_tv_device_list_free().
 * @return 0 on success (empty list is OK), -1 on fatal error.
 */
int samsung_tv_scan(const char *cidr, SamsungTvDeviceList *out);

#endif /* SAMSUNG_TV_CLIENT_H */
