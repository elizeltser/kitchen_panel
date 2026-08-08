#pragma once
#include <stdint.h>

#define BOOT_LOG_ENTRIES 64

/* NTP result codes — also returned by ntp_get_last_src() */
#define NTP_SRC_SYNCED   0   /* fresh SNTP response */
#define NTP_SRC_FALLBACK 1   /* SNTP failed, used stored epoch */
#define NTP_SRC_NONE     2   /* SNTP failed, no stored epoch */

typedef struct {
	int64_t epoch;    /* UTC seconds at this boot         */
	int32_t sleep_s;  /* sleep duration scheduled          */
	uint8_t wakeup;   /* 0=timer 1=green 2=left 3=right   */
	uint8_t ntp_src;  /* NTP_SRC_*                         */
	uint8_t mode;     /* schedule_mode_t 0=active 1=ntp_sync 2=inactive */
	uint8_t _pad;
} boot_log_entry_t;   /* 16 bytes */

/** Load ring buffer from NVS. Call once after nvs_store_init(). */
void boot_log_init(void);

/** Dump all stored entries to the serial log. */
void boot_log_dump(void);

/** Append one entry, overwriting the oldest when full, then persist to NVS. */
void boot_log_append(const boot_log_entry_t *e);
