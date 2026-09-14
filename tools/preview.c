// SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
// SPDX-License-Identifier: AGPL-3.0-or-later
// Render a PicPak screen on the Mac from saved or live feeds.
//
// usage: preview FORECAST.json NEWS.xml OUT.ppm [MODE]
//        preview --urls    prints the weather and news URLs from the settings
//
// MODE is 1-based: 1 environmental panel, 2 System Updates, 3 System Status
// (example device readings; PREVIEW_TROUBLE=1 shows warnings), 4 Orbital
// Tracking (ORBIT_TLE and ORBIT_NEO name saved responses; defaults are the
// test fixtures), 5 Crew Manifest. Settings come from config.h, or from
// config.example.h when it is missing or PREVIEW_EXAMPLE is defined.
// PREVIEW_NOW=unix seconds renders at another time, PREVIEW_PLACE=name labels
// another place. Prints each hour's conditions and each headline so the
// screen can be checked against the data.
#include "fb.h"
#include "news.h"
#include "screen.h"
#include "wx.h"

#if __has_include("config.h") && !defined(PREVIEW_EXAMPLE)
#include "config.h"
#else
#include "config.example.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const unsigned char DISP[4][3] = { { 35, 34, 31 }, { 228, 226, 216 }, { 220, 184, 0 }, { 179, 48, 31 } };
static const char *CODE[COND_COUNT] = { "NOM", "HEAT", "CRYO", "PRECIP", "WIND", "ELEC", "RANGE", "HUMID", "ARID", "RAD", "VIS", "PRES" };

static size_t slurp(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    return n;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--urls") == 0) {
        char url[512];
        wx_url(url, sizeof url, PLACE_LAT, PLACE_LON);
        printf("%s\n%s\n", url, NEWS_URL);
        return 0;
    }
    if (argc < 4) { fprintf(stderr, "usage: %s FORECAST.json NEWS.xml OUT.ppm [MODE]\n", argv[0]); return 2; }
    static char json[16384], xml[65536];
    size_t jlen = slurp(argv[1], json, sizeof json), xlen = slurp(argv[2], xml, sizeof xml);

    static wx_t w;
    if (!wx_parse(json, jlen, &w)) { fprintf(stderr, "could not parse %s\n", argv[1]); return 1; }
    static news_t news;
    if (!news_parse(xml, xlen, &news)) fprintf(stderr, "no headlines in %s\n", argv[2]);

    int64_t now = getenv("PREVIEW_NOW") ? atoll(getenv("PREVIEW_NOW")) : (int64_t)time(NULL);
    for (int i = 0; i < w.count; i++) {
        wx_flag_t f[16];
        int n = wx_conditions(&w, i, f, 16);
        printf("h%02d %5.1fF feels %5.1f rh %3.0f dew %4.1f pop %3.0f gust %4.1f uv %4.1f :",
               i, w.h[i].temp_f, w.h[i].feels_f, w.h[i].rh, w.h[i].dew_f, w.h[i].pop, w.h[i].gust_mph, w.h[i].uv);
        for (int k = 0; k < n; k++) printf(" %s%s", CODE[f[k].cond], f[k].sev == SEV_DANGER ? "!" : "");
        printf("\n");
    }
    for (int i = 0; i < news.count; i++)
        printf("n%d %4.1fh  %s\n", i, (now - news.item[i].published) / 3600.0, news.item[i].title);

    int mode = argc > 4 ? atoi(argv[4]) - 1 : 0;
    screen_ctx_t ctx = { .mode = mode, .modes = 5, .batt_pct = 78, .sync_utc = now,
                         .place = getenv("PREVIEW_PLACE") ? getenv("PREVIEW_PLACE") : PLACE_NAME, .feed = NEWS_LABEL };
    static uint8_t fb[FB_BYTES];
    if (mode == 0) screen_env(fb, &w, &ctx);
    else if (mode == 1) screen_news(fb, &news, now, w.utc_offset, &ctx);
    else if (mode == 2) {
        // Example device readings; the forecast and headline sync times are real.
        status_t st = { .batt_mv = 4110, .trend_mv = -20, .trend_hours = 24, .now = now, .utc_offset = w.utc_offset,
                        .wx_sync = now - 300, .news_sync = now - 300, .next_fetch = (now / 3600 + 1) * 3600 + 90,
                        .rssi = -61, .link_ms = 2400, .core_ok = true, .core_c = 31.5f,
                        .app_bytes = 1225000, .app_part_bytes = 4 * 1024 * 1024, .flashed = now - 1800,
                        .accel_ok = true, .accel_mg = { 40, -990, 170 } };
        snprintf(st.build, sizeof st.build, "6c747c2");
        if (getenv("PREVIEW_TROUBLE")) { st.batt_mv = 3590; st.trend_mv = -90; st.rssi = -86; st.fetch_failures = 3; st.wx_sync = now - 5 * 3600; }
        screen_status(fb, &st, "PICPAK-1A2B", "BUTTON", "USB", &ctx);
    }
    else if (mode == 3) {
        static orbit_t o;
        static char text[16384];
        const char *tle = getenv("ORBIT_TLE") ? getenv("ORBIT_TLE") : "test/fixtures/celestrak-iss-2026-09-13.tle";
        const char *neo = getenv("ORBIT_NEO") ? getenv("ORBIT_NEO") : "test/fixtures/jpl-cad-2026-09-13.json";
        slurp(tle, text, sizeof text);
        if (!orbit_parse_tle(text, o.l1, o.l2)) { fprintf(stderr, "no elements in %s\n", tle); return 1; }
        size_t nlen = slurp(neo, text, sizeof text);
        o.neo_ok = orbit_parse_neo(text, nlen, &o.neo);
        sgp4_t sat;
        sgp4_parse(o.l1, o.l2, &sat);
        const site_t site = { PLACE_LAT, PLACE_LON, PLACE_ELEV_M / 1000.0 };
        o.count = (uint8_t)pass_find(&sat, &site, now, now + 3 * 86400, PASS_MIN_EL, true, o.pass, ORBIT_PASSES);
        for (int i = 0; i < o.count; i++)
            printf("pass %d rise %+.1fh %ds max %.1f %d>%d %s\n", i, (o.pass[i].rise - now) / 3600.0, (int)(o.pass[i].set - o.pass[i].rise),
                   o.pass[i].max_el / 10.0, o.pass[i].rise_az, o.pass[i].set_az, o.pass[i].visible ? "visible" : "");
        screen_orbit(fb, &o, &site, now, w.utc_offset, &ctx);
    }
    else if (mode == 4) {
        static const crew_t crew = CREW_CONFIG;
        screen_crew(fb, &crew, now, w.utc_offset, &ctx);
    }
    else screen_placeholder(fb, &ctx);

    FILE *out = fopen(argv[3], "wb");
    if (!out) { perror(argv[3]); return 1; }
    fprintf(out, "P6\n%d %d\n255\n", FB_W, FB_H);
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) fwrite(DISP[fb_get(fb, x, y)], 1, 3, out);
    fclose(out);
    return 0;
}
