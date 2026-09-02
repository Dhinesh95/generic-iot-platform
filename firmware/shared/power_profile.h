/**
 * @file power_profile.h
 * @brief Power profile mechanism — domain-agnostic HAL configuration.
 *
 * Architecture ref: Section 8: "Power variants (mains/battery/solar):
 * handled as a firmware-level PowerProfile_t HAL configuration
 * (active/sleep duration, low-battery threshold), not a hardware or
 * MCU change — same pattern style as the domain-profile vtable."
 *
 * This is a CORE mechanism, not Agriculture-specific. Any battery or
 * solar-powered domain uses this. Mains-powered devices use a
 * PowerProfile_t with infinite active duration and no sleep.
 */

#ifndef POWER_PROFILE_H
#define POWER_PROFILE_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Constants ---------- */

#define POWER_PROFILE_MAX_PROFILES  8  /**< Maximum registered power profiles. */

/* ---------- Types ---------- */

/**
 * Power source type.
 */
typedef enum {
    POWER_SOURCE_MAINS    = 0,  /**< Continuous mains power. */
    POWER_SOURCE_BATTERY  = 1,  /**< Battery-powered (primary). */
    POWER_SOURCE_SOLAR    = 2   /**< Solar-powered with battery backup. */
} PowerSource_t;

/**
 * Power profile configuration.
 * Defines the power management behavior for a device.
 */
typedef struct {
    PowerSource_t source;            /**< Power source type. */
    uint32_t active_duration_ms;     /**< How long to stay active per cycle. 0 = indefinite (mains). */
    uint32_t sleep_duration_ms;      /**< How long to sleep between active cycles. 0 = no sleep (mains). */
    uint16_t low_battery_mv;         /**< Low-battery threshold in millivolts. 0 = no monitoring. */
    uint16_t critical_battery_mv;    /**< Critical battery threshold — force deep sleep. 0 = none. */
    bool     deep_sleep_enabled;     /**< true if deep sleep is available (battery/solar). */
    bool     telemetry_duty_cycle;   /**< true if telemetry should be throttled during low battery. */
} PowerProfile_t;

/**
 * Runtime power state.
 */
typedef struct {
    uint64_t last_wake_ms;           /**< Timestamp of last wake event. */
    uint64_t last_sleep_ms;          /**< Timestamp of last sleep entry. */
    uint16_t battery_mv;             /**< Current battery voltage (from ADC). 0 = not monitored. */
    uint8_t  cycle_count;            /**< Number of wake/sleep cycles since boot. */
    bool     low_battery;            /**< true if battery is below low_battery_mv. */
    bool     critical_battery;       /**< true if battery is below critical_battery_mv. */
} PowerState_t;

/**
 * Result of power profile evaluation.
 */
typedef enum {
    POWER_ACTION_STAY_ACTIVE,        /**< Continue normal operation. */
    POWER_ACTION_THROTTLE,           /**< Reduce telemetry frequency. */
    POWER_ACTION_ENTER_SLEEP,        /**< Enter sleep mode. */
    POWER_ACTION_ENTER_DEEP_SLEEP,   /**< Enter deep sleep (requires RTC wake). */
    POWER_ACTION_SHUTDOWN            /**< Graceful shutdown (critical battery). */
} PowerAction_t;

/* ---------- API ---------- */

/**
 * Initialise the power profile subsystem.
 *
 * @return true on success.
 */
bool power_profile_init(void);

/**
 * Register a power profile.
 *
 * @param profile  The power profile to register.
 * @return true on success.
 */
bool power_profile_register(const PowerProfile_t *profile);

/**
 * Get a registered power profile by index.
 *
 * @param index  Profile index (0-based).
 * @return Pointer to profile, or NULL if not found.
 */
const PowerProfile_t *power_profile_get(uint8_t index);

/**
 * Update the runtime power state (call periodically from main loop).
 * Uses the active profile's low_battery_mv and critical_battery_mv
 * thresholds — not hardcoded constants.
 *
 * @param state        Current power state.
 * @param profile      Active power profile (provides voltage thresholds).
 * @param current_ms   Current monotonic timestamp.
 * @param battery_mv   Current battery voltage (0 if not monitored).
 * @return Recommended power action.
 */
PowerAction_t power_profile_evaluate(
    PowerState_t *state,
    const PowerProfile_t *profile,
    uint64_t current_ms,
    uint16_t battery_mv
);

/**
 * Get the number of registered profiles.
 *
 * @return Count of registered profiles.
 */
uint8_t power_profile_count(void);

/**
 * Create a mains power profile (infinite active, no sleep).
 *
 * @return A pre-configured mains power profile.
 */
PowerProfile_t power_profile_create_mains(void);

/**
 * Create a battery power profile.
 *
 * @param active_ms      Active duration per cycle.
 * @param sleep_ms       Sleep duration between cycles.
 * @param low_battery_mv Low-battery threshold.
 * @return A pre-configured battery power profile.
 */
PowerProfile_t power_profile_create_battery(
    uint32_t active_ms, uint32_t sleep_ms, uint16_t low_battery_mv
);

/**
 * Create a solar power profile.
 *
 * @param active_ms      Active duration per cycle.
 * @param sleep_ms       Sleep duration between cycles.
 * @param low_battery_mv Low-battery threshold.
 * @return A pre-configured solar power profile.
 */
PowerProfile_t power_profile_create_solar(
    uint32_t active_ms, uint32_t sleep_ms, uint16_t low_battery_mv
);

#endif /* POWER_PROFILE_H */
