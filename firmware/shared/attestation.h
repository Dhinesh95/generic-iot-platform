/**
 * @file attestation.h
 * @brief Zero-trust attestation — HMAC-SHA256 challenge-response.
 *
 * Architecture ref: Section 4, Baseline (mandatory, all domains).
 * Threat addressed: T1 (rogue node spoofing), T4 (lateral movement).
 *
 * This is a clean-room implementation. No prior codebase assumptions.
 */

#ifndef ATTESTATION_H
#define ATTESTATION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------- Constants ---------- */

/** Size of an HMAC-SHA256 digest in bytes. */
#define ATTESTATION_HMAC_SIZE       32

/** Size of the challenge nonce in bytes. */
#define ATTESTATION_CHALLENGE_SIZE  16

/** Size of the node identity key in bytes. */
#define ATTESTATION_KEY_SIZE        32

/** Maximum number of pre-shared keys tracked (for fleet). */
#define ATTESTATION_MAX_KEYS        32

/* ---------- Types ---------- */

/**
 * Challenge sent by the Hub to a node during attestation.
 * The node must compute HMAC-SHA256(key, challenge) and return
 * the digest within the response window.
 */
typedef struct {
    uint8_t  challenge[ATTESTATION_CHALLENGE_SIZE];  /**< Random nonce. */
    uint64_t timestamp_ms;                           /**< Sender's monotonic timestamp. */
    uint8_t  sender_id;                              /**< Node/Hub ID that issued the challenge. */
} AttestationChallenge_t;

/**
 * Response returned by the challenged node.
 */
typedef struct {
    uint8_t  response[ATTESTATION_HMAC_SIZE];  /**< HMAC-SHA256 digest. */
    uint64_t timestamp_ms;                     /**< Responder's monotonic timestamp. */
    uint8_t  responder_id;                     /**< Node ID of the responder. */
} AttestationResponse_t;

/**
 * Result of an attestation verification.
 */
typedef enum {
    ATTEST_OK,                  /**< Challenge-response matched. */
    ATTEST_ERR_HMAC_MISMATCH,   /**< HMAC does not match — possible spoof. */
    ATTEST_ERR_STALE_TIMESTAMP, /**< Timestamp drift exceeds tolerance. */
    ATTEST_ERR_UNKNOWN_NODE,    /**< Node ID not in key table. */
    ATTEST_ERR_PARAM_NULL       /**< NULL pointer passed. */
} AttestationResult_t;

/**
 * Pre-shared key record for a known node.
 * In production these would be provisioned at factory time and
 * stored in secure NVM. This struct is the in-RAM representation.
 */
typedef struct {
    uint8_t  node_id;                          /**< Unique node identifier. */
    uint8_t  key[ATTESTATION_KEY_SIZE];        /**< Pre-shared HMAC key. */
    bool     active;                           /**< false = revoked. */
} AttestationKeyRecord_t;

/* ---------- API ---------- */

/**
 * Initialise the attestation subsystem.
 * Must be called once at boot before any attestation operations.
 *
 * @return true on success.
 */
bool attestation_init(void);

/**
 * Register a node's pre-shared key.
 *
 * @param record  Pointer to a key record (copied internally).
 * @return ATTEST_OK on success, ATTEST_ERR_PARAM_NULL if record is NULL,
 *         or an error if the key table is full.
 */
AttestationResult_t attestation_register_key(const AttestationKeyRecord_t *record);

/**
 * Compute an HMAC-SHA256 response to a challenge.
 *
 * @param key        The pre-shared key for the challenged node.
 * @param challenge  The incoming challenge.
 * @param out        Output: the computed response digest.
 */
void attestation_compute_response(
    const uint8_t key[ATTESTATION_KEY_SIZE],
    const AttestationChallenge_t *challenge,
    uint8_t out[ATTESTATION_HMAC_SIZE]
);

/**
 * Verify an attestation response against a challenge.
 *
 * @param challenge  The original challenge that was sent.
 * @param response   The response received from the node.
 * @param max_drift_ms  Maximum allowed timestamp difference (e.g. 5000 ms).
 * @return ATTEST_OK if the response is valid and timely.
 */
AttestationResult_t attestation_verify(
    const AttestationChallenge_t *challenge,
    const AttestationResponse_t *response,
    uint64_t max_drift_ms
);

/**
 * Compute HMAC-SHA256 over arbitrary data using a given key.
 * Exposed for firmware integrity verification and other consumers.
 *
 * @param key       Key bytes.
 * @param key_len   Length of key in bytes.
 * @param data      Data to authenticate.
 * @param data_len  Length of data in bytes.
 * @param out       Output: 32-byte HMAC-SHA256 digest.
 */
void iot_hmac_sha256(
    const uint8_t *key, size_t key_len,
    const uint8_t *data, size_t data_len,
    uint8_t out[ATTESTATION_HMAC_SIZE]
);

#endif /* ATTESTATION_H */
