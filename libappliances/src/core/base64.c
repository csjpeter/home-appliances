#include "base64.h"

#include <string.h>

static const char B64_ENC[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int base64_encode(const unsigned char *src, size_t src_len,
                  char *out, size_t out_len)
{
    if (out_len < base64_encoded_len(src_len))
        return -1;

    size_t i = 0, j = 0;
    for (; i + 2 < src_len; i += 3) {
        unsigned int v = ((unsigned int)src[i]   << 16)
                       | ((unsigned int)src[i+1] <<  8)
                       |  (unsigned int)src[i+2];
        out[j++] = B64_ENC[(v >> 18) & 0x3fu];
        out[j++] = B64_ENC[(v >> 12) & 0x3fu];
        out[j++] = B64_ENC[(v >>  6) & 0x3fu];
        out[j++] = B64_ENC[ v        & 0x3fu];
    }
    if (i < src_len) {
        unsigned int v = (unsigned int)src[i] << 16;
        if (i + 1 < src_len) v |= (unsigned int)src[i+1] << 8;
        out[j++] = B64_ENC[(v >> 18) & 0x3fu];
        out[j++] = B64_ENC[(v >> 12) & 0x3fu];
        out[j++] = (i + 1 < src_len) ? B64_ENC[(v >> 6) & 0x3fu] : '=';
        out[j++] = '=';
    }
    out[j] = '\0';
    return (int)j;
}

int base64_decode(const char *src, unsigned char *out, size_t out_len)
{
    size_t src_len = strlen(src);
    if (src_len % 4 != 0)
        return -1;
    if (out_len < base64_decoded_len(src_len))
        return -1;

    size_t j = 0;
    for (size_t i = 0; i < src_len; i += 4) {
        int v0 = b64_val(src[i]);
        int v1 = b64_val(src[i+1]);
        int v2 = (src[i+2] == '=') ? 0 : b64_val(src[i+2]);
        int v3 = (src[i+3] == '=') ? 0 : b64_val(src[i+3]);

        if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0)
            return -1;

        unsigned int v = ((unsigned int)v0 << 18)
                       | ((unsigned int)v1 << 12)
                       | ((unsigned int)v2 <<  6)
                       |  (unsigned int)v3;

        out[j++] = (unsigned char)((v >> 16) & 0xffu);
        if (src[i+2] != '=') out[j++] = (unsigned char)((v >> 8) & 0xffu);
        if (src[i+3] != '=') out[j++] = (unsigned char)( v       & 0xffu);
    }
    return (int)j;
}
