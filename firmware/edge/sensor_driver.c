/**
 * @file sensor_driver.c
 * @brief Edge Node sensor driver registry implementation.
 *
 * Architecture ref: Section 8.
 *
 * Three concrete driver types with realistic mock hardware I/O:
 * - SENSOR_DISTANCE: simulated ultrasonic/analog distance measurement
 * - SENSOR_ANALOG:   simulated ADC reading (generic analog sensor)
 * - SENSOR_I2C:      simulated I2C temperature/humidity sensor
 *
 * Each driver is domain-agnostic: a "distance sensor" doesn't know if it's
 * measuring water tank level or soil depth — that meaning is assigned by
 * the config, not the driver.
 *
 * Mock hardware: all drivers use global mock state (settable from tests)
 * to simulate sensor readings. In production, these would be replaced
 * with real HAL calls (ADC read, I2C transaction, etc.).
 */

#include "sensor_driver.h"
#include <string.h>

/* ---------- Mock hardware state (test-injectable) ---------- */

static float    g_mock_distance_cm     = 100.0f;
static bool     g_mock_distance_fault  = false;

static float    g_mock_adc_raw         = 512.0f;
static bool     g_mock_adc_fault       = false;

static float    g_mock_i2c_temp_c      = 22.5f;
static float    g_mock_i2c_humidity    = 45.0f;
static bool     g_mock_i2c_fault       = false;

/* ---------- Mock setters (for tests) ---------- */

void sensor_driver_mock_set_distance(float cm, bool fault)
{
    g_mock_distance_cm = cm;
    g_mock_distance_fault = fault;
}

void sensor_driver_mock_set_adc(float raw, bool fault)
{
    g_mock_adc_raw = raw;
    g_mock_adc_fault = fault;
}

void sensor_driver_mock_set_i2c(float temp_c, float humidity, bool fault)
{
    g_mock_i2c_temp_c = temp_c;
    g_mock_i2c_humidity = humidity;
    g_mock_i2c_fault = fault;
}

/* ---------- SENSOR_DISTANCE driver ---------- */

static bool distance_init(SensorConfig_t *cfg)
{
    (void)cfg;
    /* In production: configure GPIO for ultrasonic trigger/echo,
     * set up timer for pulse-width measurement. */
    return true;
}

static SensorDriverReading_t distance_read(void)
{
    SensorDriverReading_t r;
    r.valid  = !g_mock_distance_fault;
    r.value  = g_mock_distance_cm;
    return r;
}

static bool distance_validate(float raw_value)
{
    /* Ultrasonic sensors typically operate 2cm – 400cm. */
    return (raw_value >= 2.0f && raw_value <= 400.0f);
}

static const SensorDriver_t s_distance_driver = {
    .type   = SENSOR_DISTANCE,
    .init   = distance_init,
    .read   = distance_read,
    .validate = distance_validate
};

/* ---------- SENSOR_ANALOG driver ---------- */

static bool analog_init(SensorConfig_t *cfg)
{
    (void)cfg;
    /* In production: configure ADC channel, resolution, sample count. */
    return true;
}

static SensorDriverReading_t analog_read(void)
{
    SensorDriverReading_t r;
    r.valid  = !g_mock_adc_fault;
    /* Apply scale factor and offset from config — but we don't have
     * config access in read(). The scale is applied by the caller
     * (edge_node) after reading. This is raw ADC value. */
    r.value  = g_mock_adc_raw;
    return r;
}

static bool analog_validate(float raw_value)
{
    /* 12-bit ADC: 0 – 4095. */
    return (raw_value >= 0.0f && raw_value <= 4095.0f);
}

static const SensorDriver_t s_analog_driver = {
    .type   = SENSOR_ANALOG,
    .init   = analog_init,
    .read   = analog_read,
    .validate = analog_validate
};

/* ---------- SENSOR_I2C driver ---------- */

static bool i2c_init(SensorConfig_t *cfg)
{
    (void)cfg;
    /* In production: configure I2C bus, address, measurement mode. */
    return true;
}

static SensorDriverReading_t i2c_read(void)
{
    SensorDriverReading_t r;
    r.valid  = !g_mock_i2c_fault;
    /* Returns temperature; humidity is a separate metric.
     * In production, a single I2C transaction reads both.
     * For the driver interface (single-value return), we return
     * temperature. Humidity would be a separate read call or
     * a second metric_id mapping to the same driver. */
    r.value  = g_mock_i2c_temp_c;
    return r;
}

static bool i2c_validate(float raw_value)
{
    /* Temperature sensor: -40°C to +125°C (typical I2C temp range). */
    return (raw_value >= -40.0f && raw_value <= 125.0f);
}

static const SensorDriver_t s_i2c_driver = {
    .type   = SENSOR_I2C,
    .init   = i2c_init,
    .read   = i2c_read,
    .validate = i2c_validate
};

/* ---------- Static driver registry ---------- */

static const SensorDriver_t * const s_driver_registry[] = {
    &s_distance_driver,
    &s_analog_driver,
    &s_i2c_driver
};

#define DRIVER_REGISTRY_SIZE  (sizeof(s_driver_registry) / sizeof(s_driver_registry[0]))

/* ---------- Registry API ---------- */

uint8_t sensor_driver_registry_count(void)
{
    return (uint8_t)DRIVER_REGISTRY_SIZE;
}

const SensorDriver_t *sensor_driver_find(SensorType_t type)
{
    for (uint8_t i = 0; i < DRIVER_REGISTRY_SIZE; i++) {
        if (s_driver_registry[i]->type == type) {
            return s_driver_registry[i];
        }
    }
    return NULL;
}

const SensorDriver_t *sensor_driver_registry_get_all(uint8_t *count)
{
    if (count) *count = (uint8_t)DRIVER_REGISTRY_SIZE;
    return s_driver_registry[0];
}
