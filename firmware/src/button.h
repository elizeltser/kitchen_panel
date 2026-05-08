#pragma once
#include <stdint.h>

typedef enum {
	BUTTON_NONE  = 0,
	BUTTON_GREEN = (1 << 0),
	BUTTON_LEFT  = (1 << 1),
	BUTTON_RIGHT = (1 << 2),
} button_mask_t;

/**
 * Configure GPIO3/4/5 as inputs and wakeup sources for deep sleep.
 * Call once at boot before pm_device_wakeup_enable().
 */
int button_init(void);

/**
 * After deep sleep wakeup, return which button(s) caused the wakeup.
 * Uses esp_sleep_get_ext1_wakeup_status() to read the GPIO pin bitmask.
 * Returns BUTTON_NONE if wakeup was timer-triggered (not GPIO).
 */
button_mask_t button_get_wakeup_source(void);

/**
 * Switch to runtime edge-falling interrupts for use while fully awake
 * (e.g. during screensaver). Must be called before button_wait_any().
 */
void button_enable_runtime_irq(void);

/**
 * Block until any button is pressed (runtime mode only).
 * @param timeout_ms  Max wait in ms; pass 0 to wait indefinitely.
 * Returns BUTTON_NONE on timeout.
 */
button_mask_t button_wait_any(int32_t timeout_ms);
