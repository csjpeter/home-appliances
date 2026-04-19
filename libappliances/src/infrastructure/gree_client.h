#ifndef GREE_CLIENT_H
#define GREE_CLIENT_H

/**
 * @file gree_client.h
 * @brief Gree klíma LAN v2 protokoll (UDP/7000, AES-128-ECB).
 */

#define GREE_PORT 7000
#define GREE_DISCOVERY_KEY "a3K8Bx%2r8Y7#xDh"
#define GREE_MAC_LEN 13

typedef struct {
    char ip[16];               /**< IPv4 cím */
    char mac[GREE_MAC_LEN];    /**< MAC cím (pl. "020000aa0005") */
    char firmware[32];         /**< Firmware verzió */
    char device_key[17];       /**< Eszközspecifikus AES kulcs (binding után) */
    int  bound;                /**< 1 ha a binding megtörtént */
} GreeDevice;

typedef struct {
    GreeDevice *devices;
    int count;
} GreeDeviceList;

/**
 * @brief Hálózati scan — megkeresi az összes Gree klímát.
 * @param broadcast Broadcast cím (pl. "192.168.1.255").
 * @param timeout_ms Várakozási idő milliszekundumban.
 * @param out Kimeneti eszközlista (hívónak kell felszabadítani: gree_device_list_free).
 * @return 0 siker, -1 hiba esetén.
 */
int gree_client_scan(const char *broadcast, int timeout_ms, GreeDeviceList *out);

/**
 * @brief Binding — eszközspecifikus kulcs lekérése.
 * @return 0 siker, -1 hiba esetén.
 */
int gree_client_bind(GreeDevice *dev);

/**
 * @brief Eszközállapot lekérdezése.
 * @param keys Lekérdezendő kulcsok tömbje (pl. {"Pow", "SetTem", NULL}).
 * @param values Kimeneti értékek tömbje (hívónak kell felszabadítani).
 * @return 0 siker, -1 hiba esetén.
 */
int gree_client_get_status(const GreeDevice *dev, const char **keys,
                           int **values, int count);

/**
 * @brief Eszköz vezérlése.
 * @param keys Beállítandó kulcsok tömbje.
 * @param values Beállítandó értékek tömbje.
 * @param count Kulcs-érték párok száma.
 * @return 0 siker, -1 hiba esetén.
 */
int gree_client_set(const GreeDevice *dev, const char **keys,
                    const int *values, int count);

/**
 * @brief Eszközlista memóriájának felszabadítása.
 */
void gree_device_list_free(GreeDeviceList *list);

#endif /* GREE_CLIENT_H */
