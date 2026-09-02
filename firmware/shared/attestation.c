/**
 * @file attestation.c
 * @brief Zero-trust attestation implementation — HMAC-SHA256 challenge-response.
 *
 * Architecture ref: Section 4, Baseline (mandatory, all domains).
 *
 * Implementation uses mbedTLS for cryptographic primitives.
 * Function signature (iot_hmac_sha256) is preserved for API compatibility.
 */

#include "attestation.h"
#include <string.h>

#include "mbedtls/md.h"

/* ---------- Internal state ---------- */

static AttestationKeyRecord_t g_key_table[ATTESTATION_MAX_KEYS];
static uint8_t g_key_count = 0;
static bool g_initialised = false;

/* ---------- HMAC-SHA256 via mbedTLS ---------- */

void iot_hmac_sha256(
    const uint8_t *key, size_t key_len,
    const uint8_t *data, size_t data_len,
    uint8_t out[ATTESTATION_HMAC_SIZE])
{
    /* Use mbedTLS one-shot HMAC-SHA256.
     * Function signature preserved — call sites unchanged. */
    int ret = mbedtls_md_hmac(
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
        key, key_len,
        data, data_len,
        out
    );

    /* On failure, zero the output to avoid leaking partial state. */
    if (ret != 0) {
        memset(out, 0, ATTESTATION_HMAC_SIZE);
    }
}

/* ---------- Public API ---------- */

bool attestation_init(void)
{
    memset(g_key_table, 0, sizeof(g_key_table));
    g_key_count = 0;
    g_initialised = true;
    return true;
}

AttestationResult_t attestation_register_key(const AttestationKeyRecord_t *record)
{
    if (!record) return ATTEST_ERR_PARAM_NULL;
    if (!g_initialised) return ATTEST_ERR_PARAM_NULL;
    if (g_key_count >= ATTESTATION_MAX_KEYS) return ATTEST_ERR_PARAM_NULL;

    /* Check for duplicate node_id — update if exists. */
    for (uint8_t i = 0; i < g_key_count; i++) {
        if (g_key_table[i].node_id == record->node_id) {
            memcpy(&g_key_table[i], record, sizeof(AttestationKeyRecord_t));
            return ATTEST_OK;
        }
    }

    memcpy(&g_key_table[g_key_count], record, sizeof(AttestationKeyRecord_t));
    g_key_count++;
    return ATTEST_OK;
}

void attestation_compute_response(
    const uint8_t key[ATTESTATION_KEY_SIZE],
    const AttestationChallenge_t *challenge,
    uint8_t out[ATTESTATION_HMAC_SIZE])
{
    /* HMAC-SHA256(key, challenge_bytes || timestamp || sender_id) */
    uint8_t payload[ATTESTATION_CHALLENGE_SIZE + 8 + 1];
    size_t pos = 0;

    memcpy(payload + pos, challenge->challenge, ATTESTATION_CHALLENGE_SIZE);
    pos += ATTESTATION_CHALLENGE_SIZE;

    /* Encode timestamp as 8 big-endian bytes. */
    for (int i = 7; i >= 0; i--) {
        payload[pos++] = (uint8_t)(challenge->timestamp_ms >> (i * 8));
    }

    payload[pos++] = challenge->sender_id;

    iot_hmac_sha256(key, ATTESTATION_KEY_SIZE, payload, pos, out);
}

AttestationResult_t attestation_verify(
    const AttestationChallenge_t *challenge,
    const AttestationResponse_t *response,
    uint64_t max_drift_ms)
{
    if (!challenge || !response) return ATTEST_ERR_PARAM_NULL;
    if (!g_initialised) return ATTEST_ERR_PARAM_NULL;

    /* Check timestamp drift. */
    uint64_t diff = (challenge->timestamp_ms > response->timestamp_ms)
                  ? (challenge->timestamp_ms - response->timestamp_ms)
                  : (response->timestamp_ms - challenge->timestamp_ms);
    if (diff > max_drift_ms) {
        return ATTEST_ERR_STALE_TIMESTAMP;
    }

    /* Look up key for the responding node. */
    const AttestationKeyRecord_t *key_rec = NULL;
    for (uint8_t i = 0; i < g_key_count; i++) {
        if (g_key_table[i].node_id == response->responder_id && g_key_table[i].active) {
            key_rec = &g_key_table[i];
            break;
        }
    }
    if (!key_rec) return ATTEST_ERR_UNKNOWN_NODE;

    /* Compute expected response. */
    uint8_t expected[ATTESTATION_HMAC_SIZE];
    attestation_compute_response(key_rec->key, challenge, expected);

    /* Constant-time comparison to avoid timing side-channel. */
    uint8_t diff_byte = 0;
    for (int i = 0; i < ATTESTATION_HMAC_SIZE; i++) {
        diff_byte |= response->response[i] ^ expected[i];
    }

    return (diff_byte == 0) ? ATTEST_OK : ATTEST_ERR_HMAC_MISMATCH;
}
