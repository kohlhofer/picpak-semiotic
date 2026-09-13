#include "screen.h"
#include "gfx.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

// Layout follows the approved design page (PP-ENV-01) pixel for pixel.

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

static void deg(char *buf, size_t n, float f) { snprintf(buf, n, "%ld\xB0", lround(f)); }

static void bar(uint8_t *fb, int y, const char *label, const char *right) {
    fb_rect(fb, 8, y, 384, 2, FB_BLACK);
    int w = gfx_text_width(&FONT_SK8, label);
    fb_rect(fb, 8, y - 6, w + 10, 14, FB_BLACK);
    gfx_text(fb, 13, y + 4, &FONT_SK8, FB_WHITE, label, GFX_LEFT);
    int rw = gfx_text_width(&FONT_SK8, right);
    fb_rect(fb, 392 - rw - 6, y - 5, rw + 6, 12, FB_WHITE);
    gfx_text(fb, 392, y + 4, &FONT_SK8, FB_BLACK, right, GFX_RIGHT);
}

// The case hides the outer few pixels of the panel. Backgrounds such as the
// header band may run to the edge; text and marks stay SAFE px inside.
// Picked from the design review (5 px) until the on-device edge check is read.
#define SAFE      5
#define BAND_H    (18 + SAFE / 2)
#define BAND_BASE (12 + SAFE / 2)

static void header(uint8_t *fb, int x0, int x1, const char *left, const char *right) {
    fb_rect(fb, 0, 0, FB_W, BAND_H, FB_BLACK);
    gfx_text(fb, x0, BAND_BASE, &FONT_SK8, FB_WHITE, left, GFX_LEFT);
    if (right) gfx_text(fb, x1, BAND_BASE, &FONT_SK8, FB_WHITE, right, GFX_RIGHT);
}

static void footer(uint8_t *fb, const screen_ctx_t *ctx, const char *left, int x0, int x1) {
    const int y = FB_H - SAFE;
    fb_rect(fb, x0, y - 14, x1 - x0, 1, FB_BLACK);
    gfx_text(fb, x0, y - 1, &FONT_SK8, FB_BLACK, left, GFX_LEFT);
    const int s = 11, gap = 3;
    int x = x1 - (ctx->modes * s + (ctx->modes - 1) * gap);
    for (int k = 0; k < ctx->modes; k++) {
        bool cur = k == ctx->mode;
        fb_rect(fb, x, y - 10, s, s, FB_BLACK);
        if (!cur) fb_rect(fb, x + 1, y - 9, s - 2, s - 2, FB_WHITE);
        char digit[2] = { (char)('1' + k), 0 };
        gfx_text(fb, x + 6, y - 1, &FONT_SK8, cur ? FB_WHITE : FB_BLACK, digit, GFX_CENTER);
        x += s + gap;
    }
}

static void batt_text(char *buf, size_t n, int pct) {
    if (pct < 0) snprintf(buf, n, "BATT --");
    else snprintf(buf, n, "BATT %d%%", pct);
}

void screen_env(uint8_t *fb, const wx_t *w, const screen_ctx_t *ctx) {
    char buf[48], buf2[24];
    fb_fill(fb, FB_WHITE);

    // Header
    buf[0] = 0;
    if (w->count) {
        struct tm t;
        local_tm(w->h[0].time, w->utc_offset, &t);
        snprintf(buf, sizeof buf, LOCATION "  %s %d %s  %02d:00", DAY[t.tm_wday], t.tm_mday, MON[t.tm_mon], t.tm_hour);
    }
    header(fb, 8, 392, "ATMOSPHERIC CONDITIONS", buf);

    // NOW
    wx_flag_t active[16];
    int na = w->count ? wx_active_now(w, active, 16) : 0;
    cond_t top = na ? active[0].cond : COND_NOMINAL;
    sev_t top_sev = na ? active[0].sev : SEV_NOTED;
    gfx_tile(fb, 8, 24, top, TILE_88, top_sev);
    gfx_text(fb, 104, 32, &FONT_SK8, FB_BLACK, "NOW", GFX_LEFT);
    gfx_text(fb, 132, 32, &FONT_SK8, FB_BLACK, NAME[top], GFX_LEFT);
    snprintf(buf, sizeof buf, "ENV-%02d", (int)top + 1);
    gfx_text(fb, 392, 32, &FONT_SK8, FB_BLACK, buf, GFX_RIGHT);

    if (w->count) {
        const wx_hour_t *now = &w->h[0];
        deg(buf, sizeof buf, now->temp_f);
        int tw = gfx_text(fb, 102, 86, &FONT_BS60, FB_BLACK, buf, GFX_LEFT);
        int hx = 104 + tw + 10;
        float hi, lo;
        wx_hi_lo(w, &hi, &lo);
        gfx_text(fb, hx, 54, &FONT_SK8, FB_BLACK, "HI", GFX_LEFT);
        deg(buf, sizeof buf, hi);
        gfx_text(fb, hx + 16, 60, &FONT_BS24, FB_BLACK, buf, GFX_LEFT);
        gfx_text(fb, hx, 80, &FONT_SK8, FB_BLACK, "LO", GFX_LEFT);
        deg(buf, sizeof buf, lo);
        gfx_text(fb, hx + 16, 86, &FONT_BS24, FB_BLACK, buf, GFX_LEFT);

        const char *labels[4] = { "RH%", "GUST", "HPA", "UV" };
        const long values[4] = { lround(now->rh), lround(now->gust_mph), lround(now->pres_hpa), lround(now->uv) };
        for (int k = 0; k < 4; k++) {
            int cx = 292 + (k % 2) * 60, cy = 46 + (k / 2) * 28;
            gfx_text(fb, cx, cy, &FONT_SK8, FB_BLACK, labels[k], GFX_LEFT);
            snprintf(buf, sizeof buf, "%ld", values[k]);
            gfx_text(fb, cx, cy + 14, &FONT_SK16, FB_BLACK, buf, GFX_LEFT);
        }
    }
    if (!w->count) {
        gfx_text(fb, 200, 180, &FONT_SK16, FB_BLACK, "NO FORECAST YET", GFX_CENTER);
        gfx_text(fb, 200, 200, &FONT_SK8, FB_BLACK, "CHECKING WI-FI EVERY 10 MIN", GFX_CENTER);
    }
    gfx_text(fb, 104, 110, &FONT_SK8, FB_BLACK, "ACTIVE", GFX_LEFT);
    if (!na) gfx_text(fb, 146, 110, &FONT_SK8, FB_BLACK, "ALL READINGS NOMINAL", GFX_LEFT);
    for (int k = 0; k < na && k < 11; k++) gfx_tile(fb, 146 + k * 20, 98, active[k].cond, TILE_16, active[k].sev);

    // NEXT 06H
    bar(fb, 122, "NEXT 06H", "HOURLY");
    for (int i = 0; i < 6; i++) {
        int x = 8 + i * 64, cx = x + 32;
        if (i < 5) gfx_dotted_vline(fb, x + 64, 132, 236, FB_BLACK);
        if (i + 1 >= w->count) continue;
        const wx_hour_t *d = &w->h[i + 1];
        wx_flag_t f[16];
        int n = wx_conditions(w, i + 1, f, 16);
        struct tm t;
        local_tm(d->time, w->utc_offset, &t);
        snprintf(buf, sizeof buf, "%02d", t.tm_hour);
        gfx_text(fb, cx, 145, &FONT_SK16, FB_BLACK, buf, GFX_CENTER);
        gfx_tile(fb, x + 8, 150, n ? f[0].cond : COND_NOMINAL, TILE_48, n ? f[0].sev : SEV_NOTED);
        deg(buf, sizeof buf, d->temp_f);
        gfx_text(fb, cx, 224, &FONT_BS24, FB_BLACK, buf, GFX_CENTER);
        snprintf(buf, sizeof buf, "%ld%%", lround(d->pop));
        gfx_text(fb, cx, 234, &FONT_SK8, FB_BLACK, buf, GFX_CENTER);
    }

    // +07-12H
    bar(fb, 244, "+07-12H", "OUTLOOK");
    for (int i = 0; i < 6; i++) {
        if (i + 7 >= w->count) break;
        const wx_hour_t *d = &w->h[i + 7];
        int x = 8 + i * 64;
        wx_flag_t f[16];
        int n = wx_conditions(w, i + 7, f, 16);
        gfx_tile(fb, x + 2, 253, n ? f[0].cond : COND_NOMINAL, TILE_24, n ? f[0].sev : SEV_NOTED);
        struct tm t;
        local_tm(d->time, w->utc_offset, &t);
        snprintf(buf, sizeof buf, "%02d", t.tm_hour);
        gfx_text(fb, x + 30, 259, &FONT_SK8, FB_BLACK, buf, GFX_LEFT);
        deg(buf, sizeof buf, d->temp_f);
        gfx_text(fb, x + 30, 278, &FONT_BS17, FB_BLACK, buf, GFX_LEFT);
    }

    // Footer
    batt_text(buf2, sizeof buf2, ctx->batt_pct);
    if (ctx->sync_utc) {
        struct tm t;
        local_tm(ctx->sync_utc, w->utc_offset, &t);
        snprintf(buf, sizeof buf, "SYNC %02d:%02d  %s  F MPH", t.tm_hour, t.tm_min, buf2);
    } else {
        snprintf(buf, sizeof buf, "NO SYNC  %s  F MPH", buf2);
    }
    footer(fb, ctx, buf, 8, 392);
}

// Pixel bounds of a string's ink, relative to its pen position and baseline.
static void ink_box(const font_t *f, const char *s, int *x0, int *y0, int *x1) {
    *x0 = 1000; *y0 = 1000; *x1 = -1000;
    int pen = 0;
    for (; *s; s++) {
        for (int i = 0; i < f->count; i++) {
            if (f->chars[i] != *s) continue;
            const glyph_t *g = &f->glyphs[i];
            if (g->w) {
                if (pen + g->xo < *x0) *x0 = pen + g->xo;
                if (pen + g->xo + g->w > *x1) *x1 = pen + g->xo + g->w;
                if (g->yo < *y0) *y0 = g->yo;
            }
            pen += g->adv;
        }
    }
}

void screen_edge_check(uint8_t *fb, const screen_ctx_t *ctx) {
    (void)ctx;
    char n[4];
    int x0, y0, x1;
    fb_fill(fb, FB_WHITE);
    for (int k = 0; k < 10; k++) {
        int inset = k * 2;
        snprintf(n, sizeof n, "%d", inset);
        ink_box(&FONT_SK16, n, &x0, &y0, &x1);
        int w = x1 - x0;
        // Top and bottom: the number's top (or bottom) edge sits `inset` px in.
        int tx = 44 + k * 34;
        gfx_text(fb, tx - x0, inset - y0, &FONT_SK16, FB_BLACK, n, GFX_LEFT);
        gfx_text(fb, tx - x0, FB_H - 1 - inset, &FONT_SK16, FB_RED, n, GFX_LEFT);
        // Left and right: the number's left (or right) edge sits `inset` px in.
        int ly = 46 + k * 24;
        gfx_text(fb, inset - x0, ly, &FONT_SK16, FB_RED, n, GFX_LEFT);
        gfx_text(fb, FB_W - inset - w - x0, ly, &FONT_SK16, FB_BLACK, n, GFX_LEFT);
    }
    gfx_text(fb, 200, 132, &FONT_SK8, FB_RED, "EDGE CHECK", GFX_CENTER);
    gfx_text(fb, 200, 150, &FONT_SK8, FB_BLACK, "ON EACH SIDE, THE SMALLEST NUMBER", GFX_CENTER);
    gfx_text(fb, 200, 162, &FONT_SK8, FB_BLACK, "YOU CAN SEE WHOLE IS HOW MANY", GFX_CENTER);
    gfx_text(fb, 200, 174, &FONT_SK8, FB_BLACK, "PIXELS THE CASE HIDES", GFX_CENTER);
}

void screen_placeholder(uint8_t *fb, const screen_ctx_t *ctx) {
    char buf[24];
    fb_fill(fb, FB_WHITE);
    snprintf(buf, sizeof buf, "MODE %02d", ctx->mode + 1);
    header(fb, 8, 392, buf, "UNASSIGNED");

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
    footer(fb, ctx, buf, 8, 392);
}

void screen_news(uint8_t *fb, const news_t *n, int64_t now_utc, int32_t utc_offset, const screen_ctx_t *ctx) {
    char buf[64], batt[24], lines[2][128];
    const int m = SAFE;
    fb_fill(fb, FB_WHITE);

    snprintf(buf, sizeof buf, "NPR");
    if (ctx->sync_utc) {
        struct tm t;
        local_tm(ctx->sync_utc, utc_offset, &t);
        snprintf(buf, sizeof buf, "NPR  %s %d %s  %02d:%02d", DAY[t.tm_wday], t.tm_mday, MON[t.tm_mon], t.tm_hour, t.tm_min);
    }
    header(fb, m, FB_W - m, "SYSTEM UPDATES", buf);

    const int top = BAND_H + 6, bottom = FB_H - SAFE - 14 - 4;
    const int gap = 8, text_x = m + ORBIT_PX + 10, width = FB_W - m - text_x;
    const int line = 13, size = 11;   // Pixelify Sans 11 px
    if (!n->count) {
        gfx_text(fb, FB_W / 2, 140, &FONT_SK16, FB_BLACK, "NO UPDATES YET", GFX_CENTER);
        gfx_text(fb, FB_W / 2, 160, &FONT_SK8, FB_BLACK, "CHECKING HOURLY", GFX_CENTER);
    }
    int y = top;
    for (int i = 0; i < n->count; i++) {
        const news_item_t *it = &n->item[i];
        int nl = gfx_wrap(&FONT_PX11, it->title, width, lines, 2);
        int h = 10 + nl * line + 2;
        if (h < ORBIT_PX) h = ORBIT_PX;
        if (y + h > bottom) break;
        int64_t age = it->published && now_utc > it->published ? (now_utc - it->published) / 3600 : 0;
        gfx_sprite(fb, m, y, asset_orbit((int)age), ORBIT_PX, ORBIT_PX);
        snprintf(buf, sizeof buf, "UPD-%02d", i + 1);
        gfx_text(fb, text_x, y + 7, &FONT_SK8, FB_BLACK, buf, GFX_LEFT);
        snprintf(buf, sizeof buf, "T-%02dH", (int)(age > 99 ? 99 : age));
        gfx_text(fb, FB_W - m, y + 7, &FONT_SK8, FB_BLACK, buf, GFX_RIGHT);
        for (int k = 0; k < nl; k++)
            gfx_text(fb, text_x, y + 9 + line * (k + 1) - (line - size), &FONT_PX11, FB_BLACK, lines[k], GFX_LEFT);
        y += h + gap;
        if (y < bottom)
            for (int x = text_x; x < FB_W - m; x += 2) fb_set(fb, x, y - gap / 2 - 1, FB_BLACK);
    }

    batt_text(batt, sizeof batt, ctx->batt_pct);
    if (ctx->sync_utc) {
        struct tm t;
        local_tm(ctx->sync_utc, utc_offset, &t);
        snprintf(buf, sizeof buf, "SYNC %02d:%02d  %s", t.tm_hour, t.tm_min, batt);
    } else {
        snprintf(buf, sizeof buf, "NO SYNC  %s", batt);
    }
    footer(fb, ctx, buf, m, FB_W - m);
}
