/**
 * @file test_roborock_functional.c
 * @brief Functional tests for roborock_client using a UDP mock server.
 *
 * Compiled with -DROBOROCK_PORT=15432 so that the client code
 * talks to 127.0.0.1:15432 instead of the real device port.
 *
 * The mock server handles packets from a single test in sequence:
 *   1. Receives 32-byte hello (detected by 0xFF at bytes 4-7)
 *   2. Sends hello response with fake token and device_id
 *   3. If hello_only=0: also receives an encrypted command and replies
 */

#include "../../tests/common/test_helpers.h"
#include "../../tests/common/mock_server.h"
#include "../../libappliances/src/infrastructure/roborock_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* ── Test counters ───────────────────────────────────────────────────────── */

int g_tests_run    = 0;
int g_tests_failed = 0;

/* ── Fake credentials ────────────────────────────────────────────────────── */

/* 16 token bytes: 0x01 0x02 ... 0x10 */
static const unsigned char FAKE_TOKEN[16] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};
static const unsigned int FAKE_DEVICE_ID = 0xDEADBEEFu;

/* ── Crypto helpers ──────────────────────────────────────────────────────── */

static void mock_derive_keys(const unsigned char *token,
                             unsigned char *key, unsigned char *iv)
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned int len = 16;

    EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
    EVP_DigestUpdate(ctx, token, 16);
    EVP_DigestFinal_ex(ctx, key, &len);

    EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
    EVP_DigestUpdate(ctx, key, 16);
    EVP_DigestUpdate(ctx, token, 16);
    EVP_DigestFinal_ex(ctx, iv, &len);

    EVP_MD_CTX_free(ctx);
}

static int mock_encrypt(const unsigned char *key, const unsigned char *iv,
                        const unsigned char *plain, size_t plain_len,
                        unsigned char *out, int *out_len)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return -1;
    if (!EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv))
    {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    int outl = 0, finl = 0;
    EVP_EncryptUpdate(ctx, out, &outl, plain, (int)plain_len);
    EVP_EncryptFinal_ex(ctx, out + outl, &finl);
    *out_len = outl + finl;
    EVP_CIPHER_CTX_free(ctx);
    return 0;
}

static int mock_decrypt(const unsigned char *key, const unsigned char *iv,
                        const unsigned char *cipher, size_t cipher_len,
                        unsigned char *out, int *out_len)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return -1;
    if (!EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv))
    {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    int outl = 0, finl = 0;
    EVP_DecryptUpdate(ctx, out, &outl, cipher, (int)cipher_len);
    EVP_DecryptFinal_ex(ctx, out + outl, &finl);
    *out_len = outl + finl;
    EVP_CIPHER_CTX_free(ctx);
    return 0;
}

static void mock_compute_checksum(const unsigned char *header_prefix,
                                  const unsigned char *token,
                                  const unsigned char *payload,
                                  size_t payload_len,
                                  unsigned char *out)
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned int len = 16;
    EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
    EVP_DigestUpdate(ctx, header_prefix, 16);
    EVP_DigestUpdate(ctx, token, 16);
    if (payload && payload_len > 0)
        EVP_DigestUpdate(ctx, payload, payload_len);
    EVP_DigestFinal_ex(ctx, out, &len);
    EVP_MD_CTX_free(ctx);
}

/* ── Build a hello response packet ──────────────────────────────────────── */

static void make_hello_response(unsigned char *pkt)
{
    memset(pkt, 0, 32);
    uint32_t ts = (uint32_t)time(NULL);
    pkt[0] = 0x21; pkt[1] = 0x31;
    pkt[2] = 0x00; pkt[3] = 0x20; /* length = 32 */
    /* reserved = 0 (normal) */
    pkt[8]  = (unsigned char)((FAKE_DEVICE_ID >> 24) & 0xFF);
    pkt[9]  = (unsigned char)((FAKE_DEVICE_ID >> 16) & 0xFF);
    pkt[10] = (unsigned char)((FAKE_DEVICE_ID >> 8)  & 0xFF);
    pkt[11] = (unsigned char)(FAKE_DEVICE_ID & 0xFF);
    pkt[12] = (unsigned char)((ts >> 24) & 0xFF);
    pkt[13] = (unsigned char)((ts >> 16) & 0xFF);
    pkt[14] = (unsigned char)((ts >> 8)  & 0xFF);
    pkt[15] = (unsigned char)(ts & 0xFF);
    memcpy(pkt + 16, FAKE_TOKEN, 16);
}

/* ── Build a miio response packet from JSON ──────────────────────────────── */

static ssize_t mock_build_response(const char *response_json,
                                   const unsigned char *key,
                                   const unsigned char *iv,
                                   unsigned char *out_buf, size_t out_cap)
{
    unsigned char enc[2048];
    int enc_len = 0;
    if (mock_encrypt(key, iv,
                     (const unsigned char *)response_json,
                     strlen(response_json),
                     enc, &enc_len) != 0)
        return -1;

    size_t total = 32u + (size_t)enc_len;
    if (total > out_cap)
        return -1;

    memset(out_buf, 0, 32);
    uint32_t ts = (uint32_t)time(NULL);

    out_buf[0] = 0x21; out_buf[1] = 0x31;
    out_buf[2] = (unsigned char)((total >> 8) & 0xFF);
    out_buf[3] = (unsigned char)(total & 0xFF);
    /* reserved = 0 */
    out_buf[8]  = (unsigned char)((FAKE_DEVICE_ID >> 24) & 0xFF);
    out_buf[9]  = (unsigned char)((FAKE_DEVICE_ID >> 16) & 0xFF);
    out_buf[10] = (unsigned char)((FAKE_DEVICE_ID >> 8)  & 0xFF);
    out_buf[11] = (unsigned char)(FAKE_DEVICE_ID & 0xFF);
    out_buf[12] = (unsigned char)((ts >> 24) & 0xFF);
    out_buf[13] = (unsigned char)((ts >> 16) & 0xFF);
    out_buf[14] = (unsigned char)((ts >> 8)  & 0xFF);
    out_buf[15] = (unsigned char)(ts & 0xFF);

    unsigned char checksum[16];
    mock_compute_checksum(out_buf, FAKE_TOKEN, enc, (size_t)enc_len, checksum);
    memcpy(out_buf + 16, checksum, 16);

    memcpy(out_buf + 32, enc, (size_t)enc_len);
    return (ssize_t)total;
}

/* ── Select canned response JSON based on the method in the request ───────── */

static void choose_response(const char *req_json, int req_id,
                             char *resp_json, size_t resp_size)
{
    if (strstr(req_json, "\"get_status\""))
    {
        snprintf(resp_json, resp_size,
                 "{\"id\":%d,\"result\":[{\"battery\":85,\"state\":8,"
                 "\"error_code\":0,\"clean_time\":1920,\"clean_area\":2460000,"
                 "\"fan_power\":102,\"in_cleaning\":0}]}",
                 req_id);
    }
    else if (strstr(req_json, "\"get_consumable\""))
    {
        snprintf(resp_json, resp_size,
                 "{\"id\":%d,\"result\":[{\"main_brush_work_time\":72000,"
                 "\"side_brush_work_time\":48000,\"filter_work_time\":36000,"
                 "\"sensor_dirty_time\":7200}]}",
                 req_id);
    }
    else
    {
        snprintf(resp_json, resp_size,
                 "{\"id\":%d,\"result\":[\"ok\"]}", req_id);
    }
}

/* ── Mock server thread ──────────────────────────────────────────────────── */

typedef struct
{
    volatile int *ready;
    int           hello_only; /* 1 = handle hello only; 0 = hello + command */
} MockArg;

static void *mock_server_thread(void *varg)
{
    MockArg *ma = (MockArg *)varg;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        *ma->ready = 1;
        return NULL;
    }

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(MOCK_ROBOROCK_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("mock bind");
        close(fd);
        *ma->ready = 1;
        return NULL;
    }

    *ma->ready = 1; /* signal the test thread */

    unsigned char buf[2048];
    struct sockaddr_in client = {0};
    socklen_t client_len = sizeof(client);

    /* Step 1: receive hello */
    ssize_t n = recvfrom(fd, buf, sizeof(buf), 0,
                         (struct sockaddr *)&client, &client_len);
    if (n < 32)
        goto done;

    /* Must be a hello packet (bytes 4-7 all 0xFF) */
    if (buf[4] != 0xFF || buf[5] != 0xFF || buf[6] != 0xFF || buf[7] != 0xFF)
        goto done;

    {
        unsigned char hello_resp[32];
        make_hello_response(hello_resp);
        sendto(fd, hello_resp, 32, 0,
               (struct sockaddr *)&client, client_len);
    }

    if (ma->hello_only)
        goto done;

    /* Step 2: receive command */
    n = recvfrom(fd, buf, sizeof(buf), 0,
                 (struct sockaddr *)&client, &client_len);
    if (n < 32)
        goto done;

    {
        size_t cipher_len = (size_t)n - 32;
        if (cipher_len == 0)
            goto done;

        unsigned char key[16], iv[16];
        mock_derive_keys(FAKE_TOKEN, key, iv);

        unsigned char plain[2048];
        int plain_len = 0;
        if (mock_decrypt(key, iv, buf + 32, cipher_len, plain, &plain_len) != 0)
            goto done;
        plain[plain_len] = '\0';

        /* Extract request id */
        int req_id = 0;
        const char *p = strstr((char *)plain, "\"id\":");
        if (p)
            req_id = (int)strtol(p + 5, NULL, 10);

        char resp_json[512];
        choose_response((char *)plain, req_id, resp_json, sizeof(resp_json));

        unsigned char resp_pkt[2048];
        ssize_t resp_len = mock_build_response(resp_json, key, iv,
                                               resp_pkt, sizeof(resp_pkt));
        if (resp_len > 0)
            sendto(fd, resp_pkt, (size_t)resp_len, 0,
                   (struct sockaddr *)&client, client_len);
    }

done:
    close(fd);
    return NULL;
}

/* ── Test infrastructure ─────────────────────────────────────────────────── */

static volatile int g_mock_ready = 0;
static pthread_t    g_mock_tid;
static MockArg      g_mock_arg;

static void start_mock(int hello_only)
{
    g_mock_ready = 0;
    g_mock_arg.ready      = &g_mock_ready;
    g_mock_arg.hello_only = hello_only;
    pthread_create(&g_mock_tid, NULL, mock_server_thread, &g_mock_arg);
    MOCK_WAIT_READY(g_mock_ready);
}

static void stop_mock(void)
{
    pthread_join(g_mock_tid, NULL);
}

static RoborockDevice make_device(void)
{
    RoborockDevice dev = {0};
    /* ip already NUL-terminated: just copy the host string */
    memcpy(dev.ip, MOCK_HOST, strlen(MOCK_HOST) + 1);
    return dev;
}

/* ── Test cases ──────────────────────────────────────────────────────────── */

static void test_hello(void)
{
    start_mock(1 /* hello_only */);

    RoborockDevice dev = make_device();
    int rc = roborock_hello(&dev);

    stop_mock();

    ASSERT(rc == 0, "roborock_hello returned 0");
    ASSERT(dev.token_valid == 1, "token_valid set to 1");
    ASSERT(dev.device_id == FAKE_DEVICE_ID, "device_id matches 0xDEADBEEF");
    ASSERT(memcmp(dev.token, FAKE_TOKEN, 16) == 0, "token bytes match");
}

static void test_get_status(void)
{
    start_mock(0 /* hello + command */);

    RoborockDevice dev = make_device();
    int rc = roborock_hello(&dev);
    RoborockStatus st = {0};
    if (rc == 0)
        rc = roborock_get_status(&dev, &st);

    stop_mock();

    ASSERT(rc == 0,              "roborock_get_status returned 0");
    ASSERT(st.battery    == 85,      "battery = 85");
    ASSERT(st.state      == 8,       "state = 8 (charging)");
    ASSERT(st.error_code == 0,       "error_code = 0");
    ASSERT(st.clean_time == 1920,    "clean_time = 1920");
    ASSERT(st.clean_area == 2460000, "clean_area = 2460000");
    ASSERT(st.fan_power  == 102,     "fan_power = 102");
    ASSERT(st.in_cleaning== 0,       "in_cleaning = 0");
}

static void test_get_consumable(void)
{
    start_mock(0);

    RoborockDevice dev = make_device();
    int rc = roborock_hello(&dev);
    RoborockConsumables c = {0};
    if (rc == 0)
        rc = roborock_get_consumable(&dev, &c);

    stop_mock();

    ASSERT(rc == 0,              "roborock_get_consumable returned 0");
    ASSERT(c.main_brush == 72000, "main_brush = 72000");
    ASSERT(c.side_brush == 48000, "side_brush = 48000");
    ASSERT(c.filter     == 36000, "filter = 36000");
    ASSERT(c.sensor     == 7200,  "sensor = 7200");
}

static void test_start(void)
{
    start_mock(0);

    RoborockDevice dev = make_device();
    int rc = roborock_hello(&dev);
    if (rc == 0)
        rc = roborock_start(&dev);

    stop_mock();

    ASSERT(rc == 0, "roborock_start returned 0");
}

static void test_find(void)
{
    start_mock(0);

    RoborockDevice dev = make_device();
    int rc = roborock_hello(&dev);
    if (rc == 0)
        rc = roborock_find(&dev);

    stop_mock();

    ASSERT(rc == 0, "roborock_find returned 0");
}

static void test_set_fan(void)
{
    start_mock(0);

    RoborockDevice dev = make_device();
    int rc = roborock_hello(&dev);
    if (rc == 0)
        rc = roborock_set_fan(&dev, 103);

    stop_mock();

    ASSERT(rc == 0, "roborock_set_fan(103) returned 0");
}

static void test_reset_consumable(void)
{
    start_mock(0);

    RoborockDevice dev = make_device();
    int rc = roborock_hello(&dev);
    if (rc == 0)
        rc = roborock_reset_consumable(&dev, "main_brush_work_time");

    stop_mock();

    ASSERT(rc == 0, "roborock_reset_consumable returned 0");
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== Roborock functional tests ===\n");

    RUN_TEST(test_hello);
    RUN_TEST(test_get_status);
    RUN_TEST(test_get_consumable);
    RUN_TEST(test_start);
    RUN_TEST(test_find);
    RUN_TEST(test_set_fan);
    RUN_TEST(test_reset_consumable);

    printf("\nResult: %d/%d tests passed\n",
           g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
