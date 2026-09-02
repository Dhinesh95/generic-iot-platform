/**
 * @file batch_forwarder.c
 * @brief Batch forwarder implementation (Phase 13.1 — with authentication).
 *
 * Collects dirty entries from the stateful cache, packs them into a
 * batch frame, signs with HMAC-SHA256 (if Gateway auth is enabled),
 * and forwards via the registered radio send function.
 * Dirty flags are cleared ONLY on confirmed successful send.
 */

#include "batch_forwarder.h"
#include "stateful_cache.h"
#include "gateway_auth.h"
#include "../shared/attestation.h"  /* for iot_hmac_sha256 */
#include <string.h>

/* ---------- Constants ---------- */

#define BATCH_FRAME_MAGIC    0xB7
#define BATCH_FRAME_VERSION  0x02  /* Phase 13.1: extended header + HMAC */

/* ---------- Internal state ---------- */

static RadioSendFunc_t         s_send_func = NULL;
static BatchForwarderStats_t   s_stats;
static GatewayAuthContext_t   *s_auth_ctx = NULL;  /* Optional: NULL = no signing. */

/* ---------- API ---------- */

void batch_forwarder_init(RadioSendFunc_t send_func)
{
    s_send_func = send_func;
    s_auth_ctx  = NULL;
    memset(&s_stats, 0, sizeof(s_stats));
}

void batch_forwarder_set_auth(GatewayAuthContext_t *auth_ctx)
{
    s_auth_ctx = auth_ctx;
}

uint8_t batch_forwarder_flush(void)
{
    if (!s_send_func) return 0;

    /* Phase 15.3: reject flush if Gateway auth not configured.
     * An unsigned frame would be v0x02 with zeroed HMAC — accidental
     * safety (Hub rejects due to HMAC mismatch), not deliberate format.
     * Same fragility class as Phase 13.2's original bug.
     * Production Gateways MUST always sign frames. */
    if (!s_auth_ctx || gateway_get_state(s_auth_ctx) != GW_AUTH_AUTHENTICATED) {
        return 0;  /* No auth = no send. Entries stay dirty for retry. */
    }

    /* 1. Collect dirty entries. */
    CacheEntry_t dirty[BATCH_MAX_ENTRIES];
    uint8_t dirty_count = cache_get_dirty(dirty, BATCH_MAX_ENTRIES);

    if (dirty_count == 0) return 0;

    s_stats.total_batches++;

    /* 2. Pack into batch frame. */
    BatchFrameHeader_t header;
    header.magic           = BATCH_FRAME_MAGIC;
    header.version         = BATCH_FRAME_VERSION;
    header.entry_count     = dirty_count;
    header.flags           = 0x00;
    header.sequence_number = s_auth_ctx ? gateway_get_sequence(s_auth_ctx) : 0;

    BatchFrameEntry_t entries[BATCH_MAX_ENTRIES];
    for (uint8_t i = 0; i < dirty_count; i++) {
        entries[i].node_id   = dirty[i].node_id;
        entries[i].metric_id = dirty[i].metric_id;
        entries[i].value     = dirty[i].value;
    }

    uint8_t wire_buf[BATCH_FRAME_MAX_SIZE];
    size_t  wire_len = 0;

    if (!batch_encode_frame(&header, entries, dirty_count,
                            wire_buf, sizeof(wire_buf), &wire_len)) {
        return 0;  /* Encoding failed — entries stay dirty for retry. */
    }

    /* 3. Sign frame (auth guaranteed by check at function entry).
     *    The encoded frame already has a 32-byte zeroed HMAC placeholder
     *    at the end (written by batch_encode_frame for v0x02). We compute
     *    HMAC over everything except that placeholder, then write the
     *    HMAC into the placeholder position. */
    {
        uint8_t hmac[GATEWAY_HMAC_SIZE];
        size_t  hmac_offset = wire_len - GATEWAY_HMAC_SIZE;

        if (gateway_sign_frame(s_auth_ctx, wire_buf, hmac_offset, hmac)) {
            memcpy(&wire_buf[hmac_offset], hmac, GATEWAY_HMAC_SIZE);
        }
    }

    /* 4. Send via radio. */
    bool send_ok = s_send_func(wire_buf, (uint8_t)wire_len);

    if (send_ok) {
        /* 5. Success: clear dirty flags and advance sequence. */
        for (uint8_t i = 0; i < dirty_count; i++) {
            cache_clear_dirty(dirty[i].node_id, dirty[i].metric_id);
        }
        if (s_auth_ctx) gateway_advance_sequence(s_auth_ctx);
        s_stats.total_send_ok++;
        s_stats.total_entries_sent += dirty_count;
    } else {
        /* 6. Failure: leave dirty for retry on next cycle. */
        s_stats.total_send_fail++;
    }

    return dirty_count;
}

BatchForwarderStats_t batch_forwarder_get_stats(void)
{
    return s_stats;
}

bool batch_encode_frame(
    const BatchFrameHeader_t *header,
    const BatchFrameEntry_t *entries,
    uint8_t entry_count,
    uint8_t *out_buf,
    size_t buf_size,
    size_t *out_len)
{
    if (!header || !entries || !out_buf || !out_len) return false;

    size_t needed = BATCH_FRAME_HEADER_SIZE + (size_t)entry_count * BATCH_FRAME_ENTRY_SIZE;
    if (buf_size < needed) return false;

    size_t pos = 0;

    /* Header (8 bytes: magic + version + count + flags + sequence). */
    out_buf[pos++] = header->magic;
    out_buf[pos++] = header->version;
    out_buf[pos++] = header->entry_count;
    out_buf[pos++] = header->flags;
    /* sequence_number: 4 bytes LE */
    out_buf[pos++] = (uint8_t)(header->sequence_number & 0xFF);
    out_buf[pos++] = (uint8_t)((header->sequence_number >> 8) & 0xFF);
    out_buf[pos++] = (uint8_t)((header->sequence_number >> 16) & 0xFF);
    out_buf[pos++] = (uint8_t)((header->sequence_number >> 24) & 0xFF);

    /* Entries — packed as raw bytes (little-endian). */
    for (uint8_t i = 0; i < entry_count; i++) {
        out_buf[pos++] = (uint8_t)(entries[i].node_id & 0xFF);
        out_buf[pos++] = (uint8_t)((entries[i].node_id >> 8) & 0xFF);
        out_buf[pos++] = entries[i].metric_id;
        memcpy(&out_buf[pos], &entries[i].value, 4);
        pos += 4;
    }

    /* For v0x02: append 32-byte zeroed HMAC placeholder (caller fills in). */
    if (header->version == 0x02) {
        memset(&out_buf[pos], 0, BATCH_FRAME_HMAC_SIZE);
        pos += BATCH_FRAME_HMAC_SIZE;
    }

    *out_len = pos;
    return true;
}

bool batch_decode_frame_untrusted(
    const uint8_t *wire_buf,
    size_t wire_len,
    BatchFrameHeader_t *out_header,
    BatchFrameEntry_t *out_entries,
    uint8_t max_entries,
    uint8_t *out_count)
{
    if (!wire_buf || !out_header || !out_entries || !out_count) return false;

    if (wire_len < 4) return false;  /* Minimum: magic + version + count + flags. */

    /* Parse common header (first 4 bytes). */
    out_header->magic           = wire_buf[0];
    out_header->version         = wire_buf[1];
    out_header->entry_count     = wire_buf[2];
    out_header->flags           = wire_buf[3];
    out_header->sequence_number = 0;

    /* Validate magic. */
    if (out_header->magic != BATCH_FRAME_MAGIC) return false;

    if (out_header->version == 0x01) {
        /* v0x01: 4-byte header, no sequence, no HMAC. */
        size_t entry_bytes = (size_t)out_header->entry_count * BATCH_FRAME_ENTRY_SIZE;
        if (wire_len < 4 + entry_bytes) return false;

        uint8_t count = out_header->entry_count;
        if (count > max_entries) count = max_entries;

        size_t pos = 4;
        for (uint8_t i = 0; i < count; i++) {
            out_entries[i].node_id   = (uint16_t)wire_buf[pos] | ((uint16_t)wire_buf[pos + 1] << 8);
            pos += 2;
            out_entries[i].metric_id = wire_buf[pos];
            pos += 1;
            memcpy(&out_entries[i].value, &wire_buf[pos], 4);
            pos += 4;
        }
        *out_count = count;
        return true;
    }

    if (out_header->version != 0x02) return false;

    /* v0x02: 8-byte header (with sequence) + entries + 32-byte HMAC. */
    if (wire_len < BATCH_FRAME_HEADER_SIZE) return false;

    out_header->sequence_number = (uint32_t)wire_buf[4]
                               | ((uint32_t)wire_buf[5] << 8)
                               | ((uint32_t)wire_buf[6] << 16)
                               | ((uint32_t)wire_buf[7] << 24);

    /* For v0x02, the wire includes HMAC at the end (32 bytes after entries). */
    size_t entries_and_hmac_len = wire_len - BATCH_FRAME_HEADER_SIZE;
    size_t entry_bytes = (size_t)out_header->entry_count * BATCH_FRAME_ENTRY_SIZE;

    /* v0x02: entries + 32-byte HMAC. Entries = total - HMAC. */
    if (entries_and_hmac_len < BATCH_FRAME_HMAC_SIZE) return false;
    entry_bytes = entries_and_hmac_len - BATCH_FRAME_HMAC_SIZE;

    /* Validate entry count matches available data. */
    size_t expected_entry_bytes = (size_t)out_header->entry_count * BATCH_FRAME_ENTRY_SIZE;
    if (entry_bytes < expected_entry_bytes) return false;

    uint8_t count = out_header->entry_count;
    if (count > max_entries) count = max_entries;

    /* Parse entries. */
    size_t pos = BATCH_FRAME_HEADER_SIZE;
    for (uint8_t i = 0; i < count; i++) {
        out_entries[i].node_id   = (uint16_t)wire_buf[pos] | ((uint16_t)wire_buf[pos + 1] << 8);
        pos += 2;
        out_entries[i].metric_id = wire_buf[pos];
        pos += 1;
        memcpy(&out_entries[i].value, &wire_buf[pos], 4);
        pos += 4;
    }

    *out_count = count;
    return true;
}
