/**
 * @file ota_session.h
 * @brief OTA session — chunked transfer state machine with integrity verification.
 *
 * Architecture ref: Section 5 (OTA: chunked download, A/B flash, rollback).
 *
 * Integrates with:
 *   - ota_manifest.h (Phase 7): compatibility check before accepting chunks
 *   - firmware_integrity.h (Phase 3): HMAC-SHA256 image verification
 *   - device_identity.h (Phase 7.2): confirm-before-commit version flow
 *   - flash_partition.h (Phase 17): A/B partition write abstraction
 *
 * Transport: chunks arrive via MQTT topic
 *   rainmaker/{tenant}/{site}/{device}/ota/chunk
 * No real broker available (same limitation as Phase 3/8 MQTT tests).
 */

#ifndef OTA_SESSION_H
#define OTA_SESSION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ota_manifest.h"
#include "firmware_integrity.h"

/* ---------- Constants ---------- */

#define OTA_SESSION_MAX_CHUNK_SIZE   256   /**< Max chunk payload bytes. */
#define OTA_SESSION_MAX_IMAGE_SIZE   (128 * 1024)  /**< 128 KB max image. */
#define OTA_SESSION_TOPIC_MAX_LEN    128

/* ---------- Types ---------- */

/**
 * OTA session state machine.
 */
typedef enum {
    OTA_STATE_IDLE = 0,         /**< No active transfer. */
    OTA_STATE_MANIFEST,         /**< Manifest received, compatibility check pending. */
    OTA_STATE_TRANSFER,         /**< Chunks arriving, flash in progress. */
    OTA_STATE_VERIFY,           /**< All chunks received, integrity check. */
    OTA_STATE_COMPLETE,         /**< Transfer complete, pending version set. */
    OTA_STATE_ERROR             /**< Transfer failed/aborted. */
} OtaSessionState_t;

/**
 * OTA session result codes.
 */
typedef enum {
    OTA_RESULT_OK,
    OTA_RESULT_ERR_COMPAT,          /**< Manifest compatibility check failed. */
    OTA_RESULT_ERR_SEQUENCE,        /**< Chunk sequence mismatch (gap or duplicate). */
    OTA_RESULT_ERR_SIZE,            /**< Chunk offset exceeds image size. */
    OTA_RESULT_ERR_INTEGRITY,       /**< Final HMAC verification failed. */
    OTA_RESULT_ERR_FLASH,           /**< Flash write failed. */
    OTA_RESULT_ERR_STATE,           /**< Operation not valid in current state. */
    OTA_RESULT_ERR_PARAM_NULL       /**< NULL pointer argument. */
} OtaSessionResult_t;

/**
 * OTA chunk — a piece of the firmware image.
 * Received via MQTT, written to the inactive flash partition.
 */
typedef struct {
    uint32_t sequence;              /**< Chunk sequence number (0-based). */
    uint32_t offset;                /**< Byte offset within the image. */
    uint16_t data_len;              /**< Length of chunk data. */
    uint8_t  data[OTA_SESSION_MAX_CHUNK_SIZE]; /**< Chunk payload. */
} OtaChunk_t;

/**
 * OTA session statistics.
 */
typedef struct {
    uint32_t chunks_received;       /**< Total chunks received. */
    uint32_t chunks_accepted;       /**< Chunks written to flash. */
    uint32_t chunks_rejected;       /**< Chunks rejected (sequence, integrity). */
    uint32_t bytes_written;         /**< Total bytes written to flash. */
    uint32_t retries_requested;     /**< Sequence-gap retries requested. */
} OtaSessionStats_t;

/**
 * OTA session context — holds all state for one transfer.
 */
typedef struct {
    OtaSessionState_t   state;                  /**< Current state. */
    FirmwareManifest_t  manifest;               /**< Received manifest. */
    uint32_t            image_size;             /**< Expected total image size. */
    uint32_t            chunks_expected;        /**< Expected chunk count. */
    uint32_t            next_sequence;          /**< Next expected chunk sequence. */
    uint32_t            bytes_received;         /**< Bytes received so far. */
    uint8_t             running_hmac[FW_INTEGRITY_HMAC_SIZE]; /**< Incremental HMAC state. */
    bool                hmac_started;           /**< Whether HMAC context is initialized. */
    OtaSessionStats_t   stats;                  /**< Transfer statistics. */
} OtaSessionContext_t;

/**
 * OTA session configuration — callbacks for transport and flash.
 */
typedef struct {
    /**
     * MQTT publish callback for sending requests/responses.
     * In production: mqtt_client_publish(). In tests: mock.
     */
    bool (*mqtt_publish)(const char *topic, const uint8_t *payload,
                         uint16_t payload_len);

    /**
     * MQTT subscribe callback for receiving chunks.
     * In production: mqtt_client_subscribe(). In tests: mock.
     */
    bool (*mqtt_subscribe)(const char *topic_filter);

    /**
     * Callback to request a retry for a missing chunk.
     * In production: publishes a retry request to the cloud.
     * In tests: mock.
     */
    bool (*request_retry)(uint32_t sequence);
} OtaSessionTransport_t;

/* ---------- API ---------- */

/**
 * Initialise the OTA session subsystem.
 *
 * @return true on success.
 */
bool ota_session_init(void);

/**
 * Begin a new OTA session with a received manifest.
 * Performs compatibility check immediately — rejects upfront if
 * domain/hw_variant/version don't match.
 *
 * @param ctx       Session context (caller-allocated, zeroed).
 * @param manifest  The incoming firmware manifest.
 * @param transport Transport callbacks (MQTT publish/subscribe).
 * @return OTA_RESULT_OK if manifest accepted, specific error otherwise.
 */
OtaSessionResult_t ota_session_begin(
    OtaSessionContext_t *ctx,
    const FirmwareManifest_t *manifest,
    const OtaSessionTransport_t *transport
);

/**
 * Process an incoming chunk.
 * Validates sequence, writes to flash, updates running HMAC.
 * On final chunk: verifies integrity and sets pending version.
 *
 * @param ctx    Session context.
 * @param chunk  The received chunk.
 * @return OTA_RESULT_OK if accepted, specific error otherwise.
 */
OtaSessionResult_t ota_session_process_chunk(
    OtaSessionContext_t *ctx,
    const OtaChunk_t *chunk
);

/**
 * Get the current session state.
 *
 * @param ctx  Session context.
 * @return OtaSessionState_t value.
 */
OtaSessionState_t ota_session_get_state(const OtaSessionContext_t *ctx);

/**
 * Get session statistics.
 *
 * @param ctx  Session context.
 * @return Pointer to stats (read-only).
 */
const OtaSessionStats_t *ota_session_get_stats(const OtaSessionContext_t *ctx);

/**
 * Abort the current session (e.g. on timeout or user cancel).
 * Cleans up flash state and resets to IDLE.
 *
 * @param ctx  Session context.
 */
void ota_session_abort(OtaSessionContext_t *ctx);

/**
 * Get a human-readable string for an OTA session result.
 *
 * @param result  The result code.
 * @return Constant string describing the result.
 */
const char *ota_session_result_str(OtaSessionResult_t result);

/**
 * Boot-time self-check hook.
 * On startup, if a pending version exists (device_identity_has_pending_version),
 * runs core module initialization checks. If all pass, calls
 * device_identity_confirm_boot(). If any fail, triggers rollback.
 *
 * @return true if boot succeeded (confirmed or no pending update),
 *         false if rollback was triggered.
 */
bool ota_session_boot_self_check(void);

/**
 * Construct the OTA chunk topic for a device.
 * Output: "rainmaker/{tenant}/{site}/{device_id}/ota/chunk"
 *
 * @param out_topic   Output buffer.
 * @param buf_size    Size of output buffer.
 * @param tenant_id   Tenant identifier.
 * @param site_id     Site identifier.
 * @param device_id   Device identifier string.
 * @return true on success.
 */
bool ota_build_chunk_topic(
    char *out_topic, size_t buf_size,
    const char *tenant_id, const char *site_id, const char *device_id
);

#endif /* OTA_SESSION_H */
