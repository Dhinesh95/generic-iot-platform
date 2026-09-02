/**
 * @file flash_partition.c
 * @brief Flash partition implementation — mock/in-memory A/B backend.
 *
 * Provides a default mock backend for testing. In production, the
 * backend would be replaced with esp_ota_* calls on real ESP32 hardware.
 */

#include "flash_partition.h"
#include <string.h>

/* ---------- Internal state ---------- */

static const FlashPartitionBackend_t *s_backend = NULL;
static bool s_initialised = false;

/* ---------- Mock backend state ---------- */

static uint8_t  s_slot_data[FLASH_PARTITION_SLOT_COUNT][FLASH_PARTITION_MAX_SIZE];
static uint32_t s_slot_write_offset[FLASH_PARTITION_SLOT_COUNT];
static FlashSlotState_t s_slot_state[FLASH_PARTITION_SLOT_COUNT];
static uint8_t  s_boot_partition = FLASH_PARTITION_SLOT_A;
static uint8_t  s_original_boot_partition = FLASH_PARTITION_SLOT_A;
static uint8_t  s_ota_target = FLASH_PARTITION_SLOT_B;
static bool     s_ota_in_progress = false;

/* ---------- Mock backend callbacks ---------- */

static bool mock_begin(uint8_t slot, uint32_t size)
{
    if (slot >= FLASH_PARTITION_SLOT_COUNT) return false;
    if (size > FLASH_PARTITION_MAX_SIZE) return false;
    memset(s_slot_data[slot], 0xFF, size);  /* Erase (0xFF = erased flash) */
    s_slot_write_offset[slot] = 0;
    s_slot_state[slot] = FLASH_SLOT_STATE_WRITING;
    return true;
}

static bool mock_write(uint8_t slot, uint32_t offset,
                       const uint8_t *data, uint32_t data_len)
{
    if (slot >= FLASH_PARTITION_SLOT_COUNT) return false;
    if (offset + data_len > FLASH_PARTITION_MAX_SIZE) return false;
    if (s_slot_state[slot] != FLASH_SLOT_STATE_WRITING) return false;
    memcpy(s_slot_data[slot] + offset, data, data_len);
    s_slot_write_offset[slot] = offset + data_len;
    return true;
}

static bool mock_end(uint8_t slot)
{
    if (slot >= FLASH_PARTITION_SLOT_COUNT) return false;
    if (s_slot_state[slot] != FLASH_SLOT_STATE_WRITING) return false;
    s_slot_state[slot] = FLASH_SLOT_STATE_VALID;
    return true;
}

static bool mock_set_boot_partition(uint8_t slot)
{
    if (slot >= FLASH_PARTITION_SLOT_COUNT) return false;
    /* Allow setting boot partition to any slot — in real hardware,
     * the boot flag is independent of the slot's write state.
     * Rollback needs to switch back to the original slot which
     * may be ERASED (in the mock, since we only wrote to the new slot). */
    s_slot_state[slot] = FLASH_SLOT_STATE_BOOTABLE;
    return true;
}

static int mock_get_boot_partition(void)
{
    /* Return the slot that is in BOOTABLE state. */
    for (int i = 0; i < FLASH_PARTITION_SLOT_COUNT; i++) {
        if (s_slot_state[i] == FLASH_SLOT_STATE_BOOTABLE) return i;
    }
    return FLASH_PARTITION_SLOT_A;  /* Default. */
}

static FlashSlotState_t mock_get_slot_state(uint8_t slot)
{
    if (slot >= FLASH_PARTITION_SLOT_COUNT) return FLASH_SLOT_STATE_UNKNOWN;
    return s_slot_state[slot];
}

static bool mock_read(uint8_t slot, uint32_t offset,
                      uint8_t *out_buf, uint32_t read_len)
{
    if (slot >= FLASH_PARTITION_SLOT_COUNT) return false;
    if (offset + read_len > FLASH_PARTITION_MAX_SIZE) return false;
    memcpy(out_buf, s_slot_data[slot] + offset, read_len);
    return true;
}

static const uint8_t *mock_get_data(uint8_t slot)
{
    if (slot >= FLASH_PARTITION_SLOT_COUNT) return NULL;
    return s_slot_data[slot];
}

static uint32_t mock_get_write_offset(uint8_t slot)
{
    if (slot >= FLASH_PARTITION_SLOT_COUNT) return 0;
    return s_slot_write_offset[slot];
}

/** Default mock backend instance. */
static const FlashPartitionBackend_t s_mock_backend = {
    .begin             = mock_begin,
    .write             = mock_write,
    .end               = mock_end,
    .set_boot_partition = mock_set_boot_partition,
    .get_boot_partition = mock_get_boot_partition,
    .get_slot_state    = mock_get_slot_state,
    .read              = mock_read,
    .get_data          = mock_get_data,
    .get_write_offset  = mock_get_write_offset
};

/* ---------- Public API ---------- */

bool flash_partition_init(void)
{
    memset(s_slot_data, 0xFF, sizeof(s_slot_data));
    memset(s_slot_write_offset, 0, sizeof(s_slot_write_offset));
    for (int i = 0; i < FLASH_PARTITION_SLOT_COUNT; i++) {
        s_slot_state[i] = FLASH_SLOT_STATE_ERASED;
    }
    s_boot_partition = FLASH_PARTITION_SLOT_A;
    s_original_boot_partition = FLASH_PARTITION_SLOT_A;
    s_ota_target = FLASH_PARTITION_SLOT_B;
    s_ota_in_progress = false;

    /* Auto-register mock backend if none set. */
    if (!s_backend) {
        s_backend = &s_mock_backend;
    }

    s_initialised = true;
    return true;
}

void flash_partition_set_backend(const FlashPartitionBackend_t *backend)
{
    s_backend = backend;
}

bool flash_partition_ota_begin(uint32_t image_size)
{
    if (!s_initialised || !s_backend) return false;
    if (s_ota_in_progress) return false;

    s_original_boot_partition = s_boot_partition;
    s_ota_target = (s_boot_partition == FLASH_PARTITION_SLOT_A)
                 ? FLASH_PARTITION_SLOT_B : FLASH_PARTITION_SLOT_A;

    if (!s_backend->begin(s_ota_target, image_size)) return false;
    s_ota_in_progress = true;
    return true;
}

bool flash_partition_ota_write(uint32_t offset,
                               const uint8_t *data, uint32_t data_len)
{
    if (!s_initialised || !s_backend) return false;
    if (!s_ota_in_progress) return false;
    return s_backend->write(s_ota_target, offset, data, data_len);
}

bool flash_partition_ota_end(void)
{
    if (!s_initialised || !s_backend) return false;
    if (!s_ota_in_progress) return false;

    bool ok = s_backend->end(s_ota_target);
    s_ota_in_progress = false;
    return ok;
}

bool flash_partition_set_boot(void)
{
    if (!s_initialised || !s_backend) return false;
    if (!s_backend->set_boot_partition(s_ota_target)) return false;
    s_boot_partition = s_ota_target;
    return true;
}

bool flash_partition_rollback(void)
{
    if (!s_initialised || !s_backend) return false;
    if (!s_backend->set_boot_partition(s_original_boot_partition)) return false;
    s_boot_partition = s_original_boot_partition;
    return true;
}

uint8_t flash_partition_get_active_slot(void)
{
    return s_boot_partition;
}

uint8_t flash_partition_get_inactive_slot(void)
{
    return (s_boot_partition == FLASH_PARTITION_SLOT_A)
         ? FLASH_PARTITION_SLOT_B : FLASH_PARTITION_SLOT_A;
}

const FlashPartitionBackend_t *flash_partition_get_backend(void)
{
    return s_backend;
}
