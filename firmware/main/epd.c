// SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
// SPDX-FileCopyrightText: 2026 varanu5 (https://github.com/varanu5/picpak-tesserae-client)
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "epd.h"
#include "board.h"
#include "fb.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epd";
static spi_device_handle_t s_dev;

#define TRY(x) do { esp_err_t e_ = (x); if (e_ != ESP_OK) return e_; } while (0)

// Register setup for the panel's built-in waveform (full colour, slowest).
// Values from varanu5/picpak-tesserae-client, which traced them on the same
// hardware. 0xE7 and later are undocumented vendor registers.
typedef struct { uint8_t cmd, len, data[8]; } reg_t;
static const reg_t INIT[] = {
    { 0x00, 2, { 0x07, 0x29 } },               // panel setting
    { 0x01, 2, { 0x07, 0x00 } },               // power setting
    { 0x03, 3, { 0x10, 0x54, 0x44 } },         // power-off sequence
    { 0x06, 3, { 0xC0, 0xC0, 0xC0 } },         // booster soft start
    { 0x30, 1, { 0x08 } },                     // PLL
    { 0x41, 1, { 0x00 } },                     // temperature sensor: internal
    { 0x50, 1, { 0x37 } },                     // VCOM and data interval
    { 0x61, 4, { 0x01, 0x90, 0x01, 0x2C } },   // resolution 400x300
    { 0x65, 4, { 0x00, 0x00, 0x00, 0x00 } },   // gate/source start
    { 0xE3, 1, { 0x22 } },                     // power saving
    { 0xE7, 1, { 0x1C } },
    { 0xE9, 1, { 0x01 } },
    { 0xFF, 1, { 0xA5 } },
    { 0xEF, 8, { 0x01, 0x32, 0x08, 0x32, 0x0A, 0x32, 0x0F, 0x19 } },
    { 0xFD, 1, { 0x01 } },
    { 0xE8, 1, { 0x00 } },
    { 0xDF, 1, { 0x3C } },
    { 0xDC, 1, { 0x00 } },
    { 0xDD, 1, { 0x01 } },
    { 0xDE, 1, { 0x14 } },
    { 0xFF, 1, { 0xE3 } },
};

static void delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1); }

static esp_err_t wait_idle(uint32_t timeout_ms) {
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (gpio_get_level(PIN_EPD_BUSY) == 0) {
        if (esp_timer_get_time() > deadline) {
            ESP_LOGE(TAG, "BUSY stuck low after %lu ms", (unsigned long)timeout_ms);
            return ESP_ERR_TIMEOUT;
        }
        delay_ms(10);
    }
    return ESP_OK;
}

static esp_err_t spi_write(const uint8_t *buf, size_t n) {
    spi_transaction_t t = { .length = 8 * n, .tx_buffer = buf };
    return spi_device_polling_transmit(s_dev, &t);
}

static esp_err_t send(uint8_t cmd, const uint8_t *data, size_t n) {
    gpio_set_level(PIN_EPD_DC, 0);
    TRY(spi_write(&cmd, 1));
    if (n == 0) return ESP_OK;
    gpio_set_level(PIN_EPD_DC, 1);
    return spi_write(data, n);
}

static esp_err_t wake_and_init(void) {
    gpio_set_level(PIN_EPD_RST, 1); delay_ms(20);
    gpio_set_level(PIN_EPD_RST, 0); delay_ms(20);
    gpio_set_level(PIN_EPD_RST, 1); delay_ms(20);
    TRY(wait_idle(5000));
    for (size_t i = 0; i < sizeof INIT / sizeof INIT[0]; i++)
        TRY(send(INIT[i].cmd, INIT[i].data, INIT[i].len));
    return ESP_OK;
}

esp_err_t epd_begin(void) {
    TRY(gpio_config(&(gpio_config_t){
        .pin_bit_mask = (1ULL << PIN_EPD_DC) | (1ULL << PIN_EPD_RST), .mode = GPIO_MODE_OUTPUT }));
    TRY(gpio_config(&(gpio_config_t){ .pin_bit_mask = 1ULL << PIN_EPD_BUSY, .mode = GPIO_MODE_INPUT }));
    gpio_set_level(PIN_EPD_RST, 1);
    return spi_bus_add_device(SPI2_HOST, &(spi_device_interface_config_t){
        .spics_io_num = PIN_EPD_CS, .clock_speed_hz = 1000000, .mode = 0, .queue_size = 1 }, &s_dev);
}

static esp_err_t show(const uint8_t *fb) {
    TRY(wake_and_init());

    gpio_set_level(PIN_EPD_DC, 0);
    TRY(spi_write((const uint8_t[]){ 0x10 }, 1));   // pixel data
    gpio_set_level(PIN_EPD_DC, 1);
    for (int i = 0; i < FB_H; i++) TRY(spi_write(fb_panel_row(fb, i), FB_ROW));

    TRY(send(0x04, NULL, 0));                        // power on
    delay_ms(20);
    TRY(wait_idle(10000));
    TRY(send(0x12, (const uint8_t[]){ 0x00 }, 1));   // refresh
    delay_ms(20);
    TRY(wait_idle(60000));
    TRY(send(0x02, (const uint8_t[]){ 0x00 }, 1));   // power off
    delay_ms(20);
    TRY(wait_idle(10000));
    return send(0x07, (const uint8_t[]){ 0xA5 }, 1);   // deep sleep; 0xA5 is the required check code
}

esp_err_t epd_show(const uint8_t *fb) {
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = show(fb);
    if (err != ESP_OK) {
        // Never leave the panel's drive voltages up after a failure: reset it and
        // send power off and deep sleep without waiting on BUSY.
        gpio_set_level(PIN_EPD_RST, 0); delay_ms(20);
        gpio_set_level(PIN_EPD_RST, 1); delay_ms(20);
        send(0x02, (const uint8_t[]){ 0x00 }, 1);
        delay_ms(100);
        send(0x07, (const uint8_t[]){ 0xA5 }, 1);
        return err;
    }
    ESP_LOGI(TAG, "refresh took %lld ms", (esp_timer_get_time() - t0) / 1000);
    return ESP_OK;
}
