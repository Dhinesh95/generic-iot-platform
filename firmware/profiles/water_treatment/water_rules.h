/**
 * @file water_rules.h
 * @brief Water Treatment (Industrial) domain rule set.
 *
 * Architecture ref: Section 6, Domain Profile data.
 * Section 10, Water Treatment fail-safe table.
 *
 * This is the highest safety-stakes domain built so far — chemical
 * dosing carries direct risk of harm if handled wrong (not just
 * equipment damage like HVAC/ Agriculture).
 *
 * Contains at minimum:
 * - CRITICAL rule: chlorine dosing safety (SAFETY_LOCKED)
 * - CRITICAL rule: pH safety bounds (SAFETY_LOCKED)
 * - OPERATIONAL rule: tank level refill scheduling
 *
 * SAFETY_LOCKED rules are baked in at build time and cannot be
 * modified via the field config portal.
 */

#ifndef WATER_RULES_H
#define WATER_RULES_H

#include "../../shared/rule_engine_core.h"

/* ---------- Rule IDs ---------- */

#define WATER_RULE_CHLORINE_SAFETY       5001  /**< CRITICAL: stop dosing if chlorine out of safe range. */
#define WATER_RULE_PH_HIGH_SAFETY        5002  /**< CRITICAL: alarm + stop dosing if pH exceeds safe upper bound. */
#define WATER_RULE_PH_LOW_SAFETY         5004  /**< CRITICAL: alarm + stop dosing if pH below safe lower bound. */
#define WATER_RULE_TANK_REFILL           5003  /**< OPERATIONAL: tank level refill scheduling. */

/* ---------- Metric IDs ---------- */

#define WATER_METRIC_CHLORINE_LEVEL      40   /**< Analog: 0-10 ppm (free chlorine). */
#define WATER_METRIC_PH                  41   /**< Analog: 0-14 pH units. */
#define WATER_METRIC_TANK_LEVEL          42   /**< Analog: 0-100% (treated water tank). */
#define WATER_METRIC_FLOW_RATE           43   /**< Analog: 0-50 L/min (treated water output). */

/* ---------- Actuator IDs ---------- */

#define WATER_ACTUATOR_DOSING_VALVE      1    /**< Chlorine dosing solenoid valve. */
#define WATER_ACTUATOR_MAIN_SUPPLY_VALVE 2    /**< Main treated-water supply valve. */
#define WATER_ACTUATOR_CIRCULATION_PUMP  3    /**< Circulation/transfer pump. */

/* ---------- Safety thresholds (engineering defaults, NOT regulatory limits) ---------- */

/**
 * Chlorine dosing safety thresholds.
 *
 * These are ENGINEERING DEFAULTS for the rule engine, not regulatory
 * compliance limits. The specific safe range depends on the treatment
 * process, contact time, water temperature, and local regulations.
 * 4.0 ppm is the EPA Maximum Residual Disinfectant Level (MRDL) for
 * chlorine in drinking water — but this value MUST be validated against
 * the specific deployment's regulatory requirements before field use.
 *
 * TODO: requires water-safety regulatory review per deployment jurisdiction.
 */
#define WATER_CHLORINE_SAFE_MIN_PPM      0.2f   /**< Below this: sensor failure or complete depletion. */
#define WATER_CHLORINE_SAFE_MAX_PPM      4.0f   /**< Above this: over-dose risk (EPA MRDL reference). */

/**
 * pH safety thresholds.
 *
 * EPA Secondary Maximum Contaminant Level: pH 6.5-8.5 for drinking water.
 * These are ENGINEERING DEFAULTS — actual safe range depends on the
 * treatment process and distribution system materials.
 *
 * TODO: requires water-safety regulatory review per deployment jurisdiction.
 */
#define WATER_PH_SAFE_MIN                6.5f   /**< Below this: corrosion risk (acidic water attacks pipes). */
#define WATER_PH_SAFE_MAX                8.5f   /**< Above this: scale formation risk (alkaline water deposits). */

/**
 * Get the Water Treatment rule table.
 *
 * @param out_entries  Output: pointer to the rule array.
 * @param out_count    Output: number of rules.
 */
void water_getRuleTable(const RuleEntry_t **out_entries, uint8_t *out_count);

/**
 * Validate a sensor reading against Water Treatment plausibility bounds.
 *
 * @param reading  The sensor reading to validate.
 * @return true if valid.
 */
bool water_validateSensorReading(const SensorReading_t *reading);

/**
 * Get fail-safe mode for a Water Treatment actuator.
 *
 * @param actuator_id    Which actuator.
 * @param is_power_loss   true for power-loss, false for comms-loss.
 * @return The fail-safe action (ActuatorState_t value).
 */
uint8_t water_getFailSafeMode(uint8_t actuator_id, bool is_power_loss);

/**
 * Execute an action on a Water Treatment actuator.
 *
 * @param actuator_id  Which actuator.
 * @param state        Commanded state.
 * @return true on success.
 */
bool water_executeAction(uint8_t actuator_id, uint8_t state);

/**
 * Get the Water Treatment vtable instance.
 *
 * @return Pointer to the static vtable.
 */
const DomainProfileVTable_t *water_profile_get_vtable(void);

#endif /* WATER_RULES_H */
