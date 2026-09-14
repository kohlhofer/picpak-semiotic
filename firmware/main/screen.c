#include "screen.h"
#include "batt.h"
#include "gfx.h"
#include "sched.h"
#include "sgp4.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
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


static void local_tm(int64_t utc, int32_t offset, struct tm *out) {
    time_t t = (time_t)(utc + offset);
    gmtime_r(&t, out);
}

// The small pixel fonts carry capitals only.
static void upper(char *s) {
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 'a' + 'A');
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
        snprintf(buf, sizeof buf, "%s  %s %d %s  %02d:00", ctx->place ? ctx->place : "", DAY[t.tm_wday], t.tm_mday, MON[t.tm_mon], t.tm_hour);
        upper(buf);
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

    snprintf(buf, sizeof buf, "%s", ctx->feed ? ctx->feed : "HEADLINES");
    upper(buf);
    if (ctx->sync_utc) {
        struct tm t;
        local_tm(ctx->sync_utc, utc_offset, &t);
        snprintf(buf, sizeof buf, "%s  %02d:%02d", ctx->feed ? ctx->feed : "HEADLINES", t.tm_hour, t.tm_min);
        upper(buf);
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

void screen_status(uint8_t *fb, const status_t *s, const char *device, const char *wake, const char *reset,
                   const screen_ctx_t *ctx) {
    char buf[64];
    fb_fill(fb, FB_WHITE);
    if (s->now) {
        struct tm t;
        local_tm(s->now, s->utc_offset, &t);
        snprintf(buf, sizeof buf, "%s  %02d:%02d", device, t.tm_hour, t.tm_min);
    } else {
        snprintf(buf, sizeof buf, "%s", device);
    }
    header(fb, "SYSTEM STATUS", buf);

    readout_t cell = status_cell(s), rows[8];
    int n = status_rows(s, rows, 8);
    sev_t verdict = status_verdict(&cell, rows, n);
    const int top = BAND_H + 7, bottom = EDGE_B - 15 - 6;

    // Power cell: a tall gauge of ten segments with its terminal on top.
    const int gx = EDGE_L, gw = 70, gy = top + 8, gh = bottom - gy - 32;
    gfx_rrect(fb, gx + 22, gy - 6, 26, 10, 3, FB_BLACK);
    gfx_rrect(fb, gx, gy, gw, gh, 8, FB_BLACK);
    gfx_rrect(fb, gx + 3, gy + 3, gw - 6, gh - 6, 6, FB_WHITE);
    int pct = batt_pct(s->batt_mv);
    const int segs = 10, sh = (gh - 12) / segs;
    int lit = pct < 0 ? 0 : (pct + 5) / 10;
    for (int i = 0; i < segs; i++) {
        int sy = gy + gh - 6 - (i + 1) * sh + 2;
        fb_rect(fb, gx + 8, sy, gw - 16, sh - 3, FB_BLACK);
        fb_color_t fill = i >= lit ? FB_WHITE : lit <= 2 ? FB_RED : FB_YELLOW;
        fb_rect(fb, gx + 10, sy + 2, gw - 20, sh - 7, fill);
    }
    gfx_text(fb, gx + gw / 2, bottom - 4, &FONT_BS24, cell.sev == SEV_DANGER ? FB_RED : FB_BLACK, cell.value, GFX_CENTER);

    // Verdict: black when all is well, yellow for caution, red for attention.
    const int rx = gx + gw + 14, vw = EDGE_R - rx;
    static const char *WORD[] = { "NOMINAL", "CAUTION", "ATTENTION" };
    fb_color_t vfill = verdict == SEV_DANGER ? FB_RED : verdict == SEV_CAUTION ? FB_YELLOW : FB_BLACK;
    fb_color_t vink = verdict == SEV_CAUTION ? FB_BLACK : FB_WHITE;
    gfx_rrect(fb, rx, top, vw, 30, 6, FB_BLACK);
    gfx_rrect(fb, rx + 2, top + 2, vw - 4, 26, 5, vfill);
    gfx_text(fb, rx + 10, top + 26, &FONT_JR37, vink, WORD[verdict], GFX_LEFT);
    gfx_text(fb, EDGE_R - 10, top + 19, &FONT_SK8, vink, cell.sub, GFX_RIGHT);

    // Readouts
    int y = top + 42;
    for (int i = 0; i < n; i++) {
        if (y + 24 > bottom) break;
        const readout_t *r = &rows[i];
        gfx_sprite(fb, rx, y, asset_status(r->icon, r->level, (uint8_t)r->sev), STATUS_PX, STATUS_PX);
        gfx_text(fb, rx + 30, y + 9, &FONT_SK8, FB_BLACK, r->label, GFX_LEFT);
        int vw2 = gfx_text(fb, EDGE_R, y + 17, &FONT_BS20, r->sev == SEV_DANGER ? FB_RED : FB_BLACK, r->value, GFX_RIGHT);
        char sub[32];
        snprintf(sub, sizeof sub, "%s", r->sub);
        gfx_fit(&FONT_SK8, sub, EDGE_R - vw2 - 8 - (rx + 30));
        gfx_text(fb, rx + 30, y + 21, &FONT_SK8, FB_BLACK, sub, GFX_LEFT);
        y += 30;
        if (y + 24 <= bottom)
            for (int x = rx + 30; x < EDGE_R; x += 2) fb_set(fb, x, y - 4, FB_BLACK);
    }

    snprintf(buf, sizeof buf, "WOKE %s  RST %s", wake, reset);
    footer(fb, ctx, buf);
}

static int chip(uint8_t *fb, int x, int y, fb_color_t fill, fb_color_t ink, const char *text, gfx_align_t align) {
    int w = gfx_text_width(&FONT_SK8, text) + 12, h = 14;
    int x0 = align == GFX_RIGHT ? x - w : x;
    gfx_rrect(fb, x0, y, w, h, 4, fill == FB_WHITE ? FB_BLACK : fill);
    if (fill == FB_WHITE) gfx_rrect(fb, x0 + 1, y + 1, w - 2, h - 2, 3, FB_WHITE);
    gfx_text(fb, x0 + 6, y + 11, &FONT_SK8, ink, text, GFX_LEFT);
    return w;
}

static void thousands(char *buf, size_t n, long v) {
    char raw[24];
    snprintf(raw, sizeof raw, "%ld", v);
    size_t len = strlen(raw), o = 0;
    for (size_t i = 0; i < len && o + 1 < n; i++) {
        if (i && (len - i) % 3 == 0 && o + 1 < n) buf[o++] = ',';
        buf[o++] = raw[i];
    }
    buf[o] = 0;
}

static void hhmm(char *buf, size_t n, int64_t utc, int32_t offset) {
    struct tm t;
    local_tm(utc, offset, &t);
    snprintf(buf, n, "%02d:%02d", t.tm_hour, t.tm_min);
}

void screen_orbit(uint8_t *fb, const orbit_t *o, const site_t *site, int64_t now, int32_t off, const screen_ctx_t *ctx) {
    char buf[64], tbuf[8];
    fb_fill(fb, FB_WHITE);
    // After a reset the clock stays unset until a fetch reads a server's Date.
    bool clock = sched_time_valid(now);
    hhmm(tbuf, sizeof tbuf, now, off);
    snprintf(buf, sizeof buf, "%s %.1f%c %.1f%c", ctx->place ? ctx->place : "", fabs(site->lat), site->lat < 0 ? 'S' : 'N',
             fabs(site->lon), site->lon < 0 ? 'W' : 'E');
    if (clock) snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "  %s", tbuf);
    upper(buf);
    header(fb, "ORBITAL TRACKING", buf);
    const int top = BAND_H + 7;

    // Sky scope, north up. Elevation 90 at the centre, the horizon at the rim.
    const int R0 = 80;
    const float cx = EDGE_L + R0 + 1.5f, cy = top + R0 + 3.5f;
    gfx_ring(fb, cx, cy, R0, 2, FB_BLACK);
    gfx_ring(fb, cx, cy, R0 * 2 / 3.0f, 1, FB_BLACK);
    gfx_ring(fb, cx, cy, R0 / 3.0f, 1, FB_BLACK);
    for (int a = 0; a < 360; a += 30) {
        float sx = sin(a * M_PI / 180), cz = cos(a * M_PI / 180);
        gfx_line(fb, cx + sx * (R0 - 7), cy - cz * (R0 - 7), cx + sx * (R0 - 1), cy - cz * (R0 - 1), 2, FB_BLACK);
    }
    int icx = (int)cx, icy = (int)cy;
    for (int i = -R0 + 8; i <= R0 - 8; i += 3) {
        fb_set(fb, icx + i, icy, FB_BLACK);
        fb_set(fb, icx, icy + i, FB_BLACK);
    }
    static const struct { const char *t; int dx, dy; } CARD[] = { { "N", 0, -R0 + 16 }, { "E", R0 - 14, 4 }, { "S", 0, R0 - 10 }, { "W", -R0 + 14, 4 } };
    for (int i = 0; i < 4; i++) {
        fb_rect(fb, icx + CARD[i].dx - 5, icy + CARD[i].dy - 9, 11, 11, FB_WHITE);
        gfx_text(fb, icx + CARD[i].dx + 1, icy + CARD[i].dy, &FONT_SK8, FB_BLACK, CARD[i].t, GFX_CENTER);
    }
    gfx_text(fb, icx + 4, icy - R0 * 2 / 3 + 10, &FONT_SK8, FB_BLACK, "30", GFX_LEFT);
    gfx_text(fb, icx + 4, icy - R0 / 3 + 10, &FONT_SK8, FB_BLACK, "60", GFX_LEFT);

    if (!clock) {
        fb_rect(fb, icx - 4, icy - 4, 9, 9, FB_BLACK);
        fb_rect(fb, icx - 2, icy - 2, 5, 5, FB_YELLOW);
        const int rx = 196;
        gfx_sprite(fb, rx, top + 2, asset_picto(PI_SAT, 0, 0), 36, 36);
        gfx_text(fb, rx + 44, top + 14, &FONT_SK8, FB_BLACK, "ORBITAL INTERCEPT", GFX_LEFT);
        gfx_text(fb, rx + 44, top + 26, &FONT_SK8, FB_BLACK, "ISS  STANDBY", GFX_LEFT);
        gfx_text(fb, rx, top + 80, &FONT_JR37, FB_BLACK, "NO CLOCK", GFX_LEFT);
        chip(fb, rx, top + 104, FB_YELLOW, FB_BLACK, "AWAITING TIME SYNC", GFX_LEFT);
        for (int x = rx; x < EDGE_R; x += 2) fb_set(fb, x, top + 128, FB_BLACK);
        gfx_sprite(fb, rx, top + 134, asset_picto(PI_ROCK, 0, 0), 36, 36);
        gfx_text(fb, rx + 44, top + 145, &FONT_SK8, FB_BLACK, "ANOMALY TRACKING", GFX_LEFT);
        gfx_text(fb, rx + 44, top + 157, &FONT_SK8, FB_BLACK, o->neo_ok ? o->neo.des : "STANDBY", GFX_LEFT);
        footer(fb, ctx, "CLOCK SETS ON NEXT WI-FI FETCH");
        return;
    }

    const pass_t *p = orbit_next_pass(o, now);
    if (p) {
        float px[PASS_ARC], py[PASS_ARC];
        for (int i = 0; i < PASS_ARC; i++) {
            float el = p->el[i] / 10.0f, az = p->az[i] / 10.0f * (float)M_PI / 180;
            float rr = (R0 - 3) * (1 - (el < 0 ? 0 : el) / 90);
            px[i] = cx + rr * sin(az);
            py[i] = cy - rr * cos(az);
        }
        for (int i = 1; i < PASS_ARC; i++) gfx_line(fb, px[i - 1], py[i - 1], px[i], py[i], 4, FB_RED);
        float ex = px[PASS_ARC - 1], ey = py[PASS_ARC - 1];
        float ang = atan2(ey - py[PASS_ARC - 3], ex - px[PASS_ARC - 3]);
        gfx_tri(fb, ex + cos(ang) * 7, ey + sin(ang) * 7, ex + cos(ang + 2.4f) * 7, ey + sin(ang + 2.4f) * 7,
                ex + cos(ang - 2.4f) * 7, ey + sin(ang - 2.4f) * 7, FB_RED);
        int sx = (int)px[0], sy = (int)py[0];
        fb_rect(fb, sx - 3, sy - 3, 7, 7, FB_BLACK);
        fb_rect(fb, sx - 1, sy - 1, 3, 3, FB_WHITE);
        hhmm(tbuf, sizeof tbuf, p->rise, off);
        if (sx <= icx) gfx_text(fb, sx + 7, sy + 13, &FONT_SK8, FB_BLACK, tbuf, GFX_LEFT);
        else gfx_text(fb, sx - 7, sy + 13, &FONT_SK8, FB_BLACK, tbuf, GFX_RIGHT);
    }
    fb_rect(fb, icx - 4, icy - 4, 9, 9, FB_BLACK);
    fb_rect(fb, icx - 2, icy - 2, 5, 5, FB_YELLOW);

    // Sun and Moon beneath the scope.
    struct tm lt;
    local_tm(now, off, &lt);
    int64_t day_start = now - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);
    int64_t rise, set;
    sky_sun_times(site, day_start, &rise, &set);
    double frac, elong;
    sky_moon((double)now, &frac, &elong);
    int y1 = icy + R0 + 8;
    gfx_sprite(fb, EDGE_L, y1, asset_picto(PI_SUN, 0, 0), 22, 22);
    gfx_text(fb, EDGE_L + 30, y1 + 9, &FONT_SK8, FB_BLACK, "SOLAR", GFX_LEFT);
    char r1[8] = "--:--", s1[8] = "--:--";
    if (rise) hhmm(r1, sizeof r1, rise, off);
    if (set) hhmm(s1, sizeof s1, set, off);
    snprintf(buf, sizeof buf, "RISE %s  SET %s", r1, s1);
    gfx_text(fb, EDGE_L + 30, y1 + 21, &FONT_SK8, FB_BLACK, buf, GFX_LEFT);
    int y2 = y1 + 32;
    gfx_sprite(fb, EDGE_L, y2, asset_picto(PI_MOON, orbit_moon_level(elong), 0), 22, 22);
    snprintf(buf, sizeof buf, "LUNAR  %d%%", (int)lround(frac * 100));
    gfx_text(fb, EDGE_L + 30, y2 + 9, &FONT_SK8, FB_BLACK, buf, GFX_LEFT);
    gfx_text(fb, EDGE_L + 30, y2 + 21, &FONT_SK8, FB_BLACK, orbit_moon_name(frac, elong), GFX_LEFT);

    // Intercept
    const int rx = 196;
    bool soon = p && now >= p->rise - 15 * 60;
    gfx_sprite(fb, rx, top + 2, asset_picto(PI_SAT, 0, soon ? 2 : 0), 36, 36);
    gfx_text(fb, rx + 44, top + 14, &FONT_SK8, FB_BLACK, "ORBITAL INTERCEPT", GFX_LEFT);
    gfx_text(fb, rx + 44, top + 26, &FONT_SK8, FB_BLACK, p && p->visible ? "ISS  VISIBLE PASS" : "ISS  PASS", GFX_LEFT);
    if (p) {
        snprintf(buf, sizeof buf, "%d MIN", (int)((p->set - p->rise + 30) / 60));
        gfx_text(fb, EDGE_R, top + 14, &FONT_SK8, FB_BLACK, buf, GFX_RIGHT);
        hhmm(tbuf, sizeof tbuf, p->rise, off);
        int tw = gfx_text(fb, rx - 2, top + 96, &FONT_BS60, soon ? FB_RED : FB_BLACK, tbuf, GFX_LEFT);
        int vx = rx - 2 + tw + 10;
        gfx_text(fb, vx, top + 54, &FONT_SK8, FB_BLACK, "MAX EL", GFX_LEFT);
        snprintf(buf, sizeof buf, "%d\260", (p->max_el + 5) / 10);
        gfx_text(fb, vx, top + 78, &FONT_JR37, FB_BLACK, buf, GFX_LEFT);
        snprintf(buf, sizeof buf, "%s>%s", orbit_compass(p->rise_az), orbit_compass(p->set_az));
        gfx_text(fb, vx, top + 95, &FONT_JR19, FB_BLACK, buf, GFX_LEFT);

        struct tm pt;
        local_tm(p->rise, off, &pt);
        int days = (int)(((p->rise + off) / 86400) - ((now + off) / 86400));
        if (now >= p->rise) chip(fb, rx, top + 104, FB_RED, FB_WHITE, "INTERCEPT IN PROGRESS", GFX_LEFT);
        else if (soon) {
            snprintf(buf, sizeof buf, "INTERCEPT IMMINENT  T-%d MIN", (int)((p->rise - now + 59) / 60));
            chip(fb, rx, top + 104, FB_RED, FB_WHITE, buf, GFX_LEFT);
        } else {
            const char *when = days == 0 ? (pt.tm_hour >= 17 ? "TONIGHT" : "TODAY") : days == 1 ? "TOMORROW" : DAY[pt.tm_wday];
            snprintf(buf, sizeof buf, "%s %s", p->visible ? "VISIBLE" : "OVERHEAD", when);
            chip(fb, rx, top + 104, FB_YELLOW, FB_BLACK, buf, GFX_LEFT);
        }
    } else {
        gfx_text(fb, rx - 2, top + 96, &FONT_BS60, FB_BLACK, "--:--", GFX_LEFT);
        chip(fb, rx, top + 104, FB_WHITE, FB_BLACK, o->l1[0] ? "NO VISIBLE PASS IN 3 DAYS" : "AWAITING ELEMENTS", GFX_LEFT);
    }
    for (int x = rx; x < EDGE_R; x += 2) fb_set(fb, x, top + 128, FB_BLACK);

    // Anomaly
    const int ay = top + 134;
    if (o->neo_ok) {
        const neo_t *n = &o->neo;
        double ld = orbit_ld(n->dist_au);
        gfx_sprite(fb, rx, ay, asset_picto(PI_ROCK, 0, ld < 1.0 ? 1 : 0), 36, 36);
        gfx_text(fb, rx + 44, ay + 11, &FONT_SK8, FB_BLACK, "ANOMALY TRACKING", GFX_LEFT);
        snprintf(buf, sizeof buf, "%s", n->des);
        gfx_fit(&FONT_JR37, buf, EDGE_R - rx - 44);
        gfx_text(fb, rx + 44, ay + 34, &FONT_JR37, FB_BLACK, buf, GFX_LEFT);

        const float bx0 = rx + 6, bx1 = EDGE_R - 4, by = ay + 50.5f;
        gfx_disc(fb, bx0, by, 5, FB_BLACK);
        fb_rect(fb, (int)bx0, (int)by, (int)(bx1 - bx0), 1, FB_BLACK);
        int mx = (int)(bx0 + sqrt(1.0 / 4) * (bx1 - bx0));
        fb_rect(fb, mx, (int)by - 5, 1, 11, FB_BLACK);
        gfx_disc(fb, mx + 0.5f, by - 8.5f, 2.5f, FB_BLACK);
        gfx_text(fb, mx + 5, (int)by - 4, &FONT_SK8, FB_BLACK, "MOON", GFX_LEFT);
        float ox = bx0 + sqrt((ld > 4 ? 4 : ld) / 4) * (bx1 - bx0);
        gfx_tri(fb, ox - 5, by, ox + 5, by - 5.5f, ox + 5, by + 5.5f, FB_RED);

        char mi[16], mph[16];
        thousands(mi, sizeof mi, lround(orbit_miles(n->dist_au) / 100) * 100);
        thousands(mph, sizeof mph, lround(orbit_mph(n->v_kms) / 100) * 100);
        snprintf(buf, sizeof buf, "RANGE %s MI  %.2f LD", mi, ld);
        gfx_text(fb, rx, ay + 72, &FONT_SK8, FB_BLACK, buf, GFX_LEFT);
        int lo, hi;
        orbit_size_ft(n->h, &lo, &hi);
        snprintf(buf, sizeof buf, "SPEED %s MPH  SIZE %d-%d FT", mph, (lo + 5) / 10 * 10, (hi + 5) / 10 * 10);
        gfx_fit(&FONT_SK8, buf, EDGE_R - rx);
        gfx_text(fb, rx, ay + 84, &FONT_SK8, FB_BLACK, buf, GFX_LEFT);
        struct tm at;
        local_tm(n->approach, off, &at);
        hhmm(tbuf, sizeof tbuf, n->approach, off);
        if (n->approach < now) snprintf(buf, sizeof buf, "CLOSEST PASSED %s %02d %s", DAY[at.tm_wday], at.tm_mday, MON[at.tm_mon]);
        else snprintf(buf, sizeof buf, "CLOSEST %s %02d %s %s", DAY[at.tm_wday], at.tm_mday, MON[at.tm_mon], tbuf);
        gfx_text(fb, rx, ay + 96, &FONT_SK8, FB_BLACK, buf, GFX_LEFT);
    } else {
        gfx_sprite(fb, rx, ay, asset_picto(PI_ROCK, 0, 0), 36, 36);
        gfx_text(fb, rx + 44, ay + 11, &FONT_SK8, FB_BLACK, "ANOMALY TRACKING", GFX_LEFT);
        gfx_text(fb, rx + 44, ay + 26, &FONT_SK8, FB_BLACK, "AWAITING JPL DATA", GFX_LEFT);
    }

    sgp4_t sat;
    if (o->l1[0] && sgp4_parse(o->l1, o->l2, &sat)) {
        struct tm et;
        local_tm((int64_t)sat.epoch, 0, &et);
        snprintf(buf, sizeof buf, "TLE %02d %s  JPL CAD", et.tm_mday, MON[et.tm_mon]);
    } else {
        snprintf(buf, sizeof buf, "CELESTRAK  JPL CAD");
    }
    footer(fb, ctx, buf);
}

void screen_crew(uint8_t *fb, const crew_t *c, int64_t now, int32_t off, const screen_ctx_t *ctx) {
    char buf[160];
    fb_fill(fb, FB_WHITE);
    struct tm t;
    local_tm(now, off, &t);
    int minute = t.tm_hour * 60 + t.tm_min, yday = t.tm_yday + 1;
    bool clock = sched_time_valid(now);   // no schedule or order without it
    if (clock) snprintf(buf, sizeof buf, "%s  %s %02d %s  %02d:%02d", c->ship, DAY[t.tm_wday], t.tm_mday, MON[t.tm_mon], t.tm_hour, t.tm_min);
    else snprintf(buf, sizeof buf, "%s", c->ship);
    upper(buf);
    header(fb, "CREW MANIFEST", buf);
    const int top = BAND_H + 7, bottom = EDGE_B - 21;

    int n = c->count > CREW_MAX ? CREW_MAX : c->count, aboard = 0, away = 0, sleeping = 0;
    const int gap = 5, ch = 126;
    int cw = n ? (EDGE_R - EDGE_L - (n - 1) * gap) / n : 0;
    for (int i = 0; i < n; i++) {
        const crew_member_t *m = &c->member[i];
        crew_state_t s = clock ? crew_state(c, i, minute, t.tm_wday, yday) : (crew_state_t){ CREW_ON_DECK, "STANDBY", 0 };
        if (s.kind == CREW_AWAY) away++; else aboard++;
        if (s.kind == CREW_ASLEEP) sleeping++;
        int x = EDGE_L + i * (cw + gap), y = top;
        gfx_rrect(fb, x, y, cw, ch, 6, FB_BLACK);
        gfx_rrect(fb, x + 1, y + 1, cw - 2, ch - 2, 5, FB_WHITE);
        gfx_rrect(fb, x, y, cw, 14, 6, FB_BLACK);
        fb_rect(fb, x, y + 7, cw, 7, FB_BLACK);
        snprintf(buf, sizeof buf, "%02d", i + 1);
        gfx_text(fb, x + 6, y + 10, &FONT_SK8, FB_WHITE, buf, GFX_LEFT);
        int px = PICTO_PX[m->figure];
        gfx_sprite(fb, x + (cw - px) / 2, y + 18, asset_picto(m->figure, 0, s.sev), px, px);
        snprintf(buf, sizeof buf, "%s", m->name);
        upper(buf);
        gfx_fit(&FONT_JR19, buf, cw - 6);
        gfx_text(fb, x + cw / 2, y + 84, &FONT_JR19, FB_BLACK, buf, GFX_CENTER);
        snprintf(buf, sizeof buf, "%s", m->rank);
        upper(buf);
        gfx_fit(&FONT_SK8, buf, cw - 6);
        gfx_text(fb, x + cw / 2, y + 97, &FONT_SK8, FB_BLACK, buf, GFX_CENTER);
        fb_color_t fill = s.kind == CREW_ASLEEP ? FB_BLACK : s.kind == CREW_AWAY ? FB_YELLOW : FB_WHITE;
        int wy = y + ch - 20;
        gfx_rrect(fb, x + 4, wy, cw - 8, 15, 4, FB_BLACK);
        if (fill != FB_BLACK) gfx_rrect(fb, x + 5, wy + 1, cw - 10, 13, 3, fill);
        snprintf(buf, sizeof buf, "%s", s.word);
        upper(buf);
        gfx_fit(&FONT_SK8, buf, cw - 12);
        gfx_text(fb, x + cw / 2, wy + 11, &FONT_SK8, fill == FB_BLACK ? FB_WHITE : FB_BLACK, buf, GFX_CENTER);
    }

    // The day's Special Order on a black plate.
    const int py = top + ch + 8, ph = bottom - py;
    gfx_rrect(fb, EDGE_L, py, EDGE_R - EDGE_L, ph, 6, FB_BLACK);
    gfx_sprite_key(fb, EDGE_L + 8, py + 8, asset_picto(PI_ORDER, 0, 1), 22, 22, FB_WHITE);
    if (clock) snprintf(buf, sizeof buf, "SPECIAL ORDER %03d", yday);
    else snprintf(buf, sizeof buf, "SPECIAL ORDER ---");
    gfx_text(fb, EDGE_L + 38, py + 17, &FONT_SK8, FB_YELLOW, buf, GFX_LEFT);
    snprintf(buf, sizeof buf, "%s", c->company);
    upper(buf);
    gfx_text(fb, EDGE_R - 10, py + 17, &FONT_SK8, FB_WHITE, buf, GFX_RIGHT);
    char order[200], lines[4][128];
    if (clock) {
        crew_order(c, yday, order, sizeof order);
        upper(order);
    } else {
        snprintf(order, sizeof order, "SHIP'S CLOCK NOT SET. SPECIAL ORDERS RESUME AFTER THE NEXT SUCCESSFUL WI-FI FETCH. STAND BY.");
    }
    int nl = gfx_wrap(&FONT_JR19, order, EDGE_R - EDGE_L - 20, lines, 4);
    // Centred in the space under the title row.
    int ty = py + 30 + (ph - 30 - nl * 18) / 2 + 13;
    for (int i = 0; i < nl; i++) gfx_text(fb, EDGE_L + 10, ty + i * 18, &FONT_JR19, FB_WHITE, lines[i], GFX_LEFT);

    if (!clock) snprintf(buf, sizeof buf, "CREW %d  CLOCK NOT SET", n);
    else if (sleeping) snprintf(buf, sizeof buf, "ABOARD %d  HYPERSLEEP %d", aboard, sleeping);
    else snprintf(buf, sizeof buf, "ABOARD %d  AWAY %d", aboard, away);
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
