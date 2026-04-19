#include "../common/test_helpers.h"
#include "infrastructure/gree_client.h"
#include "infrastructure/roborock_client.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Temp file helpers ────────────────────────────────────────────────── */

static char g_tmp_home[256];
static char g_tmp_dir[300];

static int setup_tmp_home(void)
{
    snprintf(g_tmp_home, sizeof(g_tmp_home), "/tmp/ha_test_%d", (int)getpid());
    snprintf(g_tmp_dir,  sizeof(g_tmp_dir),  "%s/.config/home-appliances", g_tmp_home);
    if (mkdir(g_tmp_home, 0700) != 0) return -1;
    if (mkdir(g_tmp_home, 0700) != 0 && errno != EEXIST) return -1;
    /* create nested dirs */
    char mid[280];
    snprintf(mid, sizeof(mid), "%s/.config", g_tmp_home);
    mkdir(mid, 0700);
    mkdir(g_tmp_dir, 0700);
    setenv("HOME", g_tmp_home, 1);
    return 0;
}

static void write_gree_store(const char *content)
{
    char path[400];
    snprintf(path, sizeof(path), "%s/gree_devices", g_tmp_dir);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(content, f);
    fclose(f);
}

static void write_roborock_store(const char *content)
{
    char path[400];
    snprintf(path, sizeof(path), "%s/roborock_devices", g_tmp_dir);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(content, f);
    fclose(f);
}

static void cleanup_tmp_home(void)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_tmp_home);
    if (system(cmd) != 0) { /* best-effort cleanup */ }
}

/* ── Gree store tests ────────────────────────────────────────────────── */

static void test_gree_load_empty_file(void)
{
    write_gree_store("");
    GreeDeviceList list = {0};
    int rc = gree_client_load_bound(&list);
    ASSERT(rc == 0,         "gree_load empty: returns 0");
    ASSERT(list.count == 0, "gree_load empty: count=0");
    gree_device_list_free(&list);
}

static void test_gree_load_comment_only(void)
{
    write_gree_store("# ip mac device_key\n# another comment\n");
    GreeDeviceList list = {0};
    int rc = gree_client_load_bound(&list);
    ASSERT(rc == 0,         "gree_load comment: returns 0");
    ASSERT(list.count == 0, "gree_load comment: count=0");
    gree_device_list_free(&list);
}

static void test_gree_load_valid(void)
{
    write_gree_store("192.168.1.10 aabbccddee01 0123456789abcdef\n");
    GreeDeviceList list = {0};
    int rc = gree_client_load_bound(&list);
    ASSERT(rc == 0,         "gree_load valid: returns 0");
    ASSERT(list.count == 1, "gree_load valid: count=1");
    if (list.count == 1) {
        ASSERT(strcmp(list.devices[0].ip,  "192.168.1.10")     == 0, "gree_load: correct ip");
        ASSERT(strcmp(list.devices[0].mac, "aabbccddee01")     == 0, "gree_load: correct mac");
        ASSERT(list.devices[0].bound == 1,                           "gree_load: bound=1");
    }
    gree_device_list_free(&list);
}

static void test_gree_load_corrupt_key(void)
{
    /* Key is only 8 chars instead of 16 — should be skipped with warning. */
    write_gree_store("192.168.1.10 aabbccddee01 short\n");
    GreeDeviceList list = {0};
    int rc = gree_client_load_bound(&list);
    ASSERT(rc == 0,         "gree_load corrupt key: returns 0 (not error)");
    ASSERT(list.count == 0, "gree_load corrupt key: corrupt entry skipped");
    gree_device_list_free(&list);
}

static void test_gree_load_mixed(void)
{
    /* One corrupt, one valid — only the valid one should be loaded. */
    write_gree_store(
        "192.168.1.10 aabbccddee01 badkey\n"
        "192.168.1.11 aabbccddee02 0123456789abcdef\n"
    );
    GreeDeviceList list = {0};
    int rc = gree_client_load_bound(&list);
    ASSERT(rc == 0,         "gree_load mixed: returns 0");
    ASSERT(list.count == 1, "gree_load mixed: only valid entry loaded");
    if (list.count == 1)
        ASSERT(strcmp(list.devices[0].ip, "192.168.1.11") == 0, "gree_load mixed: correct ip");
    gree_device_list_free(&list);
}

static void test_gree_save_chmod(void)
{
    GreeDevice dev = {0};
    memcpy(dev.ip,         "192.168.1.20",    12);
    memcpy(dev.mac,        "aabbccddee03",    12);
    memcpy(dev.device_key, "0123456789abcdef", 16);
    dev.bound = 1;

    GreeDeviceList list = { .devices = &dev, .count = 1 };
    int rc = gree_client_save_bound(&list);
    ASSERT(rc == 0, "gree_save: returns 0");

    char path[400];
    snprintf(path, sizeof(path), "%s/gree_devices", g_tmp_dir);
    struct stat st;
    ASSERT(stat(path, &st) == 0, "gree_save: file exists");
    ASSERT((st.st_mode & 0777) == 0600, "gree_save: file permissions are 0600");
}

/* ── Roborock store tests ─────────────────────────────────────────────── */

static void test_roborock_load_valid(void)
{
    /* 32-char hex token + 8-char device id */
    write_roborock_store("192.168.1.50 0102030405060708090a0b0c0d0e0f10 deadbeef\n");
    RoborockDevice dev = {0};
    int rc = roborock_load("192.168.1.50", &dev);
    ASSERT(rc == 0,              "roborock_load valid: returns 0");
    ASSERT(dev.token_valid == 1, "roborock_load valid: token_valid=1");
    ASSERT(dev.device_id == 0xDEADBEEFu, "roborock_load valid: device_id correct");
}

static void test_roborock_load_not_found(void)
{
    write_roborock_store("192.168.1.50 0102030405060708090a0b0c0d0e0f10 deadbeef\n");
    RoborockDevice dev = {0};
    int rc = roborock_load("192.168.1.99", &dev);
    ASSERT(rc == 1, "roborock_load not found: returns 1");
}

static void test_roborock_load_corrupt_token(void)
{
    /* 16-char hex (too short — must be 32) — should be skipped. */
    write_roborock_store("192.168.1.50 0102030405060708 deadbeef\n");
    RoborockDevice dev = {0};
    int rc = roborock_load("192.168.1.50", &dev);
    /* corrupt entry is skipped → "not found" = 1 */
    ASSERT(rc == 1, "roborock_load corrupt token: entry skipped (returns 1)");
}

static void test_roborock_save_chmod(void)
{
    RoborockDevice dev = {0};
    memcpy(dev.ip, "192.168.1.60", 12);
    static const unsigned char token[16] = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
        0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10
    };
    memcpy(dev.token, token, 16);
    dev.device_id   = 0x12345678u;
    dev.token_valid = 1;

    int rc = roborock_save(&dev);
    ASSERT(rc == 0, "roborock_save: returns 0");

    char path[400];
    snprintf(path, sizeof(path), "%s/roborock_devices", g_tmp_dir);
    struct stat st;
    ASSERT(stat(path, &st) == 0, "roborock_save: file exists");
    ASSERT((st.st_mode & 0777) == 0600, "roborock_save: file permissions are 0600");
}

/* ── Entry point ─────────────────────────────────────────────────────── */

void run_device_store_tests(void)
{
    printf("device store tests:\n");

    if (setup_tmp_home() != 0) {
        printf("  SKIP: cannot create temp directory\n");
        return;
    }

    RUN_TEST(test_gree_load_empty_file);
    RUN_TEST(test_gree_load_comment_only);
    RUN_TEST(test_gree_load_valid);
    RUN_TEST(test_gree_load_corrupt_key);
    RUN_TEST(test_gree_load_mixed);
    RUN_TEST(test_gree_save_chmod);

    RUN_TEST(test_roborock_load_valid);
    RUN_TEST(test_roborock_load_not_found);
    RUN_TEST(test_roborock_load_corrupt_token);
    RUN_TEST(test_roborock_save_chmod);

    cleanup_tmp_home();
}
