// Host test for forecast parsing and the placard rules. Run with `make test`.
#include "wx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static bool has(const wx_flag_t *f, int n, cond_t c, sev_t s) {
    for (int i = 0; i < n; i++) if (f[i].cond == c && f[i].sev == s) return true;
    return false;
}

// A calm hour: nothing should trigger.
static wx_hour_t calm(void) {
    return (wx_hour_t){ .temp_f = 68, .feels_f = 68, .rh = 55, .dew_f = 50, .pop = 0, .precip_in = 0,
                        .gust_mph = 8, .uv = 2, .vis_ft = 60000, .pres_hpa = 1016, .code = 1 };
}

static wx_t flat(void) {
    wx_t w = { .utc_offset = -14400, .count = WX_HOURS };
    for (int i = 0; i < WX_HOURS; i++) { w.h[i] = calm(); w.h[i].time = 1789322400 + i * 3600; }
    return w;
}

static void test_fixture(const char *path) {
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (!f) return;
    static char buf[16384];
    size_t len = fread(buf, 1, sizeof buf, f);
    fclose(f);
    static wx_t w;
    CHECK(wx_parse(buf, len, &w));
    CHECK(w.count == WX_HOURS);
    CHECK(w.utc_offset == -14400);
    CHECK(w.h[0].time == 1789322400);
    CHECK(fabs(w.h[0].temp_f - 84.6f) < 0.01f);
    CHECK(fabs(w.h[0].feels_f - 93.7f) < 0.01f);
    CHECK(w.h[0].code == 1);
    CHECK(fabs(w.h[0].vis_ft - 78412.072f) < 1);
    CHECK(w.h[12].time == w.h[0].time + 12 * 3600);

    wx_flag_t fl[16];
    int n = wx_conditions(&w, 0, fl, 16);
    // 93.7 °F feels-like, UV 6.4, dew point 72.8 °F
    CHECK(n == 3);
    CHECK(fl[0].cond == COND_HEAT && fl[0].sev == SEV_CAUTION);
    CHECK(fl[1].cond == COND_RAD && fl[1].sev == SEV_CAUTION);
    CHECK(fl[2].cond == COND_HUMID);
}

static void test_bad_json(void) {
    wx_t w;
    CHECK(!wx_parse("", 0, &w));
    CHECK(!wx_parse("{\"utc_offset_seconds\":0}", 25, &w));
    const char *missing = "{\"utc_offset_seconds\":0,\"hourly\":{\"time\":[1,2],\"temperature_2m\":[1,2]}}";
    CHECK(!wx_parse(missing, strlen(missing), &w));
}

static void test_thresholds(void) {
    wx_flag_t f[16];
    wx_t w = flat();
    CHECK(wx_conditions(&w, 0, f, 16) == 0);

    w.h[1].feels_f = 86;   CHECK(has(f, wx_conditions(&w, 1, f, 16), COND_HEAT, SEV_CAUTION));
    w.h[1].feels_f = 95;   CHECK(has(f, wx_conditions(&w, 1, f, 16), COND_HEAT, SEV_DANGER));
    w = flat(); w.h[1].temp_f = 36;   CHECK(has(f, wx_conditions(&w, 1, f, 16), COND_CRYO, SEV_CAUTION));
    w.h[1].temp_f = 23;               CHECK(has(f, wx_conditions(&w, 1, f, 16), COND_CRYO, SEV_DANGER));
    w = flat(); w.h[1].precip_in = 0.01f; w.h[1].pop = 39;
    CHECK(wx_conditions(&w, 1, f, 16) == 0);   // too unlikely
    w.h[1].pop = 40;                  CHECK(has(f, wx_conditions(&w, 1, f, 16), COND_PRECIP, SEV_CAUTION));
    w.h[1].precip_in = 0.16f; w.h[1].pop = 0;
    CHECK(has(f, wx_conditions(&w, 1, f, 16), COND_PRECIP, SEV_DANGER));   // heavy rain counts regardless
    w = flat(); w.h[1].gust_mph = 25; CHECK(has(f, wx_conditions(&w, 1, f, 16), COND_WIND, SEV_CAUTION));
    w.h[1].gust_mph = 40;             CHECK(has(f, wx_conditions(&w, 1, f, 16), COND_WIND, SEV_DANGER));
    w = flat(); w.h[1].code = 95;     CHECK(has(f, wx_conditions(&w, 1, f, 16), COND_ELEC, SEV_DANGER));
    w = flat(); w.h[1].uv = 6;        CHECK(has(f, wx_conditions(&w, 1, f, 16), COND_RAD, SEV_CAUTION));
    w.h[1].uv = 8;                    CHECK(has(f, wx_conditions(&w, 1, f, 16), COND_RAD, SEV_DANGER));
    w = flat(); w.h[1].vis_ft = 3000; CHECK(has(f, wx_conditions(&w, 1, f, 16), COND_VIS, SEV_CAUTION));
    w = flat(); w.h[1].dew_f = 68;    CHECK(has(f, wx_conditions(&w, 1, f, 16), COND_HUMID, SEV_CAUTION));
    w = flat(); w.h[1].rh = 25;       CHECK(has(f, wx_conditions(&w, 1, f, 16), COND_ARID, SEV_CAUTION));

    // Pressure fall compares with three hours earlier, or hour 0 early on.
    w = flat(); w.h[4].pres_hpa = 1013;
    CHECK(has(f, wx_conditions(&w, 4, f, 16), COND_PRES, SEV_CAUTION));
    w.h[4].pres_hpa = 1013.5f;
    CHECK(!has(f, wx_conditions(&w, 4, f, 16), COND_PRES, SEV_CAUTION));
    w = flat(); w.h[2].pres_hpa = 1012;
    CHECK(has(f, wx_conditions(&w, 2, f, 16), COND_PRES, SEV_CAUTION));
}

static void test_ordering_and_spread(void) {
    wx_flag_t f[16];
    wx_t w = flat();
    w.h[0].dew_f = 70;        // caution, low priority
    w.h[0].gust_mph = 45;     // danger
    w.h[0].feels_f = 90;      // caution, above humid in priority
    int n = wx_conditions(&w, 0, f, 16);
    CHECK(n == 3);
    CHECK(f[0].cond == COND_WIND && f[0].sev == SEV_DANGER);
    CHECK(f[1].cond == COND_HEAT);
    CHECK(f[2].cond == COND_HUMID);

    w = flat();
    w.h[5].temp_f = 90;   // 22 °F above the 68 °F rest
    CHECK(has(f, wx_active_now(&w, f, 16), COND_RANGE, SEV_CAUTION));
    CHECK(!has(f, wx_conditions(&w, 0, f, 16), COND_RANGE, SEV_CAUTION));   // never per hour
    w.h[5].temp_f = 89.5f;
    CHECK(!has(f, wx_active_now(&w, f, 16), COND_RANGE, SEV_CAUTION));

    float hi, lo;
    w.h[3].temp_f = 40;
    wx_hi_lo(&w, &hi, &lo);
    CHECK(hi == 89.5f && lo == 40);

    // The output limit holds.
    w = flat(); w.h[0].code = 95; w.h[0].gust_mph = 50; w.h[0].uv = 9;
    CHECK(wx_conditions(&w, 0, f, 2) == 2);
    CHECK(f[0].cond == COND_ELEC);
}

int main(int argc, char **argv) {
    test_fixture(argc > 1 ? argv[1] : "test/fixtures/open-meteo-cary-2026-09-13.json");
    test_bad_json();
    test_thresholds();
    test_ordering_and_spread();
    if (failures) { printf("%d check(s) failed\n", failures); return EXIT_FAILURE; }
    printf("wx: all checks passed\n");
    return EXIT_SUCCESS;
}
