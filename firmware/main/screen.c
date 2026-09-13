#include "screen.h"
#include "gfx.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

// Layouts follow the approved design pages, moved inside the case margins.

// The case covers the panel unevenly. Measured with an on-device edge check on
// 2026-09-13: it hides about 6 px at the top, 10 px left and right, 2 px at the
// bottom. Content keeps 4 px clear of that; backgrounds such as the header
// band still run to the edge.
#define EDGE_L    14
#define EDGE_R    (FB_W - 14)
#define EDGE_B    (FB_H - 6)
#define BAND_H    22
#define BAND_BASE 16

static const char *NAME[COND_COUNT] = {
    "NOMINAL", "THERMAL HIGH", "CRYOGENIC", "PRECIPITATION", "AIRFLOW", "ELECTRICAL",
    "THERMAL SPREAD", "SATURATION", "DESICCATION", "RADIATION", "VISIBILITY", "PRESSURE FALL",
};
static const char *DAY[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
static const char *MON[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

#define LOCATION "CARY NC"

static void local_tm(int64_t utc, int32_t offset, struct tm *out) {
    time_t t = (time_t)(utc + offset);
    gmtime_r(&t, out);
}

static void deg(char *buf, size_t n, float f) { snprintf(buf, n, "%ld\260", lround(f)); }

static void header(uint8_t *fb, const char *left, const char *right) {
    fb_rect(fb, 0, 0, FB_W, BAND_H, FB_BLACK);
    gfx_text(fb, EDGE_L, BAND_BASE, &FONT_SK8, FB_WHITE, left, GFX_LEFT);
    if (right) gfx_text(fb, EDGE_R, BAND_BASE, &FONT_SK8, FB_WHITE, right, GFX_RIGHT);
}

static void bar(uint8_t *fb, int y, const char *label, const char *right) {
    fb_rect(fb, EDGE_L, y, EDGE_R - EDGE_L, 2, FB_BLACK);
    int w = gfx_text_width(&FONT_SK8, label);
    fb_rect(fb, EDGE_L, y - 6, w + 10, 14, FB_BLACK);
    gfx_text(fb, EDGE_L + 5, y + 4, &FONT_SK8, FB_WHITE, label, GFX_LEFT);
    int rw = gfx_text_width(&FONT_SK8, right);
    fb_rect(fb, EDGE_R - rw - 6, y - 5, rw + 6, 12, FB_WHITE);
    gfx_text(fb, EDGE_R, y + 4, &FONT_SK8, FB_BLACK, right, GFX_RIGHT);
}

static void footer(uint8_t *fb, const screen_ctx_t *ctx, const char *left) {
    const int y = EDGE_B;
    fb_rect(fb, EDGE_L, y - 15, EDGE_R - EDGE_L, 1, FB_BLACK);
    gfx_text(fb, EDGE_L, y - 2, &FONT_SK8, FB_BLACK, left, GFX_LEFT);
    const int s = 11, gap = 3;
    int x = EDGE_R - (ctx->modes * s + (ctx->modes - 1) * gap);
    for (int k = 0; k < ctx->modes; k++) {
        bool cur = k == ctx->mode;
        fb_rect(fb, x, y - 12, s, s, FB_BLACK);
        if (!cur) fb_rect(fb, x + 1, y - 11, s - 2, s - 2, FB_WHITE);
        char digit[2] = { (char)('1' + k), 0 };
        gfx_text(fb, x + 6, y - 3, &FONT_SK8, cur ? FB_WHITE : FB_BLACK, digit, GFX_CENTER);
        x += s + gap;
    }
}

static void batt_text(char *buf, size_t n, int pct) {
    if (pct < 0) snprintf(buf, n, "BATT --");
    else snprintf(buf, n, "BATT %d%%", pct);
}

static void sync_text(char *buf, size_t n, const screen_ctx_t *ctx, int32_t offset, const char *suffix) {
    char batt[16];
    batt_text(batt, sizeof batt, ctx->batt_pct);
    if (ctx->sync_utc) {
        struct tm t;
        local_tm(ctx->sync_utc, offset, &t);
        snprintf(buf, n, "SYNC %02d:%02d  %s%s", t.tm_hour, t.tm_min, batt, suffix);
    } else {
        snprintf(buf, n, "NO SYNC  %s%s", batt, suffix);
    }
}

void screen_env(uint8_t *fb, const wx_t *w, const screen_ctx_t *ctx) {
    char buf[48];
    fb_fill(fb, FB_WHITE);

    buf[0] = 0;
    if (w->count) {
        struct tm t;
        local_tm(w->h[0].time, w->utc_offset, &t);
        snprintf(buf, sizeof buf, LOCATION "  %s %d %s  %02d:00", DAY[t.tm_wday], t.tm_mday, MON[t.tm_mon], t.tm_hour);
    }
    header(fb, "ATMOSPHERIC CONDITIONS", buf);

    // NOW
    wx_flag_t active[16];
    int na = w->count ? wx_active_now(w, active, 16) : 0;
    cond_t top = na ? active[0].cond : COND_NOMINAL;
    gfx_tile(fb, EDGE_L, 25, top, TILE_88, na ? active[0].sev : SEV_NOTED);
    const int xr = EDGE_L + 88 + 8;
    gfx_text(fb, xr, 33, &FONT_SK8, FB_BLACK, "NOW", GFX_LEFT);
    gfx_text(fb, xr + 28, 33, &FONT_SK8, FB_BLACK, NAME[top], GFX_LEFT);
    snprintf(buf, sizeof buf, "ENV-%02d", (int)top + 1);
    gfx_text(fb, EDGE_R, 33, &FONT_SK8, FB_BLACK, buf, GFX_RIGHT);

    if (w->count) {
        const wx_hour_t *now = &w->h[0];
        deg(buf, sizeof buf, now->temp_f);
        int tw = gfx_text(fb, xr - 2, 85, &FONT_BS60, FB_BLACK, buf, GFX_LEFT);
        int hx = xr + tw + 8;
        float hi, lo;
        wx_hi_lo(w, &hi, &lo);
        gfx_text(fb, hx, 53, &FONT_SK8, FB_BLACK, "HI", GFX_LEFT);
        deg(buf, sizeof buf, hi);
        gfx_text(fb, hx + 16, 59, &FONT_BS24, FB_BLACK, buf, GFX_LEFT);
        gfx_text(fb, hx, 79, &FONT_SK8, FB_BLACK, "LO", GFX_LEFT);
        deg(buf, sizeof buf, lo);
        gfx_text(fb, hx + 16, 85, &FONT_BS24, FB_BLACK, buf, GFX_LEFT);

        const char *labels[4] = { "RH%", "GUST", "HPA", "UV" };
        const long values[4] = { lround(now->rh), lround(now->gust_mph), lround(now->pres_hpa), lround(now->uv) };
        for (int k = 0; k < 4; k++) {
            int cx = 290 + (k % 2) * 56, cy = 45 + (k / 2) * 28;
            gfx_text(fb, cx, cy, &FONT_SK8, FB_BLACK, labels[k], GFX_LEFT);
            snprintf(buf, sizeof buf, "%ld", values[k]);
            gfx_text(fb, cx, cy + 14, &FONT_SK16, FB_BLACK, buf, GFX_LEFT);
        }
    } else {
        gfx_text(fb, FB_W / 2, 180, &FONT_SK16, FB_BLACK, "NO FORECAST YET", GFX_CENTER);
        gfx_text(fb, FB_W / 2, 200, &FONT_SK8, FB_BLACK, "CHECKING WI-FI EVERY 10 MIN", GFX_CENTER);
    }
    gfx_text(fb, xr, 109, &FONT_SK8, FB_BLACK, "ACTIVE", GFX_LEFT);
    if (!na) gfx_text(fb, xr + 42, 109, &FONT_SK8, FB_BLACK, "ALL READINGS NOMINAL", GFX_LEFT);
    for (int k = 0; k < na && k < 11; k++) gfx_tile(fb, xr + 42 + k * 20, 97, active[k].cond, TILE_16, active[k].sev);

    // NEXT 06H, on six 62 px columns
    const int col = 62;
    bar(fb, 121, "NEXT 06H", "HOURLY");
    for (int i = 0; i < 6; i++) {
        int x = EDGE_L + i * col, cx = x + col / 2;
        if (i < 5) gfx_dotted_vline(fb, x + col, 131, 235, FB_BLACK);
        if (i + 1 >= w->count) continue;
        const wx_hour_t *d = &w->h[i + 1];
        wx_flag_t f[16];
        int n = wx_conditions(w, i + 1, f, 16);
        struct tm t;
        local_tm(d->time, w->utc_offset, &t);
        snprintf(buf, sizeof buf, "%02d", t.tm_hour);
        gfx_text(fb, cx, 144, &FONT_SK16, FB_BLACK, buf, GFX_CENTER);
        gfx_tile(fb, x + 7, 149, n ? f[0].cond : COND_NOMINAL, TILE_48, n ? f[0].sev : SEV_NOTED);
        deg(buf, sizeof buf, d->temp_f);
        gfx_text(fb, cx, 222, &FONT_BS24, FB_BLACK, buf, GFX_CENTER);
        snprintf(buf, sizeof buf, "%ld%%", lround(d->pop));
        gfx_text(fb, cx, 232, &FONT_SK8, FB_BLACK, buf, GFX_CENTER);
    }

    // +07-12H
    bar(fb, 242, "+07-12H", "OUTLOOK");
    for (int i = 0; i < 6; i++) {
        if (i + 7 >= w->count) break;
        const wx_hour_t *d = &w->h[i + 7];
        int x = EDGE_L + i * col;
        wx_flag_t f[16];
        int n = wx_conditions(w, i + 7, f, 16);
        gfx_tile(fb, x + 1, 251, n ? f[0].cond : COND_NOMINAL, TILE_24, n ? f[0].sev : SEV_NOTED);
        struct tm t;
        local_tm(d->time, w->utc_offset, &t);
        snprintf(buf, sizeof buf, "%02d", t.tm_hour);
        gfx_text(fb, x + 29, 257, &FONT_SK8, FB_BLACK, buf, GFX_LEFT);
        deg(buf, sizeof buf, d->temp_f);
        gfx_text(fb, x + 29, 276, &FONT_BS17, FB_BLACK, buf, GFX_LEFT);
    }

    sync_text(buf, sizeof buf, ctx, w->utc_offset, "  F MPH");
    footer(fb, ctx, buf);
}

void screen_news(uint8_t *fb, const news_t *n, int64_t now_utc, int32_t utc_offset, const screen_ctx_t *ctx) {
    char buf[64], lines[NEWS_MAX][2][128];
    int nl[NEWS_MAX];
    fb_fill(fb, FB_WHITE);

    snprintf(buf, sizeof buf, "NPR");
    if (ctx->sync_utc) {
        struct tm t;
        local_tm(ctx->sync_utc, utc_offset, &t);
        snprintf(buf, sizeof buf, "NPR  %02d:%02d", t.tm_hour, t.tm_min);
    }
    header(fb, "SYSTEM UPDATES", buf);

    // Each update: its orbit placard with the age beneath, and up to two lines
    // of headline beside it. As many as fit; spare height is shared out as
    // extra spacing so the list fills the panel.
    const int top = BAND_H + 7, bottom = EDGE_B - 15 - 6;
    const int text_x = EDGE_L + ORBIT_PX + 12, width = EDGE_R - text_x;
    const int line = 19, row_min = ORBIT_PX + 11, gap_min = 9;
    int count = 0, used = 0;
    for (int i = 0; i < n->count; i++) {
        nl[i] = gfx_wrap(&FONT_JR19, n->item[i].title, width, lines[i], 2);
        int h = nl[i] * line;
        if (h < row_min) h = row_min;
        int need = used + (count ? gap_min : 0) + h;
        if (top + need > bottom) break;
        used = need;
        count++;
    }
    if (!count) {
        gfx_text(fb, FB_W / 2, 140, &FONT_SK16, FB_BLACK, "NO UPDATES YET", GFX_CENTER);
        gfx_text(fb, FB_W / 2, 160, &FONT_SK8, FB_BLACK, "CHECKING HOURLY", GFX_CENTER);
    }
    int extra = count > 1 ? (bottom - top - used) / (count - 1) : 0;
    if (extra > 10) extra = 10;
    int y = top;
    for (int i = 0; i < count; i++) {
        const news_item_t *it = &n->item[i];
        int64_t age = it->published && now_utc > it->published ? (now_utc - it->published) / 3600 : 0;
        gfx_sprite(fb, EDGE_L, y, asset_orbit((int)age), ORBIT_PX, ORBIT_PX);
        snprintf(buf, sizeof buf, "%dH", (int)(age > 99 ? 99 : age));
        gfx_text(fb, EDGE_L + ORBIT_PX / 2, y + ORBIT_PX + 10, &FONT_SK8, age < 3 ? FB_RED : FB_BLACK, buf, GFX_CENTER);
        for (int k = 0; k < nl[i]; k++)
            gfx_text(fb, text_x, y + 12 + k * line, &FONT_JR19, FB_BLACK, lines[i][k], GFX_LEFT);
        int h = nl[i] * line;
        if (h < row_min) h = row_min;
        y += h + gap_min + extra;
        if (i + 1 < count)
            for (int x = text_x; x < EDGE_R; x += 2) fb_set(fb, x, y - (gap_min + extra) / 2 - 1, FB_BLACK);
    }

    sync_text(buf, sizeof buf, ctx, utc_offset, "");
    footer(fb, ctx, buf);
}

void screen_placeholder(uint8_t *fb, const screen_ctx_t *ctx) {
    char buf[24];
    fb_fill(fb, FB_WHITE);
    snprintf(buf, sizeof buf, "MODE %02d", ctx->mode + 1);
    header(fb, buf, "UNASSIGNED");

    fb_rect(fb, 132, 48, 136, 150, FB_BLACK);
    fb_rect(fb, 138, 54, 124, 138, FB_YELLOW);
    for (int y = 170; y < 192; y++)
        for (int x = 138; x < 262; x++)
            if ((x + y) % 12 < 5) fb_set(fb, x, y, FB_BLACK);
    snprintf(buf, sizeof buf, "%02d", ctx->mode + 1);
    gfx_text(fb, 200, 158, &FONT_BS104, FB_BLACK, buf, GFX_CENTER);
    gfx_text(fb, 200, 222, &FONT_SK8, FB_BLACK, "RESERVED SLOT", GFX_CENTER);
    gfx_text(fb, 200, 236, &FONT_SK8, FB_BLACK, "PRESS TO ADVANCE", GFX_CENTER);

    batt_text(buf, sizeof buf, ctx->batt_pct);
    footer(fb, ctx, buf);
}
