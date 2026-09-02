/**
 * @file ota_canary.c
 * @brief OTA canary rollout implementation — per-group state tracking.
 *
 * Architecture ref: Section 5 (OTA: canary rollout isolation).
 *
 * Canary membership uses deterministic hash-based bucketing:
 * hash(device_uuid || domain_profile_id || hw_variant_id) % 100
 * produces a stable bucket (0-99). A device is in the canary wave
 * if its bucket < canary_percentage. This is testable without randomness.
 */

#include "ota_canary.h"
#include <string.h>

/* ---------- Internal state ---------- */

static OtaCanaryGroup_t s_groups[OTA_CANARY_MAX_GROUPS];
static bool s_initialised = false;

/* ---------- Internal helpers ---------- */

/**
 * FNV-1a hash — simple, fast, deterministic.
 * Used for canary bucketing. Not cryptographic — this is for
 * stable fleet partitioning, not security.
 */
static uint32_t fnv1a_hash(const uint8_t *data, size_t len)
{
    uint32_t hash = 2166136261u;  /* FNV offset basis. */
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;  /* FNV prime. */
    }
    return hash;
}

/* ---------- Public API ---------- */

bool ota_canary_init(void)
{
    memset(s_groups, 0, sizeof(s_groups));
    s_initialised = true;
    return true;
}

OtaCanaryResult_t ota_canary_create_group(
    DomainProfileId_t domain_profile_id,
    uint8_t hw_variant_id,
    uint8_t canary_percentage,
    uint8_t error_threshold,
    OtaCanaryGroup_t **out_group)
{
    if (!s_initialised) return OTA_CANARY_ERR_PARAM_NULL;
    if (!out_group) return OTA_CANARY_ERR_PARAM_NULL;
    if (canary_percentage > 100) canary_percentage = 100;

    /* Check if group already exists. */
    for (int i = 0; i < OTA_CANARY_MAX_GROUPS; i++) {
        if (s_groups[i].active &&
            s_groups[i].domain_profile_id == domain_profile_id &&
            s_groups[i].hw_variant_id == hw_variant_id) {
            *out_group = &s_groups[i];
            return OTA_CANARY_OK;
        }
    }

    /* Find a free slot. */
    for (int i = 0; i < OTA_CANARY_MAX_GROUPS; i++) {
        if (!s_groups[i].active) {
            memset(&s_groups[i], 0, sizeof(OtaCanaryGroup_t));
            s_groups[i].domain_profile_id = domain_profile_id;
            s_groups[i].hw_variant_id = hw_variant_id;
            s_groups[i].canary_percentage = canary_percentage;
            s_groups[i].error_rate_threshold = error_threshold;
            s_groups[i].active = true;
            *out_group = &s_groups[i];
            return OTA_CANARY_OK;
        }
    }

    return OTA_CANARY_ERR_GROUP_FULL;
}

bool ota_canary_is_in_canary(const uint8_t device_uuid[16],
                              const OtaCanaryGroup_t *group)
{
    if (!device_uuid || !group || !group->active) return false;
    if (group->canary_percentage == 0) return false;
    if (group->canary_percentage >= 100) return true;

    /* Deterministic bucketing: hash(uuid || domain || variant) % 100. */
    uint8_t hash_input[16 + 1 + 1];  /* uuid + domain_id + hw_variant */
    memcpy(hash_input, device_uuid, 16);
    hash_input[16] = (uint8_t)group->domain_profile_id;
    hash_input[17] = group->hw_variant_id;

    uint8_t bucket = ota_canary_hash_bucket(hash_input, sizeof(hash_input));
    return bucket < group->canary_percentage;
}

uint8_t ota_canary_hash_bucket(const uint8_t *data, size_t len)
{
    if (!data || len == 0) return 0;
    uint32_t hash = fnv1a_hash(data, len);
    return (uint8_t)(hash % 100);
}

bool ota_canary_record_attempt(OtaCanaryGroup_t *group, bool success)
{
    if (!group || !group->active) return false;

    group->total_attempts++;

    if (!success) {
        group->error_count++;
    } else {
        group->devices_rolled_out++;
    }

    /* Check error rate threshold. */
    if (group->total_attempts > 0) {
        uint32_t error_rate = (group->error_count * 100) / group->total_attempts;
        if (error_rate > group->error_rate_threshold && group->error_rate_threshold > 0) {
            group->rollback_triggered = true;
        }
    }

    return !group->rollback_triggered;
}

OtaCanaryResult_t ota_canary_get_group(
    DomainProfileId_t domain_profile_id,
    uint8_t hw_variant_id,
    OtaCanaryGroup_t **out_group)
{
    if (!s_initialised) return OTA_CANARY_ERR_PARAM_NULL;
    if (!out_group) return OTA_CANARY_ERR_PARAM_NULL;

    for (int i = 0; i < OTA_CANARY_MAX_GROUPS; i++) {
        if (s_groups[i].active &&
            s_groups[i].domain_profile_id == domain_profile_id &&
            s_groups[i].hw_variant_id == hw_variant_id) {
            *out_group = &s_groups[i];
            return OTA_CANARY_OK;
        }
    }

    return OTA_CANARY_ERR_GROUP_NOT_FOUND;
}

OtaCanaryResult_t ota_canary_rollback(OtaCanaryGroup_t *group)
{
    if (!group || !group->active) return OTA_CANARY_ERR_PARAM_NULL;

    group->rollback_triggered = true;
    return OTA_CANARY_OK;
}

uint8_t ota_canary_get_group_count(void)
{
    if (!s_initialised) return 0;

    uint8_t count = 0;
    for (int i = 0; i < OTA_CANARY_MAX_GROUPS; i++) {
        if (s_groups[i].active) count++;
    }
    return count;
}
