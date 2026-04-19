/**
 * @file brother_client.c
 * @brief Brother printer SNMP v1 client — raw UDP/BER, no external libraries.
 *
 * Query a Brother printer on UDP/161 using SNMPv1 GetRequest encoded in BER.
 * Port can be overridden at compile time for testing:
 *   -DBROTHER_SNMP_PORT=16100
 */

#include "brother_client.h"
#include "../core/logger.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

/* ── OID definitions ─────────────────────────────────────────────────────── */

#define OID_SYS_DESCR      "1.3.6.1.2.1.1.1.0"
#define OID_PRINTER_STATUS "1.3.6.1.2.1.25.3.2.1.5.1"
#define OID_PAGE_COUNT     "1.3.6.1.2.1.43.10.2.1.4.1.1"
#define OID_TONER_CUR      "1.3.6.1.2.1.43.11.1.1.9.1.1"
#define OID_TONER_MAX      "1.3.6.1.2.1.43.11.1.1.8.1.1"
#define OID_TONER_LOW      "1.3.6.1.4.1.2435.2.3.9.1.1.2.10.1"
#define OID_DRUM_INFO      "1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.8.0"
#define OID_MAINT_NEXT     "1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.11.0"

/* ── BER constants ───────────────────────────────────────────────────────── */

#define BER_SEQ          0x30u
#define BER_INT          0x02u
#define BER_OCTET_STR    0x04u
#define BER_NULL         0x05u
#define BER_OID          0x06u
#define BER_SNMP_GET     0xa0u
#define BER_SNMP_RESP    0xa2u
#define BER_COUNTER32    0x41u
#define BER_GAUGE32      0x42u

/* ── RAII cleanup ────────────────────────────────────────────────────────── */

static void close_fd(int *p) { if (*p >= 0) close(*p); }

/* ── BER helpers ─────────────────────────────────────────────────────────── */

/*
 * Encode a BER definite-length field into out[].
 * Returns the number of bytes written (1 or 2), or -1 if length >= 65536.
 */
static int ber_encode_len(unsigned char *out, int len)
{
    if (len < 0)
        return -1;
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
    if (len <= 0xffff)
    {
        out[0] = 0x82u;
        out[1] = (unsigned char)(len >> 8);
        out[2] = (unsigned char)(len & 0xff);
        return 3;
    }
    return -1;
}

/*
 * Encode a single OID string (e.g. "1.3.6.1.2.1.1.1.0") into BER OID bytes.
 * Does NOT include the tag (0x06) or length — only the value bytes.
 * Returns the number of bytes written, or -1 on error.
 */
static int ber_encode_oid(const char *oid_str, unsigned char *out, size_t out_size)
{
    unsigned int arcs[64];
    int arc_count = 0;

    const char *p = oid_str;
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

    /* First two arcs encoded as 40 * arc0 + arc1 in base-128. */
    unsigned int first = 40u * arcs[0] + arcs[1];

    /* Encode first combined arc in base-128. */
    if (first < 0x80u)
    {
        if (pos >= out_size)
            return -1;
        out[pos++] = (unsigned char)first;
    }
    else
    {
        /* Determine number of base-128 digits needed. */
        unsigned int tmp = first;
        int nbytes = 0;
        while (tmp > 0)
        {
            nbytes++;
            tmp >>= 7;
        }
        if (pos + (size_t)nbytes > out_size)
            return -1;
        for (int i = nbytes - 1; i >= 0; i--)
        {
            out[pos + (size_t)i] = (unsigned char)(first & 0x7fu);
            if (i != nbytes - 1)
                out[pos + (size_t)i] |= 0x80u;
            first >>= 7;
        }
        pos += (size_t)nbytes;
    }

    /* Encode remaining arcs. */
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
            unsigned int tmp2 = arc;
            int nb = 0;
            while (tmp2 > 0)
            {
                nb++;
                tmp2 >>= 7;
            }
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
 * Append bytes to a buffer.  Returns 0 on success, -1 if out of space.
 */
static int buf_put(unsigned char *buf, size_t *pos, size_t cap,
                   const unsigned char *data, size_t len)
{
    if (*pos + len > cap)
        return -1;
    memcpy(buf + *pos, data, len);
    *pos += len;
    return 0;
}

static int buf_put1(unsigned char *buf, size_t *pos, size_t cap, unsigned char b)
{
    return buf_put(buf, pos, cap, &b, 1);
}

/*
 * Build a complete SNMPv1 GetRequest packet for the given OIDs.
 * Returns total packet length, or -1 on error.
 *
 * Packet structure (BER):
 *   SEQUENCE {
 *     INTEGER 0          -- version v1
 *     OCTET STRING "public"
 *     GetRequest-PDU {
 *       INTEGER request-id
 *       INTEGER 0        -- error-status
 *       INTEGER 0        -- error-index
 *       SEQUENCE {       -- varbind-list
 *         SEQUENCE { OID NULL }
 *         ...
 *       }
 *     }
 *   }
 */
static int build_snmp_get(const char **oids, int oid_count,
                           unsigned char *out, size_t out_size)
{
    static unsigned int request_id = 1;

    /* Scratch buffer — OIDs rarely exceed 32 bytes each, 16 OIDs max. */
    unsigned char varbind_buf[512];
    size_t        vb_pos = 0;

    /* Build each varbind: SEQUENCE { OID NULL }. */
    for (int i = 0; i < oid_count; i++)
    {
        unsigned char oid_bytes[64];
        int oid_len = ber_encode_oid(oids[i], oid_bytes, sizeof(oid_bytes));
        if (oid_len < 0)
            return -1;

        /* varbind body = OID TLV + NULL TLV */
        unsigned char vb_body[128];
        size_t        vb_body_pos = 0;

        /* OID TLV */
        unsigned char len_buf[4];
        int           len_bytes = ber_encode_len(len_buf, oid_len);
        if (len_bytes < 0)
            return -1;
        vb_body[vb_body_pos++] = BER_OID;
        memcpy(vb_body + vb_body_pos, len_buf, (size_t)len_bytes);
        vb_body_pos += (size_t)len_bytes;
        memcpy(vb_body + vb_body_pos, oid_bytes, (size_t)oid_len);
        vb_body_pos += (size_t)oid_len;

        /* NULL TLV */
        vb_body[vb_body_pos++] = BER_NULL;
        vb_body[vb_body_pos++] = 0x00u;

        /* Wrap in SEQUENCE */
        if (buf_put1(varbind_buf, &vb_pos, sizeof(varbind_buf), BER_SEQ) < 0)
            return -1;
        len_bytes = ber_encode_len(len_buf, (int)vb_body_pos);
        if (len_bytes < 0)
            return -1;
        if (buf_put(varbind_buf, &vb_pos, sizeof(varbind_buf),
                    len_buf, (size_t)len_bytes) < 0)
            return -1;
        if (buf_put(varbind_buf, &vb_pos, sizeof(varbind_buf),
                    vb_body, vb_body_pos) < 0)
            return -1;
    }

    /* Build varbind-list SEQUENCE. */
    unsigned char vbl_buf[512];
    size_t        vbl_pos = 0;
    {
        unsigned char len_buf[4];
        int           len_bytes = ber_encode_len(len_buf, (int)vb_pos);
        if (len_bytes < 0)
            return -1;
        if (buf_put1(vbl_buf, &vbl_pos, sizeof(vbl_buf), BER_SEQ) < 0)
            return -1;
        if (buf_put(vbl_buf, &vbl_pos, sizeof(vbl_buf),
                    len_buf, (size_t)len_bytes) < 0)
            return -1;
        if (buf_put(vbl_buf, &vbl_pos, sizeof(vbl_buf), varbind_buf, vb_pos) < 0)
            return -1;
    }

    /* Build PDU body: request-id + error-status + error-index + varbind-list. */
    unsigned char pdu_body[600];
    size_t        pdu_body_pos = 0;
    {
        /* request-id: INTEGER 4 bytes big-endian */
        unsigned int rid = request_id++;
        unsigned char rid_bytes[6];
        rid_bytes[0] = BER_INT;
        rid_bytes[1] = 0x04u;
        rid_bytes[2] = (unsigned char)((rid >> 24) & 0xffu);
        rid_bytes[3] = (unsigned char)((rid >> 16) & 0xffu);
        rid_bytes[4] = (unsigned char)((rid >> 8)  & 0xffu);
        rid_bytes[5] = (unsigned char)( rid        & 0xffu);
        if (buf_put(pdu_body, &pdu_body_pos, sizeof(pdu_body), rid_bytes, 6) < 0)
            return -1;

        /* error-status = 0 */
        unsigned char zero_int[] = { BER_INT, 0x01u, 0x00u };
        if (buf_put(pdu_body, &pdu_body_pos, sizeof(pdu_body), zero_int, 3) < 0)
            return -1;

        /* error-index = 0 */
        if (buf_put(pdu_body, &pdu_body_pos, sizeof(pdu_body), zero_int, 3) < 0)
            return -1;

        /* varbind-list */
        if (buf_put(pdu_body, &pdu_body_pos, sizeof(pdu_body), vbl_buf, vbl_pos) < 0)
            return -1;
    }

    /* Build PDU TLV (GetRequest type 0xa0). */
    unsigned char pdu_buf[700];
    size_t        pdu_pos = 0;
    {
        unsigned char len_buf[4];
        int           len_bytes = ber_encode_len(len_buf, (int)pdu_body_pos);
        if (len_bytes < 0)
            return -1;
        if (buf_put1(pdu_buf, &pdu_pos, sizeof(pdu_buf), BER_SNMP_GET) < 0)
            return -1;
        if (buf_put(pdu_buf, &pdu_pos, sizeof(pdu_buf),
                    len_buf, (size_t)len_bytes) < 0)
            return -1;
        if (buf_put(pdu_buf, &pdu_pos, sizeof(pdu_buf), pdu_body, pdu_body_pos) < 0)
            return -1;
    }

    /* Build outer message body: version + community + pdu. */
    unsigned char msg_body[800];
    size_t        msg_body_pos = 0;
    {
        /* version: INTEGER 1 byte value 0 */
        unsigned char ver[] = { BER_INT, 0x01u, 0x00u };
        if (buf_put(msg_body, &msg_body_pos, sizeof(msg_body), ver, 3) < 0)
            return -1;

        /* community: OCTET STRING "public" */
        static const unsigned char community[] =
            { BER_OCTET_STR, 0x06u, 'p','u','b','l','i','c' };
        if (buf_put(msg_body, &msg_body_pos, sizeof(msg_body),
                    community, sizeof(community)) < 0)
            return -1;

        /* PDU */
        if (buf_put(msg_body, &msg_body_pos, sizeof(msg_body), pdu_buf, pdu_pos) < 0)
            return -1;
    }

    /* Wrap everything in outer SEQUENCE. */
    size_t out_pos = 0;
    {
        unsigned char len_buf[4];
        int           len_bytes = ber_encode_len(len_buf, (int)msg_body_pos);
        if (len_bytes < 0)
            return -1;
        if (buf_put1(out, &out_pos, out_size, BER_SEQ) < 0)
            return -1;
        if (buf_put(out, &out_pos, out_size, len_buf, (size_t)len_bytes) < 0)
            return -1;
        if (buf_put(out, &out_pos, out_size, msg_body, msg_body_pos) < 0)
            return -1;
    }

    return (int)out_pos;
}

/* ── BER response navigation helpers ─────────────────────────────────────── */

/*
 * Read a BER definite-length field from resp[*pos].
 * Advances *pos past the length bytes.
 * Returns the decoded length, or -1 on error.
 */
static int ber_read_len(const unsigned char *resp, int resp_len, int *pos)
{
    if (*pos >= resp_len)
        return -1;
    unsigned char first = resp[(*pos)++];
    if (first < 0x80u)
        return (int)first;
    if (first == 0x81u)
    {
        if (*pos >= resp_len)
            return -1;
        return (int)resp[(*pos)++];
    }
    if (first == 0x82u)
    {
        if (*pos + 1 >= resp_len)
            return -1;
        int hi = (int)resp[(*pos)++];
        int lo = (int)resp[(*pos)++];
        return (hi << 8) | lo;
    }
    /* Lengths >= 3 octets not expected in SNMP. */
    return -1;
}

/*
 * Skip one complete TLV at resp[*pos].
 * Advances *pos past the TLV.
 * Returns 0 on success, -1 on error.
 */
static int ber_skip_tlv(const unsigned char *resp, int resp_len, int *pos)
{
    if (*pos >= resp_len)
        return -1;
    (*pos)++;  /* skip tag */
    int len = ber_read_len(resp, resp_len, pos);
    if (len < 0)
        return -1;
    if (*pos + len > resp_len)
        return -1;
    *pos += len;
    return 0;
}

/*
 * Parse the value of a varbind at the given 0-based index in the SNMP response.
 *
 * Navigates the BER structure:
 *   SEQUENCE (outer message)
 *     INTEGER version
 *     OCTET STRING community
 *     GetResponse-PDU (0xa2)
 *       INTEGER request-id
 *       INTEGER error-status
 *       INTEGER error-index
 *       SEQUENCE varbind-list
 *         SEQUENCE varbind[0]
 *           OID
 *           value-TLV
 *         SEQUENCE varbind[1]
 *           ...
 *
 * Fills *int_out  if the value is INTEGER / Counter32 / Gauge32.
 * Fills str_out   if the value is OCTET STRING.
 * Returns 0 on success, -1 on error or index out of range.
 */
static int parse_snmp_response(const unsigned char *resp, int resp_len,
                                int oid_index,
                                int *int_out, char *str_out, size_t str_out_size)
{
    int pos = 0;

    /* Outer SEQUENCE */
    if (pos >= resp_len || resp[pos++] != BER_SEQ)
        return -1;
    if (ber_read_len(resp, resp_len, &pos) < 0)
        return -1;

    /* Skip version INTEGER */
    if (ber_skip_tlv(resp, resp_len, &pos) < 0)
        return -1;

    /* Skip community OCTET STRING */
    if (ber_skip_tlv(resp, resp_len, &pos) < 0)
        return -1;

    /* GetResponse-PDU (tag 0xa2) */
    if (pos >= resp_len || resp[pos++] != BER_SNMP_RESP)
        return -1;
    if (ber_read_len(resp, resp_len, &pos) < 0)
        return -1;

    /* Skip request-id, error-status, error-index */
    if (ber_skip_tlv(resp, resp_len, &pos) < 0)
        return -1;
    if (ber_skip_tlv(resp, resp_len, &pos) < 0)
        return -1;
    if (ber_skip_tlv(resp, resp_len, &pos) < 0)
        return -1;

    /* Varbind-list SEQUENCE */
    if (pos >= resp_len || resp[pos++] != BER_SEQ)
        return -1;
    if (ber_read_len(resp, resp_len, &pos) < 0)
        return -1;

    /* Walk varbinds until we reach oid_index. */
    for (int i = 0; ; i++)
    {
        if (pos >= resp_len)
            return -1;

        /* Varbind SEQUENCE */
        if (resp[pos++] != BER_SEQ)
            return -1;
        int vb_len = ber_read_len(resp, resp_len, &pos);
        if (vb_len < 0)
            return -1;
        int vb_end = pos + vb_len;

        /* Skip OID TLV */
        if (ber_skip_tlv(resp, resp_len, &pos) < 0)
            return -1;

        if (i == oid_index)
        {
            /* Parse the value TLV. */
            if (pos >= resp_len)
                return -1;
            unsigned char vtag = resp[pos++];
            int vlen = ber_read_len(resp, resp_len, &pos);
            if (vlen < 0 || pos + vlen > resp_len)
                return -1;

            if ((vtag == BER_INT || vtag == BER_COUNTER32 || vtag == BER_GAUGE32)
                && int_out != NULL)
            {
                /* Big-endian signed/unsigned integer. */
                int val = 0;
                /* Handle sign extension for BER_INT. */
                if (vtag == BER_INT && vlen > 0 && (resp[pos] & 0x80u))
                    val = -1;
                for (int b = 0; b < vlen; b++)
                    val = (val << 8) | (int)resp[pos + b];
                *int_out = val;
                return 0;
            }

            if (vtag == BER_OCTET_STR && str_out != NULL && str_out_size > 0)
            {
                size_t copy = (size_t)vlen < str_out_size - 1u
                              ? (size_t)vlen : str_out_size - 1u;
                memcpy(str_out, resp + pos, copy);
                str_out[copy] = '\0';
                return 0;
            }

            return -1;
        }

        /* Not this varbind — skip the value TLV and advance to the next. */
        pos = vb_end;
    }
}

/* ── UDP send/receive ────────────────────────────────────────────────────── */

/*
 * Open a UDP socket, send req to ip:BROTHER_SNMP_PORT, wait up to timeout_ms
 * for a response.  Returns number of bytes received, 0 on timeout, -1 on error.
 */
static int snmp_send_recv(const char *ip,
                           const unsigned char *req, int req_len,
                           unsigned char *resp, int resp_size,
                           int timeout_ms)
{
    __attribute__((cleanup(close_fd))) int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        LOG_ERROR_MSG("brother: socket: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in local = {0};
    local.sin_family      = AF_INET;
    local.sin_port        = htons(0);
    local.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0)
    {
        LOG_ERROR_MSG("brother: bind: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(BROTHER_SNMP_PORT);
    if (inet_pton(AF_INET, ip, &dest.sin_addr) != 1)
    {
        LOG_ERROR_MSG("brother: invalid IP address: %s", ip);
        return -1;
    }

    ssize_t sent = sendto(fd, req, (size_t)req_len, 0,
                          (struct sockaddr *)&dest, sizeof(dest));
    if (sent < 0)
    {
        LOG_ERROR_MSG("brother: sendto: %s", strerror(errno));
        return -1;
    }

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    int sel = select(fd + 1, &fds, NULL, NULL, &tv);
    if (sel < 0)
    {
        LOG_ERROR_MSG("brother: select: %s", strerror(errno));
        return -1;
    }
    if (sel == 0)
        return 0;  /* timeout */

    ssize_t n = recvfrom(fd, resp, (size_t)resp_size, 0, NULL, NULL);
    if (n < 0)
    {
        if (errno == ECONNREFUSED)
            return 0;  /* ICMP port-unreachable → treat as timeout/unreachable */
        LOG_ERROR_MSG("brother: recvfrom: %s", strerror(errno));
        return -1;
    }
    return (int)n;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int brother_probe(const char *ip, char *model, size_t model_len)
{
    static const char *probe_oids[] = { OID_SYS_DESCR };

    unsigned char req[256];
    int req_len = build_snmp_get(probe_oids, 1, req, sizeof(req));
    if (req_len < 0)
    {
        LOG_ERROR_MSG("brother: failed to build SNMP probe packet");
        return -1;
    }

    unsigned char resp[512];
    int n = snmp_send_recv(ip, req, req_len, resp, sizeof(resp), 2000);
    if (n < 0)
        return -1;
    if (n == 0)
        return 0;  /* timeout → unreachable */

    if (model != NULL && model_len > 0)
    {
        if (parse_snmp_response(resp, n, 0, NULL, model, model_len) < 0)
            model[0] = '\0';
    }

    return 1;
}

int brother_get_status(const char *ip, BrotherStatus *out)
{
    static const char *status_oids[] =
    {
        OID_PRINTER_STATUS,
        OID_PAGE_COUNT,
        OID_TONER_CUR,
        OID_TONER_MAX,
        OID_TONER_LOW,
    };
    static const int OID_COUNT = 5;

    out->state      = 0;
    out->toner_pct  = -1;
    out->toner_low  = -1;
    out->page_count = -1;
    out->model[0]   = '\0';

    unsigned char req[512];
    int req_len = build_snmp_get(status_oids, OID_COUNT, req, sizeof(req));
    if (req_len < 0)
    {
        LOG_ERROR_MSG("brother: failed to build SNMP status packet");
        return -1;
    }

    unsigned char resp[1024];
    int n = snmp_send_recv(ip, req, req_len, resp, sizeof(resp), 5000);
    if (n <= 0)
    {
        LOG_WARN_MSG("brother: no SNMP response from %s", ip);
        return -1;
    }

    int val;

    /* varbind 0: printer status */
    val = 0;
    if (parse_snmp_response(resp, n, 0, &val, NULL, 0) == 0)
        out->state = val;

    /* varbind 1: page count */
    val = 0;
    if (parse_snmp_response(resp, n, 1, &val, NULL, 0) == 0)
        out->page_count = val;

    /* varbind 2 & 3: toner current and max */
    int toner_cur = -1;
    int toner_max = -1;
    val = 0;
    if (parse_snmp_response(resp, n, 2, &val, NULL, 0) == 0)
        toner_cur = val;
    val = 0;
    if (parse_snmp_response(resp, n, 3, &val, NULL, 0) == 0)
        toner_max = val;

    if (toner_max > 0 && toner_cur >= 0)
        out->toner_pct = toner_cur * 100 / toner_max;

    /* varbind 4: toner low flag */
    val = 0;
    if (parse_snmp_response(resp, n, 4, &val, NULL, 0) == 0)
        out->toner_low = val;

    return 0;
}

int brother_get_consumables(const char *ip, BrotherConsumables *out)
{
    static const char *oids[] =
    {
        OID_TONER_CUR,
        OID_TONER_MAX,
        OID_DRUM_INFO,
        OID_MAINT_NEXT,
    };

    out->toner_pct         = -1;
    out->drum_pct          = -1;
    out->pages_until_maint = -1;

    unsigned char req[512];
    int req_len = build_snmp_get(oids, 4, req, sizeof(req));
    if (req_len < 0)
    {
        LOG_ERROR_MSG("brother: failed to build consumables SNMP packet");
        return -1;
    }

    unsigned char resp[1024];
    int n = snmp_send_recv(ip, req, req_len, resp, sizeof(resp), 5000);
    if (n <= 0)
    {
        LOG_WARN_MSG("brother: no SNMP response from %s", ip);
        return -1;
    }

    int val;

    /* varbinds 0 & 1: toner current and max */
    int toner_cur = -1, toner_max = -1;
    val = 0;
    if (parse_snmp_response(resp, n, 0, &val, NULL, 0) == 0)
        toner_cur = val;
    val = 0;
    if (parse_snmp_response(resp, n, 1, &val, NULL, 0) == 0)
        toner_max = val;
    if (toner_max > 0 && toner_cur >= 0)
        out->toner_pct = toner_cur * 100 / toner_max;

    /* varbind 2: drum OctetString — first 2 bytes LE uint16 / 100 = percent */
    char raw[8] = {0};
    if (parse_snmp_response(resp, n, 2, NULL, raw, sizeof(raw)) == 0)
    {
        unsigned int le = (unsigned int)(unsigned char)raw[0]
                        | ((unsigned int)(unsigned char)raw[1] << 8);
        out->drum_pct = (int)(le / 100u);
        if (out->drum_pct > 100)
            out->drum_pct = 100;
    }

    /* varbind 3: pages until maintenance — first 2 bytes LE uint16 */
    memset(raw, 0, sizeof(raw));
    if (parse_snmp_response(resp, n, 3, NULL, raw, sizeof(raw)) == 0)
    {
        unsigned int le = (unsigned int)(unsigned char)raw[0]
                        | ((unsigned int)(unsigned char)raw[1] << 8);
        out->pages_until_maint = (int)le;
    }

    return 0;
}
