#pragma once
#include <stdbool.h>

/**
 * Connect to Wi-Fi using WIFI_SSID / WIFI_PASSWORD from config.h.
 * Retries WIFI_RETRY_MAX times with WIFI_TIMEOUT_MS per attempt.
 * Returns 0 on success, negative errno on failure.
 */
int wifi_connect(void);

/** Returns true if the link is currently up. */
bool wifi_is_connected(void);
