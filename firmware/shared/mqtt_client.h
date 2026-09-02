/**
 * @file mqtt_client.h
 * @brief MQTT/TLS client — Hub ↔ Cloud (domain-agnostic).
 *
 * Architecture ref: Section 3, Tier 3↔4 (MQTT/TLS, domain-agnostic).
 *
 * Basic publish, subscribe, and connection management.
 * Fleet-wide features (multi-tenancy, canary rollout) are NOT
 * implemented in Phase 1.
 */

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------- Constants ---------- */

#define MQTT_MAX_TOPIC_LEN    128  /**< Maximum topic string length. */
#define MQTT_MAX_PAYLOAD      256  /**< Maximum message payload size. */
#define MQTT_KEEPALIVE_SEC    60   /**< Default keepalive interval. */
#define MQTT_DEFAULT_PORT     8883 /**< Default MQTT/TLS port. */

/**
 * MQTT topic namespace per architecture Section 7:
 *   rainmaker/{tenant_id}/{site_id}/{device_id}/telemetry
 *
 * Phase 1: tenant_id and site_id are placeholder constants.
 */

/* ---------- Types ---------- */

/**
 * MQTT connection state.
 */
typedef enum {
    MQTT_STATE_DISCONNECTED = 0,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_ERROR
} MQTTState_t;

/**
 * MQTT QoS levels.
 */
typedef enum {
    MQTT_QOS_0 = 0,  /**< At most once. */
    MQTT_QOS_1 = 1,  /**< At least once. */
    MQTT_QOS_2 = 2   /**< Exactly once. */
} MQTTQoS_t;

/**
 * MQTT message.
 */
typedef struct {
    char     topic[MQTT_MAX_TOPIC_LEN];
    uint8_t  payload[MQTT_MAX_PAYLOAD];
    uint16_t payload_len;
    MQTTQoS_t qos;
    bool     retain;
} MQTTMessage_t;

/**
 * MQTT callback for received messages.
 */
typedef void (*mqtt_message_callback_t)(const MQTTMessage_t *message);

/**
 * MQTT client configuration.
 */
typedef struct {
    const char *broker_host;
    uint16_t    broker_port;
    const char *client_id;
    const char *username;         /**< Can be NULL for no auth. */
    const char *password;         /**< Can be NULL for no auth. */
    const char *ca_cert_pem;      /**< CA certificate for TLS. Can be NULL to skip TLS. */
    uint16_t    keepalive_sec;
    bool        use_tls;
} MQTTClientConfig_t;

/* ---------- API ---------- */

/**
 * Initialise the MQTT client.
 *
 * @return true on success.
 */
bool mqtt_client_init(void);

/**
 * Configure the MQTT client.
 * Must be called before mqtt_client_connect().
 *
 * @param config  Client configuration.
 * @return true on success.
 */
bool mqtt_client_configure(const MQTTClientConfig_t *config);

/**
 * Connect to the MQTT broker.
 *
 * @return true on successful connection.
 */
bool mqtt_client_connect(void);

/**
 * Disconnect from the MQTT broker.
 *
 * @return true on success.
 */
bool mqtt_client_disconnect(void);

/**
 * Publish a message to a topic.
 *
 * @param topic     Topic string.
 * @param payload   Message payload.
 * @param payload_len  Payload length.
 * @param qos       Quality of service level.
 * @param retain    Retain flag.
 * @return true on success.
 */
bool mqtt_client_publish(
    const char *topic,
    const uint8_t *payload, uint16_t payload_len,
    MQTTQoS_t qos,
    bool retain
);

/**
 * Subscribe to a topic.
 *
 * @param topic    Topic filter.
 * @param qos      Requested QoS level.
 * @param callback  Callback for received messages on this topic.
 * @return true on success.
 */
bool mqtt_client_subscribe(
    const char *topic,
    MQTTQoS_t qos,
    mqtt_message_callback_t callback
);

/**
 * Unsubscribe from a topic.
 *
 * @param topic  Topic filter.
 * @return true on success.
 */
bool mqtt_client_unsubscribe(const char *topic);

/**
 * Get the current connection state.
 *
 * @return MQTTState_t value.
 */
MQTTState_t mqtt_client_get_state(void);

/**
 * Construct a telemetry topic per the namespace convention.
 * Output: "rainmaker/{tenant_id}/{site_id}/{device_id}/telemetry"
 *
 * @param out_topic   Output buffer.
 * @param buf_size    Size of output buffer.
 * @param tenant_id   Tenant identifier string.
 * @param site_id     Site identifier string.
 * @param device_id   Device identifier string.
 * @return true if the topic was constructed successfully.
 */
bool mqtt_build_telemetry_topic(
    char *out_topic, size_t buf_size,
    const char *tenant_id, const char *site_id, const char *device_id
);

#endif /* MQTT_CLIENT_H */
