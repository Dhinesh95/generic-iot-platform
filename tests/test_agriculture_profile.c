/**
 * @file test_agriculture_profile.c
 * @brief Tests for Agriculture domain profile integration.
 *
 * Architecture ref: Section 6 (vtable pattern), Section 9, Section 10.
 * Phase 2: Agriculture as second domain profile proving core/profile separation.
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/rule_engine_core.h"
#include "../firmware/shared/sensor_validation.h"
#include "../firmware/shared/actuator_failsafe.h"
#include "../firmware/shared/power_profile.h"
#include "../firmware/shared/lora_handler.h"
#include "../firmware/shared/attestation.h"
#include "../firmware/shared/audit_log.h"
#include "../firmware/hub/config_portal.h"
#include "../firmware/profiles/agriculture/agriculture_rules.h"
#include "../firmware/profiles/agriculture/agriculture_validation_bounds.h"
#include "../firmware/profiles/agriculture/agriculture_failsafe.h"
#include <string.h>

/* Test key for LoRa module attestation (node_id 0xF0). */
static const uint8_t TEST_MODULE_KEY[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20
};

/** Helper: init + join LoRa for tests that need send/receive. */
static void lora_init_and_join(void)
{
    lora_init(NULL);
    attestation_init();
    audit_log_set_chain_key(TEST_MODULE_KEY, 32);
    lora_mock_set_module_key(TEST_MODULE_KEY);  /* Mock module has this key. */
    lora_join(TEST_MODULE_KEY, 32);             /* Hub uses the same key. */
}

/* ---------- Test: vtable exists ---------- */
static int test_agriculture_vtable_exists(void)
{
    const DomainProfileVTable_t *vtable = agriculture_profile_get_vtable();
    TEST_ASSERT_NOT_NULL(vtable);
    TEST_ASSERT_NOT_NULL(vtable->getRuleTable);
    TEST_ASSERT_NOT_NULL(vtable->validateSensorReading);
    TEST_ASSERT_NOT_NULL(vtable->getFailSafeMode);
    TEST_ASSERT_NOT_NULL(vtable->executeAction);
    TEST_PASS();
}

/* ---------- Test: rule table has correct count ---------- */
static int test_agriculture_rule_table_count(void)
{
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    agriculture_getRuleTable(&rules, &count);

    TEST_ASSERT_NOT_NULL(rules);
    TEST_ASSERT(count >= 3);  /* At minimum: pump dry-run, irrigation, frost. */
    TEST_PASS();
}

/* ---------- Test: CRITICAL rule exists ---------- */
static int test_agriculture_has_critical_rule(void)
{
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    agriculture_getRuleTable(&rules, &count);

    bool found_critical = false;
    for (uint8_t i = 0; i < count; i++) {
        if (rules[i].rule_class == RULE_CLASS_SAFETY_LOCKED) {
            found_critical = true;
            break;
        }
    }
    TEST_ASSERT(found_critical == true);
    TEST_PASS();
}

/* ---------- Test: OPERATIONAL rules exist ---------- */
static int test_agriculture_has_operational_rules(void)
{
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    agriculture_getRuleTable(&rules, &count);

    uint8_t op_count = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (rules[i].rule_class == RULE_CLASS_OPERATIONAL) {
            op_count++;
        }
    }
    TEST_ASSERT(op_count >= 2);  /* At minimum: irrigation + frost. */
    TEST_PASS();
}

/* ---------- Test: all rules fit in 16 bytes ---------- */
static int test_agriculture_rules_struct_size(void)
{
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    agriculture_getRuleTable(&rules, &count);

    for (uint8_t i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL(16, sizeof(RuleEntry_t));
    }
    TEST_PASS();
}

/* ---------- Test: validation bounds for soil moisture ---------- */
static int test_agriculture_soil_moisture_bounds(void)
{
    const SensorValidationBounds_t *bounds = agriculture_get_validation_bounds(AGRI_METRIC_SOIL_MOISTURE);
    TEST_ASSERT_NOT_NULL(bounds);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bounds->min_physical, 0.001f);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, bounds->max_physical, 0.001f);
    TEST_ASSERT(bounds->stuck_timeout_sec > 0);
    TEST_PASS();
}

/* ---------- Test: validation bounds for ambient temp (wider than Home) ---------- */
static int test_agriculture_ambient_temp_bounds(void)
{
    const SensorValidationBounds_t *bounds = agriculture_get_validation_bounds(AGRI_METRIC_AMBIENT_TEMP);
    TEST_ASSERT_NOT_NULL(bounds);
    /* Agriculture outdoor range: -40 to +60 C (wider than Home's indoor range). */
    TEST_ASSERT_EQUAL_FLOAT(-40.0f, bounds->min_physical, 0.001f);
    TEST_ASSERT_EQUAL_FLOAT(60.0f, bounds->max_physical, 0.001f);
    TEST_ASSERT(bounds->stuck_timeout_sec > 0);
    TEST_PASS();
}

/* ---------- Test: validation bounds for water level ---------- */
static int test_agriculture_water_level_bounds(void)
{
    const SensorValidationBounds_t *bounds = agriculture_get_validation_bounds(AGRI_METRIC_WATER_LEVEL);
    TEST_ASSERT_NOT_NULL(bounds);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bounds->min_physical, 0.001f);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, bounds->max_physical, 0.001f);
    TEST_PASS();
}

/* ---------- Test: unknown metric returns NULL ---------- */
static int test_agriculture_unknown_metric_bounds(void)
{
    const SensorValidationBounds_t *bounds = agriculture_get_validation_bounds(255);
    TEST_ASSERT_NULL(bounds);
    TEST_PASS();
}

/* ---------- Test: history buffers accessible ---------- */
static int test_agriculture_history_buffers(void)
{
    SensorHistory_t *h;

    h = agriculture_get_metric_history(AGRI_METRIC_SOIL_MOISTURE);
    TEST_ASSERT_NOT_NULL(h);

    h = agriculture_get_metric_history(AGRI_METRIC_AMBIENT_TEMP);
    TEST_ASSERT_NOT_NULL(h);

    h = agriculture_get_metric_history(AGRI_METRIC_AMBIENT_HUMIDITY);
    TEST_ASSERT_NOT_NULL(h);

    h = agriculture_get_metric_history(AGRI_METRIC_WATER_LEVEL);
    TEST_ASSERT_NOT_NULL(h);

    h = agriculture_get_metric_history(255);
    TEST_ASSERT_NULL(h);
    TEST_PASS();
}

/* ---------- Test: failsafe table registered ---------- */
static int test_agriculture_failsafe_registered(void)
{
    actuator_failsafe_init();
    bool result = agriculture_failsafe_register_all();
    TEST_ASSERT(result == true);

    /* Should have at least 3 actuators (pump, valve, frost valve). */
    TEST_ASSERT(actuator_failsafe_count() >= 3);
    TEST_PASS();
}

/* ---------- Test: pump dry-run protection (CRITICAL rule) ---------- */
static int test_agriculture_pump_dry_run_rule(void)
{
    rule_engine_init();

    const DomainProfileVTable_t *vtable = agriculture_profile_get_vtable();

    /* Water level below 10% should trigger dry-run protection. */
    SensorReading_t reading = {
        .node_id = 1,
        .metric_id = AGRI_METRIC_WATER_LEVEL,
        .value = 5.0f,  /* Below 10% threshold. */
        .timestamp_ms = 1000
    };

    RuleEvaluationResult_t results[8];
    uint8_t count = rule_engine_evaluate(vtable, &reading, results, 8);
    TEST_ASSERT(count >= 1);
    TEST_ASSERT_EQUAL(AGRI_RULE_PUMP_DRY_RUN_CRITICAL, results[0].rule_id);
    TEST_ASSERT(results[0].triggered == true);
    TEST_PASS();
}

/* ---------- Test: vtable evaluation — irrigation scheduling ---------- */
static int test_agriculture_irrigation_schedule(void)
{
    rule_engine_init();

    const DomainProfileVTable_t *vtable = agriculture_profile_get_vtable();

    /* Soil moisture below 25% should trigger irrigation. */
    SensorReading_t reading = {
        .node_id = 1,
        .metric_id = AGRI_METRIC_SOIL_MOISTURE,
        .value = 15.0f,  /* Below 25% threshold. */
        .timestamp_ms = 1000
    };

    RuleEvaluationResult_t results[8];
    uint8_t count = rule_engine_evaluate(vtable, &reading, results, 8);
    TEST_ASSERT(count >= 1);

    bool found_irrigation = false;
    for (uint8_t i = 0; i < count; i++) {
        if (results[i].rule_id == AGRI_RULE_IRRIGATION_OPERATIONAL) {
            found_irrigation = true;
            break;
        }
    }
    TEST_ASSERT(found_irrigation == true);
    TEST_PASS();
}

/* ---------- Test: vtable validate and execute ---------- */
static int test_agriculture_vtable_validate_execute(void)
{
    const DomainProfileVTable_t *vtable = agriculture_profile_get_vtable();

    SensorReading_t valid = { .node_id = 1, .metric_id = 20, .value = 50.0f, .timestamp_ms = 1000 };
    TEST_ASSERT(vtable->validateSensorReading(&valid) == true);

    SensorReading_t invalid = { .node_id = 1, .metric_id = 20, .value = -10.0f, .timestamp_ms = 1000 };
    TEST_ASSERT(vtable->validateSensorReading(&invalid) == false);

    TEST_ASSERT(vtable->executeAction(AGRI_ACTUATOR_IRRIGATION_PUMP, ACTUATOR_STATE_ON) == true);
    TEST_PASS();
}

/* ---------- Test: pump failsafe — FORCE_OFF on both power and comms loss ---------- */
static int test_agriculture_pump_failsafe(void)
{
    uint8_t power_mode = agriculture_getFailSafeMode(AGRI_ACTUATOR_IRRIGATION_PUMP, true);
    uint8_t comms_mode = agriculture_getFailSafeMode(AGRI_ACTUATOR_IRRIGATION_PUMP, false);

    /* Pump should be FORCE_OFF on both — unmonitored pump is dangerous. */
    TEST_ASSERT_EQUAL(FAILSAFE_FORCE_OFF, power_mode);
    TEST_ASSERT_EQUAL(FAILSAFE_FORCE_OFF, comms_mode);
    TEST_PASS();
}

/* ---------- Test: valve failsafe — different modes for power vs comms loss ---------- */
static int test_agriculture_valve_failsafe(void)
{
    uint8_t power_mode = agriculture_getFailSafeMode(AGRI_ACTUATOR_IRRIGATION_VALVE, true);
    uint8_t comms_mode = agriculture_getFailSafeMode(AGRI_ACTUATOR_IRRIGATION_VALVE, false);

    /* Valve: DE_ENERGIZE on power loss (spring-shut), HOLD_LAST on comms loss. */
    TEST_ASSERT_EQUAL(FAILSAFE_DE_ENERGIZE, power_mode);
    TEST_ASSERT_EQUAL(FAILSAFE_HOLD_LAST, comms_mode);

    /* They MUST be different. */
    TEST_ASSERT(power_mode != comms_mode);
    TEST_PASS();
}

/* ---------- Test: comms timeout values ---------- */
static int test_agriculture_comms_timeouts(void)
{
    uint16_t pump_timeout = agriculture_get_comms_timeout(AGRI_ACTUATOR_IRRIGATION_PUMP);
    uint16_t valve_timeout = agriculture_get_comms_timeout(AGRI_ACTUATOR_IRRIGATION_VALVE);
    uint16_t frost_timeout = agriculture_get_comms_timeout(AGRI_ACTUATOR_FROST_VALVE);

    TEST_ASSERT_EQUAL(30, pump_timeout);
    TEST_ASSERT_EQUAL(600, valve_timeout);
    TEST_ASSERT_EQUAL(300, frost_timeout);
    TEST_PASS();
}

/* ---------- Test: power profile mechanism ---------- */
static int test_power_profile_mains(void)
{
    power_profile_init();

    PowerProfile_t mains = power_profile_create_mains();
    TEST_ASSERT_EQUAL(POWER_SOURCE_MAINS, mains.source);
    TEST_ASSERT_EQUAL(0, mains.active_duration_ms);
    TEST_ASSERT_EQUAL(0, mains.sleep_duration_ms);
    TEST_ASSERT(mains.deep_sleep_enabled == false);

    TEST_ASSERT(power_profile_register(&mains) == true);
    TEST_ASSERT_EQUAL(1, power_profile_count());

    const PowerProfile_t *retrieved = power_profile_get(0);
    TEST_ASSERT_NOT_NULL(retrieved);
    TEST_ASSERT_EQUAL(POWER_SOURCE_MAINS, retrieved->source);
    TEST_PASS();
}

/* ---------- Test: power profile battery ---------- */
static int test_power_profile_battery(void)
{
    power_profile_init();

    PowerProfile_t battery = power_profile_create_battery(5000, 55000, 3300);
    TEST_ASSERT_EQUAL(POWER_SOURCE_BATTERY, battery.source);
    TEST_ASSERT_EQUAL(5000, battery.active_duration_ms);
    TEST_ASSERT_EQUAL(55000, battery.sleep_duration_ms);
    TEST_ASSERT_EQUAL(3300, battery.low_battery_mv);
    TEST_ASSERT(battery.deep_sleep_enabled == true);
    TEST_ASSERT(battery.telemetry_duty_cycle == true);
    TEST_PASS();
}

/* ---------- Test: power profile solar ---------- */
static int test_power_profile_solar(void)
{
    power_profile_init();

    PowerProfile_t solar = power_profile_create_solar(10000, 50000, 3400);
    TEST_ASSERT_EQUAL(POWER_SOURCE_SOLAR, solar.source);
    TEST_ASSERT_EQUAL(10000, solar.active_duration_ms);
    TEST_ASSERT(solar.deep_sleep_enabled == true);
    TEST_PASS();
}

/* ---------- Test: power profile evaluate ---------- */
static int test_power_profile_evaluate(void)
{
    power_profile_init();

    /* Create a battery profile with specific thresholds. */
    PowerProfile_t battery = power_profile_create_battery(5000, 55000, 3300);
    /* battery.low_battery_mv = 3300, battery.critical_battery_mv = 3100 */

    PowerState_t state;
    memset(&state, 0, sizeof(state));

    /* Normal voltage — stay active. */
    PowerAction_t action = power_profile_evaluate(&state, &battery, 1000, 3700);
    TEST_ASSERT_EQUAL(POWER_ACTION_STAY_ACTIVE, action);
    TEST_ASSERT(state.low_battery == false);

    /* Low battery — throttle. */
    action = power_profile_evaluate(&state, &battery, 2000, 3200);
    TEST_ASSERT_EQUAL(POWER_ACTION_THROTTLE, action);
    TEST_ASSERT(state.low_battery == true);

    /* Critical battery — shutdown. */
    action = power_profile_evaluate(&state, &battery, 3000, 3000);
    TEST_ASSERT_EQUAL(POWER_ACTION_SHUTDOWN, action);
    TEST_ASSERT(state.critical_battery == true);
    TEST_PASS();
}

/* ---------- Test: power profile differential thresholds ---------- */
static int test_power_profile_differential_thresholds(void)
{
    power_profile_init();

    /*
     * Two profiles with deliberately different thresholds:
     * - Battery: low=3300mV, critical=3100mV
     * - Solar:   low=3600mV, critical=3400mV (higher thresholds — solar
     *   panels need more headroom to operate reliably)
     */
    PowerProfile_t battery = power_profile_create_battery(5000, 55000, 3300);
    PowerProfile_t solar = power_profile_create_solar(10000, 50000, 3600);

    PowerState_t state;

    /* Test at 3500mV: battery is fine (above 3300), solar is low (below 3600). */
    memset(&state, 0, sizeof(state));
    PowerAction_t action_battery = power_profile_evaluate(&state, &battery, 1000, 3500);
    TEST_ASSERT_EQUAL(POWER_ACTION_STAY_ACTIVE, action_battery);
    TEST_ASSERT(state.low_battery == false);

    memset(&state, 0, sizeof(state));
    PowerAction_t action_solar = power_profile_evaluate(&state, &solar, 1000, 3500);
    TEST_ASSERT_EQUAL(POWER_ACTION_THROTTLE, action_solar);
    TEST_ASSERT(state.low_battery == true);

    /* Test at 3450mV: battery fine (above 3300), solar low (below 3600, above 3400 critical). */
    memset(&state, 0, sizeof(state));
    action_battery = power_profile_evaluate(&state, &battery, 2000, 3450);
    TEST_ASSERT_EQUAL(POWER_ACTION_STAY_ACTIVE, action_battery);

    memset(&state, 0, sizeof(state));
    action_solar = power_profile_evaluate(&state, &solar, 2000, 3450);
    TEST_ASSERT_EQUAL(POWER_ACTION_THROTTLE, action_solar);

    /* Test at 3250mV: battery low (below 3300, above 3100 critical), solar critical (below 3400). */
    memset(&state, 0, sizeof(state));
    action_battery = power_profile_evaluate(&state, &battery, 3000, 3250);
    TEST_ASSERT_EQUAL(POWER_ACTION_THROTTLE, action_battery);

    memset(&state, 0, sizeof(state));
    action_solar = power_profile_evaluate(&state, &solar, 3000, 3250);
    TEST_ASSERT_EQUAL(POWER_ACTION_SHUTDOWN, action_solar);

    /* Test at 3650mV: both profiles should be fine. */
    memset(&state, 0, sizeof(state));
    action_battery = power_profile_evaluate(&state, &battery, 4000, 3650);
    TEST_ASSERT_EQUAL(POWER_ACTION_STAY_ACTIVE, action_battery);

    memset(&state, 0, sizeof(state));
    action_solar = power_profile_evaluate(&state, &solar, 4000, 3650);
    TEST_ASSERT_EQUAL(POWER_ACTION_STAY_ACTIVE, action_solar);

    /* Test at 3050mV: both profiles should shut down (below both critical thresholds). */
    memset(&state, 0, sizeof(state));
    action_battery = power_profile_evaluate(&state, &battery, 5000, 3050);
    TEST_ASSERT_EQUAL(POWER_ACTION_SHUTDOWN, action_battery);

    memset(&state, 0, sizeof(state));
    action_solar = power_profile_evaluate(&state, &solar, 5000, 3050);
    TEST_ASSERT_EQUAL(POWER_ACTION_SHUTDOWN, action_solar);

    TEST_PASS();
}

/* ---------- Test: LoRa handler init ---------- */
static int test_lora_handler_init(void)
{
    TEST_ASSERT(lora_init(NULL) == true);
    TEST_ASSERT(lora_is_joined() == false);  /* Starts in JOINING state. */
    TEST_ASSERT(lora_is_ready() == false);   /* Not ready until joined. */

    /* Verify default config was applied. */
    const LoRaConfig_t *cfg = lora_get_config();
    TEST_ASSERT_NOT_NULL(cfg);
    TEST_ASSERT_EQUAL(868, cfg->frequency_mhz);
    TEST_ASSERT(cfg->crc_enabled == true);
    TEST_PASS();
}

/* ---------- Test: LoRa default config ---------- */
static int test_lora_default_config(void)
{
    LoRaConfig_t eu = lora_default_config("EU868");
    TEST_ASSERT_EQUAL(868, eu.frequency_mhz);
    TEST_ASSERT_EQUAL(10, eu.spreading_factor);
    TEST_ASSERT_EQUAL(14, eu.tx_power_dbm);
    TEST_ASSERT(eu.crc_enabled == true);

    LoRaConfig_t us = lora_default_config("US915");
    TEST_ASSERT_EQUAL(915, us.frequency_mhz);
    TEST_ASSERT_EQUAL(10, us.spreading_factor);
    TEST_ASSERT(us.crc_enabled == true);
    TEST_PASS();
}

/* ---------- Test: LoRa regional config validation ---------- */
static int test_lora_regional_config_validation(void)
{
    /* EU868 frequency must be in the 863-870 MHz band. */
    LoRaConfig_t eu = lora_default_config("EU868");
    TEST_ASSERT(eu.frequency_mhz >= 863 && eu.frequency_mhz <= 870);

    /* US915 frequency must be in the 902-928 MHz band. */
    LoRaConfig_t us = lora_default_config("US915");
    TEST_ASSERT(us.frequency_mhz >= 902 && us.frequency_mhz <= 928);

    /* Spreading factor must be 7-12. */
    TEST_ASSERT(eu.spreading_factor >= 7 && eu.spreading_factor <= 12);
    TEST_ASSERT(us.spreading_factor >= 7 && us.spreading_factor <= 12);

    /* TX power must be 2-20 dBm. */
    TEST_ASSERT(eu.tx_power_dbm >= 2 && eu.tx_power_dbm <= 20);
    TEST_ASSERT(us.tx_power_dbm >= 2 && us.tx_power_dbm <= 20);

    /* Config should be retrievable after init. */
    lora_init(&eu);
    const LoRaConfig_t *active = lora_get_config();
    TEST_ASSERT_NOT_NULL(active);
    TEST_ASSERT_EQUAL(868, active->frequency_mhz);
    TEST_PASS();
}

/* ---------- Test: LoRa CRC-16 correctness ---------- */
static int test_lora_crc16(void)
{
    /* Known test vector: CRC-16/CCITT of "123456789" = 0x29B1. */
    const uint8_t test_data[] = "123456789";
    uint16_t crc = lora_crc16(test_data, 9);
    TEST_ASSERT_EQUAL(0x29B1, crc);

    /* CRC of empty data should be 0xFFFF (initial value). */
    uint16_t crc_empty = lora_crc16(NULL, 0);
    TEST_ASSERT_EQUAL(0xFFFF, crc_empty);

    /* CRC should be deterministic. */
    uint16_t crc2 = lora_crc16(test_data, 9);
    TEST_ASSERT_EQUAL(crc, crc2);

    /* Different data should produce different CRC. */
    const uint8_t test_data2[] = "12345678A";
    uint16_t crc3 = lora_crc16(test_data2, 9);
    TEST_ASSERT(crc != crc3);
    TEST_PASS();
}

/* ---------- Test: LoRa send/receive roundtrip via loopback ---------- */
static int test_lora_send_receive_roundtrip(void)
{
    lora_init_and_join();

    const char *msg = "Hello from Gateway";
    LoRaResult_t result = lora_send(0x1234, (const uint8_t *)msg, strlen(msg));
    TEST_ASSERT(result == LORA_OK);

    /* After send, there should be data in the receive buffer (loopback). */
    LoRaFrame_t frame;
    result = lora_receive(&frame);
    TEST_ASSERT(result == LORA_OK);
    TEST_ASSERT(frame.payload_len == strlen(msg));
    TEST_ASSERT_MEM_EQUAL(msg, frame.payload, strlen(msg));
    TEST_PASS();
}

/* ---------- Test: LoRa sequence numbering ---------- */
static int test_lora_sequence_numbers(void)
{
    lora_init_and_join();

    /* First send should use seq 0. */
    const uint8_t data1[] = "first";
    lora_send(0x1000, data1, 5);

    /* Receive the loopback packet and check seq_num. */
    LoRaFrame_t frame;
    lora_receive(&frame);
    TEST_ASSERT_EQUAL(0, frame.seq_num);

    /* Second send should use seq 1. */
    const uint8_t data2[] = "second";
    lora_send(0x1000, data2, 6);

    lora_receive(&frame);
    TEST_ASSERT_EQUAL(1, frame.seq_num);
    TEST_PASS();
}

/* ---------- Test: LoRa malformed frame rejection ---------- */
static int test_lora_malformed_frame_rejection(void)
{
    lora_init_and_join();

    /* Try to send with NULL payload. */
    LoRaResult_t result = lora_send(0x1000, NULL, 5);
    TEST_ASSERT(result == LORA_ERR_PARAM_NULL);

    /* Try to receive with NULL frame. */
    result = lora_receive(NULL);
    TEST_ASSERT(result == LORA_ERR_PARAM_NULL);

    /* Not joined — send should fail. */
    lora_init(NULL);
    result = lora_send(0x1000, (const uint8_t *)"x", 1);
    TEST_ASSERT(result == LORA_ERR_NOT_JOINED);
    TEST_PASS();
}

/* ---------- Test: LoRa stats tracking ---------- */
static int test_lora_stats(void)
{
    lora_init_and_join();

    LoRaStats_t stats;
    lora_get_stats(&stats);
    TEST_ASSERT_EQUAL(0, stats.tx_packets);
    TEST_ASSERT_EQUAL(0, stats.rx_packets);

    /* Send + receive increments counters. */
    const uint8_t data[] = "stats test";
    lora_send(0x1000, data, 10);

    LoRaFrame_t frame;
    lora_receive(&frame);

    lora_get_stats(&stats);
    TEST_ASSERT(stats.tx_packets >= 1);
    TEST_ASSERT(stats.rx_packets >= 1);
    TEST_ASSERT(stats.tx_acks_received >= 1);
    TEST_PASS();
}

/* ---------- Test: LoRa receive with no data ---------- */
static int test_lora_receive_no_data(void)
{
    lora_init_and_join();

    LoRaFrame_t frame;
    LoRaResult_t result = lora_receive(&frame);
    TEST_ASSERT(result == LORA_ERR_NO_DATA);
    TEST_PASS();
}

/* ---------- Test: LoRa retry exhaustion — never ACK ---------- */
static int test_lora_retry_exhaustion(void)
{
    lora_init_and_join();

    /* Configure mock to never ACK (fail_count > LORA_MAX_RETRIES). */
    lora_mock_set_ack_fail_count(255);  /* Will fail for many attempts. */

    const uint8_t data[] = "retry test";
    LoRaResult_t result = lora_send(0x1000, data, sizeof(data) - 1);

    /* Should fail after exhausting all retries. */
    TEST_ASSERT(result == LORA_ERR_ACK_TIMEOUT);

    /*
     * The retry loop runs attempt = 0, 1, ..., LORA_MAX_RETRIES (=3),
     * which is LORA_MAX_RETRIES + 1 = 4 iterations total.
     * Each iteration sends a packet and counts as a tx_packets increment.
     * Each failed ACK counts as a tx_retries and tx_ack_timeouts increment.
     */
    LoRaStats_t stats;
    lora_get_stats(&stats);
    TEST_ASSERT_EQUAL(LORA_MAX_RETRIES + 1, stats.tx_packets);     /* 4 total sends */
    TEST_ASSERT_EQUAL(LORA_MAX_RETRIES + 1, stats.tx_retries);     /* 4 failed ACKs */
    TEST_ASSERT_EQUAL(LORA_MAX_RETRIES + 1, stats.tx_ack_timeouts);/* 4 timeouts */
    TEST_ASSERT_EQUAL(0, stats.tx_acks_received);  /* No ACKs received */

    /* Sequence number should NOT have advanced (no successful send). */
    lora_mock_set_ack_fail_count(0);  /* Restore normal ACK. */
    result = lora_send(0x1000, data, sizeof(data) - 1);
    TEST_ASSERT(result == LORA_OK);

    /* Radio should be ready after failed send. */
    TEST_ASSERT(lora_is_ready() == true);

    TEST_PASS();
}

/* ---------- Test: LoRa retry succeeds on second attempt ---------- */
static int test_lora_retry_succeeds_on_second_attempt(void)
{
    lora_init_and_join();

    /* Configure mock to fail ACK on first attempt, succeed on second. */
    lora_mock_set_ack_fail_count(1);

    const uint8_t data[] = "retry then succeed";
    LoRaResult_t result = lora_send(0x1000, data, sizeof(data) - 1);

    /* Should succeed on the second attempt. */
    TEST_ASSERT(result == LORA_OK);

    /* Stats: 2 packets sent (1 failed + 1 succeeded), 1 retry, 1 timeout, 1 ack. */
    LoRaStats_t stats;
    lora_get_stats(&stats);
    TEST_ASSERT_EQUAL(2, stats.tx_packets);         /* 1 failed + 1 succeeded */
    TEST_ASSERT_EQUAL(1, stats.tx_retries);         /* 1 retry */
    TEST_ASSERT_EQUAL(1, stats.tx_ack_timeouts);    /* 1 timeout */
    TEST_ASSERT_EQUAL(1, stats.tx_acks_received);   /* 1 ack received */

    /* Receive the loopback packet. */
    LoRaFrame_t frame;
    result = lora_receive(&frame);
    TEST_ASSERT(result == LORA_OK);
    TEST_ASSERT_MEM_EQUAL(data, frame.payload, sizeof(data) - 1);

    TEST_PASS();
}

/* ---------- Test: LoRa send max payload (edge case) ---------- */
static int test_lora_send_max_payload(void)
{
    lora_init_and_join();

    /* Send exactly LORA_MAX_PAYLOAD bytes — should succeed. */
    uint8_t big[LORA_MAX_PAYLOAD];
    memset(big, 'A', sizeof(big));

    LoRaResult_t result = lora_send(0x1000, big, sizeof(big));
    TEST_ASSERT(result == LORA_OK);

    /* Receive it back. */
    LoRaFrame_t frame;
    result = lora_receive(&frame);
    TEST_ASSERT(result == LORA_OK);
    TEST_ASSERT_EQUAL(LORA_MAX_PAYLOAD, frame.payload_len);
    TEST_PASS();
}

/* ---------- Main ---------- */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_agriculture_profile ===\n");
    RUN_TEST(test_agriculture_vtable_exists);
    RUN_TEST(test_agriculture_rule_table_count);
    RUN_TEST(test_agriculture_has_critical_rule);
    RUN_TEST(test_agriculture_has_operational_rules);
    RUN_TEST(test_agriculture_rules_struct_size);
    RUN_TEST(test_agriculture_soil_moisture_bounds);
    RUN_TEST(test_agriculture_ambient_temp_bounds);
    RUN_TEST(test_agriculture_water_level_bounds);
    RUN_TEST(test_agriculture_unknown_metric_bounds);
    RUN_TEST(test_agriculture_history_buffers);
    RUN_TEST(test_agriculture_failsafe_registered);
    RUN_TEST(test_agriculture_pump_dry_run_rule);
    RUN_TEST(test_agriculture_irrigation_schedule);
    RUN_TEST(test_agriculture_vtable_validate_execute);
    RUN_TEST(test_agriculture_pump_failsafe);
    RUN_TEST(test_agriculture_valve_failsafe);
    RUN_TEST(test_agriculture_comms_timeouts);
    RUN_TEST(test_power_profile_mains);
    RUN_TEST(test_power_profile_battery);
    RUN_TEST(test_power_profile_solar);
    RUN_TEST(test_power_profile_evaluate);
    RUN_TEST(test_power_profile_differential_thresholds);
    RUN_TEST(test_lora_handler_init);
    RUN_TEST(test_lora_default_config);
    RUN_TEST(test_lora_regional_config_validation);
    RUN_TEST(test_lora_crc16);
    RUN_TEST(test_lora_send_receive_roundtrip);
    RUN_TEST(test_lora_sequence_numbers);
    RUN_TEST(test_lora_malformed_frame_rejection);
    RUN_TEST(test_lora_stats);
    RUN_TEST(test_lora_receive_no_data);
    RUN_TEST(test_lora_retry_exhaustion);
    RUN_TEST(test_lora_retry_succeeds_on_second_attempt);
    RUN_TEST(test_lora_send_max_payload);

    PRINT_TEST_SUMMARY();
}
