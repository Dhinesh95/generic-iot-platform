/**
 * @file flash_partition.h
 * @brief Flash partition — mockable A/B partition write abstraction.
 *
 * Architecture ref: Section 5 (OTA: A/B partition updates).
 *
 * Separates the flash-write API from the hardware implementation,
 * following the same pattern as time_source.h and tamper_detect.h:
 * the header defines the abstract API with mockable backends;
 * real ESP32 esp_ota_* can be dropped in later without changing
 * calling code.
 *
 * Two logical slots (A and B): one is "active" (currently running),
 * the other is "inactive" (target for OTA writes). After a successful
 * write + integrity check, the inactive slot becomes the boot target.
 *
 * Hardware gap: this phase implements a mock/in-memory backend only.
 * A real ESP32 backend would use esp_ota_begin/write/end/set_boot_partition
 * from the ESP-IDF OTA API.
 */

#ifndef FLASH_PARTITION_H
#define FLASH_PARTITION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------- Constants ---------- */

#define FLASH_PARTITION_SLOT_COUNT  2    /**< A/B partitions. */
#define FLASH_PARTITION_SLOT_A      0
#define FLASH_PARTITION_SLOT_B      1
#define FLASH_PARTITION_MAX_SIZE    (256 * 1024)  /**< 256 KB per slot (indicative). */

/* ---------- Types ---------- */

/**
 * Flash partition slot state.
 */
typedef enum {
    FLASH_SLOT_STATE_UNKNOWN = 0,
    FLASH_SLOT_STATE_ERASED,      /**< Slot is empty/erased. */
    FLASH_SLOT_STATE_WRITING,     /**< Write in progress. */
    FLASH_SLOT_STATE_VALID,       /**< Written + verified. */
    FLASH_SLOT_STATE_BOOTABLE    /**< Marked as next boot target. */
} FlashSlotState_t;

/**
 * Flash partition backend — abstracts the physical flash operations.
 * In production: esp_ota_begin/write/end/set_boot_partition.
 * For testing: in-memory mock.
 */
typedef struct {
    /**
     * Begin a write operation on a slot.
     * Prepares the slot for writing (erases in production).
     *
     * @param slot  Which slot (0 or 1).
     * @param size  Expected image size in bytes.
     * @return true on success.
     */
    bool (*begin)(uint8_t slot, uint32_t size);

    /**
     * Write a chunk of data to the active slot.
     * Must be called between begin() and end().
     *
     * @param slot       Which slot.
     * @param offset     Byte offset within the slot.
     * @param data       Data to write.
     * @param data_len   Length of data.
     * @return true on success.
     */
    bool (*write)(uint8_t slot, uint32_t offset,
                  const uint8_t *data, uint32_t data_len);

    /**
     * Finalize a write operation on a slot.
     * Marks the slot as VALID (written + verified).
     *
     * @param slot  Which slot.
     * @return true on success.
     */
    bool (*end)(uint8_t slot);

    /**
     * Set the boot partition to a specific slot.
     *
     * @param slot  Which slot to boot from next.
     * @return true on success.
     */
    bool (*set_boot_partition)(uint8_t slot);

    /**
     * Get the current boot partition.
     *
     * @return Slot index (0 or 1), or -1 on error.
     */
    int (*get_boot_partition)(void);

    /**
     * Get the state of a slot.
     *
     * @param slot  Which slot.
     * @return FlashSlotState_t value.
     */
    FlashSlotState_t (*get_slot_state)(uint8_t slot);

    /**
     * Read data from a slot (for verification).
     *
     * @param slot       Which slot.
     * @param offset     Byte offset.
     * @param out_buf    Output buffer.
     * @param read_len   Bytes to read.
     * @return true on success.
     */
    bool (*read)(uint8_t slot, uint32_t offset,
                 uint8_t *out_buf, uint32_t read_len);

    /**
     * Get the data pointer for a slot (mock only — for direct verification).
     *
     * @param slot  Which slot.
     * @return Pointer to slot data, or NULL.
     */
    const uint8_t *(*get_data)(uint8_t slot);

    /**
     * Get the current write offset for a slot (for incremental integrity).
     *
     * @param slot  Which slot.
     * @return Current write offset in bytes.
     */
    uint32_t (*get_write_offset)(uint8_t slot);
} FlashPartitionBackend_t;

/* ---------- API ---------- */

/**
 * Initialise the flash partition subsystem.
 *
 * @return true on success.
 */
bool flash_partition_init(void);

/**
 * Register a flash partition backend.
 * Must be called before flash_partition_init().
 *
 * @param backend  Backend callbacks (caller-allocated, must outlive usage).
 */
void flash_partition_set_backend(const FlashPartitionBackend_t *backend);

/**
 * Begin an OTA write to the inactive slot.
 * Determines which slot is NOT the current boot partition, and calls
 * backend->begin() on it.
 *
 * @param image_size  Expected image size in bytes.
 * @return true on success (inactive slot prepared for writing).
 */
bool flash_partition_ota_begin(uint32_t image_size);

/**
 * Write a chunk to the OTA target slot.
 * Must be called between ota_begin() and ota_end().
 *
 * @param offset     Byte offset within the image.
 * @param data       Chunk data.
 * @param data_len   Chunk length.
 * @return true on success.
 */
bool flash_partition_ota_write(uint32_t offset,
                               const uint8_t *data, uint32_t data_len);

/**
 * Finalize the OTA write and mark the slot as bootable.
 * Does NOT set the boot partition — that is a separate step
 * (confirm-before-commit pattern).
 *
 * @return true on success.
 */
bool flash_partition_ota_end(void);

/**
 * Set the boot partition to the newly-written slot.
 * Called after confirm-before-commit verification.
 *
 * @return true on success.
 */
bool flash_partition_set_boot(void);

/**
 * Roll back: set the boot partition to the previous slot.
 * Used when post-flash self-check fails.
 *
 * @return true on success.
 */
bool flash_partition_rollback(void);

/**
 * Get the index of the currently active (booting) slot.
 *
 * @return Slot index (0 or 1).
 */
uint8_t flash_partition_get_active_slot(void);

/**
 * Get the index of the inactive (OTA target) slot.
 *
 * @return Slot index (0 or 1).
 */
uint8_t flash_partition_get_inactive_slot(void);

/**
 * Get the backend instance (for direct testing).
 *
 * @return Pointer to the registered backend, or NULL.
 */
const FlashPartitionBackend_t *flash_partition_get_backend(void);

#endif /* FLASH_PARTITION_H */
