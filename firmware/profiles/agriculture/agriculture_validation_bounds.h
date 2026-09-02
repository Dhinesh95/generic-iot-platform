/**
 * @file agriculture_validation_bounds.h
 * @brief Agriculture sensor plausibility bounds.
 *
 * Architecture ref: Section 9, Bounds (Domain Profile, Data Not Code).
 *
 * Contains plausibility bounds for:
 * - Soil moisture sensor (analog)
 * - Ambient temperature sensor (analog)
 * - Ambient humidity sensor (analog)
 * - Water level sensor (analog)
 */

#ifndef AGRICULTURE_VALIDATION_BOUNDS_H
#define AGRICULTURE_VALIDATION_BOUNDS_H

#include "../../shared/sensor_validation.h"
#include "agriculture_rules.h"

/**
 * Get the validation bounds for an Agriculture metric.
 *
 * @param metric_id  The metric to get bounds for.
 * @return Pointer to bounds, or NULL if metric is unknown.
 */
const SensorValidationBounds_t *agriculture_get_validation_bounds(uint8_t metric_id);

/**
 * Get the history buffer for an Agriculture metric.
 *
 * @param metric_id  The metric to get history for.
 * @return Pointer to history buffer, or NULL if metric is unknown.
 */
SensorHistory_t *agriculture_get_metric_history(uint8_t metric_id);

#endif /* AGRICULTURE_VALIDATION_BOUNDS_H */
