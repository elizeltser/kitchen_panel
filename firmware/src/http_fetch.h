#pragma once
#include <stddef.h>
#include <stdint.h>

#define HTTP_FETCH_OK            0
#define HTTP_FETCH_NOT_MODIFIED  1
#define HTTP_FETCH_ERR          -1

/**
 * Fetch /display.png from the server.
 *
 * @param etag_in   ETag from previous fetch, or NULL to skip If-None-Match.
 * @param buf       Buffer to receive PNG bytes.
 * @param buf_size  Size of buf.
 * @param bytes_out Actual bytes written to buf on HTTP_FETCH_OK.
 * @param etag_out  Buffer to store new ETag string (at least 64 bytes).
 *
 * Returns HTTP_FETCH_OK (new image), HTTP_FETCH_NOT_MODIFIED (304), or HTTP_FETCH_ERR.
 */
int http_fetch_display(const char *etag_in,
                       uint8_t *buf, size_t buf_size, size_t *bytes_out,
                       char *etag_out);
