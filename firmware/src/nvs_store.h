#pragma once
#include <stdint.h>
#include <stddef.h>

/** Initialize settings subsystem (call once at boot before any get/set). */
int nvs_store_init(void);

/** ETag string (up to 63 chars). Returns "" if not set. */
int nvs_store_etag_get(char *out, size_t maxlen);
int nvs_store_etag_set(const char *etag);

/** Display buffer index (0 = main /display.png). */
int nvs_store_buf_index_get(uint8_t *out);
int nvs_store_buf_index_set(uint8_t idx);

/** Consecutive partial/fast refresh counter — reset to 0 after forced full. */
int nvs_store_partial_count_get(uint8_t *out);
int nvs_store_partial_count_set(uint8_t count);

/** Non-zero when the last display drawn was a screensaver. */
int nvs_store_ss_last_get(uint8_t *out);
int nvs_store_ss_last_set(uint8_t val);
