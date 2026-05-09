#pragma once

/* ── Wi-Fi ─────────────────────────────────────────────────── */
#define WIFI_SSID            "Eli"
#define WIFI_PASSWORD        "1020304050"
#define WIFI_RETRY_MAX       5
#define WIFI_TIMEOUT_MS      15000

/* ── Server ─────────────────────────────────────────────────── */
/* Set SERVER_HOST to the Arch Linux machine's LAN IP           */
#define SERVER_HOST          "10.100.102.4"
#define SERVER_PORT          8080
#define SERVER_PATH          "/display.png"

/* ── NTP ────────────────────────────────────────────────────── */
#define NTP_SERVER           "il.pool.ntp.org"
#define TZ_POSIX_STRING      "IST-2IDT,M3.4.4/26,M10.5.0"

/* ── Active windows (24-hour local time) ───────────────────── */
#define MORNING_START_H      5
#define MORNING_START_M      45
#define MORNING_END_H        8
#define MORNING_END_M        0
#define EVENING_START_H      18
#define EVENING_START_M      0
#define EVENING_END_H        22
#define EVENING_END_M        0

/* ── Daily NTP sync time (no screen refresh) ────────────────── */
#define NTP_SYNC_H           0
#define NTP_SYNC_M           0

/* ── Refresh policy ─────────────────────────────────────────── */
#define MAX_PARTIAL_BEFORE_FULL   5
#define POLL_INTERVAL_ACTIVE_S    60
#define POLL_INTERVAL_INACTIVE_S  1800

/* ── GPIO ───────────────────────────────────────────────────── */
#define BUTTON_GREEN_PIN     3
#define BUTTON_LEFT_PIN      4
#define BUTTON_RIGHT_PIN     5
#define LED_PIN              6   /* active LOW */
#define BUZZER_PIN           45

/* ── Display geometry ───────────────────────────────────────── */
#define DISPLAY_WIDTH        800
#define DISPLAY_HEIGHT       480
#define FB_PLANE_SIZE        (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8)  /* 48000 bytes */
#define PNG_BUF_SIZE         (512 * 1024)

/* ── Screensaver ─────────────────────────────────────────────── */
#define SCREENSAVER_CYCLE_MS    30000   /* auto-advance every 30 s */
#define SCREENSAVER_ACTIVE_MS   300000  /* exit after 5 min */

/* ── ePaper SPI (hardware fixed — do not change) ────────────── */
#define EPAPER_CLK_PIN       7
#define EPAPER_MOSI_PIN      9
#define EPAPER_CS_PIN        10
#define EPAPER_DC_PIN        8
#define EPAPER_RST_PIN       47
#define EPAPER_BUSY_PIN      48
