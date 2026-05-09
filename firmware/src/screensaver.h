#pragma once
#include <stdint.h>
#include <stddef.h>

/**
 * Fetch one screensaver image from the server and display it.
 *
 * Requests GET /screensaver/count, then fetches
 * GET /screensaver/{index_hint % count} and drives the display.
 * Returns immediately after the single refresh.
 * Does nothing if the server reports count == 0.
 */
void screensaver_show_one(int index_hint, uint8_t *png_buf, size_t png_buf_size,
                          uint8_t *dtm1, uint8_t *dtm2, size_t fb_size);
