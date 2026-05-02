/*
 * ePaper driver interface for GDEY075T7 via UC8179 controller.
 *
 * This file wraps the Zephyr display subsystem driver.
 * The UC8179 driver is expected to be provided either by the board's
 * Zephyr BSP or vendored from the ZEReader project.
 *
 * Framebuffer format: 1 bit per pixel, packed, MSB first.
 * Display resolution: 800 × 480 pixels.
 */
#include "epaper.h"
#include "config.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(epaper, LOG_LEVEL_INF);

#define DISPLAY_WIDTH   800
#define DISPLAY_HEIGHT  480
#define FB_BYTES        (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8)

static const struct device *display_dev;

int epaper_init(void)
{
	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return -ENODEV;
	}
	display_set_pixel_format(display_dev, PIXEL_FORMAT_MONO10);
	LOG_INF("ePaper display initialised");
	return 0;
}

int epaper_full_refresh(const uint8_t *fb, size_t fb_len)
{
	if (!display_dev) return -ENODEV;

	struct display_buffer_descriptor desc = {
		.buf_size = fb_len,
		.width    = DISPLAY_WIDTH,
		.height   = DISPLAY_HEIGHT,
		.pitch    = DISPLAY_WIDTH,
	};

	/* blanking_on → write to back-buffer (DTM1) → blanking_off triggers full EPD refresh */
	display_blanking_on(display_dev);

	int ret = display_write(display_dev, 0, 0, &desc, fb);
	if (ret) {
		LOG_ERR("display_write failed: %d", ret);
		return ret;
	}

	/* PON → DRF → POF; leaves blanking_on=false for subsequent fast writes */
	ret = display_blanking_off(display_dev);
	if (ret) {
		LOG_ERR("blanking_off failed: %d", ret);
		return ret;
	}

	LOG_INF("Full refresh complete");
	return 0;
}

int epaper_fast_refresh(const uint8_t *fb, size_t fb_len)
{
	if (!display_dev) return -ENODEV;

	/* With blanking_on=false the driver writes to DTM2 and auto-triggers
	 * update_display() at the end — this is the fast/partial waveform path. */
	struct display_buffer_descriptor desc = {
		.buf_size = fb_len,
		.width    = DISPLAY_WIDTH,
		.height   = DISPLAY_HEIGHT,
		.pitch    = DISPLAY_WIDTH,
	};

	int ret = display_write(display_dev, 0, 0, &desc, fb);
	if (ret) {
		LOG_ERR("fast refresh failed: %d", ret);
	}
	LOG_INF("Fast refresh complete");
	return ret;
}

int epaper_partial_refresh(const uint8_t *fb, int x, int y, int w, int h)
{
	if (!display_dev) return -ENODEV;

	size_t region_bytes = (size_t)(w * h / 8);
	/* Build a sub-buffer for the region */
	static uint8_t region_buf[DISPLAY_WIDTH * 40 / 8]; /* max 40 rows at a time */
	if (region_bytes > sizeof(region_buf)) {
		LOG_WRN("Partial region too large — falling back to full refresh");
		return epaper_full_refresh(fb, FB_BYTES);
	}

	/* Extract rows from full framebuffer */
	int row_bytes = DISPLAY_WIDTH / 8;
	for (int row = 0; row < h; row++) {
		int src_byte = (y + row) * row_bytes + x / 8;
		memcpy(region_buf + row * (w / 8), fb + src_byte, w / 8);
	}

	struct display_buffer_descriptor desc = {
		.buf_size = region_bytes,
		.width    = w,
		.height   = h,
		.pitch    = w,
	};

	int ret = display_write(display_dev, x, y, &desc, region_buf);
	if (ret) {
		LOG_ERR("Partial refresh failed: %d", ret);
	}
	return ret;
}

void epaper_power_off(void)
{
	if (display_dev) {
		display_blanking_on(display_dev);
	}
}

void epaper_show_error(const char *msg)
{
	/* Minimal error display — white background, message in top-left.
	 * A proper implementation would render text using a bitmap font.
	 * For now, log only; the display retains its last image. */
	LOG_ERR("ePaper error screen: %s", msg);
}
