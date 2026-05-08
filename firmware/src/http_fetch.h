#pragma once
#include <stddef.h>
#include <stdint.h>

#define HTTP_FETCH_OK            0
#define HTTP_FETCH_NOT_MODIFIED  1
#define HTTP_FETCH_NOT_FOUND     2
#define HTTP_FETCH_ERR          -1

/**
 * Fetch a binary resource (PNG) from the server.
 *
 * @param path      URL path, e.g. "/display.png" or "/screensaver/0".
 * @param etag_in   ETag from previous fetch, or NULL to skip If-None-Match.
 * @param buf       Buffer for response body.
 * @param buf_size  Size of buf.
 * @param bytes_out Bytes written on HTTP_FETCH_OK.
 * @param etag_out  New ETag (at least 64 bytes), or NULL to discard.
 *
 * Returns HTTP_FETCH_OK, HTTP_FETCH_NOT_MODIFIED, HTTP_FETCH_NOT_FOUND, or HTTP_FETCH_ERR.
 */
int http_fetch(const char *path,
               const char *etag_in,
               uint8_t *buf, size_t buf_size,
               size_t *bytes_out,
               char *etag_out);

/**
 * Fetch a small text/JSON response into a stack buffer.
 * Null-terminates buf on success. Does not use ETag.
 */
int http_fetch_text(const char *path, char *buf, size_t buf_size, size_t *bytes_out);
