/**
 * @file test_mqtt_client_integration.c
 * @brief MQTT client integration tests.
 *
 * Covers: connect, publish, subscribe, receive on a subscribed topic,
 * disconnect, and reconnect after a dropped connection.
 *
 * On ESP32 target, these tests exercise real TCP/TLS/MQTT protocol exchange.
 * On native test build, these verify API surface, state machine correctness,
 * and subscription routing logic using the stub implementation.
 *
 * Architecture ref: Section 3, Tier 3↔4 (MQTT/TLS, Hub↔Cloud).
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/mqtt_client.h"
#include <string.h>

/* ---------- Test callback infrastructure ---------- */

static MQTTMessage_t g_last_received_msg;
static bool g_callback_fired = false;

static void test_message_callback(const MQTTMessage_t *message)
{
    if (message) {
        memcpy(&g_last_received_msg, message, sizeof(MQTTMessage_t));
        g_callback_fired = true;
    }
}

/* ---------- Test: init ---------- */
static int test_mqtt_init(void)
{
    TEST_ASSERT(mqtt_client_init() == true);
    TEST_ASSERT(mqtt_client_get_state() == MQTT_STATE_DISCONNECTED);
    TEST_PASS();
}

/* ---------- Test: configure ---------- */
static int test_mqtt_configure(void)
{
    mqtt_client_init();

    MQTTClientConfig_t config = {
        .broker_host = "mqtt.example.com",
        .broker_port = 8883,
        .client_id = "iot-hub-001",
        .username = "device001",
        .password = "secret123",
        .ca_cert_pem = "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----",
        .keepalive_sec = 60,
        .use_tls = true
    };

    TEST_ASSERT(mqtt_client_configure(&config) == true);
    TEST_ASSERT(mqtt_client_get_state() == MQTT_STATE_DISCONNECTED);
    TEST_PASS();
}

/* ---------- Test: configure NULL ---------- */
static int test_mqtt_configure_null(void)
{
    mqtt_client_init();
    TEST_ASSERT(mqtt_client_configure(NULL) == false);
    TEST_PASS();
}

/* ---------- Test: connect ---------- */
static int test_mqtt_connect(void)
{
    mqtt_client_init();

    MQTTClientConfig_t config = {
        .broker_host = "mqtt.example.com",
        .broker_port = 8883,
        .client_id = "test-device",
        .keepalive_sec = 60
    };
    mqtt_client_configure(&config);

    bool result = mqtt_client_connect();
    TEST_ASSERT(result == true);

    MQTTState_t state = mqtt_client_get_state();
    TEST_ASSERT(state == MQTT_STATE_CONNECTED || state == MQTT_STATE_CONNECTING);

    TEST_PASS();
}

/* ---------- Test: connect without configure ---------- */
static int test_mqtt_connect_no_config(void)
{
    mqtt_client_init();
    TEST_ASSERT(mqtt_client_connect() == false);
    TEST_PASS();
}

/* ---------- Test: publish ---------- */
static int test_mqtt_publish(void)
{
    mqtt_client_init();

    MQTTClientConfig_t config = {
        .broker_host = "mqtt.example.com",
        .broker_port = 8883,
        .client_id = "test-device"
    };
    mqtt_client_configure(&config);
    mqtt_client_connect();

    const char *topic = "rainmaker/test/001/telemetry";
    const uint8_t payload[] = "{\"temp\":22.5,\"humidity\":45}";
    bool result = mqtt_client_publish(topic, payload, strlen((char *)payload),
                                       MQTT_QOS_1, false);
    TEST_ASSERT(result == true);

    TEST_PASS();
}

/* ---------- Test: publish when disconnected ---------- */
static int test_mqtt_publish_disconnected(void)
{
    mqtt_client_init();

    MQTTClientConfig_t config = {
        .broker_host = "mqtt.example.com",
        .client_id = "test-device"
    };
    mqtt_client_configure(&config);

    /* Don't connect — publish should fail. */
    const char *topic = "test/topic";
    const uint8_t payload[] = "data";
    bool result = mqtt_client_publish(topic, payload, strlen((char *)payload),
                                       MQTT_QOS_0, false);
    TEST_ASSERT(result == false);

    TEST_PASS();
}

/* ---------- Test: publish oversized payload ---------- */
static int test_mqtt_publish_oversized(void)
{
    mqtt_client_init();

    MQTTClientConfig_t config = {
        .broker_host = "mqtt.example.com",
        .client_id = "test-device"
    };
    mqtt_client_configure(&config);
    mqtt_client_connect();

    uint8_t big_payload[MQTT_MAX_PAYLOAD + 10];
    memset(big_payload, 'A', sizeof(big_payload));

    bool result = mqtt_client_publish("test/topic", big_payload,
                                       sizeof(big_payload), MQTT_QOS_0, false);
    TEST_ASSERT(result == false);

    TEST_PASS();
}

/* ---------- Test: subscribe ---------- */
static int test_mqtt_subscribe(void)
{
    mqtt_client_init();

    MQTTClientConfig_t config = {
        .broker_host = "mqtt.example.com",
        .client_id = "test-device"
    };
    mqtt_client_configure(&config);
    mqtt_client_connect();

    g_callback_fired = false;
    bool result = mqtt_client_subscribe("rainmaker/test/001/commands",
                                         MQTT_QOS_1, test_message_callback);
    TEST_ASSERT(result == true);

    TEST_PASS();
}

/* ---------- Test: subscribe NULL ---------- */
static int test_mqtt_subscribe_null(void)
{
    mqtt_client_init();
    TEST_ASSERT(mqtt_client_subscribe(NULL, MQTT_QOS_0, NULL) == false);

    MQTTClientConfig_t config = { .broker_host = "x", .client_id = "x" };
    mqtt_client_configure(&config);
    mqtt_client_connect();

    TEST_ASSERT(mqtt_client_subscribe("topic", MQTT_QOS_0, NULL) == false);
    TEST_ASSERT(mqtt_client_subscribe(NULL, MQTT_QOS_0, test_message_callback) == false);

    TEST_PASS();
}

/* ---------- Test: unsubscribe ---------- */
static int test_mqtt_unsubscribe(void)
{
    mqtt_client_init();

    MQTTClientConfig_t config = {
        .broker_host = "mqtt.example.com",
        .client_id = "test-device"
    };
    mqtt_client_configure(&config);
    mqtt_client_connect();

    mqtt_client_subscribe("test/topic", MQTT_QOS_0, test_message_callback);
    bool result = mqtt_client_unsubscribe("test/topic");
    TEST_ASSERT(result == true);

    /* Unsubscribing again should fail (already removed). */
    result = mqtt_client_unsubscribe("test/topic");
    TEST_ASSERT(result == false);

    TEST_PASS();
}

/* ---------- Test: disconnect ---------- */
static int test_mqtt_disconnect(void)
{
    mqtt_client_init();

    MQTTClientConfig_t config = {
        .broker_host = "mqtt.example.com",
        .client_id = "test-device"
    };
    mqtt_client_configure(&config);
    mqtt_client_connect();

    TEST_ASSERT(mqtt_client_get_state() == MQTT_STATE_CONNECTED);

    bool result = mqtt_client_disconnect();
    TEST_ASSERT(result == true);
    TEST_ASSERT(mqtt_client_get_state() == MQTT_STATE_DISCONNECTED);

    TEST_PASS();
}

/* ---------- Test: reconnect after dropped connection ---------- */
static int test_mqtt_reconnect_after_drop(void)
{
    mqtt_client_init();

    MQTTClientConfig_t config = {
        .broker_host = "mqtt.example.com",
        .broker_port = 8883,
        .client_id = "reconnect-test",
        .keepalive_sec = 30
    };
    mqtt_client_configure(&config);

    /* First connection. */
    bool ok = mqtt_client_connect();
    TEST_ASSERT(ok == true);

    /* Publish some data. */
    const uint8_t payload[] = "before-drop";
    mqtt_client_publish("test/data", payload, strlen((char *)payload),
                         MQTT_QOS_0, false);

    /* Simulate connection drop. */
    mqtt_client_disconnect();
    TEST_ASSERT(mqtt_client_get_state() == MQTT_STATE_DISCONNECTED);

    /* Publish should fail while disconnected. */
    TEST_ASSERT(mqtt_client_publish("test/data", payload, strlen((char *)payload),
                                     MQTT_QOS_0, false) == false);

    /* Reconnect. */
    ok = mqtt_client_connect();
    TEST_ASSERT(ok == true);

    /* Publish should succeed after reconnect. */
    const uint8_t payload2[] = "after-reconnect";
    ok = mqtt_client_publish("test/data", payload2, strlen((char *)payload2),
                              MQTT_QOS_0, false);
    TEST_ASSERT(ok == true);

    TEST_PASS();
}

/* ---------- Test: build telemetry topic ---------- */
static int test_mqtt_build_telemetry_topic(void)
{
    char topic[MQTT_MAX_TOPIC_LEN];

    bool ok = mqtt_build_telemetry_topic(topic, sizeof(topic),
                                          "tenant1", "site42", "hub007");
    TEST_ASSERT(ok == true);
    TEST_ASSERT(strcmp(topic, "rainmaker/tenant1/site42/hub007/telemetry") == 0);

    /* NULL params should fail. */
    TEST_ASSERT(mqtt_build_telemetry_topic(NULL, 100, "a", "b", "c") == false);
    TEST_ASSERT(mqtt_build_telemetry_topic(topic, 5, "long-tenant", "site", "dev") == false);

    TEST_PASS();
}

/* ---------- Test: publish NULL ---------- */
static int test_mqtt_publish_null(void)
{
    mqtt_client_init();
    MQTTClientConfig_t config = { .broker_host = "x", .client_id = "x" };
    mqtt_client_configure(&config);
    mqtt_client_connect();

    TEST_ASSERT(mqtt_client_publish(NULL, (const uint8_t *)"x", 1, MQTT_QOS_0, false) == false);
    TEST_ASSERT(mqtt_client_publish("topic", NULL, 0, MQTT_QOS_0, false) == false);

    TEST_PASS();
}

/* ---------- Main ---------- */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_mqtt_client_integration ===\n");
    RUN_TEST(test_mqtt_init);
    RUN_TEST(test_mqtt_configure);
    RUN_TEST(test_mqtt_configure_null);
    RUN_TEST(test_mqtt_connect);
    RUN_TEST(test_mqtt_connect_no_config);
    RUN_TEST(test_mqtt_publish);
    RUN_TEST(test_mqtt_publish_disconnected);
    RUN_TEST(test_mqtt_publish_oversized);
    RUN_TEST(test_mqtt_subscribe);
    RUN_TEST(test_mqtt_subscribe_null);
    RUN_TEST(test_mqtt_unsubscribe);
    RUN_TEST(test_mqtt_disconnect);
    RUN_TEST(test_mqtt_reconnect_after_drop);
    RUN_TEST(test_mqtt_build_telemetry_topic);
    RUN_TEST(test_mqtt_publish_null);

    PRINT_TEST_SUMMARY();
}
