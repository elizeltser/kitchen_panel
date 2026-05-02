#include "schedule.h"
#include "config.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(schedule, LOG_LEVEL_DBG);

static int to_minutes(const struct tm *t)
{
	return t->tm_hour * 60 + t->tm_min;
}

bool schedule_is_morning(const struct tm *t)
{
	int m = to_minutes(t);
	int start = MORNING_START_H * 60 + MORNING_START_M;
	int end   = MORNING_END_H   * 60 + MORNING_END_M;
	return m >= start && m < end;
}

bool schedule_is_evening(const struct tm *t)
{
	int m = to_minutes(t);
	int start = EVENING_START_H * 60 + EVENING_START_M;
	int end   = EVENING_END_H   * 60 + EVENING_END_M;
	return m >= start && m < end;
}

static bool is_maintenance(const struct tm *t)
{
	int m = to_minutes(t);
	return m == MAINTENANCE_1_H * 60 + MAINTENANCE_1_M ||
	       m == MAINTENANCE_2_H * 60 + MAINTENANCE_2_M ||
	       m == MAINTENANCE_3_H * 60 + MAINTENANCE_3_M;
}

schedule_mode_t schedule_get_mode(const struct tm *t)
{
	if (is_maintenance(t)) return MODE_SCHEDULED_REFRESH;
	if (schedule_is_morning(t) || schedule_is_evening(t)) return MODE_ACTIVE_WINDOW;
	return MODE_INACTIVE;
}

int schedule_sleep_seconds(const struct tm *t)
{
	int now = to_minutes(t);

	/* Ordered list of next wake-up times (minutes since midnight) */
	int events[] = {
		MORNING_START_H * 60 + MORNING_START_M,   /* 05:45 */
		MAINTENANCE_1_H * 60 + MAINTENANCE_1_M,   /* 08:00 */
		MAINTENANCE_2_H * 60 + MAINTENANCE_2_M,   /* 12:00 */
		EVENING_START_H * 60 + EVENING_START_M,   /* 18:00 */
		MAINTENANCE_3_H * 60 + MAINTENANCE_3_M,   /* 22:00 */
	};

	for (int i = 0; i < (int)(sizeof(events) / sizeof(events[0])); i++) {
		if (events[i] > now) {
			int delta = (events[i] - now) * 60 - t->tm_sec;
			LOG_DBG("Next event in %d s (at %02d:%02d)",
			        delta, events[i] / 60, events[i] % 60);
			return delta;
		}
	}

	/* All today's events passed — sleep until 05:45 tomorrow */
	int delta = ((24 * 60 - now) + MORNING_START_H * 60 + MORNING_START_M) * 60
	            - t->tm_sec;
	LOG_DBG("Sleeping until tomorrow 05:45 (%d s)", delta);
	return delta;
}
