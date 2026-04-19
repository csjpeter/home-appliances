/**
 * @file test_brother_functional.c
 * @brief Functional tests for brother_client using a pthread-based UDP mock SNMP server.
 *
 * Compile with:
 *   -DBROTHER_SNMP_PORT=16100 -lpthread
 */

#include "../../tests/common/test_helpers.h"
#include "../../tests/common/mock_server.h"
#include "../../libappliances/src/infrastructure/brother_client.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int g_tests_run    = 0;
int g_tests_failed = 0;

/* ── Minimal BER response builder ──────────────────────────────────────────
 *
 * These helpers produce valid SNMPv1 GetResponse packets for the mock server.
 * They mirror the logic in brother_client.c but are independent copies to
 * avoid exposing the static helpers from the production code.
 */

/* Write one byte. Returns 0 on success, -1 on overflow. */
static int rb_put1(unsigned char *buf, size_t *pos, size_t cap, unsigned char b)
{
    if (*pos >= cap)
        return -1;
    buf[(*pos)++] = b;
    return 0;
}

/* Write multiple bytes. Returns 0 on success, -1 on overflow. */
static int rb_put(unsigned char *buf, size_t *pos, size_t cap,
                  const unsigned char *data, size_t len)
{
    if (*pos + len > cap)
        return -1;
    memcpy(buf + *pos, data, len);
    *pos += len;
    return 0;
}

/* Encode a BER definite-length field (1 or 2 bytes). */
static int rb_enc_len(unsigned char *out, int len)
{
    if (len < 0x80)
    {
        out[0] = (unsigned char)len;
        return 1;
    }
    if (len <= 0xff)
    {
        out[0] = 0x81u;
        out[1] = (unsigned char)len;
        return 2;
    }
    out[0] = 0x82u;
    out[1] = (unsigned char)(len >> 8);
    out[2] = (unsigned char)(len & 0xff);
    return 3;
}

/*
 * Encode an OID string into BER value bytes (no tag/length).
 * Returns number of bytes written, or -1 on error.
 */
static int rb_enc_oid(const char *oid_str, unsigned char *out, size_t out_size)
{
    unsigned int arcs[64];
    int          arc_count = 0;
    const char  *p = oid_str;

    while (*p != '\0')
    {
        if (arc_count >= 64)
            return -1;
        unsigned int val = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9')
        {
            val = val * 10u + (unsigned int)(*p - '0');
            p++;
            digits++;
        }
        if (digits == 0)
            return -1;
        arcs[arc_count++] = val;
        if (*p == '.')
            p++;
        else if (*p != '\0')
            return -1;
    }
    if (arc_count < 2)
        return -1;

    size_t pos = 0;

    /* First two arcs: 40 * arc0 + arc1 */
    unsigned int first = 40u * arcs[0] + arcs[1];
    if (first < 0x80u)
    {
        if (pos >= out_size)
            return -1;
        out[pos++] = (unsigned char)first;
    }
    else
    {
        int nb = 0;
        unsigned int tmp = first;
        while (tmp > 0) { nb++; tmp >>= 7; }
        if (pos + (size_t)nb > out_size)
            return -1;
        for (int i = nb - 1; i >= 0; i--)
        {
            out[pos + (size_t)i] = (unsigned char)(first & 0x7fu);
            if (i != nb - 1)
                out[pos + (size_t)i] |= 0x80u;
            first >>= 7;
        }
        pos += (size_t)nb;
    }

    for (int a = 2; a < arc_count; a++)
    {
        unsigned int arc = arcs[a];
        if (arc < 0x80u)
        {
            if (pos >= out_size)
                return -1;
            out[pos++] = (unsigned char)arc;
        }
        else
        {
            int nb = 0;
            unsigned int tmp2 = arc;
            while (tmp2 > 0) { nb++; tmp2 >>= 7; }
            if (pos + (size_t)nb > out_size)
                return -1;
            for (int i = nb - 1; i >= 0; i--)
            {
                out[pos + (size_t)i] = (unsigned char)(arc & 0x7fu);
                if (i != nb - 1)
                    out[pos + (size_t)i] |= 0x80u;
                arc >>= 7;
            }
            pos += (size_t)nb;
        }
    }

    return (int)pos;
}

/*
 * Build a one-varbind GetResponse TLV region (OID + OCTET STRING value).
 * Writes directly into buf at *pos.
 */
static int rb_add_varbind_str(unsigned char *buf, size_t *pos, size_t cap,
                               const char *oid_str, const char *str_val)
{
    unsigned char oid_bytes[64];
    int oid_len = rb_enc_oid(oid_str, oid_bytes, sizeof(oid_bytes));
    if (oid_len < 0)
        return -1;

    size_t sv_len = strlen(str_val);

    /* Compute varbind body length. */
    unsigned char lb[4];
    int oid_lb = rb_enc_len(lb, oid_len);
    int sv_lb  = rb_enc_len(lb, (int)sv_len);
    /* body = (1 tag + oid_lb + oid_len) + (1 tag + sv_lb + sv_len) */
    int body_len = 1 + oid_lb + oid_len + 1 + sv_lb + (int)sv_len;
    int vb_lb    = rb_enc_len(lb, body_len);

    /* Write varbind: SEQUENCE { OID OCTETSTRING } */
    if (rb_put1(buf, pos, cap, 0x30u) < 0)
        return -1;
    {
        unsigned char len_tmp[4];
        rb_enc_len(len_tmp, body_len);
        if (rb_put(buf, pos, cap, len_tmp, (size_t)vb_lb) < 0)
            return -1;
    }
    /* OID TLV */
    if (rb_put1(buf, pos, cap, 0x06u) < 0)
        return -1;
    {
        unsigned char len_tmp[4];
        int oid_lbytes = rb_enc_len(len_tmp, oid_len);
        if (rb_put(buf, pos, cap, len_tmp, (size_t)oid_lbytes) < 0)
            return -1;
    }
    if (rb_put(buf, pos, cap, oid_bytes, (size_t)oid_len) < 0)
        return -1;
    /* OCTET STRING TLV */
    if (rb_put1(buf, pos, cap, 0x04u) < 0)
        return -1;
    {
        unsigned char len_tmp[4];
        int sv_lbytes = rb_enc_len(len_tmp, (int)sv_len);
        if (rb_put(buf, pos, cap, len_tmp, (size_t)sv_lbytes) < 0)
            return -1;
    }
    if (rb_put(buf, pos, cap, (const unsigned char *)str_val, sv_len) < 0)
        return -1;

    return 0;
}

/*
 * Build a one-varbind GetResponse TLV region (OID + INTEGER value).
 */
static int rb_add_varbind_int(unsigned char *buf, size_t *pos, size_t cap,
                               const char *oid_str, int int_val)
{
    unsigned char oid_bytes[64];
    int oid_len = rb_enc_oid(oid_str, oid_bytes, sizeof(oid_bytes));
    if (oid_len < 0)
        return -1;

    /* Encode integer value: always 4 bytes big-endian for simplicity. */
    unsigned char iv[4];
    unsigned int uval = (unsigned int)int_val;
    iv[0] = (unsigned char)((uval >> 24) & 0xffu);
    iv[1] = (unsigned char)((uval >> 16) & 0xffu);
    iv[2] = (unsigned char)((uval >> 8)  & 0xffu);
    iv[3] = (unsigned char)( uval        & 0xffu);

    /* body = OID TLV + INTEGER TLV */
    unsigned char lb[4];
    int oid_lb = rb_enc_len(lb, oid_len);
    /* int: tag(1) + len(1) + 4 bytes */
    int body_len = 1 + oid_lb + oid_len + 1 + 1 + 4;
    int vb_lb    = rb_enc_len(lb, body_len);

    if (rb_put1(buf, pos, cap, 0x30u) < 0)
        return -1;
    {
        unsigned char len_tmp[4];
        rb_enc_len(len_tmp, body_len);
        if (rb_put(buf, pos, cap, len_tmp, (size_t)vb_lb) < 0)
            return -1;
    }
    /* OID TLV */
    if (rb_put1(buf, pos, cap, 0x06u) < 0)
        return -1;
    {
        unsigned char len_tmp[4];
        int oid_lbytes = rb_enc_len(len_tmp, oid_len);
        if (rb_put(buf, pos, cap, len_tmp, (size_t)oid_lbytes) < 0)
            return -1;
    }
    if (rb_put(buf, pos, cap, oid_bytes, (size_t)oid_len) < 0)
        return -1;
    /* INTEGER TLV */
    if (rb_put1(buf, pos, cap, 0x02u) < 0)
        return -1;
    if (rb_put1(buf, pos, cap, 0x04u) < 0)
        return -1;
    if (rb_put(buf, pos, cap, iv, 4) < 0)
        return -1;

    return 0;
}

/*
 * Wrap varbinds and build a complete SNMPv1 GetResponse message.
 * varbinds[] and varbind_count are already-encoded varbind SEQUENCE blobs.
 */
static int rb_build_response(unsigned char *out, size_t out_size,
                              const unsigned char *varbinds, size_t varbinds_len)
{
    size_t pos = 0;

    /* varbind-list SEQUENCE */
    unsigned char vbl[1024];
    size_t        vbl_pos = 0;
    {
        unsigned char lb[4];
        int lb_bytes = rb_enc_len(lb, (int)varbinds_len);
        if (rb_put1(vbl, &vbl_pos, sizeof(vbl), 0x30u) < 0)
            return -1;
        if (rb_put(vbl, &vbl_pos, sizeof(vbl), lb, (size_t)lb_bytes) < 0)
            return -1;
        if (rb_put(vbl, &vbl_pos, sizeof(vbl), varbinds, varbinds_len) < 0)
            return -1;
    }

    /* PDU body: request-id + error-status + error-index + varbind-list */
    unsigned char pdu_body[1200];
    size_t        pdu_body_pos = 0;
    {
        /* request-id = 1 */
        unsigned char rid[] = { 0x02u, 0x04u, 0x00u, 0x00u, 0x00u, 0x01u };
        if (rb_put(pdu_body, &pdu_body_pos, sizeof(pdu_body), rid, sizeof(rid)) < 0)
            return -1;
        /* error-status = 0 */
        unsigned char z[] = { 0x02u, 0x01u, 0x00u };
        if (rb_put(pdu_body, &pdu_body_pos, sizeof(pdu_body), z, sizeof(z)) < 0)
            return -1;
        /* error-index = 0 */
        if (rb_put(pdu_body, &pdu_body_pos, sizeof(pdu_body), z, sizeof(z)) < 0)
            return -1;
        /* varbind-list */
        if (rb_put(pdu_body, &pdu_body_pos, sizeof(pdu_body), vbl, vbl_pos) < 0)
            return -1;
    }

    /* GetResponse-PDU (0xa2) */
    unsigned char pdu[1300];
    size_t        pdu_pos = 0;
    {
        unsigned char lb[4];
        int lb_bytes = rb_enc_len(lb, (int)pdu_body_pos);
        if (rb_put1(pdu, &pdu_pos, sizeof(pdu), 0xa2u) < 0)
            return -1;
        if (rb_put(pdu, &pdu_pos, sizeof(pdu), lb, (size_t)lb_bytes) < 0)
            return -1;
        if (rb_put(pdu, &pdu_pos, sizeof(pdu), pdu_body, pdu_body_pos) < 0)
            return -1;
    }

    /* Message body: version + community + PDU */
    unsigned char msg_body[1400];
    size_t        msg_body_pos = 0;
    {
        unsigned char ver[] = { 0x02u, 0x01u, 0x00u };
        if (rb_put(msg_body, &msg_body_pos, sizeof(msg_body), ver, sizeof(ver)) < 0)
            return -1;
        unsigned char comm[] = { 0x04u, 0x06u, 'p','u','b','l','i','c' };
        if (rb_put(msg_body, &msg_body_pos, sizeof(msg_body), comm, sizeof(comm)) < 0)
            return -1;
        if (rb_put(msg_body, &msg_body_pos, sizeof(msg_body), pdu, pdu_pos) < 0)
            return -1;
    }

    /* Outer SEQUENCE */
    {
        unsigned char lb[4];
        int lb_bytes = rb_enc_len(lb, (int)msg_body_pos);
        if (rb_put1(out, &pos, out_size, 0x30u) < 0)
            return -1;
        if (rb_put(out, &pos, out_size, lb, (size_t)lb_bytes) < 0)
            return -1;
        if (rb_put(out, &pos, out_size, msg_body, msg_body_pos) < 0)
            return -1;
    }

    return (int)pos;
}

/*
 * Build a one-varbind GetResponse (OID + raw OctetString bytes).
 * Unlike rb_add_varbind_str, this takes explicit byte length for binary data.
 */
static int rb_add_varbind_raw(unsigned char *buf, size_t *pos, size_t cap,
                               const char *oid_str,
                               const unsigned char *data, size_t data_len)
{
    unsigned char oid_bytes[64];
    int oid_len = rb_enc_oid(oid_str, oid_bytes, sizeof(oid_bytes));
    if (oid_len < 0)
        return -1;

    unsigned char lb[4];
    int oid_lb  = rb_enc_len(lb, oid_len);
    int data_lb = rb_enc_len(lb, (int)data_len);
    int body_len = 1 + oid_lb + oid_len + 1 + data_lb + (int)data_len;
    int vb_lb    = rb_enc_len(lb, body_len);

    if (rb_put1(buf, pos, cap, 0x30u) < 0) return -1;
    { unsigned char lt[4]; rb_enc_len(lt, body_len);
      if (rb_put(buf, pos, cap, lt, (size_t)vb_lb) < 0) return -1; }
    /* OID TLV */
    if (rb_put1(buf, pos, cap, 0x06u) < 0) return -1;
    { unsigned char lt[4]; int lb2 = rb_enc_len(lt, oid_len);
      if (rb_put(buf, pos, cap, lt, (size_t)lb2) < 0) return -1; }
    if (rb_put(buf, pos, cap, oid_bytes, (size_t)oid_len) < 0) return -1;
    /* OCTET STRING TLV */
    if (rb_put1(buf, pos, cap, 0x04u) < 0) return -1;
    { unsigned char lt[4]; int lb2 = rb_enc_len(lt, (int)data_len);
      if (rb_put(buf, pos, cap, lt, (size_t)lb2) < 0) return -1; }
    if (rb_put(buf, pos, cap, data, data_len) < 0) return -1;
    return 0;
}

/*
 * Build a probe GetResponse: single OctetString varbind for sysDescr.
 */
static int build_snmp_probe_response(unsigned char *out, size_t out_size,
                                      const char *str_value)
{
    unsigned char varbinds[256];
    size_t        vb_pos = 0;

    if (rb_add_varbind_str(varbinds, &vb_pos, sizeof(varbinds),
                            "1.3.6.1.2.1.1.1.0", str_value) < 0)
        return -1;

    return rb_build_response(out, out_size, varbinds, vb_pos);
}

/*
 * Build a status GetResponse: 5 integer varbinds.
 * OID order must match brother_get_status() request order:
 *   [0] OID_PRINTER_STATUS
 *   [1] OID_PAGE_COUNT
 *   [2] OID_TONER_CUR
 *   [3] OID_TONER_MAX
 *   [4] OID_TONER_LOW
 */
static int build_snmp_status_response(unsigned char *out, size_t out_size,
                                       int state, int pages,
                                       int toner_cur, int toner_max,
                                       int toner_low)
{
    unsigned char varbinds[512];
    size_t        vb_pos = 0;

    if (rb_add_varbind_int(varbinds, &vb_pos, sizeof(varbinds),
                            "1.3.6.1.2.1.25.3.2.1.5.1",     state)     < 0)
        return -1;
    if (rb_add_varbind_int(varbinds, &vb_pos, sizeof(varbinds),
                            "1.3.6.1.2.1.43.10.2.1.4.1.1",  pages)     < 0)
        return -1;
    if (rb_add_varbind_int(varbinds, &vb_pos, sizeof(varbinds),
                            "1.3.6.1.2.1.43.11.1.1.9.1.1",  toner_cur) < 0)
        return -1;
    if (rb_add_varbind_int(varbinds, &vb_pos, sizeof(varbinds),
                            "1.3.6.1.2.1.43.11.1.1.8.1.1",  toner_max) < 0)
        return -1;
    if (rb_add_varbind_int(varbinds, &vb_pos, sizeof(varbinds),
                            "1.3.6.1.4.1.2435.2.3.9.1.1.2.10.1", toner_low) < 0)
        return -1;

    return rb_build_response(out, out_size, varbinds, vb_pos);
}

/*
 * Build a consumables GetResponse: 4 varbinds matching brother_get_consumables().
 * OID order:
 *   [0] OID_TONER_CUR  — INTEGER
 *   [1] OID_TONER_MAX  — INTEGER
 *   [2] OID_DRUM_INFO  — OCTET STRING (2-byte LE uint16, units 0.01%)
 *   [3] OID_MAINT_NEXT — OCTET STRING (2-byte LE uint16, pages)
 */
static int build_snmp_consumables_response(unsigned char *out, size_t out_size,
                                            int toner_cur, int toner_max,
                                            unsigned int drum_raw,
                                            unsigned int maint_raw)
{
    unsigned char varbinds[512];
    size_t        vb_pos = 0;

    if (rb_add_varbind_int(varbinds, &vb_pos, sizeof(varbinds),
                            "1.3.6.1.2.1.43.11.1.1.9.1.1", toner_cur) < 0)
        return -1;
    if (rb_add_varbind_int(varbinds, &vb_pos, sizeof(varbinds),
                            "1.3.6.1.2.1.43.11.1.1.8.1.1", toner_max) < 0)
        return -1;

    /* drum: 2-byte LE uint16 */
    unsigned char drum_bytes[2] = {
        (unsigned char)(drum_raw & 0xffu),
        (unsigned char)((drum_raw >> 8) & 0xffu)
    };
    if (rb_add_varbind_raw(varbinds, &vb_pos, sizeof(varbinds),
                            "1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.8.0",
                            drum_bytes, 2) < 0)
        return -1;

    /* maintenance pages: 2-byte LE uint16 */
    unsigned char maint_bytes[2] = {
        (unsigned char)(maint_raw & 0xffu),
        (unsigned char)((maint_raw >> 8) & 0xffu)
    };
    if (rb_add_varbind_raw(varbinds, &vb_pos, sizeof(varbinds),
                            "1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.11.0",
                            maint_bytes, 2) < 0)
        return -1;

    return rb_build_response(out, out_size, varbinds, vb_pos);
}

/* ── Mock server shared state ─────────────────────────────────────────────── */

typedef struct
{
    unsigned char resp[2048];
    int           resp_len;
} MockArgs;

static volatile int mock_ready = 0;

/* ── Generic UDP mock: receive one packet, send back the canned response. ── */

static void *mock_snmp_server(void *arg)
{
    MockArgs *ma = (MockArgs *)arg;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return NULL;

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(MOCK_BROTHER_SNMP_PORT);
    inet_pton(AF_INET, MOCK_HOST, &addr.sin_addr);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        return NULL;
    }

    mock_ready = 1;  /* signal to test thread */

    /* Receive one request from the client. */
    unsigned char req_buf[1024];
    struct sockaddr_in client_addr = {0};
    socklen_t          client_len  = sizeof(client_addr);

    ssize_t n = recvfrom(fd, req_buf, sizeof(req_buf), 0,
                         (struct sockaddr *)&client_addr, &client_len);
    if (n > 0)
    {
        /* Send the canned response back to the client's ephemeral port. */
        sendto(fd, ma->resp, (size_t)ma->resp_len, 0,
               (struct sockaddr *)&client_addr, client_len);
    }

    close(fd);
    return NULL;
}

/* ── Helper: start mock server with a pre-built response ─────────────────── */

static pthread_t start_mock(MockArgs *ma)
{
    mock_ready = 0;
    pthread_t tid;
    pthread_create(&tid, NULL, mock_snmp_server, ma);
    MOCK_WAIT_READY(mock_ready);
    return tid;
}

/* ── Tests ───────────────────────────────────────────────────────────────── */

static void test_probe_reachable(void)
{
    MockArgs ma = {0};
    ma.resp_len = build_snmp_probe_response(ma.resp, sizeof(ma.resp),
                                             "Brother NC-340");
    ASSERT(ma.resp_len > 0, "build probe response should succeed");

    pthread_t tid = start_mock(&ma);

    char model[64] = {0};
    int result = brother_probe(MOCK_HOST, model, sizeof(model));

    pthread_join(tid, NULL);

    ASSERT(result == 1, "probe should return 1 when mock responds");
    ASSERT(strcmp(model, "Brother NC-340") == 0,
           "model string should match mock response");
}

static void test_probe_unreachable(void)
{
    /* No mock running — recvfrom should time out. */
    int result = brother_probe(MOCK_HOST, NULL, 0);
    ASSERT(result == 0, "probe should return 0 on timeout (unreachable)");
}

static void test_get_status(void)
{
    MockArgs ma = {0};
    /* state=3(idle), pages=12500, toner_cur=680, toner_max=1000, toner_low=0
     * Expected toner_pct = 680*100/1000 = 68 */
    ma.resp_len = build_snmp_status_response(ma.resp, sizeof(ma.resp),
                                              3, 12500, 680, 1000, 0);
    ASSERT(ma.resp_len > 0, "build status response should succeed");

    pthread_t tid = start_mock(&ma);

    BrotherStatus st = {0};
    int result = brother_get_status(MOCK_HOST, &st);

    pthread_join(tid, NULL);

    ASSERT(result == 0,      "get_status should return 0 on success");
    ASSERT(st.state == 3,    "printer state should be 3 (idle)");
    ASSERT(st.page_count == 12500, "page count should be 12500");
    ASSERT(st.toner_pct == 68,    "toner percent should be 68");
    ASSERT(st.toner_low == 0,     "toner low flag should be 0 (ok)");
}

static void test_toner_low_flag(void)
{
    MockArgs ma = {0};
    /* Same as above but toner_low=1 */
    ma.resp_len = build_snmp_status_response(ma.resp, sizeof(ma.resp),
                                              3, 12500, 680, 1000, 1);
    ASSERT(ma.resp_len > 0, "build status response (toner low) should succeed");

    pthread_t tid = start_mock(&ma);

    BrotherStatus st = {0};
    int result = brother_get_status(MOCK_HOST, &st);

    pthread_join(tid, NULL);

    ASSERT(result == 0,      "get_status should return 0 on success");
    ASSERT(st.toner_low == 1, "toner low flag should be 1 (low)");
}

static void test_get_consumables(void)
{
    MockArgs ma = {0};
    /*
     * toner 68% (680/1000), drum 54% (5400 raw / 100), pages 8200 remaining
     * drum_raw = 5400 = 0x1518 → LE bytes [0x18, 0x15]
     * maint_raw = 8200 = 0x2008 → LE bytes [0x08, 0x20]
     */
    ma.resp_len = build_snmp_consumables_response(ma.resp, sizeof(ma.resp),
                                                   680, 1000, 5400u, 8200u);
    ASSERT(ma.resp_len > 0, "build consumables response should succeed");

    pthread_t tid = start_mock(&ma);

    BrotherConsumables c = {0};
    int result = brother_get_consumables(MOCK_HOST, &c);

    pthread_join(tid, NULL);

    ASSERT(result == 0,             "get_consumables should return 0 on success");
    ASSERT(c.toner_pct == 68,       "toner percent should be 68");
    ASSERT(c.drum_pct == 54,        "drum percent should be 54 (5400/100)");
    ASSERT(c.pages_until_maint == 8200, "pages until maintenance should be 8200");
}

/*
 * test_probe_model — verify that brother_probe returns 1 and populates the
 * model string when the mock responds with a sysDescr containing "Brother".
 */
static void test_probe_model(void)
{
    MockArgs ma = {0};
    ma.resp_len = build_snmp_probe_response(ma.resp, sizeof(ma.resp),
                                             "Brother MFC-L2750DW series");
    ASSERT(ma.resp_len > 0, "build probe-model response should succeed");

    pthread_t tid = start_mock(&ma);

    char model[64] = {0};
    int result = brother_probe(MOCK_HOST, model, sizeof(model));

    pthread_join(tid, NULL);

    ASSERT(result == 1, "probe should return 1 when mock responds with Brother model");
    ASSERT(strstr(model, "Brother") != NULL,
           "model string should contain 'Brother'");
}

/*
 * test_get_toner_low — mock returns toner at 5% (very low).
 * Assert that toner_pct == 5.
 */
static void test_get_toner_low(void)
{
    MockArgs ma = {0};
    /* state=3(idle), pages=500, toner_cur=50, toner_max=1000, toner_low=1
     * Expected toner_pct = 50*100/1000 = 5 */
    ma.resp_len = build_snmp_status_response(ma.resp, sizeof(ma.resp),
                                              3, 500, 50, 1000, 1);
    ASSERT(ma.resp_len > 0, "build toner-low response should succeed");

    pthread_t tid = start_mock(&ma);

    BrotherStatus st = {0};
    int result = brother_get_status(MOCK_HOST, &st);

    pthread_join(tid, NULL);

    ASSERT(result == 0,       "get_status should return 0 on success");
    ASSERT(st.toner_pct == 5, "toner_pct should be 5 when toner is very low");
}

/*
 * test_invalid_ip — call brother_get_status with an address that has nothing
 * listening; the call must fail with rc == -1 (or time out and return -1).
 * No mock is started.
 */
static void test_invalid_ip(void)
{
    BrotherStatus st = {0};
    int result = brother_get_status("127.0.0.2", &st);
    ASSERT(result == -1, "get_status should return -1 for unreachable IP");
}

/*
 * test_get_consumables_drum — verify that brother_get_consumables returns 0
 * and that drum_pct is > 0 when the mock provides a non-zero drum value.
 */
static void test_get_consumables_drum(void)
{
    MockArgs ma = {0};
    /*
     * toner 80% (800/1000), drum 75% (7500 raw / 100), pages 10000 remaining
     * drum_raw = 7500 = 0x1D4C → LE bytes [0x4C, 0x1D]
     * maint_raw = 10000 = 0x2710 → LE bytes [0x10, 0x27]
     */
    ma.resp_len = build_snmp_consumables_response(ma.resp, sizeof(ma.resp),
                                                   800, 1000, 7500u, 10000u);
    ASSERT(ma.resp_len > 0, "build consumables-drum response should succeed");

    pthread_t tid = start_mock(&ma);

    BrotherConsumables c = {0};
    int result = brother_get_consumables(MOCK_HOST, &c);

    pthread_join(tid, NULL);

    ASSERT(result == 0,    "get_consumables should return 0 on success");
    ASSERT(c.drum_pct > 0, "drum_pct should be greater than 0");
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("Brother printer functional tests\n");

    RUN_TEST(test_probe_reachable);
    RUN_TEST(test_probe_unreachable);
    RUN_TEST(test_get_status);
    RUN_TEST(test_toner_low_flag);
    RUN_TEST(test_get_consumables);
    RUN_TEST(test_probe_model);
    RUN_TEST(test_get_toner_low);
    RUN_TEST(test_invalid_ip);
    RUN_TEST(test_get_consumables_drum);

    printf("\n%d test(s) run, %d failed.\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
