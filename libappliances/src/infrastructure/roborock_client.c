/**
 * @file roborock_client.c
 * @brief Roborock vacuum client — miio UDP protocol implementation.
 *
 * Packet format (32-byte big-endian header):
 *   [0-1]  Magic   0x2131
 *   [2-3]  Total length (header + payload)
 *   [4-7]  Reserved: 0x00000000 (normal) / 0xFFFFFFFF (hello)
 *   [8-11] Device ID
 *  [12-15] Timestamp (Unix)
 *  [16-31] MD5 checksum  (MD5 of header[0..15] + token + encrypted_payload)
 *  [32+]   AES-128-CBC encrypted JSON payload (PKCS#7)
 *
 * Key derivation:
 *   key = MD5(token)
 *   iv  = MD5(key || token)
 */

#include "roborock_client.h"
#include "../core/logger.h"
#include "../core/raii.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ── Constants ────────────────────────────────────────────────────────────── */

#define MIIO_HEADER_LEN      32
#define MIIO_MAGIC           0x2131u
#define MIIO_HELLO_RESERVED  0xFFFFFFFFu
#define MIIO_TIMEOUT_MS      5000
#define MIIO_MAX_PAYLOAD     1024
#define MIIO_MAX_PACKET      (MIIO_HEADER_LEN + MIIO_MAX_PAYLOAD + 16)
#define ROBOROCK_STORE_FMT   "%s/.config/home-appliances/roborock_devices"

/* ── RAII helpers ─────────────────────────────────────────────────────────── */

static void close_fd(int *p)             { if (*p >= 0) close(*p); }
static void free_evp_ctx(EVP_MD_CTX **p) { if (*p) EVP_MD_CTX_free(*p); }
static void free_evp_cipher(EVP_CIPHER_CTX **p)
{
    if (*p)
        EVP_CIPHER_CTX_free(*p);
}

/* ── Request ID ───────────────────────────────────────────────────────────── */

static int g_request_id = 1;

/* ── Key derivation ───────────────────────────────────────────────────────── */

static void derive_keys(const unsigned char *token,
                        unsigned char *key, unsigned char *iv)
{
    EVP_MD_CTX *ctx RAII_WITH_CLEANUP(free_evp_ctx) = EVP_MD_CTX_new();
    if (!ctx)
        return;

    unsigned int len = 16;

    /* key = MD5(token) */
    EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
    EVP_DigestUpdate(ctx, token, 16);
    EVP_DigestFinal_ex(ctx, key, &len);

    /* iv = MD5(key || token) — 32 bytes total */
    EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
    EVP_DigestUpdate(ctx, key, 16);
    EVP_DigestUpdate(ctx, token, 16);
    EVP_DigestFinal_ex(ctx, iv, &len);
}

/* ── AES-128-CBC encrypt / decrypt ───────────────────────────────────────── */

static int aes128_cbc_encrypt(const unsigned char *key, const unsigned char *iv,
                               const unsigned char *plain, size_t plain_len,
                               unsigned char *out, int *out_len)
{
    EVP_CIPHER_CTX *ctx RAII_WITH_CLEANUP(free_evp_cipher) = EVP_CIPHER_CTX_new();
    if (!ctx)
        return -1;

    if (!EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv))
        return -1;

    int outl = 0, finl = 0;
    if (!EVP_EncryptUpdate(ctx, out, &outl, plain, (int)plain_len))
        return -1;
    if (!EVP_EncryptFinal_ex(ctx, out + outl, &finl))
        return -1;

    *out_len = outl + finl;
    return 0;
}

static int aes128_cbc_decrypt(const unsigned char *key, const unsigned char *iv,
                               const unsigned char *cipher, size_t cipher_len,
                               unsigned char *out, int *out_len)
{
    EVP_CIPHER_CTX *ctx RAII_WITH_CLEANUP(free_evp_cipher) = EVP_CIPHER_CTX_new();
    if (!ctx)
        return -1;

    if (!EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv))
        return -1;

    int outl = 0, finl = 0;
    if (!EVP_DecryptUpdate(ctx, out, &outl, cipher, (int)cipher_len))
        return -1;
    if (!EVP_DecryptFinal_ex(ctx, out + outl, &finl))
        return -1;

    *out_len = outl + finl;
    return 0;
}

/* ── MD5 checksum ────────────────────────────────────────────────────────── */

/**
 * Compute packet checksum:
 *   MD5(header[0..15] || token[0..15] || encrypted_payload)
 * Write the 16-byte result into checksum_out.
 */
static void compute_checksum(const unsigned char *header_prefix, /* bytes 0-15 */
                              const unsigned char *token,
                              const unsigned char *payload,
                              size_t payload_len,
                              unsigned char *checksum_out)
{
    EVP_MD_CTX *ctx RAII_WITH_CLEANUP(free_evp_ctx) = EVP_MD_CTX_new();
    if (!ctx)
        return;

    unsigned int len = 16;
    EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
    EVP_DigestUpdate(ctx, header_prefix, 16);
    EVP_DigestUpdate(ctx, token, 16);
    if (payload && payload_len > 0)
        EVP_DigestUpdate(ctx, payload, payload_len);
    EVP_DigestFinal_ex(ctx, checksum_out, &len);
}

/* ── UDP socket helper ───────────────────────────────────────────────────── */

static int udp_socket_with_timeout(int timeout_ms)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        LOG_ERROR_MSG("roborock: socket(): %s", strerror(errno));
        return -1;
    }
    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

/* ── Build packet ────────────────────────────────────────────────────────── */

/**
 * Encrypt json as a miio packet into out_buf.
 * dev->token and dev->device_id must be valid.
 * Returns 0 on success, -1 on error.
 */
static int build_packet(const RoborockDevice *dev, const char *json,
                        unsigned char *out_buf, size_t *out_len)
{
    unsigned char key[16], iv[16];
    derive_keys(dev->token, key, iv);

    /* Encrypt payload */
    unsigned char enc[MIIO_MAX_PAYLOAD + 16];
    int enc_len = 0;
    if (aes128_cbc_encrypt(key, iv,
                           (const unsigned char *)json, strlen(json),
                           enc, &enc_len) != 0)
    {
        LOG_ERROR_MSG("roborock: encrypt failed");
        return -1;
    }

    size_t total = (size_t)MIIO_HEADER_LEN + (size_t)enc_len;
    if (total > MIIO_MAX_PACKET)
    {
        LOG_ERROR_MSG("roborock: packet too large");
        return -1;
    }

    /* Build header (bytes 0-15 first, then compute checksum) */
    memset(out_buf, 0, MIIO_HEADER_LEN);

    uint32_t ts = (uint32_t)time(NULL);

    /* Magic */
    out_buf[0] = 0x21;
    out_buf[1] = 0x31;
    /* Total length */
    out_buf[2] = (unsigned char)((total >> 8) & 0xFF);
    out_buf[3] = (unsigned char)(total & 0xFF);
    /* Reserved = 0x00000000 */
    out_buf[4] = 0; out_buf[5] = 0; out_buf[6] = 0; out_buf[7] = 0;
    /* Device ID */
    out_buf[8]  = (unsigned char)((dev->device_id >> 24) & 0xFF);
    out_buf[9]  = (unsigned char)((dev->device_id >> 16) & 0xFF);
    out_buf[10] = (unsigned char)((dev->device_id >> 8)  & 0xFF);
    out_buf[11] = (unsigned char)(dev->device_id & 0xFF);
    /* Timestamp */
    out_buf[12] = (unsigned char)((ts >> 24) & 0xFF);
    out_buf[13] = (unsigned char)((ts >> 16) & 0xFF);
    out_buf[14] = (unsigned char)((ts >> 8)  & 0xFF);
    out_buf[15] = (unsigned char)(ts & 0xFF);
    /* Checksum placeholder already zero */

    /* Compute checksum over header[0..15] + token + enc */
    unsigned char checksum[16];
    compute_checksum(out_buf, dev->token, enc, (size_t)enc_len, checksum);
    memcpy(out_buf + 16, checksum, 16);

    /* Append encrypted payload */
    memcpy(out_buf + MIIO_HEADER_LEN, enc, (size_t)enc_len);
    *out_len = total;
    return 0;
}

/* ── Send / receive ──────────────────────────────────────────────────────── */

/**
 * Send a JSON command, receive and decrypt the response JSON.
 * resp_json is NUL-terminated on success.
 * Returns 0 on success, -1 on error.
 */
static int send_recv(const RoborockDevice *dev, const char *json,
                     char *resp_json, size_t resp_size)
{
    unsigned char pkt[MIIO_MAX_PACKET];
    size_t pkt_len = 0;
    if (build_packet(dev, json, pkt, &pkt_len) != 0)
        return -1;

    int fd RAII_WITH_CLEANUP(close_fd) = udp_socket_with_timeout(MIIO_TIMEOUT_MS);
    if (fd < 0)
        return -1;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(ROBOROCK_PORT);
    inet_pton(AF_INET, dev->ip, &addr.sin_addr);

    if (sendto(fd, pkt, pkt_len, 0,
               (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        LOG_ERROR_MSG("roborock: sendto(%s): %s", dev->ip, strerror(errno));
        return -1;
    }

    unsigned char resp_buf[MIIO_MAX_PACKET];
    ssize_t n = recvfrom(fd, resp_buf, sizeof(resp_buf), 0, NULL, NULL);
    if (n < MIIO_HEADER_LEN)
    {
        LOG_ERROR_MSG("roborock: recvfrom(%s): %s", dev->ip,
                      n < 0 ? strerror(errno) : "short response");
        return -1;
    }

    size_t cipher_len = (size_t)n - MIIO_HEADER_LEN;
    if (cipher_len == 0)
    {
        resp_json[0] = '\0';
        return 0;
    }

    unsigned char key[16], iv[16];
    derive_keys(dev->token, key, iv);

    unsigned char plain[MIIO_MAX_PAYLOAD + 16];
    int plain_len = 0;
    if (aes128_cbc_decrypt(key, iv,
                           resp_buf + MIIO_HEADER_LEN, cipher_len,
                           plain, &plain_len) != 0)
    {
        LOG_ERROR_MSG("roborock: decrypt response failed");
        return -1;
    }

    if (plain_len < 0 || (size_t)plain_len >= resp_size)
    {
        LOG_ERROR_MSG("roborock: response too large");
        return -1;
    }
    memcpy(resp_json, plain, (size_t)plain_len);
    resp_json[plain_len] = '\0';
    LOG_DEBUG_MSG("roborock: response: %.256s", resp_json);
    return 0;
}

/* ── JSON helpers ────────────────────────────────────────────────────────── */

static int json_int_get(const char *json, const char *key, int *out)
{
    char pat[80];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p)
        return -1;
    p += strlen(pat);
    while (*p == ' ')
        p++;
    char *end;
    *out = (int)strtol(p, &end, 10);
    return (end > p) ? 0 : -1;
}

/**
 * Check that the response JSON contains "ok" inside the result field.
 * Handles both `"result":["ok"]` and `"result":["ok",...]`.
 */
static int response_is_ok(const char *json)
{
    const char *result = strstr(json, "\"result\":");
    if (!result)
        return 0;
    return strstr(result, "\"ok\"") != NULL;
}

/* ── Hello handshake ─────────────────────────────────────────────────────── */

int roborock_hello(RoborockDevice *dev)
{
    /* Build 32-byte hello packet: all fields 0xFF except magic+length */
    unsigned char hello[32];
    memset(hello, 0xFF, sizeof(hello));
    hello[0] = 0x21;
    hello[1] = 0x31;
    hello[2] = 0x00;
    hello[3] = 0x20; /* length = 32 */

    int fd RAII_WITH_CLEANUP(close_fd) = udp_socket_with_timeout(MIIO_TIMEOUT_MS);
    if (fd < 0)
        return -1;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(ROBOROCK_PORT);
    inet_pton(AF_INET, dev->ip, &addr.sin_addr);

    if (sendto(fd, hello, sizeof(hello), 0,
               (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        LOG_ERROR_MSG("roborock_hello: sendto(%s): %s", dev->ip, strerror(errno));
        return -1;
    }

    unsigned char resp[32];
    ssize_t n = recvfrom(fd, resp, sizeof(resp), 0, NULL, NULL);
    if (n != 32)
    {
        LOG_ERROR_MSG("roborock_hello: unexpected response length %zd", n);
        return -1;
    }

    /* Extract device_id from bytes 8-11 (big-endian) */
    dev->device_id = ((unsigned int)resp[8]  << 24)
                   | ((unsigned int)resp[9]  << 16)
                   | ((unsigned int)resp[10] << 8)
                   |  (unsigned int)resp[11];

    /* Extract token from bytes 16-31 */
    memcpy(dev->token, resp + 16, 16);
    dev->token_valid = 1;

    LOG_INFO_MSG("roborock_hello: %s device_id=0x%08X", dev->ip, dev->device_id);
    return 0;
}

/* ── Query commands ──────────────────────────────────────────────────────── */

int roborock_get_status(RoborockDevice *dev, RoborockStatus *out)
{
    char json[128];
    snprintf(json, sizeof(json),
             "{\"id\":%d,\"method\":\"get_status\",\"params\":[]}",
             g_request_id++);

    char resp[MIIO_MAX_PAYLOAD];
    if (send_recv(dev, json, resp, sizeof(resp)) != 0)
        return -1;

    /* Response: {"id":N,"result":[{...}]} — find the object inside the array */
    const char *obj = strstr(resp, "{\"battery\":");
    if (!obj)
        obj = strstr(resp, "\"result\":[{");
    if (!obj)
    {
        LOG_ERROR_MSG("roborock_get_status: no result object in response");
        return -1;
    }

    if (json_int_get(obj, "battery",    &out->battery)    != 0 ||
        json_int_get(obj, "state",      &out->state)      != 0 ||
        json_int_get(obj, "error_code", &out->error_code) != 0 ||
        json_int_get(obj, "clean_time", &out->clean_time) != 0 ||
        json_int_get(obj, "clean_area", &out->clean_area) != 0 ||
        json_int_get(obj, "fan_power",  &out->fan_power)  != 0 ||
        json_int_get(obj, "in_cleaning",&out->in_cleaning)!= 0)
    {
        LOG_ERROR_MSG("roborock_get_status: missing fields in: %.256s", resp);
        return -1;
    }
    return 0;
}

int roborock_get_consumable(RoborockDevice *dev, RoborockConsumables *out)
{
    char json[128];
    snprintf(json, sizeof(json),
             "{\"id\":%d,\"method\":\"get_consumable\",\"params\":[]}",
             g_request_id++);

    char resp[MIIO_MAX_PAYLOAD];
    if (send_recv(dev, json, resp, sizeof(resp)) != 0)
        return -1;

    if (json_int_get(resp, "main_brush_work_time", &out->main_brush) != 0 ||
        json_int_get(resp, "side_brush_work_time", &out->side_brush) != 0 ||
        json_int_get(resp, "filter_work_time",     &out->filter)     != 0 ||
        json_int_get(resp, "sensor_dirty_time",    &out->sensor)     != 0)
    {
        LOG_ERROR_MSG("roborock_get_consumable: missing fields in: %.256s", resp);
        return -1;
    }
    return 0;
}

/* ── Simple command helper ───────────────────────────────────────────────── */

static int simple_cmd(RoborockDevice *dev, const char *method)
{
    char json[256];
    snprintf(json, sizeof(json),
             "{\"id\":%d,\"method\":\"%s\",\"params\":[]}",
             g_request_id++, method);

    char resp[MIIO_MAX_PAYLOAD];
    if (send_recv(dev, json, resp, sizeof(resp)) != 0)
        return -1;

    if (!response_is_ok(resp))
    {
        LOG_ERROR_MSG("roborock %s: unexpected response: %.128s", method, resp);
        return -1;
    }
    return 0;
}

int roborock_start(RoborockDevice *dev) { return simple_cmd(dev, "app_start"); }
int roborock_stop(RoborockDevice *dev)  { return simple_cmd(dev, "app_stop");  }
int roborock_pause(RoborockDevice *dev) { return simple_cmd(dev, "app_pause"); }
int roborock_dock(RoborockDevice *dev)  { return simple_cmd(dev, "app_charge");}
int roborock_spot(RoborockDevice *dev)  { return simple_cmd(dev, "app_spot");  }
int roborock_find(RoborockDevice *dev)  { return simple_cmd(dev, "find_device");}

int roborock_set_fan(RoborockDevice *dev, int level)
{
    if (level < 101 || level > 105)
    {
        LOG_ERROR_MSG("roborock_set_fan: invalid level %d (101-105)", level);
        return -1;
    }
    char json[256];
    snprintf(json, sizeof(json),
             "{\"id\":%d,\"method\":\"set_custom_mode\",\"params\":[%d]}",
             g_request_id++, level);

    char resp[MIIO_MAX_PAYLOAD];
    if (send_recv(dev, json, resp, sizeof(resp)) != 0)
        return -1;

    if (!response_is_ok(resp))
    {
        LOG_ERROR_MSG("roborock_set_fan: unexpected response: %.128s", resp);
        return -1;
    }
    return 0;
}

int roborock_reset_consumable(RoborockDevice *dev, const char *item)
{
    if (!item || item[0] == '\0')
    {
        LOG_ERROR_MSG("roborock_reset_consumable: item is NULL or empty");
        return -1;
    }
    char json[256];
    snprintf(json, sizeof(json),
             "{\"id\":%d,\"method\":\"reset_consumable\",\"params\":[\"%s\"]}",
             g_request_id++, item);

    char resp[MIIO_MAX_PAYLOAD];
    if (send_recv(dev, json, resp, sizeof(resp)) != 0)
        return -1;

    if (!response_is_ok(resp))
    {
        LOG_ERROR_MSG("roborock_reset_consumable: unexpected response: %.128s", resp);
        return -1;
    }
    return 0;
}

/* ── Persistence ─────────────────────────────────────────────────────────── */

/** Convert 16 raw bytes to 32-char lowercase hex string (no NUL needed from caller). */
static void bytes_to_hex(const unsigned char *in, size_t len, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++)
    {
        out[i * 2]     = hex[(in[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[in[i] & 0xF];
    }
    out[len * 2] = '\0';
}

/** Parse 32-char hex string into 16 raw bytes. Returns 0 on success, -1 on error. */
static int hex_to_bytes(const char *hex, unsigned char *out, size_t out_len)
{
    size_t hex_len = strlen(hex);
    if (hex_len != out_len * 2)
        return -1;
    for (size_t i = 0; i < out_len; i++)
    {
        char hi = hex[i * 2];
        char lo = hex[i * 2 + 1];
        int h = (hi >= '0' && hi <= '9') ? hi - '0' :
                (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10 :
                (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10 : -1;
        int l = (lo >= '0' && lo <= '9') ? lo - '0' :
                (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10 :
                (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10 : -1;
        if (h < 0 || l < 0)
            return -1;
        out[i] = (unsigned char)((h << 4) | l);
    }
    return 0;
}

int roborock_save(const RoborockDevice *dev)
{
    if (!dev->token_valid)
    {
        LOG_ERROR_MSG("roborock_save: token not valid");
        return -1;
    }

    const char *home = getenv("HOME");
    if (!home)
        return -1;

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.config/home-appliances", home);
    (void)mkdir(dir, 0700);

    char path[600];
    snprintf(path, sizeof(path), ROBOROCK_STORE_FMT, home);

    /* Read existing contents */
    char existing[8192] = {0};
    {
        RAII_FILE FILE *rf = fopen(path, "r");
        if (rf)
        {
            size_t n = fread(existing, 1, sizeof(existing) - 1, rf);
            existing[n] = '\0';
        }
    }

    char token_hex[33];
    char dev_id_hex[9];
    bytes_to_hex(dev->token, 16, token_hex);
    snprintf(dev_id_hex, sizeof(dev_id_hex), "%08x", dev->device_id);

    /* Build new line */
    char new_line[128];
    snprintf(new_line, sizeof(new_line), "%s %s %s\n",
             dev->ip, token_hex, dev_id_hex);

    RAII_FILE FILE *wf = fopen(path, "w");
    if (!wf)
    {
        LOG_ERROR_MSG("roborock_save: cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    int written = 0;
    char *line = existing;
    char *nl;
    while (*line)
    {
        nl = strchr(line, '\n');
        size_t line_len = nl ? (size_t)(nl - line + 1) : strlen(line);
        char ip_buf[16] = {0};
        /* Parse leading IP from stored line */
        size_t i = 0;
        while (i < line_len && i < 15 && line[i] != ' ' && line[i] != '\n')
        {
            ip_buf[i] = line[i];
            i++;
        }
        ip_buf[i] = '\0';

        if (strcmp(ip_buf, dev->ip) == 0)
        {
            /* Replace this line */
            fputs(new_line, wf);
            written = 1;
        }
        else
        {
            fwrite(line, 1, line_len, wf);
        }
        line = nl ? nl + 1 : line + line_len;
    }

    if (!written)
        fputs(new_line, wf);

    return 0;
}

int roborock_load(const char *ip, RoborockDevice *dev)
{
    const char *home = getenv("HOME");
    if (!home)
        return -1;

    char path[600];
    snprintf(path, sizeof(path), ROBOROCK_STORE_FMT, home);

    RAII_FILE FILE *f = fopen(path, "r");
    if (!f)
        return 1; /* not found */

    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        char ip_buf[16]      = {0};
        char token_hex[33]   = {0};
        char dev_id_hex[9]   = {0};

        if (sscanf(line, "%15s %32s %8s", ip_buf, token_hex, dev_id_hex) != 3)
            continue;
        if (strcmp(ip_buf, ip) != 0)
            continue;

        /* Found matching entry */
        memset(dev, 0, sizeof(*dev));
        memcpy(dev->ip, ip_buf, sizeof(dev->ip) - 1);
        dev->ip[sizeof(dev->ip) - 1] = '\0';

        if (hex_to_bytes(token_hex, dev->token, 16) != 0)
        {
            LOG_ERROR_MSG("roborock_load: invalid token hex for %s", ip);
            return -1;
        }

        unsigned long id = strtoul(dev_id_hex, NULL, 16);
        dev->device_id  = (unsigned int)id;
        dev->token_valid = 1;
        return 0;
    }

    return 1; /* not found */
}
