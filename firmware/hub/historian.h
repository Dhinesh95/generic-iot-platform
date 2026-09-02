/**
 * @file historian.h
 * @brief Historian — 30-day offline-resilience rotating record store.
 *
 * Architecture ref: Section 12 (flash wear), cloud_dashboard_design.md
 * ("the on-device historian remains the offline-resilience source of truth").
 *
 * Tier placement: HUB (Tier 3).
 *   - Edge Node (PY32): 64KB flash, 8KB RAM — too constrained for 30-day
 *     ring buffer. The Edge is a lightweight sensor node, not a data store.
 *   - Gateway: fog-computing relay, not designed for long-term storage.
 *   - Hub (ESP32): sufficient RAM (~520KB SRAM + 4MB PSRAM) and flash
 *     (4MB+) for a capacity-bounded ring buffer. Already runs the ingestion
 *     pipeline where historian writes naturally integrate.
 *
 * Capacity arithmetic (Water Treatment worst case):
 *   Record plaintext: 24 bytes (timestamp 8B + value 4B + sequence 4B +
 *     node_id 2B + metric_id 1B + domain_profile_id 1B + reserved 4B)
 *   Encrypted on-disk: 24B ciphertext + 16B CCM auth tag = 40 bytes/record
 *   Write rate: 5,000 records/day (aggressive Water Treatment estimate)
 *   30-day window: 150,000 records × 40B = 5.7MB
 *   Default capacity: 16,384 records = 640KB (fits in ESP32 flash)
 *   At Home rate (500/day): ~33 days → 30-day window is binding
 *   At Water Treatment rate (5,000/day): ~3.3 days → capacity is binding
 *   FINDING: at Water Treatment rates, default capacity provides only ~3.3
 *   days. Production Water Treatment deployments need external flash or a
 *   larger allocation to reach the 30-day target.
 *
 * Encryption at rest:
 *   Uses mbedTLS AES-128-CCM (same primitives as transport_encryption.c)
 *   with a DEDICATED key derived from the device attestation key via
 *   HMAC-SHA256("historian_data_at_rest"). This key is distinct from the
 *   transport session key — data-at-rest vs data-in-transit are separate
 *   trust domains with separate key material.
 *
 * Storage via callback pattern (mirrors audit_log/device_identity):
 *   In production: LittleFS-backed. For testing: RAM-backed mock.
 */

#ifndef HISTORIAN_H
#define HISTORIAN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------- Constants ---------- */

/**
 * Default capacity (number of records) for the ring buffer.
 * Power of 2 for efficient modulo via bitmask.
 * 16,384 records × 40 bytes = 640KB on-disk (with encryption).
 * Configurable at compile time via HISTORIAN_CAPACITY_OVERRIDE.
 */
#ifdef HISTORIAN_CAPACITY_OVERRIDE
#define HISTORIAN_CAPACITY  HISTORIAN_CAPACITY_OVERRIDE
#else
#define HISTORIAN_CAPACITY  16384
#endif

/** Size of the CCM auth tag for data-at-rest (stronger than transport's 4B). */
#define HISTORIAN_CCM_TAG_SIZE   16

/** Size of the CCM nonce. */
#define HISTORIAN_CCM_NONCE_SIZE 13

/** Size of the AES-128 key. */
#define HISTORIAN_KEY_SIZE       16

/** On-disk record size: ciphertext (24B) + auth tag (16B) = 40 bytes. */
#define HISTORIAN_ONDISK_RECORD_SIZE  40

/**
 * Maximum records retained at the configured capacity.
 * This is the HARD limit — oldest records are evicted when full.
 */
#define HISTORIAN_MAX_RECORDS  HISTORIAN_CAPACITY

/** Eviction threshold: 30 days in milliseconds. */
#define HISTORIAN_30_DAY_MS  (30ULL * 24 * 60 * 60 * 1000)

/** Batch flush interval: 60 seconds in milliseconds.
 * Records are buffered in RAM and flushed to flash at this interval.
 * Each flush writes only new records (append-only), not the entire ring. */
#define HISTORIAN_FLUSH_INTERVAL_MS  (60ULL * 1000)

/** Maximum pending records before forced flush.
 * This cap exists for burst-write scenarios exceeding steady-state rates.
 * At normal domain write rates (500-5,000 records/day), the 60-second
 * timer always fires first — this cap never triggers under steady state.
 * See Phase 16.1 arithmetic in flash_wear_budget.md. */
#define HISTORIAN_PENDING_MAX  64

/* ---------- Types ---------- */

/**
 * Historian record (plaintext, in-memory representation).
 * Exactly 24 bytes packed.
 */
typedef struct __attribute__((packed)) {
    uint64_t timestamp_ms;      /**< Timestamp from time_source_get_ms(). */
    float    value;             /**< Sensor reading value. */
    uint32_t sequence;          /**< Monotonic sequence for ordering. */
    uint16_t node_id;           /**< Edge Node RS-485 address. */
    uint8_t  metric_id;         /**< Metric identifier. */
    uint8_t  domain_profile_id; /**< Domain profile (0=Home, 1=Agri, etc.). */
    uint8_t  reserved[4];       /**< Padding for alignment. */
} HistorianRecord_t;

/* Compile-time assertion: record must be exactly 24 bytes. */
_Static_assert(sizeof(HistorianRecord_t) == 24,
               "HistorianRecord_t must be 24 bytes packed");

/**
 * Historian statistics (for diagnostics/telemetry).
 */
typedef struct {
    uint32_t records_written;    /**< Total records written since init. */
    uint32_t records_evicted;    /**< Total records evicted (capacity or age). */
    uint32_t write_errors;       /**< Failed writes (storage backend). */
    uint32_t read_errors;        /**< Failed reads (storage backend or decrypt). */
    uint32_t enc_errors;         /**< Encryption failures. */
    uint32_t dec_errors;         /**< Decryption failures. */
} HistorianStats_t;

/**
 * Query filter for read-back API.
 * Zero-valued fields act as wildcards (match all).
 */
typedef struct {
    uint64_t time_start_ms;      /**< Inclusive start time (0 = no lower bound). */
    uint64_t time_end_ms;        /**< Inclusive end time (0 = no upper bound). */
    uint16_t node_id;            /**< Filter by node (0 = all nodes). */
    uint8_t  metric_id;          /**< Filter by metric (0 = all metrics). */
} HistorianQuery_t;

/**
 * Storage backend for historian persistence.
 * In production: LittleFS callbacks. For testing: RAM-backed mock.
 * Follows the same pattern as AuditLogStorage_t / DeviceIdentityStorage_t.
 */
typedef struct {
    /**
     * Append a single encrypted record to persistent storage.
     * This is the primary write path — called on batch flush.
     * Only the new record is written (append-only), not the entire ring.
     *
     * @param index        Ring position to write to (0..capacity-1).
     * @param record       Encrypted record (HISTORIAN_ONDISK_RECORD_SIZE bytes).
     * @param count        Current total record count (for metadata update).
     * @param write_index  Current write index (for metadata update).
     * @return true on success.
     */
    bool (*append)(uint32_t index, const uint8_t *record,
                   uint32_t count, uint32_t write_index);

    /**
     * Load the ring buffer from persistent storage.
     *
     * @param records       Output buffer for on-disk records.
     * @param max_records   Size of output buffer (in records).
     * @param out_count     Output: number of valid records loaded.
     * @param out_write_index Output: write index to restore.
     * @return true on success.
     */
    bool (*load)(uint8_t *records, uint32_t max_records,
                 uint32_t *out_count, uint32_t *out_write_index);
} HistorianStorage_t;

/* ---------- API ---------- */

/**
 * Initialise the historian subsystem.
 * Clears the ring buffer, resets counters, loads from storage if registered.
 *
 * @return true on success.
 */
bool historian_init(void);

/**
 * Register a storage backend for historian persistence.
 * Must be called before historian_init() if persistence is desired.
 *
 * @param storage  Storage backend callbacks. Pass NULL to disable persistence.
 */
void historian_set_storage(const HistorianStorage_t *storage);

/**
 * Set the encryption key for data-at-rest.
 * Must be called before historian_init().
 * Key is derived from the device attestation key via
 * HMAC-SHA256("historian_data_at_rest") in production.
 *
 * @param key      16-byte AES-128 key.
 * @param key_len  Length of key (must be HISTORIAN_KEY_SIZE).
 */
void historian_set_key(const uint8_t *key, size_t key_len);

/**
 * Write a record to the historian.
 * Encrypts with AES-128-CCM before storage. If the ring buffer is full,
 * the oldest record is evicted. If the record is older than 30 days,
 * it is evicted regardless of capacity.
 *
 * @param record  The record to write (plaintext).
 * @return true on success, false on encryption or storage failure.
 */
bool historian_write(const HistorianRecord_t *record);

/**
 * Read records matching a query filter.
 * Decrypts records from storage and returns plaintext.
 *
 * @param query       Filter criteria (zero-valued fields = wildcard).
 * @param out_records Output array for matching records.
 * @param max_records Size of output array.
 * @return Number of matching records written to out_records.
 */
uint32_t historian_read(
    const HistorianQuery_t *query,
    HistorianRecord_t *out_records,
    uint32_t max_records
);

/**
 * Get the current number of records in the historian.
 *
 * @return Record count.
 */
uint32_t historian_count(void);

/**
 * Get historian statistics.
 *
 * @return Pointer to stats (read-only).
 */
const HistorianStats_t *historian_get_stats(void);

/**
 * Reset historian statistics.
 */
void historian_reset_stats(void);

/**
 * Get the on-disk record size (for storage backend allocation).
 *
 * @return HISTORIAN_ONDISK_RECORD_SIZE (40 bytes).
 */
uint32_t historian_ondisk_record_size(void);

/**
 * Get the configured capacity (max records).
 *
 * @return HISTORIAN_CAPACITY.
 */
uint32_t historian_capacity(void);

/**
 * Flush pending records to persistent storage.
 * Called periodically (every HISTORIAN_FLUSH_INTERVAL_MS) to batch
 * flash writes. Each flush writes only the new records since the
 * last flush (append-only), not the entire ring buffer.
 *
 * @param current_ms  Current monotonic timestamp (ms).
 */
void historian_flush(uint64_t current_ms);

/**
 * Get the number of pending (unflushed) records.
 *
 * @return Pending record count.
 */
uint32_t historian_pending_count(void);

#endif /* HISTORIAN_H */
