/**
 * @file safety_monitor.c
 * @brief Safety monitor implementation — independent watchdog for safety pipeline.
 */

#include "safety_monitor.h"
#include "../shared/time_source.h"
#include <string.h>

/* ---------- API ---------- */

SafetyMonitorResult_t safety_monitor_init(SafetyMonitorContext_t *ctx,
                                           FailsafeCallback_t failsafe)
{
    if (!ctx || !failsafe) return SAFETY_MON_ERR_PARAM_NULL;

    memset(ctx, 0, sizeof(*ctx));
    ctx->failsafe_cb = failsafe;
    ctx->state = SAFETY_STATE_IDLE;
    ctx->initialized = true;

    return SAFETY_MON_OK;
}

SafetyMonitorResult_t safety_monitor_heartbeat(SafetyMonitorContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return SAFETY_MON_ERR_PARAM_NULL;

    uint64_t now = time_source_get_ms();
    ctx->last_heartbeat_ms = now;
    ctx->missed_count = 0;

    /* Transition to active on heartbeat. */
    if (ctx->state == SAFETY_STATE_DEGRADED ||
        ctx->state == SAFETY_STATE_IDLE) {
        ctx->state = SAFETY_STATE_ACTIVE;
    }

    return SAFETY_MON_OK;
}

SafetyMonitorResult_t safety_monitor_rule_evaluated(SafetyMonitorContext_t *ctx,
                                                     uint16_t rule_id,
                                                     uint64_t timestamp)
{
    if (!ctx || !ctx->initialized) return SAFETY_MON_ERR_PARAM_NULL;

    /* Find existing slot or allocate new one. */
    for (uint8_t i = 0; i < ctx->rule_count; i++) {
        if (ctx->rules[i].active && ctx->rules[i].rule_id == rule_id) {
            ctx->rules[i].last_evaluated_ms = timestamp;
            return SAFETY_MON_OK;
        }
    }

    /* Allocate new slot. */
    if (ctx->rule_count < SAFETY_MONITOR_MAX_RULES) {
        ctx->rules[ctx->rule_count].rule_id = rule_id;
        ctx->rules[ctx->rule_count].last_evaluated_ms = timestamp;
        ctx->rules[ctx->rule_count].active = true;
        ctx->rule_count++;
        return SAFETY_MON_OK;
    }

    return SAFETY_MON_OK; /* Rules full — silently ignore (non-critical). */
}

SafetyMonitorState_t safety_monitor_check(const SafetyMonitorContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return SAFETY_STATE_EMERGENCY;

    /* Emergency is latched — once triggered, stays until re-init. */
    if (ctx->state == SAFETY_STATE_EMERGENCY) {
        return SAFETY_STATE_EMERGENCY;
    }

    uint64_t now = time_source_get_ms();

    /* Check heartbeat staleness. */
    if (ctx->state == SAFETY_STATE_IDLE) {
        /* No heartbeat ever received — check if we've been running long enough
         * to expect one. If more than 2x deadline has passed, it's a problem. */
        if (now > (2 * SAFETY_MONITOR_DEADLINE_MS)) {
            return SAFETY_STATE_EMERGENCY;
        }
        return SAFETY_STATE_IDLE;
    }

    uint64_t elapsed = now - ctx->last_heartbeat_ms;

    if (elapsed > (SAFETY_MONITOR_DEADLINE_MS * SAFETY_MONITOR_MISSED_THRESHOLD)) {
        return SAFETY_STATE_EMERGENCY;
    }

    if (elapsed > SAFETY_MONITOR_DEADLINE_MS) {
        return SAFETY_STATE_DEGRADED;
    }

    return SAFETY_STATE_ACTIVE;
}

uint32_t safety_monitor_get_missed_count(const SafetyMonitorContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return 0;
    return ctx->missed_count;
}

SafetyMonitorResult_t safety_monitor_trigger_failsafe(SafetyMonitorContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return SAFETY_MON_ERR_PARAM_NULL;

    ctx->state = SAFETY_STATE_EMERGENCY;
    if (ctx->failsafe_cb) {
        ctx->failsafe_cb();
    }

    return SAFETY_MON_OK;
}
