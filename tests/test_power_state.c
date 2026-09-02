/**
 * @file test_power_state.c
 * @brief Tests for power state management (Phase 25).
 */

#include "test_helpers/test_utils.h"
#include "power_state.h"

int _total = 0, _passed = 0, _failed = 0;

static int test_power_state_battery_thresholds(void)
{
    PowerStateContext_t ctx;
    power_state_init(&ctx);

    /* Full battery — ACTIVE. */
    TEST_ASSERT_EQUAL(POWER_STATE_ACTIVE, power_state_evaluate(&ctx, 4200));
    TEST_ASSERT(power_state_sensors_active(&ctx));
    TEST_ASSERT(power_state_radio_active(&ctx));

    /* Low battery — IDLE. */
    TEST_ASSERT_EQUAL(POWER_STATE_IDLE, power_state_evaluate(&ctx, 3100));
    TEST_ASSERT(!power_state_sensors_active(&ctx));
    TEST_ASSERT(!power_state_radio_active(&ctx));
    TEST_ASSERT(power_state_cloud_active(&ctx));

    /* Critical battery — SLEEP. */
    TEST_ASSERT_EQUAL(POWER_STATE_SLEEP, power_state_evaluate(&ctx, 2700));
    TEST_ASSERT(!power_state_cloud_active(&ctx));
    TEST_ASSERT(power_state_safety_active(&ctx));

    /* Emergency battery — EMERGENCY. */
    TEST_ASSERT_EQUAL(POWER_STATE_EMERGENCY, power_state_evaluate(&ctx, 2400));
    TEST_ASSERT(power_state_safety_active(&ctx));

    /* Shutdown — SHUTDOWN. */
    TEST_ASSERT_EQUAL(POWER_STATE_SHUTDOWN, power_state_evaluate(&ctx, 2200));
    TEST_ASSERT(power_state_should_shutdown(&ctx));
    TEST_ASSERT(!power_state_safety_active(&ctx));

    TEST_PASS();
}

static int test_power_state_sleep_wake_cycle(void)
{
    PowerStateContext_t ctx;
    power_state_init(&ctx);

    /* Start active. */
    power_state_evaluate(&ctx, 4200);
    TEST_ASSERT_EQUAL(POWER_STATE_ACTIVE, power_state_get_current(&ctx));

    /* Battery drops to critical — enters SLEEP. */
    power_state_evaluate(&ctx, 2700);
    TEST_ASSERT_EQUAL(POWER_STATE_SLEEP, power_state_get_current(&ctx));

    /* Battery recovers (charging) — stays in SLEEP (no auto-wake). */
    power_state_evaluate(&ctx, 4200);
    TEST_ASSERT_EQUAL(POWER_STATE_SLEEP, power_state_get_current(&ctx));

    /* Simulate RTC wake — reset to ACTIVE. */
    ctx.current = POWER_STATE_ACTIVE;
    power_state_evaluate(&ctx, 4200);
    TEST_ASSERT_EQUAL(POWER_STATE_ACTIVE, power_state_get_current(&ctx));

    TEST_PASS();
}

static int test_power_state_emergency_safety_only(void)
{
    PowerStateContext_t ctx;
    power_state_init(&ctx);

    /* Full battery. */
    power_state_evaluate(&ctx, 4200);
    TEST_ASSERT(power_state_sensors_active(&ctx));
    TEST_ASSERT(power_state_radio_active(&ctx));

    /* Emergency — only safety rules active. */
    power_state_evaluate(&ctx, 2400);
    TEST_ASSERT(!power_state_sensors_active(&ctx));
    TEST_ASSERT(!power_state_radio_active(&ctx));
    TEST_ASSERT(!power_state_cloud_active(&ctx));
    TEST_ASSERT(power_state_safety_active(&ctx));

    TEST_PASS();
}

static int test_power_state_emergency_ignores_non_safety_wake(void)
{
    PowerStateContext_t ctx;
    power_state_init(&ctx);

    /* Enter EMERGENCY. */
    power_state_evaluate(&ctx, 2400);
    TEST_ASSERT_EQUAL(POWER_STATE_EMERGENCY, power_state_get_current(&ctx));

    /* Radio wake event — but we're in EMERGENCY, so stay EMERGENCY.
     * Only safety-critical wake sources should be honored. */
    power_state_evaluate(&ctx, 2400);
    TEST_ASSERT_EQUAL(POWER_STATE_EMERGENCY, power_state_get_current(&ctx));

    /* Even with good battery, emergency state requires explicit clear. */
    power_state_evaluate(&ctx, 4200);
    TEST_ASSERT_EQUAL(POWER_STATE_EMERGENCY, power_state_get_current(&ctx));

    TEST_PASS();
}

int main(void)
{
    printf("\n=== Power State Tests ===\n\n");

    RUN_TEST(test_power_state_battery_thresholds);
    RUN_TEST(test_power_state_sleep_wake_cycle);
    RUN_TEST(test_power_state_emergency_safety_only);
    RUN_TEST(test_power_state_emergency_ignores_non_safety_wake);

    PRINT_TEST_SUMMARY();
}
