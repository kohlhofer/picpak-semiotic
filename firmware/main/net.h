// Wi-Fi station and one HTTPS GET per wake.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Joins Wi-Fi if needed, fetches url into buf (NUL-terminated), and reports
// the body length and the server's Date header (0 when absent).
esp_err_t net_fetch(const char *url, char *buf, size_t cap, size_t *len, int64_t *date_utc);

// Signal strength of the access point at the last join, 0 before one.
int net_rssi(void);

// Radio off. Call before a panel refresh: both are the board's biggest loads.
void net_stop(void);
