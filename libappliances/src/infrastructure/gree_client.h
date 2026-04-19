#ifndef GREE_CLIENT_H
#define GREE_CLIENT_H

/**
 * @file gree_client.h
 * @brief Gree AC LAN v2 protocol (UDP/7000, AES-128-ECB).
 */

#ifndef GREE_PORT
#define GREE_PORT            7000
#endif
#define GREE_DISCOVERY_KEY   "a3K8Bx%2r8Y7#xDh"  /* public protocol key */
#define GREE_MAC_LEN         13   /* 12 hex chars + NUL */
#define GREE_KEY_LEN         17   /* 16 char device key + NUL */
#define GREE_NAME_LEN        64

typedef struct {
    char ip[16];
    char mac[GREE_MAC_LEN];
    char name[GREE_NAME_LEN];
    char firmware[32];
    char device_key[GREE_KEY_LEN]; /**< Set after successful bind. */
    int  bound;                    /**< 1 if binding completed. */
} GreeDevice;

typedef struct {
    GreeDevice *devices;
    int         count;
} GreeDeviceList;

/** AC operating mode (Mod). */
typedef enum {
    GREE_MODE_AUTO = 0,
    GREE_MODE_COOL = 1,
    GREE_MODE_DRY  = 2,
    GREE_MODE_FAN  = 3,
    GREE_MODE_HEAT = 4,
} GreeMode;

/** Fan speed (Wnd). */
typedef enum {
    GREE_FAN_AUTO  = 0,
    GREE_FAN_LOW   = 1,
    GREE_FAN_MED   = 2,
    GREE_FAN_HIGH  = 3,
    GREE_FAN_TURBO = 4,
} GreeFanSpeed;

/** Temperature unit (TemUn). */
typedef enum {
    GREE_TEMUN_CELSIUS    = 0,
    GREE_TEMUN_FAHRENHEIT = 1,
} GreeTempUnit;

/** Full device status returned by gree_client_get_status. */
typedef struct {
    int         power;          /**< Pow:          0=off, 1=on */
    int         set_temp;       /**< SetTem:        16-30 */
    int         room_temp;      /**< TemSen:        decoded Celsius (read-only) */
    GreeMode    mode;           /**< Mod */
    GreeFanSpeed fan;           /**< WdSpd:         0=auto,1-5 */
    int         swing_v;        /**< SwUpDn:        0=fixed, 1=swing */
    int         swing_h;        /**< SwingLfRig:    0=fixed, 1=swing, 2-6=position */
    int         lights;         /**< Lig:           0=off, 1=on */
    int         quiet;          /**< Quiet:         0=off, 1=on */
    int         turbo;          /**< Tur:           0=off, 1=on */
    int         sleep;          /**< SwhSlp:        0=off, 1=on */
    int         xfan;           /**< Blo:           X-Fan / blow-dry */
    int         air;            /**< Air:           fresh air */
    int         health;         /**< Health:        anion */
    int         steady_heat;    /**< StHt:          anti-freeze */
    GreeTempUnit temp_unit;     /**< TemUn */
    int         heat_cool_type; /**< HeatCoolType:  0=cool-only, 1=heat+cool (read-only) */
} GreeStatus;

/**
 * @brief Broadcast scan — finds all Gree AC units responding on the network.
 * @param broadcast  Broadcast address (e.g. "192.168.1.255").
 * @param timeout_ms Receive timeout in milliseconds.
 * @param out        Output list (caller must free with gree_device_list_free).
 * @return 0 on success, -1 on error.
 */
int gree_client_scan(const char *broadcast, int timeout_ms, GreeDeviceList *out);

/**
 * @brief Binding handshake — obtains the device-specific AES key.
 *        On success, dev->device_key and dev->bound are set.
 * @return 0 on success, -1 on error.
 */
int gree_client_bind(GreeDevice *dev);

/**
 * @brief Query full AC status. Device must be bound.
 * @return 0 on success, -1 on error.
 */
int gree_client_get_status(const GreeDevice *dev, GreeStatus *out);

/**
 * @brief Set one or more AC parameters. Device must be bound.
 * @param keys   NULL-terminated array of Gree parameter name strings
 *               (e.g. {"Pow", "SetTem", NULL}).
 * @param values Corresponding integer values.
 * @param count  Number of key-value pairs.
 * @return 0 on success, -1 on error.
 */
int gree_client_set(const GreeDevice *dev, const char **keys,
                    const int *values, int count);

/**
 * @brief Load previously bound devices from persistent store
 *        (~/.config/home-appliances/gree_devices).
 * @param out  Output list (caller must free with gree_device_list_free).
 * @return 0 on success (empty list is not an error), -1 on I/O error.
 */
int gree_client_load_bound(GreeDeviceList *out);

/**
 * @brief Save all bound devices in list to persistent store.
 * @return 0 on success, -1 on error.
 */
int gree_client_save_bound(const GreeDeviceList *list);

/**
 * @brief Copy device keys from saved into scanned, matched by MAC address.
 *        Scanned devices whose MAC matches a saved entry get bound=1 and device_key set.
 */
void gree_client_merge_bound(GreeDeviceList *scanned, const GreeDeviceList *saved);

/**
 * @brief Free device list memory.
 */
void gree_device_list_free(GreeDeviceList *list);

#endif /* GREE_CLIENT_H */
