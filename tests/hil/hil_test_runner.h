/**
 * @file hil_test_runner.h
 * @brief Hardware-in-the-loop test framework — abstract hardware backend.
 *
 * Architecture ref: Phase 28 (HIL Test Framework).
 *
 * Provides an abstract hardware interface that can be backed by either
 * real hardware (for HIL testing) or mocks (for native testing). This
 * allows the same test cases to run in both environments.
 *
 * When hardware arrives, implement the real backend functions and pass
 * them to hil_init(). The test runner calls the same test functions
 * regardless of backend.
 */

#ifndef HIL_TEST_RUNNER_H
#define HIL_TEST_RUNNER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "rule_engine_core.h"

/* ---------- Constants ---------- */

#define HIL_MAX_GPIO_PINS    32
#define HIL_MAX_ADC_CHANNELS 8
#define HIL_TEST_NAME_MAX    64

/* ---------- Types ---------- */

/**
 * Abstract hardware backend — function pointers for real hardware.
 * In production: filled with real ESP32/PY32 implementations.
 * In tests: filled with mock implementations.
 */
typedef struct {
    bool (*gpio_read)(uint8_t pin);
    bool (*gpio_write)(uint8_t pin, bool state);
    bool (*adc_read)(uint8_t channel, uint16_t *value);
    bool (*radio_send)(const uint8_t *data, size_t len);
    bool (*radio_receive)(uint8_t *buf, size_t max_len, size_t *out_len);
    bool (*flash_write)(uint32_t addr, const uint8_t *data, size_t len);
    bool (*flash_read)(uint32_t addr, uint8_t *buf, size_t len);
    uint32_t (*get_battery_mv)(void);
    void (*system_reset)(void);
    void (*delay_ms)(uint32_t ms);
} HILHardwareBackend_t;

/**
 * HIL test result.
 */
typedef enum {
    HIL_TEST_PASS,
    HIL_TEST_FAIL,
    HIL_TEST_SKIP,      /* Hardware not available */
    HIL_TEST_ERROR      /* Framework error */
} HILTestResult_t;

/**
 * HIL test context — passed to each test function.
 */
typedef struct {
    const HILHardwareBackend_t *hw;
    const DomainProfileVTable_t *vtable;
    char test_name[HIL_TEST_NAME_MAX];
} HILTestContext_t;

/**
 * HIL test function signature.
 */
typedef HILTestResult_t (*HILTestFunc_t)(HILTestContext_t *ctx);

/**
 * HIL test entry.
 */
typedef struct {
    const char     *name;
    HILTestFunc_t   func;
} HILTestEntry_t;

/**
 * HIL test suite.
 */
typedef struct {
    const char        *suite_name;
    const HILTestEntry_t *tests;
    uint8_t            test_count;
} HILTestSuite_t;

/* ---------- API ---------- */

/**
 * Initialize the HIL test runner with a hardware backend.
 *
 * @param hw  Hardware backend (NULL for native/mock mode).
 * @return true on success.
 */
bool hil_init(const HILHardwareBackend_t *hw);

/**
 * Check if running against real hardware or mocks.
 */
bool hil_is_real_hardware(void);

/**
 * Run a single HIL test.
 *
 * @param ctx     Test context.
 * @param test    Test function to run.
 * @return Test result.
 */
HILTestResult_t hil_run_single(HILTestContext_t *ctx, HILTestFunc_t test);

/**
 * Run a full HIL test suite.
 *
 * @param ctx     Test context (hardware backend is in here).
 * @param suite   Test suite to run.
 * @return Number of failures.
 */
uint8_t hil_run_suite(HILTestContext_t *ctx, const HILTestSuite_t *suite);

/**
 * Run all registered HIL test suites.
 *
 * @param ctx  Test context.
 * @return Number of failures.
 */
uint8_t hil_run_all(HILTestContext_t *ctx);

#endif /* HIL_TEST_RUNNER_H */
