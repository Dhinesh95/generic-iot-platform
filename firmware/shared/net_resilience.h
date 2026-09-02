/**
 * @file net_resilience.h
 * @brief Network resilience — offline queue, backpressure, reconnection.
 *
 * Architecture ref: Phase 24 (Network Resilience & Backpressure).
 *
 * Handles MQTT disconnects, LoRa packet loss, and cloud unreachability
 * with an offline telemetry queue, backpressure levels, and exponential
 * backoff reconnection strategy.
 */

#ifndef NET_RESILIENCE_H
#define NET_RESILIENCE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------- Constants ---------- */

#define OFFLINE_QUEUE_MAX_RECORDS  512
#define RECONNECT_MAX_DELAY_MS     300000  /* 5 minutes cap */
#define RECONNECT_BASE_DELAY_MS    1000    /* 1 second initial */
#define RECONNECT_MULTIPLIER       2

/* ---------- Types ---------- */

/**
 * Minimal telemetry record for the offline queue.
 */
typedef struct {
    uint32_t timestamp_ms;
    uint8_t  node_id;
    uint8_t  metric_id;
    float    value;
    bool     valid;
} OfflineRecord_t;

/**
 * Offline telemetry queue (ring buffer).
 */
typedef struct {
    OfflineRecord_t records[OFFLINE_QUEUE_MAX_RECORDS];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    bool     overflow;    /* True if records were dropped. */
} OfflineQueue_t;

/**
 * Backpressure level.
 */
typedef enum {
    PRESSURE_OK = 0,          /* Normal operation */
    PRESSURE_MODERATE,        /* Reduce cloud publish rate */
    PRESSURE_HIGH,            /* Local-only, queue for later */
    PRESSURE_CRITICAL         /* Drop non-safety data */
} BackpressureLevel_t;

/**
 * Reconnection strategy (exponential backoff).
 */
typedef struct {
    uint32_t attempt;
    uint32_t delay_ms;
    uint32_t max_delay_ms;
    uint32_t next_retry_ms;
    bool     waiting;
} ReconnectStrategy_t;

/**
 * Network resilience context.
 */
typedef struct {
    OfflineQueue_t       queue;
    ReconnectStrategy_t  reconnect;
    BackpressureLevel_t  pressure;
    uint32_t             cloud_publish_rate_ms;
    uint32_t             last_publish_ms;
    bool                 cloud_connected;
    bool                 initialized;
} NetResilienceContext_t;

/* ---------- API ---------- */

/**
 * Initialize network resilience.
 *
 * @param ctx  Context (caller-owned).
 * @return true on success.
 */
bool net_resilience_init(NetResilienceContext_t *ctx);

/**
 * Enqueue a telemetry record for later delivery.
 *
 * @param ctx     Context.
 * @param record  The record to enqueue.
 * @return true if enqueued, false if queue full (record dropped).
 */
bool net_resilience_enqueue(NetResilienceContext_t *ctx,
                             const OfflineRecord_t *record);

/**
 * Dequeue the oldest record for delivery.
 *
 * @param ctx     Context.
 * @param record  Output: dequeued record.
 * @return true if a record was dequeued, false if queue empty.
 */
bool net_resilience_dequeue(NetResilienceContext_t *ctx,
                             OfflineRecord_t *record);

/**
 * Get the number of records in the offline queue.
 */
uint16_t net_resilience_queue_count(const NetResilienceContext_t *ctx);

/**
 * Check if the offline queue overflowed (records were dropped).
 */
bool net_resilience_queue_overflowed(const NetResilienceContext_t *ctx);

/**
 * Update cloud connection status and evaluate backpressure.
 *
 * @param ctx         Context.
 * @param connected   Whether cloud is currently reachable.
 * @param free_heap   Current free heap bytes (for memory pressure).
 * @return Current backpressure level.
 */
BackpressureLevel_t net_resilience_update_connection(
    NetResilienceContext_t *ctx, bool connected, uint32_t free_heap);

/**
 * Check if a publish is allowed given the current backpressure.
 *
 * @param ctx  Context.
 * @param now_ms  Current timestamp.
 * @return true if publishing is allowed right now.
 */
bool net_resilience_can_publish(const NetResilienceContext_t *ctx, uint32_t now_ms);

/**
 * Report a publish attempt (updates timing for rate limiting).
 */
void net_resilience_report_publish(NetResilienceContext_t *ctx, uint32_t now_ms);

/**
 * Get the next reconnection delay (exponential backoff).
 *
 * @param ctx  Context.
 * @return Delay in milliseconds before next retry.
 */
uint32_t net_resilience_get_reconnect_delay(NetResilienceContext_t *ctx);

/**
 * Report a failed reconnection attempt (advances backoff).
 */
void net_resilience_report_reconnect_fail(NetResilienceContext_t *ctx);

/**
 * Report a successful reconnection (resets backoff).
 */
void net_resilience_report_reconnect_success(NetResilienceContext_t *ctx);

/**
 * Get the current backpressure level.
 */
BackpressureLevel_t net_resilience_get_pressure(const NetResilienceContext_t *ctx);

#endif /* NET_RESILIENCE_H */
