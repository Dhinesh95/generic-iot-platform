/**
 * @file ota_manifest.c
 * @brief OTA manifest implementation — compatibility checking.
 *
 * Architecture ref: Section 5 (OTA: image targeting, canary rollout).
 */

#include "ota_manifest.h"

OtaCompatResult_t ota_compatibility_check(
    const DeviceIdentity_t *device,
    const FirmwareManifest_t *manifest)
{
    if (!device || !manifest) return OTA_COMPAT_ERR_PARAM_NULL;

    /* Check domain profile — reject on ANY mismatch. */
    if (device->domain_profile_id != manifest->target_domain_profile_id) {
        return OTA_COMPAT_ERR_DOMAIN_MISMATCH;
    }

    /* Check hardware variant — reject on ANY mismatch. */
    if (device->hw_variant_id != manifest->target_hw_variant_id) {
        return OTA_COMPAT_ERR_VARIANT_MISMATCH;
    }

    /* Check version — reject downgrades. */
    if (manifest->fw_version <= device->fw_version) {
        return OTA_COMPAT_ERR_VERSION_DOWNGRADE;
    }

    return OTA_COMPAT_OK;
}

const char *ota_compat_result_str(OtaCompatResult_t result)
{
    switch (result) {
        case OTA_COMPAT_OK:                    return "OK — compatible";
        case OTA_COMPAT_ERR_DOMAIN_MISMATCH:   return "REJECT — domain profile mismatch";
        case OTA_COMPAT_ERR_VARIANT_MISMATCH:  return "REJECT — hardware variant mismatch";
        case OTA_COMPAT_ERR_VERSION_DOWNGRADE: return "REJECT — version downgrade";
        case OTA_COMPAT_ERR_PARAM_NULL:        return "ERROR — NULL parameter";
        default:                               return "UNKNOWN";
    }
}
