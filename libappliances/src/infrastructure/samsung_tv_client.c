/**
 * @file samsung_tv_client.c
 * @brief Samsung Smart TV local control — legacy TCP/55000 protocol.
 *        Supports pre-2016 Samsung Smart TV models (unencrypted LAN remote).
 */

#include "samsung_tv_client.h"
#include "../core/base64.h"
#include "../core/logger.h"
#include "../core/raii.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define SAMSUNG_TV_APP_STRING    "iphone.iapp.samsung"
#define SAMSUNG_TV_APP_STR_LEN   19u   /* strlen("iphone.iapp.samsung") */

#define SAMSUNG_TV_CLIENT_NAME   "home-appliances"
#define SAMSUNG_TV_CLIENT_IP     "192.168.1.113"
#define SAMSUNG_TV_CLIENT_MAC    "000000000000"

/* Auth retry settings when TV shows "accept?" on screen. */
#define SAMSUNG_TV_WAIT_RETRIES  15
#define SAMSUNG_TV_WAIT_SLEEP_US 2000000u  /* 2 seconds */

/* Connect timeout. */
#define SAMSUNG_TV_CONNECT_TIMEOUT_S 2

/* ── RAII cleanup helpers ─────────────────────────────────────────────── */

static void close_fd(int *p) { if (*p >= 0) close(*p); }

/* ── Packet builders ──────────────────────────────────────────────────── */

/*
 * Write a 2-byte little-endian length field into buf at *pos.
 * Returns 0 on success, -1 if buffer is too small.
 */
static int append_le16(unsigned char *buf, size_t *pos, size_t cap, size_t val)
{
    if (*pos + 2 > cap)
        return -1;
    buf[(*pos)++] = (unsigned char)(val & 0xFFu);
    buf[(*pos)++] = (unsigned char)((val >> 8) & 0xFFu);
    return 0;
}

static int append_bytes(unsigned char *buf, size_t *pos, size_t cap,
                        const unsigned char *src, size_t len)
{
    if (*pos + len > cap)
        return -1;
    memcpy(buf + *pos, src, len);
    *pos += len;
    return 0;
}

/*
 * build_auth_packet — construct the Samsung TV authentication packet.
 *
 * Packet layout:
 *   0x00
 *   LE16( len("iphone.iapp.samsung") )
 *   "iphone.iapp.samsung"
 *   LE16( inner_payload_total_len )
 *   0x64  0x00               (auth type)
 *   LE16(len(b64_name)) + b64(client_name)
 *   LE16(len(b64_ip))   + b64(client_ip)
 *   LE16(len(b64_mac))  + b64(client_mac)
 *
 * Returns number of bytes written, or -1 on error.
 */
static int build_auth_packet(unsigned char *buf, size_t cap)
{
    /* Base64-encode the three fields. */
    char b64_name[64], b64_ip[64], b64_mac[64];

    if (base64_encode((const unsigned char *)SAMSUNG_TV_CLIENT_NAME,
                      strlen(SAMSUNG_TV_CLIENT_NAME),
                      b64_name, sizeof(b64_name)) < 0)
        return -1;
    if (base64_encode((const unsigned char *)SAMSUNG_TV_CLIENT_IP,
                      strlen(SAMSUNG_TV_CLIENT_IP),
                      b64_ip, sizeof(b64_ip)) < 0)
        return -1;
    if (base64_encode((const unsigned char *)SAMSUNG_TV_CLIENT_MAC,
                      strlen(SAMSUNG_TV_CLIENT_MAC),
                      b64_mac, sizeof(b64_mac)) < 0)
        return -1;

    size_t name_len = strlen(b64_name);
    size_t ip_len   = strlen(b64_ip);
    size_t mac_len  = strlen(b64_mac);

    /*
     * Inner payload:
     *   2 bytes type (0x64 0x00)
     *   2+name_len for client name field
     *   2+ip_len   for client ip field
     *   2+mac_len  for client mac field
     */
    size_t inner_len = 2u + (2u + name_len) + (2u + ip_len) + (2u + mac_len);

    size_t pos = 0;

    /* Leading 0x00 */
    if (pos + 1 > cap) return -1;
    buf[pos++] = 0x00;

    /* LE16: length of app string */
    if (append_le16(buf, &pos, cap, SAMSUNG_TV_APP_STR_LEN) != 0) return -1;

    /* App string */
    if (append_bytes(buf, &pos, cap,
                     (const unsigned char *)SAMSUNG_TV_APP_STRING,
                     SAMSUNG_TV_APP_STR_LEN) != 0) return -1;

    /* LE16: inner payload length */
    if (append_le16(buf, &pos, cap, inner_len) != 0) return -1;

    /* Auth type: 0x64 0x00 */
    if (pos + 2 > cap) return -1;
    buf[pos++] = 0x64;
    buf[pos++] = 0x00;

    /* client name: LE16(len) + b64 bytes */
    if (append_le16(buf, &pos, cap, name_len) != 0) return -1;
    if (append_bytes(buf, &pos, cap,
                     (const unsigned char *)b64_name, name_len) != 0) return -1;

    /* client ip: LE16(len) + b64 bytes */
    if (append_le16(buf, &pos, cap, ip_len) != 0) return -1;
    if (append_bytes(buf, &pos, cap,
                     (const unsigned char *)b64_ip, ip_len) != 0) return -1;

    /* client mac: LE16(len) + b64 bytes */
    if (append_le16(buf, &pos, cap, mac_len) != 0) return -1;
    if (append_bytes(buf, &pos, cap,
                     (const unsigned char *)b64_mac, mac_len) != 0) return -1;

    return (int)pos;
}

/*
 * build_key_packet — construct the key-press command packet.
 *
 * Packet layout:
 *   0x00
 *   LE16( len("iphone.iapp.samsung") )
 *   "iphone.iapp.samsung"
 *   LE16( inner_payload_total_len )
 *   0x00 0x00 0x00             (3 zero bytes)
 *   LE16( len(b64_key) )
 *   b64(key_code)
 *
 * Returns number of bytes written, or -1 on error.
 */
static int build_key_packet(const char *key_code,
                            unsigned char *buf, size_t cap)
{
    char b64_key[128];

    if (base64_encode((const unsigned char *)key_code,
                      strlen(key_code),
                      b64_key, sizeof(b64_key)) < 0)
        return -1;

    size_t key_len = strlen(b64_key);

    /*
     * Inner payload:
     *   3 zero bytes + 2-byte LE length + key_len bytes
     */
    size_t inner_len = 3u + 2u + key_len;

    size_t pos = 0;

    /* Leading 0x00 */
    if (pos + 1 > cap) return -1;
    buf[pos++] = 0x00;

    /* LE16: length of app string */
    if (append_le16(buf, &pos, cap, SAMSUNG_TV_APP_STR_LEN) != 0) return -1;

    /* App string */
    if (append_bytes(buf, &pos, cap,
                     (const unsigned char *)SAMSUNG_TV_APP_STRING,
                     SAMSUNG_TV_APP_STR_LEN) != 0) return -1;

    /* LE16: inner payload length */
    if (append_le16(buf, &pos, cap, inner_len) != 0) return -1;

    /* 3 zero bytes */
    if (pos + 3 > cap) return -1;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    /* LE16: b64 key length */
    if (append_le16(buf, &pos, cap, key_len) != 0) return -1;

    /* b64-encoded key */
    if (append_bytes(buf, &pos, cap,
                     (const unsigned char *)b64_key, key_len) != 0) return -1;

    return (int)pos;
}

/* ── TCP connect with timeout ─────────────────────────────────────────── */

/*
 * tcp_connect_timeout — create a TCP socket and connect to ip:port
 * with a timeout of SAMSUNG_TV_CONNECT_TIMEOUT_S seconds.
 *
 * Returns the connected fd (>= 0), or -1 on error/timeout.
 * The returned fd is left in blocking mode.
 */
static int tcp_connect_timeout(const char *ip, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR_MSG("samsung_tv: socket(): %s", strerror(errno));
        return -1;
    }

    /* Switch to non-blocking so connect() returns immediately. */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG_ERROR_MSG("samsung_tv: fcntl(): %s", strerror(errno));
        close(fd);
        return -1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        LOG_ERROR_MSG("samsung_tv: invalid IP address: %s", ip);
        close(fd);
        return -1;
    }

    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        /* Connection refused or similar — not an error per se, just unreachable. */
        close(fd);
        return -1;
    }

    if (rc != 0) {
        /* Wait for writable (connect completed) or timeout. */
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(fd, &wset);
        struct timeval tv = {
            .tv_sec  = SAMSUNG_TV_CONNECT_TIMEOUT_S,
            .tv_usec = 0,
        };
        int sel = select(fd + 1, NULL, &wset, NULL, &tv);
        if (sel <= 0) {
            /* Timeout (sel == 0) or select error (sel < 0). */
            close(fd);
            return -1;
        }

        /* Verify connect actually succeeded. */
        int err = 0;
        socklen_t errlen = sizeof(err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0 || err != 0) {
            close(fd);
            return -1;
        }
    }

    /* Restore blocking mode for subsequent send/recv calls. */
    if (fcntl(fd, F_SETFL, flags) < 0) {
        LOG_ERROR_MSG("samsung_tv: fcntl(restore blocking): %s", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

/* ── Auth response parsing ────────────────────────────────────────────── */

/*
 * Auth response byte at index 2:
 *   0x01 → ACCESS GRANTED
 *   0x00 → DENIED
 *   0x02 → WAITING (user must accept on TV)
 *
 * Returns 1 (granted), 0 (denied), 2 (waiting), or -1 (read error / short).
 */
static int read_auth_response(int fd)
{
    unsigned char resp[64];
    ssize_t n = recv(fd, resp, sizeof(resp), 0);
    if (n < 3) {
        LOG_ERROR_MSG("samsung_tv: short or failed auth response (n=%zd)", n);
        return -1;
    }
    return (int)resp[2];
}

/* ── Connect and authenticate ─────────────────────────────────────────── */

/*
 * tv_connect_and_auth — connect to the TV and complete the auth handshake.
 *
 * Retries on WAITING response (TV showing accept dialog).
 * On the first WAITING, prints a message to stderr for the user.
 *
 * Returns an open fd (>= 0) on success.  Caller must close() the fd.
 * Returns -1 on error (denied, timeout, or connect failure).
 */
static int tv_connect_and_auth(const char *ip)
{
    unsigned char auth_pkt[256];
    int auth_len = build_auth_packet(auth_pkt, sizeof(auth_pkt));
    if (auth_len < 0)
        return -1;

    int fd = tcp_connect_timeout(ip, SAMSUNG_TV_PORT);
    if (fd < 0) {
        LOG_ERROR_MSG("samsung_tv: cannot connect to %s:%d", ip, SAMSUNG_TV_PORT);
        return -1;
    }

    if (send(fd, auth_pkt, (size_t)auth_len, 0) != auth_len) {
        LOG_ERROR_MSG("samsung_tv: send auth failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    for (int attempt = 0; attempt < SAMSUNG_TV_WAIT_RETRIES; attempt++) {
        int code = read_auth_response(fd);
        if (code < 0) {
            close(fd);
            return -1;
        }
        if (code == 1)
            return fd;  /* granted */
        if (code == 0) {
            LOG_WARN_MSG("samsung_tv: access denied by %s", ip);
            close(fd);
            return -1;
        }
        /* code == 2: waiting */
        if (attempt == 0)
            fprintf(stderr,
                    "Waiting for TV to accept connection — please approve on screen...\n");
        usleep(SAMSUNG_TV_WAIT_SLEEP_US);
        if (send(fd, auth_pkt, (size_t)auth_len, 0) != auth_len) {
            LOG_ERROR_MSG("samsung_tv: re-send auth failed: %s", strerror(errno));
            close(fd);
            return -1;
        }
    }

    LOG_ERROR_MSG("samsung_tv: timed out waiting for acceptance on %s", ip);
    close(fd);
    return -1;
}

/* ── Public API ──────────────────────────────────────────────────────── */

int samsung_tv_probe(const char *ip)
{
    int fd RAII_WITH_CLEANUP(close_fd) = tcp_connect_timeout(ip, SAMSUNG_TV_PORT);
    if (fd < 0)
        return 0;   /* port closed / unreachable — not a fatal error */
    return 1;       /* port open */
}

int samsung_tv_send_key(const char *ip, const char *key_code)
{
    int fd RAII_WITH_CLEANUP(close_fd) = tv_connect_and_auth(ip);
    if (fd < 0)
        return -1;

    unsigned char key_pkt[256];
    int key_len = build_key_packet(key_code, key_pkt, sizeof(key_pkt));
    if (key_len < 0) {
        LOG_ERROR_MSG("samsung_tv_send_key: failed to build key packet for '%s'",
                      key_code);
        return -1;
    }

    if (send(fd, key_pkt, (size_t)key_len, 0) != key_len) {
        LOG_ERROR_MSG("samsung_tv_send_key: send key failed: %s", strerror(errno));
        return -1;
    }

    LOG_INFO_MSG("samsung_tv_send_key: sent '%s' to %s", key_code, ip);
    return 0;
}

int samsung_tv_send_keys(const char *ip, const char **keys, int delay_ms)
{
    int fd RAII_WITH_CLEANUP(close_fd) = tv_connect_and_auth(ip);
    if (fd < 0)
        return -1;

    for (int i = 0; keys[i] != NULL; i++) {
        if (i > 0 && delay_ms > 0)
            usleep((unsigned int)delay_ms * 1000u);

        unsigned char key_pkt[256];
        int key_len = build_key_packet(keys[i], key_pkt, sizeof(key_pkt));
        if (key_len < 0) {
            LOG_ERROR_MSG("samsung_tv_send_keys: failed to build key packet for '%s'",
                          keys[i]);
            return -1;
        }

        if (send(fd, key_pkt, (size_t)key_len, 0) != key_len) {
            LOG_ERROR_MSG("samsung_tv_send_keys: send failed for '%s': %s",
                          keys[i], strerror(errno));
            return -1;
        }
        LOG_INFO_MSG("samsung_tv_send_keys: sent '%s' to %s", keys[i], ip);
    }
    return 0;
}
