/**
 * @file home_rules.h
 * @brief Home/Building domain rule set.
 *
 * Architecture ref: Section 6, Domain Profile data.
 *
 * Contains at minimum:
 * - One CRITICAL rule (door lock control tied to security state)
 * - One OPERATIONAL rule (irrigation/lighting schedule)
 *
 * SAFETY_LOCKED rules are baked in at build time and cannot be
 * modified via the field config portal.
 */

#ifndef HOME_RULES_H
#define HOME_RULES_H

#include "../../shared/rule_engine_core.h"

/* ---------- Rule IDs ---------- */

#define HOME_RULE_DOOR_LOCK_CRITICAL    1001  /**< CRITICAL: door lock ↔ security state. */
#define HOME_RULE_IRRIGATION_OPERATIONAL 2001  /**< OPERATIONAL: irrigation schedule. */
#define HOME_RULE_LIGHTING_OPERATIONAL   2002  /**< OPERATIONAL: lighting schedule. */

/* ---------- Metric IDs ---------- */

#define HOME_METRIC_SECURITY_STATE      10   /**< Binary: 0=disarmed, 1=armed. */
#define HOME_METRIC_DOOR_CONTACT        11   /**< Binary: 0=closed, 1=open. */
#define HOME_METRIC_LIGHT_LEVEL         12   /**< Analog: 0-1000 lux. */
#define HOME_METRIC_SOIL_MOISTURE       13   /**< Analog: 0-100%. */

/* ---------- Actuator IDs ---------- */

#define HOME_ACTUATOR_DOOR_LOCK         1
#define HOME_ACTUATOR_IRRIGATION_VALVE  2
#define HOME_ACTUATOR_LIGHT_RELAY       3

/**
 * Get the Home/Building rule table.
 * Used to populate the DomainProfileVTable_t.
 *
 * @param out_entries  Output: pointer to the rule array.
 * @param out_count    Output: number of rules.
 */
void home_getRuleTable(const RuleEntry_t **out_entries, uint8_t *out_count);

/**
 * Validate a sensor reading against Home/Building plausibility bounds.
 *
 * @param reading  The sensor reading to validate.
 * @return true if valid.
 */
bool home_validateSensorReading(const SensorReading_t *reading);

/**
 * Get fail-safe mode for a Home/Building actuator.
 *
 * @param actuator_id    Which actuator.
 * @param is_power_loss   true for power-loss, false for comms-loss.
 * @return The fail-safe action (ActuatorState_t value).
 */
uint8_t home_getFailSafeMode(uint8_t actuator_id, bool is_power_loss);

/**
 * Execute an action on a Home/Building actuator.
 *
 * @param actuator_id  Which actuator.
 * @param state        Commanded state.
 * @return true on success.
 */
bool home_executeAction(uint8_t actuator_id, uint8_t state);

/**
 * Get the Home/Building vtable instance.
 *
 * @return Pointer to the static vtable.
 */
const DomainProfileVTable_t *home_profile_get_vtable(void);

#endif /* HOME_RULES_H */
