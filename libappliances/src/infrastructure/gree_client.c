#include "gree_client.h"
#include "../core/base64.h"
#include "../core/logger.h"
#include "../core/raii.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define GREE_UNICAST_TIMEOUT_MS 5000
#define GREE_STORE_FMT          "%s/.config/home-appliances/gree_devices"

/* Status columns requested and their fixed positions in the dat array. */
#define GREE_STATUS_COLS \
    "\"Pow\",\"SetTem\",\"TemSen\",\"Mod\",\"Lig\",\"SwUpDn\"," \
    "\"Wnd\",\"Quiet\",\"Tur\",\"SvSt\",\"TemUn\""
#define GREE_STATUS_COL_COUNT 11

/* ── RAII cleanup helpers ─────────────────────────────────────────────── */

static void close_fd(int *p)          { if (*p >= 0) close(*p); }
static void free_evp(EVP_CIPHER_CTX **p) { EVP_CIPHER_CTX_free(*p); }

/* ── Buffer append helpers ───────────────────────────────────────────── */

static int buf_append(char *buf, size_t *pos, size_t cap, const char *s)
{
    size_t len = strlen(s);
    if (*pos + len >= cap)
        return -1;
    memcpy(buf + *pos, s, len);
    *pos += len;
    buf[*pos] = '\0';
    return 0;
}

__attribute__((format(printf, 4, 5)))
static int buf_appendf(char *buf, size_t *pos, size_t cap,
                       const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *pos, cap - *pos, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - *pos)
        return -1;
    *pos += (size_t)n;
    return 0;
}

/* ── AES-128-ECB via OpenSSL EVP ─────────────────────────────────────── */

/* Encrypt plaintext (must be pre-padded to 16-byte multiple) with a 16-byte key. */
static int aes128_encrypt(const unsigned char *key,
                          const unsigned char *plain, size_t plain_len,
                          unsigned char *out, int *out_len)
{
    EVP_CIPHER_CTX *ctx RAII_WITH_CLEANUP(free_evp) = EVP_CIPHER_CTX_new();
    if (!ctx)
        return -1;
    if (!EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL))
        return -1;
    EVP_CIPHER_CTX_set_padding(ctx, 0); /* we handle zero-padding ourselves */

    int outl = 0, finl = 0;
    if (!EVP_EncryptUpdate(ctx, out, &outl, plain, (int)plain_len))
        return -1;
    if (!EVP_EncryptFinal_ex(ctx, out + outl, &finl))
        return -1;
    *out_len = outl + finl;
    return 0;
}

/* Decrypt ciphertext (multiple of 16 bytes) with a 16-byte key. */
static int aes128_decrypt(const unsigned char *key,
                          const unsigned char *cipher, size_t cipher_len,
                          unsigned char *out, int *out_len)
{
    EVP_CIPHER_CTX *ctx RAII_WITH_CLEANUP(free_evp) = EVP_CIPHER_CTX_new();
    if (!ctx)
        return -1;
    if (!EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL))
        return -1;
    EVP_CIPHER_CTX_set_padding(ctx, 0);

    int outl = 0, finl = 0;
    if (!EVP_DecryptUpdate(ctx, out, &outl, cipher, (int)cipher_len))
        return -1;
    if (!EVP_DecryptFinal_ex(ctx, out + outl, &finl))
        return -1;
    *out_len = outl + finl;
    return 0;
}

/* ── Gree pack encrypt / decrypt ─────────────────────────────────────── */

/* Encrypt a plain-text JSON string as a Base64-encoded AES pack field.
 * key_str: 16-char ASCII key. */
static int gree_pack_encrypt(const char *key_str, const char *plain,
                             char *b64_out, size_t b64_out_size)
{
    size_t plain_len = strlen(plain);
    size_t padded_len = (plain_len + 15u) / 16u * 16u;
    if (padded_len > 512u)
        return -1;

    unsigned char padded[512] = {0};
    memcpy(padded, plain, plain_len);

    unsigned char enc[512];
    int enc_len = 0;
    if (aes128_encrypt((const unsigned char *)key_str, padded, padded_len,
                       enc, &enc_len) != 0)
        return -1;

    return base64_encode(enc, (size_t)enc_len, b64_out, b64_out_size);
}

/* Decrypt a Base64-encoded AES pack field to plain-text JSON.
 * key_str: 16-char ASCII key. */
static int gree_pack_decrypt(const char *key_str, const char *b64_in,
                             char *out, size_t out_size)
{
    unsigned char enc[512];
    int enc_len = base64_decode(b64_in, enc, sizeof(enc));
    if (enc_len < 0 || (size_t)enc_len % 16u != 0)
        return -1;

    unsigned char dec[512];
    int dec_len = 0;
    if (aes128_decrypt((const unsigned char *)key_str, enc, (size_t)enc_len,
                       dec, &dec_len) != 0)
        return -1;

    /* Strip trailing null bytes */
    while (dec_len > 0 && dec[dec_len - 1] == '\0')
        dec_len--;

    if ((size_t)dec_len >= out_size)
        return -1;
    memcpy(out, dec, (size_t)dec_len);
    out[dec_len] = '\0';
    return 0;
}

/* ── Minimal JSON field extraction ──────────────────────────────────── */

/* Extract a string value for key from a flat JSON object.
 * Searches for the pattern "key":"<value>". */
static int json_str_get(const char *json, const char *key,
                        char *out, size_t out_len)
{
    char pat[80];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char *pos = strstr(json, pat);
    if (!pos)
        return -1;
    pos += strlen(pat);
    const char *end = strchr(pos, '"');
    if (!end)
        return -1;
    size_t len = (size_t)(end - pos);
    if (len >= out_len)
        len = out_len - 1;
    memcpy(out, pos, len);
    out[len] = '\0';
    return 0;
}

/* Extract an integer value for key from a flat JSON object.
 * Searches for the pattern "key":<integer>. */
static int json_int_get(const char *json, const char *key, int *out)
{
    char pat[80];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *pos = strstr(json, pat);
    if (!pos)
        return -1;
    pos += strlen(pat);
    while (*pos == ' ')
        pos++;
    if (*pos != '-' && (*pos < '0' || *pos > '9'))
        return -1;
    char *end;
    *out = (int)strtol(pos, &end, 10);
    return (end > pos) ? 0 : -1;
}

/* Parse an integer array for key from JSON.
 * Example: "dat":[1,24,26,...] */
static int json_int_array(const char *json, const char *key,
                          int *arr, int max_count)
{
    char pat[80];
    snprintf(pat, sizeof(pat), "\"%s\":[", key);
    const char *pos = strstr(json, pat);
    if (!pos)
        return -1;
    pos += strlen(pat);

    int count = 0;
    while (count < max_count && *pos != ']' && *pos != '\0') {
        while (*pos == ' ')
            pos++;
        if (*pos == ']' || *pos == '\0')
            break;
        char *end;
        arr[count++] = (int)strtol(pos, &end, 10);
        if (end == pos)
            break;
        pos = end;
        while (*pos == ' ')
            pos++;
        if (*pos == ',')
            pos++;
    }
    return count;
}

/* ── UDP socket helpers ──────────────────────────────────────────────── */

static int udp_socket_create(int timeout_ms)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOG_ERROR_MSG("socket(): %s", strerror(errno));
        return -1;
    }

    int bcast = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast));

    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

/* Send msg to dest_ip:GREE_PORT and receive one response into resp. */
static int gree_send_recv(const char *dest_ip, const char *msg,
                          char *resp, size_t resp_size)
{
    int fd RAII_WITH_CLEANUP(close_fd) = udp_socket_create(GREE_UNICAST_TIMEOUT_MS);
    if (fd < 0)
        return -1;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(GREE_PORT);
    inet_pton(AF_INET, dest_ip, &addr.sin_addr);

    if (sendto(fd, msg, strlen(msg), 0,
               (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR_MSG("sendto(%s): %s", dest_ip, strerror(errno));
        return -1;
    }

    ssize_t n = recvfrom(fd, resp, resp_size - 1, 0, NULL, NULL);
    if (n < 0) {
        LOG_ERROR_MSG("recvfrom(%s): %s", dest_ip, strerror(errno));
        return -1;
    }
    resp[n] = '\0';
    return 0;
}

/* ── Public API ──────────────────────────────────────────────────────── */

int gree_client_scan(const char *broadcast, int timeout_ms, GreeDeviceList *out)
{
    out->devices = NULL;
    out->count   = 0;

    int fd RAII_WITH_CLEANUP(close_fd) = udp_socket_create(timeout_ms);
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
        return -1;
    }

    int capacity = 8;
    out->devices = malloc((size_t)capacity * sizeof(GreeDevice));
    if (!out->devices)
        return -1;

    char buf[2048];
    struct sockaddr_in sender = {0};
    socklen_t sender_len = sizeof(sender);

    while (1) {
        ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&sender, &sender_len);
        if (n < 0)
            break; /* timeout or no more responses */
        buf[n] = '\0';

        char pack_b64[1024] = {0};
        if (json_str_get(buf, "pack", pack_b64, sizeof(pack_b64)) != 0) {
            LOG_WARN_MSG("Gree scan: no pack field, skipping");
            continue;
        }

        char pack_plain[1024] = {0};
        if (gree_pack_decrypt(GREE_DISCOVERY_KEY, pack_b64,
                              pack_plain, sizeof(pack_plain)) != 0) {
            LOG_WARN_MSG("Gree scan: decrypt failed, skipping");
            continue;
        }
        LOG_DEBUG_MSG("Gree scan decrypted: %.200s", pack_plain);

        if (out->count >= capacity) {
            capacity *= 2;
            GreeDevice *tmp = realloc(out->devices,
                                      (size_t)capacity * sizeof(GreeDevice));
            if (!tmp)
                break;
            out->devices = tmp;
        }

        GreeDevice *dev = &out->devices[out->count++];
        memset(dev, 0, sizeof(*dev));
        inet_ntop(AF_INET, &sender.sin_addr, dev->ip, sizeof(dev->ip));
        json_str_get(pack_plain, "cid",  dev->mac,      sizeof(dev->mac));
        json_str_get(pack_plain, "name", dev->name,     sizeof(dev->name));
        json_str_get(pack_plain, "ver",  dev->firmware, sizeof(dev->firmware));
    }

    LOG_INFO_MSG("Gree scan done: %d device(s) found", out->count);
    return 0;
}

int gree_client_bind(GreeDevice *dev)
{
    /* Build and encrypt inner pack */
    char inner[256] = {0};
    snprintf(inner, sizeof(inner),
             "{\"mac\":\"%s\",\"t\":\"bind\",\"uid\":0}", dev->mac);

    char pack_b64[512] = {0};
    if (gree_pack_encrypt(GREE_DISCOVERY_KEY, inner,
                          pack_b64, sizeof(pack_b64)) < 0) {
        LOG_ERROR_MSG("gree_client_bind: encrypt failed");
        return -1;
    }

    /* Build outer message */
    char msg[1024] = {0};
    snprintf(msg, sizeof(msg),
             "{\"cid\":\"app\",\"i\":1,\"pack\":\"%s\","
             "\"t\":\"pack\",\"tcid\":\"%s\",\"uid\":0}",
             pack_b64, dev->mac);

    /* Send and receive */
    char resp[2048] = {0};
    if (gree_send_recv(dev->ip, msg, resp, sizeof(resp)) != 0)
        return -1;
    LOG_DEBUG_MSG("Gree bind response: %.200s", resp);

    /* Decrypt response */
    char resp_b64[1024] = {0};
    if (json_str_get(resp, "pack", resp_b64, sizeof(resp_b64)) != 0) {
        LOG_ERROR_MSG("gree_client_bind: no pack in response");
        return -1;
    }
    char plain[1024] = {0};
    if (gree_pack_decrypt(GREE_DISCOVERY_KEY, resp_b64,
                          plain, sizeof(plain)) != 0) {
        LOG_ERROR_MSG("gree_client_bind: decrypt response failed");
        return -1;
    }
    LOG_DEBUG_MSG("Gree bind decrypted: %.200s", plain);

    char t[16] = {0};
    json_str_get(plain, "t", t, sizeof(t));
    if (strcmp(t, "bindok") != 0) {
        LOG_ERROR_MSG("gree_client_bind: unexpected response type '%s'", t);
        return -1;
    }

    if (json_str_get(plain, "key", dev->device_key,
                     sizeof(dev->device_key)) != 0) {
        LOG_ERROR_MSG("gree_client_bind: no key in response");
        return -1;
    }
    dev->bound = 1;
    LOG_INFO_MSG("Gree bind OK: %s key=%.4s****", dev->ip, dev->device_key);
    return 0;
}

int gree_client_get_status(const GreeDevice *dev, GreeStatus *out)
{
    if (!dev->bound) {
        LOG_ERROR_MSG("gree_client_get_status: %s not bound", dev->ip);
        return -1;
    }

    char inner[512] = {0};
    snprintf(inner, sizeof(inner),
             "{\"cols\":[" GREE_STATUS_COLS "],\"mac\":\"%s\",\"t\":\"status\"}",
             dev->mac);

    char pack_b64[512] = {0};
    if (gree_pack_encrypt(dev->device_key, inner,
                          pack_b64, sizeof(pack_b64)) < 0)
        return -1;

    char msg[1024] = {0};
    snprintf(msg, sizeof(msg),
             "{\"cid\":\"app\",\"i\":0,\"pack\":\"%s\","
             "\"t\":\"pack\",\"tcid\":\"%s\",\"uid\":0}",
             pack_b64, dev->mac);

    char resp[2048] = {0};
    if (gree_send_recv(dev->ip, msg, resp, sizeof(resp)) != 0)
        return -1;

    char resp_b64[1024] = {0};
    if (json_str_get(resp, "pack", resp_b64, sizeof(resp_b64)) != 0)
        return -1;
    char plain[1024] = {0};
    if (gree_pack_decrypt(dev->device_key, resp_b64,
                          plain, sizeof(plain)) != 0)
        return -1;
    LOG_DEBUG_MSG("Gree status: %.200s", plain);

    /* dat array: Pow SetTem TemSen Mod Lig SwUpDn Wnd Quiet Tur SvSt TemUn */
    int dat[GREE_STATUS_COL_COUNT] = {0};
    int n = json_int_array(plain, "dat", dat, GREE_STATUS_COL_COUNT);
    if (n < GREE_STATUS_COL_COUNT) {
        LOG_ERROR_MSG("gree_client_get_status: short dat array (%d)", n);
        return -1;
    }

    out->power     =  dat[0];
    out->set_temp  =  dat[1];
    out->room_temp =  dat[2];
    out->mode      = (GreeMode)dat[3];
    out->lights    =  dat[4];
    out->swing     =  dat[5];
    out->fan       = (GreeFanSpeed)dat[6];
    out->quiet     =  dat[7];
    out->turbo     =  dat[8];
    out->sleep     =  dat[9];
    out->temp_unit = (GreeTempUnit)dat[10];
    return 0;
}

int gree_client_set(const GreeDevice *dev, const char **keys,
                    const int *values, int count)
{
    if (!dev->bound) {
        LOG_ERROR_MSG("gree_client_set: %s not bound", dev->ip);
        return -1;
    }
    if (count <= 0 || count > 16) {
        LOG_ERROR_MSG("gree_client_set: invalid count %d", count);
        return -1;
    }

    /* Build opt and p arrays */
    char opt[256] = {0};
    char p[128]   = {0};
    size_t opos = 0, ppos = 0;

    if (buf_append(opt, &opos, sizeof(opt), "[") != 0) return -1;
    if (buf_append(p,   &ppos, sizeof(p),   "[") != 0) return -1;

    for (int i = 0; i < count; i++) {
        if (i > 0) {
            if (buf_append(opt, &opos, sizeof(opt), ",") != 0) return -1;
            if (buf_append(p,   &ppos, sizeof(p),   ",") != 0) return -1;
        }
        if (buf_appendf(opt, &opos, sizeof(opt), "\"%s\"", keys[i])  != 0) return -1;
        if (buf_appendf(p,   &ppos, sizeof(p),   "%d",    values[i]) != 0) return -1;
    }
    if (buf_append(opt, &opos, sizeof(opt), "]") != 0) return -1;
    if (buf_append(p,   &ppos, sizeof(p),   "]") != 0) return -1;

    char inner[512] = {0};
    snprintf(inner, sizeof(inner),
             "{\"opt\":%s,\"p\":%s,\"t\":\"cmd\"}", opt, p);

    char pack_b64[512] = {0};
    if (gree_pack_encrypt(dev->device_key, inner,
                          pack_b64, sizeof(pack_b64)) < 0)
        return -1;

    char msg[1024] = {0};
    snprintf(msg, sizeof(msg),
             "{\"cid\":\"app\",\"i\":0,\"pack\":\"%s\","
             "\"t\":\"pack\",\"tcid\":\"%s\",\"uid\":0}",
             pack_b64, dev->mac);

    char resp[2048] = {0};
    if (gree_send_recv(dev->ip, msg, resp, sizeof(resp)) != 0)
        return -1;

    /* Check device return code if available */
    char resp_b64[1024] = {0};
    char plain[1024] = {0};
    if (json_str_get(resp, "pack", resp_b64, sizeof(resp_b64)) == 0 &&
        gree_pack_decrypt(dev->device_key, resp_b64, plain, sizeof(plain)) == 0) {
        int r = 0;
        if (json_int_get(plain, "r", &r) == 0 && r != 200)
            LOG_WARN_MSG("gree_client_set: device returned r=%d", r);
    }

    LOG_INFO_MSG("gree_client_set: sent %d param(s) to %s", count, dev->ip);
    return 0;
}

/* ── Persistence ─────────────────────────────────────────────────────── */

int gree_client_load_bound(GreeDeviceList *out)
{
    out->devices = NULL;
    out->count   = 0;

    const char *home = getenv("HOME");
    if (!home)
        return -1;

    char path[512];
    snprintf(path, sizeof(path), GREE_STORE_FMT, home);

    RAII_FILE FILE *f = fopen(path, "r");
    if (!f)
        return 0; /* not found is fine — first run */

    int capacity = 8;
    out->devices = malloc((size_t)capacity * sizeof(GreeDevice));
    if (!out->devices)
        return -1;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;

        if (out->count >= capacity) {
            capacity *= 2;
            GreeDevice *tmp = realloc(out->devices,
                                      (size_t)capacity * sizeof(GreeDevice));
            if (!tmp)
                break;
            out->devices = tmp;
        }

        GreeDevice *dev = &out->devices[out->count];
        memset(dev, 0, sizeof(*dev));

        char key[GREE_KEY_LEN] = {0};
        if (sscanf(line, "%15s %12s %16s", dev->ip, dev->mac, key) == 3
            && strlen(key) == 16) {
            memcpy(dev->device_key, key, GREE_KEY_LEN);
            dev->bound = 1;
            out->count++;
        }
    }
    return 0;
}

int gree_client_save_bound(const GreeDeviceList *list)
{
    const char *home = getenv("HOME");
    if (!home)
        return -1;

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.config/home-appliances", home);
    (void)mkdir(dir, 0700); /* ignore error if already exists */

    char path[600];
    snprintf(path, sizeof(path), GREE_STORE_FMT, home);

    RAII_FILE FILE *f = fopen(path, "w");
    if (!f) {
        LOG_ERROR_MSG("Cannot write gree device store: %s", path);
        return -1;
    }

    fprintf(f, "# ip mac device_key\n");
    for (int i = 0; i < list->count; i++) {
        const GreeDevice *dev = &list->devices[i];
        if (dev->bound && strlen(dev->device_key) == 16)
            fprintf(f, "%s %s %s\n", dev->ip, dev->mac, dev->device_key);
    }
    return 0;
}

void gree_client_merge_bound(GreeDeviceList *scanned, const GreeDeviceList *saved)
{
    for (int i = 0; i < scanned->count; i++) {
        GreeDevice *dev = &scanned->devices[i];
        for (int j = 0; j < saved->count; j++) {
            const GreeDevice *s = &saved->devices[j];
            if (strncmp(dev->mac, s->mac, GREE_MAC_LEN - 1) == 0) {
                memcpy(dev->device_key, s->device_key, sizeof(dev->device_key));
                dev->bound = 1;
                break;
            }
        }
    }
}

void gree_device_list_free(GreeDeviceList *list)
{
    free(list->devices);
    list->devices = NULL;
    list->count   = 0;
}
