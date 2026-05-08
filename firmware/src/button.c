#include "button.h"
#include "config.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device.h>
#include <zephyr/logging/log.h>
#include <esp_sleep.h>

LOG_MODULE_REGISTER(button, LOG_LEVEL_INF);

/* Board DTS aliases: sw0=GPIO3 (green), sw1=GPIO4 (left), sw2=GPIO5 (right) */
static const struct gpio_dt_spec btn_green =
	GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec btn_left =
	GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios);
static const struct gpio_dt_spec btn_right =
	GPIO_DT_SPEC_GET(DT_ALIAS(sw2), gpios);

/* Runtime interrupt state */
static K_SEM_DEFINE(btn_sem, 0, 1);
static volatile button_mask_t _last_pressed = BUTTON_NONE;
static int64_t _last_event_ms = 0;
#define DEBOUNCE_MS 50

static struct gpio_callback _btn_cb;

static void btn_isr(const struct device *dev, struct gpio_callback *cb,
                    uint32_t pins)
{
	int64_t now = k_uptime_get();
	if ((now - _last_event_ms) < DEBOUNCE_MS) {
		return;
	}
	_last_event_ms = now;

	if (pins & BIT(BUTTON_GREEN_PIN)) {
		_last_pressed = BUTTON_GREEN;
	} else if (pins & BIT(BUTTON_LEFT_PIN)) {
		_last_pressed = BUTTON_LEFT;
	} else if (pins & BIT(BUTTON_RIGHT_PIN)) {
		_last_pressed = BUTTON_RIGHT;
	}
	k_sem_give(&btn_sem);
}

int button_init(void)
{
	const struct gpio_dt_spec *btns[] = {&btn_green, &btn_left, &btn_right};
	const char *names[] = {"green", "left", "right"};

	for (int i = 0; i < 3; i++) {
		if (!gpio_is_ready_dt(btns[i])) {
			LOG_ERR("Button %s GPIO not ready", names[i]);
			return -ENODEV;
		}
		int ret = gpio_pin_configure_dt(btns[i], GPIO_INPUT);
		if (ret) {
			LOG_ERR("Button %s configure failed: %d", names[i], ret);
			return ret;
		}
		/* GPIO_INT_TRIG_WAKE_LOW registers the pin with
		 * esp_sleep_enable_ext1_wakeup_io() via the Zephyr GPIO driver,
		 * enabling EXT1 deep-sleep wakeup for this pin. */
		ret = gpio_pin_interrupt_configure_dt(btns[i], GPIO_INT_TRIG_WAKE_LOW);
		if (ret) {
			LOG_WRN("Button %s wakeup config failed: %d", names[i], ret);
		}
	}

	LOG_INF("Buttons initialised (green=GPIO%d, left=GPIO%d, right=GPIO%d)",
	        BUTTON_GREEN_PIN, BUTTON_LEFT_PIN, BUTTON_RIGHT_PIN);
	return 0;
}

button_mask_t button_get_wakeup_source(void)
{
	esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
	if (cause != ESP_SLEEP_WAKEUP_EXT1) {
		return BUTTON_NONE;
	}

	uint64_t pins = esp_sleep_get_ext1_wakeup_status();
	button_mask_t result = BUTTON_NONE;

	if (pins & BIT64(BUTTON_GREEN_PIN)) result |= BUTTON_GREEN;
	if (pins & BIT64(BUTTON_LEFT_PIN))  result |= BUTTON_LEFT;
	if (pins & BIT64(BUTTON_RIGHT_PIN)) result |= BUTTON_RIGHT;

	return result;
}

void button_enable_runtime_irq(void)
{
	const struct gpio_dt_spec *btns[] = {&btn_green, &btn_left, &btn_right};

	/* Switch from wakeup mode to standard falling-edge runtime interrupts */
	for (int i = 0; i < 3; i++) {
		gpio_pin_interrupt_configure_dt(btns[i], GPIO_INT_EDGE_FALLING);
	}

	uint32_t pin_mask = BIT(BUTTON_GREEN_PIN) | BIT(BUTTON_LEFT_PIN) |
	                    BIT(BUTTON_RIGHT_PIN);
	gpio_init_callback(&_btn_cb, btn_isr, pin_mask);
	gpio_add_callback(btn_green.port, &_btn_cb);

	k_sem_reset(&btn_sem);
	_last_event_ms = 0;
}

button_mask_t button_wait_any(int32_t timeout_ms)
{
	k_timeout_t to = (timeout_ms <= 0) ? K_FOREVER : K_MSEC(timeout_ms);
	_last_pressed = BUTTON_NONE;

	if (k_sem_take(&btn_sem, to) == 0) {
		return _last_pressed;
	}
	return BUTTON_NONE;
}
