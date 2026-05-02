#pragma once
#include <stddef.h>
#include <stdint.h>

/**
 * Decode a PNG from memory into a 1-bit framebuffer.
 *
 * @param png_data   Pointer to PNG bytes in memory.
 * @param png_len    Size of PNG data.
 * @param fb         Output framebuffer — 1 bit per pixel, packed, MSB first.
 *                   Must be at least (width * height / 8) bytes.
 * @param fb_width   Expected image width in pixels (800).
 * @param fb_height  Expected image height in pixels (480).
 *
 * Returns 0 on success, -1 on error.
 */
int png_decode(const uint8_t *png_data, size_t png_len,
               uint8_t *fb, int fb_width, int fb_height);
