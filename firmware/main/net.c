// SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "net.h"
#include "sched.h"
#include "app_config.h"

#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "net";

#define GOT_IP     BIT0
#define GAVE_UP    BIT1
#define JOIN_MS    20000

static EventGroupHandle_t s_events;
static bool s_started;
static int s_retries;
static int s_rssi;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        if (++s_retries <= 3) {
            ESP_LOGW(TAG, "disconnected (reason %d), retry %d", d->reason, s_retries);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_events, GAVE_UP);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_events, GOT_IP);
    }
}

static esp_err_t join(void) {
    if (s_started) {
        EventBits_t b = xEventGroupGetBits(s_events);
        if (b & GOT_IP) return ESP_OK;
    } else {
        s_events = xEventGroupCreate();
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
        wifi_config_t wc = { 0 };
        strlcpy((char *)wc.sta.ssid, WIFI_SSID, sizeof wc.sta.ssid);
        strlcpy((char *)wc.sta.password, WIFI_PASSWORD, sizeof wc.sta.password);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
        s_started = true;
    }
    s_retries = 0;
    xEventGroupClearBits(s_events, GOT_IP | GAVE_UP);
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) return err;
    // Units of 0.25 dBm. The PicPak's supply browns out at full power (varanu5).
    esp_wifi_set_max_tx_power(40);
    EventBits_t b = xEventGroupWaitBits(s_events, GOT_IP | GAVE_UP, pdFALSE, pdFALSE, pdMS_TO_TICKS(JOIN_MS));
    if (b & GOT_IP) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) s_rssi = ap.rssi;
        return ESP_OK;
    }
    ESP_LOGE(TAG, "could not join %s", b & GAVE_UP ? "after retries" : "in time");
    return ESP_ERR_TIMEOUT;
}

typedef struct { char *buf; size_t cap, len; int64_t date; bool overflow; } body_t;

static esp_err_t on_http(esp_http_client_event_t *e) {
    body_t *b = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(e->header_key, "Date") == 0) {
        int64_t t;
        if (sched_http_date(e->header_value, &t)) b->date = t;
    } else if (e->event_id == HTTP_EVENT_ON_DATA) {
        if (b->len + e->data_len >= b->cap) { b->overflow = true; return ESP_OK; }
        memcpy(b->buf + b->len, e->data, e->data_len);
        b->len += e->data_len;
    }
    return ESP_OK;
}

esp_err_t net_fetch(const char *url, char *buf, size_t cap, size_t *len, int64_t *date_utc) {
    esp_err_t err = join();
    if (err != ESP_OK) return err;
    body_t body = { .buf = buf, .cap = cap };
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = on_http,
        .user_data = &body,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_ERR_NO_MEM;
    err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) return err;
    if (status != 200) { ESP_LOGE(TAG, "HTTP %d", status); return ESP_FAIL; }
    if (body.overflow) { ESP_LOGE(TAG, "response larger than %u bytes", (unsigned)cap); return ESP_ERR_INVALID_SIZE; }
    buf[body.len] = 0;
    *len = body.len;
    *date_utc = body.date;
    return ESP_OK;
}

int net_rssi(void) { return s_rssi; }

void net_stop(void) {
    if (!s_started) return;
    esp_wifi_stop();
    xEventGroupClearBits(s_events, GOT_IP);
}
