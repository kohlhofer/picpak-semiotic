// SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
// SPDX-License-Identifier: AGPL-3.0-or-later
// Weather for the environmental panel: an Open-Meteo hourly forecast in °F,
// mph and inches, and the rules that turn each hour into placards. Pure C.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cond.h"

#define WX_HOURS 13   // now plus twelve

typedef struct {
    int64_t time;        // unix seconds at the start of the hour
    float temp_f;        // air temperature
    float feels_f;       // apparent temperature
    float rh;            // relative humidity, %
    float dew_f;         // dew point
    float pop;           // chance of precipitation, %
    float precip_in;     // precipitation in the hour
    float gust_mph;
    float uv;
    float vis_ft;
    float pres_hpa;
    int   code;          // WMO weather code
} wx_hour_t;

typedef struct {
    int32_t utc_offset;  // seconds, local = utc + offset
    uint8_t count;       // rows filled, up to WX_HOURS
    wx_hour_t h[WX_HOURS];
} wx_t;

typedef struct { cond_t cond; sev_t sev; } wx_flag_t;

// The Open-Meteo request for a place, as the board and the preview tool both
// send it: imperial units, and the time zone the service finds for the
// coordinates. Returns the length, or a negative number if it did not fit.
int wx_url(char *buf, size_t cap, double lat, double lon);

// Parse an Open-Meteo response. Returns false when a required field is
// missing or empty; `out` is then undefined.
bool wx_parse(const char *json, size_t len, wx_t *out);

// Conditions for hour i, most severe first, then by placard priority.
// Returns how many were written (at most max).
int wx_conditions(const wx_t *w, int i, wx_flag_t *out, int max);

// Hour 0's conditions plus thermal spread across all hours.
int wx_active_now(const wx_t *w, wx_flag_t *out, int max);

// Highest and lowest air temperature across all hours.
void wx_hi_lo(const wx_t *w, float *hi, float *lo);
