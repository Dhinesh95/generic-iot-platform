/**
 * @file config_engine.h
 * @brief Runtime configuration engine — profile registry + parameter store.
 *
 * Architecture ref: Phase 23 (Runtime Configuration Engine).
 *
 * Replaces compile-time #ifdef profile selection with runtime registration
 * and a key-value parameter store with factory-set vs field-configurable
 * distinction.
 */

#ifndef CONFIG_ENGINE_H
#define CONFIG_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "device_identity.h"
#include "rule_engine_core.h"

/* ---------- Constants ---------- */

#define CONFIG_MAX_PROFILES     4
#define CONFIG_MAX_PARAMS       32
#define CONFIG_KEY_MAX_LEN      32
#define CONFIG_VALUE_MAX_LEN    64

/* ---------- Types ---------- */

typedef enum {
    CONFIG_OK,
    CONFIG_ERR_FULL,
    CONFIG_ERR_NOT_FOUND,
    CONFIG_ERR_READONLY,
    CONFIG_ERR_PARAM_NULL
} ConfigResult_t;

/**
 * Profile registration entry.
 */
typedef struct {
    DomainProfileId_t           id;
    const DomainProfileVTable_t *vtable;
    const char                  *name;
    bool                        active;
} ProfileRegistration_t;

/**
 * Configuration parameter.
 */
typedef struct {
    char key[CONFIG_KEY_MAX_LEN];
    char value[CONFIG_VALUE_MAX_LEN];
    bool editable;      /* false = factory-set, true = field-configurable */
    bool active;
} ConfigParam_t;

/**
 * Configuration engine context.
 */
typedef struct {
    ProfileRegistration_t profiles[CONFIG_MAX_PROFILES];
    uint8_t               profile_count;
    uint8_t               active_profile_index;
    ConfigParam_t         params[CONFIG_MAX_PARAMS];
    uint8_t               param_count;
    bool                  initialized;
} ConfigEngineContext_t;

/* ---------- API ---------- */

/**
 * Initialize the configuration engine.
 *
 * @param ctx  Config engine context (caller-owned).
 * @return CONFIG_OK on success.
 */
ConfigResult_t config_engine_init(ConfigEngineContext_t *ctx);

/**
 * Register a domain profile.
 *
 * @param ctx     Config engine context.
 * @param id      Domain profile ID.
 * @param vtable  Profile vtable (rules, validation, failsafe).
 * @param name    Human-readable name (e.g. "home").
 * @return CONFIG_OK on success.
 */
ConfigResult_t config_register_profile(ConfigEngineContext_t *ctx,
                                        DomainProfileId_t id,
                                        const DomainProfileVTable_t *vtable,
                                        const char *name);

/**
 * Set the active profile by ID.
 *
 * @param ctx  Config engine context.
 * @param id   Domain profile ID to activate.
 * @return CONFIG_OK on success.
 */
ConfigResult_t config_set_active_profile(ConfigEngineContext_t *ctx,
                                          DomainProfileId_t id);

/**
 * Get the active profile's vtable.
 *
 * @param ctx  Config engine context.
 * @return Pointer to active vtable, or NULL if none active.
 */
const DomainProfileVTable_t *config_get_active_vtable(const ConfigEngineContext_t *ctx);

/**
 * Get the active profile ID.
 *
 * @param ctx  Config engine context.
 * @return Active profile ID, or 0 if none active.
 */
DomainProfileId_t config_get_active_profile_id(const ConfigEngineContext_t *ctx);

/**
 * Set a configuration parameter.
 *
 * @param ctx    Config engine context.
 * @param key    Parameter key.
 * @param value  Parameter value.
 * @return CONFIG_OK on success.
 */
ConfigResult_t config_set_param(ConfigEngineContext_t *ctx,
                                 const char *key, const char *value);

/**
 * Get a configuration parameter value.
 *
 * @param ctx    Config engine context.
 * @param key    Parameter key.
 * @param value  Output buffer.
 * @param len    Size of output buffer.
 * @return CONFIG_OK on success.
 */
ConfigResult_t config_get_param(const ConfigEngineContext_t *ctx,
                                 const char *key, char *value, size_t len);

/**
 * Check if a parameter is editable (field-configurable).
 *
 * @param ctx  Config engine context.
 * @param key  Parameter key.
 * @return true if editable, false if factory-set or not found.
 */
bool config_is_param_editable(const ConfigEngineContext_t *ctx, const char *key);

/**
 * Get the number of registered profiles.
 */
uint8_t config_get_profile_count(const ConfigEngineContext_t *ctx);

/**
 * Get the number of registered parameters.
 */
uint8_t config_get_param_count(const ConfigEngineContext_t *ctx);

#endif /* CONFIG_ENGINE_H */
