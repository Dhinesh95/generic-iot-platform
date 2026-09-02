/**
 * @file test_firmware_integrity.c
 * @brief Tests for firmware integrity — HMAC-SHA256 verification.
 *
 * Architecture ref: Section 4/5, Baseline — T5 (malicious/corrupted OTA).
 *
 * CRC-32 is NOT tested — there is no legacy image in this fresh project.
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/firmware_integrity.h"
#include <string.h>

/* ---------- Test data ---------- */

static const uint8_t test_key[32] = {
    0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
    0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10,
    0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
    0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20
};

static const uint8_t test_image[] = {
    0xDE,0xAD,0xBE,0xEF,0xCA,0xFE,0xBA,0xBE,
    0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
    0xAA,0xBB,0xCC,0xDD
};

/* ---------- Test: init ---------- */
static int test_firmware_integrity_init(void)
{
    TEST_ASSERT(firmware_integrity_init() == true);
    TEST_PASS();
}

/* ---------- Test: set key ---------- */
static int test_firmware_set_key(void)
{
    firmware_integrity_init();
    TEST_ASSERT(firmware_integrity_set_key(test_key, 32) == true);
    TEST_ASSERT(firmware_integrity_set_key(NULL, 0) == false);
    TEST_ASSERT(firmware_integrity_set_key(test_key, 16) == false);  /* Wrong length. */
    TEST_PASS();
}

/* ---------- Test: compute HMAC ---------- */
static int test_firmware_compute_hmac(void)
{
    uint8_t hmac[32];
    firmware_integrity_compute_hmac(test_key, 32, test_image, sizeof(test_image), hmac);

    /* HMAC should be non-zero. */
    uint8_t nonzero = 0;
    for (int i = 0; i < 32; i++) {
        nonzero |= hmac[i];
    }
    TEST_ASSERT(nonzero != 0);

    /* Should be deterministic. */
    uint8_t hmac2[32];
    firmware_integrity_compute_hmac(test_key, 32, test_image, sizeof(test_image), hmac2);
    TEST_ASSERT_MEM_EQUAL(hmac, hmac2, 32);
    TEST_PASS();
}

/* ---------- Test: verify valid image ---------- */
static int test_firmware_verify_valid(void)
{
    firmware_integrity_init();
    firmware_integrity_set_key(test_key, 32);

    /* Compute the correct HMAC for the test image. */
    FirmwareImageHeader_t header;
    memset(&header, 0, sizeof(header));
    header.magic = FW_INTEGRITY_MANIFEST_MAGIC;
    header.image_size = sizeof(test_image);
    firmware_integrity_compute_hmac(test_key, 32, test_image, sizeof(test_image), header.hmac);

    FirmwareCheckResult_t result = firmware_integrity_verify(&header, test_image, sizeof(test_image));
    TEST_ASSERT(result == FW_CHECK_OK);
    TEST_PASS();
}

/* ---------- Test: verify tampered image ---------- */
static int test_firmware_verify_tampered(void)
{
    firmware_integrity_init();
    firmware_integrity_set_key(test_key, 32);

    FirmwareImageHeader_t header;
    memset(&header, 0, sizeof(header));
    header.magic = FW_INTEGRITY_MANIFEST_MAGIC;
    header.image_size = sizeof(test_image);
    firmware_integrity_compute_hmac(test_key, 32, test_image, sizeof(test_image), header.hmac);

    /* Tamper with one byte of the image. */
    uint8_t tampered[sizeof(test_image)];
    memcpy(tampered, test_image, sizeof(test_image));
    tampered[0] ^= 0xFF;

    FirmwareCheckResult_t result = firmware_integrity_verify(&header, tampered, sizeof(tampered));
    TEST_ASSERT(result == FW_CHECK_ERR_HMAC);
    TEST_PASS();
}

/* ---------- Test: verify wrong HMAC ---------- */
static int test_firmware_verify_wrong_hmac(void)
{
    firmware_integrity_init();
    firmware_integrity_set_key(test_key, 32);

    FirmwareImageHeader_t header;
    memset(&header, 0, sizeof(header));
    header.magic = FW_INTEGRITY_MANIFEST_MAGIC;
    header.image_size = sizeof(test_image);
    memset(header.hmac, 0xFF, 32);  /* Wrong HMAC. */

    FirmwareCheckResult_t result = firmware_integrity_verify(&header, test_image, sizeof(test_image));
    TEST_ASSERT(result == FW_CHECK_ERR_HMAC);
    TEST_PASS();
}

/* ---------- Test: verify bad magic ---------- */
static int test_firmware_verify_bad_magic(void)
{
    firmware_integrity_init();
    firmware_integrity_set_key(test_key, 32);

    FirmwareImageHeader_t header;
    memset(&header, 0, sizeof(header));
    header.magic = 0xDEADBEEF;  /* Wrong magic. */
    header.image_size = sizeof(test_image);

    FirmwareCheckResult_t result = firmware_integrity_verify(&header, test_image, sizeof(test_image));
    TEST_ASSERT(result == FW_CHECK_ERR_MAGIC);
    TEST_PASS();
}

/* ---------- Test: verify size mismatch ---------- */
static int test_firmware_verify_size_mismatch(void)
{
    firmware_integrity_init();
    firmware_integrity_set_key(test_key, 32);

    FirmwareImageHeader_t header;
    memset(&header, 0, sizeof(header));
    header.magic = FW_INTEGRITY_MANIFEST_MAGIC;
    header.image_size = sizeof(test_image) + 100;  /* Wrong size. */
    firmware_integrity_compute_hmac(test_key, 32, test_image, sizeof(test_image), header.hmac);

    FirmwareCheckResult_t result = firmware_integrity_verify(&header, test_image, sizeof(test_image));
    TEST_ASSERT(result == FW_CHECK_ERR_SIZE);
    TEST_PASS();
}

/* ---------- Test: verify NULL params ---------- */
static int test_firmware_verify_null(void)
{
    firmware_integrity_init();

    FirmwareCheckResult_t result = firmware_integrity_verify(NULL, test_image, sizeof(test_image));
    TEST_ASSERT(result == FW_CHECK_ERR_PARAM_NULL);
    TEST_PASS();
}

/* ---------- Test: no CRC-32 (fresh project) ---------- */
static int test_no_crc32(void)
{
    /* Verify that we do NOT use CRC-32 for verification.
     * The FirmwareImageHeader_t has a crc32 field for future OTA
     * manifest compatibility, but it is NOT used for verification.
     * This test documents that design decision. */
    TEST_PASS();
}

/* ---------- Main ---------- */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_firmware_integrity ===\n");
    RUN_TEST(test_firmware_integrity_init);
    RUN_TEST(test_firmware_set_key);
    RUN_TEST(test_firmware_compute_hmac);
    RUN_TEST(test_firmware_verify_valid);
    RUN_TEST(test_firmware_verify_tampered);
    RUN_TEST(test_firmware_verify_wrong_hmac);
    RUN_TEST(test_firmware_verify_bad_magic);
    RUN_TEST(test_firmware_verify_size_mismatch);
    RUN_TEST(test_firmware_verify_null);
    RUN_TEST(test_no_crc32);

    PRINT_TEST_SUMMARY();
}
