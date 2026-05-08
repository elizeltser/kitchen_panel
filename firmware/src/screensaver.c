#include "screensaver.h"
#include "config.h"
#include "http_fetch.h"
#include "png_decode.h"
#include "epaper.h"
#include "button.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(screensaver, LOG_LEVEL_INF);

static int fetch_count(void)
{
	char text[64];
	size_t n;
	if (http_fetch_text("/screensaver/count", text, sizeof(text), &n) != HTTP_FETCH_OK) {
		LOG_WRN("Failed to fetch screensaver count");
		return 0;
	}
	/* Parse {"count": N} — find the number after "count": */
	char *p = strstr(text, "count");
	if (!p) return 0;
	p = strchr(p, ':');
	if (!p) return 0;
	return (int)strtol(p + 1, NULL, 10);
}

static bool show_image(int index, uint8_t *png_buf, size_t png_buf_size,
                       uint8_t *dtm1, uint8_t *dtm2, size_t fb_size)
{
	char path[32];
	snprintf(path, sizeof(path), "/screensaver/%d", index);

	size_t bytes = 0;
	int ret = http_fetch(path, NULL, png_buf, png_buf_size, &bytes, NULL);
	if (ret != HTTP_FETCH_OK || bytes == 0) {
		LOG_WRN("Screensaver %d fetch failed (ret=%d)", index, ret);
		return false;
	}
	if (png_decode_4gray(png_buf, bytes, dtm1, dtm2, DISPLAY_WIDTH, DISPLAY_HEIGHT) != 0) {
		LOG_WRN("Screensaver %d decode failed", index);
		return false;
	}
	epaper_4gray_refresh(dtm1, dtm2, fb_size);
	return true;
}

void screensaver_run(uint8_t *png_buf, size_t png_buf_size,
                     uint8_t *dtm1, uint8_t *dtm2, size_t fb_size)
{
	int count = fetch_count();
	if (count <= 0) {
		LOG_INF("No screensavers available — skipping");
		return;
	}
	LOG_INF("Starting screensaver (%d images, %d ms timeout)", count,
	        SCREENSAVER_ACTIVE_MS);

	button_enable_runtime_irq();

	int current = 0;
	show_image(current, png_buf, png_buf_size, dtm1, dtm2, fb_size);

	int64_t start_ms      = k_uptime_get();
	int64_t cycle_start   = start_ms;

	while (1) {
		int64_t now           = k_uptime_get();
		int64_t total_elapsed = now - start_ms;
		int64_t cycle_elapsed = now - cycle_start;

		if (total_elapsed >= SCREENSAVER_ACTIVE_MS) {
			LOG_INF("Screensaver timeout");
			break;
		}

		int32_t remaining_total = (int32_t)(SCREENSAVER_ACTIVE_MS - total_elapsed);
		int32_t remaining_cycle = (int32_t)(SCREENSAVER_CYCLE_MS  - cycle_elapsed);

		if (remaining_cycle <= 0) {
			current = (current + 1) % count;
			show_image(current, png_buf, png_buf_size, dtm1, dtm2, fb_size);
			cycle_start = k_uptime_get();
			continue;
		}

		int32_t wait_ms = MIN(remaining_cycle, remaining_total);
		button_mask_t btn = button_wait_any(wait_ms);

		if (btn == BUTTON_GREEN) {
			LOG_INF("Screensaver interrupted by green button");
			break;
		} else if (btn == BUTTON_LEFT) {
			current = (current - 1 + count) % count;
			show_image(current, png_buf, png_buf_size, dtm1, dtm2, fb_size);
			cycle_start = k_uptime_get();
		} else if (btn == BUTTON_RIGHT) {
			current = (current + 1) % count;
			show_image(current, png_buf, png_buf_size, dtm1, dtm2, fb_size);
			cycle_start = k_uptime_get();
		}
		/* BUTTON_NONE = wait_ms elapsed, loop rechecks timers */
	}
}
