#include "../common/test_helpers.h"
#include "core/base64.h"

#include <string.h>

/* RFC 4648 §10 test vectors */
static void test_encode_rfc_vectors(void)
{
    char out[64];

    /* Empty */
    ASSERT(base64_encode((const unsigned char *)"", 0, out, sizeof(out)) == 0,
           "encode empty returns 0");
    ASSERT(out[0] == '\0', "encode empty is NUL");

    /* "f" -> "Zg==" */
    ASSERT(base64_encode((const unsigned char *)"f", 1, out, sizeof(out)) == 4,
           "encode 'f' length");
    ASSERT(strcmp(out, "Zg==") == 0, "encode 'f' value");

    /* "fo" -> "Zm8=" */
    ASSERT(base64_encode((const unsigned char *)"fo", 2, out, sizeof(out)) == 4,
           "encode 'fo' length");
    ASSERT(strcmp(out, "Zm8=") == 0, "encode 'fo' value");

    /* "foo" -> "Zm9v" */
    ASSERT(base64_encode((const unsigned char *)"foo", 3, out, sizeof(out)) == 4,
           "encode 'foo' length");
    ASSERT(strcmp(out, "Zm9v") == 0, "encode 'foo' value");

    /* "foobar" -> "Zm9vYmFy" */
    ASSERT(base64_encode((const unsigned char *)"foobar", 6, out, sizeof(out)) == 8,
           "encode 'foobar' length");
    ASSERT(strcmp(out, "Zm9vYmFy") == 0, "encode 'foobar' value");

    /* "Man" -> "TWFu" */
    ASSERT(base64_encode((const unsigned char *)"Man", 3, out, sizeof(out)) == 4,
           "encode 'Man' length");
    ASSERT(strcmp(out, "TWFu") == 0, "encode 'Man' value");
}

static void test_decode_rfc_vectors(void)
{
    unsigned char out[64];
    int n;

    /* Empty */
    n = base64_decode("", out, sizeof(out));
    ASSERT(n == 0, "decode empty returns 0");

    /* "Zg==" -> "f" */
    n = base64_decode("Zg==", out, sizeof(out));
    ASSERT(n == 1, "decode 'Zg==' length");
    ASSERT(out[0] == 'f', "decode 'Zg==' value");

    /* "Zm8=" -> "fo" */
    n = base64_decode("Zm8=", out, sizeof(out));
    ASSERT(n == 2, "decode 'Zm8=' length");
    ASSERT(out[0] == 'f' && out[1] == 'o', "decode 'Zm8=' value");

    /* "Zm9v" -> "foo" */
    n = base64_decode("Zm9v", out, sizeof(out));
    ASSERT(n == 3, "decode 'Zm9v' length");
    ASSERT(memcmp(out, "foo", 3) == 0, "decode 'Zm9v' value");

    /* "TWFu" -> "Man" */
    n = base64_decode("TWFu", out, sizeof(out));
    ASSERT(n == 3, "decode 'TWFu' length");
    ASSERT(memcmp(out, "Man", 3) == 0, "decode 'TWFu' value");
}

static void test_roundtrip(void)
{
    /* Round-trip: encode then decode restores original bytes */
    const unsigned char src[] = {
        0x00, 0x01, 0x7f, 0x80, 0xff, 0xde, 0xad, 0xbe,
        0xef, 0x42, 0x13, 0x37, 0xa5, 0x5a, 0xc3, 0x3c,
        0x61, 0x62, 0x63 /* "abc" */
    };
    const size_t src_len = sizeof(src);

    char enc[64];
    int enc_len = base64_encode(src, src_len, enc, sizeof(enc));
    ASSERT(enc_len > 0, "roundtrip: encode succeeds");

    unsigned char dec[64];
    int dec_len = base64_decode(enc, dec, sizeof(dec));
    ASSERT(dec_len == (int)src_len, "roundtrip: decoded length matches");
    ASSERT(memcmp(dec, src, src_len) == 0, "roundtrip: decoded bytes match");
}

static void test_error_cases(void)
{
    unsigned char out[64];
    char enc[16];

    /* Buffer too small for encode */
    ASSERT(base64_encode((const unsigned char *)"foo", 3, enc, 4) == -1,
           "encode: buffer too small (need 5)");

    /* Exactly right size */
    ASSERT(base64_encode((const unsigned char *)"foo", 3, enc, 5) == 4,
           "encode: exact buffer size works");

    /* Invalid character in decode */
    ASSERT(base64_decode("Zm9!", out, sizeof(out)) == -1,
           "decode: invalid char returns -1");

    /* Wrong length (not multiple of 4) */
    ASSERT(base64_decode("Zm9", out, sizeof(out)) == -1,
           "decode: length not multiple of 4 returns -1");
}

void run_base64_tests(void)
{
    printf("base64 tests:\n");
    RUN_TEST(test_encode_rfc_vectors);
    RUN_TEST(test_decode_rfc_vectors);
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_error_cases);
}
