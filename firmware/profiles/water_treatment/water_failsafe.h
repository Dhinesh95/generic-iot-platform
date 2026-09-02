/**
 * @file water_failsafe.h
 * @brief Water Treatment actuator fail-safe registration.
 *
 * Architecture ref: Section 10, Water Treatment fail-safe table.
 */

#ifndef WATER_FAILSAFE_H
#define WATER_FAILSAFE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Register all Water Treatment actuators in the fail-safe table.
 *
 * @return true if all registrations succeeded.
 */
bool water_failsafe_register_all(void);

/**
 * Get comms-loss timeout for a Water Treatment actuator.
 *
 * @param actuator_id  Which actuator.
 * @return Timeout in seconds (0 if actuator not found).
 */
uint16_t water_get_comms_timeout(uint8_t actuator_id);

#endif /* WATER_FAILSAFE_H */
