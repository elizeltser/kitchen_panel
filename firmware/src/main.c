/*
 * main.c — ePaper Dashboard Firmware, Stage 2
 *
 * Boot sequence (runs once; ends in sys_poweroff / deep sleep):
 *   Init → detect wakeup source → load NVS → Wi-Fi → NTP →
 *   schedule mode dispatch → sleep
 *
 * Deep sleep on ESP32-S3 is a full chip reset. All state that must
 * survive sleep is persisted in NVS (settings subsystem).
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/poweroff.h>
#include <esp_sleep.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "wifi.h"
#include "ntp.h"
#include "http_fetch.h"
#include "png_decode.h"
#include "epaper.h"
#include "schedule.h"
#include "button.h"
#include "nvs_store.h"
#include "screensaver.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Large buffers live in 8 MB PSRAM, not internal DRAM */
#define EXT_RAM __attribute__((section(".ext_ram.bss")))

static EXT_RAM uint8_t png_buf[PNG_BUF_SIZE];
static EXT_RAM uint8_t fb_dtm1[FB_PLANE_SIZE];
static EXT_RAM uint8_t fb_dtm2[FB_PLANE_SIZE];

/* File-scope state loaded from NVS at boot */
static char    stored_etag[64];
static uint8_t buf_index;
static uint8_t partial_count;

static void enter_deep_sleep(int sleep_s)
{
	epaper_power_off();
	LOG_INF("Deep sleep %d s", sleep_s);
	esp_sleep_enable_timer_wakeup((uint64_t)sleep_s * 1000000ULL);
	sys_poweroff();
}

static void do_fetch_and_display(const char *path, const char *etag_in,
                                  bool force_full)
{
	size_t bytes = 0;
	char new_etag[64] = "";

	int ret = http_fetch(path, etag_in, png_buf, PNG_BUF_SIZE, &bytes, new_etag);

	if (ret == HTTP_FETCH_NOT_MODIFIED) {
		LOG_INF("304 Not Modified — skipping refresh");
		return;
	}
	if (ret == HTTP_FETCH_NOT_FOUND) {
		LOG_WRN("404 for %s — buffer index reset to 0", path);
		buf_index = 0;
		nvs_store_buf_index_set(0);
		/* Retry with main display path */
		ret = http_fetch("/display.png", etag_in, png_buf, PNG_BUF_SIZE,
		                 &bytes, new_etag);
		if (ret != HTTP_FETCH_OK || bytes == 0) {
			LOG_ERR("Fallback fetch also failed");
			return;
		}
	}
	if (ret != HTTP_FETCH_OK || bytes == 0) {
		LOG_ERR("Fetch failed for %s (ret=%d)", path, ret);
		return;
	}

	if (png_decode_4gray(png_buf, bytes, fb_dtm1, fb_dtm2,
	                     DISPLAY_WIDTH, DISPLAY_HEIGHT) != 0) {
		LOG_ERR("PNG decode failed");
		return;
	}

	if (new_etag[0]) {
		strncpy(stored_etag, new_etag, sizeof(stored_etag) - 1);
		stored_etag[sizeof(stored_etag) - 1] = '\0';
		nvs_store_etag_set(stored_etag);
	}

	bool do_full = force_full || (partial_count >= MAX_PARTIAL_BEFORE_FULL);

	epaper_4gray_refresh(fb_dtm1, fb_dtm2, FB_PLANE_SIZE);

	if (do_full) {
		partial_count = 0;
	} else {
		partial_count++;
	}
	nvs_store_partial_count_set(partial_count);
}

int main(void)
{
	LOG_INF("ePaper Dashboard starting (Stage 2)");

	/* ── Phase 1: Hardware init ─────────────────────────────── */
	nvs_store_init();

	if (epaper_init() != 0 || epaper_4gray_init() != 0) {
		LOG_ERR("ePaper init failed — sleeping %d s", POLL_INTERVAL_INACTIVE_S);
		esp_sleep_enable_timer_wakeup(
			(uint64_t)POLL_INTERVAL_INACTIVE_S * 1000000ULL);
		sys_poweroff();
		return 0;
	}

	button_init();

	/* Enable GPIO wakeup at the device level — required for EXT1 wakeup */
	const struct device *gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
	if (device_is_ready(gpio0)) {
		pm_device_wakeup_enable(gpio0, true);
	} else {
		LOG_WRN("gpio0 device not ready — button wakeup may not work");
	}

	/* ── Phase 2: Wakeup source detection ──────────────────── */
	button_mask_t wakeup = button_get_wakeup_source();
	bool green_wake = (wakeup & BUTTON_GREEN) != 0;
	bool left_wake  = (wakeup & BUTTON_LEFT)  != 0;
	bool right_wake = (wakeup & BUTTON_RIGHT) != 0;
	bool btn_wake   = (wakeup != BUTTON_NONE);

	LOG_INF("Wakeup: green=%d left=%d right=%d timer=%d",
	        green_wake, left_wake, right_wake, !btn_wake);

	/* ── Phase 3: Load persisted state ──────────────────────── */
	nvs_store_etag_get(stored_etag, sizeof(stored_etag));
	nvs_store_buf_index_get(&buf_index);
	nvs_store_partial_count_get(&partial_count);

	/* ── Phase 4: Wi-Fi ─────────────────────────────────────── */
	LOG_INF("Connecting to Wi-Fi \"%s\"", WIFI_SSID);
	if (wifi_connect() != 0) {
		epaper_show_error(
			"Wi-Fi connection failed\n"
			"Could not connect to: " WIFI_SSID "\n"
			"Retrying in 5 minutes."
		);
		enter_deep_sleep(300);
		return 0;
	}

	/* ── Phase 5: Time sync ──────────────────────────────────── */
	ntp_sync();
	struct tm now = ntp_get_local_time();
	schedule_mode_t mode = schedule_get_mode(&now);

	/* ── Phase 6: Mode dispatch ──────────────────────────────── */
	if (mode == MODE_INACTIVE) {

		if (btn_wake) {
			/* Adjust buffer index if left/right triggered */
			if (left_wake && buf_index > 0) {
				buf_index--;
				nvs_store_buf_index_set(buf_index);
			} else if (right_wake) {
				buf_index++;
				nvs_store_buf_index_set(buf_index);
			}
			/* Show screensaver for up to 5 min, then fall through to sleep */
			screensaver_run(png_buf, PNG_BUF_SIZE, fb_dtm1, fb_dtm2,
			                FB_PLANE_SIZE);
		}
		/* Timer wake in INACTIVE: no display update, just sleep */

	} else if (mode == MODE_SCHEDULED_REFRESH) {

		/* Maintenance refresh (08:00 / 12:00 / 22:00): force full, clear ETag */
		partial_count = 0;
		nvs_store_partial_count_set(0);
		nvs_store_etag_set("");
		stored_etag[0] = '\0';
		do_fetch_and_display("/display.png", NULL, true);

	} else { /* MODE_ACTIVE_WINDOW */

		char path[32];

		if (green_wake) {
			/* Force full refresh, ignore stored ETag */
			partial_count = 0;
			nvs_store_partial_count_set(0);
			nvs_store_etag_set("");
			stored_etag[0] = '\0';
			do_fetch_and_display("/display.png", NULL, true);

		} else if (left_wake || right_wake) {
			/* Cycle display buffer */
			if (left_wake && buf_index > 0) {
				buf_index--;
			} else if (right_wake) {
				buf_index++;
			}
			nvs_store_buf_index_set(buf_index);
			nvs_store_etag_set("");
			stored_etag[0] = '\0';
			snprintf(path, sizeof(path), "/display/%u", (unsigned)buf_index);
			do_fetch_and_display(path, NULL, true);

		} else {
			/* Normal poll — use ETag to skip unchanged display */
			do_fetch_and_display("/display.png", stored_etag, false);
		}
	}

	/* ── Phase 7: Sleep ─────────────────────────────────────── */
	int sleep_s;
	if (mode == MODE_ACTIVE_WINDOW) {
		sleep_s = POLL_INTERVAL_ACTIVE_S;
	} else {
		sleep_s = schedule_sleep_seconds(&now);
		if (sleep_s <= 0) {
			sleep_s = POLL_INTERVAL_INACTIVE_S;
		}
	}

	enter_deep_sleep(sleep_s);
	return 0;
}
