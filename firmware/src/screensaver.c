#include "screensaver.h"
#include "config.h"
#include "http_fetch.h"
#include "png_decode.h"
#include "epaper.h"

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
	char *p = strstr(text, "count");
	if (!p) return 0;
	p = strchr(p, ':');
	if (!p) return 0;
	return (int)strtol(p + 1, NULL, 10);
}

void screensaver_show_one(int index_hint, uint8_t *png_buf, size_t png_buf_size,
                          uint8_t *dtm1, uint8_t *dtm2, size_t fb_size)
{
	int count = fetch_count();
	if (count <= 0) {
		LOG_INF("No screensavers available — skipping");
		return;
	}

	int index = ((index_hint % count) + count) % count;
	char path[32];
	snprintf(path, sizeof(path), "/screensaver/%d", index);

	size_t bytes = 0;
	int ret = http_fetch(path, NULL, png_buf, png_buf_size, &bytes, NULL);
	if (ret != HTTP_FETCH_OK || bytes == 0) {
		LOG_WRN("Screensaver %d fetch failed (ret=%d)", index, ret);
		return;
	}
	if (png_decode_4gray(png_buf, bytes, dtm1, dtm2, DISPLAY_WIDTH, DISPLAY_HEIGHT) != 0) {
		LOG_WRN("Screensaver %d decode failed", index);
		return;
	}
	epaper_4gray_refresh(dtm1, dtm2, fb_size);
	LOG_INF("Screensaver %d displayed", index);
}
