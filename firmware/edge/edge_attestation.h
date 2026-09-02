/**
 * @file edge_attestation.h
 * @brief Edge Node attestation — wraps shared attestation for Tier 1.
 *
 * Architecture ref: Section 4 (Baseline: HMAC-SHA256 challenge-response),
 *                   Section 8 (Edge Node).
 *
 * Mirrors gateway_auth.h/c's structure but for Edge Nodes (Tier 1).
 * The Edge Node receives an attestation challenge (via RS-485 from the
 * Hub, relayed transparently by the Gateway per Phase 13's finding),
 * computes the HMAC-SHA256 response using its own pre-shared key,
 * and sends the response back.
 *
 * Trust topology (unchanged from Phase 13.1):
 *   - Edge Node ↔ Hub: attested via this module + shared attestation.c.
 *   - Gateway ↔ Hub: attested independently via gateway_auth.h/c.
 *   - Gateway is a TRANSPARENT relay for Edge↔Hub attestation frames.
 *   - These are two independent, parallel trust relationships.
 *
 * Data flow gating (same pattern as Phase 6.6 LoRa join):
 *   edge_node_read_and_send() checks EdgeAuthState_t before sending
 *   any sensor data. No data frames are sent before successful
 *   attestation.
 */

#ifndef EDGE_ATTESTATION_H
#define EDGE_ATTESTATION_H

#include <stdint.h>
#include <stdbool.h>
#include "../shared/attestation.h"  /* for AttestationChallenge_t, AttestationResponse_t */

/* ---------- Constants ---------- */

/** Timeout for attestation response (ms). Must respond within this window. */
#define EDGE_ATTEST_RESPONSE_TIMEOUT_MS  5000

/**
 * Explicit sentinel to disable attestation gating in test harnesses only.
 *
 * edge_node_init() rejects NULL auth_ctx — a NULL pointer is always a
 * parameter error. Test code that genuinely needs ungated data flow
 * (e.g. pre-attestation bring-up testing, driver-only tests) must
 * pass this sentinel instead of NULL.
 *
 * This naming makes the security bypass explicit at every call site,
 * mirroring the batch_decode_frame_untrusted pattern from Phase 13.3.
 * Production code MUST NEVER pass this sentinel.
 */
#define EDGE_AUTH_UNGATED_TESTING_ONLY ((EdgeAuthContext_t *)0xDEAD)

/* ---------- Types ---------- */

/**
 * Edge Node attestation state.
 */
typedef enum {
    EDGE_AUTH_UNINIT = 0,        /**< Not initialised. */
    EDGE_AUTH_IDLE,              /**< Initialised, challenge pending or not yet received. */
    EDGE_AUTH_AUTHENTICATED,     /**< Attestation complete — data flow permitted. */
    EDGE_AUTH_FAILED             /**< Attestation failed (wrong key, timeout, etc.). */
} EdgeAuthState_t;

/**
 * Edge Node authentication context.
 * Holds the Edge Node's identity and session state for Hub communication.
 */
typedef struct {
    uint8_t  node_id;                            /**< This Edge Node's RS-485 address. */
    uint8_t  pre_shared_key[ATTESTATION_KEY_SIZE]; /**< Factory-provisioned HMAC key. */
    bool     key_set;                            /**< false until key is provisioned. */
    EdgeAuthState_t state;                       /**< Current attestation state. */
} EdgeAuthContext_t;

/**
 * Result of Edge Node attestation response generation.
 */
typedef enum {
    EDGE_ATTEST_OK,                 /**< Response computed successfully. */
    EDGE_ATTEST_ERR_NOT_READY,      /**< No key provisioned or context not initialised. */
    EDGE_ATTEST_ERR_PARAM_NULL      /**< NULL pointer argument. */
} EdgeAttestResult_t;

/* ---------- API ---------- */

/**
 * Initialise the Edge Node authentication context.
 *
 * @param ctx       Output context (caller-allocated).
 * @param node_id   This Edge Node's RS-485 address.
 */
void edge_auth_init(EdgeAuthContext_t *ctx, uint8_t node_id);

/**
 * Provision the Edge Node's pre-shared key (factory time).
 * After provisioning, the node can respond to attestation challenges.
 *
 * @param ctx    Auth context.
 * @param key    32-byte pre-shared HMAC key.
 * @return true on success.
 */
bool edge_auth_set_key(EdgeAuthContext_t *ctx, const uint8_t key[ATTESTATION_KEY_SIZE]);

/**
 * Process an incoming attestation challenge and compute the response.
 *
 * This is the Edge Node side of the challenge-response flow:
 *   1. Hub sends AttestationChallenge_t (via RS-485, relayed by Gateway)
 *   2. Edge Node calls this function with the received challenge
 *   3. This function computes HMAC-SHA256(key, challenge) and returns it
 *   4. Edge Node sends AttestationResponse_t back to Hub
 *
 * On success, transitions state to EDGE_AUTH_AUTHENTICATED.
 * On failure, transitions state to EDGE_AUTH_FAILED.
 *
 * @param ctx         Auth context (must have key provisioned).
 * @param challenge   The incoming challenge from the Hub.
 * @param out_response  Output: the computed response.
 * @return EDGE_ATTEST_OK on success.
 */
EdgeAttestResult_t edge_auth_respond(
    EdgeAuthContext_t *ctx,
    const AttestationChallenge_t *challenge,
    AttestationResponse_t *out_response);

/**
 * Get the current Edge Node attestation state.
 *
 * @param ctx  Auth context.
 * @return Current state.
 */
EdgeAuthState_t edge_auth_get_state(const EdgeAuthContext_t *ctx);

/**
 * Check whether the Edge Node is authenticated (data flow permitted).
 *
 * @param ctx  Auth context.
 * @return true if state == EDGE_AUTH_AUTHENTICATED.
 */
bool edge_auth_is_authenticated(const EdgeAuthContext_t *ctx);

#endif /* EDGE_ATTESTATION_H */
