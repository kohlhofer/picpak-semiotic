// SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "keypin.h"
#include "board.h"

#include <stdlib.h>
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// A full cell puts about 2.9 V on the pin, an empty one about 2.3 V, and a
// press pulls it to ground. Anything under this raw value counts as pressed.
#define PRESSED_RAW_MAX 400

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;

esp_err_t keypin_begin(void) {
    esp_err_t err = adc_oneshot_new_unit(&(adc_oneshot_unit_init_cfg_t){ .unit_id = ADC_UNIT_1 }, &s_adc);
    if (err != ESP_OK) return err;
    err = adc_oneshot_config_channel(s_adc, BATT_ADC_CHAN,
        &(adc_oneshot_chan_cfg_t){ .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 });
    if (err != ESP_OK) return err;
    return adc_cali_create_scheme_curve_fitting(&(adc_cali_curve_fitting_config_t){
        .unit_id = ADC_UNIT_1, .chan = BATT_ADC_CHAN, .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12 }, &s_cali);
}

static unsigned s_errors;
static bool s_last_pressed;

int keypin_raw(void) {
    int raw = 0;
    if (adc_oneshot_read(s_adc, BATT_ADC_CHAN, &raw) != ESP_OK) { s_errors++; return -1; }
    return raw;
}

unsigned keypin_errors(void) { return s_errors; }

// A failed read keeps the previous state. Reading a failure as 0 once made a
// press appear out of nothing while Wi-Fi was up.
bool keypin_pressed(void) {
    int raw = keypin_raw();
    if (raw >= 0) s_last_pressed = raw < PRESSED_RAW_MAX;
    return s_last_pressed;
}

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

int keypin_battery_mv(void) {
    enum { N = 15 };
    int s[N], n = 0;
    for (int i = 0; i < N; i++) {
        int raw = keypin_raw();
        if (raw >= 0) s[n++] = raw;
    }
    if (n < N / 2) return -1;
    qsort(s, n, sizeof s[0], cmp_int);
    if (s[n / 2] < PRESSED_RAW_MAX) return -1;
    int pin_mv = 0;
    if (adc_cali_raw_to_voltage(s_cali, s[n / 2], &pin_mv) != ESP_OK) return -1;
    return (int)(pin_mv * BATT_DIVIDER);
}

void keypin_end(void) {
    adc_cali_delete_scheme_curve_fitting(s_cali);
    adc_oneshot_del_unit(s_adc);
    // The divider holds the pin high, so no pull is needed or wanted.
    gpio_config(&(gpio_config_t){ .pin_bit_mask = 1ULL << PIN_BUTTON, .mode = GPIO_MODE_INPUT });
}
