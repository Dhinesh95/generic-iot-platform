/**
 * @file test_attestation.c
 * @brief Tests for zero-trust attestation (HMAC-SHA256 challenge-response).
 *
 * Architecture ref: Section 4, Baseline — T1 (rogue node spoofing).
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/attestation.h"
#include <string.h>

/* ---------- Test: init ---------- */
static int test_attestation_init(void)
{
    TEST_ASSERT(attestation_init() == true);
    TEST_PASS();
}

/* ---------- Test: register key ---------- */
static int test_attestation_register_key(void)
{
    attestation_init();

    AttestationKeyRecord_t record = {
        .node_id = 42,
        .active = true
    };
    /* Set a known key. */
    memset(record.key, 0xAB, ATTESTATION_KEY_SIZE);

    AttestationResult_t result = attestation_register_key(&record);
    TEST_ASSERT(result == ATTEST_OK);
    TEST_PASS();
}

/* ---------- Test: compute response ---------- */
static int test_attestation_compute_response(void)
{
    attestation_init();

    uint8_t key[ATTESTATION_KEY_SIZE];
    memset(key, 0x55, ATTESTATION_KEY_SIZE);

    AttestationChallenge_t challenge;
    memset(challenge.challenge, 0x01, ATTESTATION_CHALLENGE_SIZE);
    challenge.timestamp_ms = 1000;
    challenge.sender_id = 1;

    uint8_t response[ATTESTATION_HMAC_SIZE];
    attestation_compute_response(key, &challenge, response);

    /* Response should be non-zero (HMAC produced output). */
    uint8_t nonzero = 0;
    for (int i = 0; i < ATTESTATION_HMAC_SIZE; i++) {
        nonzero |= response[i];
    }
    TEST_ASSERT(nonzero != 0);
    TEST_PASS();
}

/* ---------- Test: verify valid attestation ---------- */
static int test_attestation_verify_valid(void)
{
    attestation_init();

    uint8_t key[ATTESTATION_KEY_SIZE];
    memset(key, 0xAB, ATTESTATION_KEY_SIZE);

    /* Register node. */
    AttestationKeyRecord_t record = {
        .node_id = 5,
        .active = true
    };
    memcpy(record.key, key, ATTESTATION_KEY_SIZE);
    attestation_register_key(&record);

    /* Create challenge. */
    AttestationChallenge_t challenge;
    memset(challenge.challenge, 0x42, ATTESTATION_CHALLENGE_SIZE);
    challenge.timestamp_ms = 5000;
    challenge.sender_id = 1;

    /* Compute valid response. */
    AttestationResponse_t response;
    response.responder_id = 5;
    response.timestamp_ms = 5000;
    attestation_compute_response(key, &challenge, response.response);

    /* Verify should pass. */
    AttestationResult_t result = attestation_verify(&challenge, &response, 5000);
    TEST_ASSERT(result == ATTEST_OK);
    TEST_PASS();
}

/* ---------- Test: verify wrong HMAC ---------- */
static int test_attestation_verify_wrong_hmac(void)
{
    attestation_init();

    uint8_t key[ATTESTATION_KEY_SIZE];
    memset(key, 0xAB, ATTESTATION_KEY_SIZE);

    AttestationKeyRecord_t record = {
        .node_id = 5,
        .active = true
    };
    memcpy(record.key, key, ATTESTATION_KEY_SIZE);
    attestation_register_key(&record);

    AttestationChallenge_t challenge;
    memset(challenge.challenge, 0x42, ATTESTATION_CHALLENGE_SIZE);
    challenge.timestamp_ms = 5000;
    challenge.sender_id = 1;

    /* Fabricate a response with wrong HMAC. */
    AttestationResponse_t response;
    response.responder_id = 5;
    response.timestamp_ms = 5000;
    memset(response.response, 0xFF, ATTESTATION_HMAC_SIZE);  /* Wrong. */

    AttestationResult_t result = attestation_verify(&challenge, &response, 5000);
    TEST_ASSERT(result == ATTEST_ERR_HMAC_MISMATCH);
    TEST_PASS();
}

/* ---------- Test: verify stale timestamp ---------- */
static int test_attestation_verify_stale_timestamp(void)
{
    attestation_init();

    uint8_t key[ATTESTATION_KEY_SIZE];
    memset(key, 0xAB, ATTESTATION_KEY_SIZE);

    AttestationKeyRecord_t record = {
        .node_id = 5,
        .active = true
    };
    memcpy(record.key, key, ATTESTATION_KEY_SIZE);
    attestation_register_key(&record);

    AttestationChallenge_t challenge;
    memset(challenge.challenge, 0x42, ATTESTATION_CHALLENGE_SIZE);
    challenge.timestamp_ms = 5000;
    challenge.sender_id = 1;

    /* Compute valid HMAC but with a stale timestamp. */
    AttestationResponse_t response;
    response.responder_id = 5;
    response.timestamp_ms = 20000;  /* Way beyond max_drift. */
    attestation_compute_response(key, &challenge, response.response);

    AttestationResult_t result = attestation_verify(&challenge, &response, 5000);
    TEST_ASSERT(result == ATTEST_ERR_STALE_TIMESTAMP);
    TEST_PASS();
}

/* ---------- Test: verify unknown node ---------- */
static int test_attestation_verify_unknown_node(void)
{
    attestation_init();

    AttestationChallenge_t challenge;
    memset(challenge.challenge, 0x42, ATTESTATION_CHALLENGE_SIZE);
    challenge.timestamp_ms = 5000;
    challenge.sender_id = 1;

    AttestationResponse_t response;
    response.responder_id = 99;  /* Not registered. */
    response.timestamp_ms = 5000;
    memset(response.response, 0x00, ATTESTATION_HMAC_SIZE);

    AttestationResult_t result = attestation_verify(&challenge, &response, 5000);
    TEST_ASSERT(result == ATTEST_ERR_UNKNOWN_NODE);
    TEST_PASS();
}

/* ---------- Test: NULL parameter ---------- */
static int test_attestation_null_param(void)
{
    attestation_init();

    AttestationResult_t result = attestation_verify(NULL, NULL, 5000);
    TEST_ASSERT(result == ATTEST_ERR_PARAM_NULL);
    TEST_PASS();
}

/* ---------- Test: HMAC determinism ---------- */
static int test_hmac_determinism(void)
{
    uint8_t key[32];
    memset(key, 0xAA, 32);

    uint8_t data[] = "test data for HMAC";
    uint8_t out1[32], out2[32];

    iot_hmac_sha256(key, 32, data, sizeof(data) - 1, out1);
    iot_hmac_sha256(key, 32, data, sizeof(data) - 1, out2);

    TEST_ASSERT_MEM_EQUAL(out1, out2, 32);
    TEST_PASS();
}

/* ---------- Main ---------- */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_attestation ===\n");
    RUN_TEST(test_attestation_init);
    RUN_TEST(test_attestation_register_key);
    RUN_TEST(test_attestation_compute_response);
    RUN_TEST(test_attestation_verify_valid);
    RUN_TEST(test_attestation_verify_wrong_hmac);
    RUN_TEST(test_attestation_verify_stale_timestamp);
    RUN_TEST(test_attestation_verify_unknown_node);
    RUN_TEST(test_attestation_null_param);
    RUN_TEST(test_hmac_determinism);

    PRINT_TEST_SUMMARY();
}
