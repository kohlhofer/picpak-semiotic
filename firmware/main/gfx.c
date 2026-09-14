// SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gfx.h"

#include <math.h>
#include <string.h>

static void sprite(uint8_t *fb, int x, int y, const uint8_t *px, int w, int h, int key) {
    if (!px) return;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int k = j * w + i, c = (px[k >> 2] >> (6 - 2 * (k & 3))) & 3;
            if (c != key) fb_set(fb, x + i, y + j, (fb_color_t)c);
        }
}

void gfx_sprite(uint8_t *fb, int x, int y, const uint8_t *px, int w, int h) { sprite(fb, x, y, px, w, h, -1); }

void gfx_sprite_key(uint8_t *fb, int x, int y, const uint8_t *px, int w, int h, fb_color_t key) {
    sprite(fb, x, y, px, w, h, (int)key);
}

void gfx_tile(uint8_t *fb, int x, int y, cond_t c, tile_size_t s, sev_t sev) {
    gfx_sprite(fb, x, y, asset_tile(c, s, (uint8_t)sev), TILE_PX[s], TILE_PX[s]);
}

static int width_n(const font_t *f, const char *s, size_t n) {
    char buf[128];
    if (n >= sizeof buf) n = sizeof buf - 1;
    memcpy(buf, s, n);
    buf[n] = 0;
    return gfx_text_width(f, buf);
}

int gfx_wrap(const font_t *f, const char *s, int width, char lines[][128], int max_lines) {
    int count = 0;
    while (*s == ' ') s++;
    while (*s && count < max_lines) {
        // Longest run of whole words that fits.
        size_t fit = 0, i = 0;
        while (s[i]) {
            size_t j = i;
            while (s[j] == ' ') j++;
            size_t k = j;
            while (s[k] && s[k] != ' ') k++;
            if (k > 127 || width_n(f, s, k) > width) break;
            fit = k;
            i = k;
        }
        if (fit == 0) {   // a single word wider than the line: cut it
            while (s[fit] && fit < 127 && width_n(f, s, fit + 1) <= width) fit++;
            if (fit == 0) fit = 1;
        }
        memcpy(lines[count], s, fit);
        lines[count][fit] = 0;
        count++;
        s += fit;
        while (*s == ' ') s++;
    }
    if (*s && count) {
        char *last = lines[count - 1];
        size_t n = strlen(last);
        int dots = gfx_text_width(f, "...");
        while (n > 0 && (n > 124 || width_n(f, last, n) + dots > width)) {
            // Drop the last word, or a character when only one word is left.
            size_t sp = n;
            while (sp > 0 && last[sp - 1] != ' ') sp--;
            if (sp > 0) {
                n = sp - 1;
                while (n > 0 && last[n - 1] == ' ') n--;
            } else {
                n--;
            }
        }
        memcpy(last + n, "...", 4);
    }
    return count;
}

static const glyph_t *find(const font_t *f, char ch) {
    for (int i = 0; i < f->count; i++)
        if (f->chars[i] == ch) return &f->glyphs[i];
    return 0;
}

static int advance(const font_t *f, char ch) {
    const glyph_t *g = find(f, ch);
    if (g) return g->adv;
    const glyph_t *zero = find(f, '0');
    return zero ? zero->adv : 0;
}

int gfx_text_width(const font_t *f, const char *s) {
    int w = 0;
    for (; *s; s++) w += advance(f, *s);
    return w;
}

int gfx_text(uint8_t *fb, int x, int y, const font_t *f, fb_color_t color, const char *s, gfx_align_t align) {
    int w = gfx_text_width(f, s);
    int pen = align == GFX_CENTER ? x - w / 2 : align == GFX_RIGHT ? x - w : x;
    for (; *s; s++) {
        const glyph_t *g = find(f, *s);
        if (g) {
            for (int j = 0; j < g->h; j++)
                for (int i = 0; i < g->w; i++) {
                    int k = j * g->w + i;
                    if (f->bits[g->off + (k >> 3)] & (0x80 >> (k & 7)))
                        fb_set(fb, pen + g->xo + i, y + g->yo + j, color);
                }
        }
        pen += advance(f, *s);
    }
    return w;
}

void gfx_rrect(uint8_t *fb, int x, int y, int w, int h, int r, fb_color_t color) {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            // Distance from the nearest corner centre, measured at pixel centres.
            float cx = i < r ? r - (i + 0.5f) : i >= w - r ? (i + 0.5f) - (w - r) : 0;
            float cy = j < r ? r - (j + 0.5f) : j >= h - r ? (j + 0.5f) - (h - r) : 0;
            if (cx > 0 && cy > 0 && cx * cx + cy * cy > (float)(r * r)) continue;
            fb_set(fb, x + i, y + j, color);
        }
    }
}

int gfx_fit(const font_t *f, char *s, int width) {
    size_t n = strlen(s);
    while (n && gfx_text_width(f, s) > width) s[--n] = 0;
    while (n && s[n - 1] == ' ') s[--n] = 0;
    return gfx_text_width(f, s);
}

void gfx_dotted_vline(uint8_t *fb, int x, int y0, int y1, fb_color_t color) {
    for (int y = y0; y < y1; y += 2) fb_set(fb, x, y, color);
}

static void span(int *lo, int *hi, float a, float b, int max) {
    *lo = (int)floor(a);
    *hi = (int)ceil(b);
    if (*lo < 0) *lo = 0;
    if (*hi > max) *hi = max;
}

void gfx_disc(uint8_t *fb, float cx, float cy, float r, fb_color_t color) { gfx_ring(fb, cx, cy, r, r + 1, color); }

void gfx_ring(uint8_t *fb, float cx, float cy, float r, float w, fb_color_t color) {
    int x0, x1, y0, y1;
    span(&x0, &x1, cx - r, cx + r, FB_W);
    span(&y0, &y1, cy - r, cy + r, FB_H);
    float in = r - w < 0 ? 0 : (r - w) * (r - w);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = dx * dx + dy * dy;
            if (d <= r * r && d >= in) fb_set(fb, x, y, color);
        }
}

void gfx_line(uint8_t *fb, float ax, float ay, float bx, float by, float w, fb_color_t color) {
    float h = w / 2;
    int x0, x1, y0, y1;
    span(&x0, &x1, fmin(ax, bx) - h, fmax(ax, bx) + h, FB_W);
    span(&y0, &y1, fmin(ay, by) - h, fmax(ay, by) + h, FB_H);
    float vx = bx - ax, vy = by - ay, len2 = vx * vx + vy * vy;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f - ax, py = y + 0.5f - ay;
            float t = len2 > 0 ? (px * vx + py * vy) / len2 : 0;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            float dx = px - t * vx, dy = py - t * vy;
            if (dx * dx + dy * dy <= h * h) fb_set(fb, x, y, color);
        }
}

void gfx_tri(uint8_t *fb, float ax, float ay, float bx, float by, float cx, float cy, fb_color_t color) {
    int x0, x1, y0, y1;
    span(&x0, &x1, fmin(ax, fmin(bx, cx)), fmax(ax, fmax(bx, cx)), FB_W);
    span(&y0, &y1, fmin(ay, fmin(by, cy)), fmax(ay, fmax(by, cy)), FB_H);
    float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (area == 0) return;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float e0 = ((bx - ax) * (py - ay) - (by - ay) * (px - ax)) / area;
            float e1 = ((cx - bx) * (py - by) - (cy - by) * (px - bx)) / area;
            float e2 = ((ax - cx) * (py - cy) - (ay - cy) * (px - cx)) / area;
            if (e0 >= 0 && e1 >= 0 && e2 >= 0) fb_set(fb, x, y, color);
        }
}
