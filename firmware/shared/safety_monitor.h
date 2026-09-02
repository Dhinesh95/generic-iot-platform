/**
 * @file safety_monitor.h
 * @brief Safety monitor — independent watchdog for the safety pipeline.
 *
 * Architecture ref: Phase 19 (Safety Monitor / Watchdog).
 * Threat addressed: Rule engine silent failure, actuator stuck-on.
 *
 * The safety monitor runs independently of the ingestion pipeline.
 * It expects a heartbeat pulse from the ingestion handler every
 * SAFETY_MONITOR_DEADLINE_MS. If the heartbeat stops, the monitor
 * triggers a failsafe override on all actuators.
 *
 * Additionally, it tracks whether SAFETY_LOCKED rules have fired
 * within their expected deadline window. If a safety rule hasn't
 * fired when it should have, the monitor raises an alarm.
 *
 * This is the architectural guarantee that "safety is not just a
 * feature — it's monitored."
 */

#ifndef SAFETY_MONITOR_H
#define SAFETY_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Constants ---------- */

/** Maximum time between heartbeat pulses (ms). */
#define SAFETY_MONITOR_DEADLINE_MS       500

/** Maximum time a SAFETY_LOCKED rule can go without evaluation (ms). */
#define SAFETY_MONITOR_RULE_DEADLINE_MS  1000

/** Maximum number of tracked safety rules. */
#define SAFETY_MONITOR_MAX_RULES         32

/** Number of consecutive missed heartbeats before failsafe triggers. */
#define SAFETY_MONITOR_MISSED_THRESHOLD  3

/* ---------- Types ---------- */

/**
 * Safety monitor state.
 */
typedef enum {
    SAFETY_STATE_IDLE = 0,          /**< No heartbeat received yet. */
    SAFETY_STATE_ACTIVE,            /**< Heartbeat is being received normally. */
    SAFETY_STATE_DEGRADED,          /**< Heartbeat missed but within threshold. */
    SAFETY_STATE_EMERGENCY          /**< Failsafe triggered — actuators in safe state. */
} SafetyMonitorState_t;

/**
 * Result of safety monitor operations.
 */
typedef enum {
    SAFETY_MON_OK,
    SAFETY_MON_ERR_NOT_INIT,        /**< Monitor not initialized. */
    SAFETY_MON_ERR_PARAM_NULL       /**< NULL pointer argument. */
} SafetyMonitorResult_t;

/**
 * Callback type for failsafe action.
 * Called when the safety monitor triggers a failsafe.
 * The implementation should set ALL actuators to their safe state.
 */
typedef void (*FailsafeCallback_t)(void);

/**
 * Per-rule tracking entry.
 * Tracks when a SAFETY_LOCKED rule was last evaluated.
 */
typedef struct {
    uint16_t rule_id;               /**< Rule identifier. */
    uint64_t last_evaluated_ms;     /**< Timestamp of last evaluation. */
    bool     active;                /**< false = slot unused. */
} SafetyRuleTracker_t;

/**
 * Safety monitor context (all state in one struct for testability).
 */
typedef struct {
    SafetyMonitorState_t   state;
    uint64_t               last_heartbeat_ms;    /**< Timestamp of last heartbeat. */
    uint32_t               missed_count;          /**< Consecutive missed heartbeats. */
    FailsafeCallback_t     failsafe_cb;           /**< Called on emergency. */
    SafetyRuleTracker_t    rules[SAFETY_MONITOR_MAX_RULES];
    uint8_t                rule_count;            /**< Number of tracked rules. */
    bool                   initialized;
} SafetyMonitorContext_t;

/* ---------- API ---------- */

/**
 * Initialize the safety monitor.
 *
 * @param ctx       Monitor context (caller-owned).
 * @param failsafe  Callback invoked when failsafe triggers. Must NOT be NULL.
 * @return SAFETY_MON_OK on success.
 */
SafetyMonitorResult_t safety_monitor_init(SafetyMonitorContext_t *ctx,
                                           FailsafeCallback_t failsafe);

/**
 * Pulse the heartbeat. Must be called periodically by the ingestion handler.
 * If not called within SAFETY_MONITOR_DEADLINE_MS, the monitor degrades.
 *
 * @param ctx  Monitor context.
 * @return SAFETY_MON_OK on success.
 */
SafetyMonitorResult_t safety_monitor_heartbeat(SafetyMonitorContext_t *ctx);

/**
 * Notify the monitor that a SAFETY_LOCKED rule was just evaluated.
 * Called by the rule engine after each safety rule evaluation.
 *
 * @param ctx        Monitor context.
 * @param rule_id    The rule that was evaluated.
 * @param timestamp  Current timestamp in ms.
 * @return SAFETY_MON_OK on success.
 */
SafetyMonitorResult_t safety_monitor_rule_evaluated(SafetyMonitorContext_t *ctx,
                                                     uint16_t rule_id,
                                                     uint64_t timestamp);

/**
 * Check the monitor state. Should be called periodically (e.g. from a
 * FreeRTOS timer task) to evaluate whether a failsafe is needed.
 *
 * @param ctx  Monitor context.
 * @return Current monitor state.
 */
SafetyMonitorState_t safety_monitor_check(const SafetyMonitorContext_t *ctx);

/**
 * Get the number of consecutive missed heartbeats.
 *
 * @param ctx  Monitor context.
 * @return Number of missed heartbeats since last successful heartbeat.
 */
uint32_t safety_monitor_get_missed_count(const SafetyMonitorContext_t *ctx);

/**
 * Manually trigger a failsafe (e.g. from tamper detect or config portal).
 *
 * @param ctx  Monitor context.
 * @return SAFETY_MON_OK on success.
 */
SafetyMonitorResult_t safety_monitor_trigger_failsafe(SafetyMonitorContext_t *ctx);

#endif /* SAFETY_MONITOR_H */
