#include "gree_client.h"
#include "../core/logger.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Gree protokoll implementáció helye.
 * A binding, állapotlekérdezés és vezérlés AES-128-ECB titkosítást igényel.
 * Jelenlegi állapot: stub — tényleges AES implementáció szükséges. */

static int udp_socket_create(int timeout_ms)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOG_ERROR_MSG("socket(): %s", strerror(errno));
        return -1;
    }

    int broadcast = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    return fd;
}

int gree_client_scan(const char *broadcast, int timeout_ms, GreeDeviceList *out)
{
    out->devices = NULL;
    out->count = 0;

    int fd = udp_socket_create(timeout_ms);
    if (fd < 0)
        return -1;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(GREE_PORT);
    inet_pton(AF_INET, broadcast, &addr.sin_addr);

    const char *scan_msg = "{\"t\":\"scan\"}";
    if (sendto(fd, scan_msg, strlen(scan_msg), 0,
               (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR_MSG("sendto(): %s", strerror(errno));
        close(fd);
        return -1;
    }

    int capacity = 8;
    out->devices = malloc(capacity * sizeof(GreeDevice));
    if (!out->devices) {
        close(fd);
        return -1;
    }

    char buf[2048];
    struct sockaddr_in sender = {0};
    socklen_t sender_len = sizeof(sender);

    while (1) {
        ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&sender, &sender_len);
        if (n < 0)
            break; /* timeout vagy hiba */

        buf[n] = '\0';

        /* TODO: JSON parse + AES-128-ECB dekódolás a pack mezőre */
        LOG_DEBUG_MSG("Gree válasz: %s", buf);

        if (out->count >= capacity) {
            capacity *= 2;
            GreeDevice *tmp = realloc(out->devices, capacity * sizeof(GreeDevice));
            if (!tmp)
                break;
            out->devices = tmp;
        }

        GreeDevice *dev = &out->devices[out->count++];
        memset(dev, 0, sizeof(*dev));
        inet_ntop(AF_INET, &sender.sin_addr, dev->ip, sizeof(dev->ip));
    }

    close(fd);
    LOG_INFO_MSG("Gree scan kész: %d eszköz találva", out->count);
    return 0;
}

int gree_client_bind(GreeDevice *dev)
{
    /* TODO: binding handshake implementálása */
    LOG_WARN_MSG("gree_client_bind: nem implementált (%s)", dev->ip);
    return -1;
}

int gree_client_get_status(const GreeDevice *dev, const char **keys,
                           int **values, int count)
{
    (void)keys;
    (void)count;
    /* TODO: állapotlekérdezés implementálása */
    LOG_WARN_MSG("gree_client_get_status: nem implementált (%s)", dev->ip);
    *values = NULL;
    return -1;
}

int gree_client_set(const GreeDevice *dev, const char **keys,
                    const int *values, int count)
{
    (void)keys;
    (void)values;
    (void)count;
    /* TODO: vezérlés implementálása */
    LOG_WARN_MSG("gree_client_set: nem implementált (%s)", dev->ip);
    return -1;
}

void gree_device_list_free(GreeDeviceList *list)
{
    free(list->devices);
    list->devices = NULL;
    list->count = 0;
}
