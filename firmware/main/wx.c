#include "wx.h"
#include "cJSON.h"

#include <stdio.h>

// Placard priority when severities tie.
static const cond_t PRIORITY[] = {
    COND_ELEC, COND_PRECIP, COND_WIND, COND_HEAT, COND_CRYO, COND_RAD,
    COND_VIS, COND_PRES, COND_HUMID, COND_ARID, COND_RANGE, COND_NOMINAL,
};

static int rank(cond_t c) {
    for (int i = 0; i < (int)(sizeof PRIORITY / sizeof PRIORITY[0]); i++)
        if (PRIORITY[i] == c) return i;
    return 99;
}

static void sort(wx_flag_t *f, int n) {
    for (int i = 1; i < n; i++) {
        wx_flag_t v = f[i];
        int j = i - 1;
        while (j >= 0 && (f[j].sev < v.sev || (f[j].sev == v.sev && rank(f[j].cond) > rank(v.cond)))) {
            f[j + 1] = f[j];
            j--;
        }
        f[j + 1] = v;
    }
}

static void add(wx_flag_t *out, int *n, int max, cond_t c, sev_t s) {
    if (*n < max) out[(*n)++] = (wx_flag_t){ c, s };
}

int wx_conditions(const wx_t *w, int i, wx_flag_t *out, int max) {
    if (i < 0 || i >= w->count) return 0;
    const wx_hour_t *d = &w->h[i];
    const wx_hour_t *past = &w->h[i >= 3 ? i - 3 : 0];
    int n = 0;
    if (d->code >= 95) add(out, &n, max, COND_ELEC, SEV_DANGER);
    if (d->precip_in >= 0.16f) add(out, &n, max, COND_PRECIP, SEV_DANGER);
    else if (d->precip_in >= 0.01f && d->pop >= 40) add(out, &n, max, COND_PRECIP, SEV_CAUTION);
    if (d->gust_mph >= 40) add(out, &n, max, COND_WIND, SEV_DANGER);
    else if (d->gust_mph >= 25) add(out, &n, max, COND_WIND, SEV_CAUTION);
    if (d->feels_f >= 95) add(out, &n, max, COND_HEAT, SEV_DANGER);
    else if (d->feels_f >= 86) add(out, &n, max, COND_HEAT, SEV_CAUTION);
    if (d->temp_f <= 23) add(out, &n, max, COND_CRYO, SEV_DANGER);
    else if (d->temp_f <= 36) add(out, &n, max, COND_CRYO, SEV_CAUTION);
    if (d->uv >= 8) add(out, &n, max, COND_RAD, SEV_DANGER);
    else if (d->uv >= 6) add(out, &n, max, COND_RAD, SEV_CAUTION);
    if (d->vis_ft <= 3281) add(out, &n, max, COND_VIS, SEV_CAUTION);   // 1 km
    if (past->pres_hpa - d->pres_hpa >= 3) add(out, &n, max, COND_PRES, SEV_CAUTION);
    if (d->dew_f >= 68) add(out, &n, max, COND_HUMID, SEV_CAUTION);
    if (d->rh <= 25) add(out, &n, max, COND_ARID, SEV_CAUTION);
    sort(out, n);
    return n;
}

void wx_hi_lo(const wx_t *w, float *hi, float *lo) {
    *hi = *lo = w->count ? w->h[0].temp_f : 0;
    for (int i = 1; i < w->count; i++) {
        if (w->h[i].temp_f > *hi) *hi = w->h[i].temp_f;
        if (w->h[i].temp_f < *lo) *lo = w->h[i].temp_f;
    }
}

int wx_active_now(const wx_t *w, wx_flag_t *out, int max) {
    int n = wx_conditions(w, 0, out, max);
    float hi, lo;
    wx_hi_lo(w, &hi, &lo);
    if (hi - lo >= 22) add(out, &n, max, COND_RANGE, SEV_CAUTION);
    sort(out, n);
    return n;
}

// Copies up to WX_HOURS numbers from hourly[name] into the rows, at the given
// field offset. Returns how many there were, or -1 if the array is missing.
static int field(const cJSON *hourly, const char *name, wx_t *w, size_t offset, bool as_int) {
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(hourly, name);
    if (!cJSON_IsArray(arr)) return -1;
    int n = 0;
    const cJSON *v;
    cJSON_ArrayForEach(v, arr) {
        if (n >= WX_HOURS) break;
        double x = cJSON_IsNumber(v) ? v->valuedouble : 0;   // null reads as 0
        char *row = (char *)&w->h[n];
        if (as_int) *(int *)(row + offset) = (int)x;
        else *(float *)(row + offset) = (float)x;
        n++;
    }
    return n;
}

bool wx_parse(const char *json, size_t len, wx_t *out) {
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return false;
    bool ok = false;
    const cJSON *off = cJSON_GetObjectItemCaseSensitive(root, "utc_offset_seconds");
    const cJSON *hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    const cJSON *times = cJSON_GetObjectItemCaseSensitive(hourly, "time");
    if (cJSON_IsNumber(off) && cJSON_IsArray(times)) {
        *out = (wx_t){ .utc_offset = (int32_t)off->valuedouble };
        int n = 0;
        const cJSON *t;
        cJSON_ArrayForEach(t, times) {
            if (n >= WX_HOURS || !cJSON_IsNumber(t)) break;
            out->h[n++].time = (int64_t)t->valuedouble;
        }
        out->count = (uint8_t)n;
        ok = n > 0;
        static const struct { const char *name; size_t offset; bool as_int; } F[] = {
            { "temperature_2m", offsetof(wx_hour_t, temp_f), false },
            { "apparent_temperature", offsetof(wx_hour_t, feels_f), false },
            { "relative_humidity_2m", offsetof(wx_hour_t, rh), false },
            { "dew_point_2m", offsetof(wx_hour_t, dew_f), false },
            { "precipitation_probability", offsetof(wx_hour_t, pop), false },
            { "precipitation", offsetof(wx_hour_t, precip_in), false },
            { "weather_code", offsetof(wx_hour_t, code), true },
            { "wind_gusts_10m", offsetof(wx_hour_t, gust_mph), false },
            { "uv_index", offsetof(wx_hour_t, uv), false },
            { "visibility", offsetof(wx_hour_t, vis_ft), false },
            { "pressure_msl", offsetof(wx_hour_t, pres_hpa), false },
        };
        for (size_t i = 0; ok && i < sizeof F / sizeof F[0]; i++)
            if (field(hourly, F[i].name, out, F[i].offset, F[i].as_int) < n) ok = false;
    }
    cJSON_Delete(root);
    return ok;
}

int wx_url(char *buf, size_t cap, double lat, double lon) {
    int n = snprintf(buf, cap,
                     "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                     "&hourly=temperature_2m,apparent_temperature,relative_humidity_2m,dew_point_2m,"
                     "precipitation_probability,precipitation,weather_code,wind_gusts_10m,uv_index,visibility,pressure_msl"
                     "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch"
                     "&timezone=auto&forecast_hours=13&timeformat=unixtime",
                     lat, lon);
    return n >= 0 && (size_t)n < cap ? n : -1;
}
