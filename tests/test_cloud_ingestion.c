/**
 * @file test_cloud_ingestion.c
 * @brief Tests for cloud multi-tenancy pipeline — ingestion, tenant
 *        isolation, and cardinality warnings.
 *
 * Architecture ref: Section 7 (Cloud multi-tenancy pipeline).
 *
 * Uses mock time-series store (in-memory) since InfluxDB is not
 * available in this sandbox. Real InfluxDB integration is a tracked
 * pre-deployment item.
 */

#include "test_helpers/test_utils.h"
#include "../cloud/telemetry_store.h"
#include "../cloud/ingestion_service.h"
#include "../firmware/shared/mqtt_client.h"
#include <string.h>

/* ================================================================
 * TELEMETRY STORE TESTS
 * ================================================================ */

/* ---------- Test: store init ---------- */
static int test_store_init(void)
{
    TEST_ASSERT(telemetry_store_init() == true);
    TEST_ASSERT_EQUAL(0, telemetry_store_get_cardinality("any_tenant"));
    TEST_PASS();
}

/* ---------- Test: store write ---------- */
static int test_store_write(void)
{
    telemetry_store_init();

    TelemetryDataPoint_t point;
    memset(&point, 0, sizeof(point));
    strcpy(point.tenant_id, "tenant_a");
    strcpy(point.site_id, "site_1");
    strcpy(point.node_id, "node_1");
    strcpy(point.metric_id, "temperature");
    point.value = 22.5f;
    point.timestamp_ms = 1000;

    TEST_ASSERT(telemetry_store_write(&point) == STORE_OK);
    TEST_ASSERT_EQUAL(1, telemetry_store_get_cardinality("tenant_a"));
    TEST_PASS();
}

/* ---------- Test: store query tenant isolation ---------- */
static int test_store_tenant_isolation(void)
{
    telemetry_store_init();

    /* Write data for tenant A. */
    TelemetryDataPoint_t pt_a;
    memset(&pt_a, 0, sizeof(pt_a));
    strcpy(pt_a.tenant_id, "tenant_a");
    strcpy(pt_a.site_id, "site_1");
    strcpy(pt_a.node_id, "node_1");
    strcpy(pt_a.metric_id, "temperature");
    pt_a.value = 22.0f;
    pt_a.timestamp_ms = 1000;
    telemetry_store_write(&pt_a);

    /* Write data for tenant B. */
    TelemetryDataPoint_t pt_b;
    memset(&pt_b, 0, sizeof(pt_b));
    strcpy(pt_b.tenant_id, "tenant_b");
    strcpy(pt_b.site_id, "site_2");
    strcpy(pt_b.node_id, "node_2");
    strcpy(pt_b.metric_id, "humidity");
    pt_b.value = 65.0f;
    pt_b.timestamp_ms = 2000;
    telemetry_store_write(&pt_b);

    /* Query tenant A — must NOT include tenant B's data. */
    TelemetryDataPoint_t results[8];
    uint8_t count = 0;
    TenantToken_t tok_a;
    telemetry_issue_tenant_token("tenant_a", &tok_a);
    TEST_ASSERT(telemetry_store_query(&tok_a, results, 8, &count) == STORE_OK);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT(strcmp(results[0].tenant_id, "tenant_a") == 0);
    TEST_ASSERT(results[0].value == 22.0f);

    /* Query tenant B — must NOT include tenant A's data. */
    TenantToken_t tok_b;
    telemetry_issue_tenant_token("tenant_b", &tok_b);
    TEST_ASSERT(telemetry_store_query(&tok_b, results, 8, &count) == STORE_OK);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT(strcmp(results[0].tenant_id, "tenant_b") == 0);
    TEST_ASSERT(results[0].value == 65.0f);

    /* Query non-existent tenant — must return 0 results. */
    TenantToken_t tok_c;
    telemetry_issue_tenant_token("tenant_c", &tok_c);
    TEST_ASSERT(telemetry_store_query(&tok_c, results, 8, &count) == STORE_OK);
    TEST_ASSERT_EQUAL(0, count);

    TEST_PASS();
}

/* ---------- Test: store multiple series per tenant ---------- */
static int test_store_multiple_series(void)
{
    telemetry_store_init();

    for (int i = 0; i < 5; i++) {
        TelemetryDataPoint_t pt;
        memset(&pt, 0, sizeof(pt));
        strcpy(pt.tenant_id, "tenant_x");
        sprintf(pt.site_id, "site_%d", i);
        strcpy(pt.node_id, "node_1");
        strcpy(pt.metric_id, "temp");
        pt.value = (float)i;
        pt.timestamp_ms = i * 1000;
        telemetry_store_write(&pt);
    }

    /* 5 different site_id values = 5 unique series. */
    TEST_ASSERT_EQUAL(5, telemetry_store_get_cardinality("tenant_x"));

    /* Write duplicate — cardinality should not increase. */
    TelemetryDataPoint_t dup;
    memset(&dup, 0, sizeof(dup));
    strcpy(dup.tenant_id, "tenant_x");
    strcpy(dup.site_id, "site_0");
    strcpy(dup.node_id, "node_1");
    strcpy(dup.metric_id, "temp");
    dup.value = 99.0f;
    dup.timestamp_ms = 9999;
    telemetry_store_write(&dup);

    TEST_ASSERT_EQUAL(5, telemetry_store_get_cardinality("tenant_x"));
    TEST_PASS();
}

/* ---------- Test: store null params ---------- */
static int test_store_null_params(void)
{
    telemetry_store_init();
    TEST_ASSERT(telemetry_store_write(NULL) == STORE_ERR_PARAM_NULL);
    uint8_t count;
    TenantToken_t dummy;
    TEST_ASSERT(telemetry_store_query(NULL, NULL, 0, &count) == STORE_ERR_PARAM_NULL);
    TEST_ASSERT(telemetry_store_query(&dummy, NULL, 0, &count) == STORE_ERR_PARAM_NULL);
    TEST_PASS();
}

/* ================================================================
 * INGESTION SERVICE TESTS
 * ================================================================ */

/* ---------- Test: topic parsing ---------- */
static int test_ingestion_parse_topic(void)
{
    ingestion_service_init(NULL);

    char tenant[64], site[64], device[64];
    TEST_ASSERT(ingestion_parse_topic(
        "rainmaker/acme/site1/hub01/telemetry",
        tenant, site, device) == INGEST_OK);
    TEST_ASSERT(strcmp(tenant, "acme") == 0);
    TEST_ASSERT(strcmp(site, "site1") == 0);
    TEST_ASSERT(strcmp(device, "hub01") == 0);
    TEST_PASS();
}

/* ---------- Test: malformed topic rejected ---------- */
static int test_ingestion_malformed_topic(void)
{
    ingestion_service_init(NULL);

    char tenant[64], site[64], device[64];

    /* Missing "rainmaker" prefix. */
    TEST_ASSERT(ingestion_parse_topic("wrong/site/dev/telemetry", tenant, site, device)
                == INGEST_ERR_TOPIC_PARSE);

    /* Missing tenant. */
    TEST_ASSERT(ingestion_parse_topic("rainmaker//site/dev/telemetry", tenant, site, device)
                == INGEST_ERR_TOPIC_PARSE);

    /* Missing telemetry suffix. */
    TEST_ASSERT(ingestion_parse_topic("rainmaker/t/s/d/other", tenant, site, device)
                == INGEST_ERR_TOPIC_PARSE);

    /* Too few components. */
    TEST_ASSERT(ingestion_parse_topic("rainmaker/tenant", tenant, site, device)
                == INGEST_ERR_TOPIC_PARSE);

    /* NULL topic. */
    TEST_ASSERT(ingestion_parse_topic(NULL, tenant, site, device)
                == INGEST_ERR_PARAM_NULL);

    TEST_PASS();
}

/* ---------- Test: full ingest flow ---------- */
static int test_ingestion_full_flow(void)
{
    telemetry_store_init();
    ingestion_service_init(NULL);

    /* Ingest via topic + payload. */
    IngestionResult_t result = ingestion_ingest(
        "rainmaker/acme/site1/hub01/telemetry",
        "temperature:23.5", 6, 1000);
    TEST_ASSERT(result == INGEST_OK);

    /* Verify data is in the store, scoped to tenant. */
    TelemetryDataPoint_t results[8];
    uint8_t count = 0;
    TenantToken_t tok;
    telemetry_issue_tenant_token("acme", &tok);
    TEST_ASSERT(telemetry_store_query(&tok, results, 8, &count) == STORE_OK);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT(strcmp(results[0].tenant_id, "acme") == 0);
    TEST_ASSERT(strcmp(results[0].node_id, "hub01") == 0);
    TEST_ASSERT(results[0].value == 23.5f);
    TEST_PASS();
}

/* ================================================================
 * TENANT TOKEN AUTHORIZATION TESTS
 * ================================================================ */

/* ---------- Test: token cannot query other tenant ---------- */
static int test_tenant_token_cannot_query_other_tenant(void)
{
    telemetry_store_init();

    /* Write data for tenant A and tenant B. */
    TelemetryDataPoint_t pt_a;
    memset(&pt_a, 0, sizeof(pt_a));
    strcpy(pt_a.tenant_id, "tenant_a");
    strcpy(pt_a.site_id, "site_1");
    strcpy(pt_a.node_id, "node_1");
    strcpy(pt_a.metric_id, "temp");
    pt_a.value = 22.0f;
    telemetry_store_write(&pt_a);

    TelemetryDataPoint_t pt_b;
    memset(&pt_b, 0, sizeof(pt_b));
    strcpy(pt_b.tenant_id, "tenant_b");
    strcpy(pt_b.site_id, "site_2");
    strcpy(pt_b.node_id, "node_2");
    strcpy(pt_b.metric_id, "humidity");
    pt_b.value = 65.0f;
    telemetry_store_write(&pt_b);

    /* Issue a token for tenant A only. */
    TenantToken_t tok_a;
    TEST_ASSERT(telemetry_issue_tenant_token("tenant_a", &tok_a) == STORE_OK);
    TEST_ASSERT(tok_a.valid == true);
    TEST_ASSERT(strcmp(tok_a.bound_tenant_id, "tenant_a") == 0);

    /* Token A can query tenant A's data — must succeed. */
    TelemetryDataPoint_t results[8];
    uint8_t count = 0;
    TEST_ASSERT(telemetry_store_query(&tok_a, results, 8, &count) == STORE_OK);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT(strcmp(results[0].tenant_id, "tenant_a") == 0);

    /* CRITICAL: there is no way for this token to query tenant B.
     * The token is bound to tenant_a — the store uses the token's
     * bound_tenant_id internally, not any caller-supplied string.
     * The old API accepted a raw tenant_id string that could be anything.
     * The new API forces the tenant from the token. */

    /* Revoke the token and confirm query is rejected. */
    tok_a.valid = false;
    TEST_ASSERT(telemetry_store_query(&tok_a, results, 8, &count) == STORE_ERR_UNAUTHORIZED);

    TEST_PASS();
}

/* ---------- Test: query requires valid token ---------- */
static int test_tenant_token_required(void)
{
    telemetry_store_init();

    /* Write some data. */
    TelemetryDataPoint_t pt;
    memset(&pt, 0, sizeof(pt));
    strcpy(pt.tenant_id, "tenant_x");
    strcpy(pt.site_id, "site_1");
    strcpy(pt.node_id, "node_1");
    strcpy(pt.metric_id, "temp");
    pt.value = 20.0f;
    telemetry_store_write(&pt);

    TelemetryDataPoint_t results[8];
    uint8_t count = 0;

    /* NULL token rejected. */
    TEST_ASSERT(telemetry_store_query(NULL, results, 8, &count) == STORE_ERR_PARAM_NULL);

    /* Invalid/uninitialized token rejected. */
    TenantToken_t bad_token;
    memset(&bad_token, 0, sizeof(bad_token));
    TEST_ASSERT(telemetry_store_query(&bad_token, results, 8, &count) == STORE_ERR_UNAUTHORIZED);

    /* Revoked token rejected. */
    TenantToken_t valid_token;
    telemetry_issue_tenant_token("tenant_x", &valid_token);
    valid_token.valid = false;
    TEST_ASSERT(telemetry_store_query(&valid_token, results, 8, &count) == STORE_ERR_UNAUTHORIZED);

    /* Empty-bound token rejected. */
    TenantToken_t empty_token;
    memset(&empty_token, 0, sizeof(empty_token));
    empty_token.valid = true;
    TEST_ASSERT(telemetry_store_query(&empty_token, results, 8, &count) == STORE_ERR_UNAUTHORIZED);

    TEST_PASS();
}

/* ---------- Test: token issuance ---------- */
static int test_tenant_token_issuance(void)
{
    telemetry_store_init();

    TenantToken_t token;
    TEST_ASSERT(telemetry_issue_tenant_token("my_tenant", &token) == STORE_OK);
    TEST_ASSERT(token.valid == true);
    TEST_ASSERT(token.token_id > 0);
    TEST_ASSERT(strcmp(token.bound_tenant_id, "my_tenant") == 0);

    /* Two tokens for same tenant get different IDs. */
    TenantToken_t token2;
    telemetry_issue_tenant_token("my_tenant", &token2);
    TEST_ASSERT(token.token_id != token2.token_id);

    TEST_PASS();
}

/* ================================================================
 * CROSS-TENANT ISOLATION — THE CRITICAL INTEGRATION TEST
 * ================================================================ */

static int test_cross_tenant_isolation(void)
{
    telemetry_store_init();
    ingestion_service_init(NULL);

    /* Ingest telemetry for two different tenants. */
    ingestion_ingest("rainmaker/tenant_a/site1/hub1/telemetry",
                     "temp:20.0", 8, 1000);
    ingestion_ingest("rainmaker/tenant_a/site1/hub2/telemetry",
                     "temp:21.0", 8, 2000);
    ingestion_ingest("rainmaker/tenant_b/site2/hub3/telemetry",
                     "temp:30.0", 8, 3000);
    ingestion_ingest("rainmaker/tenant_b/site2/hub4/telemetry",
                     "temp:31.0", 8, 4000);

    /* Query tenant A — must have exactly 2 points, none from tenant B. */
    TelemetryDataPoint_t results[16];
    uint8_t count = 0;
    TenantToken_t tok_a;
    telemetry_issue_tenant_token("tenant_a", &tok_a);
    TEST_ASSERT(telemetry_store_query(&tok_a, results, 16, &count) == STORE_OK);
    TEST_ASSERT_EQUAL(2, count);
    for (uint8_t i = 0; i < count; i++) {
        TEST_ASSERT(strcmp(results[i].tenant_id, "tenant_a") == 0);
    }

    /* Query tenant B — must have exactly 2 points, none from tenant A. */
    TenantToken_t tok_b;
    telemetry_issue_tenant_token("tenant_b", &tok_b);
    TEST_ASSERT(telemetry_store_query(&tok_b, results, 16, &count) == STORE_OK);
    TEST_ASSERT_EQUAL(2, count);
    for (uint8_t i = 0; i < count; i++) {
        TEST_ASSERT(strcmp(results[i].tenant_id, "tenant_b") == 0);
    }

    /* Cardinality: tenant_a has 2 unique series, tenant_b has 2. */
    TEST_ASSERT_EQUAL(2, telemetry_store_get_cardinality("tenant_a"));
    TEST_ASSERT_EQUAL(2, telemetry_store_get_cardinality("tenant_b"));

    TEST_PASS();
}

/* ================================================================
 * CARDINALITY WARNING TESTS
 * ================================================================ */

static int s_warnings_fired = 0;
static char s_warned_tenant[64] = {0};

static void test_cardinality_warning(const char *tenant_id,
                                      uint32_t current_count,
                                      uint32_t threshold)
{
    s_warnings_fired++;
    strncpy(s_warned_tenant, tenant_id, sizeof(s_warned_tenant) - 1);
    (void)current_count;
    (void)threshold;
}

static int test_cardinality_warning_fires(void)
{
    telemetry_store_init();
    IngestionConfig_t config = { .cardinality_warning_threshold = 3 };
    ingestion_service_init(&config);
    ingestion_set_cardinality_callback(test_cardinality_warning);

    s_warnings_fired = 0;

    /* Write 4 unique series for tenant_x — threshold is 3.
     * Use valid topics so ingestion_ingest parses them correctly. */
    for (int i = 0; i < 4; i++) {
        char topic[64];
        sprintf(topic, "rainmaker/tenant_x/site_%d/hub_1/telemetry", i);
        ingestion_ingest(topic, "temp:0", 5, i * 1000);
    }

    /* With threshold=3, cardinality exceeds threshold at count=4.
     * The ingestion service checks cardinality after each write and
     * fires the callback when it exceeds the threshold. */
    TEST_ASSERT(s_warnings_fired >= 1);
    TEST_ASSERT(strcmp(s_warned_tenant, "tenant_x") == 0);
    TEST_PASS();
}

/* ---------- Test: cardinality below threshold no warning ---------- */
static int test_cardinality_below_threshold(void)
{
    telemetry_store_init();
    IngestionConfig_t config = { .cardinality_warning_threshold = 100 };
    ingestion_service_init(&config);
    ingestion_set_cardinality_callback(test_cardinality_warning);

    s_warnings_fired = 0;

    for (int i = 0; i < 5; i++) {
        TelemetryDataPoint_t pt;
        memset(&pt, 0, sizeof(pt));
        strcpy(pt.tenant_id, "tenant_y");
        sprintf(pt.site_id, "site_%d", i);
        strcpy(pt.node_id, "node_1");
        strcpy(pt.metric_id, "temp");
        pt.value = (float)i;
        telemetry_store_write(&pt);
    }

    /* 5 series < 100 threshold — no warning expected via store directly. */
    TEST_ASSERT(telemetry_store_cardinality_exceeds("tenant_y", 100) == false);
    TEST_PASS();
}

/* ================================================================
 * MQTT TOPIC BUILDER EXTENDED TESTS
 * ================================================================ */

static int test_mqtt_topic_tenant_scoped(void)
{
    char topic[128];

    /* Normal construction. */
    TEST_ASSERT(mqtt_build_telemetry_topic(topic, sizeof(topic),
        "tenant_a", "site_1", "hub_01") == true);
    TEST_ASSERT(strcmp(topic, "rainmaker/tenant_a/site_1/hub_01/telemetry") == 0);

    /* NULL tenant rejected. */
    TEST_ASSERT(mqtt_build_telemetry_topic(topic, sizeof(topic),
        NULL, "site_1", "hub_01") == false);

    /* NULL site rejected. */
    TEST_ASSERT(mqtt_build_telemetry_topic(topic, sizeof(topic),
        "tenant_a", NULL, "hub_01") == false);

    /* NULL device rejected. */
    TEST_ASSERT(mqtt_build_telemetry_topic(topic, sizeof(topic),
        "tenant_a", "site_1", NULL) == false);

    /* Buffer too small rejected. */
    TEST_ASSERT(mqtt_build_telemetry_topic(topic, 5,
        "tenant_a", "site_1", "hub_01") == false);

    TEST_PASS();
}

/* ================================================================
 * MAIN
 * ================================================================ */

int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_cloud_ingestion ===\n");

    /* Telemetry store tests. */
    RUN_TEST(test_store_init);
    RUN_TEST(test_store_write);
    RUN_TEST(test_store_tenant_isolation);
    RUN_TEST(test_store_multiple_series);
    RUN_TEST(test_store_null_params);
    RUN_TEST(test_tenant_token_cannot_query_other_tenant);
    RUN_TEST(test_tenant_token_required);
    RUN_TEST(test_tenant_token_issuance);

    /* Ingestion service tests. */
    RUN_TEST(test_ingestion_parse_topic);
    RUN_TEST(test_ingestion_malformed_topic);
    RUN_TEST(test_ingestion_full_flow);

    /* Cross-tenant isolation (critical). */
    RUN_TEST(test_cross_tenant_isolation);

    /* Cardinality warning tests. */
    RUN_TEST(test_cardinality_warning_fires);
    RUN_TEST(test_cardinality_below_threshold);

    /* MQTT topic builder tests. */
    RUN_TEST(test_mqtt_topic_tenant_scoped);

    PRINT_TEST_SUMMARY();
}
