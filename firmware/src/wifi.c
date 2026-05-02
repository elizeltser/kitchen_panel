#include "wifi.h"
#include "config.h"

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(wifi_mgr, LOG_LEVEL_INF);

static K_SEM_DEFINE(wifi_sem, 0, 1);
static bool connected = false;
static struct net_mgmt_event_callback wifi_cb;

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
                                uint64_t event, struct net_if *iface)
{
	if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
		struct wifi_status *st = (struct wifi_status *)cb->info;
		connected = (st->status == 0);
		if (connected) {
			LOG_INF("Wi-Fi connected");
		} else {
			LOG_WRN("Wi-Fi connect failed: %s (%d)",
			        wifi_conn_status_txt(st->status), st->status);
		}
		k_sem_give(&wifi_sem);
	} else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		connected = false;
		LOG_INF("Wi-Fi disconnected");
	}
}

int wifi_connect(void)
{
	struct net_if *iface = net_if_get_default();
	if (!iface) {
		return -ENODEV;
	}

	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler,
		NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_cb);

	struct wifi_connect_req_params params = {
		.ssid        = (uint8_t *)WIFI_SSID,
		.ssid_length = strlen(WIFI_SSID),
		.psk         = (uint8_t *)WIFI_PASSWORD,
		.psk_length  = strlen(WIFI_PASSWORD),
		.security    = WIFI_SECURITY_TYPE_PSK,
		.mfp         = WIFI_MFP_OPTIONAL,
		.channel     = WIFI_CHANNEL_ANY,
		.band        = WIFI_FREQ_BAND_UNKNOWN,
	};

	for (int attempt = 0; attempt < WIFI_RETRY_MAX; attempt++) {
		LOG_INF("Wi-Fi connect attempt %d/%d to \"%s\"",
		        attempt + 1, WIFI_RETRY_MAX, WIFI_SSID);
		k_sem_reset(&wifi_sem);
		connected = false;

		/* Disconnect any stale association before retrying */
		if (attempt > 0) {
			net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
			k_sleep(K_SECONDS(2));
		}

		int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface,
		                   &params, sizeof(params));
		if (ret && ret != -EALREADY) {
			LOG_WRN("Connect request error: %d", ret);
			k_sleep(K_SECONDS(2));
			continue;
		}

		if (k_sem_take(&wifi_sem, K_MSEC(WIFI_TIMEOUT_MS)) == 0 && connected) {
			return 0;
		}
		LOG_WRN("Timeout on attempt %d", attempt + 1);
	}

	LOG_ERR("Wi-Fi failed after %d attempts", WIFI_RETRY_MAX);
	return -ETIMEDOUT;
}

bool wifi_is_connected(void)
{
	return connected;
}
