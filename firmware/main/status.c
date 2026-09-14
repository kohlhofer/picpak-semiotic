// SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "status.h"
#include "batt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void hhmm(char *buf, size_t n, int64_t utc, int32_t offset) {
    time_t t = (time_t)(utc + offset);
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(buf, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

readout_t status_cell(const status_t *s) {
    readout_t r = { .label = "POWER CELL", .icon = STI_CELL };
    int pct = batt_pct(s->batt_mv);
    if (pct < 0) {
        snprintf(r.value, sizeof r.value, "--");
        snprintf(r.sub, sizeof r.sub, "NO READING");
        r.sev = SEV_CAUTION;
        return r;
    }
    snprintf(r.value, sizeof r.value, "%d%%", pct);
    if (s->trend_hours > 0)
        snprintf(r.sub, sizeof r.sub, "%d.%02dV  %c%d.%02dV/%dH", s->batt_mv / 1000, s->batt_mv / 10 % 100,
                 s->trend_mv < 0 ? '-' : '+', abs(s->trend_mv) / 1000, abs(s->trend_mv) / 10 % 100, s->trend_hours);
    else
        snprintf(r.sub, sizeof r.sub, "%d.%02dV", s->batt_mv / 1000, s->batt_mv / 10 % 100);
    r.level = (pct + 12) / 25;
    if (r.level > 4) r.level = 4;
    r.sev = pct < 20 ? SEV_DANGER : pct < 35 ? SEV_CAUTION : SEV_NOTED;
    return r;
}

static readout_t feeds(const status_t *s) {
    readout_t r = { .label = "FEEDS", .icon = STI_FEED };
    char next[8];
    if (!s->wx_sync || !s->now) {
        snprintf(r.value, sizeof r.value, "--:--");
        snprintf(r.sub, sizeof r.sub, s->fetch_failures ? "%u FAILED FETCHES" : "NOT SYNCED YET", s->fetch_failures);
        r.sev = s->fetch_failures >= 3 ? SEV_DANGER : SEV_CAUTION;
        return r;
    }
    hhmm(r.value, sizeof r.value, s->wx_sync, s->utc_offset);
    int64_t age_h = (s->now - s->wx_sync) / 3600;
    hhmm(next, sizeof next, s->next_fetch, s->utc_offset);
    if (s->fetch_failures) {
        snprintf(r.sub, sizeof r.sub, "WX %lldH OLD  RETRY %s", (long long)age_h, next);
    } else if (s->news_failed || !s->news_sync) {
        snprintf(r.sub, sizeof r.sub, "NEWS FAIL  NEXT %s", next);
    } else {
        snprintf(r.sub, sizeof r.sub, "BOTH FRESH  NEXT %s", next);
    }
    r.sev = age_h >= 12 ? SEV_DANGER : (age_h >= 3 || s->news_failed || s->fetch_failures) ? SEV_CAUTION : SEV_NOTED;
    return r;
}

static readout_t link(const status_t *s) {
    readout_t r = { .label = "COMMS LINK", .icon = STI_LINK };
    if (!s->rssi) {
        snprintf(r.value, sizeof r.value, "--");
        snprintf(r.sub, sizeof r.sub, "NO LINK YET");
        r.sev = SEV_NOTED;
        return r;
    }
    snprintf(r.value, sizeof r.value, "%dDBM", s->rssi);
    if (s->fetch_failures) snprintf(r.sub, sizeof r.sub, "%u FAILED FETCHES", s->fetch_failures);
    else snprintf(r.sub, sizeof r.sub, "JOIN+FETCH %d.%dS", s->link_ms / 1000, s->link_ms / 100 % 10);
    r.level = s->rssi >= -55 ? 4 : s->rssi >= -65 ? 3 : s->rssi >= -75 ? 2 : s->rssi >= -85 ? 1 : 0;
    r.sev = s->fetch_failures >= 3 ? SEV_DANGER : (s->rssi < -80 || s->fetch_failures) ? SEV_CAUTION : SEV_NOTED;
    return r;
}

static readout_t core(const status_t *s) {
    readout_t r = { .label = "CORE TEMP", .icon = STI_CORE };
    if (!s->core_ok) {
        snprintf(r.value, sizeof r.value, "--");
        snprintf(r.sub, sizeof r.sub, "SENSOR UNAVAILABLE");
        return r;
    }
    snprintf(r.value, sizeof r.value, "%ld\260F", lround(s->core_c * 9 / 5 + 32));
    snprintf(r.sub, sizeof r.sub, "CHIP %ld\260C", lround(s->core_c));
    r.sev = s->core_c >= 70 ? SEV_DANGER : s->core_c >= 55 ? SEV_CAUTION : SEV_NOTED;
    return r;
}

static readout_t memory(const status_t *s) {
    readout_t r = { .label = "MEMORY CORE", .icon = STI_STORE };
    if (!s->app_bytes || !s->app_part_bytes) {
        snprintf(r.value, sizeof r.value, "--");
        snprintf(r.sub, sizeof r.sub, "SIZE UNKNOWN");
        return r;
    }
    unsigned pct = (unsigned)((uint64_t)s->app_bytes * 100 / s->app_part_bytes);
    snprintf(r.value, sizeof r.value, "%u%%", pct);
    unsigned used10 = (unsigned)((uint64_t)s->app_bytes * 10 / (1024 * 1024));
    unsigned size10 = (unsigned)((uint64_t)s->app_part_bytes * 10 / (1024 * 1024));
    snprintf(r.sub, sizeof r.sub, "APP %u.%u OF %u.%uMB", used10 / 10, used10 % 10, size10 / 10, size10 % 10);
    r.sev = pct >= 95 ? SEV_DANGER : pct >= 85 ? SEV_CAUTION : SEV_NOTED;
    return r;
}

static readout_t firmware(const status_t *s) {
    static const char *MON[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    readout_t r = { .label = "FIRMWARE", .icon = STI_CHIP };
    size_t n = 0;
    for (const char *p = s->build; *p && *p != '-' && n < 7; p++) {   // "6c747c2-dirty" -> "6C747C2"
        char c = *p;
        r.value[n++] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
    }
    r.value[n] = 0;
    if (!n) snprintf(r.value, sizeof r.value, "--");
    if (s->flashed) {
        time_t t = (time_t)(s->flashed + s->utc_offset);
        struct tm tm;
        gmtime_r(&t, &tm);
        snprintf(r.sub, sizeof r.sub, "FLASHED %02d:%02d %d %s", tm.tm_hour, tm.tm_min, tm.tm_mday, MON[tm.tm_mon]);
    } else {
        snprintf(r.sub, sizeof r.sub, "FLASHED, AWAITING CLOCK");
    }
    return r;
}

static readout_t attitude(const status_t *s) {
    readout_t r = { .label = "ATTITUDE", .icon = STI_TILT };
    if (!s->accel_ok) {
        snprintf(r.value, sizeof r.value, "--");
        snprintf(r.sub, sizeof r.sub, "SENSOR UNAVAILABLE");
        return r;
    }
    double x = s->accel_mg[0], y = s->accel_mg[1], z = s->accel_mg[2];
    double g = sqrt(x * x + y * y + z * z);
    if (g < 300) {
        snprintf(r.value, sizeof r.value, "FALLING");
        snprintf(r.sub, sizeof r.sub, "%.0f MG", g);
        r.sev = SEV_CAUTION;
        return r;
    }
    // Angle between gravity and the panel's face.
    double tilt = atan2(fabs(z), sqrt(x * x + y * y)) * 180.0 / M_PI;
    if (tilt >= 60) {
        snprintf(r.value, sizeof r.value, "FLAT");
        snprintf(r.sub, sizeof r.sub, z > 0 ? "FACE UP  %ld\260" : "FACE DOWN  %ld\260", lround(90 - tilt));
    } else {
        snprintf(r.value, sizeof r.value, "UPRIGHT");
        snprintf(r.sub, sizeof r.sub, "LEAN %ld\260", lround(tilt));
    }
    return r;
}

int status_rows(const status_t *s, readout_t *out, int max) {
    readout_t (*const F[])(const status_t *) = { feeds, link, core, memory, firmware, attitude };
    int n = 0;
    for (size_t i = 0; i < sizeof F / sizeof F[0] && n < max; i++) out[n++] = F[i](s);
    return n;
}

sev_t status_verdict(const readout_t *cell, const readout_t *rows, int n) {
    sev_t worst = cell ? cell->sev : SEV_NOTED;
    for (int i = 0; i < n; i++) if (rows[i].sev > worst) worst = rows[i].sev;
    return worst;
}

void status_trend_add(batt_sample_t *ring, int n, uint32_t *count, int64_t t, int mv) {
    if (mv <= 0 || !t) return;
    ring[*count % (uint32_t)n] = (batt_sample_t){ t, (int16_t)mv };
    (*count)++;
}

void status_trend(const batt_sample_t *ring, int n, uint32_t count, int64_t now, int mv, int *delta_mv, int *hours) {
    *delta_mv = 0;
    *hours = 0;
    if (mv <= 0 || !now) return;
    uint32_t have = count < (uint32_t)n ? count : (uint32_t)n;
    const batt_sample_t *best = NULL;
    for (uint32_t i = 0; i < have; i++) {
        const batt_sample_t *b = &ring[i];
        int64_t age = now - b->t;
        if (age < 3 * 3600 || age > 30 * 3600) continue;
        if (!best || b->t < best->t) best = b;
    }
    if (!best) return;
    *delta_mv = mv - best->mv;
    *hours = (int)((now - best->t + 1800) / 3600);
}
