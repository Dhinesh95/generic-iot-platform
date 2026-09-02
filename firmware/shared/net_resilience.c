/**
 * @file net_resilience.c
 * @brief Network resilience implementation — offline queue, backpressure, reconnect.
 */

#include "net_resilience.h"
#include <string.h>

/* ---------- API ---------- */

bool net_resilience_init(NetResilienceContext_t *ctx)
{
    if (!ctx) return false;
    memset(ctx, 0, sizeof(*ctx));
    ctx->reconnect.max_delay_ms = RECONNECT_MAX_DELAY_MS;
    ctx->reconnect.delay_ms = RECONNECT_BASE_DELAY_MS;
    ctx->pressure = PRESSURE_OK;
    ctx->cloud_connected = true;
    ctx->initialized = true;
    return true;
}

bool net_resilience_enqueue(NetResilienceContext_t *ctx,
                             const OfflineRecord_t *record)
{
    if (!ctx || !ctx->initialized || !record) return false;

    if (ctx->queue.count >= OFFLINE_QUEUE_MAX_RECORDS) {
        ctx->queue.overflow = true;
        return false;
    }

    uint16_t slot = (ctx->queue.head + ctx->queue.count) % OFFLINE_QUEUE_MAX_RECORDS;
    ctx->queue.records[slot] = *record;
    ctx->queue.count++;

    return true;
}

bool net_resilience_dequeue(NetResilienceContext_t *ctx,
                             OfflineRecord_t *record)
{
    if (!ctx || !ctx->initialized || !record) return false;
    if (ctx->queue.count == 0) return false;

    *record = ctx->queue.records[ctx->queue.head];
    ctx->queue.head = (ctx->queue.head + 1) % OFFLINE_QUEUE_MAX_RECORDS;
    ctx->queue.count--;

    return true;
}

uint16_t net_resilience_queue_count(const NetResilienceContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return 0;
    return ctx->queue.count;
}

bool net_resilience_queue_overflowed(const NetResilienceContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return false;
    return ctx->queue.overflow;
}

BackpressureLevel_t net_resilience_update_connection(
    NetResilienceContext_t *ctx, bool connected, uint32_t free_heap)
{
    if (!ctx || !ctx->initialized) return PRESSURE_CRITICAL;

    ctx->cloud_connected = connected;

    /* Evaluate backpressure based on connection + memory. */
    if (!connected) {
        ctx->pressure = PRESSURE_HIGH;
    } else if (free_heap < 10240) {
        ctx->pressure = PRESSURE_CRITICAL;
    } else if (free_heap < 32768) {
        ctx->pressure = PRESSURE_HIGH;
    } else if (free_heap < 65536) {
        ctx->pressure = PRESSURE_MODERATE;
    } else {
        ctx->pressure = PRESSURE_OK;
    }

    return ctx->pressure;
}

bool net_resilience_can_publish(const NetResilienceContext_t *ctx, uint32_t now_ms)
{
    if (!ctx || !ctx->initialized) return false;
    if (!ctx->cloud_connected) return false;
    if (ctx->pressure == PRESSURE_CRITICAL) return false;

    /* Rate limiting based on pressure level. */
    uint32_t min_interval = ctx->cloud_publish_rate_ms;
    switch (ctx->pressure) {
        case PRESSURE_MODERATE: min_interval *= 4; break;
        case PRESSURE_HIGH:     return false;  /* No publishing at high pressure. */
        case PRESSURE_CRITICAL: return false;
        default: break;
    }

    if (min_interval == 0) min_interval = 1000; /* Default 1 second. */

    return (now_ms - ctx->last_publish_ms) >= min_interval;
}

void net_resilience_report_publish(NetResilienceContext_t *ctx, uint32_t now_ms)
{
    if (!ctx || !ctx->initialized) return;
    ctx->last_publish_ms = now_ms;
}

uint32_t net_resilience_get_reconnect_delay(NetResilienceContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return RECONNECT_MAX_DELAY_MS;
    return ctx->reconnect.delay_ms;
}

void net_resilience_report_reconnect_fail(NetResilienceContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return;

    ctx->reconnect.attempt++;
    ctx->reconnect.delay_ms *= RECONNECT_MULTIPLIER;
    if (ctx->reconnect.delay_ms > ctx->reconnect.max_delay_ms) {
        ctx->reconnect.delay_ms = ctx->reconnect.max_delay_ms;
    }
    ctx->reconnect.waiting = true;
}

void net_resilience_report_reconnect_success(NetResilienceContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return;

    ctx->reconnect.attempt = 0;
    ctx->reconnect.delay_ms = RECONNECT_BASE_DELAY_MS;
    ctx->reconnect.waiting = false;
    ctx->cloud_connected = true;
}

BackpressureLevel_t net_resilience_get_pressure(const NetResilienceContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return PRESSURE_CRITICAL;
    return ctx->pressure;
}
