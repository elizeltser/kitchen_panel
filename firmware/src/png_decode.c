#include "png_decode.h"
#include "pngle.h"

#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(png_decode, LOG_LEVEL_INF);

/* Context passed through pngle callbacks */
struct decode_ctx {
	uint8_t *fb;
	int width;
	int height;
};

static void on_draw(pngle_t *pngle, uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h, const uint8_t rgba[4])
{
	struct decode_ctx *ctx = pngle_get_user_data(pngle);

	if ((int)x >= ctx->width || (int)y >= ctx->height) return;

	/* Convert to 1-bit: luminance > 127 → white (1), else black (0) */
	uint8_t lum = (uint8_t)(0.299f * rgba[0] + 0.587f * rgba[1] + 0.114f * rgba[2]);
	int bit = (lum > 127) ? 1 : 0;

	/* Pack into framebuffer MSB first */
	int pixel_idx = y * ctx->width + x;
	int byte_idx  = pixel_idx / 8;
	int bit_pos   = 7 - (pixel_idx % 8);

	if (bit) {
		ctx->fb[byte_idx] |=  (1 << bit_pos);
	} else {
		ctx->fb[byte_idx] &= ~(1 << bit_pos);
	}
}

int png_decode(const uint8_t *png_data, size_t png_len,
               uint8_t *fb, int fb_width, int fb_height)
{
	/* Zero the framebuffer (all black) */
	memset(fb, 0, (fb_width * fb_height + 7) / 8);

	pngle_t *pngle = pngle_new();
	if (!pngle) {
		LOG_ERR("pngle_new failed");
		return -1;
	}

	struct decode_ctx ctx = {.fb = fb, .width = fb_width, .height = fb_height};
	pngle_set_user_data(pngle, &ctx);
	pngle_set_draw_callback(pngle, on_draw);

	int ret = pngle_feed(pngle, png_data, png_len);
	pngle_destroy(pngle);

	if (ret < 0) {
		LOG_ERR("pngle_feed error: %d", ret);
		return -1;
	}
	return 0;
}
