/**
 * @file test_verified_boot.c
 * @brief Tests for verified boot (Phase 21).
 */

#include "test_helpers/test_utils.h"
#include "verified_boot.h"
#include "attestation.h"
#include <string.h>

int _total = 0, _passed = 0, _failed = 0;

/* Test identity key. */
static const uint8_t s_test_key[32] = {
    0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
    0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
    0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
    0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42
};

/* Pre-computed expected hash for the test image. */
static uint8_t s_expected_hash[VERIFIED_BOOT_HASH_SIZE];

static void compute_expected_hash(uint32_t version, uint32_t size, uint32_t slot)
{
    uint8_t derived_key[VERIFIED_BOOT_KEY_SIZE];
    verified_boot_derive_key(s_test_key, 32, derived_key);

    uint8_t data[64];
    size_t len = 0;
    data[len++] = (version >> 24) & 0xFF;
    data[len++] = (version >> 16) & 0xFF;
    data[len++] = (version >> 8) & 0xFF;
    data[len++] = version & 0xFF;
    data[len++] = (size >> 24) & 0xFF;
    data[len++] = (size >> 16) & 0xFF;
    data[len++] = (size >> 8) & 0xFF;
    data[len++] = size & 0xFF;
    data[len++] = (uint8_t)slot;

    iot_hmac_sha256(derived_key, VERIFIED_BOOT_KEY_SIZE, data, len, s_expected_hash);
    memset(derived_key, 0, sizeof(derived_key));
}

static int test_verified_boot_valid_image(void)
{
    compute_expected_hash(200, 65536, 0);

    FlashImageDescriptor_t desc = {
        .slot = 0,
        .size = 65536,
        .version = 200,
        .present = true
    };
    memcpy(desc.hash, s_expected_hash, VERIFIED_BOOT_HASH_SIZE);

    BootVerification_t result;
    VerifiedBootResult_t vr = verified_boot_check(&desc, s_test_key, 32, &result);

    TEST_ASSERT_EQUAL(BOOT_OK, vr);
    TEST_ASSERT(result.image_valid);
    TEST_ASSERT_EQUAL(200, result.fw_version);

    TEST_PASS();
}

static int test_verified_boot_corrupted_image(void)
{
    FlashImageDescriptor_t desc = {
        .slot = 0,
        .size = 65536,
        .version = 200,
        .present = true
    };
    /* Wrong hash — corrupted image. */
    memset(desc.hash, 0xFF, VERIFIED_BOOT_HASH_SIZE);

    BootVerification_t result;
    VerifiedBootResult_t vr = verified_boot_check(&desc, s_test_key, 32, &result);

    TEST_ASSERT_EQUAL(BOOT_OK, vr);
    TEST_ASSERT(!result.image_valid);

    TEST_PASS();
}

static int test_verified_boot_partial_ota(void)
{
    /* Simulate a partially-written image (size = 0). */
    FlashImageDescriptor_t desc = {
        .slot = 1,
        .size = 0,  /* Empty — incomplete OTA. */
        .version = 300,
        .present = true
    };
    memset(desc.hash, 0xAA, VERIFIED_BOOT_HASH_SIZE);

    BootVerification_t result;
    VerifiedBootResult_t vr = verified_boot_check(&desc, s_test_key, 32, &result);

    TEST_ASSERT_EQUAL(BOOT_OK, vr);
    /* Hash won't match because we computed for a different size. */
    TEST_ASSERT(!result.image_valid);

    TEST_PASS();
}

static int test_verified_boot_key_derivation(void)
{
    uint8_t key1[VERIFIED_BOOT_KEY_SIZE];
    uint8_t key2[VERIFIED_BOOT_KEY_SIZE];

    /* Same input → same output. */
    VerifiedBootResult_t r1 = verified_boot_derive_key(s_test_key, 32, key1);
    VerifiedBootResult_t r2 = verified_boot_derive_key(s_test_key, 32, key2);

    TEST_ASSERT_EQUAL(BOOT_OK, r1);
    TEST_ASSERT_EQUAL(BOOT_OK, r2);
    TEST_ASSERT_MEM_EQUAL(key1, key2, VERIFIED_BOOT_KEY_SIZE);

    /* Different key → different output. */
    uint8_t other_key[32];
    memset(other_key, 0x55, 32);
    uint8_t key3[VERIFIED_BOOT_KEY_SIZE];
    VerifiedBootResult_t r3 = verified_boot_derive_key(other_key, 32, key3);

    TEST_ASSERT_EQUAL(BOOT_OK, r3);
    TEST_ASSERT(memcmp(key1, key3, VERIFIED_BOOT_KEY_SIZE) != 0);

    /* Clean up. */
    memset(key1, 0, sizeof(key1));
    memset(key2, 0, sizeof(key2));
    memset(key3, 0, sizeof(key3));

    TEST_PASS();
}

int main(void)
{
    printf("\n=== Verified Boot Tests ===\n\n");

    RUN_TEST(test_verified_boot_valid_image);
    RUN_TEST(test_verified_boot_corrupted_image);
    RUN_TEST(test_verified_boot_partial_ota);
    RUN_TEST(test_verified_boot_key_derivation);

    PRINT_TEST_SUMMARY();
}
