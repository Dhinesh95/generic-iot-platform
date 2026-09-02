/**
 * @file home_failsafe.h
 * @brief Home/Building actuator fail-safe decision table.
 *
 * Architecture ref: Section 10, Home/Building table.
 *
 * Includes electronic door lock (with compliance TODO),
 * irrigation valve, and light relay.
 */

#ifndef HOME_FAILSAFE_H
#define HOME_FAILSAFE_H

#include "../../shared/actuator_failsafe.h"

/**
 * Register the Home/Building fail-safe table with the actuator
 * fail-safe subsystem.
 *
 * @return true on success.
 */
bool home_failsafe_register_all(void);

/**
 * Get the comms-loss timeout for a specific actuator.
 *
 * @param actuator_id  The actuator.
 * @return Timeout in seconds.
 */
uint16_t home_get_comms_timeout(uint8_t actuator_id);

#endif /* HOME_FAILSAFE_H */
