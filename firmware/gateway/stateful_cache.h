/**
 * @file stateful_cache.h
 * @brief Stateful per-node, per-metric value cache for Gateway fog computing.
 *
 * Architecture ref: Section 2 (Tier 2 — Gateway, fog computing).
 *                   Section 9 (validation should reuse per-node metric cache
 *                   rather than duplicate storage).
 *
 * Domain-agnostic: stores last-known value per (node_id, metric_id) pair
 * with a "dirty" flag. Used by delta_filter to suppress unchanged values
 * and by batch_forwarder to collect dirty entries for forwarding.
 *
 * Capacity: GATEWAY_MAX_NODES × GATEWAY_MAX_METRICS per Gateway device.
 */

#ifndef STATEFUL_CACHE_H
#define STATEFUL_CACHE_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Constants ---------- */

#define GATEWAY_MAX_NODES    64   /**< Maximum Edge Nodes per Gateway. */
#define GATEWAY_MAX_METRICS  16   /**< Maximum metrics per Node. */

/* ---------- Types ---------- */

/**
 * A single cache entry: one metric for one node.
 */
typedef struct {
    uint16_t node_id;       /**< Edge Node identifier. */
    uint8_t  metric_id;     /**< Metric identifier (sensor/actuator type). */
    float    value;         /**< Last-known value. */
    bool     valid;         /**< Whether this entry has ever been written. */
    bool     dirty;         /**< True if value changed since last forward. */
} CacheEntry_t;

/**
 * Cache statistics for diagnostics.
 */
typedef struct {
    uint32_t total_updates;     /**< Total write_cache() calls. */
    uint32_t total_hits;        /**< Cases where value actually changed. */
    uint32_t total_suppressed;  /**< Cases where value was identical (no-op). */
    uint8_t  active_entries;    /**< Number of entries with valid == true. */
} CacheStats_t;

/* ---------- API ---------- */

/**
 * Initialise the cache. Clears all entries.
 */
void cache_init(void);

/**
 * Write (or update) a cached value for a given (node_id, metric_id).
 *
 * If the entry already exists and the new value differs from the cached
 * value, the dirty flag is set. If the value is identical, dirty is NOT
 * set (suppressing a redundant update).
 *
 * @param node_id    Edge Node identifier.
 * @param metric_id  Metric identifier.
 * @param value      The new sensor reading.
 * @return true if the value was written, false if cache is full.
 */
bool cache_write(uint16_t node_id, uint8_t metric_id, float value);

/**
 * Read a cached value.
 *
 * @param node_id    Edge Node identifier.
 * @param metric_id  Metric identifier.
 * @param out_value  Output: the cached value.
 * @return true if found and valid, false otherwise.
 */
bool cache_read(uint16_t node_id, uint8_t metric_id, float *out_value);

/**
 * Check whether a specific entry is dirty (changed since last forward).
 *
 * @param node_id    Edge Node identifier.
 * @param metric_id  Metric identifier.
 * @return true if the entry exists, is valid, and is dirty.
 */
bool cache_is_dirty(uint16_t node_id, uint8_t metric_id);

/**
 * Clear the dirty flag for a specific entry (called after successful forward).
 *
 * @param node_id    Edge Node identifier.
 * @param metric_id  Metric identifier.
 * @return true if the entry was found and its dirty flag cleared.
 */
bool cache_clear_dirty(uint16_t node_id, uint8_t metric_id);

/**
 * Get all dirty entries (for batch forwarder to collect).
 *
 * @param out_entries  Output array (caller-allocated).
 * @param max_entries  Size of output array.
 * @return Number of dirty entries written to out_entries.
 */
uint8_t cache_get_dirty(CacheEntry_t *out_entries, uint8_t max_entries);

/**
 * Get cache statistics.
 */
CacheStats_t cache_get_stats(void);

/**
 * Get the total number of active (valid) entries in the cache.
 */
uint16_t cache_entry_count(void);

#endif /* STATEFUL_CACHE_H */
