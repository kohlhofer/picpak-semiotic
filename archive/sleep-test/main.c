// PicPak sleep and wake test.
//
// The board spends its time in deep sleep and wakes on:
//   - the button:        draws the next test pattern (LED lit while held)
//   - an IMU double tap: 3 blinks, draws the next test pattern
//   - IMU motion:        2 blinks
//   - a 60 s timer:      nothing visible
// Holding the button 3 s after a wake enters awake mode (bringup.c), which keeps
// USB up for flashing and logs. A 2 s hold there goes back to sleeping.
//
// Every wake is recorded in RTC memory, which survives deep sleep, and printed
// once a USB host is attached, so a run on battery can be read back afterwards.
#include "app.h"
#include "board.h"
#include "epd.h"
#include "fb.h"
#include "imu.h"
#include "keypin.h"
#include "wake.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/usb_serial_jtag.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rtc_time.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "picpak";

#define TIMER_WAKE_S      60
#define AWAKE_HOLD_MS     3000
#define TAP_SETTLE_MS     600    // a double tap finishes after the first tap has already woken us
#define USB_WAIT_MS       2000
#define MAX_EVENTS        64

typedef struct {
    uint32_t t_s;        // RTC clock, seconds since power-on
    uint8_t  cause;      // wake_t
    uint8_t  wake_up_src, tap_src;
    uint16_t batt_mv;    // 0 when the button was held
    uint16_t held_ms;
} wake_event_t;

// RTC memory: kept through deep sleep, cleared by a reset or power loss.
RTC_DATA_ATTR static wake_event_t s_events[MAX_EVENTS];
RTC_DATA_ATTR static uint32_t s_count, s_printed;
RTC_DATA_ATTR static int s_pattern, s_int_pin;

static uint8_t s_fb[FB_BYTES];

void led_set(bool on) { gpio_set_level(PIN_LED, on ? 0 : 1); }

void led_blink(int n, int ms) {
    for (int i = 0; i < n; i++) {
        led_set(true);  vTaskDelay(pdMS_TO_TICKS(ms));
        led_set(false); vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

esp_err_t draw_pattern(int n) {
    const char *name = fb_pattern(s_fb, n);
    ESP_LOGI(TAG, "drawing pattern '%s'", name);
    led_set(true);
    esp_err_t err = epd_show(s_fb);
    led_set(false);
    if (err != ESP_OK) ESP_LOGE(TAG, "epd_show failed: %s", esp_err_to_name(err));
    return err;
}

static wake_src_t sleep_source(void) {
    switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_UNDEFINED: return WAKE_SRC_NONE;
    case ESP_SLEEP_WAKEUP_TIMER:     return WAKE_SRC_TIMER;
    case ESP_SLEEP_WAKEUP_GPIO:      return WAKE_SRC_GPIO;
    default:                         return WAKE_SRC_OTHER;
    }
}

static bool wait_for_usb(int timeout_ms) {
    for (int t = 0; t < timeout_ms && !usb_serial_jtag_is_connected(); t += 50) vTaskDelay(pdMS_TO_TICKS(50));
    if (!usb_serial_jtag_is_connected()) return false;
    vTaskDelay(pdMS_TO_TICKS(500));   // give the host time to reopen the port after re-enumeration
    return true;
}

static void print_event(uint32_t i) {
    const wake_event_t *e = &s_events[i % MAX_EVENTS];
    ESP_LOGI(TAG, "wake #%lu t=%lus cause=%s bat=%umV wake_up_src=0x%02X tap_src=0x%02X held=%ums",
             (unsigned long)i, (unsigned long)e->t_s, wake_name((wake_t)e->cause), e->batt_mv,
             e->wake_up_src, e->tap_src, e->held_ms);
}

static void print_pending(void) {
    if (s_count - s_printed > MAX_EVENTS) {
        ESP_LOGW(TAG, "%lu wake events overwritten", (unsigned long)(s_count - s_printed - MAX_EVENTS));
        s_printed = s_count - MAX_EVENTS;
    }
    for (; s_printed < s_count; s_printed++) print_event(s_printed);
}

static void sleep_now(bool imu_ok) {
    // A held button or a latched IMU interrupt would wake us straight back up.
    for (int t = 0; t < 10000 && keypin_pressed(); t += 20) vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t wu, tap;
    if (imu_ok) imu_read_sources(&wu, &tap);
    keypin_end();

    esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_BUTTON, ESP_GPIO_WAKEUP_GPIO_LOW);
    if (imu_ok && (s_int_pin == 1 || s_int_pin == 2))
        esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_IMU_INT, ESP_GPIO_WAKEUP_GPIO_HIGH);
    esp_sleep_enable_timer_wakeup(TIMER_WAKE_S * 1000000ULL);
    ESP_LOGI(TAG, "sleeping, imu_int=%d level=%d", s_int_pin, gpio_get_level(PIN_IMU_INT));
    esp_deep_sleep_start();
}

void app_main(void) {
    gpio_config(&(gpio_config_t){ .pin_bit_mask = 1ULL << PIN_LED, .mode = GPIO_MODE_OUTPUT });
    led_set(false);

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &(spi_bus_config_t){
        .mosi_io_num = PIN_SPI_MOSI, .miso_io_num = PIN_SPI_MISO, .sclk_io_num = PIN_SPI_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4096 }, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(keypin_begin());
    ESP_ERROR_CHECK(epd_begin());
    uint8_t who = 0;
    bool imu_ok = imu_begin(&who) == ESP_OK;

    wake_src_t src = sleep_source();
    uint64_t pins = src == WAKE_SRC_GPIO ? esp_sleep_get_gpio_wakeup_status() : 0;
    // Sample the button before the tap delay: a short press that lost the wake
    // race to the IMU is already released 600 ms later.
    bool button_down = keypin_pressed();
    if ((pins & (1ULL << PIN_IMU_INT)) && !(pins & (1ULL << PIN_BUTTON)) && !button_down)
        vTaskDelay(pdMS_TO_TICKS(TAP_SETTLE_MS));
    uint8_t wu = 0, tap = 0;
    if (imu_ok) imu_read_sources(&wu, &tap);
    wake_t cause = wake_classify(src, pins, tap, button_down);

    int mv = keypin_battery_mv();
    wake_event_t *e = &s_events[s_count % MAX_EVENTS];
    *e = (wake_event_t){ .t_s = esp_rtc_get_time_us() / 1000000, .cause = cause,
                         .wake_up_src = wu, .tap_src = tap, .batt_mv = mv < 0 ? 0 : mv };
    s_count++;

    switch (cause) {
    case WAKE_RESET:
        led_blink(3, 100);
        if (imu_ok) s_int_pin = imu_find_int_pin();
        wait_for_usb(USB_WAIT_MS);
        ESP_LOGI(TAG, "reset boot: imu who_am_i=0x%02X int pin on GPIO5=%d (1=INT1 2=INT2)", who, s_int_pin);
        draw_pattern(s_pattern);
        break;
    case WAKE_BUTTON: {
        int64_t t0 = esp_timer_get_time() / 1000, held_ms = 0;
        led_set(true);
        while (keypin_pressed() && held_ms < AWAKE_HOLD_MS) {
            vTaskDelay(pdMS_TO_TICKS(20));
            held_ms = esp_timer_get_time() / 1000 - t0;
        }
        led_set(false);
        e->held_ms = (uint16_t)held_ms;
        if (held_ms >= AWAKE_HOLD_MS) {
            led_blink(5, 60);
            wait_for_usb(10000);
            print_pending();
            bringup_run(&s_pattern);
        } else {
            draw_pattern(++s_pattern);
        }
        break;
    }
    case WAKE_DOUBLE_TAP:
        led_blink(3, 80);
        draw_pattern(++s_pattern);
        break;
    case WAKE_MOTION:
        led_blink(2, 80);
        break;
    default:
        break;
    }

    if (imu_ok && (s_int_pin == 1 || s_int_pin == 2)) imu_arm_wake(s_int_pin);
    if (wait_for_usb(cause == WAKE_TIMER || cause == WAKE_MOTION ? USB_WAIT_MS : 0)) print_pending();
    sleep_now(imu_ok);
}
