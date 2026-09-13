// Render a PicPak screen on the Mac from saved or live feeds.
//
// usage: preview FORECAST.json NEWS.xml OUT.ppm [MODE]
//
// MODE is 1-based: 1 environmental panel, 2 System Updates, 3 System Status
// (example device readings; PREVIEW_TROUBLE=1 shows warnings), anything else
// the placeholder. Prints each hour's conditions and each
// headline so the screen can be checked against the data.
#include "fb.h"
#include "news.h"
#include "screen.h"
#include "wx.h"

#include <stdio.h>
#include <stdlib.h>
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
    if (argc < 4) { fprintf(stderr, "usage: %s FORECAST.json NEWS.xml OUT.ppm [MODE]\n", argv[0]); return 2; }
    static char json[16384], xml[65536];
    size_t jlen = slurp(argv[1], json, sizeof json), xlen = slurp(argv[2], xml, sizeof xml);

    static wx_t w;
    if (!wx_parse(json, jlen, &w)) { fprintf(stderr, "could not parse %s\n", argv[1]); return 1; }
    static news_t news;
    if (!news_parse(xml, xlen, &news)) fprintf(stderr, "no headlines in %s\n", argv[2]);

    int64_t now = (int64_t)time(NULL);
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
    screen_ctx_t ctx = { .mode = mode, .modes = 5, .batt_pct = 78, .sync_utc = now };
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
        screen_status(fb, &st, "PICPAK-3E44", "BUTTON", "USB", &ctx);
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
