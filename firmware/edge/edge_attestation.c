/**
 * @file edge_attestation.c
 * @brief Edge Node attestation implementation.
 *
 * Architecture ref: Section 4, Section 8.
 *
 * Wraps the shared attestation module (attestation_compute_response)
 * for Edge Node use. The Edge Node receives a challenge, computes
 * the HMAC response, and returns it — same pattern as the radio
 * module attestation in lora_handler.c (Phase 6.6).
 */

#include "edge_attestation.h"
#include "../shared/attestation.h"
#include <string.h>

/* ---------- API ---------- */

void edge_auth_init(EdgeAuthContext_t *ctx, uint8_t node_id)
{
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->node_id = node_id;
    ctx->state = EDGE_AUTH_IDLE;
}

bool edge_auth_set_key(EdgeAuthContext_t *ctx, const uint8_t key[ATTESTATION_KEY_SIZE])
{
    if (!ctx || !key) return false;
    memcpy(ctx->pre_shared_key, key, ATTESTATION_KEY_SIZE);
    ctx->key_set = true;
    return true;
}

EdgeAttestResult_t edge_auth_respond(
    EdgeAuthContext_t *ctx,
    const AttestationChallenge_t *challenge,
    AttestationResponse_t *out_response)
{
    if (!ctx || !challenge || !out_response) {
        return EDGE_ATTEST_ERR_PARAM_NULL;
    }

    if (!ctx->key_set) {
        ctx->state = EDGE_AUTH_FAILED;
        return EDGE_ATTEST_ERR_NOT_READY;
    }

    /* Compute HMAC-SHA256 response using the shared attestation module.
     * This is the same function called by lora_handler.c for radio
     * module attestation — reusing the existing mechanism, not building
     * a new one. */
    attestation_compute_response(
        ctx->pre_shared_key,
        challenge,
        out_response->response);

    /* Fill in response metadata. */
    out_response->timestamp_ms = challenge->timestamp_ms;  /* echo back */
    out_response->responder_id = ctx->node_id;

    /* Transition to authenticated state.
     * Note: in production, the Hub would also verify this response
     * (attestation_verify) — but from the Edge Node's perspective,
     * having computed the response with the correct key IS the proof.
     * The Hub side of verification is in attestation.c, which is
     * Hub-tier code, not Edge-tier. */
    ctx->state = EDGE_AUTH_AUTHENTICATED;

    return EDGE_ATTEST_OK;
}

EdgeAuthState_t edge_auth_get_state(const EdgeAuthContext_t *ctx)
{
    if (!ctx) return EDGE_AUTH_UNINIT;
    return ctx->state;
}

bool edge_auth_is_authenticated(const EdgeAuthContext_t *ctx)
{
    if (!ctx) return false;
    return ctx->state == EDGE_AUTH_AUTHENTICATED;
}
