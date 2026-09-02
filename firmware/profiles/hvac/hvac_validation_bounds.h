/**
 * @file hvac_validation_bounds.h
 * @brief HVAC sensor plausibility bounds.
 *
 * Architecture ref: Section 9, Bounds (Domain Profile, Data Not Code).
 *
 * HVAC bounds are tighter than Agriculture's outdoor range because
 * indoor environments have smaller temperature/humidity swings.
 */

#ifndef HVAC_VALIDATION_BOUNDS_H
#define HVAC_VALIDATION_BOUNDS_H

#include "../../shared/sensor_validation.h"
#include "hvac_rules.h"

/**
 * Get validation bounds for an HVAC metric.
 *
 * @param metric_id  HVAC_METRIC_* identifier.
 * @return Bounds pointer, or NULL if unknown metric.
 */
const SensorValidationBounds_t *hvac_get_validation_bounds(uint8_t metric_id);

/**
 * Get the history buffer for an HVAC metric.
 *
 * @param metric_id  HVAC_METRIC_* identifier.
 * @return History buffer pointer, or NULL if unknown metric.
 */
SensorHistory_t *hvac_get_metric_history(uint8_t metric_id);

#endif /* HVAC_VALIDATION_BOUNDS_H */
