#ifndef ROBOROCK_CLIENT_H
#define ROBOROCK_CLIENT_H

/**
 * @file roborock_client.h
 * @brief Roborock vacuum client using the miio UDP/54321 protocol.
 *
 * Packet structure: 32-byte big-endian header + AES-128-CBC encrypted JSON.
 * Key derivation: key = MD5(token), iv = MD5(key || token).
 */

#ifndef ROBOROCK_PORT
#define ROBOROCK_PORT 54321
#endif

/** Device descriptor.  Populate ip before calling roborock_hello(). */
typedef struct
{
    char         ip[16];
    unsigned char token[16];
    unsigned int  device_id;
    int           token_valid;
} RoborockDevice;

/**
 * Vacuum operational status.
 * state: 1=init, 2=sleep, 3=idle, 5=cleaning, 6=returning,
 *        8=charging, 11=error, 16=shutdown.
 * fan_power: 101=Silent, 102=Balanced, 103=Turbo, 104=Max, 105=Gentle.
 */
typedef struct
{
    int battery;
    int state;
    int error_code;
    int clean_time;   /**< seconds */
    int clean_area;   /**< cm² */
    int fan_power;
    int in_cleaning;
} RoborockStatus;

/** Consumable usage in seconds of operation. */
typedef struct
{
    int main_brush;
    int side_brush;
    int filter;
    int sensor;
} RoborockConsumables;

/**
 * @brief Perform hello handshake; fills dev->token, dev->device_id,
 *        dev->token_valid = 1.
 * @return 0 on success, -1 on error.
 */
int roborock_hello(RoborockDevice *dev);

/**
 * @brief Query current vacuum status.
 * @return 0 on success, -1 on error.
 */
int roborock_get_status(RoborockDevice *dev, RoborockStatus *out);

/**
 * @brief Query consumable usage counters.
 * @return 0 on success, -1 on error.
 */
int roborock_get_consumable(RoborockDevice *dev, RoborockConsumables *out);

/** @brief Start cleaning.      @return 0 on success, -1 on error. */
int roborock_start(RoborockDevice *dev);

/** @brief Stop cleaning.       @return 0 on success, -1 on error. */
int roborock_stop(RoborockDevice *dev);

/** @brief Pause cleaning.      @return 0 on success, -1 on error. */
int roborock_pause(RoborockDevice *dev);

/** @brief Return to dock.      @return 0 on success, -1 on error. */
int roborock_dock(RoborockDevice *dev);

/** @brief Spot-clean mode.     @return 0 on success, -1 on error. */
int roborock_spot(RoborockDevice *dev);

/** @brief Make robot beep/locate itself.  @return 0 on success, -1 on error. */
int roborock_find(RoborockDevice *dev);

/**
 * @brief Set fan power level (101-105).
 * @return 0 on success, -1 on error.
 */
int roborock_set_fan(RoborockDevice *dev, int level);

/**
 * @brief Reset a named consumable counter.
 * @param item One of: "main_brush_work_time", "side_brush_work_time",
 *             "filter_work_time", "sensor_dirty_time".
 * @return 0 on success, -1 on error.
 */
int roborock_reset_consumable(RoborockDevice *dev, const char *item);

/**
 * @brief Save device credentials to ~/.config/home-appliances/roborock_devices.
 *        Updates existing entry if ip already present, otherwise appends.
 * @return 0 on success, -1 on error.
 */
int roborock_save(const RoborockDevice *dev);

/**
 * @brief Load device credentials for a given IP address.
 * @return 0 = found and filled, 1 = not found, -1 = error.
 */
int roborock_load(const char *ip, RoborockDevice *dev);

#endif /* ROBOROCK_CLIENT_H */
