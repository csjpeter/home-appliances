#ifndef CONFIG_H
#define CONFIG_H

/**
 * @file config.h
 * @brief Alkalmazás konfiguráció (~/.config/home-appliances/config.ini).
 */

#define MAX_NETWORK_LEN 64
#define MAX_IFACE_LEN   32

typedef struct {
    char network[MAX_NETWORK_LEN]; /**< Alhálózat, pl. "192.168.1.0/24" */
    char iface[MAX_IFACE_LEN];     /**< Hálózati interfész, pl. "wlan0" */
    int  discovery_timeout_ms;     /**< UDP scan timeout milliszekundumban */
} Config;

/**
 * @brief Konfiguráció betöltése. Hiányos mezőket alapértékekkel tölti.
 * @param cfg Kimeneti konfiguráció struktúra.
 * @return 0 siker, -1 hiba esetén.
 */
int config_load(Config *cfg);

/**
 * @brief Konfiguráció mentése.
 * @return 0 siker, -1 hiba esetén.
 */
int config_save(const Config *cfg);

#endif /* CONFIG_H */
