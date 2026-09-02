/**
 * @file time_source.h
 * @brief Time source — NTP/RTC fallback with unreliable-time detection.
 *
 * Architecture ref: Section 12 (RTC fallback, NTP sync).
 *
 * Provides a single function (time_source_get_ms()) that all other
 * modules call for timestamps. Implements fallback priority:
 *   1. NTP if recently synced and considered valid
 *   2. RTC if available
 *   3. Documented "unreliable" flag (never silently return 0 or a
 *      plausible-looking-but-wrong value)
 *
 * Matches the HVAC clock-unreliable design principle from Phase 5.2:
 * callers need to know if the timestamp is untrustworthy.
 */

#ifndef TIME_SOURCE_H
#define TIME_SOURCE_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Constants ---------- */

/** Maximum age (ms) for NTP sync to be considered valid. */
#define TIME_SOURCE_NTP_VALIDITY_MS  (30 * 60 * 1000)  /**< 30 minutes. */

/* ---------- Types ---------- */

/**
 * Time source state.
 */
typedef enum {
    TIME_SRC_UNINITIALISED = 0,
    TIME_SRC_NTP_ACTIVE,       /**< NTP recently synced, timestamps trustworthy. */
    TIME_SRC_RTC_ONLY,         /**< NTP unavailable, using RTC (battery-backed). */
    TIME_SRC_UNRELIABLE        /**< Neither NTP nor RTC available. */
} TimeSourceState_t;

/**
 * Time source backend — mockable for testing.
 * In production, these would call real NTP/RTC hardware.
 */
typedef struct {
    /** Get NTP time in ms. Returns 0 if NTP is unavailable/stale. */
    uint64_t (*ntp_time_get)(void);
    /** Check if NTP was recently synced (within validity window). */
    bool (*ntp_is_synced)(void);
    /** Get RTC time in ms. Returns 0 if RTC hardware is unavailable. */
    uint64_t (*rtc_time_get)(void);
    /** Check if RTC hardware is present and functional. */
    bool (*rtc_is_available)(void);
} TimeSourceBackend_t;

/**
 * Storage backend for mock RTC persistence.
 * In production, the RTC chip (e.g. DS3231) IS the persistence —
 * it provides absolute wall-clock time directly from its own
 * independently-powered oscillator. The MCU just reads it.
 *
 * This storage is for the MOCK layer only: it lets tests simulate
 * what the RTC chip would report after a power cycle, without needing
 * real hardware. Tests call time_source_mock_set_rtc_persistent(ms)
 * to set "what the RTC chip reports now" before each simulated reboot.
 */
typedef struct {
    /** Save mock RTC value for persistence across simulated reboots. */
    bool (*save)(uint64_t rtc_time_ms);
    /** Load mock RTC value after simulated reboot. */
    bool (*load)(uint64_t *rtc_time_ms);
} TimeSourceStorage_t;

/* ---------- API ---------- */

/**
 * Register a storage backend for time source persistence.
 * Must be called before time_source_init() if persistence is desired.
 *
 * @param storage  Storage backend callbacks. Pass NULL to disable.
 */
void time_source_set_storage(const TimeSourceStorage_t *storage);

/**
 * Initialise the time source subsystem.
 * If a storage backend is registered, loads the last-known RTC time
 * reference so RTC-backed timestamps persist across reboots.
 *
 * @return true on success.
 */
bool time_source_init(void);

/**
 * Register the time source backend (NTP/RTC callbacks).
 * Must be called before time_source_init() if real sources are available.
 *
 * @param backend  Backend callbacks (NULL for mock-only operation).
 */
void time_source_set_backend(const TimeSourceBackend_t *backend);

/**
 * Get the current time in milliseconds.
 *
 * Implements fallback priority:
 *   1. NTP if recently synced → trustworthy
 *   2. RTC if available → trustworthy
 *   3. Neither → returns 0, sets unreliable flag
 *
 * @return Current time in ms, or 0 if no reliable source available.
 */
uint64_t time_source_get_ms(void);

/**
 * Check if the current timestamp is trustworthy.
 *
 * @return true if a reliable time source (NTP or RTC) is active.
 */
bool time_source_is_reliable(void);

/**
 * Get the current time source state.
 *
 * @return TimeSourceState_t value.
 */
TimeSourceState_t time_source_get_state(void);

/**
 * Mock: set the time source to a specific value for testing.
 *
 * @param ms       Time value in milliseconds.
 * @param reliable Whether to report this time as trustworthy.
 */
void time_source_mock_set(uint64_t ms, bool reliable);

/**
 * Mock: advance time by a delta (for testing timeout/aging logic).
 *
 * @param delta_ms  Milliseconds to advance.
 */
void time_source_mock_advance(uint64_t delta_ms);

/**
 * Mock: set what the RTC chip would report as its current time.
 * This represents absolute wall-clock time from the RTC hardware —
 * the MCU just reads it, no delta arithmetic needed.
 *
 * Call this before each simulated reboot to advance the mock RTC's
 * notion of wall-clock time (simulating the RTC chip's independently
 * powered oscillator continuing to tick during the outage).
 *
 * @param ms  Absolute wall-clock time the RTC chip would report.
 */
void time_source_mock_set_rtc_persistent(uint64_t ms);

#endif /* TIME_SOURCE_H */
