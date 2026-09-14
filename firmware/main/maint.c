// SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app.h"
#include "keypin.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "maint";

void maint_run(void) {
    ESP_LOGI(TAG, "maintenance mode: USB stays up; hold the button 2 s to leave");
    // Wait for the hold that got us here to end, so it does not count as the exit.
    while (keypin_pressed()) vTaskDelay(pdMS_TO_TICKS(20));
    int64_t next_status = 0, down_at = -1;
    for (;;) {
        int64_t ms = esp_timer_get_time() / 1000;
        if (keypin_pressed()) {
            if (down_at < 0) down_at = ms;
            led_set(ms - down_at >= 2000);
        } else if (down_at >= 0) {
            bool leave = ms - down_at >= 2000;
            down_at = -1;
            led_set(false);
            if (leave) { ESP_LOGI(TAG, "leaving maintenance mode"); return; }
        }
        if (ms >= next_status) {
            app_status_line();
            next_status = ms + 5000;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
