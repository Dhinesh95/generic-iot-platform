/**
 * @file edge_config.c
 * @brief Edge Node field-configurable sensor configuration implementation.
 *
 * Architecture ref: Section 8.
 */

#include "edge_config.h"
#include "sensor_driver.h"
#include <string.h>

/* ---------- Module state ---------- */

static EdgeNodeConfig_t          s_config;
static const EdgeConfigStorage_t *s_storage = NULL;
static bool                       s_initialised = false;

/* ---------- API ---------- */

bool edge_config_init(const EdgeConfigStorage_t *storage)
{
    memset(&s_config, 0, sizeof(s_config));
    s_storage = storage;
    s_initialised = true;

    /* Try to load persisted config. */
    if (s_storage && s_storage->load) {
        EdgeNodeConfig_t loaded;
        if (s_storage->load(&loaded) && loaded.valid) {
            s_config = loaded;
            return true;
        }
    }

    return true;
}

const EdgeNodeConfig_t *edge_config_get(void)
{
    if (!s_initialised) return NULL;
    if (!s_config.valid) return NULL;
    return &s_config;
}

EdgeConfigResult_t edge_config_set(const EdgeNodeConfig_t *config)
{
    if (!config) return EDGE_CONFIG_ERR_PARAM_NULL;
    if (!s_initialised) return EDGE_CONFIG_ERR_STORAGE;

    /* Validate that the requested driver type exists in the registry. */
    if (!sensor_driver_find(config->active_driver)) {
        return EDGE_CONFIG_ERR_DRIVER_NOT_FOUND;
    }

    s_config = *config;
    s_config.valid = true;

    /* Persist if storage backend is available. */
    if (s_storage && s_storage->save) {
        if (!s_storage->save(&s_config)) {
            return EDGE_CONFIG_ERR_STORAGE;
        }
    }

    return EDGE_CONFIG_OK;
}

bool edge_config_is_provisioned(void)
{
    return s_initialised && s_config.valid;
}
