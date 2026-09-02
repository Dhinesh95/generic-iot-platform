/**
 * @file mqtt_client.c
 * @brief MQTT/TLS client implementation.
 *
 * Architecture ref: Section 3, Tier 3↔4.
 *
 * IMPLEMENTATION STATUS:
 *   ESP32 TARGET: Uses esp_mqtt_client (ESP-IDF MQTT library) with real
 *                 TCP connection and TLS via esp_tls. This provides genuine
 *                 MQTT CONNECT/PUBLISH/SUBSCRIBE protocol exchange.
 *   NATIVE TEST: Stub implementation for API surface testing. The stub
 *                 maintains state consistency but does not perform real I/O.
 *
 * PRE-FIELD-DEPLOYMENT ACTION ITEM:
 *   - Validate TLS certificate chain against real broker
 *   - Test with production MQTT broker (not just local mock)
 *   - Verify reconnection logic under network instability
 *
 * Out of scope: fleet orchestration, multi-tenancy logic.
 */

#include "mqtt_client.h"
#include <string.h>
#include <stdio.h>

#ifdef ESP_PLATFORM
/* ===== ESP32 target: real MQTT via esp_mqtt_client ===== */
#include <mqtt_client.h>  /* esp-mqtt API — use angle brackets to avoid picking up our own header */

/* Internal adapter: map our config to esp_mqtt_client_config_t */
static esp_mqtt_client_handle_t s_client = NULL;
static MQTTState_t g_state = MQTT_STATE_DISCONNECTED;
static bool g_initialised = false;

/* Subscription tracking (for unsubscribe support). */
#define MQTT_MAX_SUBSCRIPTIONS  8
typedef struct {
    char topic_filter[MQTT_MAX_TOPIC_LEN];
    MQTTQoS_t qos;
    mqtt_message_callback_t callback;
    bool active;
    int esp_msg_id;  /* ESP MQTT subscription message ID */
} MQTTSubscription_t;
static MQTTSubscription_t g_subscriptions[MQTT_MAX_SUBSCRIPTIONS];

/**
 * ESP MQTT event handler.
 * Routes events to our internal state machine and subscription callbacks.
 */
static void esp_mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                    int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        g_state = MQTT_STATE_CONNECTED;
        break;

    case MQTT_EVENT_DISCONNECTED:
        g_state = MQTT_STATE_DISCONNECTED;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        /* Match to our subscription table and invoke callback. */
        for (int i = 0; i < MQTT_MAX_SUBSCRIPTIONS; i++) {
            if (g_subscriptions[i].active &&
                g_subscriptions[i].esp_msg_id == event->msg_id) {
                /* Subscription confirmed — callback will be invoked on DATA events. */
                break;
            }
        }
        break;

    case MQTT_EVENT_DATA: {
        /* Route received message to matching subscription callback. */
        char topic_buf[MQTT_MAX_TOPIC_LEN];
        size_t topic_len = (event->topic_len < MQTT_MAX_TOPIC_LEN - 1)
                         ? event->topic_len : MQTT_MAX_TOPIC_LEN - 1;
        memcpy(topic_buf, event->topic, topic_len);
        topic_buf[topic_len] = '\0';

        for (int i = 0; i < MQTT_MAX_SUBSCRIPTIONS; i++) {
            if (g_subscriptions[i].active &&
                strcmp(g_subscriptions[i].topic_filter, topic_buf) == 0) {
                MQTTMessage_t msg;
                memset(&msg, 0, sizeof(msg));
                strncpy(msg.topic, topic_buf, MQTT_MAX_TOPIC_LEN - 1);
                size_t data_len = (event->data_len < MQTT_MAX_PAYLOAD)
                                ? event->data_len : MQTT_MAX_PAYLOAD;
                memcpy(msg.payload, event->data, data_len);
                msg.payload_len = (uint16_t)data_len;
                msg.qos = g_subscriptions[i].qos;

                if (g_subscriptions[i].callback) {
                    g_subscriptions[i].callback(&msg);
                }
                break;
            }
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        g_state = MQTT_STATE_ERROR;
        break;

    default:
        break;
    }
}

bool mqtt_client_init(void)
{
    memset(g_subscriptions, 0, sizeof(g_subscriptions));
    g_state = MQTT_STATE_DISCONNECTED;
    g_initialised = true;
    return true;
}

bool mqtt_client_configure(const MQTTClientConfig_t *config)
{
    if (!config) return false;
    if (!g_initialised) return false;

    esp_mqtt_client_config_t esp_cfg = {0};
    esp_cfg.host = config->broker_host;
    esp_cfg.port = config->broker_port;
    esp_cfg.client_id = config->client_id;
    esp_cfg.username = config->username;
    esp_cfg.password = config->password;
    esp_cfg.keepalive = config->keepalive_sec;

    if (config->use_tls && config->ca_cert_pem) {
        esp_cfg.cert_pem = config->ca_cert_pem;
    }

    s_client = esp_mqtt_client_init(&esp_cfg);
    if (!s_client) return false;

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                    esp_mqtt_event_handler, NULL);

    return true;
}

bool mqtt_client_connect(void)
{
    if (!g_initialised || !s_client) return false;

    g_state = MQTT_STATE_CONNECTING;
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        g_state = MQTT_STATE_ERROR;
        return false;
    }

    /* Note: connection is asynchronous; state transitions to CONNECTED
     * via the event handler. For API compatibility, we return true
     * indicating the connect attempt was initiated successfully. */
    return true;
}

bool mqtt_client_disconnect(void)
{
    if (!g_initialised || !s_client) return false;

    esp_err_t err = esp_mqtt_client_stop(s_client);
    g_state = MQTT_STATE_DISCONNECTED;
    return (err == ESP_OK);
}

bool mqtt_client_publish(
    const char *topic,
    const uint8_t *payload, uint16_t payload_len,
    MQTTQoS_t qos,
    bool retain)
{
    if (!g_initialised || !s_client) return false;
    if (g_state != MQTT_STATE_CONNECTED) return false;
    if (!topic || !payload) return false;
    if (payload_len > MQTT_MAX_PAYLOAD) return false;

    int msg_id = esp_mqtt_client_publish(
        s_client, topic, (const char *)payload, payload_len,
        (int)qos, retain ? 1 : 0);

    return (msg_id >= 0);
}

bool mqtt_client_subscribe(
    const char *topic,
    MQTTQoS_t qos,
    mqtt_message_callback_t callback)
{
    if (!g_initialised || !s_client) return false;
    if (!topic || !callback) return false;

    /* Find free subscription slot. */
    int slot = -1;
    for (int i = 0; i < MQTT_MAX_SUBSCRIPTIONS; i++) {
        if (!g_subscriptions[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return false;

    int msg_id = esp_mqtt_client_subscribe(s_client, topic, (int)qos);
    if (msg_id < 0) return false;

    strncpy(g_subscriptions[slot].topic_filter, topic, MQTT_MAX_TOPIC_LEN - 1);
    g_subscriptions[slot].topic_filter[MQTT_MAX_TOPIC_LEN - 1] = '\0';
    g_subscriptions[slot].qos = qos;
    g_subscriptions[slot].callback = callback;
    g_subscriptions[slot].esp_msg_id = msg_id;
    g_subscriptions[slot].active = true;

    return true;
}

bool mqtt_client_unsubscribe(const char *topic)
{
    if (!g_initialised || !s_client) return false;
    if (!topic) return false;

    for (int i = 0; i < MQTT_MAX_SUBSCRIPTIONS; i++) {
        if (g_subscriptions[i].active &&
            strcmp(g_subscriptions[i].topic_filter, topic) == 0) {
            esp_mqtt_client_unsubscribe(s_client, topic);
            g_subscriptions[i].active = false;
            g_subscriptions[i].callback = NULL;
            return true;
        }
    }
    return false;
}

MQTTState_t mqtt_client_get_state(void)
{
    return g_state;
}

#else
/* ===== Native test build: stub implementation ===== */

static MQTTClientConfig_t g_config;
static MQTTState_t g_state = MQTT_STATE_DISCONNECTED;
static bool g_initialised = false;

#define MQTT_MAX_SUBSCRIPTIONS  8
typedef struct {
    char topic_filter[MQTT_MAX_TOPIC_LEN];
    MQTTQoS_t qos;
    mqtt_message_callback_t callback;
    bool active;
} MQTTSubscription_t;
static MQTTSubscription_t g_subscriptions[MQTT_MAX_SUBSCRIPTIONS];

bool mqtt_client_init(void)
{
    memset(&g_config, 0, sizeof(g_config));
    memset(g_subscriptions, 0, sizeof(g_subscriptions));
    g_state = MQTT_STATE_DISCONNECTED;
    g_initialised = true;
    return true;
}

bool mqtt_client_configure(const MQTTClientConfig_t *config)
{
    if (!config) return false;
    if (!g_initialised) return false;

    memcpy(&g_config, config, sizeof(MQTTClientConfig_t));

    if (g_config.keepalive_sec == 0) {
        g_config.keepalive_sec = MQTT_KEEPALIVE_SEC;
    }
    if (g_config.broker_port == 0) {
        g_config.broker_port = MQTT_DEFAULT_PORT;
    }

    return true;
}

bool mqtt_client_connect(void)
{
    if (!g_initialised) return false;
    if (!g_config.broker_host) return false;

    g_state = MQTT_STATE_CONNECTING;
    /*
     * NATIVE STUB: Simulates a successful connection.
     * On ESP32 target, this performs real TCP + TLS + MQTT CONNECT.
     */
    g_state = MQTT_STATE_CONNECTED;
    return true;
}

bool mqtt_client_disconnect(void)
{
    if (!g_initialised) return false;
    g_state = MQTT_STATE_DISCONNECTED;
    return true;
}

bool mqtt_client_publish(
    const char *topic,
    const uint8_t *payload, uint16_t payload_len,
    MQTTQoS_t qos,
    bool retain)
{
    if (!g_initialised) return false;
    if (g_state != MQTT_STATE_CONNECTED) return false;
    if (!topic || !payload) return false;
    if (payload_len > MQTT_MAX_PAYLOAD) return false;
    (void)qos;
    (void)retain;

    /* NATIVE STUB: Silently succeed. */
    return true;
}

bool mqtt_client_subscribe(
    const char *topic,
    MQTTQoS_t qos,
    mqtt_message_callback_t callback)
{
    if (!g_initialised) return false;
    if (!topic || !callback) return false;

    for (int i = 0; i < MQTT_MAX_SUBSCRIPTIONS; i++) {
        if (!g_subscriptions[i].active) {
            strncpy(g_subscriptions[i].topic_filter, topic, MQTT_MAX_TOPIC_LEN - 1);
            g_subscriptions[i].topic_filter[MQTT_MAX_TOPIC_LEN - 1] = '\0';
            g_subscriptions[i].qos = qos;
            g_subscriptions[i].callback = callback;
            g_subscriptions[i].active = true;
            return true;
        }
    }

    return false;
}

bool mqtt_client_unsubscribe(const char *topic)
{
    if (!g_initialised) return false;
    if (!topic) return false;

    for (int i = 0; i < MQTT_MAX_SUBSCRIPTIONS; i++) {
        if (g_subscriptions[i].active &&
            strcmp(g_subscriptions[i].topic_filter, topic) == 0) {
            g_subscriptions[i].active = false;
            g_subscriptions[i].callback = NULL;
            return true;
        }
    }
    return false;
}

MQTTState_t mqtt_client_get_state(void)
{
    return g_state;
}

#endif /* ESP_PLATFORM */

/* Common code (both platforms) */

bool mqtt_build_telemetry_topic(
    char *out_topic, size_t buf_size,
    const char *tenant_id, const char *site_id, const char *device_id)
{
    if (!out_topic || !tenant_id || !site_id || !device_id) return false;

    int written = snprintf(out_topic, buf_size,
                           "rainmaker/%s/%s/%s/telemetry",
                           tenant_id, site_id, device_id);

    return (written > 0 && (size_t)written < buf_size);
}
