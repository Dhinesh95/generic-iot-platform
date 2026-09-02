/**
 * @file test_full_pipeline_integration.c
 * @brief Phase 15: Full Edge→Gateway→Hub Integration Test (Home Domain)
 *
 * Drives data through all three tiers in one continuous flow:
 *   1. Edge Node attests to Hub (via Gateway relay)
 *   2. Edge Node reads sensor, encodes RS-485 frame
 *   3. Gateway receives RS-485, applies delta-filter/cache, batches, signs
 *   4. Hub receives batch frame, verifies Gateway auth, decodes entries,
 *      feeds each reading through the Home profile's sensor validation gate,
 *      then through rule_engine_evaluate()
 *
 * Also includes:
 *   - Negative path: Edge Node that fails attestation never reaches Hub
 *   - Negative path: Tampered Gateway batch frame rejected by Hub
 *   - Interface mismatch report: findings from wiring tiers together
 */

#include "test_helpers/test_utils.h"
#include "../firmware/edge/sensor_driver.h"
#include "../firmware/edge/edge_config.h"
#include "../firmware/edge/edge_node.h"
#include "../firmware/edge/edge_attestation.h"
#include "../firmware/shared/rs485_protocol.h"
#include "../firmware/shared/attestation.h"
#include "../firmware/shared/rule_engine_core.h"
#include "../firmware/shared/sensor_validation.h"
#include "../firmware/shared/actuator_failsafe.h"
#include "../firmware/shared/audit_log.h"
#include "../firmware/gateway/stateful_cache.h"
#include "../firmware/gateway/delta_filter.h"
#include "../firmware/gateway/batch_forwarder.h"
#include "../firmware/gateway/gateway_auth.h"
#include "../firmware/profiles/home/home_rules.h"
#include "../firmware/profiles/home/home_validation_bounds.h"
#include "../firmware/profiles/home/home_failsafe.h"
#include <string.h>
#include <math.h>

/* ================================================================
 * Constants
 * ================================================================ */

#define EDGE_NODE_ID       0x01
#define GATEWAY_NODE_ID    0xF0
#define HUB_NODE_ID        0x01
#define GW_DEST_ADDR       0x01

/* Edge node config: metric_id = HOME_METRIC_SECURITY_STATE (10).
 * Sensor: SENSOR_ANALOG (ADC). Physical value = raw * scale + offset.
 * We want physical_value = 1.0 to trigger the CRITICAL rule.
 * Set raw ADC = 2048, scale = 1.0/2048.0, offset = 0.0 → physical = 1.0. */
#define EDGE_METRIC_ID     HOME_METRIC_SECURITY_STATE
#define EDGE_SENSOR_TYPE   SENSOR_ANALOG
#define TARGET_VALUE       1.0f
#define MOCK_ADC_RAW       2048.0f
#define SCALE_FACTOR       (1.0f / 2048.0f)
#define OFFSET             0.0f

/* Pre-shared keys (identical on Edge↔Hub and Gateway↔Hub for simplicity). */
static const uint8_t s_edge_key[ATTESTATION_KEY_SIZE] = {
    0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8,
    0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0,
    0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF, 0x00
};

static const uint8_t s_gw_session_key[GATEWAY_HMAC_SIZE] = {
    0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8,
    0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0,
    0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8,
    0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0
};

/* ================================================================
 * Mock RS-485 Transport (Edge → Gateway)
 * ================================================================ */

static uint8_t  s_edge_wire_buf[256];
static size_t   s_edge_wire_len = 0;
static bool     s_edge_send_ok = true;

static bool mock_rs485_send(const uint8_t *data, size_t len)
{
    if (len > sizeof(s_edge_wire_buf)) return false;
    memcpy(s_edge_wire_buf, data, len);
    s_edge_wire_len = len;
    return s_edge_send_ok;
}

/* ================================================================
 * Mock Radio Transport (Gateway → Hub)
 * ================================================================ */

static uint8_t  s_gw_batch_buf[BATCH_FRAME_MAX_SIZE];
static size_t   s_gw_batch_len = 0;
static bool     s_gw_send_ok = true;

static bool mock_radio_send(const uint8_t *data, uint8_t len)
{
    size_t copy = len;
    if (copy > sizeof(s_gw_batch_buf)) copy = sizeof(s_gw_batch_buf);
    memcpy(s_gw_batch_buf, data, copy);
    s_gw_batch_len = copy;
    return s_gw_send_ok;
}

/* ================================================================
 * Hub-side Gate State (tracks whether rule engine was called)
 * ================================================================ */

static uint8_t s_hub_rules_triggered = 0;
static uint16_t s_last_triggered_rule_id = 0;
static uint8_t s_last_triggered_actuator_id = 0;

/* ================================================================
 * Hub-side Edge Node Trust Tracker (Phase 15.1)
 * ================================================================ */

static EdgeNodeTracker_t s_edge_tracker;

/* ================================================================
 * Mock timestamp for edge tracker expiry checks
 * ================================================================ */

static uint64_t s_current_ms = 0;

/* ================================================================
 * Helper: Reset all tier state to clean slate
 * ================================================================ */

static void reset_all_state(void)
{
    /* Edge */
    memset(s_edge_wire_buf, 0, sizeof(s_edge_wire_buf));
    s_edge_wire_len = 0;
    s_edge_send_ok = true;

    /* Gateway */
    memset(s_gw_batch_buf, 0, sizeof(s_gw_batch_buf));
    s_gw_batch_len = 0;
    s_gw_send_ok = true;

    /* Hub */
    s_hub_rules_triggered = 0;
    s_last_triggered_rule_id = 0;
    s_last_triggered_actuator_id = 0;
    s_current_ms = 1000;  /* Default timestamp */

    /* Subsystem inits */
    attestation_init();
    edge_tracker_init(&s_edge_tracker, EDGE_NODE_ATTEST_EXPIRY_MS);
    sensor_validation_init();
    rule_engine_init();
    cache_init();
    delta_filter_init();
    audit_log_init();
}

/* ================================================================
 * Helper: Provision Edge Node (Home profile)
 * ================================================================ */

static EdgeAuthContext_t s_edge_auth;

static void provision_edge_node(void)
{
    /* Edge config: node_id=0x01, SENSOR_ANALOG, metric=10, scale for physical=1.0 */
    edge_config_init(NULL);
    EdgeNodeConfig_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.node_id = EDGE_NODE_ID;
    cfg.active_driver = EDGE_SENSOR_TYPE;
    cfg.metric_id = EDGE_METRIC_ID;
    cfg.driver_config.pin_or_addr = 0x00;
    cfg.driver_config.scale_factor = SCALE_FACTOR;
    cfg.driver_config.offset = OFFSET;
    cfg.driver_config.sample_count = 1;
    cfg.gw_dest_addr = GW_DEST_ADDR;
    cfg.valid = true;
    EdgeConfigResult_t r = edge_config_set(&cfg);
    (void)r;

    /* Edge attestation: init + key provision (no challenge yet) */
    edge_auth_init(&s_edge_auth, EDGE_NODE_ID);
    edge_auth_set_key(&s_edge_auth, s_edge_key);
}

/* ================================================================
 * Helper: Edge Node attests to Hub (challenge-response)
 * ================================================================ */

static bool edge_attest_to_hub(void)
{
    /* Register Edge's key with Hub's attestation system */
    AttestationKeyRecord_t record;
    record.node_id = EDGE_NODE_ID;
    memcpy(record.key, s_edge_key, ATTESTATION_KEY_SIZE);
    record.active = true;
    attestation_register_key(&record);

    /* Hub creates challenge */
    AttestationChallenge_t challenge;
    memset(&challenge, 0, sizeof(challenge));
    memset(challenge.challenge, 0xAB, ATTESTATION_CHALLENGE_SIZE);
    challenge.timestamp_ms = 1000;
    challenge.sender_id = HUB_NODE_ID;

    /* Edge computes response */
    AttestationResponse_t response;
    memset(&response, 0, sizeof(response));
    EdgeAttestResult_t r = edge_auth_respond(&s_edge_auth, &challenge, &response);
    if (r != EDGE_ATTEST_OK) return false;

    /* Hub verifies */
    AttestationResult_t verify = attestation_verify(&challenge, &response, 5000);
    if (verify != ATTEST_OK) return false;

    /* Phase 15.1: Record this Edge Node as trusted in the Hub's tracker. */
    edge_tracker_attest(&s_edge_tracker, &challenge, &response, s_current_ms);
    return true;
}

/* ================================================================
 * Helper: Gateway provisioning + attestation
 * ================================================================ */

static GatewayAuthContext_t s_gw_auth;

static void provision_gateway(void)
{
    gateway_auth_init(&s_gw_auth, GATEWAY_NODE_ID, HUB_NODE_ID, s_gw_session_key);
    /* Simulate Gateway attestation complete */
    gateway_set_state(&s_gw_auth, GW_AUTH_AUTHENTICATED);

    /* Wire Gateway batch forwarder */
    batch_forwarder_init(mock_radio_send);
    batch_forwarder_set_auth(&s_gw_auth);

    /* Configure delta filter for our metric (wide threshold to always pass) */
    delta_filter_set_threshold(EDGE_METRIC_ID, 0.5f);
}

/* ================================================================
 * Helper: Hub-side Gateway tracker
 * ================================================================ */

static GatewayGatewayTracker_t s_hub_gw_tracker;

static void provision_hub_gateway_tracker(void)
{
    gateway_tracker_init(&s_hub_gw_tracker, GATEWAY_NODE_ID, s_gw_session_key);
    s_hub_gw_tracker.authenticated = true;
}

/* ================================================================
 * Helper: Simulate Gateway receiving Edge's RS-485 frame
 *
 * This bridges Tier 1 → Tier 2: parse RS-485, extract payload,
 * apply delta filter, write to cache.
 *
 * INTERFACE NOTE: Edge sends node_id as uint8 (1 byte in RS-485 payload),
 * but Gateway cache uses uint16_t. Widen is safe.
 * ================================================================ */

static void gateway_receive_edge_frame(const uint8_t *wire, size_t wire_len)
{
    RS485Frame_t frame;
    RS485ParseResult_t pr = rs485_parse(wire, wire_len, &frame);
    if (pr != RS485_OK) return;

    /* Edge payload: [node_id(1)] [metric_id(1)] [value(4, float LE)] */
    if (frame.payload_len < 6) return;

    uint8_t  node_id_u8  = frame.payload[0];
    uint8_t  metric_id   = frame.payload[1];
    float    value;
    memcpy(&value, &frame.payload[2], 4);

    /* Widen node_id to uint16_t for Gateway cache/delta-filter/batch.
     * Safe: Edge originals are uint8 (0-255), upper byte = 0. */
    uint16_t node_id = (uint16_t)node_id_u8;

    /* Apply delta filter */
    DeltaFilterResult_t dr = delta_filter_evaluate(node_id, metric_id, value);
    if (dr == DELTA_FIRST_READ || dr == DELTA_FORWARD) {
        cache_write(node_id, metric_id, value);
    }
}

/* ================================================================
 * Helper: Gateway flushes batch frame
 * ================================================================ */

static void gateway_flush_batch(void)
{
    s_gw_batch_len = 0;
    batch_forwarder_flush();
}

/* ================================================================
 * Helper: Hub receives and processes batch frame
 *
 * INTERFACE NOTE: BatchFrameEntry_t.node_id is uint16_t, but
 * SensorReading_t.node_id is uint8_t. Narrowing is safe here
 * because the original Edge node_id is uint8.
 * ================================================================ */

static uint8_t hub_process_batch_frame(void)
{
    BatchFrameHeader_t header;
    BatchFrameEntry_t entries[BATCH_MAX_ENTRIES];
    uint8_t entry_count = 0;

    GatewayFrameResult_t r = gateway_ingest_frame(
        &s_hub_gw_tracker, &s_edge_tracker, s_gw_batch_buf, s_gw_batch_len,
        &header, entries, BATCH_MAX_ENTRIES, &entry_count);

    if (r != GW_FRAME_OK) return 0;

    /* Process each entry through Home profile's sensor validation + rule engine */
    const DomainProfileVTable_t *vtable = home_profile_get_vtable();
    uint8_t total_triggered = 0;

    for (uint8_t i = 0; i < entry_count; i++) {
        /* Narrow uint16_t → uint8_t (safe: upper byte is 0) */
        SensorReading_t reading;
        reading.node_id    = (uint8_t)entries[i].node_id;
        reading.metric_id  = entries[i].metric_id;
        reading.value      = entries[i].value;
        reading.timestamp_ms = 1000;

        /* Sensor validation gate (Home profile vtable) */
        if (!vtable->validateSensorReading(&reading)) {
            continue;
        }

        /* Also run through sensorValidate() with Home bounds (if available).
         * NOTE: HOME_METRIC_SECURITY_STATE (10) has NO bounds in
         * home_get_validation_bounds() — it returns NULL. This means
         * the standalone sensorValidate() CANNOT validate metric 10.
         * The vtable's validateSensorReading() is the functional gate.
         * This is an INTERFACE FINDING (see report). */
        const SensorValidationBounds_t *bounds = home_get_validation_bounds(reading.metric_id);
        if (bounds != NULL) {
            SensorHistory_t *hist = home_get_metric_history(reading.metric_id);
            if (hist != NULL) {
                SensorValidationResult_t svr = sensorValidate(
                    reading.node_id, reading.metric_id, reading.value,
                    bounds, hist, 1000);
                if (svr != SENSOR_VALID) {
                    continue;  /* Out of bounds — skip this reading */
                }
                sensor_history_record(hist, reading.node_id, reading.metric_id,
                                      reading.value, 1000);
            }
        }

        /* Rule engine evaluation */
        RuleEvaluationResult_t results[RULE_ENGINE_MAX_ACTIONS];
        uint8_t triggered = rule_engine_evaluate(vtable, &reading,
                                                 results, RULE_ENGINE_MAX_ACTIONS);
        total_triggered += triggered;

        if (triggered > 0) {
            s_last_triggered_rule_id = results[0].rule_id;
            s_last_triggered_actuator_id = results[0].actuator_id;
        }
    }

    s_hub_rules_triggered = total_triggered;
    return total_triggered;
}

/* ================================================================
 * TEST 1: Full Happy Path
 *
 * Edge attests → Edge reads sensor → Gateway relays → Hub evaluates rules
 * Home CRITICAL rule (door lock) fires when security state >= 1.0
 * ================================================================ */

static int test_full_pipeline_happy_path(void)
{
    reset_all_state();

    /* --- Step 1: Provision all three tiers --- */
    provision_edge_node();
    provision_gateway();
    provision_hub_gateway_tracker();

    /* --- Step 2: Edge attests to Hub (via Gateway relay) --- */
    bool attested = edge_attest_to_hub();
    TEST_ASSERT(attested);
    TEST_ASSERT(edge_auth_is_authenticated(&s_edge_auth));

    /* --- Step 3: Edge reads sensor, encodes RS-485 frame --- */
    sensor_driver_mock_set_adc(MOCK_ADC_RAW, false);
    s_edge_wire_len = 0;
    edge_node_init(mock_rs485_send, &s_edge_auth);

    EdgeNodeResult_t edge_result = edge_node_read_and_send(1000);
    TEST_ASSERT(edge_result.valid);
    TEST_ASSERT(edge_result.frame_sent);
    TEST_ASSERT(edge_result.node_id == EDGE_NODE_ID);
    /* physical_value = 2048 * (1/2048) + 0 = 1.0 */
    TEST_ASSERT(fabsf(edge_result.value - TARGET_VALUE) < 0.001f);
    TEST_ASSERT(s_edge_wire_len > 0);

    /* Verify RS-485 frame is parseable */
    RS485Frame_t parsed;
    RS485ParseResult_t pr = rs485_parse(s_edge_wire_buf, s_edge_wire_len, &parsed);
    TEST_ASSERT(pr == RS485_OK);
    TEST_ASSERT(parsed.payload_len == 6);

    /* --- Step 4: Gateway receives RS-485, applies delta-filter/cache --- */
    gateway_receive_edge_frame(s_edge_wire_buf, s_edge_wire_len);

    /* Verify cache has the entry */
    float cached_val = 0;
    TEST_ASSERT(cache_read(EDGE_NODE_ID, EDGE_METRIC_ID, &cached_val));
    TEST_ASSERT(fabsf(cached_val - TARGET_VALUE) < 0.001f);
    TEST_ASSERT(cache_is_dirty(EDGE_NODE_ID, EDGE_METRIC_ID));

    /* --- Step 5: Gateway flushes signed batch frame --- */
    gateway_flush_batch();
    TEST_ASSERT(s_gw_batch_len > 0);

    /* Verify dirty flag cleared after successful send */
    TEST_ASSERT(!cache_is_dirty(EDGE_NODE_ID, EDGE_METRIC_ID));

    /* --- Step 6: Hub receives batch, verifies Gateway auth, processes --- */
    uint8_t triggered = hub_process_batch_frame();
    TEST_ASSERT(triggered >= 1);

    /* Verify the CRITICAL rule fired */
    TEST_ASSERT_EQUAL(HOME_RULE_DOOR_LOCK_CRITICAL, s_last_triggered_rule_id);
    TEST_ASSERT_EQUAL(HOME_ACTUATOR_DOOR_LOCK, s_last_triggered_actuator_id);

    printf("\n    [FINDING] Full pipeline happy path: Edge(ANALOG/ADC=2048) "
           "→ RS-485(6B) → GW(delta+cache+batch+sign) → Hub(verify+decode+validate+rule) "
           "→ CRITICAL rule %d triggered, actuator %d\n",
           s_last_triggered_rule_id, s_last_triggered_actuator_id);

    TEST_PASS();
}

/* ================================================================
 * TEST 2: Negative Path — Edge Node Fails Attestation (Phase 15.1)
 *
 * An Edge Node whose attestation the Hub never verified (wrong key,
 * HMAC mismatch) must have its data REJECTED at the Hub ingestion
 * gate, never reaching sensor validation or the rule engine.
 *
 * This closes the T1 threat gap identified in Phase 15:
 * - Before 15.1: Hub trusted all entries in a Gateway-signed batch,
 *   regardless of whether the Edge Node had been attested.
 * - After 15.1: gateway_ingest_frame() filters entries by the
 *   EdgeNodeTracker — only entries from attested nodes pass through.
 *
 * WHY THE ORIGINAL PHASE 15 FRAMING WAS WRONG:
 * Phase 15's test_negative_edge_attestation_failure concluded that
 * "data still reaches the Hub because Gateway auth is independent of
 * Edge auth — this is architecturally correct." This was correct about
 * the two trust domains being independent, but wrong in concluding
 * that the Hub should therefore accept unattested Edge data. The
 * Gateway HMAC authenticates the RELAY (proving the frame hasn't been
 * tampered in transit), but the Hub must ALSO verify that the
 * SOURCE NODE of each entry is trusted. These are orthogonal gates:
 *   1. Is the relay trustworthy? (Gateway HMAC — already enforced)
 *   2. Is the source node trustworthy? (Edge attestation — NOW enforced)
 * Both must pass for data to reach the rule engine.
 * ================================================================ */

static int test_hub_rejects_data_from_unattested_edge_node(void)
{
    reset_all_state();

    /* --- Provision Edge (but with WRONG key) --- */
    provision_edge_node();

    /* Override with a different key than what Hub expects */
    uint8_t wrong_key[ATTESTATION_KEY_SIZE];
    memset(wrong_key, 0xFF, ATTESTATION_KEY_SIZE);
    edge_auth_set_key(&s_edge_auth, wrong_key);

    /* Register the CORRECT key with Hub (not the Edge's wrong key) */
    AttestationKeyRecord_t record;
    record.node_id = EDGE_NODE_ID;
    memcpy(record.key, s_edge_key, ATTESTATION_KEY_SIZE);
    record.active = true;
    attestation_register_key(&record);

    /* Hub creates challenge */
    AttestationChallenge_t challenge;
    memset(&challenge, 0, sizeof(challenge));
    memset(challenge.challenge, 0xCD, ATTESTATION_CHALLENGE_SIZE);
    challenge.timestamp_ms = 2000;
    challenge.sender_id = HUB_NODE_ID;

    /* Edge computes response (with wrong key) */
    AttestationResponse_t response;
    memset(&response, 0, sizeof(response));
    EdgeAttestResult_t ar = edge_auth_respond(&s_edge_auth, &challenge, &response);
    TEST_ASSERT(ar == EDGE_ATTEST_OK);  /* Edge thinks it succeeded */

    /* Hub REJECTS — HMAC mismatch */
    AttestationResult_t verify = attestation_verify(&challenge, &response, 5000);
    TEST_ASSERT_EQUAL(ATTEST_ERR_HMAC_MISMATCH, verify);

    /* Phase 15.1: Edge is NOT recorded in the trust tracker because
     * attestation_verify() failed. edge_tracker_attest() would not be
     * called here (the Hub only records trust on ATTEST_OK). */

    /* Setup Gateway + Hub tracker for the data path test */
    provision_gateway();
    provision_hub_gateway_tracker();

    /* Edge sends data (it thinks it's authenticated) */
    sensor_driver_mock_set_adc(MOCK_ADC_RAW, false);
    s_edge_wire_len = 0;
    edge_node_init(mock_rs485_send, &s_edge_auth);
    EdgeNodeResult_t edge_result = edge_node_read_and_send(2000);
    /* Edge sends data because edge_auth_respond() set AUTHENTICATED */
    TEST_ASSERT(edge_result.frame_sent);

    /* Gateway receives and forwards (Gateway is authenticated) */
    gateway_receive_edge_frame(s_edge_wire_buf, s_edge_wire_len);
    gateway_flush_batch();

    /* Hub processes — Phase 15.1: gateway_ingest_frame() now checks
     * each entry's node_id against the EdgeNodeTracker. Since this
     * Edge Node was never attested (Hub rejected the HMAC), its entry
     * is filtered out. out_count = 0. */
    uint8_t triggered = hub_process_batch_frame();

    /* Data must NOT reach the rule engine. */
    TEST_ASSERT_EQUAL(0, triggered);
    TEST_ASSERT_EQUAL(0, s_hub_rules_triggered);

    printf("\n    [PHASE 15.1 FIX] Unattested Edge Node: data REJECTED at Hub "
           "ingestion gate (triggered=%d). Gateway auth alone is no longer "
           "sufficient — Edge attestation is also required.\n", triggered);

    TEST_PASS();
}

/* ================================================================
 * TEST: Happy Path with Hub-side Trust Verification
 *
 * Confirms that a legitimately attested Edge Node's data flows
 * through the full pipeline including the Hub-side trust gate.
 * This is the complementary test to test_hub_rejects_data_from_unattested_edge_node.
 * ================================================================ */

static int test_hub_accepts_data_from_attested_edge_node(void)
{
    reset_all_state();

    /* Provision all three tiers */
    provision_edge_node();
    provision_gateway();
    provision_hub_gateway_tracker();

    /* Edge attests to Hub (challenge-response succeeds) */
    bool attested = edge_attest_to_hub();
    TEST_ASSERT(attested);
    TEST_ASSERT(edge_auth_is_authenticated(&s_edge_auth));

    /* Verify Edge is now in the trust tracker */
    TEST_ASSERT(edge_tracker_is_trusted(&s_edge_tracker, EDGE_NODE_ID, s_current_ms));

    /* Edge reads and sends sensor data */
    sensor_driver_mock_set_adc(MOCK_ADC_RAW, false);
    s_edge_wire_len = 0;
    edge_node_init(mock_rs485_send, &s_edge_auth);
    EdgeNodeResult_t edge_result = edge_node_read_and_send(s_current_ms);
    TEST_ASSERT(edge_result.frame_sent);

    /* Gateway processes and forwards */
    gateway_receive_edge_frame(s_edge_wire_buf, s_edge_wire_len);
    gateway_flush_batch();

    /* Hub processes — entry should pass the trust gate */
    uint8_t triggered = hub_process_batch_frame();
    TEST_ASSERT(triggered >= 1);
    TEST_ASSERT_EQUAL(HOME_RULE_DOOR_LOCK_CRITICAL, s_last_triggered_rule_id);
    TEST_ASSERT_EQUAL(HOME_ACTUATOR_DOOR_LOCK, s_last_triggered_actuator_id);

    printf("\n    [PHASE 15.1] Attested Edge Node: data accepted at Hub trust gate, "
           "CRITICAL rule %d fired.\n", s_last_triggered_rule_id);

    TEST_PASS();
}

/* ================================================================
 * TEST 3: Negative Path — Edge Node Never Provisioned
 *
 * An Edge Node with no key provisioned (edge_auth_init only, no set_key)
 * cannot respond to attestation challenges and is blocked from sending.
 * This is the true "failed attestation" gate at the Edge tier.
 * ================================================================ */

static int test_negative_edge_no_key_blocks_data(void)
{
    reset_all_state();

    /* Provision Edge config but NOT the auth key */
    edge_config_init(NULL);
    EdgeNodeConfig_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.node_id = EDGE_NODE_ID;
    cfg.active_driver = EDGE_SENSOR_TYPE;
    cfg.metric_id = EDGE_METRIC_ID;
    cfg.driver_config.scale_factor = SCALE_FACTOR;
    cfg.driver_config.offset = OFFSET;
    cfg.gw_dest_addr = GW_DEST_ADDR;
    cfg.valid = true;
    edge_config_set(&cfg);

    /* Init auth context but do NOT set key */
    EdgeAuthContext_t no_key_auth;
    edge_auth_init(&no_key_auth, EDGE_NODE_ID);
    /* edge_auth_set_key() NOT called */

    /* Attempt attestation — fails */
    AttestationChallenge_t challenge;
    memset(&challenge, 0, sizeof(challenge));
    memset(challenge.challenge, 0xEF, ATTESTATION_CHALLENGE_SIZE);
    challenge.timestamp_ms = 3000;
    challenge.sender_id = HUB_NODE_ID;

    AttestationResponse_t response;
    memset(&response, 0, sizeof(response));
    EdgeAttestResult_t ar = edge_auth_respond(&no_key_auth, &challenge, &response);
    TEST_ASSERT_EQUAL(EDGE_ATTEST_ERR_NOT_READY, ar);
    TEST_ASSERT(!edge_auth_is_authenticated(&no_key_auth));

    /* Edge Node attempt to send — blocked by attestation gate */
    sensor_driver_mock_set_adc(MOCK_ADC_RAW, false);
    s_edge_wire_len = 0;
    edge_node_init(mock_rs485_send, &no_key_auth);
    EdgeNodeResult_t edge_result = edge_node_read_and_send(3000);

    TEST_ASSERT(!edge_result.valid);
    TEST_ASSERT(!edge_result.frame_sent);
    TEST_ASSERT_EQUAL(0, s_edge_wire_len);

    /* Verify no data reached Gateway cache */
    float cached_val = 0;
    TEST_ASSERT(!cache_read(EDGE_NODE_ID, EDGE_METRIC_ID, &cached_val));

    printf("\n    [FINDING] No-key edge: data flow blocked at Edge tier. "
           "RS-485 frame length = %zu (expected 0).\n", s_edge_wire_len);

    TEST_PASS();
}

/* ================================================================
 * TEST 4: Negative Path — Tampered Gateway Batch Frame
 *
 * A Gateway batch frame with a corrupted HMAC must be rejected by
 * the Hub, and the Home rule engine must never see the fabricated entries.
 * ================================================================ */

static int test_negative_tampered_gateway_frame(void)
{
    reset_all_state();

    /* Provision Gateway + Hub tracker */
    provision_gateway();
    provision_hub_gateway_tracker();

    /* Gateway processes legitimate Edge data */
    provision_edge_node();
    sensor_driver_mock_set_adc(MOCK_ADC_RAW, false);
    s_edge_wire_len = 0;
    edge_node_init(mock_rs485_send, EDGE_AUTH_UNGATED_TESTING_ONLY);
    EdgeNodeResult_t edge_result = edge_node_read_and_send(4000);
    TEST_ASSERT(edge_result.frame_sent);

    gateway_receive_edge_frame(s_edge_wire_buf, s_edge_wire_len);
    gateway_flush_batch();
    TEST_ASSERT(s_gw_batch_len > 0);

    /* Save the legitimate batch frame */
    uint8_t legit_frame[BATCH_FRAME_MAX_SIZE];
    size_t legit_len = s_gw_batch_len;
    memcpy(legit_frame, s_gw_batch_buf, legit_len);

    /* TAMPER: corrupt a byte in the entry payload (after HMAC has been computed) */
    TEST_ASSERT(legit_len > BATCH_FRAME_HMAC_SIZE + 4);
    legit_frame[BATCH_FRAME_HEADER_SIZE + 2] ^= 0xFF;  /* Flip metric_id byte */

    /* Hub receives tampered frame */
    BatchFrameHeader_t header;
    BatchFrameEntry_t entries[BATCH_MAX_ENTRIES];
    uint8_t entry_count = 0;

    GatewayFrameResult_t r = gateway_ingest_frame(
        &s_hub_gw_tracker, &s_edge_tracker, legit_frame, legit_len,
        &header, entries, BATCH_MAX_ENTRIES, &entry_count);

    /* Hub MUST reject — HMAC mismatch */
    TEST_ASSERT_EQUAL(GW_FRAME_ERR_HMAC_MISMATCH, r);
    TEST_ASSERT_EQUAL(0, entry_count);

    /* Verify rule engine was NEVER called with fabricated entries */
    TEST_ASSERT_EQUAL(0, s_hub_rules_triggered);

    printf("\n    [FINDING] Tampered Gateway frame: HMAC verification correctly "
           "rejected frame (r=%d), entry_count=0, rule_engine never called.\n", r);

    TEST_PASS();
}

/* ================================================================
 * TEST 5: End-to-End — CRITICAL Rule Threshold Boundary
 *
 * Verify the rule engine fires correctly at the exact threshold boundary.
 * - Value = 0.9 (below threshold 1.0) → rule should NOT fire
 * - Value = 1.0 (at threshold, RULE_COMPARE_ABOVE means >=) → fires
 * ================================================================ */

static int test_threshold_boundary_below(void)
{
    reset_all_state();

    provision_gateway();
    provision_hub_gateway_tracker();

    /* Simulate a batch entry with value BELOW the threshold */
    BatchFrameHeader_t header = {
        .magic = 0xB7,
        .version = 0x02,
        .entry_count = 1,
        .flags = 0,
        .sequence_number = 0
    };
    BatchFrameEntry_t entry = {
        .node_id = EDGE_NODE_ID,
        .metric_id = EDGE_METRIC_ID,
        .value = 0.9f  /* Below threshold of 1.0 */
    };

    /* Encode, sign, and send */
    uint8_t wire_buf[BATCH_FRAME_MAX_SIZE];
    size_t wire_len = 0;
    batch_encode_frame(&header, &entry, 1, wire_buf, sizeof(wire_buf), &wire_len);

    uint8_t hmac[GATEWAY_HMAC_SIZE];
    gateway_sign_frame(&s_gw_auth, wire_buf, wire_len - GATEWAY_HMAC_SIZE, hmac);
    memcpy(&wire_buf[wire_len - GATEWAY_HMAC_SIZE], hmac, GATEWAY_HMAC_SIZE);

    /* Hub processes */
    BatchFrameHeader_t out_header;
    BatchFrameEntry_t out_entries[BATCH_MAX_ENTRIES];
    uint8_t out_count = 0;

    /* Explicitly disable trust check: these tests verify rule-engine
     * thresholds, not trust gating. Trust gating is tested separately. */
    GatewayFrameResult_t gr = gateway_ingest_frame(
        &s_hub_gw_tracker, EDGE_TRACKER_CHECK_DISABLED_TESTING_ONLY, wire_buf, wire_len,
        &out_header, out_entries, BATCH_MAX_ENTRIES, &out_count);

    TEST_ASSERT(gr == GW_FRAME_OK);
    TEST_ASSERT_EQUAL(1, out_count);

    /* Feed through vtable validation + rule engine */
    const DomainProfileVTable_t *vtable = home_profile_get_vtable();
    SensorReading_t reading;
    reading.node_id = (uint8_t)out_entries[0].node_id;
    reading.metric_id = out_entries[0].metric_id;
    reading.value = out_entries[0].value;
    reading.timestamp_ms = 5000;

    TEST_ASSERT(vtable->validateSensorReading(&reading));

    RuleEvaluationResult_t results[RULE_ENGINE_MAX_ACTIONS];
    uint8_t triggered = rule_engine_evaluate(vtable, &reading, results, RULE_ENGINE_MAX_ACTIONS);

    /* 0.9 < 1.0 → RULE_COMPARE_ABOVE (>=) → NOT triggered */
    TEST_ASSERT_EQUAL(0, triggered);

    TEST_PASS();
}

static int test_threshold_boundary_at(void)
{
    reset_all_state();

    provision_gateway();
    provision_hub_gateway_tracker();

    /* Value = 1.0 (exactly at threshold) */
    BatchFrameHeader_t header = {
        .magic = 0xB7,
        .version = 0x02,
        .entry_count = 1,
        .flags = 0,
        .sequence_number = 1
    };
    BatchFrameEntry_t entry = {
        .node_id = EDGE_NODE_ID,
        .metric_id = EDGE_METRIC_ID,
        .value = 1.0f  /* Exactly at threshold */
    };

    uint8_t wire_buf[BATCH_FRAME_MAX_SIZE];
    size_t wire_len = 0;
    batch_encode_frame(&header, &entry, 1, wire_buf, sizeof(wire_buf), &wire_len);

    uint8_t hmac[GATEWAY_HMAC_SIZE];
    gateway_sign_frame(&s_gw_auth, wire_buf, wire_len - GATEWAY_HMAC_SIZE, hmac);
    memcpy(&wire_buf[wire_len - GATEWAY_HMAC_SIZE], hmac, GATEWAY_HMAC_SIZE);

    BatchFrameHeader_t out_header;
    BatchFrameEntry_t out_entries[BATCH_MAX_ENTRIES];
    uint8_t out_count = 0;

    /* Explicitly disable trust check: testing rule-engine thresholds only. */
    GatewayFrameResult_t gr = gateway_ingest_frame(
        &s_hub_gw_tracker, EDGE_TRACKER_CHECK_DISABLED_TESTING_ONLY, wire_buf, wire_len,
        &out_header, out_entries, BATCH_MAX_ENTRIES, &out_count);

    TEST_ASSERT(gr == GW_FRAME_OK);
    TEST_ASSERT_EQUAL(1, out_count);

    const DomainProfileVTable_t *vtable = home_profile_get_vtable();
    SensorReading_t reading;
    reading.node_id = (uint8_t)out_entries[0].node_id;
    reading.metric_id = out_entries[0].metric_id;
    reading.value = out_entries[0].value;
    reading.timestamp_ms = 6000;

    TEST_ASSERT(vtable->validateSensorReading(&reading));

    RuleEvaluationResult_t results[RULE_ENGINE_MAX_ACTIONS];
    uint8_t triggered = rule_engine_evaluate(vtable, &reading, results, RULE_ENGINE_MAX_ACTIONS);

    /* 1.0 >= 1.0 → RULE_COMPARE_ABOVE → TRIGGERED */
    TEST_ASSERT_EQUAL(1, triggered);
    TEST_ASSERT_EQUAL(HOME_RULE_DOOR_LOCK_CRITICAL, results[0].rule_id);
    TEST_ASSERT_EQUAL(HOME_ACTUATOR_DOOR_LOCK, results[0].actuator_id);

    TEST_PASS();
}

/* ================================================================
 * TEST 6: Interface Mismatch Verification
 *
 * Explicitly verify the type narrowing/widening at each tier boundary
 * works correctly, and document the findings.
 * ================================================================ */

static int test_interface_node_id_widening(void)
{
    /* Edge sends node_id as uint8 (0-255) in RS-485 payload.
     * Gateway cache uses uint16_t. Verify widening is lossless. */
    uint8_t edge_node_id = 0x01;
    uint16_t gw_node_id = (uint16_t)edge_node_id;
    TEST_ASSERT_EQUAL(edge_node_id, (uint8_t)gw_node_id);

    /* Batch frame carries uint16_t node_id.
     * Hub SensorReading uses uint8_t. Verify narrowing is safe
     * when upper byte is 0. */
    uint16_t batch_node_id = 0x0001;
    uint8_t hub_node_id = (uint8_t)batch_node_id;
    TEST_ASSERT_EQUAL(1, hub_node_id);

    /* Edge node_id = 255 (max uint8) — still fits in uint16 */
    edge_node_id = 0xFF;
    gw_node_id = (uint16_t)edge_node_id;
    batch_node_id = gw_node_id;
    hub_node_id = (uint8_t)batch_node_id;
    TEST_ASSERT_EQUAL(0xFF, hub_node_id);

    printf("\n    [FINDING] node_id types: Edge(uint8) → cache(uint16) → "
           "batch(uint16) → Hub SensorReading(uint8). Widening is lossless; "
           "narrowing is safe for values <= 255. No truncation in practice.\n");

    TEST_PASS();
}

static int test_interface_metric_id_consistency(void)
{
    /* Edge config.metric_id must match Home profile metric IDs.
     * Verify the constant used by Edge matches the one used by Hub rules. */
    TEST_ASSERT_EQUAL(HOME_METRIC_SECURITY_STATE, 10);

    /* The Edge encodes metric_id as 1 byte in RS-485 payload.
     * The Hub rule engine expects the same metric_id in RuleEntry_t.
     * Verify consistency. */
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    home_getRuleTable(&rules, &count);

    bool found_metric = false;
    for (uint8_t i = 0; i < count; i++) {
        if (rules[i].metric_id == HOME_METRIC_SECURITY_STATE) {
            found_metric = true;
            break;
        }
    }
    TEST_ASSERT(found_metric);

    printf("\n    [FINDING] metric_id consistency: Edge config metric_id (%d) "
           "matches Home rule table metric_id. Encoding is uint8 (1 byte) "
           "in RS-485 payload — sufficient for metric IDs 0-255.\n",
           EDGE_METRIC_ID);

    TEST_PASS();
}

static int test_interface_sensor_validation_bounds_gap(void)
{
    /* CRITICAL FINDING: HOME_METRIC_SECURITY_STATE (10) has NO
     * SensorValidationBounds_t entry in home_validation_bounds.c.
     * home_get_validation_bounds(10) returns NULL.
     *
     * This means sensorValidate() returns SENSOR_OUT_OF_PHYSICAL_RANGE
     * for metric 10 (because bounds == NULL).
     *
     * The vtable's validateSensorReading() works (value >= 0 check),
     * but the standalone sensorValidate() function cannot be used as
     * a gate for the CRITICAL metric.
     *
     * This is an INTERFACE MISMATCH between the sensor validation
     * subsystem (which expects bounds for every metric) and the Home
     * profile (which doesn't provide bounds for metric 10). */

    const SensorValidationBounds_t *bounds = home_get_validation_bounds(HOME_METRIC_SECURITY_STATE);
    TEST_ASSERT_NULL(bounds);  /* Confirmed: no bounds for metric 10 */

    /* The vtable validation works fine */
    SensorReading_t reading = {
        .node_id = 1,
        .metric_id = HOME_METRIC_SECURITY_STATE,
        .value = 1.0f,
        .timestamp_ms = 1000
    };
    TEST_ASSERT(home_validateSensorReading(&reading));

    /* But standalone sensorValidate() would fail */
    SensorHistory_t history;
    memset(&history, 0, sizeof(history));
    SensorValidationResult_t svr = sensorValidate(
        1, HOME_METRIC_SECURITY_STATE, 1.0f, bounds, &history, 1000);
    TEST_ASSERT_EQUAL(SENSOR_OUT_OF_PHYSICAL_RANGE, svr);

    printf("\n    [INTERFACE MISMATCH] HOME_METRIC_SECURITY_STATE (10) — "
           "the Home CRITICAL metric — has NO SensorValidationBounds_t. "
           "sensorValidate() rejects it (bounds=NULL → OUT_OF_RANGE). "
           "The Hub must use the vtable's validateSensorReading() instead. "
           "Recommendation: add bounds for metric 10 (e.g., min=0, max=1) "
           "to home_validation_bounds.c, OR document that the vtable is the "
           "sole validation gate for binary/state metrics.\n");

    TEST_PASS();
}

/* ================================================================
 * TEST 7: Full Pipeline — Sensor Validation With Bounded Metric
 *
 * Use HOME_METRIC_LIGHT_LEVEL (12) which HAS validation bounds,
 * to prove the full sensorValidate() → rule_engine path works.
 * ================================================================ */

static int test_full_pipeline_with_sensor_validation(void)
{
    reset_all_state();

    /* Configure Edge for light level metric (12) with analog driver.
     * Physical range: 0-1000 lux. Lighting rule: light < 100 → turn on relay.
     * But our rule triggers on ABOVE threshold (>= 100).
     * So we send value = 200 (above threshold 100) → rule fires. */

    edge_config_init(NULL);
    EdgeNodeConfig_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.node_id = EDGE_NODE_ID;
    cfg.active_driver = EDGE_SENSOR_TYPE;
    cfg.metric_id = HOME_METRIC_LIGHT_LEVEL;
    /* physical = raw * scale + offset → 200 = 2048 * scale + 0 → scale = 200/2048 */
    cfg.driver_config.scale_factor = 200.0f / 2048.0f;
    cfg.driver_config.offset = 0.0f;
    cfg.gw_dest_addr = GW_DEST_ADDR;
    cfg.valid = true;
    edge_config_set(&cfg);

    /* Edge attests */
    EdgeAuthContext_t light_auth;
    edge_auth_init(&light_auth, EDGE_NODE_ID);
    edge_auth_set_key(&light_auth, s_edge_key);
    AttestationChallenge_t challenge;
    memset(&challenge, 0, sizeof(challenge));
    memset(challenge.challenge, 0x55, ATTESTATION_CHALLENGE_SIZE);
    challenge.timestamp_ms = 7000;
    challenge.sender_id = HUB_NODE_ID;
    AttestationResponse_t response;
    edge_auth_respond(&light_auth, &challenge, &response);

    /* Register key + Gateway */
    AttestationKeyRecord_t record;
    record.node_id = EDGE_NODE_ID;
    memcpy(record.key, s_edge_key, ATTESTATION_KEY_SIZE);
    record.active = true;
    attestation_register_key(&record);
    provision_gateway();
    provision_hub_gateway_tracker();

    /* Also set delta filter threshold for metric 12 (light level).
     * provision_gateway() only sets threshold for EDGE_METRIC_ID (10).
     * Without this, delta_filter_evaluate() returns DELTA_ERR_NOT_FOUND
     * for metric 12, and nothing is cached/batched. */
    delta_filter_set_threshold(HOME_METRIC_LIGHT_LEVEL, 0.5f);

    /* Edge reads, sends */
    sensor_driver_mock_set_adc(2048.0f, false);
    s_edge_wire_len = 0;
    edge_node_init(mock_rs485_send, &light_auth);
    EdgeNodeResult_t edge_result = edge_node_read_and_send(7000);
    TEST_ASSERT(edge_result.frame_sent);
    TEST_ASSERT(fabsf(edge_result.value - 200.0f) < 0.1f);

    /* Gateway processes */
    gateway_receive_edge_frame(s_edge_wire_buf, s_edge_wire_len);
    gateway_flush_batch();

    /* Hub processes with full sensorValidate() */
    BatchFrameHeader_t header;
    BatchFrameEntry_t entries[BATCH_MAX_ENTRIES];
    uint8_t entry_count = 0;

    /* Explicitly disable trust check: this test verifies the sensorValidate()
     * gate and rule engine for bounded metrics, not trust gating. */
    GatewayFrameResult_t gr = gateway_ingest_frame(
        &s_hub_gw_tracker, EDGE_TRACKER_CHECK_DISABLED_TESTING_ONLY, s_gw_batch_buf, s_gw_batch_len,
        &header, entries, BATCH_MAX_ENTRIES, &entry_count);

    TEST_ASSERT(gr == GW_FRAME_OK);
    TEST_ASSERT_EQUAL(1, entry_count);

    const DomainProfileVTable_t *vtable = home_profile_get_vtable();
    SensorReading_t reading;
    reading.node_id = (uint8_t)entries[0].node_id;
    reading.metric_id = entries[0].metric_id;
    reading.value = entries[0].value;
    reading.timestamp_ms = 7000;

    /* Vtable validation */
    TEST_ASSERT(vtable->validateSensorReading(&reading));

    /* Standalone sensorValidate() with Home bounds (metric 12 has bounds) */
    const SensorValidationBounds_t *bounds = home_get_validation_bounds(reading.metric_id);
    TEST_ASSERT_NOT_NULL(bounds);
    SensorHistory_t *hist = home_get_metric_history(reading.metric_id);
    TEST_ASSERT_NOT_NULL(hist);

    SensorValidationResult_t svr = sensorValidate(
        reading.node_id, reading.metric_id, reading.value,
        bounds, hist, 7000);
    TEST_ASSERT_EQUAL(SENSOR_VALID, svr);
    sensor_history_record(hist, reading.node_id, reading.metric_id,
                          reading.value, 7000);

    /* Rule engine */
    RuleEvaluationResult_t results[RULE_ENGINE_MAX_ACTIONS];
    uint8_t triggered = rule_engine_evaluate(vtable, &reading, results, RULE_ENGINE_MAX_ACTIONS);

    /* LIGHTING rule: light level >= 100 → turn on light relay */
    TEST_ASSERT(triggered >= 1);
    TEST_ASSERT_EQUAL(HOME_RULE_LIGHTING_OPERATIONAL, results[0].rule_id);
    TEST_ASSERT_EQUAL(HOME_ACTUATOR_LIGHT_RELAY, results[0].actuator_id);

    printf("\n    [PASS] Full pipeline with sensorValidate() gate: "
           "light_level=200.0 lux → bounds check PASSED → "
           "rule %d triggered → actuator %d\n",
           results[0].rule_id, results[0].actuator_id);

    TEST_PASS();
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("\n========================================\n");
    printf("Phase 15: Full Pipeline Integration Tests\n");
    printf("Edge → Gateway → Hub (Home Domain)\n");
    printf("========================================\n\n");

    printf("--- Happy Path ---\n");
    RUN_TEST(test_full_pipeline_happy_path);

    printf("\n--- Negative Paths ---\n");
    RUN_TEST(test_hub_rejects_data_from_unattested_edge_node);
    RUN_TEST(test_negative_edge_no_key_blocks_data);
    RUN_TEST(test_negative_tampered_gateway_frame);

    printf("\n--- Hub-side Trust Gate (Phase 15.1) ---\n");
    RUN_TEST(test_hub_accepts_data_from_attested_edge_node);

    printf("\n--- Threshold Boundaries ---\n");
    RUN_TEST(test_threshold_boundary_below);
    RUN_TEST(test_threshold_boundary_at);

    printf("\n--- Interface Mismatch Verification ---\n");
    RUN_TEST(test_interface_node_id_widening);
    RUN_TEST(test_interface_metric_id_consistency);
    RUN_TEST(test_interface_sensor_validation_bounds_gap);

    printf("\n--- Full Pipeline with Sensor Validation ---\n");
    RUN_TEST(test_full_pipeline_with_sensor_validation);

    /* Print final findings report */
    printf("\n========================================\n");
    printf("INTERFACE MISMATCH REPORT\n");
    printf("========================================\n");
    printf("1. node_id type: Edge(uint8) → cache(uint16) → batch(uint16)\n");
    printf("   → Hub SensorReading(uint8). Widening lossless, narrowing safe.\n");
    printf("   STATUS: No mismatch — works correctly.\n\n");
    printf("2. metric_id: Edge config(uint8) → RS-485(uint8) → batch(uint8)\n");
    printf("   → Hub RuleEntry(uint8). All uint8 — no mismatch.\n");
    printf("   STATUS: No mismatch — works correctly.\n\n");
    printf("3. HOME_METRIC_SECURITY_STATE (10) — CRITICAL rule metric:\n");
    printf("   NO SensorValidationBounds_t entry in home_validation_bounds.c.\n");
    printf("   sensorValidate() returns OUT_OF_RANGE for metric 10.\n");
    printf("   Hub MUST use vtable's validateSensorReading() for this metric.\n");
    printf("   STATUS: INTERFACE GAP — functional but asymmetric.\n");
    printf("   RECOMMENDATION: Add bounds (min=0, max=1) for metric 10,\n");
    printf("   OR document that binary/state metrics use vtable validation only.\n\n");
    printf("4. Edge attestation: FIXED in Phase 15.1:\n");
    printf("   gateway_ingest_frame() now filters by EdgeNodeTracker.\n");
    printf("   Unattested Edge data is rejected at Hub ingestion gate.\n");
    printf("   Gateway HMAC alone is no longer sufficient — both gates required.\n");
    printf("   STATUS: FIXED — T1 threat gap closed.\n\n");
    printf("5. Value encoding: Edge float LE → RS-485(float LE) → cache(float)\n");
    printf("   → batch(float) → Hub SensorReading(float). All IEEE 754.\n");
    printf("   STATUS: No mismatch — works correctly.\n");
    printf("========================================\n");

    PRINT_TEST_SUMMARY();
}
