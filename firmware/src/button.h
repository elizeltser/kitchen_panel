#pragma once
#include <stdbool.h>

/** Initialise button GPIO and configure green button as wakeup source. */
int button_init(void);

/** Returns true if the device woke up due to green button press. */
bool button_wakeup_was_green(void);
