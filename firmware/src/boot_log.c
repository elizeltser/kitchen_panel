#include "boot_log.h"

#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <time.h>

LOG_MODULE_REGISTER(boot_log, LOG_LEVEL_INF);

#define STORE_KEY "dash/bootlog"

typedef struct {
	uint8_t head;    /* index where the NEXT write goes   */
	uint8_t count;   /* 0..BOOT_LOG_ENTRIES valid entries */
	uint8_t _pad[2];
	boot_log_entry_t entries[BOOT_LOG_ENTRIES];
} boot_log_store_t;  /* 4 + 32*16 = 516 bytes */

static boot_log_store_t _store;
static bool _loaded;

static void load(void)
{
	if (_loaded) return;
	ssize_t n = settings_load_one(STORE_KEY, &_store, sizeof(_store));
	if (n != (ssize_t)sizeof(_store)) {
		memset(&_store, 0, sizeof(_store));
	}
	_loaded = true;
}

void boot_log_init(void)
{
	load();
}

/* Israel local time from UTC epoch (same approximation as ntp.c). */
static void epoch_to_local(int64_t epoch, int *year, int *mon, int *mday,
			    int *hour, int *min)
{
	time_t t = (time_t)epoch;
	struct tm u;
	gmtime_r(&t, &u);
	int m = u.tm_mon + 1;
	int off = 2;
	if ((m >= 4 && m <= 9) ||
	    (m == 3 && u.tm_mday >= 25) ||
	    (m == 10 && u.tm_mday < 26)) {
		off = 3;
	}
	t += off * 3600;
	struct tm l;
	gmtime_r(&t, &l);
	*year  = l.tm_year + 1900;
	*mon   = l.tm_mon  + 1;
	*mday  = l.tm_mday;
	*hour  = l.tm_hour;
	*min   = l.tm_min;
}

void boot_log_dump(void)
{
	load();
	if (_store.count == 0) {
		LOG_INF("boot_log: empty");
		return;
	}

	static const char *wake_s[] = {"timer", "green", "left ", "right"};
	static const char *ntp_s[]  = {"sync    ", "fallback", "none    "};
	static const char *mode_s[] = {"active  ", "ntp_sync", "inactive"};

	LOG_INF("=== boot_log: %u entries (oldest first) ===", _store.count);

	uint8_t start = (_store.head + BOOT_LOG_ENTRIES - _store.count)
	                % BOOT_LOG_ENTRIES;
	for (uint8_t i = 0; i < _store.count; i++) {
		uint8_t idx = (start + i) % BOOT_LOG_ENTRIES;
		const boot_log_entry_t *e = &_store.entries[idx];
		int y, mo, d, h, mi;
		epoch_to_local(e->epoch, &y, &mo, &d, &h, &mi);
		LOG_INF("[%02u] %04d-%02d-%02d %02d:%02d  wake=%s  ntp=%s  mode=%s  sleep=%ds",
			i, y, mo, d, h, mi,
			wake_s[e->wakeup  < 4 ? e->wakeup  : 0],
			ntp_s [e->ntp_src < 3 ? e->ntp_src : 0],
			mode_s[e->mode    < 3 ? e->mode    : 0],
			e->sleep_s);
	}
	LOG_INF("==========================================");
}

void boot_log_append(const boot_log_entry_t *e)
{
	load();
	_store.entries[_store.head] = *e;
	_store.head = (_store.head + 1) % BOOT_LOG_ENTRIES;
	if (_store.count < BOOT_LOG_ENTRIES) {
		_store.count++;
	}
	settings_save_one(STORE_KEY, &_store, sizeof(_store));
}
