/**
 * @file historian.c
 * @brief Historian implementation — 30-day offline-resilient ring buffer.
 *
 * Uses mbedTLS AES-128-CCM for encryption at rest, reusing the same
 * primitives as transport_encryption.c but with a dedicated key derived
 * from the device attestation key.
 */

#include "historian.h"
#include "../shared/attestation.h"   /* for iot_hmac_sha256 (key derivation) */
#include "../shared/time_source.h"
#include "mbedtls/ccm.h"
#include "mbedtls/aes.h"
#include <string.h>

/* ---------- Internal state ---------- */

/** On-disk encrypted records (ring buffer). */
static uint8_t s_ring[HISTORIAN_CAPACITY][HISTORIAN_ONDISK_RECORD_SIZE];

/** Ring buffer state. */
static uint32_t s_write_index = 0;    /**< Next write position. */
static uint32_t s_count = 0;          /**< Number of valid records. */

/** Batch flush state.
 * Records are buffered in RAM and flushed to flash periodically.
 * Each flush writes only new records (append-only), not the entire ring.
 * This reduces flash write amplification from 640 KB/record to 40 bytes/record.
 */
static uint32_t s_pending_start = 0;   /**< Start index of pending records in ring. */
static uint32_t s_pending_count = 0;   /**< Number of pending (unflushed) records. */
static uint64_t s_last_flush_ms = 0;   /**< Timestamp of last flush. */

/** Encryption key for data-at-rest. */
static uint8_t s_key[HISTORIAN_KEY_SIZE];
static bool    s_key_set = false;

/** Storage backend. */
static const HistorianStorage_t *s_storage = NULL;

/** Statistics. */
static HistorianStats_t s_stats;

/** Module state. */
static bool s_initialised = false;

/* ---------- Internal: CCM encryption/decryption ---------- */

/**
 * Build a CCM nonce from the record's ring index and a monotonic counter.
 * Layout: [index(4)] [counter(4)] [zero_pad(5)] = 13 bytes.
 * The index ensures different records at the same counter value have
 * different nonces (critical for CCM security).
 */
static void build_nonce(uint8_t nonce[HISTORIAN_CCM_NONCE_SIZE],
                        uint32_t record_index, uint32_t counter)
{
    memset(nonce, 0, HISTORIAN_CCM_NONCE_SIZE);
    nonce[0] = (record_index >> 24) & 0xff;
    nonce[1] = (record_index >> 16) & 0xff;
    nonce[2] = (record_index >>  8) & 0xff;
    nonce[3] = (record_index      ) & 0xff;
    nonce[4] = (counter >> 24) & 0xff;
    nonce[5] = (counter >> 16) & 0xff;
    nonce[6] = (counter >>  8) & 0xff;
    nonce[7] = (counter      ) & 0xff;
}

/**
 * Encrypt a plaintext record into the on-disk format.
 * On-disk: [ciphertext(24)] [auth_tag(16)] = 40 bytes.
 */
static bool encrypt_record(uint32_t ring_index, uint32_t counter,
                           const HistorianRecord_t *plaintext,
                           uint8_t ondisk[HISTORIAN_ONDISK_RECORD_SIZE])
{
    if (!s_key_set) return false;

    uint8_t nonce[HISTORIAN_CCM_NONCE_SIZE];
    build_nonce(nonce, ring_index, counter);

    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);

    int ret = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES,
                                  s_key, HISTORIAN_KEY_SIZE * 8);
    if (ret != 0) {
        mbedtls_ccm_free(&ctx);
        return false;
    }

    /*
     * mbedtls_ccm_encrypt_and_tag outputs: ciphertext || tag.
     * We split them: ciphertext → ondisk[0..23], tag → ondisk[24..39].
     */
    uint8_t enc_buf[sizeof(HistorianRecord_t) + HISTORIAN_CCM_TAG_SIZE];

    ret = mbedtls_ccm_encrypt_and_tag(
        &ctx,
        sizeof(HistorianRecord_t),       /* plaintext length */
        nonce, HISTORIAN_CCM_NONCE_SIZE, /* nonce */
        NULL, 0,                         /* No AAD */
        (const uint8_t *)plaintext,      /* input */
        enc_buf,                         /* output ciphertext */
        enc_buf + sizeof(HistorianRecord_t), /* tag output */
        HISTORIAN_CCM_TAG_SIZE           /* tag length */
    );

    mbedtls_ccm_free(&ctx);

    if (ret != 0) return false;

    /* Split into on-disk format. */
    memcpy(ondisk, enc_buf, sizeof(HistorianRecord_t));
    memcpy(ondisk + sizeof(HistorianRecord_t),
           enc_buf + sizeof(HistorianRecord_t),
           HISTORIAN_CCM_TAG_SIZE);

    return true;
}

/**
 * Decrypt an on-disk record back to plaintext.
 */
static bool decrypt_record(uint32_t ring_index, uint32_t counter,
                           const uint8_t ondisk[HISTORIAN_ONDISK_RECORD_SIZE],
                           HistorianRecord_t *plaintext)
{
    if (!s_key_set) return false;

    uint8_t nonce[HISTORIAN_CCM_NONCE_SIZE];
    build_nonce(nonce, ring_index, counter);

    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);

    int ret = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES,
                                  s_key, HISTORIAN_KEY_SIZE * 8);
    if (ret != 0) {
        mbedtls_ccm_free(&ctx);
        return false;
    }

    /* Assemble ciphertext || tag for mbedTLS. */
    uint8_t auth_buf[sizeof(HistorianRecord_t) + HISTORIAN_CCM_TAG_SIZE];
    memcpy(auth_buf, ondisk, sizeof(HistorianRecord_t));
    memcpy(auth_buf + sizeof(HistorianRecord_t),
           ondisk + sizeof(HistorianRecord_t),
           HISTORIAN_CCM_TAG_SIZE);

    ret = mbedtls_ccm_auth_decrypt(
        &ctx,
        sizeof(HistorianRecord_t),       /* ciphertext length */
        nonce, HISTORIAN_CCM_NONCE_SIZE, /* nonce */
        NULL, 0,                         /* No AAD */
        auth_buf,                        /* input (ciphertext || tag) */
        (uint8_t *)plaintext,            /* output (decrypted) */
        auth_buf + sizeof(HistorianRecord_t), /* expected tag */
        HISTORIAN_CCM_TAG_SIZE           /* tag length */
    );

    mbedtls_ccm_free(&ctx);

    if (ret == MBEDTLS_ERR_CCM_AUTH_FAILED) return false;
    if (ret != 0) return false;

    return true;
}

/* ---------- Internal: persistence ---------- */

/**
 * Buffer a record for deferred flash write.
 * The ring buffer in RAM is always up-to-date; flash persistence
 * is batched to reduce write amplification.
 */
static void historian_buffer_for_flush(uint32_t ring_index)
{
    if (s_pending_count == 0) {
        s_pending_start = ring_index;
    }
    s_pending_count++;
}

/**
 * Flush pending records to flash via the storage append callback.
 * Each flush writes only the new records (40 bytes each), not the
 * entire ring buffer (640 KB). This is the critical fix for the
 * flash-wear budget: without it, each record write triggers a
 * 640 KB flash write (20,000× amplification).
 */
void historian_flush(uint64_t current_ms)
{
    if (!s_initialised) return;
    if (s_pending_count == 0) return;
    if (!s_storage || !s_storage->append) return;

    /* Write each pending record via the append callback. */
    for (uint32_t i = 0; i < s_pending_count; i++) {
        uint32_t pos = (s_pending_start + i) % HISTORIAN_CAPACITY;
        if (!s_storage->append(pos, s_ring[pos], s_count, s_write_index)) {
            s_stats.write_errors++;
        }
    }

    s_pending_count = 0;
    s_pending_start = 0;
    s_last_flush_ms = current_ms;
}

uint32_t historian_pending_count(void)
{
    return s_pending_count;
}

/**
 * Auto-flush: if interval elapsed or pending buffer full, flush now.
 */
static void historian_auto_flush(uint64_t current_ms)
{
    if (s_pending_count >= HISTORIAN_PENDING_MAX) {
        historian_flush(current_ms);
    } else if (s_last_flush_ms > 0 &&
               (current_ms - s_last_flush_ms) >= HISTORIAN_FLUSH_INTERVAL_MS) {
        historian_flush(current_ms);
    }
}

/* ---------- Internal: eviction ---------- */

/**
 * Evict the oldest record if the ring is at capacity.
 * Also evicts records older than 30 days.
 */
static void historian_evict(uint64_t current_ms)
{
    /* Evict by age: remove leading records older than 30 days.
     * Each record is encrypted with its ring position as the nonce
     * counter, making decryption deterministic from the position alone
     * (survives reboot since ring position is restored from storage). */
    while (s_count > 0) {
        uint32_t oldest_pos = (s_write_index - s_count + HISTORIAN_CAPACITY) % HISTORIAN_CAPACITY;
        HistorianRecord_t oldest;
        if (decrypt_record(oldest_pos, oldest_pos,
                           s_ring[oldest_pos], &oldest)) {
            if (current_ms > 0 && oldest.timestamp_ms > 0 &&
                (current_ms - oldest.timestamp_ms) > HISTORIAN_30_DAY_MS) {
                /* This record is too old — evict it. */
                s_count--;
                s_stats.records_evicted++;
            } else {
                break; /* Found a record within the 30-day window. */
            }
        } else {
            /* Can't decrypt oldest record — treat as corrupt, evict. */
            s_count--;
            s_stats.records_evicted++;
            s_stats.dec_errors++;
        }
    }

    /* Evict by capacity: if still full, remove oldest. */
    while (s_count >= HISTORIAN_CAPACITY) {
        s_count--;
        s_stats.records_evicted++;
    }
}

/* ---------- Public API ---------- */

bool historian_init(void)
{
    memset(&s_ring, 0, sizeof(s_ring));
    s_write_index = 0;
    s_count = 0;
    s_pending_start = 0;
    s_pending_count = 0;
    s_last_flush_ms = 0;
    memset(&s_stats, 0, sizeof(s_stats));

    /* Load from storage if registered. */
    if (s_storage && s_storage->load) {
        uint32_t loaded_count = 0;
        uint32_t loaded_write_index = 0;
        if (s_storage->load((uint8_t *)s_ring, HISTORIAN_CAPACITY,
                            &loaded_count, &loaded_write_index)) {
            if (loaded_count <= HISTORIAN_CAPACITY) {
                s_count = loaded_count;
                s_write_index = loaded_write_index % HISTORIAN_CAPACITY;
            }
        }
    }

    s_initialised = true;
    return true;
}

void historian_set_storage(const HistorianStorage_t *storage)
{
    s_storage = storage;
}

void historian_set_key(const uint8_t *key, size_t key_len)
{
    if (!key || key_len < HISTORIAN_KEY_SIZE) return;
    memcpy(s_key, key, HISTORIAN_KEY_SIZE);
    s_key_set = true;
}

bool historian_write(const HistorianRecord_t *record)
{
    if (!s_initialised) return false;
    if (!record) return false;
    if (!s_key_set) {
        s_stats.write_errors++;
        return false;
    }

    /* Evict old/full records before writing. */
    historian_evict(record->timestamp_ms);

    /* Encrypt and store. Nonce counter = ring position (survives reboot). */
    uint8_t ondisk[HISTORIAN_ONDISK_RECORD_SIZE];
    if (!encrypt_record(s_write_index, s_write_index, record, ondisk)) {
        s_stats.enc_errors++;
        s_stats.write_errors++;
        return false;
    }

    memcpy(s_ring[s_write_index], ondisk, HISTORIAN_ONDISK_RECORD_SIZE);
    uint32_t written_index = s_write_index;
    s_write_index = (s_write_index + 1) % HISTORIAN_CAPACITY;
    if (s_count < HISTORIAN_CAPACITY) {
        s_count++;
    }
    s_stats.records_written++;

    /* Buffer for deferred flash write (batch flush).
     * The ring buffer in RAM is always up-to-date for reads.
     * Flash persistence is batched to reduce write amplification. */
    historian_buffer_for_flush(written_index);
    historian_auto_flush(record->timestamp_ms);

    return true;
}

uint32_t historian_read(
    const HistorianQuery_t *query,
    HistorianRecord_t *out_records,
    uint32_t max_records)
{
    if (!s_initialised) return 0;
    if (!out_records || max_records == 0) return 0;

    uint32_t found = 0;

    /* Iterate from oldest to newest.
     * Each record's nonce counter = its ring position (survives reboot). */
    uint32_t oldest_pos = (s_write_index - s_count + HISTORIAN_CAPACITY) % HISTORIAN_CAPACITY;

    for (uint32_t j = 0; j < s_count && found < max_records; j++) {
        uint32_t pos = (oldest_pos + j) % HISTORIAN_CAPACITY;

        /* Decrypt the record. */
        HistorianRecord_t rec;
        if (!decrypt_record(pos, pos, s_ring[pos], &rec)) {
            s_stats.dec_errors++;
            continue; /* Skip corrupt/undecryptable records. */
        }

        /* Apply query filter. */
        if (query) {
            if (query->time_start_ms > 0 && rec.timestamp_ms < query->time_start_ms)
                continue;
            if (query->time_end_ms > 0 && rec.timestamp_ms > query->time_end_ms)
                continue;
            if (query->node_id != 0 && rec.node_id != query->node_id)
                continue;
            if (query->metric_id != 0 && rec.metric_id != query->metric_id)
                continue;
        }

        out_records[found++] = rec;
    }

    return found;
}

uint32_t historian_count(void)
{
    return s_count;
}

const HistorianStats_t *historian_get_stats(void)
{
    return &s_stats;
}

void historian_reset_stats(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}

uint32_t historian_ondisk_record_size(void)
{
    return HISTORIAN_ONDISK_RECORD_SIZE;
}

uint32_t historian_capacity(void)
{
    return HISTORIAN_CAPACITY;
}
