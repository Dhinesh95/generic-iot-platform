/**
 * @file ota_canary.h
 * @brief OTA canary rollout — per-group state tracking with rollback.
 *
 * Architecture ref: Section 5 (OTA: canary rollout isolation).
 *
 * A canary group is scoped to one (domain_profile_id, hw_variant_id) pair.
 * Rollback triggered in one group does NOT affect any other group.
 *
 * Canary membership is deterministic: hash(device_uuid + group_key) % 100
 * produces a stable bucket (0-99). A device is in the canary wave if its
 * bucket is less than the canary percentage. This is testable without
 * randomness.
 */

#ifndef OTA_CANARY_H
#define OTA_CANARY_H

#include <stdint.h>
#include <stdbool.h>
#include "device_identity.h"

/* ---------- Constants ---------- */

#define OTA_CANARY_MAX_GROUPS  16   /**< Maximum concurrent canary groups. */

/* ---------- Types ---------- */

/**
 * Canary rollout state for a single (domain_profile_id, hw_variant_id) group.
 */
typedef struct {
    DomainProfileId_t domain_profile_id;  /**< Domain profile for this group. */
    uint8_t           hw_variant_id;      /**< Hardware variant for this group. */
    uint8_t           canary_percentage;  /**< 0-100: percentage of fleet in canary. */
    uint32_t          devices_in_canary;  /**< Devices currently in canary wave. */
    uint32_t          devices_pending;    /**< Devices waiting for canary wave. */
    uint32_t          devices_rolled_out; /**< Devices fully rolled out. */
    uint8_t           error_rate_threshold; /**< % error rate that triggers auto-rollback. */
    uint32_t          error_count;        /**< Errors observed during this rollout. */
    uint32_t          total_attempts;     /**< Total update attempts. */
    bool              rollback_triggered; /**< true if auto-rollback fired. */
    bool              active;             /**< true if this group slot is in use. */
} OtaCanaryGroup_t;

/**
 * Result of canary operations.
 */
typedef enum {
    OTA_CANARY_OK,
    OTA_CANARY_ERR_GROUP_NOT_FOUND,   /**< No canary group for this profile/variant. */
    OTA_CANARY_ERR_GROUP_FULL,        /**< Max groups reached. */
    OTA_CANARY_ERR_PARAM_NULL,        /**< NULL pointer argument. */
    OTA_CANARY_ERR_NOT_ACTIVE         /**< Canary group is not active. */
} OtaCanaryResult_t;

/* ---------- API ---------- */

/**
 * Initialise the canary rollout subsystem.
 *
 * @return true on success.
 */
bool ota_canary_init(void);

/**
 * Create or get a canary group for a (domain_profile_id, hw_variant_id) pair.
 * If the group already exists, returns it. If not, creates a new one.
 *
 * @param domain_profile_id  Domain profile.
 * @param hw_variant_id      Hardware variant.
 * @param canary_percentage  Initial canary percentage (0-100).
 * @param error_threshold    Error rate threshold for auto-rollback (0-100%).
 * @param out_group          Output: pointer to the group (internal storage).
 * @return OTA_CANARY_OK on success.
 */
OtaCanaryResult_t ota_canary_create_group(
    DomainProfileId_t domain_profile_id,
    uint8_t hw_variant_id,
    uint8_t canary_percentage,
    uint8_t error_threshold,
    OtaCanaryGroup_t **out_group
);

/**
 * Check if a specific device is in the canary wave for its group.
 * Uses deterministic bucketing: hash(device_uuid + group_key) % 100.
 *
 * @param device_uuid  The device's UUID (16 bytes).
 * @param group        The canary group to check against.
 * @return true if the device is in the canary wave.
 */
bool ota_canary_is_in_canary(const uint8_t device_uuid[16],
                              const OtaCanaryGroup_t *group);

/**
 * Record an update attempt (success or failure) for a canary group.
 * If the error rate exceeds the threshold, auto-rollback is triggered.
 *
 * @param group    The canary group.
 * @param success  true if the update succeeded, false if it failed.
 * @return true if the update was allowed (not rolled back).
 */
bool ota_canary_record_attempt(OtaCanaryGroup_t *group, bool success);

/**
 * Get a canary group by domain profile and hardware variant.
 *
 * @param domain_profile_id  Domain profile to look up.
 * @param hw_variant_id      Hardware variant to look up.
 * @param out_group          Output: pointer to the group.
 * @return OTA_CANARY_OK if found.
 */
OtaCanaryResult_t ota_canary_get_group(
    DomainProfileId_t domain_profile_id,
    uint8_t hw_variant_id,
    OtaCanaryGroup_t **out_group
);

/**
 * Trigger a manual rollback for a specific canary group.
 * Only affects the specified group — other groups are untouched.
 *
 * @param group  The canary group to roll back.
 * @return OTA_CANARY_OK on success.
 */
OtaCanaryResult_t ota_canary_rollback(OtaCanaryGroup_t *group);

/**
 * Get the total number of active canary groups.
 *
 * @return Number of active groups.
 */
uint8_t ota_canary_get_group_count(void);

/**
 * Compute a deterministic hash of a device UUID for canary bucketing.
 * Exposed for testing.
 *
 * @param data  Data to hash.
 * @param len   Length of data.
 * @return Hash value (0-99 for bucketing).
 */
uint8_t ota_canary_hash_bucket(const uint8_t *data, size_t len);

#endif /* OTA_CANARY_H */
