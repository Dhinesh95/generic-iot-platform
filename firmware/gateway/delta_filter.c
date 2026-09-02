/**
 * @file delta_filter.c
 * @brief Delta filter implementation.
 *
 * Compares new readings against cached values via stateful_cache.
 * Only marks entries dirty (forwarding them) when the absolute change
 * exceeds the configured per-metric threshold.
 *
 * First reads are always forwarded (no cached value to compare against).
 */

#include "delta_filter.h"
#include "stateful_cache.h"
#include <math.h>

/* ---------- Internal state ---------- */

static DeltaThreshold_t s_thresholds[DELTA_FILTER_MAX_METRICS];
static uint8_t s_config_count = 0;

/* ---------- Internal helpers ---------- */

/**
 * Find threshold config for a metric, or return NULL.
 */
static const DeltaThreshold_t *find_threshold(uint8_t metric_id)
{
    for (uint8_t i = 0; i < s_config_count; i++) {
        if (s_thresholds[i].configured && s_thresholds[i].metric_id == metric_id) {
            return &s_thresholds[i];
        }
    }
    return NULL;
}

/* ---------- API ---------- */

void delta_filter_init(void)
{
    for (uint8_t i = 0; i < DELTA_FILTER_MAX_METRICS; i++) {
        s_thresholds[i].configured = false;
    }
    s_config_count = 0;
}

bool delta_filter_set_threshold(uint8_t metric_id, float abs_threshold)
{
    /* Check if already configured — update in place. */
    for (uint8_t i = 0; i < s_config_count; i++) {
        if (s_thresholds[i].configured && s_thresholds[i].metric_id == metric_id) {
            s_thresholds[i].abs_threshold = abs_threshold;
            return true;
        }
    }

    /* New slot. */
    if (s_config_count >= DELTA_FILTER_MAX_METRICS) {
        return false;
    }

    s_thresholds[s_config_count].metric_id     = metric_id;
    s_thresholds[s_config_count].abs_threshold = abs_threshold;
    s_thresholds[s_config_count].configured    = true;
    s_config_count++;

    return true;
}

DeltaFilterResult_t delta_filter_evaluate(
    uint16_t node_id, uint8_t metric_id, float new_value)
{
    /* Check if a threshold is configured for this metric. */
    const DeltaThreshold_t *t = find_threshold(metric_id);
    if (!t) {
        return DELTA_ERR_NOT_FOUND;
    }

    /* Try to read cached value. */
    float cached;
    if (!cache_read(node_id, metric_id, &cached)) {
        /* No cached value — first read, always forward. */
        return DELTA_FIRST_READ;
    }

    /* Compare: absolute delta vs threshold. */
    float delta = fabsf(new_value - cached);
    if (delta > t->abs_threshold) {
        return DELTA_FORWARD;
    }

    return DELTA_SUPPRESS;
}

uint8_t delta_filter_config_count(void)
{
    return s_config_count;
}
