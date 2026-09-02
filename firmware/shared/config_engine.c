/**
 * @file config_engine.c
 * @brief Runtime configuration engine implementation.
 */

#include "config_engine.h"
#include <string.h>

/* ---------- API ---------- */

ConfigResult_t config_engine_init(ConfigEngineContext_t *ctx)
{
    if (!ctx) return CONFIG_ERR_PARAM_NULL;
    memset(ctx, 0, sizeof(*ctx));
    ctx->active_profile_index = 0xFF; /* No active profile. */
    ctx->initialized = true;
    return CONFIG_OK;
}

ConfigResult_t config_register_profile(ConfigEngineContext_t *ctx,
                                        DomainProfileId_t id,
                                        const DomainProfileVTable_t *vtable,
                                        const char *name)
{
    if (!ctx || !ctx->initialized || !vtable || !name) return CONFIG_ERR_PARAM_NULL;

    if (ctx->profile_count >= CONFIG_MAX_PROFILES) return CONFIG_ERR_FULL;

    /* Check for duplicate ID. */
    for (uint8_t i = 0; i < ctx->profile_count; i++) {
        if (ctx->profiles[i].id == id) {
            /* Update existing registration. */
            ctx->profiles[i].vtable = vtable;
            ctx->profiles[i].name = name;
            return CONFIG_OK;
        }
    }

    ProfileRegistration_t *reg = &ctx->profiles[ctx->profile_count];
    reg->id = id;
    reg->vtable = vtable;
    reg->name = name;
    reg->active = false;
    ctx->profile_count++;

    return CONFIG_OK;
}

ConfigResult_t config_set_active_profile(ConfigEngineContext_t *ctx,
                                          DomainProfileId_t id)
{
    if (!ctx || !ctx->initialized) return CONFIG_ERR_PARAM_NULL;

    /* Deactivate all profiles. */
    for (uint8_t i = 0; i < ctx->profile_count; i++) {
        ctx->profiles[i].active = false;
    }

    /* Find and activate the requested profile. */
    for (uint8_t i = 0; i < ctx->profile_count; i++) {
        if (ctx->profiles[i].id == id) {
            ctx->profiles[i].active = true;
            ctx->active_profile_index = i;
            return CONFIG_OK;
        }
    }

    return CONFIG_ERR_NOT_FOUND;
}

const DomainProfileVTable_t *config_get_active_vtable(const ConfigEngineContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return NULL;
    if (ctx->active_profile_index >= ctx->profile_count) return NULL;
    return ctx->profiles[ctx->active_profile_index].vtable;
}

DomainProfileId_t config_get_active_profile_id(const ConfigEngineContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return 0;
    if (ctx->active_profile_index >= ctx->profile_count) return 0;
    return ctx->profiles[ctx->active_profile_index].id;
}

ConfigResult_t config_set_param(ConfigEngineContext_t *ctx,
                                 const char *key, const char *value)
{
    if (!ctx || !ctx->initialized || !key || !value) return CONFIG_ERR_PARAM_NULL;

    /* Check if param already exists. */
    for (uint8_t i = 0; i < ctx->param_count; i++) {
        if (strcmp(ctx->params[i].key, key) == 0) {
            if (!ctx->params[i].editable) return CONFIG_ERR_READONLY;
            strncpy(ctx->params[i].value, value, CONFIG_VALUE_MAX_LEN - 1);
            ctx->params[i].value[CONFIG_VALUE_MAX_LEN - 1] = '\0';
            return CONFIG_OK;
        }
    }

    /* Create new param. */
    if (ctx->param_count >= CONFIG_MAX_PARAMS) return CONFIG_ERR_FULL;

    ConfigParam_t *p = &ctx->params[ctx->param_count];
    strncpy(p->key, key, CONFIG_KEY_MAX_LEN - 1);
    p->key[CONFIG_KEY_MAX_LEN - 1] = '\0';
    strncpy(p->value, value, CONFIG_VALUE_MAX_LEN - 1);
    p->value[CONFIG_VALUE_MAX_LEN - 1] = '\0';
    p->editable = true; /* User-created params are editable by default. */
    p->active = true;
    ctx->param_count++;

    return CONFIG_OK;
}

ConfigResult_t config_get_param(const ConfigEngineContext_t *ctx,
                                 const char *key, char *value, size_t len)
{
    if (!ctx || !ctx->initialized || !key || !value) return CONFIG_ERR_PARAM_NULL;

    for (uint8_t i = 0; i < ctx->param_count; i++) {
        if (strcmp(ctx->params[i].key, key) == 0) {
            strncpy(value, ctx->params[i].value, len - 1);
            value[len - 1] = '\0';
            return CONFIG_OK;
        }
    }

    return CONFIG_ERR_NOT_FOUND;
}

bool config_is_param_editable(const ConfigEngineContext_t *ctx, const char *key)
{
    if (!ctx || !ctx->initialized || !key) return false;

    for (uint8_t i = 0; i < ctx->param_count; i++) {
        if (strcmp(ctx->params[i].key, key) == 0) {
            return ctx->params[i].editable;
        }
    }

    return false;
}

uint8_t config_get_profile_count(const ConfigEngineContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return 0;
    return ctx->profile_count;
}

uint8_t config_get_param_count(const ConfigEngineContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return 0;
    return ctx->param_count;
}
