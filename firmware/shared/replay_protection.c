/**
 * @file replay_protection.c
 * @brief Nonce + timestamp anti-replay with sliding window bitmap.
 *
 * Architecture ref: Section 4, Baseline (mandatory, all domains).
 * Threat addressed: T9 (replay attack).
 */

#include "replay_protection.h"
#include <string.h>

/* ---------- Internal state ---------- */

static ReplayNodeState_t g_nodes[REPLAY_MAX_NODES];
static bool g_initialised = false;

/* ---------- Internal helpers ---------- */

/**
 * Find or allocate a node state entry.
 * Returns pointer to existing entry, or allocates a new one.
 * Returns NULL if table is full.
 */
static ReplayNodeState_t *find_or_alloc_node(uint8_t node_id)
{
    ReplayNodeState_t *empty_slot = NULL;

    for (int i = 0; i < REPLAY_MAX_NODES; i++) {
        if (g_nodes[i].initialised && g_nodes[i].node_id == node_id) {
            return &g_nodes[i];
        }
        if (!g_nodes[i].initialised && empty_slot == NULL) {
            empty_slot = &g_nodes[i];
        }
    }

    /* Allocate new entry. */
    if (empty_slot) {
        memset(empty_slot, 0, sizeof(ReplayNodeState_t));
        empty_slot->node_id = node_id;
        empty_slot->initialised = true;
        return empty_slot;
    }

    return NULL;  /* Table full. */
}

/* ---------- Public API ---------- */

bool replay_init(void)
{
    memset(g_nodes, 0, sizeof(g_nodes));
    g_initialised = true;
    return true;
}

ReplayCheckResult_t replay_check(
    uint8_t node_id,
    uint32_t nonce,
    uint64_t timestamp_ms)
{
    if (!g_initialised) return REPLAY_ERR_PARAM_NULL;

    ReplayNodeState_t *state = find_or_alloc_node(node_id);
    if (!state) return REPLAY_ERR_PARAM_NULL;

    /* First frame from this node — accept and initialise. */
    if (state->highest_nonce == 0 && state->bitmap == 0) {
        state->highest_nonce = nonce;
        state->highest_timestamp_ms = timestamp_ms;
        state->bitmap = 1ULL;  /* Bit 0 = nonce 0 seen (the current nonce). */
        return REPLAY_OK;
    }

    /* Timestamp drift check. */
    uint64_t drift;
    if (timestamp_ms > state->highest_timestamp_ms) {
        drift = timestamp_ms - state->highest_timestamp_ms;
    } else {
        drift = state->highest_timestamp_ms - timestamp_ms;
    }
    if (drift > REPLAY_MAX_DRIFT_MS) {
        return REPLAY_ERR_STALE_TIMESTAMP;
    }

    if (nonce > state->highest_nonce) {
        /* New nonce is ahead — advance window. */
        uint32_t shift = nonce - state->highest_nonce;
        if (shift < REPLAY_WINDOW_SIZE) {
            state->bitmap <<= shift;
            state->bitmap |= 1ULL;  /* Mark current nonce as seen. */
        } else {
            /* Shifted past the window — reset bitmap. */
            state->bitmap = 1ULL;
        }
        state->highest_nonce = nonce;
        state->highest_timestamp_ms = timestamp_ms;
        return REPLAY_OK;
    }

    /* Nonce is at or behind highest_nonce — check bitmap. */
    uint32_t delta = state->highest_nonce - nonce;
    if (delta >= REPLAY_WINDOW_SIZE) {
        /* Outside the window — treat as replay (too old). */
        return REPLAY_ERR_DUPLICATE;
    }

    uint64_t mask = 1ULL << delta;
    if (state->bitmap & mask) {
        /* Already seen — replay! */
        return REPLAY_ERR_DUPLICATE;
    }

    /* Fresh nonce within the window — mark it. */
    state->bitmap |= mask;
    return REPLAY_OK;
}

void replay_reset_node(uint8_t node_id)
{
    for (int i = 0; i < REPLAY_MAX_NODES; i++) {
        if (g_nodes[i].initialised && g_nodes[i].node_id == node_id) {
            memset(&g_nodes[i], 0, sizeof(ReplayNodeState_t));
            return;
        }
    }
}

void replay_reset_all(void)
{
    memset(g_nodes, 0, sizeof(g_nodes));
}
