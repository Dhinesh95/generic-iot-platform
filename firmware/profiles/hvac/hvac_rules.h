/**
 * @file hvac_rules.h
 * @brief HVAC domain rule set.
 *
 * Architecture ref: Section 6, Domain Profile data.
 * Section 10, HVAC fail-safe table.
 *
 * Contains at minimum:
 * - One CRITICAL rule (compressor short-cycle protection)
 * - One OPERATIONAL rule (comfort setpoint scheduling)
 *
 * SAFETY_LOCKED rules are baked in at build time and cannot be
 * modified via the field config portal.
 */

#ifndef HVAC_RULES_H
#define HVAC_RULES_H

#include "../../shared/rule_engine_core.h"

/* ---------- Rule IDs ---------- */

#define HVAC_RULE_COMPRESSOR_POST_RUN_LOCKOUT  4001  /**< CRITICAL: shut down compressor when supply temp indicates residual heat from recent run. */
#define HVAC_RULE_COMFORT_SETPOINT              4002  /**< OPERATIONAL: comfort setpoint scheduling. */
#define HVAC_RULE_OVERTEMP_PROTECT              4003  /**< CRITICAL: high-temperature protection (fire/equipment risk). */

/* Short-cycle protection constants — stateful, time-based, NOT a rule table entry. */
#define HVAC_MIN_COMPRESSOR_OFF_SEC             300   /**< 5 minutes minimum off-time before restart. */
#define HVAC_CLOCK_UNRELIABLE_FALLBACK_SEC      120   /**< 2-minute conservative delay when clock is unreliable (power brownout/reboot). */
#define HVAC_SHORT_CYCLE_ACTUATOR               HVAC_ACTUATOR_COMPRESSOR_RELAY

/**
 * Check whether the compressor is allowed to restart based on short-cycle
 * protection (minimum off-time since last shutdown).
 *
 * @param current_time_sec  Current time in seconds (from system clock or simulated).
 * @return true if restart is permitted, false if still in lockout window.
 */
bool hvac_short_cycle_check(uint32_t current_time_sec);

/**
 * Record that the compressor has been turned off (for short-cycle tracking).
 *
 * @param current_time_sec  Current time in seconds.
 */
void hvac_short_cycle_record_off(uint32_t current_time_sec);

/**
 * Reset short-cycle state (e.g. on init or manual override).
 */
void hvac_short_cycle_reset(void);

/**
 * Get the minimum off-time constant for testing/inspection.
 */
uint32_t hvac_short_cycle_get_min_off_sec(void);

/**
 * Get the last compressor-off timestamp (0 = never tracked). For testing.
 */
uint32_t hvac_short_cycle_get_last_off(void);

/**
 * Get the clock-unreliable fallback delay constant. For testing.
 */
uint32_t hvac_clock_unreliable_fallback_sec(void);

/* ---------- Metric IDs ---------- */

#define HVAC_METRIC_SUPPLY_TEMP        30   /**< Analog: 0-60 C (supply air temperature). */
#define HVAC_METRIC_RETURN_TEMP        31   /**< Analog: 0-60 C (return air temperature). */
#define HVAC_METRIC_INDOOR_HUMIDITY    32   /**< Analog: 0-100%. */
#define HVAC_METRIC_AMBIENT_TEMP       33   /**< Analog: -10 to +50 C (indoor). */

/* ---------- Actuator IDs ---------- */

#define HVAC_ACTUATOR_COMPRESSOR_RELAY  1
#define HVAC_ACTUATOR_DAMPER_MOTOR      2
#define HVAC_ACTUATOR_FAN_MOTOR         3

/**
 * Get the HVAC rule table.
 *
 * @param out_entries  Output: pointer to the rule array.
 * @param out_count    Output: number of rules.
 */
void hvac_getRuleTable(const RuleEntry_t **out_entries, uint8_t *out_count);

/**
 * Validate a sensor reading against HVAC plausibility bounds.
 *
 * @param reading  The sensor reading to validate.
 * @return true if valid.
 */
bool hvac_validateSensorReading(const SensorReading_t *reading);

/**
 * Get fail-safe mode for an HVAC actuator.
 *
 * @param actuator_id    Which actuator.
 * @param is_power_loss   true for power-loss, false for comms-loss.
 * @return The fail-safe action (ActuatorState_t value).
 */
uint8_t hvac_getFailSafeMode(uint8_t actuator_id, bool is_power_loss);

/**
 * Execute an action on an HVAC actuator.
 *
 * @param actuator_id  Which actuator.
 * @param state        Commanded state.
 * @return true on success.
 */
bool hvac_executeAction(uint8_t actuator_id, uint8_t state);

/**
 * Get the HVAC vtable instance.
 *
 * @return Pointer to the static vtable.
 */
const DomainProfileVTable_t *hvac_profile_get_vtable(void);

#endif /* HVAC_RULES_H */
