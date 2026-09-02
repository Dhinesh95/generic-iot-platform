/**
 * @file test_hil_framework.c
 * @brief Tests for HIL framework (Phase 28).
 */

#include "test_helpers/test_utils.h"
#include "hil_test_runner.h"
#include <string.h>

int _total = 0, _passed = 0, _failed = 0;

/* Mock hardware backend. */
static bool mock_gpio_read(uint8_t pin) { (void)pin; return true; }
static bool mock_gpio_write(uint8_t pin, bool state) { (void)pin; (void)state; return true; }
static bool mock_adc_read(uint8_t ch, uint16_t *val) { (void)ch; *val = 2048; return true; }
static bool mock_radio_send(const uint8_t *data, size_t len) { (void)data; (void)len; return true; }
static bool mock_radio_receive(uint8_t *buf, size_t max, size_t *out) { (void)buf; (void)max; *out = 0; return false; }
static bool mock_flash_write(uint32_t addr, const uint8_t *data, size_t len) { (void)addr; (void)data; (void)len; return true; }
static bool mock_flash_read(uint32_t addr, uint8_t *buf, size_t len) { (void)addr; memset(buf, 0xAA, len); return true; }
static uint32_t mock_get_battery_mv(void) { return 4200; }
static void mock_system_reset(void) { /* no-op */ }
static void mock_delay_ms(uint32_t ms) { (void)ms; }

static const HILHardwareBackend_t s_mock_backend = {
    .gpio_read = mock_gpio_read,
    .gpio_write = mock_gpio_write,
    .adc_read = mock_adc_read,
    .radio_send = mock_radio_send,
    .radio_receive = mock_radio_receive,
    .flash_write = mock_flash_write,
    .flash_read = mock_flash_read,
    .get_battery_mv = mock_get_battery_mv,
    .system_reset = mock_system_reset,
    .delay_ms = mock_delay_ms
};

static int test_hil_init_mock_mode(void)
{
    TEST_ASSERT(hil_init(NULL));
    TEST_ASSERT(!hil_is_real_hardware());

    TEST_PASS();
}

static int test_hil_init_real_mode(void)
{
    TEST_ASSERT(hil_init(&s_mock_backend));
    TEST_ASSERT(hil_is_real_hardware());

    TEST_PASS();
}

static int test_hil_gpio_operations(void)
{
    hil_init(&s_mock_backend);

    /* GPIO write then read. */
    TEST_ASSERT(s_mock_backend.gpio_write(0, true));
    TEST_ASSERT(s_mock_backend.gpio_read(0));

    /* ADC read. */
    uint16_t adc_val;
    TEST_ASSERT(s_mock_backend.adc_read(0, &adc_val));
    TEST_ASSERT_EQUAL(2048, adc_val);

    /* Battery. */
    TEST_ASSERT_EQUAL(4200, s_mock_backend.get_battery_mv());

    TEST_PASS();
}

static int test_hil_flash_operations(void)
{
    hil_init(&s_mock_backend);

    /* Write data. */
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    TEST_ASSERT(s_mock_backend.flash_write(0x1000, data, sizeof(data)));

    /* Read data (mock returns 0xAA). */
    uint8_t read_buf[4];
    TEST_ASSERT(s_mock_backend.flash_read(0x1000, read_buf, sizeof(read_buf)));
    TEST_ASSERT_EQUAL(0xAA, read_buf[0]);

    TEST_PASS();
}

int main(void)
{
    printf("\n=== HIL Framework Tests ===\n\n");

    RUN_TEST(test_hil_init_mock_mode);
    RUN_TEST(test_hil_init_real_mode);
    RUN_TEST(test_hil_gpio_operations);
    RUN_TEST(test_hil_flash_operations);

    PRINT_TEST_SUMMARY();
}
