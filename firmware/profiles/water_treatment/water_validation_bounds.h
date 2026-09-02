/**
 * @file water_validation_bounds.h
 * @brief Water Treatment sensor plausibility bounds.
 *
 * Architecture ref: Section 9, Bounds (Domain Profile, Data Not Code).
 *
 * Contains plausibility bounds for:
 * - Chlorine level sensor (analog: 0-10 ppm)
 * - pH sensor (analog: 0-14)
 * - Tank level sensor (analog: 0-100%)
 * - Flow rate sensor (analog: 0-50 L/min)
 */

#ifndef WATER_VALIDATION_BOUNDS_H
#define WATER_VALIDATION_BOUNDS_H

#include "../../shared/sensor_validation.h"
#include "water_rules.h"

/**
 * Get validation bounds for a Water Treatment metric.
 *
 * @param metric_id  WATER_METRIC_* identifier.
 * @return Bounds pointer, or NULL if unknown metric.
 */
const SensorValidationBounds_t *water_get_validation_bounds(uint8_t metric_id);

/**
 * Get the history buffer for a Water Treatment metric.
 *
 * @param metric_id  WATER_METRIC_* identifier.
 * @return History buffer pointer, or NULL if unknown metric.
 */
SensorHistory_t *water_get_metric_history(uint8_t metric_id);

#endif /* WATER_VALIDATION_BOUNDS_H */
