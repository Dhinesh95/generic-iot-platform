/**
 * @file agriculture_failsafe.h
 * @brief Agriculture actuator fail-safe decision table.
 *
 * Architecture ref: Section 10, Agriculture table.
 *
 * Includes irrigation pump, irrigation valve, and frost protection valve.
 */

#ifndef AGRICULTURE_FAILSAFE_H
#define AGRICULTURE_FAILSAFE_H

#include "../../shared/actuator_failsafe.h"

/**
 * Register the Agriculture fail-safe table with the actuator
 * fail-safe subsystem.
 *
 * @return true on success.
 */
bool agriculture_failsafe_register_all(void);

/**
 * Get the comms-loss timeout for a specific actuator.
 *
 * @param actuator_id  The actuator.
 * @return Timeout in seconds.
 */
uint16_t agriculture_get_comms_timeout(uint8_t actuator_id);

#endif /* AGRICULTURE_FAILSAFE_H */
