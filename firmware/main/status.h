// System Status readouts: what the board knows about itself, turned into the
// labels, values and severities the console screen draws. Pure C.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "assets.h"
#include "cond.h"

#define STATUS_TREND_SAMPLES 26   // one per hourly fetch, a little over a day

typedef struct { int64_t t; int16_t mv; } batt_sample_t;

typedef struct {
    int     batt_mv;          // -1 unknown
    int     trend_mv;         // change over trend_hours
    int     trend_hours;      // 0 when there is not enough history

    int64_t now;              // unix seconds, 0 when the clock is not set
    int32_t utc_offset;

    int64_t wx_sync, news_sync, next_fetch;   // 0 when never
    uint8_t fetch_failures;   // consecutive failed forecast fetches
    bool    news_failed;      // the last headline fetch failed

    int     rssi;             // dBm at the last join, 0 when unknown
    int     link_ms;          // join plus both fetches, 0 when unknown

    bool    core_ok;
    float   core_c;           // chip temperature

    uint32_t app_bytes, app_part_bytes;       // 0 when unknown

    char    build[16];        // firmware version string
    int64_t flashed;          // first sync after this build was flashed, 0 when unknown

    bool    accel_ok;
    int     accel_mg[3];
} status_t;

typedef struct {
    const char   *label;
    char          value[16];
    char          sub[32];
    sev_t         sev;
    status_icon_t icon;
    int           level;      // 0-4 for CELL and LINK
} readout_t;

// Power cell readout; the console draws it as the tall gauge.
readout_t status_cell(const status_t *s);

// The row readouts in console order: feeds, comms link, core temp, memory
// core, firmware, attitude. Returns how many were written.
int status_rows(const status_t *s, readout_t *out, int max);

// Worst severity across the cell and every row.
sev_t status_verdict(const readout_t *cell, const readout_t *rows, int n);

// Keeps the change in cell voltage over roughly the last day. Samples go into
// a ring; trend is measured against the oldest sample at least 3 h old.
void status_trend_add(batt_sample_t *ring, int n, uint32_t *count, int64_t t, int mv);
void status_trend(const batt_sample_t *ring, int n, uint32_t count, int64_t now, int mv, int *delta_mv, int *hours);
