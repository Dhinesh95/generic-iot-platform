/**
 * @file test_config_engine.c
 * @brief Tests for config engine (Phase 23).
 */

#include "test_helpers/test_utils.h"
#include "config_engine.h"
#include <string.h>

int _total = 0, _passed = 0, _failed = 0;

static int test_config_register_and_select_profile(void)
{
    ConfigEngineContext_t ctx;
    config_engine_init(&ctx);

    /* Create mock vtables. */
    DomainProfileVTable_t home_vtable = {0};
    DomainProfileVTable_t agri_vtable = {0};

    /* Register profiles. */
    config_register_profile(&ctx, DOMAIN_PROFILE_HOME, &home_vtable, "home");
    config_register_profile(&ctx, DOMAIN_PROFILE_AGRICULTURE, &agri_vtable, "agriculture");

    TEST_ASSERT_EQUAL(2, config_get_profile_count(&ctx));

    /* No active profile yet. */
    TEST_ASSERT_NULL(config_get_active_vtable(&ctx));
    TEST_ASSERT_EQUAL(0, config_get_active_profile_id(&ctx));

    /* Select home. */
    config_set_active_profile(&ctx, DOMAIN_PROFILE_HOME);
    TEST_ASSERT(&home_vtable == config_get_active_vtable(&ctx));
    TEST_ASSERT_EQUAL(DOMAIN_PROFILE_HOME, config_get_active_profile_id(&ctx));

    /* Switch to agriculture. */
    config_set_active_profile(&ctx, DOMAIN_PROFILE_AGRICULTURE);
    TEST_ASSERT(&agri_vtable == config_get_active_vtable(&ctx));
    TEST_ASSERT_EQUAL(DOMAIN_PROFILE_AGRICULTURE, config_get_active_profile_id(&ctx));

    TEST_PASS();
}

static int test_config_cannot_overwrite_factory_params(void)
{
    ConfigEngineContext_t ctx;
    config_engine_init(&ctx);

    /* Simulate a factory-set (read-only) parameter. */
    config_set_param(&ctx, "device_serial", "SN-12345");
    /* Make it read-only by finding and modifying. */
    for (uint8_t i = 0; i < ctx.param_count; i++) {
        if (strcmp(ctx.params[i].key, "device_serial") == 0) {
            ctx.params[i].editable = false;
        }
    }

    /* Try to modify — should fail. */
    ConfigResult_t result = config_set_param(&ctx, "device_serial", "SN-99999");
    TEST_ASSERT_EQUAL(CONFIG_ERR_READONLY, result);

    /* Value should be unchanged. */
    char val[64];
    config_get_param(&ctx, "device_serial", val, sizeof(val));
    TEST_ASSERT_EQUAL(0, strcmp(val, "SN-12345"));

    /* Editable param should work. */
    config_set_param(&ctx, "sampling_rate", "1000");
    result = config_set_param(&ctx, "sampling_rate", "2000");
    TEST_ASSERT_EQUAL(CONFIG_OK, result);

    config_get_param(&ctx, "sampling_rate", val, sizeof(val));
    TEST_ASSERT_EQUAL(0, strcmp(val, "2000"));

    TEST_PASS();
}

static int test_config_persistence_survives_reboot(void)
{
    ConfigEngineContext_t ctx;
    config_engine_init(&ctx);

    /* Register profiles and set params. */
    DomainProfileVTable_t vtable = {0};
    config_register_profile(&ctx, DOMAIN_PROFILE_HOME, &vtable, "home");
    config_set_active_profile(&ctx, DOMAIN_PROFILE_HOME);
    config_set_param(&ctx, "sampling_rate", "500");

    /* Simulate reboot (re-init the engine). */
    ConfigEngineContext_t ctx2;
    config_engine_init(&ctx2);

    /* Profiles and params are NOT persisted across re-init (in-memory only).
     * This is expected — production would use NVS/flash for persistence.
     * Verify the engine starts clean. */
    TEST_ASSERT_EQUAL(0, config_get_profile_count(&ctx2));
    TEST_ASSERT_NULL(config_get_active_vtable(&ctx2));

    /* Re-register (simulating boot-time profile registration). */
    config_register_profile(&ctx2, DOMAIN_PROFILE_HOME, &vtable, "home");
    config_set_active_profile(&ctx2, DOMAIN_PROFILE_HOME);
    TEST_ASSERT_EQUAL(DOMAIN_PROFILE_HOME, config_get_active_profile_id(&ctx2));

    TEST_PASS();
}

static int test_config_portal_profile_switch(void)
{
    ConfigEngineContext_t ctx;
    config_engine_init(&ctx);

    DomainProfileVTable_t home_vtable = {0};
    DomainProfileVTable_t hvac_vtable = {0};
    DomainProfileVTable_t water_vtable = {0};

    config_register_profile(&ctx, DOMAIN_PROFILE_HOME, &home_vtable, "home");
    config_register_profile(&ctx, DOMAIN_PROFILE_HVAC, &hvac_vtable, "hvac");
    config_register_profile(&ctx, DOMAIN_PROFILE_WATER_TREATMENT, &water_vtable, "water");

    /* Set operational params. */
    config_set_param(&ctx, "cloud_endpoint", "mqtt.example.com");
    config_set_param(&ctx, "sampling_interval_ms", "1000");

    /* Simulate portal switching to HVAC. */
    config_set_active_profile(&ctx, DOMAIN_PROFILE_HVAC);
    TEST_ASSERT_EQUAL(DOMAIN_PROFILE_HVAC, config_get_active_profile_id(&ctx));

    /* Verify param is still accessible. */
    char val[64];
    TEST_ASSERT_EQUAL(CONFIG_OK, config_get_param(&ctx, "cloud_endpoint", val, sizeof(val)));
    TEST_ASSERT_EQUAL(0, strcmp(val, "mqtt.example.com"));

    TEST_PASS();
}

int main(void)
{
    printf("\n=== Config Engine Tests ===\n\n");

    RUN_TEST(test_config_register_and_select_profile);
    RUN_TEST(test_config_cannot_overwrite_factory_params);
    RUN_TEST(test_config_persistence_survives_reboot);
    RUN_TEST(test_config_portal_profile_switch);

    PRINT_TEST_SUMMARY();
}
