// SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
// SPDX-License-Identifier: AGPL-3.0-or-later
// Host test for System Status readouts: formatting, severities and trend.
#include "status.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)
#define STR_IS(a, b) do { if (strcmp((a), (b)) != 0) { printf("FAIL %s:%d: [%s] != [%s]\n", __FILE__, __LINE__, (a), (b)); failures++; } } while (0)

static const int64_t NOW = 1789338720;   // 2026-09-13 18:32 EDT
static const int32_t EDT = -14400;

static status_t healthy(void) {
    status_t s = { .batt_mv = 4110, .now = NOW, .utc_offset = EDT, .wx_sync = NOW - 1200, .news_sync = NOW - 1200,
                   .next_fetch = NOW + 1740, .rssi = -61, .link_ms = 2400, .core_ok = true, .core_c = 31,
                   .app_bytes = 1209000, .app_part_bytes = 4 * 1024 * 1024, .flashed = NOW - 3000,
                   .accel_ok = true, .accel_mg = { 2, -5, 1006 } };
    strcpy(s.build, "6c747c2-dirty");
    return s;
}

static readout_t row(const status_t *s, const char *label) {
    readout_t r[8];
    int n = status_rows(s, r, 8);
    for (int i = 0; i < n; i++) if (strcmp(r[i].label, label) == 0) return r[i];
    printf("FAIL no row %s\n", label);
    failures++;
    return r[0];
}

static void test_healthy(void) {
    status_t s = healthy();
    readout_t r[8];
    CHECK(status_rows(&s, r, 8) == 6);
    STR_IS(r[0].label, "FEEDS");
    STR_IS(r[5].label, "ATTITUDE");
    CHECK(status_rows(&s, r, 2) == 2);

    readout_t cell = status_cell(&s);
    STR_IS(cell.value, "93%");   // 4110 mV on the batt.c curve
    STR_IS(cell.sub, "4.11V");
    CHECK(cell.level == 4 && cell.sev == SEV_NOTED);

    readout_t f = row(&s, "FEEDS");
    STR_IS(f.value, "18:12");
    STR_IS(f.sub, "BOTH FRESH  NEXT 19:01");
    CHECK(f.sev == SEV_NOTED);

    readout_t l = row(&s, "COMMS LINK");
    STR_IS(l.value, "-61DBM");
    STR_IS(l.sub, "JOIN+FETCH 2.4S");
    CHECK(l.level == 3 && l.sev == SEV_NOTED);

    readout_t c = row(&s, "CORE TEMP");
    STR_IS(c.value, "88\260F");
    STR_IS(c.sub, "CHIP 31\260C");

    readout_t m = row(&s, "MEMORY CORE");
    STR_IS(m.value, "28%");
    STR_IS(m.sub, "APP 1.1 OF 4.0MB");

    readout_t fw = row(&s, "FIRMWARE");
    STR_IS(fw.value, "6C747C2");
    STR_IS(fw.sub, "FLASHED 17:42 13 SEP");

    readout_t a = row(&s, "ATTITUDE");
    STR_IS(a.value, "FLAT");
    STR_IS(a.sub, "FACE UP  0\260");

    CHECK(status_verdict(&cell, r, 6) == SEV_NOTED);
}

static void test_trouble(void) {
    status_t s = healthy();
    s.batt_mv = 3580;
    readout_t cell = status_cell(&s);
    CHECK(cell.sev == SEV_DANGER && cell.level <= 1);
    s.batt_mv = 3740;
    CHECK(status_cell(&s).sev == SEV_CAUTION);
    s.batt_mv = -1;
    STR_IS(status_cell(&s).value, "--");

    s = healthy();
    s.news_failed = true;
    STR_IS(row(&s, "FEEDS").sub, "NEWS FAIL  NEXT 19:01");
    CHECK(row(&s, "FEEDS").sev == SEV_CAUTION);

    s = healthy();
    s.fetch_failures = 3;
    s.wx_sync = NOW - 4 * 3600;
    CHECK(row(&s, "FEEDS").sev == SEV_CAUTION);
    STR_IS(row(&s, "FEEDS").sub, "WX 4H OLD  RETRY 19:01");
    CHECK(row(&s, "COMMS LINK").sev == SEV_DANGER);
    STR_IS(row(&s, "COMMS LINK").sub, "3 FAILED FETCHES");
    s.wx_sync = NOW - 13 * 3600;
    CHECK(row(&s, "FEEDS").sev == SEV_DANGER);

    s = healthy();
    s.wx_sync = 0;
    STR_IS(row(&s, "FEEDS").value, "--:--");
    STR_IS(row(&s, "FEEDS").sub, "NOT SYNCED YET");

    s = healthy();
    s.rssi = -84;
    CHECK(row(&s, "COMMS LINK").sev == SEV_CAUTION && row(&s, "COMMS LINK").level == 1);
    s.rssi = 0;
    STR_IS(row(&s, "COMMS LINK").value, "--");

    s = healthy();
    s.core_c = 60;
    CHECK(row(&s, "CORE TEMP").sev == SEV_CAUTION);
    s.core_ok = false;
    STR_IS(row(&s, "CORE TEMP").value, "--");

    s = healthy();
    s.app_bytes = 4 * 1024 * 1024 - 1000;
    CHECK(row(&s, "MEMORY CORE").sev == SEV_DANGER);

    s = healthy();
    s.flashed = 0;
    STR_IS(row(&s, "FIRMWARE").sub, "FLASHED, AWAITING CLOCK");
    s.build[0] = 0;
    STR_IS(row(&s, "FIRMWARE").value, "--");

    s = healthy();
    s.accel_mg[0] = 0; s.accel_mg[1] = -1000; s.accel_mg[2] = 180;
    STR_IS(row(&s, "ATTITUDE").value, "UPRIGHT");
    STR_IS(row(&s, "ATTITUDE").sub, "LEAN 10\260");
    s.accel_mg[1] = 0; s.accel_mg[2] = -1000;
    STR_IS(row(&s, "ATTITUDE").sub, "FACE DOWN  0\260");
    s.accel_mg[0] = 10; s.accel_mg[1] = 20; s.accel_mg[2] = 30;
    STR_IS(row(&s, "ATTITUDE").value, "FALLING");

    s = healthy();
    s.rssi = -90; s.fetch_failures = 1;
    readout_t r[8];
    int n = status_rows(&s, r, 8);
    readout_t cell2 = status_cell(&s);
    CHECK(status_verdict(&cell2, r, n) == SEV_CAUTION);
}

static void test_trend(void) {
    batt_sample_t ring[STATUS_TREND_SAMPLES];
    uint32_t count = 0;
    int d, h;
    status_trend(ring, STATUS_TREND_SAMPLES, count, NOW, 4000, &d, &h);
    CHECK(d == 0 && h == 0);   // no history

    for (int i = 30; i >= 0; i--) status_trend_add(ring, STATUS_TREND_SAMPLES, &count, NOW - i * 3600, 4100 - (30 - i) * 2);
    CHECK(count == 31);
    status_trend(ring, STATUS_TREND_SAMPLES, count, NOW, 4040, &d, &h);
    // The ring holds the last 26 samples (25 h back); the oldest is 4100 - 5 * 2 = 4090 mV.
    CHECK(h == 25);
    CHECK(d == 4040 - 4090);

    count = 0;
    status_trend_add(ring, STATUS_TREND_SAMPLES, &count, NOW - 3600, 4000);   // only 1 h of history
    status_trend(ring, STATUS_TREND_SAMPLES, count, NOW, 4010, &d, &h);
    CHECK(h == 0);
    status_trend_add(ring, STATUS_TREND_SAMPLES, &count, NOW, -1);   // bad readings are skipped
    CHECK(count == 1);

    status_t s = healthy();
    s.trend_mv = -90; s.trend_hours = 24;
    STR_IS(status_cell(&s).sub, "4.11V  -0.09V/24H");
    s.trend_mv = 20;
    STR_IS(status_cell(&s).sub, "4.11V  +0.02V/24H");
}

int main(void) {
    test_healthy();
    test_trouble();
    test_trend();
    if (failures) { printf("%d check(s) failed\n", failures); return EXIT_FAILURE; }
    printf("status: all checks passed\n");
    return EXIT_SUCCESS;
}
