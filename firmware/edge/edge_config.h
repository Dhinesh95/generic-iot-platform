/**
 * @file edge_config.h
 * @brief Edge Node field-configurable sensor configuration.
 *
 * Architecture ref: Section 8 (Two Layers of Pluggable).
 *
 * The UniversalConfigRecord determines which driver is active on a
 * given Edge Node at field time — no reflash required to change
 * a node's sensor type. Stored via the same storage-callback
 * abstraction used by audit_log, device_identity, and time_source.
 */

#ifndef EDGE_CONFIG_H
#define EDGE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "sensor_driver.h"  /* for SensorType_t, SensorConfig_t */

/* ---------- Types ---------- */

/**
 * Universal config record — the field-configurable identity of an Edge Node.
 * One per physical node, stored in persistent storage.
 */
typedef struct {
    uint8_t       node_id;           /**< This node's unique address (RS-485). */
    SensorType_t  active_driver;     /**< Which sensor driver is active. */
    uint8_t       metric_id;         /**< Semantic metric ID sent in RS-485 payload.
                                         This is the Hub-side metric identifier (e.g.
                                         HOME_METRIC_SECURITY_STATE = 10), NOT the driver
                                         type enum. The Gateway passes this through to the
                                         Hub's rule engine, which uses it to look up rules.
                                         Must be configured to match the active domain
                                         profile's metric definitions. */
    SensorConfig_t driver_config;    /**< Driver-specific pin/addr/scale config. */
    uint8_t       gw_dest_addr;      /**< Gateway RS-485 address to send to. */
    bool          valid;             /**< false = no config loaded (unprovisioned). */
} EdgeNodeConfig_t;

/**
 * Storage backend for edge config persistence.
 * Same pattern as AuditLogStorage_t / DeviceIdentityStorage_t.
 */
typedef struct {
    bool (*save)(const EdgeNodeConfig_t *config);
    bool (*load)(EdgeNodeConfig_t *config);
} EdgeConfigStorage_t;

/**
 * Result of config operations.
 */
typedef enum {
    EDGE_CONFIG_OK,
    EDGE_CONFIG_ERR_NOT_PROVISIONED,    /**< No config stored. */
    EDGE_CONFIG_ERR_DRIVER_NOT_FOUND,   /**< Config references unknown driver type. */
    EDGE_CONFIG_ERR_DRIVER_INIT_FAIL,   /**< Driver's init() returned false. */
    EDGE_CONFIG_ERR_PARAM_NULL,         /**< NULL pointer. */
    EDGE_CONFIG_ERR_STORAGE             /**< Storage backend failure. */
} EdgeConfigResult_t;

/* ---------- API ---------- */

/**
 * Initialise the edge config subsystem.
 *
 * @param storage  Storage backend (NULL for RAM-only / test mode).
 * @return true on success.
 */
bool edge_config_init(const EdgeConfigStorage_t *storage);

/**
 * Get the current node configuration.
 *
 * @return Pointer to the in-RAM config, or NULL if not provisioned.
 */
const EdgeNodeConfig_t *edge_config_get(void);

/**
 * Store a new configuration (factory provisioning or field reconfiguration).
 * Validates that the requested driver type exists in the registry before
 * accepting the config.
 *
 * @param config  The configuration to store.
 * @return EDGE_CONFIG_OK on success.
 */
EdgeConfigResult_t edge_config_set(const EdgeNodeConfig_t *config);

/**
 * Check whether this node has been provisioned (has a valid config).
 *
 * @return true if provisioned.
 */
bool edge_config_is_provisioned(void);

#endif /* EDGE_CONFIG_H */
