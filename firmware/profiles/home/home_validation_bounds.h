/**
 * @file home_validation_bounds.h
 * @brief Home/Building sensor plausibility bounds.
 *
 * Architecture ref: Section 9, Bounds (Domain Profile, Data Not Code).
 *
 * Contains plausibility bounds for:
 * - Door/window contact sensor (binary)
 * - Ambient light level sensor (analog)
 * - Soil moisture sensor (analog)
 */

#ifndef HOME_VALIDATION_BOUNDS_H
#define HOME_VALIDATION_BOUNDS_H

#include "../../shared/sensor_validation.h"
#include "home_rules.h"

/**
 * Get the validation bounds for a Home/Building metric.
 *
 * @param metric_id  The metric to get bounds for.
 * @return Pointer to bounds, or NULL if metric is unknown.
 */
const SensorValidationBounds_t *home_get_validation_bounds(uint8_t metric_id);

/**
 * Get the history buffer for a Home/Building metric.
 *
 * @param metric_id  The metric to get history for.
 * @return Pointer to history buffer, or NULL if metric is unknown.
 */
SensorHistory_t *home_get_metric_history(uint8_t metric_id);

#endif /* HOME_VALIDATION_BOUNDS_H */
