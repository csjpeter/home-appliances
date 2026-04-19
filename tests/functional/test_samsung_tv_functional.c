/**
 * @file test_samsung_tv_functional.c
 * @brief Functional tests for samsung_tv_client using a pthread mock TCP server.
 *
 * Compile with:
 *   -DSAMSUNG_TV_PORT=15500 -lpthread
 */

#include "../../tests/common/test_helpers.h"
#include "../../tests/common/mock_server.h"
#include "../../libappliances/src/infrastructure/samsung_tv_client.h"
#include "../../libappliances/src/core/base64.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int g_tests_run    = 0;
int g_tests_failed = 0;

/* ── Auth response byte values ─────────────────────────────────────────── */

#define AUTH_GRANTED 0x01
#define AUTH_DENIED  0x00
#define AUTH_WAITING 0x02

/* 6-byte auth response: bytes 0-1 framing, byte 2 = status, bytes 3-5 padding. */
static const unsigned char RESP_GRANTED[] = { 0x00, 0x00, AUTH_GRANTED, 0x00, 0x00, 0x00 };
static const unsigned char RESP_DENIED[]  = { 0x00, 0x00, AUTH_DENIED,  0x00, 0x00, 0x00 };
static const unsigned char RESP_WAITING[] = { 0x00, 0x00, AUTH_WAITING, 0x00, 0x00, 0x00 };

/* ── Mock server shared state ──────────────────────────────────────────── */

typedef enum
{
    GRANT_TYPE_GRANTED,         /* grant immediately */
    GRANT_TYPE_DENIED,          /* deny immediately */
    GRANT_TYPE_WAITING_THEN_GRANT, /* send waiting once, then grant on retry */
} GrantType;

typedef struct
{
    GrantType grant_type;
    char      received_key[64]; /* base64-decoded key from key packet */
} MockState;

static MockState          g_mock_state  = {0};
static volatile int       mock_ready    = 0;

/* ── Mock server helpers ───────────────────────────────────────────────── */

/*
 * Drain incoming bytes from the client until there is nothing left to read
 * at the moment (non-blocking peek via MSG_DONTWAIT), or until the buffer
 * is full.  Used to consume the auth packet before replying.
 */
static void drain_socket(int client_fd)
{
    unsigned char discard[512];
    /* First read with blocking to get the initial data. */
    recv(client_fd, discard, sizeof(discard), 0);
}

/*
 * Parse a key packet and extract the base64-decoded key name.
 *
 * Key packet layout:
 *   byte 0x00
 *   LE16 len_app          = 19
 *   19 bytes app string
 *   LE16 inner_len
 *   0x00 0x00 0x00        (3 zeros)
 *   LE16 b64_key_len
 *   <b64_key_len bytes>   base64(key_code)
 *
 * Returns 0 on success, -1 on error.
 */
static int parse_key_packet(int client_fd, char *key_out, size_t key_out_size)
{
    unsigned char pkt[256];
    ssize_t n = recv(client_fd, pkt, sizeof(pkt), 0);
    if (n < 1)
        return -1;

    /*
     * Fixed header:
     *   1 (0x00) + 2 (app len LE16) + 19 (app string) + 2 (inner len LE16) = 24 bytes
     * Then inner:
     *   3 (zeros) + 2 (b64 key len LE16) = 5 bytes before b64 data
     * Total minimum = 29 bytes.
     */
    if (n < 29)
        return -1;

    /* Offset of 3 zeros: byte 24 */
    /* Offset of b64 key len LE16: byte 27 */
    size_t b64_len_lo = pkt[27];
    size_t b64_len_hi = pkt[28];
    size_t b64_len    = b64_len_lo | (b64_len_hi << 8);

    if (n < 29 + (ssize_t)b64_len)
        return -1;

    /* b64 key starts at byte 29 */
    char b64_key[128];
    if (b64_len >= sizeof(b64_key))
        return -1;
    memcpy(b64_key, pkt + 29, b64_len);
    b64_key[b64_len] = '\0';

    /* Decode */
    unsigned char decoded[64];
    int dec_len = base64_decode(b64_key, decoded, sizeof(decoded));
    if (dec_len < 0)
        return -1;

    size_t copy_len = (size_t)dec_len < key_out_size - 1u
                      ? (size_t)dec_len
                      : key_out_size - 1u;
    memcpy(key_out, decoded, copy_len);
    key_out[copy_len] = '\0';
    return 0;
}

/* ── Mock server thread ────────────────────────────────────────────────── */

static void *mock_tv_server(void *arg)
{
    (void)arg;

    int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0)
        return NULL;

    int reuse = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(MOCK_SAMSUNG_TV_PORT);
    inet_pton(AF_INET, MOCK_HOST, &addr.sin_addr);

    if (bind(srv_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(srv_fd);
        return NULL;
    }

    if (listen(srv_fd, 1) < 0) {
        close(srv_fd);
        return NULL;
    }

    mock_ready = 1;   /* signal: test thread may now call the client */

    int client_fd = accept(srv_fd, NULL, NULL);
    if (client_fd < 0) {
        close(srv_fd);
        return NULL;
    }

    /* ── Step 1: receive auth packet ── */
    drain_socket(client_fd);

    /* ── Step 2: send auth response ── */
    switch (g_mock_state.grant_type) {
    case GRANT_TYPE_GRANTED:
        send(client_fd, RESP_GRANTED, sizeof(RESP_GRANTED), 0);
        break;

    case GRANT_TYPE_DENIED:
        send(client_fd, RESP_DENIED, sizeof(RESP_DENIED), 0);
        break;

    case GRANT_TYPE_WAITING_THEN_GRANT:
        /* Send waiting response for the first auth attempt. */
        send(client_fd, RESP_WAITING, sizeof(RESP_WAITING), 0);
        /* Client will re-send auth; drain it, then grant. */
        drain_socket(client_fd);
        send(client_fd, RESP_GRANTED, sizeof(RESP_GRANTED), 0);
        break;
    }

    /* ── Step 3: if granted, receive key packet ── */
    if (g_mock_state.grant_type == GRANT_TYPE_GRANTED ||
        g_mock_state.grant_type == GRANT_TYPE_WAITING_THEN_GRANT) {
        parse_key_packet(client_fd, g_mock_state.received_key,
                         sizeof(g_mock_state.received_key));
    }

    close(client_fd);
    close(srv_fd);
    return NULL;
}

/* ── Helper: start mock server ─────────────────────────────────────────── */

static pthread_t start_mock_server(GrantType grant_type)
{
    memset(&g_mock_state, 0, sizeof(g_mock_state));
    g_mock_state.grant_type = grant_type;
    mock_ready = 0;

    pthread_t tid;
    pthread_create(&tid, NULL, mock_tv_server, NULL);
    MOCK_WAIT_READY(mock_ready);
    return tid;
}

/* ── Tests ─────────────────────────────────────────────────────────────── */

static void test_probe_online(void)
{
    pthread_t tid = start_mock_server(GRANT_TYPE_DENIED);
    int result = samsung_tv_probe(MOCK_HOST);
    pthread_join(tid, NULL);
    ASSERT(result == 1, "probe should return 1 when port is open");
}

static void test_probe_offline(void)
{
    /* No server running on this port. */
    int result = samsung_tv_probe(MOCK_HOST);
    ASSERT(result == 0, "probe should return 0 when port is closed");
}

static void test_send_key_granted(void)
{
    pthread_t tid = start_mock_server(GRANT_TYPE_GRANTED);
    int result = samsung_tv_send_key(MOCK_HOST, "KEY_POWER");
    pthread_join(tid, NULL);
    ASSERT(result == 0, "send_key should return 0 when auth is granted");
}

static void test_send_key_denied(void)
{
    pthread_t tid = start_mock_server(GRANT_TYPE_DENIED);
    int result = samsung_tv_send_key(MOCK_HOST, "KEY_POWER");
    pthread_join(tid, NULL);
    ASSERT(result == -1, "send_key should return -1 when auth is denied");
}

static void test_send_key_correct_key(void)
{
    pthread_t tid = start_mock_server(GRANT_TYPE_GRANTED);
    int result = samsung_tv_send_key(MOCK_HOST, "KEY_VOLUP");
    pthread_join(tid, NULL);
    ASSERT(result == 0, "send_key should succeed");
    ASSERT(strcmp(g_mock_state.received_key, "KEY_VOLUP") == 0,
           "mock should receive the correct key code");
}

static void test_send_key_waiting_then_grant(void)
{
    pthread_t tid = start_mock_server(GRANT_TYPE_WAITING_THEN_GRANT);
    int result = samsung_tv_send_key(MOCK_HOST, "KEY_MUTE");
    pthread_join(tid, NULL);
    ASSERT(result == 0, "send_key should succeed after waiting response");
    ASSERT(strcmp(g_mock_state.received_key, "KEY_MUTE") == 0,
           "correct key received after waiting→grant");
}

/* ── main ────────────────────────────────────────────────────────────────  */

int main(void)
{
    printf("Samsung TV functional tests\n");

    RUN_TEST(test_probe_online);
    RUN_TEST(test_probe_offline);
    RUN_TEST(test_send_key_granted);
    RUN_TEST(test_send_key_denied);
    RUN_TEST(test_send_key_correct_key);
    RUN_TEST(test_send_key_waiting_then_grant);

    printf("\n%d test(s) run, %d failed.\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
