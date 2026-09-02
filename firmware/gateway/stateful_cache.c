/**
 * @file stateful_cache.c
 * @brief Stateful per-node, per-metric value cache implementation.
 *
 * Storage: flat array of GATEWAY_MAX_NODES × GATEWAY_MAX_METRICS entries.
 * Lookup: linear scan (acceptable at this scale — 1024 entries max).
 * For production at scale (960 nodes × 16 metrics), a hash table or
 * sorted array would be more efficient, but linear scan is correct and
 * testable at this stage.
 */

#include "stateful_cache.h"
#include <string.h>

/* ---------- Internal state ---------- */

static CacheEntry_t s_entries[GATEWAY_MAX_NODES * GATEWAY_MAX_METRICS];
static uint16_t     s_entry_count = 0;
static CacheStats_t s_stats;

/* ---------- Internal helpers ---------- */

/**
 * Find an existing entry for (node_id, metric_id), or return -1.
 */
static int16_t find_entry(uint16_t node_id, uint8_t metric_id)
{
    for (uint16_t i = 0; i < s_entry_count; i++) {
        if (s_entries[i].valid &&
            s_entries[i].node_id == node_id &&
            s_entries[i].metric_id == metric_id) {
            return (int16_t)i;
        }
    }
    return -1;
}

/* ---------- API ---------- */

void cache_init(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    s_entry_count = 0;
    memset(&s_stats, 0, sizeof(s_stats));
}

bool cache_write(uint16_t node_id, uint8_t metric_id, float value)
{
    s_stats.total_updates++;

    /* Look for existing entry. */
    int16_t idx = find_entry(node_id, metric_id);
    if (idx >= 0) {
        /* Entry exists — check if value actually changed. */
        if (s_entries[idx].value == value) {
            s_stats.total_suppressed++;
            return true;  /* Written (no change needed), not dirty. */
        }
        s_entries[idx].value = value;
        s_entries[idx].dirty = true;
        s_stats.total_hits++;
        return true;
    }

    /* New entry — allocate if space available. */
    if (s_entry_count >= GATEWAY_MAX_NODES * GATEWAY_MAX_METRICS) {
        return false;  /* Cache full. */
    }

    CacheEntry_t *e = &s_entries[s_entry_count];
    e->node_id   = node_id;
    e->metric_id = metric_id;
    e->value     = value;
    e->valid     = true;
    e->dirty     = true;  /* First write is always dirty. */
    s_entry_count++;
    s_stats.total_hits++;

    return true;
}

bool cache_read(uint16_t node_id, uint8_t metric_id, float *out_value)
{
    if (!out_value) return false;

    int16_t idx = find_entry(node_id, metric_id);
    if (idx < 0 || !s_entries[idx].valid) return false;

    *out_value = s_entries[idx].value;
    return true;
}

bool cache_is_dirty(uint16_t node_id, uint8_t metric_id)
{
    int16_t idx = find_entry(node_id, metric_id);
    return (idx >= 0 && s_entries[idx].valid && s_entries[idx].dirty);
}

bool cache_clear_dirty(uint16_t node_id, uint8_t metric_id)
{
    int16_t idx = find_entry(node_id, metric_id);
    if (idx < 0 || !s_entries[idx].valid) return false;

    s_entries[idx].dirty = false;
    return true;
}

uint8_t cache_get_dirty(CacheEntry_t *out_entries, uint8_t max_entries)
{
    if (!out_entries || max_entries == 0) return 0;

    uint8_t count = 0;
    for (uint16_t i = 0; i < s_entry_count && count < max_entries; i++) {
        if (s_entries[i].valid && s_entries[i].dirty) {
            out_entries[count++] = s_entries[i];
        }
    }
    return count;
}

CacheStats_t cache_get_stats(void)
{
    /* Recompute active_entries from actual state. */
    s_stats.active_entries = 0;
    for (uint16_t i = 0; i < s_entry_count; i++) {
        if (s_entries[i].valid) s_stats.active_entries++;
    }
    return s_stats;
}

uint16_t cache_entry_count(void)
{
    return s_entry_count;
}
