#include "button.h"
#include "config.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/pm.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(button, LOG_LEVEL_INF);

/* sw0 alias = refresh_key0 = GPIO3, defined in board DTS */
static const struct gpio_dt_spec btn_green =
	GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

int button_init(void)
{
	if (!gpio_is_ready_dt(&btn_green)) {
		LOG_ERR("Green button GPIO not ready");
		return -ENODEV;
	}

	int ret = gpio_pin_configure_dt(&btn_green, GPIO_INPUT);
	if (ret) {
		LOG_ERR("Button configure failed: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&btn_green, GPIO_INT_EDGE_FALLING);
	if (ret) {
		LOG_WRN("Wakeup interrupt configure failed: %d", ret);
	}

	LOG_INF("Green button initialised");
	return 0;
}

bool button_wakeup_was_green(void)
{
	/* Zephyr 4.x: pm_state_next_get returns a pointer, not via out-params */
	const struct pm_state_info *info = pm_state_next_get(0);
	if (!info) {
		return false;
	}
	return (gpio_pin_get_dt(&btn_green) == 0);
}
