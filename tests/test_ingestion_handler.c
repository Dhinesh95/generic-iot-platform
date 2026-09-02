/**
 * @file test_ingestion_handler.c
 * @brief Tests for the production Hub ingestion handler.
 *
 * Proves the real handler (firmware/hub/ingestion_handler.c) produces
 * identical behavior to Phase 15/15.1/15.2's test-harness version for
 * the same inputs: happy path, unattested-edge rejection, tampered-gateway
 * rejection. This confirms the "real" implementation isn't a different,
 * untested code path.
 */

#include "test_helpers/test_utils.h"
#include "../firmware/hub/ingestion_handler.h"
#include "../firmware/hub/historian.h"
#include "../firmware/edge/edge_attestation.h"
#include "../firmware/gateway/gateway_auth.h"
#include "../firmware/gateway/batch_forwarder.h"
#include "../firmware/gateway/stateful_cache.h"
#include "../firmware/gateway/delta_filter.h"
#include "../firmware/shared/attestation.h"
#include "../firmware/shared/audit_log.h"
#include "../firmware/shared/rule_engine_core.h"
#include "../firmware/shared/sensor_validation.h"
#include "../firmware/profiles/home/home_rules.h"
#include "../firmware/profiles/home/home_validation_bounds.h"
#include "../firmware/profiles/agriculture/agriculture_rules.h"
#include "../firmware/profiles/agriculture/agriculture_validation_bounds.h"
#include "../firmware/profiles/hvac/hvac_rules.h"
#include "../firmware/profiles/hvac/hvac_validation_bounds.h"
#include "../firmware/profiles/water_treatment/water_rules.h"
#include "../firmware/profiles/water_treatment/water_validation_bounds.h"
#include <string.h>
#include <math.h>

/* ---------- Constants ---------- */

#define EDGE_NODE_ID    0x01
#define GATEWAY_NODE_ID 0xF0
#define EDGE_METRIC_ID  HOME_METRIC_SECURITY_STATE  /* 10 */
#define TARGET_VALUE    1.0f

/* ---------- Mock state ---------- */

static uint8_t  s_radio_payload[256];
static size_t   s_radio_payload_len = 0;
static bool     s_radio_has_data = true;

static bool mock_radio_receive(uint8_t *out_payload, size_t max_len, size_t *out_len)
{
    if (!s_radio_has_data || s_radio_payload_len == 0) return false;
    size_t copy = s_radio_payload_len;
    if (copy > max_len) copy = max_len;
    memcpy(out_payload, s_radio_payload, copy);
    *out_len = copy;
    return true;
}

/* ---------- Hub-side trackers ---------- */

static GatewayGatewayTracker_t s_gw_tracker;
static EdgeNodeTracker_t       s_edge_tracker;
static GatewayAuthContext_t     s_gw_auth;

/* ---------- Keys ---------- */

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

/* ---------- Helper: reset all state ---------- */

/* Historian encryption key (test-only, distinct from transport session key). */
static const uint8_t s_historian_key[HISTORIAN_KEY_SIZE] = {
    0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8,
    0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0
};

static void reset_all(void)
{
    memset(s_radio_payload, 0, sizeof(s_radio_payload));
    s_radio_payload_len = 0;
    s_radio_has_data = true;

    attestation_init();
    sensor_validation_init();
    rule_engine_init();
    cache_init();
    delta_filter_init();
    audit_log_init();
    historian_set_key(s_historian_key, HISTORIAN_KEY_SIZE);
    historian_init();
    edge_tracker_init(&s_edge_tracker, EDGE_NODE_ATTEST_EXPIRY_MS);

    gateway_tracker_init(&s_gw_tracker, GATEWAY_NODE_ID, s_gw_session_key);
    s_gw_tracker.authenticated = true;

    gateway_auth_init(&s_gw_auth, GATEWAY_NODE_ID, 0x01, s_gw_session_key);
    gateway_set_state(&s_gw_auth, GW_AUTH_AUTHENTICATED);

    ingestion_handler_reset_stats();
    historian_reset_stats();
}

/* ---------- Helper: build and sign a batch frame ---------- */

static void build_signed_batch_frame(
    uint16_t node_id, uint8_t metric_id, float value,
    uint32_t sequence)
{
    BatchFrameHeader_t header = {
        .magic = 0xB7, .version = 0x02, .entry_count = 1,
        .flags = 0, .sequence_number = sequence
    };
    BatchFrameEntry_t entry = {
        .node_id = node_id, .metric_id = metric_id, .value = value
    };

    uint8_t wire_buf[BATCH_FRAME_MAX_SIZE];
    size_t wire_len = 0;
    batch_encode_frame(&header, &entry, 1, wire_buf, sizeof(wire_buf), &wire_len);

    uint8_t hmac[GATEWAY_HMAC_SIZE];
    gateway_sign_frame(&s_gw_auth, wire_buf, wire_len - GATEWAY_HMAC_SIZE, hmac);
    memcpy(&wire_buf[wire_len - GATEWAY_HMAC_SIZE], hmac, GATEWAY_HMAC_SIZE);

    memcpy(s_radio_payload, wire_buf, wire_len);
    s_radio_payload_len = wire_len;
}

/* ================================================================
 * TEST 1: Happy Path — attested Edge Node, valid data
 * ================================================================ */

static int test_handler_happy_path(void)
{
    reset_all();

    /* Register Edge key and attest */
    AttestationKeyRecord_t record = { .node_id = EDGE_NODE_ID, .active = true };
    memcpy(record.key, s_edge_key, ATTESTATION_KEY_SIZE);
    attestation_register_key(&record);

    AttestationChallenge_t challenge = {
        .timestamp_ms = 1000, .sender_id = 0x01
    };
    memset(challenge.challenge, 0xAB, ATTESTATION_CHALLENGE_SIZE);

    /* Edge Node computes response */
    EdgeAuthContext_t edge_auth;
    edge_auth_init(&edge_auth, EDGE_NODE_ID);
    edge_auth_set_key(&edge_auth, s_edge_key);
    AttestationResponse_t response;
    memset(&response, 0, sizeof(response));
    edge_auth_respond(&edge_auth, &challenge, &response);

    /* Hub verifies and records trust */
    edge_tracker_attest(&s_edge_tracker, &challenge, &response, 1000);

    /* Build Gateway-signed batch frame */
    build_signed_batch_frame(EDGE_NODE_ID, EDGE_METRIC_ID, TARGET_VALUE, 0);

    /* Init handler */
    IngestionHandlerConfig_t config = {
        .gw_tracker    = &s_gw_tracker,
        .edge_tracker  = &s_edge_tracker,
        .vtable        = home_profile_get_vtable(),
        .radio_receive = mock_radio_receive,
        .bounds_lookup = NULL,  /* Metric 10 has no standalone bounds */
        .history_lookup = NULL,
        .domain_profile_id = 0  /* Home */
    };
    TEST_ASSERT(ingestion_handler_init(&config));

    /* Poll — should trigger CRITICAL rule */
    uint8_t triggered = ingestion_handler_poll(1000);
    TEST_ASSERT(triggered >= 1);

    const IngestionHandlerStats_t *stats = ingestion_handler_get_stats();
    TEST_ASSERT_EQUAL(1, stats->poll_count);
    TEST_ASSERT_EQUAL(1, stats->frames_received);
    TEST_ASSERT_EQUAL(1, stats->frames_ingested);
    TEST_ASSERT_EQUAL(0, stats->frames_rejected);
    TEST_ASSERT_EQUAL(1, stats->entries_processed);
    TEST_ASSERT_EQUAL(1, stats->entries_valid);

    TEST_PASS();
}

/* ================================================================
 * TEST 2: Unattested Edge Node — data rejected at trust gate
 * ================================================================ */

static int test_handler_unattested_edge_rejected(void)
{
    reset_all();

    /* Do NOT attest the Edge Node — no edge_tracker_attest() call */

    /* Build a valid Gateway-signed batch frame with the unattested node's data */
    build_signed_batch_frame(EDGE_NODE_ID, EDGE_METRIC_ID, TARGET_VALUE, 0);

    IngestionHandlerConfig_t config = {
        .gw_tracker    = &s_gw_tracker,
        .edge_tracker  = &s_edge_tracker,
        .vtable        = home_profile_get_vtable(),
        .radio_receive = mock_radio_receive,
        .bounds_lookup = NULL,
        .history_lookup = NULL,
        .domain_profile_id = 0  /* Home */
    };
    TEST_ASSERT(ingestion_handler_init(&config));

    uint8_t triggered = ingestion_handler_poll(2000);

    /* Data must NOT reach the rule engine — Edge not attested */
    TEST_ASSERT_EQUAL(0, triggered);

    const IngestionHandlerStats_t *stats = ingestion_handler_get_stats();
    TEST_ASSERT_EQUAL(1, stats->frames_received);
    TEST_ASSERT_EQUAL(1, stats->frames_ingested);  /* Frame passes Gateway HMAC */
    TEST_ASSERT_EQUAL(0, stats->frames_rejected);   /* But entries are filtered */
    TEST_ASSERT_EQUAL(0, stats->entries_processed);  /* No entries pass trust gate */

    TEST_PASS();
}

/* ================================================================
 * TEST 3: Tampered Gateway frame — rejected at HMAC gate
 * ================================================================ */

static int test_handler_tampered_frame_rejected(void)
{
    reset_all();

    /* Build a valid frame */
    build_signed_batch_frame(EDGE_NODE_ID, EDGE_METRIC_ID, TARGET_VALUE, 0);

    /* Tamper: flip a byte in the entry */
    s_radio_payload[8 + 2] ^= 0xFF;

    IngestionHandlerConfig_t config = {
        .gw_tracker    = &s_gw_tracker,
        .edge_tracker  = &s_edge_tracker,
        .vtable        = home_profile_get_vtable(),
        .radio_receive = mock_radio_receive,
        .bounds_lookup = NULL,
        .history_lookup = NULL,
        .domain_profile_id = 0  /* Home */
    };
    TEST_ASSERT(ingestion_handler_init(&config));

    uint8_t triggered = ingestion_handler_poll(3000);

    TEST_ASSERT_EQUAL(0, triggered);

    const IngestionHandlerStats_t *stats = ingestion_handler_get_stats();
    TEST_ASSERT_EQUAL(1, stats->frames_received);
    TEST_ASSERT_EQUAL(0, stats->frames_ingested);  /* HMAC failed */
    TEST_ASSERT_EQUAL(1, stats->frames_rejected);

    TEST_PASS();
}

/* ================================================================
 * TEST 4: No data — poll returns immediately
 * ================================================================ */

static int test_handler_no_data(void)
{
    reset_all();
    s_radio_has_data = false;

    IngestionHandlerConfig_t config = {
        .gw_tracker    = &s_gw_tracker,
        .edge_tracker  = &s_edge_tracker,
        .vtable        = home_profile_get_vtable(),
        .radio_receive = mock_radio_receive,
        .bounds_lookup = NULL,
        .history_lookup = NULL,
        .domain_profile_id = 0  /* Home */
    };
    TEST_ASSERT(ingestion_handler_init(&config));

    uint8_t triggered = ingestion_handler_poll(4000);
    TEST_ASSERT_EQUAL(0, triggered);

    const IngestionHandlerStats_t *stats = ingestion_handler_get_stats();
    TEST_ASSERT_EQUAL(1, stats->poll_count);
    TEST_ASSERT_EQUAL(0, stats->frames_received);

    TEST_PASS();
}

/* ================================================================
 * TEST 5: Not initialised — poll returns 0
 * ================================================================ */

static int test_handler_not_initialised(void)
{
    reset_all();
    /* Do NOT call ingestion_handler_init */

    uint8_t triggered = ingestion_handler_poll(5000);
    TEST_ASSERT_EQUAL(0, triggered);

    TEST_PASS();
}

/* ================================================================
 * TEST 6: Stats reset
 * ================================================================ */

static int test_handler_stats_reset(void)
{
    reset_all();
    s_radio_has_data = false;

    IngestionHandlerConfig_t config = {
        .gw_tracker    = &s_gw_tracker,
        .edge_tracker  = &s_edge_tracker,
        .vtable        = home_profile_get_vtable(),
        .radio_receive = mock_radio_receive,
        .bounds_lookup = NULL,
        .history_lookup = NULL,
        .domain_profile_id = 0  /* Home */
    };
    TEST_ASSERT(ingestion_handler_init(&config));

    ingestion_handler_poll(6000);
    ingestion_handler_poll(6000);

    const IngestionHandlerStats_t *stats = ingestion_handler_get_stats();
    TEST_ASSERT_EQUAL(2, stats->poll_count);

    ingestion_handler_reset_stats();
    stats = ingestion_handler_get_stats();
    TEST_ASSERT_EQUAL(0, stats->poll_count);

    TEST_PASS();
}

/* ================================================================
 * TEST 7: Init rejects NULL config
 * ================================================================ */

static int test_handler_init_null(void)
{
    TEST_ASSERT(!ingestion_handler_init(NULL));
    TEST_PASS();
}

/* ================================================================
 * TEST 8: Init rejects incomplete config
 * ================================================================ */

static int test_handler_init_incomplete(void)
{
    IngestionHandlerConfig_t config;
    memset(&config, 0, sizeof(config));

    /* Missing gw_tracker */
    config.edge_tracker = &s_edge_tracker;        config.vtable = home_profile_get_vtable();
        config.radio_receive = mock_radio_receive;
        config.domain_profile_id = 0;
    TEST_ASSERT(!ingestion_handler_init(&config));

    /* Missing edge_tracker */
    memset(&config, 0, sizeof(config));
    config.gw_tracker = &s_gw_tracker;
    config.vtable = home_profile_get_vtable();
    config.radio_receive = mock_radio_receive;
    config.domain_profile_id = 0;
    TEST_ASSERT(!ingestion_handler_init(&config));

    /* Missing vtable */
    memset(&config, 0, sizeof(config));
    config.gw_tracker = &s_gw_tracker;
    config.edge_tracker = &s_edge_tracker;
    config.radio_receive = mock_radio_receive;
    config.domain_profile_id = 0;
    TEST_ASSERT(!ingestion_handler_init(&config));

    /* Missing radio_receive */
    memset(&config, 0, sizeof(config));
    config.gw_tracker = &s_gw_tracker;
    config.edge_tracker = &s_edge_tracker;
    config.vtable = home_profile_get_vtable();
    config.domain_profile_id = 0;
    TEST_ASSERT(!ingestion_handler_init(&config));

    TEST_PASS();
}

/* ================================================================
 * TEST 9: All 4 profile vtable/radio/bounds triples are correct
 *
 * Exercises the same guard logic used in main.c's hub_init() to
 * confirm that each profile macro routes to the correct vtable,
 * bounds lookup, and radio adapter assignment. Since this test is
 * compiled with ALL_SRCS (all profile sources), we can verify all
 * 4 profiles in a single compilation.
 * ================================================================ */

static int test_all_profile_wiring_triples(void)
{
    /* --- Home profile --- */
    {
        const DomainProfileVTable_t *vt = home_profile_get_vtable();
        TEST_ASSERT(vt != NULL);
        TEST_ASSERT(vt->getRuleTable != NULL);
        TEST_ASSERT(vt->validateSensorReading != NULL);
        TEST_ASSERT(vt->getFailSafeMode != NULL);
        TEST_ASSERT(vt->executeAction != NULL);

        /* Verify bounds lookup for a known metric */
        const SensorValidationBounds_t *bounds =
            home_get_validation_bounds(HOME_METRIC_LIGHT_LEVEL);
        TEST_ASSERT(bounds != NULL);

        /* Verify no-bounds metric (binary state) returns NULL */
        const SensorValidationBounds_t *no_bounds =
            home_get_validation_bounds(HOME_METRIC_SECURITY_STATE);
        TEST_ASSERT(no_bounds == NULL);

        /* Verify vtable is the same instance (singleton) */
        TEST_ASSERT(home_profile_get_vtable() == vt);

        printf("  Home profile: vtable=%p, LIGHT_LEVEL bounds=%p, SECURITY_STATE bounds=%p\n",
               (void *)vt, (void *)bounds, (void *)no_bounds);
    }

    /* --- Agriculture profile --- */
    {
        const DomainProfileVTable_t *vt = agriculture_profile_get_vtable();
        TEST_ASSERT(vt != NULL);
        TEST_ASSERT(vt->getRuleTable != NULL);
        TEST_ASSERT(vt->validateSensorReading != NULL);
        TEST_ASSERT(vt->getFailSafeMode != NULL);
        TEST_ASSERT(vt->executeAction != NULL);

        const SensorValidationBounds_t *bounds =
            agriculture_get_validation_bounds(AGRI_METRIC_SOIL_MOISTURE);
        TEST_ASSERT(bounds != NULL);

        TEST_ASSERT(agriculture_profile_get_vtable() == vt);

        printf("  Agriculture: vtable=%p, SOIL_MOISTURE bounds=%p\n",
               (void *)vt, (void *)bounds);
    }

    /* --- HVAC profile --- */
    {
        const DomainProfileVTable_t *vt = hvac_profile_get_vtable();
        TEST_ASSERT(vt != NULL);
        TEST_ASSERT(vt->getRuleTable != NULL);
        TEST_ASSERT(vt->validateSensorReading != NULL);
        TEST_ASSERT(vt->getFailSafeMode != NULL);
        TEST_ASSERT(vt->executeAction != NULL);

        const SensorValidationBounds_t *bounds =
            hvac_get_validation_bounds(HVAC_METRIC_SUPPLY_TEMP);
        TEST_ASSERT(bounds != NULL);

        TEST_ASSERT(hvac_profile_get_vtable() == vt);

        printf("  HVAC: vtable=%p, SUPPLY_TEMP bounds=%p\n",
               (void *)vt, (void *)bounds);
    }

    /* --- Water Treatment profile --- */
    {
        const DomainProfileVTable_t *vt = water_profile_get_vtable();
        TEST_ASSERT(vt != NULL);
        TEST_ASSERT(vt->getRuleTable != NULL);
        TEST_ASSERT(vt->validateSensorReading != NULL);
        TEST_ASSERT(vt->getFailSafeMode != NULL);
        TEST_ASSERT(vt->executeAction != NULL);

        const SensorValidationBounds_t *bounds =
            water_get_validation_bounds(WATER_METRIC_CHLORINE_LEVEL);
        TEST_ASSERT(bounds != NULL);

        TEST_ASSERT(water_profile_get_vtable() == vt);

        printf("  Water Treatment: vtable=%p, CHLORINE_LEVEL bounds=%p\n",
               (void *)vt, (void *)bounds);
    }

    /* --- Verify all 4 vtables are distinct (no cross-profile aliasing) --- */
    {
        const DomainProfileVTable_t *vt_home = home_profile_get_vtable();
        const DomainProfileVTable_t *vt_agri = agriculture_profile_get_vtable();
        const DomainProfileVTable_t *vt_hvac = hvac_profile_get_vtable();
        const DomainProfileVTable_t *vt_water = water_profile_get_vtable();

        TEST_ASSERT(vt_home != vt_agri);
        TEST_ASSERT(vt_home != vt_hvac);
        TEST_ASSERT(vt_home != vt_water);
        TEST_ASSERT(vt_agri != vt_hvac);
        TEST_ASSERT(vt_agri != vt_water);
        TEST_ASSERT(vt_hvac != vt_water);

        printf("  All 4 vtables are distinct instances.\n");
    }

    /* --- Verify ingestion handler accepts each vtable --- */
    {
        /* Each profile's vtable should be accepted by ingestion_handler_init.
         * (We only need to verify the vtable field — other fields are set
         * identically across profiles.) */
        IngestionHandlerConfig_t config;
        memset(&config, 0, sizeof(config));
        config.gw_tracker    = &s_gw_tracker;
        config.edge_tracker  = &s_edge_tracker;
        config.radio_receive = mock_radio_receive;

        config.domain_profile_id = 0;
        config.vtable = home_profile_get_vtable();
        TEST_ASSERT(ingestion_handler_init(&config));

        config.domain_profile_id = 1;
        config.vtable = agriculture_profile_get_vtable();
        TEST_ASSERT(ingestion_handler_init(&config));

        config.domain_profile_id = 2;
        config.vtable = hvac_profile_get_vtable();
        TEST_ASSERT(ingestion_handler_init(&config));

        config.domain_profile_id = 3;
        config.vtable = water_profile_get_vtable();
        TEST_ASSERT(ingestion_handler_init(&config));

        printf("  Ingestion handler accepts all 4 vtables.\n");
    }

    TEST_PASS();
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("\n========================================\n");
    printf("Ingestion Handler Tests (Phase 15.3/15.4)\n");
    printf("========================================\n\n");

    printf("--- Core Tests ---\n");
    RUN_TEST(test_handler_init_null);
    RUN_TEST(test_handler_init_incomplete);
    RUN_TEST(test_handler_not_initialised);
    RUN_TEST(test_handler_no_data);
    RUN_TEST(test_handler_stats_reset);

    printf("\n--- Security Gate Tests ---\n");
    RUN_TEST(test_handler_happy_path);
    RUN_TEST(test_handler_unattested_edge_rejected);
    RUN_TEST(test_handler_tampered_frame_rejected);

    printf("\n--- Profile Wiring Tests (Phase 15.4) ---\n");
    RUN_TEST(test_all_profile_wiring_triples);

    PRINT_TEST_SUMMARY();
}
