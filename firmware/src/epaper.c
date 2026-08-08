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
#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(epaper, LOG_LEVEL_INF);

#define DISPLAY_WIDTH   800
#define DISPLAY_HEIGHT  480
#define FB_BYTES        (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8)

static const struct device *display_dev;
static const struct device *mipi_dev;

/* MIPI DBI SPI driver overrides .cs from DTS; frequency matches mipi-max-frequency */
static const struct mipi_dbi_config dbi_cfg = {
	.mode = MIPI_DBI_MODE_SPI_4WIRE,
	.config = {
		.frequency = 4000000,
		.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8),
	},
};

/* BUSY pin — active-low, already configured as input by the UC81xx driver */
static const struct gpio_dt_spec busy_gpio =
	GPIO_DT_SPEC_GET(DT_CHOSEN(zephyr_display), busy_gpios);

int epaper_init(void)
{
	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return -ENODEV;
	}
	display_set_pixel_format(display_dev, PIXEL_FORMAT_MONO10);

	/* DT_PARENT of epd_uc8179 = the mipi_dbi_epd controller */
	mipi_dev = DEVICE_DT_GET(DT_PARENT(DT_CHOSEN(zephyr_display)));
	if (!device_is_ready(mipi_dev)) {
		LOG_ERR("MIPI DBI device not ready");
		return -ENODEV;
	}

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

/* --- 4-gray helpers ---------------------------------------------------- */

/* Generous ceiling for a single BUSY assertion. Full 4-gray DRF can take
 * multiple seconds; this only exists to catch a panel that never releases
 * BUSY (electrical glitch, brownout during PON, bad waveform) so the boot
 * cycle can fail and retry instead of spinning forever. */
#define EPAPER_BUSY_TIMEOUT_MS 15000

static int epaper_4gray_busy_wait(void)
{
	/* gpio_pin_get_dt returns 1 = active = busy (active-low: low pin = busy) */
	int64_t start = k_uptime_get();
	while (gpio_pin_get_dt(&busy_gpio) > 0) {
		if (k_uptime_get() - start > EPAPER_BUSY_TIMEOUT_MS) {
			LOG_ERR("BUSY stuck low for over %d ms — panel unresponsive",
			        EPAPER_BUSY_TIMEOUT_MS);
			return -ETIMEDOUT;
		}
		k_msleep(5);
	}
	return 0;
}

static int epaper_4gray_cmd(uint8_t cmd, const uint8_t *data, size_t len)
{
	int ret = mipi_dbi_command_write(mipi_dev, &dbi_cfg, cmd, data, len);
	mipi_dbi_release(mipi_dev, &dbi_cfg);
	if (ret) {
		LOG_ERR("4gray cmd 0x%02x failed: %d", cmd, ret);
	}
	return ret;
}

/* --- 4-gray public API -------------------------------------------------- */

int epaper_4gray_init(void)
{
	if (!mipi_dev || !device_is_ready(mipi_dev)) {
		return -ENODEV;
	}

	static const uint8_t pwr[]  = {0x07, 0x07, 0x3F, 0x3F, 0x09};
	static const uint8_t btst[] = {0x27, 0x27, 0x18, 0x17};

	if (epaper_4gray_cmd(0x01, pwr,  sizeof(pwr)))   return -EIO;  /* PWR  */
	if (epaper_4gray_cmd(0x06, btst, sizeof(btst)))  return -EIO;  /* BTST */
	if (epaper_4gray_cmd(0x00, (uint8_t[]){0x1F}, 1)) return -EIO; /* PSR: KW, OTP */
	if (epaper_4gray_cmd(0x50, (uint8_t[]){0x10, 0x07}, 2)) return -EIO; /* CDI */

	if (epaper_4gray_cmd(0x04, NULL, 0)) return -EIO; /* PON */
	k_msleep(100);
	if (epaper_4gray_busy_wait()) return -ETIMEDOUT;

	if (epaper_4gray_cmd(0xE0, (uint8_t[]){0x02}, 1)) return -EIO; /* CCSET: TSFIX=1 */
	if (epaper_4gray_cmd(0xE5, (uint8_t[]){0x5F}, 1)) return -EIO; /* TSSET: 4-gray OTP table */

	LOG_INF("4-gray init complete");
	return 0;
}

int epaper_4gray_refresh(const uint8_t *dtm1, const uint8_t *dtm2, size_t plane_len)
{
	if (!mipi_dev) return -ENODEV;

	if (epaper_4gray_cmd(0x10, dtm1, plane_len)) return -EIO; /* DTM1 plane */
	if (epaper_4gray_cmd(0x13, dtm2, plane_len)) return -EIO; /* DTM2 plane */

	if (epaper_4gray_cmd(0x04, NULL, 0)) return -EIO; /* PON */
	if (epaper_4gray_busy_wait()) return -ETIMEDOUT;

	if (epaper_4gray_cmd(0x12, NULL, 0)) return -EIO; /* DRF */
	k_msleep(100);
	if (epaper_4gray_busy_wait()) return -ETIMEDOUT;

	if (epaper_4gray_cmd(0x02, NULL, 0)) return -EIO; /* POF */
	if (epaper_4gray_busy_wait()) return -ETIMEDOUT;

	LOG_INF("4-gray refresh complete");
	return 0;
}
