#pragma once
#include <time.h>

/**
 * Sync system clock via SNTP (il.pool.ntp.org).
 * Falls back to RTC on failure.
 * Returns 0 on success, negative errno on failure.
 */
int ntp_sync(void);

/**
 * Return current local time broken down into struct tm.
 * Applies a fixed IST +2h offset (simplified — does not handle DST).
 * For a full solution, configure Zephyr's POSIX TZ support.
 */
struct tm ntp_get_local_time(void);
