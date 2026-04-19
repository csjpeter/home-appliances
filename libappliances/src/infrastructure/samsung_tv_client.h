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

#endif /* SAMSUNG_TV_CLIENT_H */
