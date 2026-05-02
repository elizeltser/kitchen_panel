#include "ntp.h"
#include "config.h"

#include <zephyr/kernel.h>
#include <zephyr/net/sntp.h>
#include <zephyr/logging/log.h>
#include <time.h>

LOG_MODULE_REGISTER(ntp_mgr, LOG_LEVEL_INF);

/* UTC epoch captured at sync, plus the uptime at that moment. */
static time_t  _epoch_at_sync = 0;
static int64_t _uptime_ms_at_sync = 0;

int ntp_sync(void)
{
	struct sntp_time t;
	int ret = sntp_simple(NTP_SERVER, 5000, &t);
	if (ret) {
		LOG_WRN("SNTP failed (%d) — will use fallback time", ret);
		return ret;
	}
	_epoch_at_sync    = (time_t)t.seconds;
	_uptime_ms_at_sync = k_uptime_get();
	LOG_INF("Clock synced: epoch=%lld", (long long)_epoch_at_sync);
	return 0;
}

struct tm ntp_get_local_time(void)
{
	int64_t elapsed_ms = k_uptime_get() - _uptime_ms_at_sync;
	/* +2h fixed offset for IST (no DST handling at this stage) */
	time_t local = _epoch_at_sync + (time_t)(elapsed_ms / 1000) + 2 * 3600;

	struct tm result;
	gmtime_r(&local, &result);
	return result;
}
