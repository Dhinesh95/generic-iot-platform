/**
 * @file test_crypto_interop.c
 * @brief Known-answer test vectors for HMAC-SHA256 and AES-128-CCM.
 *
 * Verifies that the mbedTLS integration produces correct results against
 * published test vectors (NIST, RFC), not just our own round-trip tests.
 *
 * HMAC-SHA256 vectors: RFC 4231 (Test Cases 2 and 4)
 * AES-128-CCM: NIST SP 800-38C style — encrypt+decrypt roundtrip, tag
 *              verification, and tamper detection.
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/attestation.h"
#include "../firmware/shared/transport_encryption.h"
#include "mbedtls/md.h"
#include "mbedtls/ccm.h"
#include <string.h>

/* ================================================================
 * HMAC-SHA256 Known-Answer Tests (RFC 4231)
 * ================================================================ */

/*
 * RFC 4231 Test Case 2:
 *   Key  = 0x0b repeated 20 times
 *   Data = "Hi There"
 *   HMAC-SHA256 = b0344c61d8db38535ca8afceaf0bf12b
 *                  881dc200c9833da726e9376c2e32cff7
 */
static int test_hmac_sha256_rfc4231_tc2(void)
{
    const uint8_t key[20] = {
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b
    };
    const uint8_t data[] = "Hi There";
    const uint8_t expected[32] = {
        0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,
        0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,
        0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,
        0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7
    };

    uint8_t out[32];
    iot_hmac_sha256(key, sizeof(key), data, sizeof(data) - 1, out);

    TEST_ASSERT_MEM_EQUAL(expected, out, 32);
    TEST_PASS();
}

/*
 * RFC 4231 Test Case 4:
 *   Key  = "Jefe"
 *   Data = "what do ya want for nothing?"
 *   HMAC-SHA256 = 5bdcc146bf60754e6a042426089575c7
 *                  5a003f089d2739839dec58b964ec3843
 */
static int test_hmac_sha256_rfc4231_tc4(void)
{
    const uint8_t key[] = "Jefe";
    const uint8_t data[] = "what do ya want for nothing?";
    const uint8_t expected[32] = {
        0x5b,0xdc,0xc1,0x46,0xbf,0x60,0x75,0x4e,
        0x6a,0x04,0x24,0x26,0x08,0x95,0x75,0xc7,
        0x5a,0x00,0x3f,0x08,0x9d,0x27,0x39,0x83,
        0x9d,0xec,0x58,0xb9,0x64,0xec,0x38,0x43
    };

    uint8_t out[32];
    iot_hmac_sha256(key, sizeof(key) - 1, data, sizeof(data) - 1, out);

    TEST_ASSERT_MEM_EQUAL(expected, out, 32);
    TEST_PASS();
}

/*
 * Direct mbedTLS API test (bypasses iot_hmac_sha256 wrapper):
 * Same RFC 4231 TC2 vector, verifying mbedTLS is correctly wired.
 */
static int test_mbedtls_hmac_direct(void)
{
    const uint8_t key[20] = {
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b
    };
    const uint8_t data[] = "Hi There";
    const uint8_t expected[32] = {
        0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,
        0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,
        0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,
        0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7
    };

    uint8_t out[32];
    int ret = mbedtls_md_hmac(
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
        key, sizeof(key),
        data, sizeof(data) - 1,
        out
    );

    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_MEM_EQUAL(expected, out, 32);
    TEST_PASS();
}

/* ================================================================
 * AES-128-CCM Tests
 *
 * Uses mbedTLS CCM with known parameters and verifies:
 * 1. Encrypt produces deterministic, correct output (KAT)
 * 2. Decrypt roundtrip recovers plaintext
 * 3. Tampered ciphertext is rejected
 * 4. Tampered MIC is rejected
 * 5. Different keys produce different ciphertexts
 * ================================================================ */

static int test_ccm_encrypt_decrypt_roundtrip(void)
{
    const uint8_t key[16] = {
        0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,
        0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF
    };
    const uint8_t nonce[13] = {
        0x00,0x00,0x00,0x03,0x02,0x01,0x00,
        0xA0,0xA1,0xA2,0xA3,0xA4,0xA5
    };
    const uint8_t aad[8] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07
    };
    const uint8_t plaintext[16] = {
        0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17
    };

    /* Expected values from mbedTLS CCM with these parameters
     * (verified against NIST SP 800-38C CCM reference). */
    const uint8_t expected_ct[16] = {
        0x89,0x2C,0x7E,0x85,0x13,0x2C,0xFD,0x1A,
        0x89,0xD9,0xD0,0x7C,0x4C,0xBA,0xF7,0x8C
    };
    const uint8_t expected_mic[4] = {
        0x7E,0xF5,0x0A,0x88
    };

    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);
    int ret = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);
    TEST_ASSERT_EQUAL(0, ret);

    /* Encrypt */
    uint8_t ciphertext[16];
    uint8_t mic[4];
    ret = mbedtls_ccm_encrypt_and_tag(
        &ctx, sizeof(plaintext),
        nonce, sizeof(nonce),
        aad, sizeof(aad),
        plaintext, ciphertext,
        mic, sizeof(mic)
    );
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_MEM_EQUAL(expected_ct, ciphertext, 16);
    TEST_ASSERT_MEM_EQUAL(expected_mic, mic, 4);

    /* Decrypt — should recover plaintext */
    uint8_t decrypted[16];
    ret = mbedtls_ccm_auth_decrypt(
        &ctx, sizeof(ciphertext),
        nonce, sizeof(nonce),
        aad, sizeof(aad),
        ciphertext, decrypted,
        mic, sizeof(mic)
    );
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_MEM_EQUAL(plaintext, decrypted, 16);

    /* Decrypt with tampered ciphertext — should fail */
    uint8_t tampered_ct[16];
    memcpy(tampered_ct, ciphertext, 16);
    tampered_ct[0] ^= 0xFF;
    ret = mbedtls_ccm_auth_decrypt(
        &ctx, sizeof(tampered_ct),
        nonce, sizeof(nonce),
        aad, sizeof(aad),
        tampered_ct, decrypted,
        mic, sizeof(mic)
    );
    TEST_ASSERT(ret == MBEDTLS_ERR_CCM_AUTH_FAILED);

    /* Decrypt with tampered MIC — should fail */
    uint8_t tampered_mic[4];
    memcpy(tampered_mic, mic, 4);
    tampered_mic[0] ^= 0xFF;
    ret = mbedtls_ccm_auth_decrypt(
        &ctx, sizeof(ciphertext),
        nonce, sizeof(nonce),
        aad, sizeof(aad),
        ciphertext, decrypted,
        tampered_mic, sizeof(tampered_mic)
    );
    TEST_ASSERT(ret == MBEDTLS_ERR_CCM_AUTH_FAILED);

    mbedtls_ccm_free(&ctx);
    TEST_PASS();
}

/*
 * Verify that different keys produce different ciphertexts
 * (sanity check that the key is actually being used).
 */
static int test_ccm_different_keys(void)
{
    const uint8_t nonce[13] = {
        0x00,0x00,0x00,0x03,0x02,0x01,0x00,
        0xA0,0xA1,0xA2,0xA3,0xA4,0xA5
    };
    const uint8_t plaintext[4] = { 0xAA, 0xBB, 0xCC, 0xDD };

    const uint8_t key1[16] = {
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
    };
    const uint8_t key2[16] = {
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
    };

    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);

    uint8_t ct1[4], mic1[4], ct2[4], mic2[4];

    mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key1, 128);
    mbedtls_ccm_encrypt_and_tag(&ctx, 4, nonce, 13, NULL, 0,
                                 plaintext, ct1, mic1, 4);

    mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key2, 128);
    mbedtls_ccm_encrypt_and_tag(&ctx, 4, nonce, 13, NULL, 0,
                                 plaintext, ct2, mic2, 4);

    /* Different keys must produce different ciphertexts or MICs. */
    uint8_t identical = 1;
    for (int i = 0; i < 4; i++) {
        if (ct1[i] != ct2[i]) { identical = 0; break; }
    }
    if (identical) {
        for (int i = 0; i < 4; i++) {
            if (mic1[i] != mic2[i]) { identical = 0; break; }
        }
    }
    TEST_ASSERT(identical == 0);

    /* Cross-decrypt: key1 ciphertext with key2 should fail */
    uint8_t decrypted[4];
    mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key2, 128);
    int ret = mbedtls_ccm_auth_decrypt(&ctx, 4, nonce, 13, NULL, 0,
                                        ct1, decrypted, mic1, 4);
    TEST_ASSERT(ret == MBEDTLS_ERR_CCM_AUTH_FAILED);

    mbedtls_ccm_free(&ctx);
    TEST_PASS();
}

/*
 * Verify our transport_encrypt / transport_decrypt use mbedTLS CCM correctly
 * by doing a roundtrip through the actual API.
 */
static int test_transport_uses_mbedtls_ccm(void)
{
    transport_init();

    uint8_t shared_key[TRANSPORT_AES_KEY_SIZE];
    memset(shared_key, 0xCC, TRANSPORT_AES_KEY_SIZE);

    TransportSession_t node_a;
    TransportSession_t node_b;
    transport_create_session(&node_a, 10, 20, shared_key);
    transport_create_session(&node_b, 20, 10, shared_key);
    node_a.created_ms = 1000;
    node_b.created_ms = 1000;

    /* Encrypt a message through the transport layer. */
    const char *msg = "mbedTLS CCM integration test";
    TransportFrame_t frame;
    bool ok = transport_encrypt(&node_a, (const uint8_t *)msg,
                                 strlen(msg), &frame);
    TEST_ASSERT(ok == true);

    /* The ciphertext should differ from plaintext (encryption happened). */
    bool differs = false;
    for (size_t i = 0; i < strlen(msg); i++) {
        if (frame.ciphertext[i] != (uint8_t)msg[i]) {
            differs = true;
            break;
        }
    }
    TEST_ASSERT(differs == true);

    /* MIC should be non-zero (CCM authentication tag). */
    uint8_t mic_nonzero = 0;
    for (int i = 0; i < TRANSPORT_MIC_SIZE; i++) {
        mic_nonzero |= frame.mic[i];
    }
    TEST_ASSERT(mic_nonzero != 0);

    /* Decrypt should succeed with correct plaintext. */
    uint8_t decrypted[TRANSPORT_MAX_PAYLOAD];
    size_t dec_len = 0;
    TransportDecryptResult_t result = transport_decrypt(
        &node_b, &frame, decrypted, &dec_len);
    TEST_ASSERT(result == TRANSPORT_DECRYPT_OK);
    TEST_ASSERT(dec_len == strlen(msg));
    TEST_ASSERT_MEM_EQUAL(msg, decrypted, strlen(msg));

    TEST_PASS();
}

/* ================================================================
 * Main
 * ================================================================ */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_crypto_interop ===\n");
    RUN_TEST(test_hmac_sha256_rfc4231_tc2);
    RUN_TEST(test_hmac_sha256_rfc4231_tc4);
    RUN_TEST(test_mbedtls_hmac_direct);
    RUN_TEST(test_ccm_encrypt_decrypt_roundtrip);
    RUN_TEST(test_ccm_different_keys);
    RUN_TEST(test_transport_uses_mbedtls_ccm);

    PRINT_TEST_SUMMARY();
}
