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

/**
 * Decode a grayscale PNG into two 1-bit planes for UC8179 4-gray mode.
 *
 * Luminance quantization (lum = rgba[0]):
 *   >= 192 → white      (dtm1=0, dtm2=0)
 *   >= 128 → light gray (dtm1=1, dtm2=0)
 *   >=  64 → dark gray  (dtm1=0, dtm2=1)
 *    < 64  → black      (dtm1=1, dtm2=1)
 *
 * @param dtm1  DTM1 plane output — (width * height / 8) bytes, MSB first.
 * @param dtm2  DTM2 plane output — same size.
 *
 * Returns 0 on success, -1 on error.
 */
int png_decode_4gray(const uint8_t *png_data, size_t png_len,
                     uint8_t *dtm1, uint8_t *dtm2,
                     int fb_width, int fb_height);
