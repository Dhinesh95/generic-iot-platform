/**
 * @file replay_protection.h
 * @brief Nonce + timestamp anti-replay on every frame.
 *
 * Architecture ref: Section 4, Baseline (mandatory, all domains).
 * Threat addressed: T9 (replay attack).
 *
 * This is a clean-room implementation.
 */

#ifndef REPLAY_PROTECTION_H
#define REPLAY_PROTECTION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Constants ---------- */

/** Maximum number of tracked node replay contexts. */
#define REPLAY_MAX_NODES          64

/** Size of the nonce window bitmap. Tracks last 64 nonces. */
#define REPLAY_WINDOW_SIZE        64

/** Maximum allowed timestamp drift in milliseconds. */
#define REPLAY_MAX_DRIFT_MS       10000

/* ---------- Types ---------- */

/**
 * Anti-replay state for a single peer node.
 * Tracks the highest nonce seen and a sliding window bitmap
 * to detect duplicates within the window.
 */
typedef struct {
    uint8_t  node_id;                             /**< Peer node ID. */
    uint32_t highest_nonce;                       /**< Highest nonce received so far. */
    uint64_t highest_timestamp_ms;                /**< Timestamp of the highest-nonce frame. */
    uint64_t bitmap;                              /**< Sliding window: bit i = (highest - i) seen. */
    bool     initialised;                         /**< false until first frame received. */
} ReplayNodeState_t;

/**
 * Result of replay check.
 */
typedef enum {
    REPLAY_OK,                  /**< Frame is fresh and valid. */
    REPLAY_ERR_DUPLICATE,       /**< Nonce was already seen (replay detected). */
    REPLAY_ERR_STALE_TIMESTAMP, /**< Timestamp drift too large. */
    REPLAY_ERR_PARAM_NULL       /**< NULL pointer argument. */
} ReplayCheckResult_t;

/* ---------- API ---------- */

/**
 * Initialise the replay protection subsystem.
 *
 * @return true on success.
 */
bool replay_init(void);

/**
 * Check a frame's nonce and timestamp for replay.
 * If the frame is fresh, update the replay state.
 *
 * @param node_id       Sender's node ID.
 * @param nonce         Frame nonce (monotonically increasing per sender).
 * @param timestamp_ms  Frame timestamp.
 * @return REPLAY_OK if the frame is legitimate and state is updated.
 */
ReplayCheckResult_t replay_check(
    uint8_t node_id,
    uint32_t nonce,
    uint64_t timestamp_ms
);

/**
 * Reset the replay state for a specific node (e.g. on re-attestation).
 *
 * @param node_id  Node whose state to reset.
 */
void replay_reset_node(uint8_t node_id);

/**
 * Reset all replay state.
 */
void replay_reset_all(void);

#endif /* REPLAY_PROTECTION_H */
