#ifndef BASE64_H
#define BASE64_H

/**
 * @file base64.h
 * @brief Base64 encode/decode — standard alphabet (RFC 4648), '=' padded.
 */

#include <stddef.h>

/**
 * @brief Minimum output buffer size (including NUL) for base64_encode.
 */
static inline size_t base64_encoded_len(size_t src_len)
{
    return ((src_len + 2) / 3) * 4 + 1;
}

/**
 * @brief Upper bound on decoded byte count for a base64 string of length b64_len.
 */
static inline size_t base64_decoded_len(size_t b64_len)
{
    return (b64_len / 4) * 3;
}

/**
 * @brief Encode bytes to a NUL-terminated Base64 string.
 * @param src     Input bytes.
 * @param src_len Number of input bytes.
 * @param out     Caller-allocated output buffer (at least base64_encoded_len(src_len)).
 * @param out_len Capacity of out in bytes.
 * @return Number of Base64 characters written (excluding NUL), or -1 on error.
 */
int base64_encode(const unsigned char *src, size_t src_len,
                  char *out, size_t out_len);

/**
 * @brief Decode a NUL-terminated Base64 string to bytes.
 * @param src     NUL-terminated Base64 input.
 * @param out     Caller-allocated output buffer (at least base64_decoded_len(strlen(src))).
 * @param out_len Capacity of out in bytes.
 * @return Number of bytes written, or -1 on invalid input or insufficient buffer.
 */
int base64_decode(const char *src, unsigned char *out, size_t out_len);

#endif /* BASE64_H */
