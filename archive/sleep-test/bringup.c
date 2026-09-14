// SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app.h"
#include "imu.h"
#include "keypin.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "awake";

#define POLL_MS         20
#define STATUS_EVERY_MS 5000
#define LONG_PRESS_MS   2000

static void status(bool pressed, int presses) {
    int ax = 0, ay = 0, az = 0;
    imu_accel_mg(&ax, &ay, &az);
    ESP_LOGI(TAG, "status up=%llds bat=%dmV btn=%s raw=%d acc=%d,%d,%dmg presses=%d",
             esp_timer_get_time() / 1000000, pressed ? -1 : keypin_battery_mv(),
             pressed ? "down" : "up", keypin_raw(), ax, ay, az, presses);
    if (!pressed) led_blink(1, 30);
}

void bringup_run(int *pattern) {
    ESP_LOGI(TAG, "awake mode: short press = next pattern, hold 2 s = back to sleep");
    bool stable = true, last = true;   // entered with the button still held
    int presses = 0;
    int64_t pressed_at = esp_timer_get_time() / 1000, next_status = 0;
    for (;;) {
        bool now = keypin_pressed();
        int64_t ms = esp_timer_get_time() / 1000;
        if (now == last && now != stable) {   // same reading twice in a row: debounced edge
            stable = now;
            if (stable) {
                pressed_at = ms;
                led_set(true);
            } else {
                led_set(false);
                int64_t held = ms - pressed_at;
                if (presses++ > 0) {   // the first release ends the hold that entered awake mode
                    ESP_LOGI(TAG, "button released after %lld ms", held);
                    if (held >= LONG_PRESS_MS) return;
                    draw_pattern(++*pattern);
                }
            }
        }
        last = now;
        if (ms >= next_status) {
            status(stable, presses);
            next_status = esp_timer_get_time() / 1000 + STATUS_EVERY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
