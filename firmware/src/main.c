/*
 * main.c — ePaper Dashboard Firmware
 *
 * Stage 1 build: minimal loop — Wi-Fi + fetch + display every 60s.
 * No ETag, no PM sleep, no schedule. Proves hardware pipeline.
 *
 * Stage 2 will add: ETag/NVS, PM sleep, scheduling, green button.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "config.h"
#include "wifi.h"
#include "ntp.h"
#include "http_fetch.h"
#include "png_decode.h"
#include "epaper.h"
#include "schedule.h"
#include "button.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Large buffers live in 8 MB PSRAM, not internal DRAM */
#define EXT_RAM __attribute__((section(".ext_ram.bss")))

#define PNG_BUF_SIZE (512 * 1024)
static EXT_RAM uint8_t png_buf[PNG_BUF_SIZE];

#define FB_SIZE (800 * 480 / 8)
static EXT_RAM uint8_t fb[FB_SIZE];

/* ETag storage (Stage 2: move to NVS) */
static char stored_etag[64] = "";

int main(void)
{
	LOG_INF("ePaper Dashboard starting");

	/* Init peripherals */
	button_init();
	if (epaper_init()) {
		LOG_ERR("ePaper init failed — halting");
		return -1;
	}

	/* Connect Wi-Fi */
	LOG_INF("Connecting to Wi-Fi \"%s\"", WIFI_SSID);
	if (wifi_connect()) {
		epaper_show_error(
			"Wi-Fi connection failed\n"
			"Could not connect to: " WIFI_SSID "\n"
			"Check config.h"
		);
		/* Stage 1: halt. Stage 2: PM sleep 5 min and retry. */
		while (1) k_sleep(K_FOREVER);
	}

	/* Sync time */
	ntp_sync();

	/* First refresh is always full; then cycle partial/full */
	int refresh_counter = 0;

	while (1) {
		size_t bytes = 0;
		char new_etag[64] = "";

		LOG_INF("Fetching %s:%d%s", SERVER_HOST, SERVER_PORT, SERVER_PATH);

		int result = http_fetch_display(
			NULL,          /* Stage 1: no ETag — always fetch */
			png_buf, PNG_BUF_SIZE, &bytes, new_etag
		);

		if (result == HTTP_FETCH_OK && bytes > 0) {
			LOG_INF("Received %zu bytes, ETag: %s", bytes, new_etag);

			if (png_decode(png_buf, bytes, fb, 800, 480) == 0) {
				bool do_full = (refresh_counter % MAX_PARTIAL_BEFORE_FULL == 0);
				if (do_full) {
					LOG_INF("Full refresh (#%d)", refresh_counter);
					epaper_full_refresh(fb, FB_SIZE);
				} else {
					LOG_INF("Fast refresh (#%d)", refresh_counter);
					epaper_fast_refresh(fb, FB_SIZE);
				}
				refresh_counter++;
				strncpy(stored_etag, new_etag, sizeof(stored_etag) - 1);
			} else {
				LOG_ERR("PNG decode failed");
			}
		} else if (result == HTTP_FETCH_NOT_MODIFIED) {
			LOG_INF("Image unchanged (304)");
		} else {
			LOG_ERR("Fetch failed");
		}

		LOG_INF("Sleeping %d s", POLL_INTERVAL_ACTIVE_S);
		k_sleep(K_SECONDS(POLL_INTERVAL_ACTIVE_S));
	}

	return 0;
}
