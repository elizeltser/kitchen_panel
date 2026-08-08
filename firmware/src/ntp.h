#pragma once
#include <stdint.h>
#include <time.h>

/**
 * Sync system clock via SNTP (il.pool.ntp.org).
 * On failure logs a warning and continues with RTC estimate.
 * Returns 0 on success, negative errno on failure.
 */
int ntp_sync(void);

/** Current local time with DST-correct offset (uses POSIX TZ). */
struct tm ntp_get_local_time(void);

/** Raw UTC epoch seconds — for computing precise sleep durations. */
time_t ntp_get_utc_now(void);

/** NTP_SRC_* code describing the outcome of the last ntp_sync() call. */
uint8_t ntp_get_last_src(void);
