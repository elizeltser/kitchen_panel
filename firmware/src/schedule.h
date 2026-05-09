#pragma once
#include <stdbool.h>
#include <time.h>

typedef enum {
	MODE_ACTIVE_WINDOW,       /* morning or evening window — poll every 60s */
	MODE_NTP_SYNC_ONLY,       /* 00:00 daily NTP sync — no display update */
	MODE_INACTIVE,            /* outside all windows — sleep until next event */
} schedule_mode_t;

/**
 * Determine the current operating mode from local time.
 * @param t  Current local time (from ntp_get_local_time()).
 */
schedule_mode_t schedule_get_mode(const struct tm *t);

/**
 * Compute seconds to sleep until the next scheduled event.
 * @param t  Current local time.
 */
int schedule_sleep_seconds(const struct tm *t);

/** Returns true if t falls within the morning active window. */
bool schedule_is_morning(const struct tm *t);

/** Returns true if t falls within the evening active window. */
bool schedule_is_evening(const struct tm *t);
