// PicPak firmware: modes stepped with the one button. Mode 1 is the
// environmental panel for the configured place; mode 2 is System Updates (RSS headlines);
// mode 3 is System Status, the board reporting on itself; mode 4 is Orbital
// Tracking (ISS passes and the nearest asteroid); mode 5 is the Crew Manifest.
//
// The board lives in deep sleep and wakes on the button or the hourly timer.
//   - Button: every press moves the target mode on, wrapping after the last.
//     Drawing starts 1.2 s after the last press. Presses during a refresh
//     count, and the panel draws wherever the count got to once it is free.
//   - Timer: fetch the forecast and headlines (and every 6 h the orbit data),
//     then redraw whatever is on screen. With mode 4 showing, also wake ten
//     minutes before a visible ISS pass and again just after it.
//   - Button held 3 s: maintenance mode, which keeps USB up for flashing.
// The forecast, the mode on screen and a short wake log live in RTC memory.
#include "app.h"
#include "app_config.h"
#include "batt.h"
#include "board.h"
#include "epd.h"
#include "fb.h"
#include "imu.h"
#include "keypin.h"
#include "net.h"
#include "news.h"
#include "orbit.h"
#include "sched.h"
#include "screen.h"
#include "status.h"
#include "wake.h"
#include "wx.h"

#include <ctype.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/temperature_sensor.h"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "picpak";

#define MODES          5
#define SETTLE_MS      1200
#define MAINT_HOLD_MS  3000
#define RETRY_S        600
#define MAX_AWAKE_MS   180000
#define EVENTS         32

typedef enum { FETCH_NONE, FETCH_OK, FETCH_NET_FAILED, FETCH_BAD_DATA } fetch_result_t;
typedef struct { uint32_t t; uint8_t cause, presses, shown, fetch; int16_t mv; } event_t;

// RTC memory: kept through deep sleep, cleared by a reset or power loss.
RTC_DATA_ATTR static wx_t s_wx;
RTC_DATA_ATTR static news_t s_news;
RTC_DATA_ATTR static int64_t s_sync_utc, s_next_fetch;
RTC_DATA_ATTR static uint8_t s_shown;
RTC_DATA_ATTR static event_t s_events[EVENTS];
RTC_DATA_ATTR static uint32_t s_count, s_printed;
// For System Status.
RTC_DATA_ATTR static int64_t s_news_sync, s_flashed;
RTC_DATA_ATTR static uint8_t s_fetch_failures;
RTC_DATA_ATTR static bool s_news_failed;
RTC_DATA_ATTR static int s_rssi, s_link_ms;
RTC_DATA_ATTR static uint32_t s_app_bytes, s_app_part_bytes;
RTC_DATA_ATTR static batt_sample_t s_trend[STATUS_TREND_SAMPLES];
RTC_DATA_ATTR static uint32_t s_trend_count;
// For Orbital Tracking.
RTC_DATA_ATTR static orbit_t s_orbit;
RTC_DATA_ATTR static int64_t s_alert_at;

static const site_t SITE = { PLACE_LAT, PLACE_LON, PLACE_ELEV_M / 1000.0 };
static const crew_t CREW = CREW_CONFIG;

static uint8_t s_fb[FB_BYTES];
static char s_body[49152];   // responses up to 48 KB; the default BBC World feed is about 24 KB
static int s_batt_mv = -1;
static wake_t s_wake;
static bool s_imu_ok;

// One worker task runs the slow jobs so the button keeps being read.
typedef enum { JOB_FETCH, JOB_DRAW } job_kind_t;
typedef struct { job_kind_t kind; uint8_t mode; } job_t;
static QueueHandle_t s_jobs;
static volatile bool s_busy;
static volatile uint8_t s_fetch_result;

void led_set(bool on) { gpio_set_level(PIN_LED, on ? 0 : 1); }

void led_blink(int n, int ms) {
    for (int i = 0; i < n; i++) {
        led_set(true);  vTaskDelay(pdMS_TO_TICKS(ms));
        led_set(false); vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

void app_status_line(void) {
    int64_t now = time(NULL);
    bool clock = sched_time_valid(now);
    ESP_LOGI(TAG, "status up=%llds bat=%dmV (%d%%) shown=%d clock=%s rows=%d sync_age=%llds next_fetch_in=%llds adc_errors=%u",
             esp_timer_get_time() / 1000000, s_batt_mv, batt_pct(s_batt_mv), s_shown + 1, clock ? "set" : "unset",
             s_wx.count, s_sync_utc && clock ? now - s_sync_utc : -1LL, clock ? s_next_fetch - now : -1LL, keypin_errors());
}

// When this build was first seen with a set clock. NVS keeps it per ELF hash,
// so a reset keeps the date and a new flash replaces it.
static void note_flash(void) {
    if (s_flashed) return;
    char sha[17];
    esp_app_get_elf_sha256(sha, sizeof sha);
    nvs_handle_t h;
    if (nvs_open("status", NVS_READWRITE, &h) != ESP_OK) return;
    char stored[17] = "";
    size_t n = sizeof stored;
    int64_t t = 0, now = time(NULL);
    if (nvs_get_str(h, "elf", stored, &n) == ESP_OK && strcmp(stored, sha) == 0 && nvs_get_i64(h, "flashed", &t) == ESP_OK) {
        s_flashed = t;
    } else if (sched_time_valid(now) && nvs_set_str(h, "elf", sha) == ESP_OK && nvs_set_i64(h, "flashed", now) == ESP_OK &&
               nvs_commit(h) == ESP_OK) {
        s_flashed = now;
        ESP_LOGI(TAG, "new build %s", sha);
    }
    nvs_close(h);
}

static void yield_tick(void) { vTaskDelay(1); }

// ISS elements and the nearest asteroid every six hours, and the passes they
// give. Each keeps its last good value when a fetch fails.
static void fetch_orbit(int64_t now) {
    size_t len = 0;
    int64_t date = 0;
    bool fresh_tle = false;
    if (!s_orbit.l1[0] || now - s_orbit.tle_sync >= ORBIT_REFRESH_S) {
        char l1[70], l2[70];
        esp_err_t err = net_fetch(TLE_URL, s_body, sizeof s_body, &len, &date);
        if (err != ESP_OK) ESP_LOGE(TAG, "elements fetch failed: %s", esp_err_to_name(err));
        else if (!orbit_parse_tle(s_body, l1, l2)) ESP_LOGE(TAG, "no elements in %u bytes", (unsigned)len);
        else {
            memcpy(s_orbit.l1, l1, sizeof l1);
            memcpy(s_orbit.l2, l2, sizeof l2);
            s_orbit.tle_sync = now;
            fresh_tle = true;
        }
    }
    if (!s_orbit.neo_ok || now - s_orbit.neo_sync >= ORBIT_REFRESH_S || s_orbit.neo.approach < now) {
        neo_t neo;
        esp_err_t err = net_fetch(NEO_URL, s_body, sizeof s_body, &len, &date);
        if (err != ESP_OK) ESP_LOGE(TAG, "close-approach fetch failed: %s", esp_err_to_name(err));
        else if (!orbit_parse_neo(s_body, len, &neo)) ESP_LOGE(TAG, "close-approach data did not parse (%u bytes)", (unsigned)len);
        else {
            s_orbit.neo = neo;
            s_orbit.neo_ok = true;
            s_orbit.neo_sync = now;
            ESP_LOGI(TAG, "nearest asteroid %s at %.4f AU", neo.des, neo.dist_au);
        }
    }
    net_stop();

    sgp4_t sat;
    // Search again every six hours, or once the passes found have all gone by.
    bool stale = !s_orbit.searched_at || now - s_orbit.searched_at >= ORBIT_REFRESH_S ||
                 (s_orbit.count && !orbit_next_pass(&s_orbit, now));
    if ((fresh_tle || stale) && s_orbit.l1[0] && sgp4_parse(s_orbit.l1, s_orbit.l2, &sat)) {
        // Seconds of soft-float work on one core: let the button loop and the
        // idle task (which feeds the watchdog) run in between.
        pass_set_yield(yield_tick);
        static pass_t found[ORBIT_PASSES];
        int64_t t0 = esp_timer_get_time();
        int n = pass_find(&sat, &SITE, now, now + 3 * 86400, PASS_MIN_EL, true, found, ORBIT_PASSES);
        memcpy(s_orbit.pass, found, sizeof found);
        s_orbit.count = (uint8_t)n;
        s_orbit.searched_at = now;
        ESP_LOGI(TAG, "%d visible passes, next in %llds, found in %lld ms", n, n ? found[0].rise - now : -1LL,
                 (esp_timer_get_time() - t0) / 1000);
    }
}

static void do_fetch(void) {
    int64_t started = esp_timer_get_time();
    size_t len = 0;
    int64_t date = 0;
    // One Wi-Fi session for the forecast, the headlines and the orbit data;
    // fetch_orbit turns the radio off before its long calculation.
    char url[512];
    wx_url(url, sizeof url, PLACE_LAT, PLACE_LON);
    esp_err_t err = net_fetch(url, s_body, sizeof s_body, &len, &date);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "fetch failed: %s", esp_err_to_name(err));
        s_fetch_result = FETCH_NET_FAILED;
    } else {
        if (date) settimeofday(&(struct timeval){ .tv_sec = (time_t)date }, NULL);
        static wx_t parsed;
        if (wx_parse(s_body, len, &parsed)) {
            s_wx = parsed;
            s_sync_utc = time(NULL);
            s_fetch_result = FETCH_OK;
            ESP_LOGI(TAG, "forecast: %u bytes, %d rows, %.1f F now", (unsigned)len, parsed.count, parsed.h[0].temp_f);
        } else {
            s_fetch_result = FETCH_BAD_DATA;
            ESP_LOGE(TAG, "forecast did not parse (%u bytes)", (unsigned)len);
        }
    }
    // Headlines ride on the same wake. A failure keeps the old ones and does
    // not change the schedule, which the forecast drives.
    if (s_fetch_result == FETCH_OK) {
        err = net_fetch(NEWS_URL, s_body, sizeof s_body, &len, &date);
        static news_t parsed_news;
        s_news_failed = true;
        if (err != ESP_OK) ESP_LOGE(TAG, "news fetch failed: %s", esp_err_to_name(err));
        else if (!news_parse(s_body, len, &parsed_news)) ESP_LOGE(TAG, "news did not parse (%u bytes)", (unsigned)len);
        else {
            s_news = parsed_news;
            s_news_sync = time(NULL);
            s_news_failed = false;
            ESP_LOGI(TAG, "news: %u bytes, %d headlines", (unsigned)len, parsed_news.count);
        }
    }
    int64_t now = time(NULL);
    if (s_fetch_result == FETCH_OK) {
        s_fetch_failures = 0;
        s_link_ms = (int)((esp_timer_get_time() - started) / 1000);
        status_trend_add(s_trend, STATUS_TREND_SAMPLES, &s_trend_count, now, s_batt_mv);
        note_flash();
        fetch_orbit(now);
    } else if (s_fetch_failures < 255) {
        s_fetch_failures++;
    }
    net_stop();   // already off after fetch_orbit; this covers a failed forecast
    s_rssi = net_rssi();
    s_next_fetch = s_fetch_result == FETCH_OK ? sched_next_fetch(now) : now + RETRY_S;
}

static const char *reset_name(esp_reset_reason_t r) {
    switch (r) {
    case ESP_RST_POWERON:   return "POWER";
    case ESP_RST_EXT:       return "PIN";
    case ESP_RST_SW:        return "SOFT";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "SLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_USB:       return "USB";
    case ESP_RST_JTAG:      return "JTAG";
    default:                return "OTHER";
    }
}

static void read_status(status_t *st) {
    int64_t now = time(NULL);
    *st = (status_t){ .batt_mv = s_batt_mv, .now = sched_time_valid(now) ? now : 0, .utc_offset = s_wx.utc_offset,
                      .wx_sync = s_sync_utc, .news_sync = s_news_sync, .next_fetch = s_next_fetch,
                      .fetch_failures = s_fetch_failures, .news_failed = s_news_failed, .rssi = s_rssi,
                      .link_ms = s_link_ms };
    if (st->now) status_trend(s_trend, STATUS_TREND_SAMPLES, s_trend_count, now, s_batt_mv, &st->trend_mv, &st->trend_hours);

    temperature_sensor_handle_t ts = NULL;
    temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&tcfg, &ts) == ESP_OK) {
        if (temperature_sensor_enable(ts) == ESP_OK) {
            st->core_ok = temperature_sensor_get_celsius(ts, &st->core_c) == ESP_OK;
            temperature_sensor_disable(ts);
        }
        temperature_sensor_uninstall(ts);
    }

    // The image size only changes with a flash, which also clears RTC memory.
    if (!s_app_bytes) {
        const esp_partition_t *p = esp_ota_get_running_partition();
        esp_image_metadata_t md;
        if (p && esp_image_get_metadata(&(esp_partition_pos_t){ .offset = p->address, .size = p->size }, &md) == ESP_OK) {
            s_app_bytes = md.image_len;
            s_app_part_bytes = p->size;
        }
    }
    st->app_bytes = s_app_bytes;
    st->app_part_bytes = s_app_part_bytes;

    strlcpy(st->build, esp_app_get_description()->version, sizeof st->build);
    note_flash();
    st->flashed = s_flashed;
    st->accel_ok = s_imu_ok && imu_sample_mg(&st->accel_mg[0], &st->accel_mg[1], &st->accel_mg[2]) == ESP_OK;
}

static void do_draw(uint8_t mode) {
    screen_ctx_t ctx = { .mode = mode, .modes = MODES, .batt_pct = batt_pct(s_batt_mv), .sync_utc = s_sync_utc,
                         .place = PLACE_NAME, .feed = NEWS_LABEL };
    if (mode == 0) screen_env(s_fb, &s_wx, &ctx);
    else if (mode == 1) screen_news(s_fb, &s_news, (int64_t)time(NULL), s_wx.utc_offset, &ctx);
    else if (mode == 2) {
        static status_t st;
        read_status(&st);
        uint8_t mac[6] = { 0 };
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char device[16], wake[16];
        snprintf(device, sizeof device, "PICPAK-%02X%02X", mac[4], mac[5]);
        strlcpy(wake, wake_name(s_wake), sizeof wake);
        for (char *c = wake; *c; c++) *c = (char)toupper((unsigned char)*c);
        screen_status(s_fb, &st, device, wake, reset_name(esp_reset_reason()), &ctx);
    } else if (mode == 3) screen_orbit(s_fb, &s_orbit, &SITE, (int64_t)time(NULL), s_wx.utc_offset, &ctx);
    else if (mode == 4) screen_crew(s_fb, &CREW, (int64_t)time(NULL), s_wx.utc_offset, &ctx);
    else screen_placeholder(s_fb, &ctx);
    ESP_LOGI(TAG, "drawing mode %d", mode + 1);
    esp_err_t err = epd_show(s_fb);
    if (err == ESP_OK) s_shown = mode;
    else ESP_LOGE(TAG, "panel refresh failed: %s", esp_err_to_name(err));
}

static void worker(void *arg) {
    job_t j;
    for (;;) {
        if (xQueueReceive(s_jobs, &j, portMAX_DELAY) != pdTRUE) continue;
        if (j.kind == JOB_FETCH) do_fetch(); else do_draw(j.mode);
        s_busy = false;
    }
}

static void run(job_kind_t kind, uint8_t mode) {
    s_busy = true;
    job_t j = { kind, mode };
    xQueueSend(s_jobs, &j, portMAX_DELAY);
}

// Debounced button: same reading twice in a row, 20 ms apart.
typedef struct { bool stable, last; int64_t down_at; } btn_t;
static bool btn_pressed_edge(btn_t *b, int64_t ms) {
    bool raw = keypin_pressed(), edge = false;
    if (raw == b->last && raw != b->stable) {
        b->stable = raw;
        if (raw) { b->down_at = ms; edge = true; }
    }
    b->last = raw;
    return edge;
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
    vTaskDelay(pdMS_TO_TICKS(500));
    return true;
}

static void print_events(void) {
    if (s_count - s_printed > EVENTS) s_printed = s_count - EVENTS;
    static const char *FETCH[] = { "-", "ok", "net-failed", "bad-data" };
    for (; s_printed < s_count; s_printed++) {
        const event_t *e = &s_events[s_printed % EVENTS];
        ESP_LOGI(TAG, "wake #%lu t=%lu cause=%s presses=%u shown=%u fetch=%s bat=%dmV", (unsigned long)s_printed,
                 (unsigned long)e->t, wake_name((wake_t)e->cause), e->presses, e->shown + 1, FETCH[e->fetch % 4], e->mv);
    }
}

static void sleep_now(void) {
    int64_t now = time(NULL), wake_at = s_next_fetch;
    s_alert_at = 0;
    const pass_t *p = s_shown == 3 && sched_time_valid(now) ? orbit_next_pass(&s_orbit, now) : NULL;
    if (p) {
        // Ten minutes before a pass to show it as imminent, then just after it
        // ends to move on to the next one.
        int64_t t = now < p->rise - 720 ? p->rise - 600 : p->set + 60;
        if (t > now + 30 && t < wake_at) wake_at = s_alert_at = t;
    }
    int64_t secs = wake_at - now;
    if (secs < 30) secs = 30;
    if (secs > 2 * 3600) secs = 2 * 3600;
    keypin_end();
    esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_BUTTON, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL);
    ESP_LOGI(TAG, "sleeping %llds with mode %d on screen", secs, s_shown + 1);
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
    s_imu_ok = imu_begin(&who) == ESP_OK;
    if (s_imu_ok) imu_power_down();   // off between readings; 150 µA when left running
    else ESP_LOGW(TAG, "IMU not found (WHO_AM_I 0x%02X)", who);

    esp_err_t nv = nvs_flash_init();   // Wi-Fi keeps calibration here
    if (nv == ESP_ERR_NVS_NO_FREE_PAGES || nv == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nv = nvs_flash_init();
    }
    if (nv != ESP_OK) ESP_LOGE(TAG, "nvs: %s", esp_err_to_name(nv));

    wake_src_t src = sleep_source();
    uint64_t pins = src == WAKE_SRC_GPIO ? esp_sleep_get_gpio_wakeup_status() : 0;
    bool down = keypin_pressed();
    wake_t cause = wake_classify(src, pins, 0, down);
    s_wake = cause;
    int mv = keypin_battery_mv();
    if (mv > 0) s_batt_mv = mv;

    s_jobs = xQueueCreate(2, sizeof(job_t));
    xTaskCreate(worker, "worker", 10240, NULL, 5, NULL);

    int64_t now = time(NULL);
    int64_t ms = esp_timer_get_time() / 1000;
    // A timer wake can land a moment before the scheduled second; 20 s of slack
    // avoids a second wake just to fetch. After a failure the retry time holds
    // even with no forecast or clock yet (the unset clock still counts up
    // through sleep), so presses away from known Wi-Fi don't each wait out a
    // join timeout.
    bool fetch = cause == WAKE_RESET || now >= s_next_fetch - 20;
    // The RTC runs a few seconds an hour fast, so an alert wake can come early.
    bool redraw = cause == WAKE_RESET || (cause == WAKE_TIMER && s_alert_at && now >= s_alert_at - 120 && s_shown == 3);
    int pending = cause == WAKE_BUTTON ? 1 : 0;
    int presses = pending;
    int64_t last_input = pending ? ms : -SETTLE_MS;
    int64_t led_off_at = 0;
    btn_t b = { .stable = down, .last = down, .down_at = ms };
    bool was_busy = false;
    job_kind_t last_kind = JOB_DRAW;
    s_fetch_result = FETCH_NONE;
    uint8_t fetched = FETCH_NONE;
    ESP_LOGI(TAG, "wake: %s, mode %d on screen, fetch %s", wake_name(cause), s_shown + 1, fetch ? "due" : "not due");
    if (strcmp(WIFI_SSID, "your-network") == 0) ESP_LOGE(TAG, "config.h still has the example Wi-Fi network; nothing will be fetched");
    if (pending) { led_set(true); led_off_at = ms + 60; }

    for (;;) {
        ms = esp_timer_get_time() / 1000;
        if (btn_pressed_edge(&b, ms)) {
            pending++;
            presses++;
            last_input = ms;
            led_set(true);
            led_off_at = ms + 60;
        }
        if (led_off_at && ms >= led_off_at) { led_set(false); led_off_at = 0; }

        if (b.stable && ms - b.down_at >= MAINT_HOLD_MS && !s_busy) {
            if (pending > 0) pending--;   // the hold is not a mode step
            led_blink(5, 60);
            wait_for_usb(10000);
            print_events();
            maint_run();
            b = (btn_t){ 0 };
            last_input = esp_timer_get_time() / 1000;
            continue;
        }

        if (was_busy && !s_busy && last_kind == JOB_FETCH) {
            fetched = s_fetch_result;
            if (s_fetch_result == FETCH_OK && pending == 0) redraw = true;
        }
        was_busy = s_busy;

        if (!s_busy) {
            bool settled = !b.stable && ms - last_input >= SETTLE_MS;
            if (pending && settled) {
                uint8_t target = (uint8_t)((s_shown + pending) % MODES);
                if (fetch) {
                    fetch = false; last_kind = JOB_FETCH; run(JOB_FETCH, 0);
                } else {
                    int fresh = keypin_battery_mv();
                    if (fresh > 0) s_batt_mv = fresh;
                    pending = 0; redraw = false; last_kind = JOB_DRAW; run(JOB_DRAW, target);
                }
            } else if (!pending && fetch) {
                fetch = false; last_kind = JOB_FETCH; run(JOB_FETCH, 0);
            } else if (!pending && redraw && !b.stable) {
                redraw = false; last_kind = JOB_DRAW; run(JOB_DRAW, s_shown);
            } else if (!pending && settled) {
                break;
            }
        }
        if (ms > MAX_AWAKE_MS && !s_busy) { ESP_LOGW(TAG, "awake too long, sleeping"); break; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    now = time(NULL);
    s_events[s_count % EVENTS] = (event_t){ .t = sched_time_valid(now) ? (uint32_t)now : 0, .cause = (uint8_t)cause,
        .presses = (uint8_t)(presses > 255 ? 255 : presses), .shown = s_shown, .fetch = fetched, .mv = (int16_t)s_batt_mv };
    s_count++;
    if (usb_serial_jtag_is_connected()) { vTaskDelay(pdMS_TO_TICKS(300)); print_events(); app_status_line(); }
    sleep_now();
}
