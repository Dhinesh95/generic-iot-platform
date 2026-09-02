/**
 * @file power_state.c
 * @brief Power state management implementation.
 */

#include "power_state.h"
#include <string.h>

/* ---------- API ---------- */

PowerTransitionResult_t power_state_init(PowerStateContext_t *ctx)
{
    PowerStateConfig_t defaults = {
        .low_battery_mv = POWER_LOW_BATTERY_MV,
        .critical_battery_mv = POWER_CRITICAL_BATTERY_MV,
        .emergency_battery_mv = POWER_EMERGENCY_BATTERY_MV,
        .shutdown_battery_mv = POWER_SHUTDOWN_BATTERY_MV
    };
    return power_state_init_custom(ctx, &defaults);
}

PowerTransitionResult_t power_state_init_custom(PowerStateContext_t *ctx,
                                                  const PowerStateConfig_t *config)
{
    if (!ctx || !config) return POWER_TRANSITION_ERR_PARAM_NULL;

    memset(ctx, 0, sizeof(*ctx));
    ctx->config = *config;
    ctx->current = POWER_STATE_ACTIVE;
    ctx->initialized = true;
    return POWER_TRANSITION_OK;
}

PowerState_t power_state_evaluate(PowerStateContext_t *ctx, uint32_t battery_mv)
{
    if (!ctx || !ctx->initialized) return POWER_STATE_SHUTDOWN;

    ctx->battery_mv = battery_mv;

    /* Evaluate transitions (can only move DOWN to worse state, not up). */
    if (battery_mv <= ctx->config.shutdown_battery_mv) {
        ctx->current = POWER_STATE_SHUTDOWN;
    } else if (battery_mv <= ctx->config.emergency_battery_mv) {
        if (ctx->current != POWER_STATE_SHUTDOWN) {
            ctx->current = POWER_STATE_EMERGENCY;
        }
    } else if (battery_mv <= ctx->config.critical_battery_mv) {
        if (ctx->current == POWER_STATE_ACTIVE ||
            ctx->current == POWER_STATE_IDLE) {
            ctx->current = POWER_STATE_SLEEP;
        }
    } else if (battery_mv <= ctx->config.low_battery_mv) {
        if (ctx->current == POWER_STATE_ACTIVE) {
            ctx->current = POWER_STATE_IDLE;
        }
    }
    /* If battery recovers (e.g. charging), stay in current state
     * — explicit wake-up required via power_state_wake()). */

    return ctx->current;
}

PowerState_t power_state_get_current(const PowerStateContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return POWER_STATE_SHUTDOWN;
    return ctx->current;
}

bool power_state_sensors_active(const PowerStateContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return false;
    return ctx->current == POWER_STATE_ACTIVE;
}

bool power_state_radio_active(const PowerStateContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return false;
    return ctx->current == POWER_STATE_ACTIVE;
}

bool power_state_cloud_active(const PowerStateContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return false;
    return ctx->current == POWER_STATE_ACTIVE ||
           ctx->current == POWER_STATE_IDLE;
}

bool power_state_safety_active(const PowerStateContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return false;
    return ctx->current != POWER_STATE_SHUTDOWN;
}

bool power_state_should_shutdown(const PowerStateContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return true;
    return ctx->current == POWER_STATE_SHUTDOWN;
}
