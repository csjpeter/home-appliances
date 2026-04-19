/**
 * @file test_gree_functional.c
 * @brief Functional tests for gree_client using a UDP mock server.
 *
 * Compile with -DGREE_PORT=17000 so the client connects to the local mock
 * instead of the real device port.
 *
 * Link with: -lpthread -lssl -lcrypto
 */

#include "../../tests/common/test_helpers.h"
#include "../../tests/common/mock_server.h"
#include "../../libappliances/src/infrastructure/gree_client.h"
#include "../../libappliances/src/core/base64.h"

#include <pthread.h>
#include <openssl/evp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

int g_tests_run    = 0;
int g_tests_failed = 0;

/* ── AES-128-ECB helpers (mirror of gree_client.c static functions) ──── */

static int mock_evp_free_ctx(EVP_CIPHER_CTX **p)
{
    EVP_CIPHER_CTX_free(*p);
    return 0;
}

static int mock_raw_aes128_encrypt(const unsigned char *key,
                                   const unsigned char *plain, size_t plain_len,
                                   unsigned char *out, int *out_len)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return -1;
    int rc = -1;
    do {
        if (!EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL))
            break;
        EVP_CIPHER_CTX_set_padding(ctx, 0);
        int outl = 0, finl = 0;
        if (!EVP_EncryptUpdate(ctx, out, &outl, plain, (int)plain_len))
            break;
        if (!EVP_EncryptFinal_ex(ctx, out + outl, &finl))
            break;
        *out_len = outl + finl;
        rc = 0;
    } while (0);
    mock_evp_free_ctx(&ctx);
    return rc;
}

static int mock_raw_aes128_decrypt(const unsigned char *key,
                                   const unsigned char *cipher, size_t cipher_len,
                                   unsigned char *out, int *out_len)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return -1;
    int rc = -1;
    do {
        if (!EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL))
            break;
        EVP_CIPHER_CTX_set_padding(ctx, 0);
        int outl = 0, finl = 0;
        if (!EVP_DecryptUpdate(ctx, out, &outl, cipher, (int)cipher_len))
            break;
        if (!EVP_DecryptFinal_ex(ctx, out + outl, &finl))
            break;
        *out_len = outl + finl;
        rc = 0;
    } while (0);
    mock_evp_free_ctx(&ctx);
    return rc;
}

/* Encrypt JSON string with 16-char ASCII key → Base64 output. */
static int mock_aes128_encrypt(const char *key, const char *plain,
                                char *b64_out, size_t b64_out_size)
{
    size_t plain_len  = strlen(plain);
    size_t padded_len = (plain_len + 15u) / 16u * 16u;
    if (padded_len > 1024u)
        return -1;

    unsigned char padded[1024] = {0};
    memcpy(padded, plain, plain_len);

    unsigned char enc[1024];
    int enc_len = 0;
    if (mock_raw_aes128_encrypt((const unsigned char *)key, padded, padded_len,
                                enc, &enc_len) != 0)
        return -1;

    return base64_encode(enc, (size_t)enc_len, b64_out, b64_out_size);
}

/* Decrypt Base64-encoded AES pack → JSON string. */
static int mock_aes128_decrypt(const char *key, const char *b64_in,
                                char *plain_out, size_t plain_out_size)
{
    unsigned char enc[1024];
    int enc_len = base64_decode(b64_in, enc, sizeof(enc));
    if (enc_len < 0 || (size_t)enc_len % 16u != 0)
        return -1;

    unsigned char dec[1024];
    int dec_len = 0;
    if (mock_raw_aes128_decrypt((const unsigned char *)key, enc, (size_t)enc_len,
                                dec, &dec_len) != 0)
        return -1;

    while (dec_len > 0 && dec[dec_len - 1] == '\0')
        dec_len--;

    if ((size_t)dec_len >= plain_out_size)
        return -1;
    memcpy(plain_out, dec, (size_t)dec_len);
    plain_out[dec_len] = '\0';
    return 0;
}

/* ── Minimal JSON field helpers for mock use ─────────────────────────── */

static int mock_json_str_get(const char *json, const char *key,
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

/* ── Mock state ──────────────────────────────────────────────────────── */

#define MOCK_DEVICE_MAC "aabbccddee01"
#define MOCK_DEVICE_KEY "0123456789abcdef"
#define MOCK_DEVICE_NAME "GreeTestAC"

/* Status dat that the mock will return (17 values, matching GREE_STATUS_COLS order).
 * power=1, SetTem=22, TemSen=65, Mod=1(cool), Lig=1, SwUpDn=1,
 * WdSpd=2, Quiet=0, Tur=0, SwhSlp=0, TemUn=0, SwingLfRig=3,
 * Blo=1, Air=0, Health=1, StHt=0, HeatCoolType=1 */
static const char MOCK_STATUS_RESPONSE[] =
    "{\"cols\":[\"Pow\",\"SetTem\",\"TemSen\",\"Mod\",\"Lig\",\"SwUpDn\","
    "\"WdSpd\",\"Quiet\",\"Tur\",\"SwhSlp\",\"TemUn\","
    "\"SwingLfRig\",\"Blo\",\"Air\",\"Health\",\"StHt\",\"HeatCoolType\"],"
    "\"dat\":[1,22,65,1,1,1,2,0,0,0,0,3,1,0,1,0,1],\"t\":\"dat\"}";

typedef struct
{
    int  phase;             /* which exchange to serve next (0-based) */
    char last_set_key[64];
    int  last_set_val;
} GreeTestState;

static GreeTestState  g_state;
static volatile int   mock_ready = 0;

/* ── UDP helpers for mock ─────────────────────────────────────────────── */

/* Send a plain-JSON response encrypted with key back to sender. */
static void mock_send_encrypted(int fd, const struct sockaddr_in *to,
                                 const char *key, const char *inner_json)
{
    char pack_b64[2048] = {0};
    if (mock_aes128_encrypt(key, inner_json, pack_b64, sizeof(pack_b64)) < 0)
        return;

    char outer[4096] = {0};
    snprintf(outer, sizeof(outer),
             "{\"pack\":\"%s\",\"t\":\"pack\"}", pack_b64);

    sendto(fd, outer, strlen(outer), 0,
           (const struct sockaddr *)to, sizeof(*to));
}

/* ── Mock server thread ───────────────────────────────────────────────── */

/*
 * Handles up to 3 UDP exchanges. The phase field in GreeTestState controls
 * which scenario is active:
 *   phase 0 — scan response
 *   phase 1 — bind response
 *   phase 2 — status query response
 *   phase 3 — set command response
 *
 * The mock serves exactly the number of exchanges needed by the test that
 * launched it. Each test resets g_state.phase before spawning the thread.
 */

typedef struct
{
    int exchanges; /* how many UDP exchanges the mock should serve */
} MockArg;

static void *mock_gree_server(void *arg)
{
    MockArg *ma = (MockArg *)arg;
    int      exchanges = ma->exchanges;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return NULL;

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(MOCK_GREE_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return NULL;
    }

    mock_ready = 1;

    char buf[4096];
    for (int ex = 0; ex < exchanges; ex++) {
        struct sockaddr_in sender = {0};
        socklen_t slen = sizeof(sender);
        ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&sender, &slen);
        if (n <= 0)
            break;
        buf[n] = '\0';

        /* Determine message type from outer JSON */
        const char *t_scan = strstr(buf, "\"t\":\"scan\"");
        const char *t_pack = strstr(buf, "\"t\":\"pack\"");

        if (t_scan) {
            /* Scan: respond with device info encrypted with discovery key */
            char inner[512];
            snprintf(inner, sizeof(inner),
                     "{\"t\":\"dev\",\"cid\":\"%s\",\"bc\":\"b\","
                     "\"brand\":\"gree\",\"catalog\":\"gree\","
                     "\"mac\":\"%s\",\"mid\":\"10001\",\"model\":\"gree\","
                     "\"name\":\"%s\",\"series\":\"gree\","
                     "\"ver\":\"V1.1.13\",\"lock\":0}",
                     MOCK_DEVICE_MAC, MOCK_DEVICE_MAC, MOCK_DEVICE_NAME);
            mock_send_encrypted(fd, &sender, GREE_DISCOVERY_KEY, inner);

        } else if (t_pack) {
            /* Decrypt the inner pack to find out what request type it is */
            char pack_b64[2048] = {0};
            if (mock_json_str_get(buf, "pack", pack_b64, sizeof(pack_b64)) != 0)
                continue;

            /* Try discovery key first (bind), then device key (status/set).
             * AES-ECB decryption never returns a crypto error with a wrong key;
             * it just produces garbage.  Validate the result starts with '{'
             * (basic JSON sanity) before committing to that key. */
            char plain[2048] = {0};
            const char *used_key = NULL;
            if (mock_aes128_decrypt(GREE_DISCOVERY_KEY, pack_b64,
                                    plain, sizeof(plain)) == 0
                && plain[0] == '{') {
                used_key = GREE_DISCOVERY_KEY;
            } else if (mock_aes128_decrypt(MOCK_DEVICE_KEY, pack_b64,
                                           plain, sizeof(plain)) == 0
                       && plain[0] == '{') {
                used_key = MOCK_DEVICE_KEY;
            } else {
                continue; /* can't decrypt to valid JSON */
            }

            /* Inspect the decrypted type field */
            char inner_t[32] = {0};
            mock_json_str_get(plain, "t", inner_t, sizeof(inner_t));

            if (strcmp(inner_t, "bind") == 0) {
                /* Bind response */
                char resp[256];
                snprintf(resp, sizeof(resp),
                         "{\"t\":\"bindok\",\"mac\":\"%s\","
                         "\"key\":\"%s\"}",
                         MOCK_DEVICE_MAC, MOCK_DEVICE_KEY);
                mock_send_encrypted(fd, &sender, GREE_DISCOVERY_KEY, resp);

            } else if (strcmp(inner_t, "status") == 0) {
                /* Status response — send pre-built dat array */
                mock_send_encrypted(fd, &sender, MOCK_DEVICE_KEY,
                                    MOCK_STATUS_RESPONSE);

            } else if (strcmp(inner_t, "cmd") == 0) {
                /* Set (cmd) response — record what was set */
                /* Extract first opt name and first p value */
                const char *opt_pos = strstr(plain, "\"opt\":[\"");
                if (opt_pos) {
                    opt_pos += strlen("\"opt\":[\"");
                    const char *opt_end = strchr(opt_pos, '"');
                    if (opt_end) {
                        size_t klen = (size_t)(opt_end - opt_pos);
                        if (klen >= sizeof(g_state.last_set_key))
                            klen = sizeof(g_state.last_set_key) - 1;
                        memcpy(g_state.last_set_key, opt_pos, klen);
                        g_state.last_set_key[klen] = '\0';
                    }
                }
                const char *p_pos = strstr(plain, "\"p\":[");
                if (p_pos) {
                    p_pos += strlen("\"p\":[");
                    g_state.last_set_val = (int)strtol(p_pos, NULL, 10);
                }

                /* Respond with standard success */
                char resp[256];
                snprintf(resp, sizeof(resp),
                         "{\"opt\":[\"%s\"],\"p\":[%d],"
                         "\"r\":200,\"t\":\"res\"}",
                         g_state.last_set_key, g_state.last_set_val);
                mock_send_encrypted(fd, &sender, MOCK_DEVICE_KEY, resp);
            }
            (void)used_key; /* suppress unused warning */
        }
    }

    close(fd);
    return NULL;
}

/* ── Helper: launch mock, run one test function, join ─────────────────── */

typedef void (*TestFn)(GreeDevice *dev);

static void run_with_mock(TestFn fn, int exchanges)
{
    memset(&g_state, 0, sizeof(g_state));
    mock_ready = 0;

    MockArg ma = { .exchanges = exchanges };
    pthread_t tid;
    pthread_create(&tid, NULL, mock_gree_server, &ma);
    MOCK_WAIT_READY(mock_ready);

    /* Provide a pre-configured device pointing at 127.0.0.1 */
    GreeDevice dev = {0};
    /* Use memcpy to avoid -Werror=stringop-truncation on fixed-size fields. */
    memcpy(dev.ip,         MOCK_HOST,       strlen(MOCK_HOST));
    memcpy(dev.mac,        MOCK_DEVICE_MAC, strlen(MOCK_DEVICE_MAC));
    memcpy(dev.device_key, MOCK_DEVICE_KEY, strlen(MOCK_DEVICE_KEY));
    dev.bound = 1;

    fn(&dev);

    pthread_join(tid, NULL);
}

/* ── Individual tests ─────────────────────────────────────────────────── */

/* ---------- test_scan ---------- */
static void *mock_scan_server(void *arg)
{
    (void)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return NULL;

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(MOCK_GREE_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return NULL;
    }

    mock_ready = 1;

    char buf[4096];
    struct sockaddr_in sender = {0};
    socklen_t slen = sizeof(sender);
    ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0,
                         (struct sockaddr *)&sender, &slen);
    if (n > 0) {
        buf[n] = '\0';
        if (strstr(buf, "\"t\":\"scan\"")) {
            char inner[512];
            snprintf(inner, sizeof(inner),
                     "{\"t\":\"dev\",\"cid\":\"%s\",\"bc\":\"b\","
                     "\"brand\":\"gree\",\"catalog\":\"gree\","
                     "\"mac\":\"%s\",\"mid\":\"10001\",\"model\":\"gree\","
                     "\"name\":\"%s\",\"series\":\"gree\","
                     "\"ver\":\"V1.1.13\",\"lock\":0}",
                     MOCK_DEVICE_MAC, MOCK_DEVICE_MAC, MOCK_DEVICE_NAME);
            mock_send_encrypted(fd, &sender, GREE_DISCOVERY_KEY, inner);
        }
    }

    close(fd);
    return NULL;
}

static void test_scan(void)
{
    mock_ready = 0;
    pthread_t tid;
    pthread_create(&tid, NULL, mock_scan_server, NULL);
    MOCK_WAIT_READY(mock_ready);

    GreeDeviceList list = {0};
    int rc = gree_client_scan(MOCK_HOST, 500, &list);

    pthread_join(tid, NULL);

    ASSERT(rc == 0,             "scan returns 0");
    ASSERT(list.count == 1,     "scan found 1 device");
    if (list.count == 1) {
        ASSERT(strcmp(list.devices[0].mac,  MOCK_DEVICE_MAC)  == 0,
               "scan: correct MAC");
        ASSERT(strcmp(list.devices[0].name, MOCK_DEVICE_NAME) == 0,
               "scan: correct name");
    }
    gree_device_list_free(&list);
}

/* ---------- test_bind ---------- */
static void do_test_bind(GreeDevice *dev)
{
    /* Reset bound so we can test the bind call */
    dev->bound = 0;
    memset(dev->device_key, 0, sizeof(dev->device_key));

    int rc = gree_client_bind(dev);
    ASSERT(rc == 0,   "bind returns 0");
    ASSERT(dev->bound == 1, "bind sets bound=1");
    ASSERT(strcmp(dev->device_key, MOCK_DEVICE_KEY) == 0,
           "bind sets correct device_key");
}

static void test_bind(void)
{
    run_with_mock(do_test_bind, 1);
}

/* ---------- test_get_status ---------- */
static void do_test_get_status(GreeDevice *dev)
{
    GreeStatus st = {0};
    int rc = gree_client_get_status(dev, &st);
    ASSERT(rc == 0,          "get_status returns 0");
    ASSERT(st.power   == 1,  "power=1");
    ASSERT(st.set_temp == 22, "set_temp=22");
    /* TemSen=65 → 65-40=25 */
    ASSERT(st.room_temp == 25, "room_temp=25 (TemSen offset applied)");
    ASSERT(st.mode == GREE_MODE_COOL, "mode=COOL");
    ASSERT(st.lights  == 1,  "lights=1");
    ASSERT(st.swing_v == 1,  "swing_v=1");
    ASSERT(st.fan == GREE_FAN_MED, "fan=MED(2)");
    ASSERT(st.swing_h == 3,  "swing_h=3");
    ASSERT(st.xfan    == 1,  "xfan=1");
    ASSERT(st.health   == 1, "health=1");
    ASSERT(st.heat_cool_type == 1, "heat_cool_type=1");
}

static void test_get_status(void)
{
    run_with_mock(do_test_get_status, 1);
}

/* ---------- test_temsen_offset ---------- */
static void test_temsen_offset(void)
{
    /* TemSen=65 → room_temp=25 */
    int raw1 = 65;
    int decoded1 = (raw1 > 60) ? (raw1 - 40) : raw1;
    ASSERT(decoded1 == 25, "TemSen 65 → 25");

    /* TemSen=25 → room_temp=25 (no offset for normal range) */
    int raw2 = 25;
    int decoded2 = (raw2 > 60) ? (raw2 - 40) : raw2;
    ASSERT(decoded2 == 25, "TemSen 25 → 25 (pass-through)");

    /* TemSen=60 → 60 (boundary: not > 60) */
    int raw3 = 60;
    int decoded3 = (raw3 > 60) ? (raw3 - 40) : raw3;
    ASSERT(decoded3 == 60, "TemSen 60 → 60 (boundary, no offset)");

    /* TemSen=61 → 21 (just above boundary) */
    int raw4 = 61;
    int decoded4 = (raw4 > 60) ? (raw4 - 40) : raw4;
    ASSERT(decoded4 == 21, "TemSen 61 → 21 (offset applied)");
}

/* ---------- test_set_power ---------- */
static void do_test_set_power(GreeDevice *dev)
{
    const char *keys[]  = { "Pow" };
    const int   vals[]  = { 1 };
    int rc = gree_client_set(dev, keys, vals, 1);
    ASSERT(rc == 0, "set_power returns 0");
    ASSERT(strcmp(g_state.last_set_key, "Pow") == 0,
           "set_power: key is Pow");
    ASSERT(g_state.last_set_val == 1, "set_power: value is 1");
}

static void test_set_power(void)
{
    run_with_mock(do_test_set_power, 1);
}

/* ---------- test_extended_params ---------- */
static void do_test_extended_params(GreeDevice *dev)
{
    /* Verify the extended fields that weren't in the old struct */
    GreeStatus st = {0};
    int rc = gree_client_get_status(dev, &st);
    ASSERT(rc == 0,          "extended_params: get_status ok");
    ASSERT(st.swing_h == 3,  "extended_params: swing_h=3");
    ASSERT(st.xfan    == 1,  "extended_params: xfan=1");
    ASSERT(st.air     == 0,  "extended_params: air=0");
    ASSERT(st.health  == 1,  "extended_params: health=1");
    ASSERT(st.steady_heat == 0, "extended_params: steady_heat=0");
    ASSERT(st.heat_cool_type == 1, "extended_params: heat_cool_type=1");
    ASSERT(st.temp_unit == GREE_TEMUN_CELSIUS, "extended_params: temp_unit=Celsius");
}

static void test_extended_params(void)
{
    run_with_mock(do_test_extended_params, 1);
}

/* ---------- test_set_temp ---------- */
static void do_test_set_temp(GreeDevice *dev)
{
    const char *keys[] = { "SetTem" };
    const int   vals[] = { 24 };
    int rc = gree_client_set(dev, keys, vals, 1);
    ASSERT(rc == 0, "set_temp returns 0");
    ASSERT(strcmp(g_state.last_set_key, "SetTem") == 0, "set_temp: key is SetTem");
    ASSERT(g_state.last_set_val == 24, "set_temp: value is 24");
}

static void test_set_temp(void) { run_with_mock(do_test_set_temp, 1); }

/* ---------- test_set_mode ---------- */
static void do_test_set_mode(GreeDevice *dev)
{
    const char *keys[] = { "Mod" };
    const int   vals[] = { 0 };
    int rc = gree_client_set(dev, keys, vals, 1);
    ASSERT(rc == 0, "set_mode returns 0");
    ASSERT(strcmp(g_state.last_set_key, "Mod") == 0, "set_mode: key is Mod");
    ASSERT(g_state.last_set_val == 0, "set_mode: value is 0 (auto)");
}

static void test_set_mode(void) { run_with_mock(do_test_set_mode, 1); }

/* ---------- test_set_fan ---------- */
static void do_test_set_fan(GreeDevice *dev)
{
    const char *keys[] = { "WdSpd" };
    const int   vals[] = { 3 };
    int rc = gree_client_set(dev, keys, vals, 1);
    ASSERT(rc == 0, "set_fan returns 0");
    ASSERT(strcmp(g_state.last_set_key, "WdSpd") == 0, "set_fan: key is WdSpd");
    ASSERT(g_state.last_set_val == 3, "set_fan: value is 3 (high)");
}

static void test_set_fan(void) { run_with_mock(do_test_set_fan, 1); }

/* ---------- test_set_sleep ---------- */
static void do_test_set_sleep(GreeDevice *dev)
{
    const char *keys[] = { "SwhSlp" };
    const int   vals[] = { 1 };
    int rc = gree_client_set(dev, keys, vals, 1);
    ASSERT(rc == 0, "set_sleep returns 0");
    ASSERT(strcmp(g_state.last_set_key, "SwhSlp") == 0, "set_sleep: key is SwhSlp");
    ASSERT(g_state.last_set_val == 1, "set_sleep: value is 1");
}

static void test_set_sleep(void) { run_with_mock(do_test_set_sleep, 1); }

/* ---------- test_set_swing_v ---------- */
static void do_test_set_swing_v(GreeDevice *dev)
{
    const char *keys[] = { "SwUpDn" };
    const int   vals[] = { 2 };
    int rc = gree_client_set(dev, keys, vals, 1);
    ASSERT(rc == 0, "set_swing_v returns 0");
    ASSERT(strcmp(g_state.last_set_key, "SwUpDn") == 0, "set_swing_v: key is SwUpDn");
    ASSERT(g_state.last_set_val == 2, "set_swing_v: value is 2");
}

static void test_set_swing_v(void) { run_with_mock(do_test_set_swing_v, 1); }

/* ---------- test_set_swing_h ---------- */
static void do_test_set_swing_h(GreeDevice *dev)
{
    const char *keys[] = { "SwingLfRig" };
    const int   vals[] = { 1 };
    int rc = gree_client_set(dev, keys, vals, 1);
    ASSERT(rc == 0, "set_swing_h returns 0");
    ASSERT(strcmp(g_state.last_set_key, "SwingLfRig") == 0, "set_swing_h: key is SwingLfRig");
    ASSERT(g_state.last_set_val == 1, "set_swing_h: value is 1");
}

static void test_set_swing_h(void) { run_with_mock(do_test_set_swing_h, 1); }

/* ---------- test_set_xfan ---------- */
static void do_test_set_xfan(GreeDevice *dev)
{
    const char *keys[] = { "Blo" };
    const int   vals[] = { 1 };
    int rc = gree_client_set(dev, keys, vals, 1);
    ASSERT(rc == 0, "set_xfan returns 0");
    ASSERT(strcmp(g_state.last_set_key, "Blo") == 0, "set_xfan: key is Blo");
    ASSERT(g_state.last_set_val == 1, "set_xfan: value is 1");
}

static void test_set_xfan(void) { run_with_mock(do_test_set_xfan, 1); }

/* ---------- test_set_health ---------- */
static void do_test_set_health(GreeDevice *dev)
{
    const char *keys[] = { "Health" };
    const int   vals[] = { 1 };
    int rc = gree_client_set(dev, keys, vals, 1);
    ASSERT(rc == 0, "set_health returns 0");
    ASSERT(strcmp(g_state.last_set_key, "Health") == 0, "set_health: key is Health");
    ASSERT(g_state.last_set_val == 1, "set_health: value is 1");
}

static void test_set_health(void) { run_with_mock(do_test_set_health, 1); }

/* ---------- test_set_antifrost ---------- */
static void do_test_set_antifrost(GreeDevice *dev)
{
    const char *keys[] = { "StHt" };
    const int   vals[] = { 1 };
    int rc = gree_client_set(dev, keys, vals, 1);
    ASSERT(rc == 0, "set_antifrost returns 0");
    ASSERT(strcmp(g_state.last_set_key, "StHt") == 0, "set_antifrost: key is StHt");
    ASSERT(g_state.last_set_val == 1, "set_antifrost: value is 1");
}

static void test_set_antifrost(void) { run_with_mock(do_test_set_antifrost, 1); }

/* ---------- test_set_multi_param ---------- */
static void do_test_set_multi_param(GreeDevice *dev)
{
    const char *keys[] = { "Pow", "SetTem", "Mod" };
    const int   vals[] = { 1, 26, 1 };
    int rc = gree_client_set(dev, keys, vals, 3);
    ASSERT(rc == 0, "set_multi_param returns 0");
    ASSERT(strcmp(g_state.last_set_key, "Pow") == 0, "set_multi_param: first key is Pow");
}

static void test_set_multi_param(void) { run_with_mock(do_test_set_multi_param, 1); }

/* ---------- test_set_unbound ---------- */
static void test_set_unbound(void)
{
    GreeDevice dev = {0};
    dev.bound = 0;
    const char *keys[] = { "Pow" };
    const int   vals[] = { 1 };
    int rc = gree_client_set(&dev, keys, vals, 1);
    ASSERT(rc == -1, "set on unbound device returns -1");
}

/* ---------- test_get_status_unbound ---------- */
static void test_get_status_unbound(void)
{
    GreeDevice dev = {0};
    dev.bound = 0;
    GreeStatus st = {0};
    int rc = gree_client_get_status(&dev, &st);
    ASSERT(rc == -1, "get_status on unbound device returns -1");
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== Gree Functional Tests (port %d) ===\n", GREE_PORT);

    RUN_TEST(test_scan);
    RUN_TEST(test_bind);
    RUN_TEST(test_get_status);
    RUN_TEST(test_temsen_offset);
    RUN_TEST(test_set_power);
    RUN_TEST(test_extended_params);
    RUN_TEST(test_set_temp);
    RUN_TEST(test_set_mode);
    RUN_TEST(test_set_fan);
    RUN_TEST(test_set_sleep);
    RUN_TEST(test_set_swing_v);
    RUN_TEST(test_set_swing_h);
    RUN_TEST(test_set_xfan);
    RUN_TEST(test_set_health);
    RUN_TEST(test_set_antifrost);
    RUN_TEST(test_set_multi_param);
    RUN_TEST(test_set_unbound);
    RUN_TEST(test_get_status_unbound);

    printf("\nResults: %d/%d passed", g_tests_run - g_tests_failed, g_tests_run);
    if (g_tests_failed)
        printf(" (%d FAILED)", g_tests_failed);
    printf("\n");

    return g_tests_failed ? 1 : 0;
}
