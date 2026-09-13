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

// The request the board and the preview tool both send.
#define WX_URL "https://api.open-meteo.com/v1/forecast?latitude=35.7915&longitude=-78.7811" \
    "&hourly=temperature_2m,apparent_temperature,relative_humidity_2m,dew_point_2m," \
    "precipitation_probability,precipitation,weather_code,wind_gusts_10m,uv_index,visibility,pressure_msl" \
    "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch" \
    "&timezone=America%2FNew_York&forecast_hours=13&timeformat=unixtime"

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
