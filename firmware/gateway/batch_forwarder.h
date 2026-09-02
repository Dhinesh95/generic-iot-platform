/**
 * @file batch_forwarder.h
 * @brief Batch forwarder — collects dirty cache entries and forwards them.
 *
 * Architecture ref: Section 2 (Tier 2 — Gateway, fog computing).
 *                   Batches dirty metrics and forwards via LoRa/Zigbee
 *                   (whichever radio handler is linked for the active profile).
 *
 * Domain-agnostic: packs multiple dirty entries into a single outgoing
 * frame, reducing per-message overhead on constrained radio links.
 * Dirty flags are cleared ONLY on confirmed successful send (ACK received),
 * mirroring the LoRa ACK/retry discipline from Phase 4.
 *
 * Forwarding strategy:
 *   1. Collect all dirty entries from the stateful cache.
 *   2. Pack them into a batch frame (header + N × {node_id, metric_id, value}).
 *   3. Forward via the registered radio send function.
 *   4. On success (ACK): clear dirty flags.
 *   5. On failure (no ACK): leave dirty for retry on next cycle.
 */

#ifndef BATCH_FORWARDER_H
#define BATCH_FORWARDER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Forward declaration for Gateway auth context (defined in gateway_auth.h). */
typedef struct GatewayAuthContext_s GatewayAuthContext_t;

/* ---------- Constants ---------- */

#define BATCH_MAX_ENTRIES  22  /**< Max entries per batch frame (22×7+8+32=194 ≤ 255 LoRa max). */

/**
 * Batch frame overhead: header(8) = magic(1) + version(1) + entry_count(1) + flags(1) + seq(4).
 * Per entry: node_id(2) + metric_id(1) + value(4) = 7 bytes.
 * HMAC tag: 32 bytes appended after entries.
 * Total max: 8 + 22×7 + 32 = 194 bytes (fits in LoRa's 255-byte payload).
 */
#define BATCH_FRAME_HEADER_SIZE   8   /* magic(1) + version(1) + count(1) + flags(1) + seq(4) */
#define BATCH_FRAME_ENTRY_SIZE    7   /* node_id(2) + metric_id(1) + value(4) */
#define BATCH_FRAME_HMAC_SIZE     32  /* HMAC-SHA256 tag */
#define BATCH_FRAME_MAX_SIZE     (BATCH_FRAME_HEADER_SIZE + BATCH_MAX_ENTRIES * BATCH_FRAME_ENTRY_SIZE + BATCH_FRAME_HMAC_SIZE)

/* ---------- Types ---------- */

/**
 * Radio send function signature.
 * Takes a raw payload buffer and length, returns true on successful send
 * (ACK received), false on failure (will retry on next cycle).
 */
typedef bool (*RadioSendFunc_t)(const uint8_t *data, uint8_t len);

/**
 * Batch forwarder statistics.
 */
typedef struct {
    uint32_t total_batches;       /**< Number of batch-forward attempts. */
    uint32_t total_entries_sent;  /**< Total entries forwarded across all batches. */
    uint32_t total_send_ok;       /**< Number of successful sends. */
    uint32_t total_send_fail;     /**< Number of failed sends (dirty flags retained). */
} BatchForwarderStats_t;

/**
 * Batch frame header (on-the-wire format, Phase 13.1 extended).
 *
 * Layout: [magic(1)] [version(1)] [entry_count(1)] [flags(1)]
 *         [sequence_number(4)]
 *         [entries: node_id(2) + metric_id(1) + value(4)] × entry_count
 *         [hmac_tag(32)]
 *
 * magic: 0xB7 (batch marker)
 * version: 0x02 (extended header with sequence + HMAC)
 * entry_count: number of entries following
 * flags: reserved (0x00)
 * sequence_number: monotonic counter for gap detection (LE uint32)
 * hmac_tag: HMAC-SHA256 over header (with hmac_tag zeroed) + entries
 */
struct BatchFrameHeader_s {
    uint8_t  magic;             /**< Must be 0xB7. */
    uint8_t  version;           /**< Frame version (0x02 for Phase 13.1). */
    uint8_t  entry_count;       /**< Number of entries. */
    uint8_t  flags;             /**< Reserved (0x00). */
    uint32_t sequence_number;   /**< Monotonic sequence (LE). */
} __attribute__((packed));
typedef struct BatchFrameHeader_s BatchFrameHeader_t;

/**
 * A single entry in a batch frame.
 */
struct BatchFrameEntry_s {
    uint16_t node_id;       /**< Edge Node identifier. */
    uint8_t  metric_id;     /**< Metric identifier. */
    float    value;         /**< Sensor reading value. */
} __attribute__((packed));
typedef struct BatchFrameEntry_s BatchFrameEntry_t;

/* ---------- API ---------- */

/**
 * Initialise the batch forwarder.
 *
 * @param send_func  The radio send function to use for forwarding.
 *                   Must not be NULL.
 */
void batch_forwarder_init(RadioSendFunc_t send_func);

/**
 * Set the Gateway authentication context for frame signing.
 * If NULL, frames are sent unsigned (backward-compatible mode).
 *
 * @param auth_ctx  Gateway auth context (must be AUTHENTICATED for signing).
 */
void batch_forwarder_set_auth(GatewayAuthContext_t *auth_ctx);

/**
 * Perform one batch-forward cycle:
 *   1. Collect all dirty entries from the cache.
 *   2. Pack into a batch frame.
 *   3. Send via the registered radio function.
 *   4. On success: clear dirty flags for all sent entries.
 *   5. On failure: leave dirty for retry.
 *
 * @return Number of entries forwarded (0 if nothing dirty or send failed).
 */
uint8_t batch_forwarder_flush(void);

/**
 * Get batch forwarder statistics.
 */
BatchForwarderStats_t batch_forwarder_get_stats(void);

/**
 * Encode a batch frame into a wire buffer.
 * Exposed for testing — not typically called directly.
 *
 * @param header       The frame header (entry_count filled by caller).
 * @param entries      Array of entries to encode.
 * @param entry_count  Number of entries.
 * @param out_buf      Output wire buffer.
 * @param buf_size     Size of output buffer.
 * @param out_len      Output: bytes written.
 * @return true on success.
 */
bool batch_encode_frame(
    const BatchFrameHeader_t *header,
    const BatchFrameEntry_t *entries,
    uint8_t entry_count,
    uint8_t *out_buf,
    size_t buf_size,
    size_t *out_len);

/**
 * Decode a batch frame from a wire buffer.
 * Exposed for testing — not typically called directly.
 *
 * @param wire_buf     Raw bytes.
 * @param wire_len     Number of bytes.
 * @param out_header   Output: frame header.
 * @param out_entries  Output array of entries.
 * @param max_entries  Size of output array.
 * @param out_count    Output: number of entries decoded.
 * @return true on success.
 */
bool batch_decode_frame_untrusted(
    const uint8_t *wire_buf,
    size_t wire_len,
    BatchFrameHeader_t *out_header,
    BatchFrameEntry_t *out_entries,
    uint8_t max_entries,
    uint8_t *out_count);

#endif /* BATCH_FORWARDER_H */
