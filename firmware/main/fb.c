#include "fb.h"
#include <string.h>

void fb_fill(uint8_t *fb, fb_color_t c) {
    memset(fb, c * 0x55, FB_BYTES);   // 0x55 repeats the 2-bit value in all four slots
}

void fb_set(uint8_t *fb, int x, int y, fb_color_t c) {
    if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return;
    uint8_t *b = &fb[y * FB_ROW + x / 4];
    int shift = 6 - 2 * (x & 3);
    *b = (uint8_t)((*b & ~(3 << shift)) | (c << shift));
}

fb_color_t fb_get(const uint8_t *fb, int x, int y) {
    return (fb_color_t)((fb[y * FB_ROW + x / 4] >> (6 - 2 * (x & 3))) & 3);
}

void fb_rect(uint8_t *fb, int x, int y, int w, int h, fb_color_t c) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > FB_W) x1 = FB_W;
    if (y1 > FB_H) y1 = FB_H;
    for (int j = y; j < y1; j++)
        for (int i = x; i < x1; i++) fb_set(fb, i, j, c);
}

const uint8_t *fb_panel_row(const uint8_t *fb, int i) {
    return &fb[(FB_H - 1 - i) * FB_ROW];
}

// Four vertical bars with a white square in the top-left corner. The bar order
// and the square's position together show any flip or rotation.
static void pattern_bars(uint8_t *fb) {
    static const fb_color_t order[4] = { FB_BLACK, FB_WHITE, FB_YELLOW, FB_RED };
    for (int k = 0; k < 4; k++) fb_rect(fb, k * FB_W / 4, 0, FB_W / 4, FB_H, order[k]);
    fb_rect(fb, 10, 10, 40, 40, FB_WHITE);
}

// Quadrants of fine detail: 1px black/white checker, 1px red/yellow checker,
// 10px black/red checker, and concentric 1px squares. A 1px black frame runs
// around the screen edge to catch any offset.
static void pattern_detail(uint8_t *fb) {
    const int hw = FB_W / 2, hh = FB_H / 2;
    fb_fill(fb, FB_WHITE);
    for (int y = 0; y < hh; y++) {
        for (int x = 0; x < hw; x++) {
            int odd = (x + y) & 1;
            fb_set(fb, x, y, odd ? FB_BLACK : FB_WHITE);
            fb_set(fb, hw + x, y, odd ? FB_RED : FB_YELLOW);
            fb_set(fb, x, hh + y, ((x / 10 + y / 10) & 1) ? FB_BLACK : FB_RED);
        }
    }
    for (int d = 8; d < hh / 2; d += 8) {
        fb_rect(fb, hw + d, hh + d, hw - 2 * d, 1, FB_BLACK);
        fb_rect(fb, hw + d, FB_H - d - 1, hw - 2 * d, 1, FB_BLACK);
        fb_rect(fb, hw + d, hh + d, 1, hh - 2 * d, FB_BLACK);
        fb_rect(fb, FB_W - d - 1, hh + d, 1, hh - 2 * d, FB_BLACK);
    }
    fb_rect(fb, 0, 0, FB_W, 1, FB_BLACK);
    fb_rect(fb, 0, FB_H - 1, FB_W, 1, FB_BLACK);
    fb_rect(fb, 0, 0, 1, FB_H, FB_BLACK);
    fb_rect(fb, FB_W - 1, 0, 1, FB_H, FB_BLACK);
}

const char *fb_pattern(uint8_t *fb, int n) {
    switch (((n % FB_PATTERN_COUNT) + FB_PATTERN_COUNT) % FB_PATTERN_COUNT) {
    case 0:  pattern_bars(fb);   return "bars";
    case 1:  pattern_detail(fb); return "detail";
    default: fb_fill(fb, FB_WHITE); return "white";
    }
}
