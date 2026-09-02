/**
 * @file agriculture_rules.h
 * @brief Agriculture domain rule set.
 *
 * Architecture ref: Section 6, Domain Profile data.
 *
 * Contains at minimum:
 * - One CRITICAL rule (irrigation pump dry-run protection)
 * - One OPERATIONAL rule (soil-moisture-triggered irrigation scheduling)
 *
 * SAFETY_LOCKED rules are baked in at build time and cannot be
 * modified via the field config portal.
 */

#ifndef AGRICULTURE_RULES_H
#define AGRICULTURE_RULES_H

#include "../../shared/rule_engine_core.h"

/* ---------- Rule IDs ---------- */

#define AGRI_RULE_PUMP_DRY_RUN_CRITICAL     3001  /**< CRITICAL: stop pump if no water. */
#define AGRI_RULE_IRRIGATION_OPERATIONAL     3002  /**< OPERATIONAL: soil moisture irrigation. */
#define AGRI_RULE_FROST_PROTECT_OPERATIONAL  3003  /**< OPERATIONAL: frost protection. */

/* ---------- Metric IDs ---------- */

#define AGRI_METRIC_SOIL_MOISTURE       20   /**< Analog: 0-100%. */
#define AGRI_METRIC_AMBIENT_TEMP        21   /**< Analog: -40 to +60 C. */
#define AGRI_METRIC_AMBIENT_HUMIDITY    22   /**< Analog: 0-100%. */
#define AGRI_METRIC_WATER_LEVEL         23   /**< Analog: 0-100% (reservoir level). */
#define AGRI_METRIC_FLOW_RATE           24   /**< Analog: 0-50 L/min. */

/* ---------- Actuator IDs ---------- */

#define AGRI_ACTUATOR_IRRIGATION_PUMP   1
#define AGRI_ACTUATOR_IRRIGATION_VALVE  2
#define AGRI_ACTUATOR_FROST_VALVE       3

/**
 * Get the Agriculture rule table.
 *
 * @param out_entries  Output: pointer to the rule array.
 * @param out_count    Output: number of rules.
 */
void agriculture_getRuleTable(const RuleEntry_t **out_entries, uint8_t *out_count);

/**
 * Validate a sensor reading against Agriculture plausibility bounds.
 *
 * @param reading  The sensor reading to validate.
 * @return true if valid.
 */
bool agriculture_validateSensorReading(const SensorReading_t *reading);

/**
 * Get fail-safe mode for an Agriculture actuator.
 *
 * @param actuator_id    Which actuator.
 * @param is_power_loss   true for power-loss, false for comms-loss.
 * @return The fail-safe action (ActuatorState_t value).
 */
uint8_t agriculture_getFailSafeMode(uint8_t actuator_id, bool is_power_loss);

/**
 * Execute an action on an Agriculture actuator.
 *
 * @param actuator_id  Which actuator.
 * @param state        Commanded state.
 * @return true on success.
 */
bool agriculture_executeAction(uint8_t actuator_id, uint8_t state);

/**
 * Get the Agriculture vtable instance.
 *
 * @return Pointer to the static vtable.
 */
const DomainProfileVTable_t *agriculture_profile_get_vtable(void);

#endif /* AGRICULTURE_RULES_H */
