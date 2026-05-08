#pragma once
#include <stdint.h>
#include <stddef.h>

/**
 * Run the screensaver blocking loop.
 *
 * Fetches screensaver count from GET /screensaver/count, then cycles
 * images from GET /screensaver/{n} every SCREENSAVER_CYCLE_MS seconds.
 * Left/right buttons cycle prev/next; green button exits early.
 * Returns after SCREENSAVER_ACTIVE_MS or green button press.
 * Caller should then enter deep sleep.
 *
 * If the server has no screensaver images (count == 0), returns immediately.
 */
void screensaver_run(uint8_t *png_buf, size_t png_buf_size,
                     uint8_t *dtm1, uint8_t *dtm2, size_t fb_size);
