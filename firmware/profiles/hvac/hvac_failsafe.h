/**
 * @file hvac_failsafe.h
 * @brief HVAC actuator fail-safe registration.
 *
 * Architecture ref: Section 10, HVAC fail-safe table.
 */

#ifndef HVAC_FAILSAFE_H
#define HVAC_FAILSAFE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Register all HVAC actuators in the fail-safe table.
 *
 * @return true if all registrations succeeded.
 */
bool hvac_failsafe_register_all(void);

/**
 * Get comms-loss timeout for an HVAC actuator.
 *
 * @param actuator_id  Which actuator.
 * @return Timeout in seconds (0 if actuator not found).
 */
uint16_t hvac_get_comms_timeout(uint8_t actuator_id);

#endif /* HVAC_FAILSAFE_H */
