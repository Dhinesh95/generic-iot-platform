/**
 * @file test_edge_node.c
 * @brief Tests for Edge Node sensor driver registry, config, attestation, and main loop.
 *
 * Phase 14 + 14.1: Edge Node Sensor Driver Registry + Attestation Gating.
 *
 * Covers:
 *   - Driver registry: count, find by type, iteration
 *   - Individual driver: init, read, validate for each of 3 types
 *   - Edge config: provision, get, driver validation, storage round-trip
 *   - Edge attestation: init, key provision, challenge-response, state gating
 *   - End-to-end: edge_node_read_and_send → RS-485 frame → Gateway parse
 *   - Attestation gating: no data sent before successful attestation
 *   - Gateway relay transparency: attestation frame passes through unmodified
 *   - RAM budget: realistic measurement against Section 8's 8KB PY32 budget
 */

#include "test_helpers/test_utils.h"
#include "../firmware/edge/sensor_driver.h"
#include "../firmware/edge/edge_config.h"
#include "../firmware/edge/edge_node.h"
#include "../firmware/edge/edge_attestation.h"
#include "../firmware/shared/rs485_protocol.h"
#include "../firmware/shared/attestation.h"
#include <string.h>

/* ---------- Additional assertion macros not in test_utils.h ---------- */

#define TEST_ASSERT_TRUE(expr) TEST_ASSERT(expr)
#define TEST_ASSERT_FLOAT_EQUAL(expected, actual) \
    TEST_ASSERT_EQUAL_FLOAT((expected), (actual), 0.001f)

/* ---------- Mock RS-485 send ---------- */

static uint8_t  s_last_wire_buf[256];
static size_t   s_last_wire_len = 0;
static bool     s_send_return = true;

static bool mock_rs485_send(const uint8_t *data, size_t len)
{
    if (len > sizeof(s_last_wire_buf)) return false;
    memcpy(s_last_wire_buf, data, len);
    s_last_wire_len = len;
    return s_send_return;
}

/* ---------- Mock storage for edge config ---------- */

static EdgeNodeConfig_t s_persisted_config;
static bool s_storage_saved = false;
static bool s_storage_loaded = false;

static bool mock_config_save(const EdgeNodeConfig_t *config)
{
    s_persisted_config = *config;
    s_storage_saved = true;
    return true;
}

static bool mock_config_load(EdgeNodeConfig_t *config)
{
    if (s_storage_saved) {
        *config = s_persisted_config;
        s_storage_loaded = true;
        return true;
    }
    return false;
}

static EdgeConfigStorage_t s_mock_storage = {
    .save = mock_config_save,
    .load = mock_config_load
};

/* ---------- Helper: provision a test config ---------- */

static EdgeNodeConfig_t make_test_config(uint8_t node_id, SensorType_t driver)
{
    EdgeNodeConfig_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.node_id = node_id;
    cfg.active_driver = driver;
    cfg.metric_id = 0;  /* Default: driver type as metric_id (backward compat). */
    cfg.driver_config.pin_or_addr = 0x48;
    cfg.driver_config.scale_factor = 1.0f;
    cfg.driver_config.offset = 0.0f;
    cfg.driver_config.sample_count = 4;
    cfg.gw_dest_addr = 0x01;
    cfg.valid = true;
    return cfg;
}

/* ---------- Helper: provision an Edge Node fully ---------- */

static void provision_edge(uint8_t node_id, SensorType_t driver)
{
    edge_config_init(NULL);
    EdgeNodeConfig_t cfg = make_test_config(node_id, driver);
    edge_config_set(&cfg);
}

/* ================================================================ */
/* Driver Registry Tests                                           */
/* ================================================================ */

static int test_driver_registry_count(void)
{
    uint8_t count = sensor_driver_registry_count();
    TEST_ASSERT_EQUAL(3, count);
    TEST_PASS();
}

static int test_driver_registry_find_distance(void)
{
    const SensorDriver_t *d = sensor_driver_find(SENSOR_DISTANCE);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL(SENSOR_DISTANCE, d->type);
    TEST_ASSERT_NOT_NULL(d->init);
    TEST_ASSERT_NOT_NULL(d->read);
    TEST_ASSERT_NOT_NULL(d->validate);
    TEST_PASS();
}

static int test_driver_registry_find_analog(void)
{
    const SensorDriver_t *d = sensor_driver_find(SENSOR_ANALOG);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL(SENSOR_ANALOG, d->type);
    TEST_PASS();
}

static int test_driver_registry_find_i2c(void)
{
    const SensorDriver_t *d = sensor_driver_find(SENSOR_I2C);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL(SENSOR_I2C, d->type);
    TEST_PASS();
}

static int test_driver_registry_find_unknown(void)
{
    const SensorDriver_t *d = sensor_driver_find((SensorType_t)99);
    TEST_ASSERT_NULL(d);
    TEST_PASS();
}

static int test_driver_registry_iteration(void)
{
    uint8_t count = 0;
    const SensorDriver_t *all = sensor_driver_registry_get_all(&count);
    TEST_ASSERT_NOT_NULL(all);
    TEST_ASSERT_EQUAL(3, count);
    bool found_distance = false, found_analog = false, found_i2c = false;
    for (uint8_t i = 0; i < count; i++) {
        if (all[i].type == SENSOR_DISTANCE) found_distance = true;
        if (all[i].type == SENSOR_ANALOG)   found_analog = true;
        if (all[i].type == SENSOR_I2C)      found_i2c = true;
    }
    TEST_ASSERT(found_distance && found_analog && found_i2c);
    TEST_PASS();
}

/* ================================================================ */
/* Individual Driver Tests                                         */
/* ================================================================ */

static int test_distance_driver_read(void)
{
    const SensorDriver_t *d = sensor_driver_find(SENSOR_DISTANCE);
    TEST_ASSERT_NOT_NULL(d);

    sensor_driver_mock_set_distance(150.0f, false);
    SensorDriverReading_t r = d->read();
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_FLOAT_EQUAL(150.0f, r.value);

    sensor_driver_mock_set_distance(0.0f, true);
    r = d->read();
    TEST_ASSERT(!r.valid);

    TEST_PASS();
}

static int test_distance_driver_validate(void)
{
    const SensorDriver_t *d = sensor_driver_find(SENSOR_DISTANCE);
    TEST_ASSERT_NOT_NULL(d);

    TEST_ASSERT_TRUE(d->validate(50.0f));
    TEST_ASSERT_TRUE(d->validate(2.0f));
    TEST_ASSERT_TRUE(d->validate(400.0f));
    TEST_ASSERT(!d->validate(0.5f));
    TEST_ASSERT(!d->validate(500.0f));

    TEST_PASS();
}

static int test_analog_driver_read(void)
{
    const SensorDriver_t *d = sensor_driver_find(SENSOR_ANALOG);
    TEST_ASSERT_NOT_NULL(d);

    sensor_driver_mock_set_adc(2048.0f, false);
    SensorDriverReading_t r = d->read();
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_FLOAT_EQUAL(2048.0f, r.value);

    sensor_driver_mock_set_adc(0.0f, true);
    r = d->read();
    TEST_ASSERT(!r.valid);

    TEST_PASS();
}

static int test_analog_driver_validate(void)
{
    const SensorDriver_t *d = sensor_driver_find(SENSOR_ANALOG);
    TEST_ASSERT_NOT_NULL(d);

    TEST_ASSERT_TRUE(d->validate(0.0f));
    TEST_ASSERT_TRUE(d->validate(2048.0f));
    TEST_ASSERT_TRUE(d->validate(4095.0f));
    TEST_ASSERT(!d->validate(-1.0f));
    TEST_ASSERT(!d->validate(4096.0f));

    TEST_PASS();
}

static int test_i2c_driver_read(void)
{
    const SensorDriver_t *d = sensor_driver_find(SENSOR_I2C);
    TEST_ASSERT_NOT_NULL(d);

    sensor_driver_mock_set_i2c(22.5f, 45.0f, false);
    SensorDriverReading_t r = d->read();
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_FLOAT_EQUAL(22.5f, r.value);

    sensor_driver_mock_set_i2c(0.0f, 0.0f, true);
    r = d->read();
    TEST_ASSERT(!r.valid);

    TEST_PASS();
}

static int test_i2c_driver_validate(void)
{
    const SensorDriver_t *d = sensor_driver_find(SENSOR_I2C);
    TEST_ASSERT_NOT_NULL(d);

    TEST_ASSERT_TRUE(d->validate(-40.0f));
    TEST_ASSERT_TRUE(d->validate(25.0f));
    TEST_ASSERT_TRUE(d->validate(125.0f));
    TEST_ASSERT(!d->validate(-41.0f));
    TEST_ASSERT(!d->validate(126.0f));

    TEST_PASS();
}

static int test_driver_init_all(void)
{
    SensorConfig_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    uint8_t count = 0;
    const SensorDriver_t *all = sensor_driver_registry_get_all(&count);
    for (uint8_t i = 0; i < count; i++) {
        TEST_ASSERT_TRUE(all[i].init(&cfg));
    }

    TEST_PASS();
}

/* ================================================================ */
/* Edge Config Tests                                               */
/* ================================================================ */

static int test_edge_config_not_provisioned_initially(void)
{
    edge_config_init(NULL);
    TEST_ASSERT(!edge_config_is_provisioned());
    TEST_ASSERT_NULL(edge_config_get());
    TEST_PASS();
}

static int test_edge_config_provision(void)
{
    edge_config_init(NULL);

    EdgeNodeConfig_t cfg = make_test_config(42, SENSOR_DISTANCE);
    EdgeConfigResult_t r = edge_config_set(&cfg);
    TEST_ASSERT_EQUAL(EDGE_CONFIG_OK, r);
    TEST_ASSERT_TRUE(edge_config_is_provisioned());

    const EdgeNodeConfig_t *loaded = edge_config_get();
    TEST_ASSERT_NOT_NULL(loaded);
    TEST_ASSERT_EQUAL(42, loaded->node_id);
    TEST_ASSERT_EQUAL(SENSOR_DISTANCE, loaded->active_driver);

    TEST_PASS();
}

static int test_edge_config_rejects_unknown_driver(void)
{
    edge_config_init(NULL);

    EdgeNodeConfig_t cfg = make_test_config(1, (SensorType_t)99);
    EdgeConfigResult_t r = edge_config_set(&cfg);
    TEST_ASSERT_EQUAL(EDGE_CONFIG_ERR_DRIVER_NOT_FOUND, r);
    TEST_ASSERT(!edge_config_is_provisioned());

    TEST_PASS();
}

static int test_edge_config_storage_round_trip(void)
{
    s_storage_saved = false;
    s_storage_loaded = false;
    edge_config_init(&s_mock_storage);

    EdgeNodeConfig_t cfg = make_test_config(7, SENSOR_I2C);
    cfg.driver_config.pin_or_addr = 0x48;
    cfg.driver_config.scale_factor = 0.1f;
    cfg.driver_config.offset = -40.0f;
    EdgeConfigResult_t r = edge_config_set(&cfg);
    TEST_ASSERT_EQUAL(EDGE_CONFIG_OK, r);
    TEST_ASSERT_TRUE(s_storage_saved);

    edge_config_init(&s_mock_storage);
    TEST_ASSERT_TRUE(edge_config_is_provisioned());

    const EdgeNodeConfig_t *loaded = edge_config_get();
    TEST_ASSERT_NOT_NULL(loaded);
    TEST_ASSERT_EQUAL(7, loaded->node_id);
    TEST_ASSERT_EQUAL(SENSOR_I2C, loaded->active_driver);
    TEST_ASSERT_FLOAT_EQUAL(0.1f, loaded->driver_config.scale_factor);

    TEST_PASS();
}

static int test_edge_config_null_param(void)
{
    edge_config_init(NULL);
    EdgeConfigResult_t r = edge_config_set(NULL);
    TEST_ASSERT_EQUAL(EDGE_CONFIG_ERR_PARAM_NULL, r);
    TEST_PASS();
}

/* ================================================================ */
/* Edge Attestation Tests                                          */
/* ================================================================ */

static int test_edge_auth_init(void)
{
    EdgeAuthContext_t ctx;
    edge_auth_init(&ctx, 0x10);

    TEST_ASSERT_EQUAL(EDGE_AUTH_IDLE, edge_auth_get_state(&ctx));
    TEST_ASSERT(!edge_auth_is_authenticated(&ctx));

    TEST_PASS();
}

static int test_edge_auth_set_key(void)
{
    EdgeAuthContext_t ctx;
    edge_auth_init(&ctx, 0x10);

    uint8_t key[ATTESTATION_KEY_SIZE];
    memset(key, 0xAB, ATTESTATION_KEY_SIZE);

    TEST_ASSERT_TRUE(edge_auth_set_key(&ctx, key));
    TEST_ASSERT_EQUAL(EDGE_AUTH_IDLE, edge_auth_get_state(&ctx));

    TEST_PASS();
}

static int test_edge_auth_challenge_response_success(void)
{
    EdgeAuthContext_t ctx;
    edge_auth_init(&ctx, 0x10);

    uint8_t key[ATTESTATION_KEY_SIZE];
    memset(key, 0xAB, ATTESTATION_KEY_SIZE);
    edge_auth_set_key(&ctx, key);

    /* Create a challenge. */
    AttestationChallenge_t challenge;
    memset(&challenge, 0, sizeof(challenge));
    memset(challenge.challenge, 0xCD, ATTESTATION_CHALLENGE_SIZE);
    challenge.timestamp_ms = 1000;
    challenge.sender_id = 0x01;  /* Hub */

    /* Compute response. */
    AttestationResponse_t response;
    memset(&response, 0, sizeof(response));

    EdgeAttestResult_t r = edge_auth_respond(&ctx, &challenge, &response);
    TEST_ASSERT_EQUAL(EDGE_ATTEST_OK, r);
    TEST_ASSERT_EQUAL(EDGE_AUTH_AUTHENTICATED, edge_auth_get_state(&ctx));
    TEST_ASSERT_TRUE(edge_auth_is_authenticated(&ctx));

    /* Verify the response is non-zero (actual HMAC computed). */
    uint8_t nonzero = 0;
    for (int i = 0; i < ATTESTATION_HMAC_SIZE; i++) {
        nonzero |= response.response[i];
    }
    TEST_ASSERT(nonzero != 0);

    /* Verify responder_id matches. */
    TEST_ASSERT_EQUAL(0x10, response.responder_id);

    /* Verify timestamp echoed back. */
    TEST_ASSERT_EQUAL(1000, response.timestamp_ms);

    TEST_PASS();
}

static int test_edge_auth_response_verifiable_by_hub(void)
{
    /* Prove the Edge Node's response can be verified by the Hub's
     * attestation_verify() — proving the two sides are interoperable. */

    /* Provision the same key on both sides. */
    uint8_t key[ATTESTATION_KEY_SIZE];
    memset(key, 0xAB, ATTESTATION_KEY_SIZE);

    /* Register the key with the Hub's attestation system. */
    attestation_init();
    AttestationKeyRecord_t record;
    record.node_id = 0x10;
    memcpy(record.key, key, ATTESTATION_KEY_SIZE);
    record.active = true;
    attestation_register_key(&record);

    /* Edge Node computes response. */
    EdgeAuthContext_t ctx;
    edge_auth_init(&ctx, 0x10);
    edge_auth_set_key(&ctx, key);

    AttestationChallenge_t challenge;
    memset(&challenge, 0, sizeof(challenge));
    memset(challenge.challenge, 0xCD, ATTESTATION_CHALLENGE_SIZE);
    challenge.timestamp_ms = 1000;
    challenge.sender_id = 0x01;

    AttestationResponse_t response;
    edge_auth_respond(&ctx, &challenge, &response);

    /* Hub verifies. */
    AttestationResult_t verify_result = attestation_verify(&challenge, &response, 5000);
    TEST_ASSERT_EQUAL(ATTEST_OK, verify_result);

    TEST_PASS();
}

static int test_edge_auth_no_key_fails(void)
{
    EdgeAuthContext_t ctx;
    edge_auth_init(&ctx, 0x10);
    /* Don't set a key. */

    AttestationChallenge_t challenge;
    memset(&challenge, 0, sizeof(challenge));

    AttestationResponse_t response;
    EdgeAttestResult_t r = edge_auth_respond(&ctx, &challenge, &response);
    TEST_ASSERT_EQUAL(EDGE_ATTEST_ERR_NOT_READY, r);
    TEST_ASSERT_EQUAL(EDGE_AUTH_FAILED, edge_auth_get_state(&ctx));
    TEST_ASSERT(!edge_auth_is_authenticated(&ctx));

    TEST_PASS();
}

static int test_edge_auth_null_params(void)
{
    EdgeAuthContext_t ctx;
    edge_auth_init(&ctx, 0x10);

    AttestationChallenge_t challenge;
    AttestationResponse_t response;

    EdgeAttestResult_t r = edge_auth_respond(NULL, &challenge, &response);
    TEST_ASSERT_EQUAL(EDGE_ATTEST_ERR_PARAM_NULL, r);

    r = edge_auth_respond(&ctx, NULL, &response);
    TEST_ASSERT_EQUAL(EDGE_ATTEST_ERR_PARAM_NULL, r);

    r = edge_auth_respond(&ctx, &challenge, NULL);
    TEST_ASSERT_EQUAL(EDGE_ATTEST_ERR_PARAM_NULL, r);

    TEST_PASS();
}

static int test_edge_auth_wrong_key_detected(void)
{
    /* Edge Node has key 0xAB, Hub registered key 0xCD — should mismatch. */
    attestation_init();
    AttestationKeyRecord_t record;
    record.node_id = 0x10;
    memset(record.key, 0xCD, ATTESTATION_KEY_SIZE);
    record.active = true;
    attestation_register_key(&record);

    EdgeAuthContext_t ctx;
    edge_auth_init(&ctx, 0x10);
    uint8_t wrong_key[ATTESTATION_KEY_SIZE];
    memset(wrong_key, 0xAB, ATTESTATION_KEY_SIZE);
    edge_auth_set_key(&ctx, wrong_key);

    AttestationChallenge_t challenge;
    memset(&challenge, 0, sizeof(challenge));
    memset(challenge.challenge, 0xEF, ATTESTATION_CHALLENGE_SIZE);
    challenge.timestamp_ms = 2000;
    challenge.sender_id = 0x01;

    AttestationResponse_t response;
    edge_auth_respond(&ctx, &challenge, &response);

    /* Edge Node thinks it's authenticated (it computed a response). */
    TEST_ASSERT_TRUE(edge_auth_is_authenticated(&ctx));

    /* But Hub rejects it — HMAC mismatch. */
    AttestationResult_t verify = attestation_verify(&challenge, &response, 5000);
    TEST_ASSERT_EQUAL(ATTEST_ERR_HMAC_MISMATCH, verify);

    TEST_PASS();
}

/* ================================================================ */
/* End-to-End: Edge Node → RS-485 Frame → Gateway Parse             */
/* ================================================================ */

static int test_edge_node_distance_read_and_send(void)
{
    provision_edge(10, SENSOR_DISTANCE);
    sensor_driver_mock_set_distance(200.0f, false);

    s_last_wire_len = 0;
    s_send_return = true;
    edge_node_init(mock_rs485_send, EDGE_AUTH_UNGATED_TESTING_ONLY);  /* Test-only: no auth gating. */

    EdgeNodeResult_t result = edge_node_read_and_send(1000);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_TRUE(result.frame_sent);
    TEST_ASSERT_FLOAT_EQUAL(200.0f, result.value);
    TEST_ASSERT_EQUAL(10, result.node_id);
    TEST_ASSERT_EQUAL(1, result.gw_addr);

    /* Verify the wire frame is parseable by Gateway's RS-485 parser. */
    TEST_ASSERT(s_last_wire_len > 0);
    RS485Frame_t parsed;
    RS485ParseResult_t pr = rs485_parse(s_last_wire_buf, s_last_wire_len, &parsed);
    TEST_ASSERT_EQUAL(RS485_OK, pr);
    TEST_ASSERT_EQUAL(0x01, parsed.dest_addr);
    TEST_ASSERT_EQUAL(10, parsed.src_addr);
    TEST_ASSERT_EQUAL(6, parsed.payload_len);

    float decoded_value;
    memcpy(&decoded_value, &parsed.payload[2], 4);
    TEST_ASSERT_FLOAT_EQUAL(200.0f, decoded_value);

    TEST_PASS();
}

static int test_edge_node_analog_with_scale(void)
{
    provision_edge(20, SENSOR_ANALOG);
    EdgeNodeConfig_t cfg = make_test_config(20, SENSOR_ANALOG);
    cfg.driver_config.scale_factor = 3.3f / 4095.0f;
    cfg.driver_config.offset = 0.0f;
    cfg.gw_dest_addr = 0x01;
    edge_config_set(&cfg);

    sensor_driver_mock_set_adc(2048.0f, false);

    s_last_wire_len = 0;
    s_send_return = true;
    edge_node_init(mock_rs485_send, EDGE_AUTH_UNGATED_TESTING_ONLY);

    EdgeNodeResult_t result = edge_node_read_and_send(2000);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_TRUE(result.frame_sent);

    float expected = 2048.0f * (3.3f / 4095.0f);
    TEST_ASSERT_FLOAT_EQUAL(expected, result.value);

    TEST_PASS();
}

static int test_edge_node_i2c_read_and_send(void)
{
    provision_edge(30, SENSOR_I2C);
    sensor_driver_mock_set_i2c(23.5f, 50.0f, false);

    s_last_wire_len = 0;
    s_send_return = true;
    edge_node_init(mock_rs485_send, EDGE_AUTH_UNGATED_TESTING_ONLY);

    EdgeNodeResult_t result = edge_node_read_and_send(3000);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_TRUE(result.frame_sent);
    TEST_ASSERT_FLOAT_EQUAL(23.5f, result.value);

    TEST_PASS();
}

static int test_edge_node_not_provisioned(void)
{
    edge_config_init(NULL);
    s_last_wire_len = 0;
    edge_node_init(mock_rs485_send, EDGE_AUTH_UNGATED_TESTING_ONLY);

    EdgeNodeResult_t result = edge_node_read_and_send(1000);
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(!result.frame_sent);
    TEST_ASSERT_EQUAL(0, s_last_wire_len);

    TEST_PASS();
}

static int test_edge_node_driver_fault(void)
{
    provision_edge(10, SENSOR_DISTANCE);
    sensor_driver_mock_set_distance(0.0f, true);

    s_last_wire_len = 0;
    s_send_return = true;
    edge_node_init(mock_rs485_send, EDGE_AUTH_UNGATED_TESTING_ONLY);

    EdgeNodeResult_t result = edge_node_read_and_send(1000);
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(!result.frame_sent);

    TEST_PASS();
}

static int test_edge_node_send_failure(void)
{
    provision_edge(10, SENSOR_DISTANCE);
    sensor_driver_mock_set_distance(100.0f, false);

    s_last_wire_len = 0;
    s_send_return = false;
    edge_node_init(mock_rs485_send, EDGE_AUTH_UNGATED_TESTING_ONLY);

    EdgeNodeResult_t result = edge_node_read_and_send(1000);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT(!result.frame_sent);

    const EdgeNodeStats_t *stats = edge_node_get_stats();
    TEST_ASSERT_EQUAL(1, stats->reads_valid);
    TEST_ASSERT_EQUAL(1, stats->frames_failed);

    TEST_PASS();
}

static int test_edge_node_stats(void)
{
    provision_edge(10, SENSOR_DISTANCE);

    s_send_return = true;
    edge_node_init(mock_rs485_send, EDGE_AUTH_UNGATED_TESTING_ONLY);
    edge_node_reset_stats();

    sensor_driver_mock_set_distance(100.0f, false);
    edge_node_read_and_send(1000);

    sensor_driver_mock_set_distance(0.0f, true);
    edge_node_read_and_send(2000);

    sensor_driver_mock_set_distance(200.0f, false);
    edge_node_read_and_send(3000);

    const EdgeNodeStats_t *stats = edge_node_get_stats();
    TEST_ASSERT_EQUAL(3, stats->reads_total);
    TEST_ASSERT_EQUAL(2, stats->reads_valid);
    TEST_ASSERT_EQUAL(1, stats->reads_invalid);
    TEST_ASSERT_EQUAL(2, stats->frames_sent);

    TEST_PASS();
}

/* ================================================================ */
/* Attestation Gating Tests                                        */
/* ================================================================ */

static int test_edge_node_blocked_before_attestation(void)
{
    provision_edge(10, SENSOR_DISTANCE);
    sensor_driver_mock_set_distance(100.0f, false);

    /* Create and configure auth context — but do NOT authenticate. */
    EdgeAuthContext_t auth;
    edge_auth_init(&auth, 10);
    uint8_t key[ATTESTATION_KEY_SIZE];
    memset(key, 0xAB, ATTESTATION_KEY_SIZE);
    edge_auth_set_key(&auth, key);
    /* auth.state == EDGE_AUTH_IDLE (not authenticated). */

    s_last_wire_len = 0;
    s_send_return = true;
    edge_node_init(mock_rs485_send, &auth);

    EdgeNodeResult_t result = edge_node_read_and_send(1000);
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(!result.frame_sent);
    TEST_ASSERT_EQUAL(0, s_last_wire_len);

    const EdgeNodeStats_t *stats = edge_node_get_stats();
    TEST_ASSERT_EQUAL(1, stats->reads_total);
    TEST_ASSERT_EQUAL(1, stats->reads_invalid);

    TEST_PASS();
}

static int test_edge_node_proceeds_after_attestation(void)
{
    provision_edge(10, SENSOR_DISTANCE);
    sensor_driver_mock_set_distance(100.0f, false);

    /* Create, provision key, and authenticate. */
    EdgeAuthContext_t auth;
    edge_auth_init(&auth, 10);
    uint8_t key[ATTESTATION_KEY_SIZE];
    memset(key, 0xAB, ATTESTATION_KEY_SIZE);
    edge_auth_set_key(&auth, key);

    AttestationChallenge_t challenge;
    memset(&challenge, 0, sizeof(challenge));
    memset(challenge.challenge, 0xCD, ATTESTATION_CHALLENGE_SIZE);
    challenge.timestamp_ms = 1000;
    challenge.sender_id = 0x01;

    AttestationResponse_t response;
    edge_auth_respond(&auth, &challenge, &response);
    TEST_ASSERT_TRUE(edge_auth_is_authenticated(&auth));

    s_last_wire_len = 0;
    s_send_return = true;
    edge_node_init(mock_rs485_send, &auth);

    EdgeNodeResult_t result = edge_node_read_and_send(2000);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_TRUE(result.frame_sent);
    TEST_ASSERT_FLOAT_EQUAL(100.0f, result.value);

    TEST_PASS();
}

static int test_edge_node_null_auth_context_is_error(void)
{
    /* NULL auth_ctx is now a parameter error — data flow rejected.
     * This closes the downgrade-bypass gap from Phase 14.1. */
    provision_edge(10, SENSOR_DISTANCE);
    sensor_driver_mock_set_distance(100.0f, false);

    s_last_wire_len = 0;
    s_send_return = true;
    edge_node_init(mock_rs485_send, NULL);  /* NULL = error */

    EdgeNodeResult_t result = edge_node_read_and_send(1000);
    TEST_ASSERT(!result.valid);
    TEST_ASSERT(!result.frame_sent);
    TEST_ASSERT_EQUAL(0, s_last_wire_len);

    const EdgeNodeStats_t *stats = edge_node_get_stats();
    TEST_ASSERT_EQUAL(1, stats->reads_invalid);

    TEST_PASS();
}

static int test_edge_node_explicit_disabled_sentinel_still_works(void)
{
    /* EDGE_AUTH_UNGATED_TESTING_ONLY explicitly disables auth gating
     * for test harness use. This is the ONLY way to bypass auth —
     * the sentinel name makes the bypass visible at the call site. */
    provision_edge(10, SENSOR_DISTANCE);
    sensor_driver_mock_set_distance(100.0f, false);

    s_last_wire_len = 0;
    s_send_return = true;
    edge_node_init(mock_rs485_send, EDGE_AUTH_UNGATED_TESTING_ONLY);

    EdgeNodeResult_t result = edge_node_read_and_send(1000);
    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_TRUE(result.frame_sent);

    TEST_PASS();
}

/* ================================================================ */
/* Gateway Relay Transparency Test                                 */
/* ================================================================ */

static int test_gateway_relay_attestation_frame_transparently(void)
{
    /* Prove that an attestation challenge/response frame passed through
     * the Gateway's RS-485 relay is NOT interpreted or modified by the
     * Gateway — it arrives at the Hub exactly as the Edge Node sent it.
     *
     * The Gateway is a transparent relay for Edge↔Hub attestation:
     * it reads RS-485 bytes from the Edge side and forwards them to
     * the Hub side without any attestation-specific processing.
     * (Gateway attestation is a separate, independent relationship.) */

    /* 1. Edge Node computes attestation response. */
    EdgeAuthContext_t auth;
    edge_auth_init(&auth, 0x10);
    uint8_t key[ATTESTATION_KEY_SIZE];
    memset(key, 0xAB, ATTESTATION_KEY_SIZE);
    edge_auth_set_key(&auth, key);

    AttestationChallenge_t challenge;
    memset(&challenge, 0, sizeof(challenge));
    memset(challenge.challenge, 0xCD, ATTESTATION_CHALLENGE_SIZE);
    challenge.timestamp_ms = 1000;
    challenge.sender_id = 0x01;

    AttestationResponse_t response;
    edge_auth_respond(&auth, &challenge, &response);

    /* 2. Encode the response as an RS-485 frame (what the Edge Node
     * would send to the Gateway). */
    uint8_t payload[32 + 8 + 1];  /* response HMAC + timestamp + responder_id */
    memcpy(payload, response.response, ATTESTATION_HMAC_SIZE);
    memcpy(&payload[ATTESTATION_HMAC_SIZE], &response.timestamp_ms, 8);
    payload[ATTESTATION_HMAC_SIZE + 8] = response.responder_id;

    RS485Frame_t frame;
    rs485_create_frame(&frame, 0x01, 0x10, payload, sizeof(payload));

    uint8_t wire_buf[256];
    size_t wire_len = 0;
    TEST_ASSERT_TRUE(rs485_encode(&frame, wire_buf, sizeof(wire_buf), &wire_len));

    /* 3. Simulate Gateway relay: parse the frame (Gateway reads it),
     * then re-encode it (Gateway forwards it to Hub). The Gateway
     * does NOT interpret the payload — it's just bytes to relay. */
    RS485Frame_t relayed;
    RS485ParseResult_t pr = rs485_parse(wire_buf, wire_len, &relayed);
    TEST_ASSERT_EQUAL(RS485_OK, pr);

    /* Re-encode as if forwarding to Hub. */
    uint8_t relay_buf[256];
    size_t relay_len = 0;
    TEST_ASSERT_TRUE(rs485_encode(&relayed, relay_buf, sizeof(relay_buf), &relay_len));

    /* 4. Hub parses the relayed frame. */
    RS485Frame_t hub_received;
    pr = rs485_parse(relay_buf, relay_len, &hub_received);
    TEST_ASSERT_EQUAL(RS485_OK, pr);

    /* 5. Verify the payload is byte-identical to what the Edge Node sent. */
    TEST_ASSERT_EQUAL(sizeof(payload), hub_received.payload_len);
    TEST_ASSERT_MEM_EQUAL(payload, hub_received.payload, sizeof(payload));

    /* 6. Hub can now extract and verify the attestation response. */
    AttestationResponse_t hub_response;
    memcpy(hub_response.response, hub_received.payload, ATTESTATION_HMAC_SIZE);
    memcpy(&hub_response.timestamp_ms, &hub_received.payload[ATTESTATION_HMAC_SIZE], 8);
    hub_response.responder_id = hub_received.payload[ATTESTATION_HMAC_SIZE + 8];

    /* Register the key with Hub's attestation system and verify. */
    attestation_init();
    AttestationKeyRecord_t record;
    record.node_id = 0x10;
    memcpy(record.key, key, ATTESTATION_KEY_SIZE);
    record.active = true;
    attestation_register_key(&record);

    AttestationResult_t verify = attestation_verify(&challenge, &hub_response, 5000);
    TEST_ASSERT_EQUAL(ATTEST_OK, verify);

    TEST_PASS();
}

/* ================================================================ */
/* RAM Budget Measurement                                          */
/* ================================================================ */

static int test_ram_budget_measurement(void)
{
    /*
     * Architecture Section 8 RAM budget (8KB PY32F030x8: 8192 bytes):
     *
     * RTOS/stack overhead:            ~1.5KB  (1536 bytes)
     * RS-485/wireless buffer:         ~1KB    (1024 bytes)
     * Sensor driver working buffer:   ~0.5KB  (512 bytes)
     * Config struct (in-RAM copy):    ~0.1KB  (102 bytes)
     * Attestation/crypto working:     ~0.5KB  (512 bytes)
     * Application state:              ~0.5KB  (512 bytes)
     * -----------------------------------------
     * Headroom remaining:             ~4KB    (4006 bytes)
     *
     * The above is the architecture's ESTIMATE. We now measure the
     * ACTUAL footprint of the implemented Edge Node components and
     * compare against the budget.
     */

    printf("\n    --- RAM Budget Measurement (8KB PY32) ---\n");

    /* Measured struct sizes (actual, not estimated). */
    size_t sensor_driver_t   = sizeof(SensorDriver_t);
    size_t sensor_config_t   = sizeof(SensorConfig_t);
    size_t edge_config_t     = sizeof(EdgeNodeConfig_t);
    size_t edge_result_t     = sizeof(EdgeNodeResult_t);
    size_t edge_stats_t      = sizeof(EdgeNodeStats_t);
    size_t edge_auth_t       = sizeof(EdgeAuthContext_t);
    size_t rs485_frame_t     = sizeof(RS485Frame_t);
    size_t attestation_chg_t = sizeof(AttestationChallenge_t);
    size_t attestation_rsp_t = sizeof(AttestationResponse_t);

    printf("    SensorDriver_t:              %4zu bytes\n", sensor_driver_t);
    printf("    SensorConfig_t:              %4zu bytes\n", sensor_config_t);
    printf("    EdgeNodeConfig_t:            %4zu bytes\n", edge_config_t);
    printf("    EdgeNodeResult_t:            %4zu bytes\n", edge_result_t);
    printf("    EdgeNodeStats_t:             %4zu bytes\n", edge_stats_t);
    printf("    EdgeAuthContext_t:           %4zu bytes\n", edge_auth_t);
    printf("    RS485Frame_t:                %4zu bytes\n", rs485_frame_t);
    printf("    AttestationChallenge_t:      %4zu bytes\n", attestation_chg_t);
    printf("    AttestationResponse_t:       %4zu bytes\n", attestation_rsp_t);

    /* Driver registry: array of 3 const pointers. */
    size_t registry_ptrs = sensor_driver_registry_count() * sizeof(void*);
    printf("    Driver registry (3 ptrs):    %4zu bytes\n", registry_ptrs);

    printf("    ---\n");

    /*
     * RAM usage breakdown (stack-allocated, not global):
     *
     * The Edge Node firmware's static/global memory includes:
     *   - Driver registry pointers:     registry_ptrs bytes (global const)
     *   - EdgeAuthContext_t:            edge_auth_t bytes (one per node)
     *   - EdgeNodeConfig_t:            edge_config_t bytes (one per node)
     *   - EdgeNodeStats_t:             edge_stats_t bytes (one per node)
     *   - Sensor mock state:           ~12 bytes (globals in sensor_driver.c)
     *   - Edge config module state:    ~edge_config_t + sizeof(ptr) bytes
     *   - Edge node module state:      ~sizeof(ptr)*2 + edge_stats_t bytes
     *   - Edge attestation module:     0 bytes global (context is caller-allocated)
     *
     * Plus stack-allocated temporaries per read cycle:
     *   - RS485Frame_t:                rs485_frame_t bytes
     *   - Wire buffer:                 ~136 bytes (RS485_MAX_FRAME_PAYLOAD + 8)
     *   - EdgeNodeResult_t:            edge_result_t bytes
     *   - AttestationChallenge_t:      attestation_chg_t bytes (during auth only)
     *   - AttestationResponse_t:       attestation_rsp_t bytes (during auth only)
     */

    size_t globals = registry_ptrs + edge_auth_t + edge_config_t + edge_stats_t + 12 + 16;
    size_t stack_per_cycle = rs485_frame_t + 136 + edge_result_t;
    size_t stack_auth_cycle = attestation_chg_t + attestation_rsp_t;
    size_t total_static = globals;
    size_t total_peak_stack = stack_per_cycle + stack_auth_cycle;

    printf("    Static/global memory:        %4zu bytes\n", total_static);
    printf("    Peak stack (auth+send):      %4zu bytes\n", total_peak_stack);

    /*
     * Section 8 budget allocation for these categories:
     *   Sensor driver working buffer:  512 bytes  → our static + stack: %zu
     *   Config struct (in-RAM copy):   102 bytes  → EdgeNodeConfig_t: %zu
     *   Attestation/crypto working:    512 bytes  → EdgeAuthContext_t + stack: %zu
     *   Application state:             512 bytes  → stats + result + registry: %zu
     *   RS-485 buffer:                1024 bytes  → wire_buf: 136 bytes
     */
    printf("    ---\n");
    printf("    Section 8 budget categories vs actual:\n");
    printf("      Sensor driver working:     %4d alloc, %4zu actual\n", 512, total_static);
    printf("      Config struct:             %4d alloc, %4zu actual\n", 102, edge_config_t);
    printf("      Attestation/crypto:        %4d alloc, %4zu actual\n", 512, edge_auth_t + stack_auth_cycle);
    printf("      Application state:         %4d alloc, %4zu actual\n", 512, edge_stats_t + edge_result_t + registry_ptrs);
    printf("      RS-485 buffer:             %4d alloc, %4zu actual\n", 1024, (size_t)136);

    /* The 8KB total includes RTOS/stack overhead (~1536 bytes) which we
     * don't control. Subtract that from the budget. */
    size_t rtos_overhead = 1536;
    size_t available_for_firmware = 8192 - rtos_overhead;  /* 6656 bytes */
    size_t firmware_used = total_static + total_peak_stack;
    size_t headroom = (available_for_firmware > firmware_used)
                    ? available_for_firmware - firmware_used : 0;

    printf("    ---\n");
    printf("    TOTAL: 8192 bytes (8KB PY32)\n");
    printf("    RTOS/stack overhead:         %4zu bytes (architecture estimate)\n", rtos_overhead);
    printf("    Available for firmware:      %4zu bytes\n", available_for_firmware);
    printf("    Edge Node firmware used:     %4zu bytes (static + peak stack)\n", firmware_used);
    printf("    Headroom:                    %zu bytes\n", headroom);
    printf("    --- END RAM Budget ---\n");

    /* Verify: Edge Node firmware must fit within available budget. */
    TEST_ASSERT(firmware_used <= available_for_firmware);

    TEST_PASS();
}

/* ================================================================ */
/* Test runner                                                     */
/* ================================================================ */

int main(void)
{
    static int _total = 0, _passed = 0, _failed = 0;

    printf("\n========================================\n");
    printf("Edge Node Tests (Phase 14 + 14.1)\n");
    printf("========================================\n\n");

    printf("--- Driver Registry ---\n");
    RUN_TEST(test_driver_registry_count);
    RUN_TEST(test_driver_registry_find_distance);
    RUN_TEST(test_driver_registry_find_analog);
    RUN_TEST(test_driver_registry_find_i2c);
    RUN_TEST(test_driver_registry_find_unknown);
    RUN_TEST(test_driver_registry_iteration);

    printf("\n--- Individual Drivers ---\n");
    RUN_TEST(test_distance_driver_read);
    RUN_TEST(test_distance_driver_validate);
    RUN_TEST(test_analog_driver_read);
    RUN_TEST(test_analog_driver_validate);
    RUN_TEST(test_i2c_driver_read);
    RUN_TEST(test_i2c_driver_validate);
    RUN_TEST(test_driver_init_all);

    printf("\n--- Edge Config ---\n");
    RUN_TEST(test_edge_config_not_provisioned_initially);
    RUN_TEST(test_edge_config_provision);
    RUN_TEST(test_edge_config_rejects_unknown_driver);
    RUN_TEST(test_edge_config_storage_round_trip);
    RUN_TEST(test_edge_config_null_param);

    printf("\n--- Edge Attestation ---\n");
    RUN_TEST(test_edge_auth_init);
    RUN_TEST(test_edge_auth_set_key);
    RUN_TEST(test_edge_auth_challenge_response_success);
    RUN_TEST(test_edge_auth_response_verifiable_by_hub);
    RUN_TEST(test_edge_auth_no_key_fails);
    RUN_TEST(test_edge_auth_null_params);
    RUN_TEST(test_edge_auth_wrong_key_detected);

    printf("\n--- End-to-End Edge Node ---\n");
    RUN_TEST(test_edge_node_distance_read_and_send);
    RUN_TEST(test_edge_node_analog_with_scale);
    RUN_TEST(test_edge_node_i2c_read_and_send);
    RUN_TEST(test_edge_node_not_provisioned);
    RUN_TEST(test_edge_node_driver_fault);
    RUN_TEST(test_edge_node_send_failure);
    RUN_TEST(test_edge_node_stats);

    printf("\n--- Attestation Gating ---\n");
    RUN_TEST(test_edge_node_blocked_before_attestation);
    RUN_TEST(test_edge_node_proceeds_after_attestation);
    RUN_TEST(test_edge_node_null_auth_context_is_error);
    RUN_TEST(test_edge_node_explicit_disabled_sentinel_still_works);

    printf("\n--- Gateway Relay Transparency ---\n");
    RUN_TEST(test_gateway_relay_attestation_frame_transparently);

    printf("\n--- RAM Budget ---\n");
    RUN_TEST(test_ram_budget_measurement);

    PRINT_TEST_SUMMARY();
}
