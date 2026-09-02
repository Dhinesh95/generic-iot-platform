/**
 * @file gateway_auth.h
 * @brief Gateway authentication — attestation, frame HMAC, sequence tracking.
 *
 * Architecture ref: Section 4 (T4 lateral movement), Section 2 (Tier 2).
 *
 * Extends the existing attestation system (firmware/shared/attestation.h)
 * to authenticate the Gateway to the Hub. Uses the same HMAC-SHA256
 * challenge-response pattern — no new crypto mechanism.
 *
 * Trust topology (Phase 13.1):
 *   - Edge Node ↔ Hub: attested via the existing attestation system.
 *     The Gateway is a TRANSPARENT relay for Edge↔Hub attestation —
 *     it does NOT participate in or modify Edge Node attestation.
 *   - Gateway ↔ Hub: attested via this module. The Gateway has its own
 *     node_id and pre-shared key. The Hub challenges the Gateway
 *     directly, independently of Edge Node attestation.
 *   - These are TWO INDEPENDENT, PARALLEL attestation relationships.
 *     A compromised Gateway can relay Edge Node data (read/modify) but
 *     cannot forge Edge Node attestation responses (doesn't have their keys).
 *     Conversely, a compromised Edge Node cannot impersonate the Gateway.
 *
 * Batch frame integrity:
 *   - Each batch frame includes an HMAC-SHA256 tag computed over the
 *     entire encoded frame (header with hmac_tag field zeroed + entries),
 *     using the Gateway's session key.
 *   - The Hub verifies this tag before trusting any entry in the batch.
 *   - A tampered frame (any single entry modified) is rejected entirely.
 *
 * Suppression detection:
 *   - Each batch frame includes a monotonic sequence number.
 *   - The Hub tracks the last-seen sequence number per Gateway.
 *   - A gap (skipped sequence number) is audit-logged as suspicious.
 *   - This is DETECTION, not prevention — a fully compromised Gateway
 *     could simply stop sending. Full prevention of suppression by a
 *     trusted-but-compromised relay is not solvable by cryptography alone.
 *
 * Phase 15.1 — Per-Edge-Node Trust Tracking:
 *   - The Hub now tracks which Edge Nodes have been verified via
 *     attestation_verify(). Only entries from attested nodes are
 *     passed through to sensor validation and the rule engine.
 *   - This closes the T1 threat gap identified in Phase 15: a Gateway
 *     relaying data from an unauthenticated Edge Node is now rejected
 *     at the Hub ingestion gate, regardless of the Gateway's own auth.
 *   - Re-attestation is required after EDGE_NODE_ATTEST_EXPIRY_MS
 *     (24 hours) to prevent stale trust.
 */

#ifndef GATEWAY_AUTH_H
#define GATEWAY_AUTH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../shared/attestation.h"  /* for AttestationChallenge_t, AttestationResponse_t */

/* Forward declarations for batch frame types (defined in batch_forwarder.h).
 * These are used as pointer parameters in gateway_ingest_frame().
 * The actual struct definitions must match batch_forwarder.h's definitions. */
typedef struct BatchFrameHeader_s BatchFrameHeader_t;
typedef struct BatchFrameEntry_s  BatchFrameEntry_t;



/* ---------- Constants ---------- */

#define GATEWAY_HMAC_SIZE        32   /**< HMAC-SHA256 digest size. */
#define GATEWAY_NODE_ID_DEFAULT  0xF0 /**< Default Gateway node ID. */

/**
 * Maximum Edge Nodes whose attestation can be tracked simultaneously.
 * Aligned with ATTESTATION_MAX_KEYS in attestation.h.
 */
#define EDGE_NODE_TRACKER_MAX    32

/**
 * Explicit sentinel to disable edge-node trust checking in test harnesses only.
 *
 * gateway_ingest_frame() rejects NULL edge_tracker — a NULL pointer is always a
 * parameter error. Test code that genuinely needs to bypass the trust check
 * (e.g. threshold-boundary unit tests that construct synthetic batch frames)
 * must pass this sentinel instead of NULL.
 *
 * This naming makes the security bypass explicit at every call site,
 * mirroring the EDGE_AUTH_UNGATED_TESTING_ONLY pattern from Phase 14.2.
 * Production code MUST NEVER pass this sentinel.
 */
#define EDGE_TRACKER_CHECK_DISABLED_TESTING_ONLY ((EdgeNodeTracker_t *)0xDEAD)

/**
 * Default re-attestation expiry: 24 hours in milliseconds.
 * After this period, a node's trust entry is considered stale
 * and its data will be rejected until re-attestation succeeds.
 */
#define EDGE_NODE_ATTEST_EXPIRY_MS  (24ULL * 60 * 60 * 1000)

/* ---------- Types ---------- */

/**
 * Gateway attestation state.
 */
typedef enum {
    GW_AUTH_UNINIT = 0,        /**< Not initialised. */
    GW_AUTH_IDLE,              /**< Initialised, not yet attested. */
    GW_AUTH_AUTHENTICATED,     /**< Attestation complete, frame signing enabled. */
    GW_AUTH_FAILED             /**< Attestation failed. */
} GatewayAuthState_t;

/**
 * Gateway authentication context.
 * Holds the Gateway's identity and session state for Hub communication.
 */
typedef struct GatewayAuthContext_s {
    uint8_t  gateway_node_id;                   /**< This Gateway's node ID. */
    uint8_t  hub_node_id;                       /**< Hub's node ID. */
    uint8_t  session_key[GATEWAY_HMAC_SIZE];    /**< Session key for frame HMAC. */
    uint32_t tx_sequence;                       /**< Outgoing sequence number (monotonic). */
    GatewayAuthState_t state;                   /**< Current attestation state. */
} GatewayAuthContext_t;

/**
 * Hub-side per-Gateway tracking state.
 * One instance per known Gateway, used by the Hub to verify frames.
 */
typedef struct {
    uint8_t  gateway_node_id;                   /**< Gateway's node ID. */
    uint32_t last_sequence;                     /**< Last accepted sequence number. */
    bool     sequence_initialized;              /**< Whether last_sequence has been set. */
    uint8_t  session_key[GATEWAY_HMAC_SIZE];    /**< Session key for HMAC verification. */
    bool     authenticated;                     /**< Whether this Gateway has been attested. */
} GatewayGatewayTracker_t;

/**
 * Result of batch frame HMAC verification.
 */
typedef enum {
    GW_FRAME_OK,                /**< Frame HMAC valid. */
    GW_FRAME_ERR_HMAC_MISMATCH, /**< HMAC does not match — tampered frame. */
    GW_FRAME_ERR_GAP_DETECTED,  /**< Sequence gap detected — possible suppression. */
    GW_FRAME_ERR_NOT_AUTH,      /**< Gateway not yet authenticated. */
    GW_FRAME_ERR_PARAM_NULL     /**< NULL pointer argument. */
} GatewayFrameResult_t;

/**
 * Per-Edge-Node trust entry (Hub-side).
 * Records whether a given Edge Node's attestation has been verified
 * by the Hub via attestation_verify(). Used to gate sensor data
 * ingestion: only entries from attested nodes pass to the rule engine.
 *
 * Mirrors GatewayGatewayTracker_t's pattern but for Edge Nodes.
 * Phase 15.1: closes the T1 threat gap (rogue node spoofing).
 */
typedef struct {
    uint8_t  node_id;           /**< Edge Node's RS-485 address. */
    bool     attested;          /**< true if attestation_verify() succeeded. */
    uint64_t attested_at_ms;    /**< Timestamp when attestation succeeded. */
    bool     active;            /**< false = slot unused. */
} EdgeNodeTrackerEntry_t;

/**
 * Hub-side Edge Node attestation tracker.
 * One instance per Hub, holds trust state for all known Edge Nodes.
 */
typedef struct {
    EdgeNodeTrackerEntry_t entries[EDGE_NODE_TRACKER_MAX];
    uint8_t  count;             /**< Number of active entries. */
    uint64_t expiry_ms;         /**< Re-attestation timeout (ms). Default: EDGE_NODE_ATTEST_EXPIRY_MS. */
} EdgeNodeTracker_t;

/* ---------- API ---------- */

/**
 * Initialise a Gateway authentication context.
 *
 * @param ctx          Output context (caller-allocated).
 * @param gateway_id   This Gateway's node ID.
 * @param hub_id       Hub's node ID.
 * @param session_key  32-byte session key (derived from attestation or provisioning).
 */
void gateway_auth_init(
    GatewayAuthContext_t *ctx,
    uint8_t gateway_id,
    uint8_t hub_id,
    const uint8_t session_key[GATEWAY_HMAC_SIZE]);

/**
 * Compute HMAC-SHA256 over an encoded batch frame.
 *
 * The HMAC covers the entire encoded frame buffer (header with hmac_tag
 * field zeroed + all entry data). The caller must zero the hmac_tag field
 * in the encoded buffer before calling this function.
 *
 * @param ctx           Gateway auth context (must be AUTHENTICATED).
 * @param encoded_frame The complete encoded frame (header + entries).
 * @param frame_len     Total length of the encoded frame.
 * @param out_hmac      Output: 32-byte HMAC digest.
 * @return true on success, false if not authenticated.
 */
bool gateway_sign_frame(
    const GatewayAuthContext_t *ctx,
    const uint8_t *encoded_frame, size_t frame_len,
    uint8_t out_hmac[GATEWAY_HMAC_SIZE]);

/**
 * Advance the outgoing sequence number (called after successful send).
 */
void gateway_advance_sequence(GatewayAuthContext_t *ctx);

/**
 * Get the current outgoing sequence number.
 */
uint32_t gateway_get_sequence(const GatewayAuthContext_t *ctx);

/**
 * Get the Gateway's attestation state.
 */
GatewayAuthState_t gateway_get_state(const GatewayAuthContext_t *ctx);

/**
 * Set the Gateway's attestation state (called after attestation completes).
 */
void gateway_set_state(GatewayAuthContext_t *ctx, GatewayAuthState_t state);

/* ---------- Hub-side verification ---------- */

/**
 * Initialise a Gateway tracker (Hub side).
 */
void gateway_tracker_init(
    GatewayGatewayTracker_t *tracker,
    uint8_t gateway_id,
    const uint8_t session_key[GATEWAY_HMAC_SIZE]);

/**
 * Verify a received batch frame's HMAC and check sequence gap.
 *
 * The HMAC covers the encoded frame with the hmac_tag field zeroed.
 * The sequence number is extracted from the header for gap detection.
 *
 * @param tracker        Hub-side tracker for this Gateway.
 * @param encoded_frame  The received frame (including hmac_tag in header).
 * @param frame_len      Total length of the encoded frame.
 * @return GW_FRAME_OK, GW_FRAME_ERR_HMAC_MISMATCH, or GW_FRAME_ERR_GAP_DETECTED.
 */
GatewayFrameResult_t gateway_verify_frame(
    const GatewayGatewayTracker_t *tracker,
    const uint8_t *encoded_frame, size_t frame_len);

/**
 * Update the tracker's sequence number after successful frame acceptance.
 */
void gateway_tracker_update_sequence(
    GatewayGatewayTracker_t *tracker,
    uint32_t sequence);

/* ---------- Single Enforced Ingestion Entry Point (Phase 13.3) ---------- */

/**
 * THE single entry point for Hub-side ingestion of Gateway batch frames.
 *
 * Internally enforces:
 *   Step 1: Verify (HMAC + version + sequence) — Gateway trust gate.
 *   Step 2: Decode (structural parser).
 *   Step 3: Filter entries by Edge Node attestation trust (Phase 15.1).
 *
 * This is the ONLY safe API for processing incoming Gateway frames in
 * production/trusted code paths.
 *
 * Phase 15.1 change: after decoding, each entry's node_id is checked
 * against the EdgeNodeTracker. Entries from unattested or expired nodes
 * are rejected and audit-logged. Only trusted entries appear in
 * out_entries with the reduced out_count.
 *
 * @param tracker      Hub-side tracker for this Gateway.
 * @param edge_tracker Hub-side Edge Node attestation tracker.
 *                     NULL is rejected (parameter error, fail-closed).
 *                     EDGE_TRACKER_CHECK_DISABLED_TESTING_ONLY explicitly
 *                     disables the trust check for test-only use.
 * @param raw_frame    The received wire frame (header + entries + HMAC).
 * @param frame_len    Total length of the raw frame.
 * @param out_header   Output: decoded frame header.
 * @param out_entries  Output array of decoded entries (only trusted entries).
 * @param max_entries  Size of output array.
 * @param out_count    Output: number of trusted entries decoded.
 * @return GW_FRAME_OK on success, or the appropriate error code.
 */
GatewayFrameResult_t gateway_ingest_frame(
    GatewayGatewayTracker_t *tracker,
    const EdgeNodeTracker_t *edge_tracker,
    const uint8_t *raw_frame, size_t frame_len,
    BatchFrameHeader_t *out_header,
    BatchFrameEntry_t *out_entries,
    uint8_t max_entries,
    uint8_t *out_count);

/* ---------- Edge Node Trust Tracker (Phase 15.1) ---------- */

/**
 * Initialise the Edge Node attestation tracker.
 *
 * @param tracker    Output tracker (caller-allocated).
 * @param expiry_ms  Re-attestation timeout in milliseconds.
 *                   Pass 0 for default (EDGE_NODE_ATTEST_EXPIRY_MS).
 */
void edge_tracker_init(EdgeNodeTracker_t *tracker, uint64_t expiry_ms);

/**
 * Verify an Edge Node's attestation response and record trust.
 *
 * Calls attestation_verify() internally. On success, records the node
 * as trusted in the tracker with the current timestamp.
 *
 * @param tracker      Edge Node trust tracker.
 * @param challenge    The challenge that was sent to the Edge Node.
 * @param response     The response received from the Edge Node.
 * @param current_ms   Current monotonic timestamp (for expiry tracking).
 * @return ATTEST_OK if attestation succeeded and trust was recorded,
 *         or the attestation error code on failure.
 */
AttestationResult_t edge_tracker_attest(
    EdgeNodeTracker_t *tracker,
    const AttestationChallenge_t *challenge,
    const AttestationResponse_t *response,
    uint64_t current_ms);

/**
 * Check whether an Edge Node is currently trusted (attested and not expired).
 *
 * @param tracker    Edge Node trust tracker.
 * @param node_id    Edge Node to check.
 * @param current_ms Current monotonic timestamp.
 * @return true if the node has a valid, non-expired attestation record.
 */
bool edge_tracker_is_trusted(
    const EdgeNodeTracker_t *tracker,
    uint8_t node_id,
    uint64_t current_ms);

/**
 * Get the number of active entries in the tracker.
 */
uint8_t edge_tracker_count(const EdgeNodeTracker_t *tracker);

#endif /* GATEWAY_AUTH_H */
