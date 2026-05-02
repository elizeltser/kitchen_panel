#pragma once
#include <stdint.h>
#include <stddef.h>

/** Initialise ePaper display and SPI peripheral. */
int epaper_init(void);

/** Send a full-refresh (3s, flickers). */
int epaper_full_refresh(const uint8_t *fb, size_t fb_len);

/** Send a fast-refresh (~1.5s, single flash). */
int epaper_fast_refresh(const uint8_t *fb, size_t fb_len);

/**
 * Partial refresh — only redraws a rectangular region.
 * @param fb       Full framebuffer (800×480 packed 1-bit).
 * @param x, y     Top-left corner of region.
 * @param w, h     Width and height of region.
 */
int epaper_partial_refresh(const uint8_t *fb, int x, int y, int w, int h);

/** Power off the panel (keeps image). Call before PM sleep. */
void epaper_power_off(void);

/** Display a plain-text error message (uses built-in font). */
void epaper_show_error(const char *msg);

/** Initialize UC8179 for 4-gray (OTP waveform + TSSET trick). Call after epaper_init(). */
int epaper_4gray_init(void);

/** Full 4-gray refresh. dtm1/dtm2 are the two 48000-byte plane buffers. */
int epaper_4gray_refresh(const uint8_t *dtm1, const uint8_t *dtm2, size_t plane_len);
