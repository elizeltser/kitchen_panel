#include "nvs_store.h"

#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(nvs_store, LOG_LEVEL_INF);

#define KEY_ETAG   "dash/etag"
#define KEY_BUFIDX "dash/bufidx"
#define KEY_PCNT   "dash/partcnt"

int nvs_store_init(void)
{
	int ret = settings_subsys_init();
	if (ret) {
		LOG_ERR("settings_subsys_init failed: %d", ret);
	}
	return ret;
}

int nvs_store_etag_get(char *out, size_t maxlen)
{
	ssize_t n = settings_load_one(KEY_ETAG, out, maxlen - 1);
	if (n < 0) {
		out[0] = '\0';
		return 0;
	}
	out[n] = '\0';
	return 0;
}

int nvs_store_etag_set(const char *etag)
{
	return settings_save_one(KEY_ETAG, etag, strlen(etag));
}

int nvs_store_buf_index_get(uint8_t *out)
{
	ssize_t n = settings_load_one(KEY_BUFIDX, out, sizeof(*out));
	if (n < 0) {
		*out = 0;
	}
	return 0;
}

int nvs_store_buf_index_set(uint8_t idx)
{
	return settings_save_one(KEY_BUFIDX, &idx, sizeof(idx));
}

int nvs_store_partial_count_get(uint8_t *out)
{
	ssize_t n = settings_load_one(KEY_PCNT, out, sizeof(*out));
	if (n < 0) {
		*out = 0;
	}
	return 0;
}

int nvs_store_partial_count_set(uint8_t count)
{
	return settings_save_one(KEY_PCNT, &count, sizeof(count));
}
