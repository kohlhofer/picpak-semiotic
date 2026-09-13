// GPIO2 carries both the button and the battery divider, so both are read
// through the ADC: near 0 V means pressed, otherwise it is the battery.
#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t keypin_begin(void);
int       keypin_raw(void);   // 0-4095, or -1 when the ADC read fails
unsigned  keypin_errors(void); // failed reads since boot
bool      keypin_pressed(void);
int       keypin_battery_mv(void);   // -1 while the button is held
void      keypin_end(void);          // releases the ADC and leaves the pin a digital input for deep-sleep wake
