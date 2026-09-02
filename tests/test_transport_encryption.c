/**
 * @file test_transport_encryption.c
 * @brief Tests for transport encryption (AES-128-CCM, per-session keys).
 *
 * Architecture ref: Section 4, Baseline — T2 (wireless MITM).
 *
 * FIX 1 (Phase 1.1): Key derivation is now symmetric — Node A's TX key
 * == Node B's RX key. The peer roundtrip test exercises a real two-party
 * exchange with independently created sessions.
 *
 * HOW THE OLD TEST MASKED THE BUG:
 * The original "encrypt roundtrip" test created ONE session (Node 1→2),
 * encrypted with its tx_key, then set that same session's rx_nonce_counter
 * to match and decrypted with the session's own rx_key. Since a single
 * session derives both tx_key and rx_key from the same salt+shared_key,
 * it could always decrypt its own messages — but this never proved that
 * a DIFFERENT session (Node 2→1) could decrypt them. The bug was that
 * Node 2→1's rx_key was derived from a different salt ([2,1] vs [1,2]),
 * so it would NEVER match Node 1→2's tx_key. The old test simply never
 * exercised that cross-session path.
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/transport_encryption.h"
#include <string.h>

/* ---------- Test: init ---------- */
static int test_transport_init(void)
{
    TEST_ASSERT(transport_init() == true);
    TEST_PASS();
}

/* ---------- Test: create session ---------- */
static int test_transport_create_session(void)
{
    transport_init();

    uint8_t shared_key[TRANSPORT_AES_KEY_SIZE];
    memset(shared_key, 0x55, TRANSPORT_AES_KEY_SIZE);

    TransportSession_t session;
    bool result = transport_create_session(&session, 1, 2, shared_key);
    TEST_ASSERT(result == true);
    TEST_ASSERT(session.active == true);
    TEST_ASSERT(session.local_node_id == 1);
    TEST_ASSERT(session.remote_node_id == 2);
    TEST_ASSERT(session.session_id > 0);
    TEST_PASS();
}

/* ---------- Test: session validity ---------- */
static int test_transport_session_valid(void)
{
    transport_init();

    uint8_t shared_key[TRANSPORT_AES_KEY_SIZE];
    memset(shared_key, 0x55, TRANSPORT_AES_KEY_SIZE);

    TransportSession_t session;
    transport_create_session(&session, 1, 2, shared_key);
    session.created_ms = 1000;

    TEST_ASSERT(transport_session_valid(&session, 1000) == true);
    TEST_ASSERT(transport_session_valid(&session, 2000000) == true);
    TEST_ASSERT(transport_session_valid(&session, 1000 + TRANSPORT_SESSION_KEY_TTL_MS + 1) == false);

    TransportSession_t invalid_session;
    memset(&invalid_session, 0, sizeof(invalid_session));
    TEST_ASSERT(transport_session_valid(&invalid_session, 1000) == false);

    TEST_PASS();
}

/* ---------- Test: encrypt produces valid frame ---------- */
static int test_transport_encrypt_produces_frame(void)
{
    transport_init();

    uint8_t shared_key[TRANSPORT_AES_KEY_SIZE];
    memset(shared_key, 0xAA, TRANSPORT_AES_KEY_SIZE);

    TransportSession_t session;
    transport_create_session(&session, 1, 2, shared_key);
    session.created_ms = 0;

    const char *plaintext = "Hello, IoT world!";
    TransportFrame_t frame;
    bool enc_result = transport_encrypt(&session, (const uint8_t *)plaintext,
                                        strlen(plaintext), &frame);
    TEST_ASSERT(enc_result == true);
    TEST_ASSERT(frame.ciphertext_len > 0);
    TEST_ASSERT(frame.session_id == session.session_id);

    /* Verify encryption changed the data. */
    bool data_differs = false;
    for (size_t i = 0; i < strlen(plaintext); i++) {
        if (frame.ciphertext[i] != (uint8_t)plaintext[i]) {
            data_differs = true;
            break;
        }
    }
    TEST_ASSERT(data_differs == true);

    /* Verify MIC is non-zero. */
    uint8_t mic_nonzero = 0;
    for (int i = 0; i < TRANSPORT_MIC_SIZE; i++) {
        mic_nonzero |= frame.mic[i];
    }
    TEST_ASSERT(mic_nonzero != 0);

    /* Verify nonce counter was incremented. */
    TEST_ASSERT_EQUAL(1, session.tx_nonce_counter);

    TEST_PASS();
}

/* ---------- Test: key symmetry — both sessions derive matching keys ---------- */
static int test_transport_key_symmetry(void)
{
    transport_init();

    uint8_t shared_key[TRANSPORT_AES_KEY_SIZE];
    memset(shared_key, 0xAA, TRANSPORT_AES_KEY_SIZE);

    /* Node 1→2 session. */
    TransportSession_t session_a;
    transport_create_session(&session_a, 1, 2, shared_key);

    /* Node 2→1 session (separate context, same shared key). */
    TransportSession_t session_b;
    transport_create_session(&session_b, 2, 1, shared_key);

    /*
     * Symmetry requirement:
     *   session_a.tx_key == session_b.rx_key  (A→B = B's receive key)
     *   session_b.tx_key == session_a.rx_key  (B→A = A's receive key)
     */
    TEST_ASSERT_MEM_EQUAL(session_a.tx_key, session_b.rx_key, TRANSPORT_AES_KEY_SIZE);
    TEST_ASSERT_MEM_EQUAL(session_b.tx_key, session_a.rx_key, TRANSPORT_AES_KEY_SIZE);

    /* TX and RX keys within a session should differ. */
    uint8_t same = 0;
    for (int i = 0; i < TRANSPORT_AES_KEY_SIZE; i++) {
        same |= session_a.tx_key[i] ^ session_a.rx_key[i];
    }
    TEST_ASSERT(same != 0);

    TEST_PASS();
}

/* ---------- Test: peer roundtrip — real two-party exchange ---------- */
static int test_transport_peer_roundtrip(void)
{
    transport_init();

    uint8_t shared_key[TRANSPORT_AES_KEY_SIZE];
    memset(shared_key, 0xBB, TRANSPORT_AES_KEY_SIZE);

    /*
     * Create two independent sessions — one for each node.
     * This is how a real deployment works: each node creates its own
     * session context with the shared attestation key.
     */
    TransportSession_t node_a;  /* Node A (ID=10) */
    TransportSession_t node_b;  /* Node B (ID=20) */

    bool ok_a = transport_create_session(&node_a, 10, 20, shared_key);
    bool ok_b = transport_create_session(&node_b, 20, 10, shared_key);
    TEST_ASSERT(ok_a == true);
    TEST_ASSERT(ok_b == true);

    node_a.created_ms = 1000;
    node_b.created_ms = 1000;

    /* ========== A→B direction ========== */
    const char *msg_ab = "A sends to B";
    TransportFrame_t frame_ab;
    bool enc_ok = transport_encrypt(&node_a, (const uint8_t *)msg_ab,
                                    strlen(msg_ab), &frame_ab);
    TEST_ASSERT(enc_ok == true);

    /* Node B decrypts the frame from Node A. */
    uint8_t decrypted_ab[TRANSPORT_MAX_PAYLOAD];
    size_t dec_len_ab = 0;
    TransportDecryptResult_t dec_result = transport_decrypt(
        &node_b, &frame_ab, decrypted_ab, &dec_len_ab);
    TEST_ASSERT(dec_result == TRANSPORT_DECRYPT_OK);
    TEST_ASSERT(dec_len_ab == strlen(msg_ab));
    TEST_ASSERT_MEM_EQUAL(msg_ab, decrypted_ab, strlen(msg_ab));

    /* ========== B→A direction ========== */
    const char *msg_ba = "B sends to A";
    TransportFrame_t frame_ba;
    enc_ok = transport_encrypt(&node_b, (const uint8_t *)msg_ba,
                               strlen(msg_ba), &frame_ba);
    TEST_ASSERT(enc_ok == true);

    /* Node A decrypts the frame from Node B. */
    uint8_t decrypted_ba[TRANSPORT_MAX_PAYLOAD];
    size_t dec_len_ba = 0;
    dec_result = transport_decrypt(
        &node_a, &frame_ba, decrypted_ba, &dec_len_ba);
    TEST_ASSERT(dec_result == TRANSPORT_DECRYPT_OK);
    TEST_ASSERT(dec_len_ba == strlen(msg_ba));
    TEST_ASSERT_MEM_EQUAL(msg_ba, decrypted_ba, strlen(msg_ba));

    TEST_PASS();
}

/* ---------- Test: cross-node decryption with wrong key fails ---------- */
static int test_transport_wrong_key_fails(void)
{
    transport_init();

    uint8_t key_a[TRANSPORT_AES_KEY_SIZE];
    memset(key_a, 0xAA, TRANSPORT_AES_KEY_SIZE);
    uint8_t key_b[TRANSPORT_AES_KEY_SIZE];
    memset(key_b, 0xBB, TRANSPORT_AES_KEY_SIZE);  /* Different shared key! */

    TransportSession_t node_a;
    TransportSession_t node_b;
    transport_create_session(&node_a, 1, 2, key_a);
    transport_create_session(&node_b, 2, 1, key_b);  /* Different key material. */
    node_b.created_ms = 0;

    const char *msg = "secret";
    TransportFrame_t frame;
    transport_encrypt(&node_a, (const uint8_t *)msg, strlen(msg), &frame);

    /* Node B cannot decrypt because it has a different shared key. */
    uint8_t decrypted[TRANSPORT_MAX_PAYLOAD];
    size_t dec_len = 0;
    TransportDecryptResult_t dec_result = transport_decrypt(
        &node_b, &frame, decrypted, &dec_len);
    TEST_ASSERT(dec_result == TRANSPORT_DECRYPT_ERR_MIC_FAIL);

    TEST_PASS();
}

/* ---------- Test: MIC verification failure ---------- */
static int test_transport_mic_failure(void)
{
    transport_init();

    uint8_t shared_key[TRANSPORT_AES_KEY_SIZE];
    memset(shared_key, 0xAA, TRANSPORT_AES_KEY_SIZE);

    TransportSession_t session;
    transport_create_session(&session, 1, 2, shared_key);
    session.created_ms = 0;

    TransportFrame_t frame;
    const char *data = "test";
    transport_encrypt(&session, (const uint8_t *)data, strlen(data), &frame);

    /* Tamper with the MIC. */
    frame.mic[0] ^= 0xFF;

    /* Set rx_nonce_counter to match so we reach MIC verification. */
    session.rx_nonce_counter = frame.nonce_counter;

    uint8_t decrypted[TRANSPORT_MAX_PAYLOAD];
    size_t dec_len = 0;
    TransportDecryptResult_t dec_result = transport_decrypt(&session, &frame,
                                                            decrypted, &dec_len);
    TEST_ASSERT(dec_result == TRANSPORT_DECRYPT_ERR_MIC_FAIL);
    TEST_PASS();
}

/* ---------- Test: session invalidation ---------- */
static int test_transport_session_invalidate(void)
{
    transport_init();

    uint8_t shared_key[TRANSPORT_AES_KEY_SIZE];
    memset(shared_key, 0xAA, TRANSPORT_AES_KEY_SIZE);

    TransportSession_t session;
    transport_create_session(&session, 1, 2, shared_key);
    session.created_ms = 0;

    TEST_ASSERT(session.active == true);

    transport_session_invalidate(&session);
    TEST_ASSERT(session.active == false);

    /* Keys should be zeroed. */
    uint8_t zero_key[TRANSPORT_AES_KEY_SIZE];
    memset(zero_key, 0, TRANSPORT_AES_KEY_SIZE);
    TEST_ASSERT_MEM_EQUAL(session.tx_key, zero_key, TRANSPORT_AES_KEY_SIZE);

    TEST_PASS();
}

/* ---------- Test: key derivation determinism ---------- */
static int test_transport_key_derivation(void)
{
    uint8_t shared_key[TRANSPORT_AES_KEY_SIZE];
    memset(shared_key, 0x55, TRANSPORT_AES_KEY_SIZE);

    uint8_t salt[] = {0x01, 0x02};
    uint8_t tx_key[TRANSPORT_AES_KEY_SIZE];
    uint8_t rx_key[TRANSPORT_AES_KEY_SIZE];

    /* Derive with "a_to_b" direction. */
    transport_derive_keys(shared_key, salt, 2, tx_key, rx_key, "a_to_b", "b_to_a");

    /* TX and RX keys should differ (different direction labels). */
    uint8_t same = 0;
    for (int i = 0; i < TRANSPORT_AES_KEY_SIZE; i++) {
        same |= tx_key[i] ^ rx_key[i];
    }
    TEST_ASSERT(same != 0);

    /* Derivation should be deterministic. */
    uint8_t tx_key2[TRANSPORT_AES_KEY_SIZE];
    uint8_t rx_key2[TRANSPORT_AES_KEY_SIZE];
    transport_derive_keys(shared_key, salt, 2, tx_key2, rx_key2, "a_to_b", "b_to_a");
    TEST_ASSERT_MEM_EQUAL(tx_key, tx_key2, TRANSPORT_AES_KEY_SIZE);
    TEST_ASSERT_MEM_EQUAL(rx_key, rx_key2, TRANSPORT_AES_KEY_SIZE);

    /* Reversed direction labels should produce swapped keys. */
    uint8_t tx_rev[TRANSPORT_AES_KEY_SIZE];
    uint8_t rx_rev[TRANSPORT_AES_KEY_SIZE];
    transport_derive_keys(shared_key, salt, 2, tx_rev, rx_rev, "b_to_a", "a_to_b");
    TEST_ASSERT_MEM_EQUAL(tx_key, rx_rev, TRANSPORT_AES_KEY_SIZE);
    TEST_ASSERT_MEM_EQUAL(rx_key, tx_rev, TRANSPORT_AES_KEY_SIZE);

    TEST_PASS();
}

/* ---------- Main ---------- */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_transport_encryption ===\n");
    RUN_TEST(test_transport_init);
    RUN_TEST(test_transport_create_session);
    RUN_TEST(test_transport_session_valid);
    RUN_TEST(test_transport_encrypt_produces_frame);
    RUN_TEST(test_transport_key_symmetry);
    RUN_TEST(test_transport_peer_roundtrip);
    RUN_TEST(test_transport_wrong_key_fails);
    RUN_TEST(test_transport_mic_failure);
    RUN_TEST(test_transport_session_invalidate);
    RUN_TEST(test_transport_key_derivation);

    PRINT_TEST_SUMMARY();
}
