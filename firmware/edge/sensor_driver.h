/**
 * @file sensor_driver.h
 * @brief Edge Node sensor driver registry — static, no heap allocation.
 *
 * Architecture ref: Section 8 (Edge Node Hardware / Firmware Detail).
 *
 * The driver registry is a static array of SensorDriver_t entries.
 * Build-time selection: only drivers relevant to the active domain profile
 * are compiled in (via #ifdef PROFILE_X in the .c file).
 * Field-time selection: which driver is active on a given node is determined
 * by the UniversalConfigRecord (edge_config.h), not by compile-time flags.
 *
 * Design rule: only one sensor driver's working memory is resident in RAM
 * at a time per node — full field flexibility at zero extra RAM cost.
 */

#ifndef SENSOR_DRIVER_H
#define SENSOR_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Types ---------- */

/**
 * Sensor type identifiers.
 * Each concrete driver registers one of these.
 */
typedef enum {
    SENSOR_DISTANCE = 0,    /**< Ultrasonic / analog distance sensor. */
    SENSOR_ANALOG   = 1,    /**< Generic analog (ADC) sensor. */
    SENSOR_I2C      = 2,    /**< I2C temperature / humidity sensor. */
    SENSOR_TYPE_COUNT
} SensorType_t;

/**
 * Sensor configuration — per-node, loaded from UniversalConfigRecord.
 * Each driver type interprets this differently (pin numbers, scale
 * factors, I2C addresses, etc.).
 */
typedef struct {
    uint8_t  pin_or_addr;   /**< GPIO pin (analog/distance) or I2C address. */
    float    scale_factor;  /**< Raw-to-physical scale factor. */
    float    offset;        /**< Raw-to-physical offset. */
    uint8_t  sample_count;  /**< Number of ADC samples to average (analog). */
} SensorConfig_t;

/**
 * A sensor reading produced by a driver.
 * This is the Edge Node's output, encoded into an RS-485 frame
 * and sent to the Gateway.
 */
typedef struct {
    float    value;         /**< Processed sensor value (physical units). */
    bool     valid;         /**< false if the driver detected a hardware fault. */
} SensorDriverReading_t;

/**
 * Sensor driver interface — static function pointers, no dynamic dispatch.
 * Each concrete driver fills in these four functions.
 */
typedef struct {
    SensorType_t type;                          /**< Driver identifier. */
    bool         (*init)(SensorConfig_t *cfg);  /**< Initialise hardware. */
    SensorDriverReading_t (*read)(void);        /**< Read sensor. */
    bool         (*validate)(float raw_value);  /**< Driver-level sanity check. */
} SensorDriver_t;

/* ---------- Registry API ---------- */

/**
 * Return the number of drivers compiled into the registry.
 * This is determined at build time by which #ifdef PROFILE_X blocks
 * are active.
 *
 * @return Number of entries in the driver registry.
 */
uint8_t sensor_driver_registry_count(void);

/**
 * Look up a driver by type.
 *
 * @param type  The sensor type to find.
 * @return Pointer to the driver entry, or NULL if not found.
 */
const SensorDriver_t *sensor_driver_find(SensorType_t type);

/**
 * Return a pointer to the full driver registry array (for iteration).
 *
 * @param count  Output: number of entries in the array.
 * @return Pointer to the first entry.
 */
const SensorDriver_t *sensor_driver_registry_get_all(uint8_t *count);

/* ---------- Mock hardware setters (for tests) ---------- */

/**
 * Set the mock distance sensor value.
 * @param cm     Distance in centimeters.
 * @param fault  If true, next read() reports a hardware fault.
 */
void sensor_driver_mock_set_distance(float cm, bool fault);

/**
 * Set the mock ADC raw value.
 * @param raw    Raw ADC reading (0–4095 for 12-bit).
 * @param fault  If true, next read() reports a hardware fault.
 */
void sensor_driver_mock_set_adc(float raw, bool fault);

/**
 * Set the mock I2C sensor values.
 * @param temp_c    Temperature in °C.
 * @param humidity  Relative humidity in %.
 * @param fault     If true, next read() reports a hardware fault.
 */
void sensor_driver_mock_set_i2c(float temp_c, float humidity, bool fault);

#endif /* SENSOR_DRIVER_H */
