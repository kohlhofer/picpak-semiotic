// Host test for SGP4, the pass finder, and the Sun and Moon, against Skyfield
// (python-sgp4 and the JPL DE421 ephemeris) for the ISS over Cary.
//
// usage: test_orbit ISS.tle
#include "orbit.h"
#include "pass.h"
#include "sgp4.h"
#include "sky.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)
#define NEAR(a, b, tol) do { double a_ = (a), b_ = (b); if (fabs(a_ - b_) > (tol)) { printf("FAIL %s:%d: %s = %.4f, want %.4f +/- %g\n", __FILE__, __LINE__, #a, a_, b_, (double)(tol)); failures++; } } while (0)

static const int64_t NOW = 1789340868;   // 2026-09-13 23:07:48 UTC
static const site_t CARY = { 35.7915, -78.7811, 0.154 };

static const struct { int64_t t; double r[3], v[3]; } TEME[] = {
    { 1789340868, { 1674.664754, -4010.414905, 5215.428503 }, { 6.307965357, 4.188437857, 1.188193451 } },
    { 1789362468, { -2883.165997, -5400.982380, 2948.182069 }, { 5.795373921, -0.350676424, 4.999346219 } },
    { 1789427268, { -1388.911882, 4113.920152, -5237.865437 }, { -6.616819400, -3.665204995, -1.129007235 } },
    { 1789600068, { -780.602314, 4248.943805, -5255.519829 }, { -7.133680555, -2.563354976, -1.017834238 } },
};

static const struct { int64_t rise, set; double max_el, rise_az, set_az; bool visible; } PASSES[] = {
    { 1789346215, 1789346338, 11.03, 319.9, 355.6, true },
    { 1789363724, 1789364109, 42.77, 324.0, 111.0, false },
    { 1789369586, 1789369831, 15.31, 269.9, 194.0, false },
    { 1789423781, 1789424183, 72.01, 220.8, 52.2, false },
    { 1789429685, 1789429933, 15.20, 294.9, 11.2, true },
    { 1789447286, 1789447632, 26.14, 333.7, 93.0, false },
    { 1789453091, 1789453438, 27.48, 289.0, 168.2, false },
    { 1789507345, 1789507720, 36.53, 201.5, 64.4, false },
    { 1789513183, 1789513507, 21.88, 275.1, 22.0, false },
    { 1789530854, 1789531138, 17.66, 343.9, 74.0, false },
    { 1789536621, 1789537012, 52.60, 302.6, 147.7, false },
    { 1789590934, 1789591239, 20.08, 179.0, 80.1, false },
    { 1789596697, 1789597071, 33.97, 256.8, 31.8, false },
};
#define NPASS (int)(sizeof PASSES / sizeof PASSES[0])

static double az_diff(double a, double b) { double d = fmod(fabs(a - b), 360.0); return d > 180 ? 360 - d : d; }

static void read_tle(const char *path, sgp4_t *s) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(2); }
    char lines[3][100];
    for (int i = 0; i < 3; i++) if (!fgets(lines[i], sizeof lines[i], f)) { printf("short TLE\n"); exit(2); }
    fclose(f);
    for (int i = 0; i < 3; i++) lines[i][strcspn(lines[i], "\r\n")] = 0;
    CHECK(sgp4_parse(lines[1], lines[2], s));
}

static void test_sgp4(const sgp4_t *s) {
    for (size_t i = 0; i < sizeof TEME / sizeof TEME[0]; i++) {
        double r[3], v[3];
        CHECK(sgp4_at(s, (double)TEME[i].t, r, v));
        for (int k = 0; k < 3; k++) {
            NEAR(r[k], TEME[i].r[k], 0.01);
            NEAR(v[k], TEME[i].v[k], 1e-5);
        }
    }
    sgp4_t bad;
    CHECK(!sgp4_parse("1 25544U", "2 25544", &bad));
    CHECK(!sgp4_parse("2 25544U 98067A   26256.36908747  .00004787  00000+0  94684-4 0  9995",
                      "1 25544  51.6306 223.6594 0004901 135.5950 224.5433 15.49098779585422", &bad));
}

static void test_passes(const sgp4_t *s) {
    pass_t p[20];
    clock_t c0 = clock();
    int n = pass_find(s, &CARY, NOW, NOW + 3 * 86400, 10.0, false, p, 20);
    printf("orbit: %d passes in %.1f ms on the host\n", n, (clock() - c0) * 1000.0 / CLOCKS_PER_SEC);
    CHECK(n == NPASS);
    for (int i = 0; i < n && i < NPASS; i++) {
        NEAR((double)p[i].rise, (double)PASSES[i].rise, 15);
        NEAR((double)p[i].set, (double)PASSES[i].set, 15);
        NEAR(p[i].max_el / 10.0, PASSES[i].max_el, 0.3);
        CHECK(az_diff(p[i].rise_az, PASSES[i].rise_az) <= 1.5);
        CHECK(az_diff(p[i].set_az, PASSES[i].set_az) <= 1.5);
        CHECK(p[i].visible == PASSES[i].visible);
        CHECK(p[i].el[0] >= 95 && p[i].el[PASS_ARC - 1] >= 95);
    }
    int v = pass_find(s, &CARY, NOW, NOW + 3 * 86400, 10.0, true, p, 20);
    CHECK(v == 2);
    CHECK(llabs(p[0].rise - PASSES[0].rise) <= 15 && llabs(p[1].rise - PASSES[4].rise) <= 15);
    CHECK(pass_find(s, &CARY, NOW, NOW + 3 * 86400, 10.0, true, p, 1) == 1);

    // Starting mid-pass: the pass begins at the start time.
    int64_t mid = PASSES[1].rise + 120;
    CHECK(pass_find(s, &CARY, mid, mid + 3600, 10.0, false, p, 4) == 1);
    CHECK(p[0].rise == mid && llabs(p[0].set - PASSES[1].set) <= 15);
}

static void test_sky(void) {
    int64_t rise, set;
    sky_sun_times(&CARY, 1789272000, &rise, &set);   // 2026-09-13 00:00 EDT
    NEAR((double)rise, 1789296977.0, 90);
    NEAR((double)set, 1789341905.0, 90);
    NEAR(sky_sun_el(&CARY, (double)NOW), 2.67, 0.5);

    double frac, elong;
    sky_moon((double)NOW, &frac, &elong);
    NEAR(frac, 0.0909, 0.02);
    NEAR(elong, 34.8, 2.0);
    sky_moon((double)NOW + 14 * 86400, &frac, &elong);
    CHECK(elong > 180 && frac > 0.8);   // two weeks on: waning gibbous
}

static void test_feeds(const char *tle_path) {
    FILE *f = fopen(tle_path, "r");
    char text[512];
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    char l1[70], l2[70];
    CHECK(orbit_parse_tle(text, l1, l2));
    CHECK(strncmp(l1, "1 25544U", 8) == 0 && strncmp(l2, "2 25544", 7) == 0 && strlen(l1) == 69);
    CHECK(orbit_parse_tle(strchr(text, '\n') + 1, l1, l2));   // without the name line
    CHECK(!orbit_parse_tle("GP data has not updated since your last successful download.", l1, l2));
    CHECK(!orbit_parse_tle("", l1, l2));

    static const char CAD[] = "{\"count\":1,\"fields\":[\"des\",\"orbit_id\",\"jd\",\"cd\",\"dist\",\"dist_min\",\"dist_max\",\"v_rel\",\"v_inf\",\"t_sigma_f\",\"h\"],"
        "\"data\":[[\"2026 RG15\",\"1\",\"2461298.816338493\",\"2026-Sep-15 07:36\",\"0.000633383546084647\",\"0.0006\",\"0.0006\",\"9.12929198854666\",\"8.6\",\"01:23\",\"25.775\"]]}";
    neo_t neo;
    CHECK(orbit_parse_neo(CAD, sizeof CAD - 1, &neo));
    CHECK(strcmp(neo.des, "2026 RG15") == 0);
    CHECK(neo.approach == 1789457760);   // 2026-09-15 07:36 UTC
    NEAR(orbit_ld(neo.dist_au), 0.2465, 0.001);
    NEAR(orbit_miles(neo.dist_au), 58877, 5);
    NEAR(orbit_mph(neo.v_kms), 20422, 5);
    int lo, hi;
    orbit_size_ft(neo.h, &lo, &hi);
    CHECK(lo == 61 && hi == 136);
    CHECK(!orbit_parse_neo("{\"count\":0,\"fields\":[\"des\"]}", 28, &neo));
    CHECK(!orbit_parse_neo("not json", 8, &neo));

    CHECK(strcmp(orbit_compass(320), "NW") == 0 && strcmp(orbit_compass(356), "N") == 0 && strcmp(orbit_compass(22), "N") == 0 &&
          strcmp(orbit_compass(23), "NE") == 0 && strcmp(orbit_compass(-90), "W") == 0);
    CHECK(orbit_moon_level(34.8) == 1 && orbit_moon_level(180) == 4 && orbit_moon_level(350) == 0);
    CHECK(strcmp(orbit_moon_name(0.09, 34.8), "WAXING CRESCENT") == 0 && strcmp(orbit_moon_name(0.5, 270), "LAST QUARTER") == 0 &&
          strcmp(orbit_moon_name(0.99, 180), "FULL MOON") == 0 && strcmp(orbit_moon_name(0.7, 220), "WANING GIBBOUS") == 0);

    orbit_t o = { .count = 2 };
    o.pass[0].set = 100;
    o.pass[1].set = 200;
    CHECK(orbit_next_pass(&o, 50) == &o.pass[0] && orbit_next_pass(&o, 100) == &o.pass[1] && orbit_next_pass(&o, 200) == NULL);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s ISS.tle\n", argv[0]); return 2; }
    sgp4_t s;
    read_tle(argv[1], &s);
    test_sgp4(&s);
    test_passes(&s);
    test_sky();
    test_feeds(argv[1]);
    if (failures) { printf("%d check(s) failed\n", failures); return EXIT_FAILURE; }
    printf("orbit: all checks passed\n");
    return EXIT_SUCCESS;
}
