#include "ntp.h"
#include "boot_log.h"
#include "config.h"
#include "nvs_store.h"

#include <zephyr/kernel.h>
#include <zephyr/net/sntp.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/logging/log.h>
#include <time.h>

LOG_MODULE_REGISTER(ntp_mgr, LOG_LEVEL_INF);

static time_t  _epoch_at_sync     = 0;
static int64_t _uptime_ms_at_sync = 0;
static uint8_t _last_ntp_src      = NTP_SRC_NONE;

/* UTC struct tm → epoch without TZ dependency */
static time_t utc_tm_to_epoch(const struct tm *t)
{
	static const int mdays[] = {0,31,59,90,120,151,181,212,243,273,304,334};
	int y    = t->tm_year + 1900;
	int leap = (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0);
	long days = (long)(y - 1970) * 365
	            + (y - 1969) / 4 - (y - 1901) / 100 + (y - 1601) / 400
	            + mdays[t->tm_mon]
	            + (t->tm_mon > 1 ? leap : 0)
	            + t->tm_mday - 1;
	return (time_t)(days * 86400L
	                + t->tm_hour * 3600L
	                + t->tm_min  * 60L
	                + t->tm_sec);
}

static const struct device *rtc_dev(void)
{
	static const struct device *dev;
	if (!dev) {
		dev = DEVICE_DT_GET_ANY(nxp_pcf8563);
		if (!dev || !device_is_ready(dev)) {
			LOG_WRN("PCF8563 not ready");
			dev = NULL;
		}
	}
	return dev;
}

static void rtc_write_utc(time_t utc_epoch)
{
	const struct device *dev = rtc_dev();
	if (!dev) return;
	struct tm t;
	gmtime_r(&utc_epoch, &t);
	struct rtc_time rt = {
		.tm_sec  = t.tm_sec,  .tm_min  = t.tm_min,
		.tm_hour = t.tm_hour, .tm_mday = t.tm_mday,
		.tm_mon  = t.tm_mon,  .tm_year = t.tm_year,
		.tm_wday = t.tm_wday,
	};
	if (rtc_set_time(dev, &rt) != 0) {
		LOG_WRN("RTC set failed");
	}
}

static time_t rtc_read_utc_epoch(void)
{
	const struct device *dev = rtc_dev();
	if (!dev) return 0;
	struct rtc_time rt;
	if (rtc_get_time(dev, &rt) != 0) return 0;
	struct tm t = {
		.tm_sec  = rt.tm_sec,  .tm_min  = rt.tm_min,
		.tm_hour = rt.tm_hour, .tm_mday = rt.tm_mday,
		.tm_mon  = rt.tm_mon,  .tm_year = rt.tm_year,
	};
	return utc_tm_to_epoch(&t);
}

int ntp_sync(void)
{
	struct sntp_time t;
	int ret = sntp_simple(NTP_SERVER, 5000, &t);
	if (ret) {
		/* RTC keeps running during deep sleep — use it as primary fallback. */
		time_t rtc_epoch = rtc_read_utc_epoch();
		if (rtc_epoch > 1700000000) {   /* sanity: after Nov 2023 */
			_epoch_at_sync     = rtc_epoch;
			_uptime_ms_at_sync = k_uptime_get();
			_last_ntp_src      = NTP_SRC_FALLBACK;
			LOG_WRN("SNTP failed (%d) — using RTC epoch %lld", ret, (long long)rtc_epoch);
		} else {
			/* RTC not set yet — last resort: NVS snapshot */
			int64_t stored = 0;
			nvs_store_epoch_get(&stored);
			if (stored > 0) {
				_epoch_at_sync     = (time_t)stored;
				_uptime_ms_at_sync = 0;
				_last_ntp_src      = NTP_SRC_FALLBACK;
				LOG_WRN("SNTP failed (%d) — RTC unset, using NVS epoch %lld",
				        ret, (long long)stored);
			} else {
				_last_ntp_src = NTP_SRC_NONE;
				LOG_WRN("SNTP failed (%d) — no time source available", ret);
			}
		}
		return ret;
	}

	_epoch_at_sync     = (time_t)t.seconds;
	_uptime_ms_at_sync = k_uptime_get();
	_last_ntp_src      = NTP_SRC_SYNCED;
	rtc_write_utc((time_t)t.seconds);
	nvs_store_epoch_set((int64_t)t.seconds);
	LOG_INF("Clock synced: epoch=%lld (RTC updated)", (long long)_epoch_at_sync);
	return 0;
}

uint8_t ntp_get_last_src(void)
{
	return _last_ntp_src;
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
