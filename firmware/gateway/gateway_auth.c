/**
 * @file gateway_auth.c
 * @brief Gateway authentication implementation.
 *
 * Uses the existing iot_hmac_sha256() for frame HMAC computation —
 * no new crypto mechanism. Sequence numbers are monotonically increasing
 * uint32_t values for gap detection.
 *
 * Phase 15.1: Added Hub-side per-Edge-Node trust tracking.
 * gateway_ingest_frame() now filters batch entries by Edge Node
 * attestation status before returning them to the caller.
 */

#include "gateway_auth.h"
#include "batch_forwarder.h"        /* for batch_decode_frame_untrusted */
#include "../shared/attestation.h"  /* for iot_hmac_sha256, attestation_verify */
#include "../shared/audit_log.h"    /* for audit_log_add */
#include <string.h>

/* ---------- Gateway-side API ---------- */

void gateway_auth_init(
    GatewayAuthContext_t *ctx,
    uint8_t gateway_id,
    uint8_t hub_id,
    const uint8_t session_key[GATEWAY_HMAC_SIZE])
{
    if (!ctx || !session_key) return;

    memset(ctx, 0, sizeof(*ctx));
    ctx->gateway_node_id = gateway_id;
    ctx->hub_node_id     = hub_id;
    memcpy(ctx->session_key, session_key, GATEWAY_HMAC_SIZE);
    ctx->tx_sequence = 0;
    ctx->state = GW_AUTH_IDLE;
}

bool gateway_sign_frame(
    const GatewayAuthContext_t *ctx,
    const uint8_t *encoded_frame, size_t frame_len,
    uint8_t out_hmac[GATEWAY_HMAC_SIZE])
{
    if (!ctx || !encoded_frame || !out_hmac) return false;
    if (ctx->state != GW_AUTH_AUTHENTICATED) return false;
    if (frame_len == 0) return false;

    iot_hmac_sha256(ctx->session_key, GATEWAY_HMAC_SIZE,
                    encoded_frame, frame_len,
                    out_hmac);

    return true;
}

void gateway_advance_sequence(GatewayAuthContext_t *ctx)
{
    if (ctx) ctx->tx_sequence++;
}

uint32_t gateway_get_sequence(const GatewayAuthContext_t *ctx)
{
    return ctx ? ctx->tx_sequence : 0;
}

GatewayAuthState_t gateway_get_state(const GatewayAuthContext_t *ctx)
{
    return ctx ? ctx->state : GW_AUTH_UNINIT;
}

void gateway_set_state(GatewayAuthContext_t *ctx, GatewayAuthState_t state)
{
    if (ctx) ctx->state = state;
}

/* ---------- Hub-side verification ---------- */

void gateway_tracker_init(
    GatewayGatewayTracker_t *tracker,
    uint8_t gateway_id,
    const uint8_t session_key[GATEWAY_HMAC_SIZE])
{
    if (!tracker || !session_key) return;

    memset(tracker, 0, sizeof(*tracker));
    tracker->gateway_node_id = gateway_id;
    memcpy(tracker->session_key, session_key, GATEWAY_HMAC_SIZE);
    tracker->sequence_initialized = false;
    tracker->authenticated = false;
}

GatewayFrameResult_t gateway_verify_frame(
    const GatewayGatewayTracker_t *tracker,
    const uint8_t *encoded_frame, size_t frame_len)
{
    if (!tracker || !encoded_frame)
        return GW_FRAME_ERR_PARAM_NULL;

    if (!tracker->authenticated)
        return GW_FRAME_ERR_NOT_AUTH;

    if (frame_len < 4)  /* Minimum: header without HMAC. */
        return GW_FRAME_ERR_PARAM_NULL;

    /*
     * CRITICAL: Version check. Reject v0x01 frames outright.
     * A v0x01 frame has no HMAC tag and no sequence number. Accepting
     * it would bypass authentication entirely — an attacker could craft
     * a v0x01 frame with arbitrary entries and the Hub would process
     * them as trusted data.
     *
     * Backward-compatible decode (batch_decode_frame accepting v0x01)
     * is for byte-layout parsing only, NOT for bypassing auth requirements.
     * Once authentication is enabled for a Gateway, only v0x02 frames
     * (with HMAC + sequence) are accepted.
     */
    if (encoded_frame[1] != 0x02) {
        audit_log_add(AUDIT_AUTH_FAILURE, 0, 0,
                      "Gateway batch frame rejected: unsupported version — possible downgrade attack");
        return GW_FRAME_ERR_HMAC_MISMATCH;  /* Reuse HMAC error for auth bypass. */
    }

    /*
     * The encoded frame layout (Phase 13.1 extended header):
     *   [magic(1)] [version(1)] [entry_count(1)] [flags(1)]
     *   [sequence_number(4)]
     *   [entries: node_id(2) + metric_id(1) + value(4)] × entry_count
     *   [hmac_tag(32)]
     *
     * To verify HMAC: zero the hmac_tag field (last 32 bytes of frame),
     * compute HMAC over the rest, compare with the stored hmac_tag.
     */
    if (frame_len < 4 + 4 + 32)  /* header + seq + hmac minimum */
        return GW_FRAME_ERR_PARAM_NULL;

    /* Extract sequence number from header (bytes 4-7). */
    uint32_t sequence = (uint32_t)encoded_frame[4]
                      | ((uint32_t)encoded_frame[5] << 8)
                      | ((uint32_t)encoded_frame[6] << 16)
                      | ((uint32_t)encoded_frame[7] << 24);

    /* Extract the HMAC tag (last 32 bytes). */
    const uint8_t *received_hmac = &encoded_frame[frame_len - GATEWAY_HMAC_SIZE];

    /* Build verification buffer: everything except the HMAC tag. */
    size_t verify_len = frame_len - GATEWAY_HMAC_SIZE;
    uint8_t expected_hmac[GATEWAY_HMAC_SIZE];

    iot_hmac_sha256(tracker->session_key, GATEWAY_HMAC_SIZE,
                    encoded_frame, verify_len,
                    expected_hmac);

    /* Constant-time comparison. */
    uint8_t diff = 0;
    for (uint8_t i = 0; i < GATEWAY_HMAC_SIZE; i++) {
        diff |= received_hmac[i] ^ expected_hmac[i];
    }
    if (diff != 0) {
        audit_log_add(AUDIT_AUTH_FAILURE, 0, 0,
                      "Gateway batch frame HMAC mismatch — possible tampering");
        return GW_FRAME_ERR_HMAC_MISMATCH;
    }

    /* Check sequence gap. */
    if (tracker->sequence_initialized) {
        if (sequence != tracker->last_sequence + 1) {
            audit_log_add(AUDIT_CONFIG_WRITE, 0, 0,
                          "Gateway sequence gap detected — possible suppression");
        }
    }

    return GW_FRAME_OK;
}

void gateway_tracker_update_sequence(
    GatewayGatewayTracker_t *tracker,
    uint32_t sequence)
{
    if (!tracker) return;
    tracker->last_sequence = sequence;
    tracker->sequence_initialized = true;
}

/* ---------- Single Enforced Ingestion Entry Point (Phase 13.3 + 15.1) ---------- */

GatewayFrameResult_t gateway_ingest_frame(
    GatewayGatewayTracker_t *tracker,
    const EdgeNodeTracker_t *edge_tracker,
    const uint8_t *raw_frame, size_t frame_len,
    BatchFrameHeader_t *out_header,
    BatchFrameEntry_t *out_entries,
    uint8_t max_entries,
    uint8_t *out_count)
{
    if (!tracker || !raw_frame || !out_header || !out_entries || !out_count)
        return GW_FRAME_ERR_PARAM_NULL;

    /* Step 1: Verify (auth gate). This MUST happen before decode. */
    GatewayFrameResult_t vr = gateway_verify_frame(tracker, raw_frame, frame_len);
    if (vr != GW_FRAME_OK) {
        /* Verification failed — do NOT decode. Return the error. */
        *out_count = 0;
        return vr;
    }

    /* Step 2: Decode (structural parser). Only reached if verification passed. */
    BatchFrameHeader_t decoded_header;
    BatchFrameEntry_t decoded_entries[BATCH_MAX_ENTRIES];
    uint8_t decoded_count = 0;

    bool decoded = batch_decode_frame_untrusted(
        raw_frame, frame_len,
        &decoded_header, decoded_entries, BATCH_MAX_ENTRIES, &decoded_count);

    if (!decoded) {
        /* Structural decode failed — frame passed HMAC but is malformed. */
        audit_log_add(AUDIT_AUTH_FAILURE, 0, 0,
                      "Gateway frame passed HMAC but failed structural decode");
        *out_count = 0;
        return GW_FRAME_ERR_HMAC_MISMATCH;
    }

    /* Step 3: Update sequence tracker (accepted frame). */
    gateway_tracker_update_sequence(tracker, decoded_header.sequence_number);

    /* Step 4: Filter entries by Edge Node attestation trust (Phase 15.1).
     *
     * Phase 15.2: NULL edge_tracker is rejected (fail-closed).
     * EDGE_TRACKER_CHECK_DISABLED_TESTING_ONLY explicitly disables
     * the trust check for test-only use — mirroring the pattern
     * from Phase 14.2 (EDGE_AUTH_UNGATED_TESTING_ONLY) and
     * Phase 13.3 (batch_decode_frame_untrusted naming).
     *
     * Production code MUST ALWAYS pass a valid edge_tracker.
     * The sentinel makes the security bypass visible at the call site. */
    if (edge_tracker == NULL) {
        /* NULL is always a parameter error — fail closed. */
        audit_log_add(AUDIT_AUTH_FAILURE, 0, 0,
                      "gateway_ingest_frame: NULL edge_tracker — parameter error");
        *out_count = 0;
        return GW_FRAME_ERR_PARAM_NULL;
    }

    bool trust_check_disabled = (edge_tracker == EDGE_TRACKER_CHECK_DISABLED_TESTING_ONLY);

    uint8_t trusted_count = 0;
    for (uint8_t i = 0; i < decoded_count && trusted_count < max_entries; i++) {
        uint8_t node_id = (uint8_t)decoded_entries[i].node_id;

        if (!trust_check_disabled) {
            /* Check if this Edge Node is currently trusted. */
            if (!edge_tracker_is_trusted(edge_tracker, node_id, 0)) {
                /* Node not trusted — reject this entry, audit-log it. */
                audit_log_add(AUDIT_AUTH_FAILURE, node_id, 0,
                              "Edge Node data rejected: attestation not verified or expired");
                continue;
            }
        }

        /* Node is trusted (or trust check explicitly disabled) — copy to output. */
        out_entries[trusted_count] = decoded_entries[i];
        trusted_count++;
    }

    *out_header = decoded_header;
    *out_count = trusted_count;

    return GW_FRAME_OK;
}

/* ---------- Edge Node Trust Tracker (Phase 15.1) ---------- */

void edge_tracker_init(EdgeNodeTracker_t *tracker, uint64_t expiry_ms)
{
    if (!tracker) return;
    memset(tracker, 0, sizeof(*tracker));
    tracker->expiry_ms = (expiry_ms > 0) ? expiry_ms : EDGE_NODE_ATTEST_EXPIRY_MS;
}

AttestationResult_t edge_tracker_attest(
    EdgeNodeTracker_t *tracker,
    const AttestationChallenge_t *challenge,
    const AttestationResponse_t *response,
    uint64_t current_ms)
{
    if (!tracker || !challenge || !response)
        return ATTEST_ERR_PARAM_NULL;

    /* Delegate actual HMAC verification to the shared attestation module. */
    AttestationResult_t result = attestation_verify(challenge, response, 5000);
    if (result != ATTEST_OK) {
        return result;
    }

    /* Attestation succeeded — record trust for this node. */
    uint8_t node_id = response->responder_id;

    /* Check if node already has an entry — update in place. */
    for (uint8_t i = 0; i < tracker->count; i++) {
        if (tracker->entries[i].active && tracker->entries[i].node_id == node_id) {
            tracker->entries[i].attested = true;
            tracker->entries[i].attested_at_ms = current_ms;
            return ATTEST_OK;
        }
    }

    /* New entry — allocate if space available. */
    if (tracker->count >= EDGE_NODE_TRACKER_MAX) {
        return ATTEST_ERR_PARAM_NULL;  /* Table full. */
    }

    EdgeNodeTrackerEntry_t *entry = &tracker->entries[tracker->count];
    entry->node_id = node_id;
    entry->attested = true;
    entry->attested_at_ms = current_ms;
    entry->active = true;
    tracker->count++;

    return ATTEST_OK;
}

bool edge_tracker_is_trusted(
    const EdgeNodeTracker_t *tracker,
    uint8_t node_id,
    uint64_t current_ms)
{
    if (!tracker) return false;

    for (uint8_t i = 0; i < tracker->count; i++) {
        if (tracker->entries[i].active && tracker->entries[i].node_id == node_id) {
            if (!tracker->entries[i].attested) return false;

            /* Check expiry if current_ms is provided (> 0). */
            if (current_ms > 0 && tracker->expiry_ms > 0) {
                uint64_t elapsed = current_ms - tracker->entries[i].attested_at_ms;
                if (elapsed > tracker->expiry_ms) {
                    return false;  /* Expired — re-attestation required. */
                }
            }

            return true;
        }
    }

    return false;  /* Node not in tracker. */
}

uint8_t edge_tracker_count(const EdgeNodeTracker_t *tracker)
{
    if (!tracker) return 0;
    return tracker->count;
}
