#include "ntp.h"
#include "config.h"

#include <zephyr/kernel.h>
#include <zephyr/net/sntp.h>
#include <zephyr/logging/log.h>
#include <time.h>

LOG_MODULE_REGISTER(ntp_mgr, LOG_LEVEL_INF);

static time_t  _epoch_at_sync  = 0;
static int64_t _uptime_ms_at_sync = 0;

int ntp_sync(void)
{
	struct sntp_time t;
	int ret = sntp_simple(NTP_SERVER, 5000, &t);
	if (ret) {
		LOG_WRN("SNTP failed (%d) — continuing with RTC estimate", ret);
		return ret;
	}
	_epoch_at_sync     = (time_t)t.seconds;
	_uptime_ms_at_sync = k_uptime_get();
	LOG_INF("Clock synced: epoch=%lld", (long long)_epoch_at_sync);
	return 0;
}

time_t ntp_get_utc_now(void)
{
	int64_t elapsed_ms = k_uptime_get() - _uptime_ms_at_sync;
	return _epoch_at_sync + (time_t)(elapsed_ms / 1000);
}

/*
 * Israel offset: UTC+2 (IST) in winter, UTC+3 (IDT) in summer.
 * DST starts last Friday of March at 02:00 local; ends last Sunday of
 * October at 02:00 local. We approximate with fixed month boundaries
 * (at most 7 days error during transition weeks — acceptable for a display).
 */
static int israel_offset_sec(const struct tm *utc)
{
	int m = utc->tm_mon + 1; /* 1-12 */

	if (m >= 4 && m <= 9) return 3 * 3600; /* April-September: IDT */
	if (m == 3) return (utc->tm_mday >= 25) ? 3 * 3600 : 2 * 3600;
	if (m == 10) return (utc->tm_mday < 26) ? 3 * 3600 : 2 * 3600;
	return 2 * 3600; /* November-February: IST */
}

struct tm ntp_get_local_time(void)
{
	time_t utc_epoch = ntp_get_utc_now();
	struct tm utc_tm;
	gmtime_r(&utc_epoch, &utc_tm);

	time_t local_epoch = utc_epoch + israel_offset_sec(&utc_tm);
	struct tm result;
	gmtime_r(&local_epoch, &result);
	return result;
}
