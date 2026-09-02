/**
 * @file power_state.h
 * @brief Power state management — Edge Node battery-aware state machine.
 *
 * Architecture ref: Phase 25 (Power State Management).
 *
 * Manages sleep/wake states based on battery voltage thresholds.
 * The Edge Node (PY32) runs on battery, so power management is
 * critical for field longevity.
 *
 * State machine:
 *   ACTIVE → IDLE → SLEEP → EMERGENCY → HARD SHUTDOWN
 */

#ifndef POWER_STATE_H
#define POWER_STATE_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Constants ---------- */

#define POWER_LOW_BATTERY_MV        3200   /* 3.2V — enter IDLE */
#define POWER_CRITICAL_BATTERY_MV   2800   /* 2.8V — enter SLEEP */
#define POWER_EMERGENCY_BATTERY_MV  2500   /* 2.5V — EMERGENCY (safety-only) */
#define POWER_SHUTDOWN_BATTERY_MV   2300   /* 2.3V — HARD SHUTDOWN */

/* ---------- Types ---------- */

typedef enum {
    POWER_STATE_ACTIVE = 0,     /* Full operation, all sensors polling */
    POWER_STATE_IDLE,           /* Reduced sampling, radio off */
    POWER_STATE_SLEEP,          /* CPU halted, RTC running */
    POWER_STATE_EMERGENCY,      /* Safety-only operation */
    POWER_STATE_SHUTDOWN        /* Hard shutdown */
} PowerState_t;

typedef enum {
    POWER_TRANSITION_OK,
    POWER_TRANSITION_ERR_PARAM_NULL,
    POWER_TRANSITION_ERR_INVALID
} PowerTransitionResult_t;

/**
 * Power state configuration (thresholds).
 */
typedef struct {
    uint32_t low_battery_mv;
    uint32_t critical_battery_mv;
    uint32_t emergency_battery_mv;
    uint32_t shutdown_battery_mv;
} PowerStateConfig_t;

/**
 * Power state context.
 */
typedef struct {
    PowerState_t         current;
    PowerStateConfig_t   config;
    uint32_t             battery_mv;
    bool                 wake_on_radio;
    bool                 wake_on_timer;
    bool                 initialized;
} PowerStateContext_t;

/* ---------- API ---------- */

/**
 * Initialize with default thresholds.
 */
PowerTransitionResult_t power_state_init(PowerStateContext_t *ctx);

/**
 * Initialize with custom thresholds.
 */
PowerTransitionResult_t power_state_init_custom(PowerStateContext_t *ctx,
                                                  const PowerStateConfig_t *config);

/**
 * Update battery reading and evaluate state transition.
 *
 * @param ctx          Context.
 * @param battery_mv   Current battery voltage in mV.
 * @return New power state after evaluation.
 */
PowerState_t power_state_evaluate(PowerStateContext_t *ctx, uint32_t battery_mv);

/**
 * Get current power state.
 */
PowerState_t power_state_get_current(const PowerStateContext_t *ctx);

/**
 * Check if sensors should be active in the current state.
 */
bool power_state_sensors_active(const PowerStateContext_t *ctx);

/**
 * Check if radio should be active in the current state.
 */
bool power_state_radio_active(const PowerStateContext_t *ctx);

/**
 * Check if cloud sync should be active in the current state.
 */
bool power_state_cloud_active(const PowerStateContext_t *ctx);

/**
 * Check if safety rules should still operate (always true except SHUTDOWN).
 */
bool power_state_safety_active(const PowerStateContext_t *ctx);

/**
 * Check if the system should shut down.
 */
bool power_state_should_shutdown(const PowerStateContext_t *ctx);

#endif /* POWER_STATE_H */
