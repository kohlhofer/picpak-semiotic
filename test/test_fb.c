// SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
// SPDX-License-Identifier: AGPL-3.0-or-later
// Host test for the framebuffer: bit layout, clipping, panel row order and
// the orientation pattern. Run with `make test`.
#include "fb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static uint8_t fb[FB_BYTES], copy[FB_BYTES];

static void test_set_get_roundtrip(void) {
    const int pts[][2] = { {0, 0}, {FB_W - 1, 0}, {0, FB_H - 1}, {FB_W - 1, FB_H - 1}, {123, 45} };
    for (int c = 0; c < 4; c++) {
        fb_fill(fb, FB_WHITE);
        for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++) {
            fb_set(fb, pts[i][0], pts[i][1], (fb_color_t)c);
            CHECK(fb_get(fb, pts[i][0], pts[i][1]) == (fb_color_t)c);
        }
        CHECK(fb_get(fb, 1, 0) == FB_WHITE);
        CHECK(fb_get(fb, 0, 1) == FB_WHITE);
    }
}

static void test_bit_layout(void) {
    fb_fill(fb, FB_WHITE);
    CHECK(fb[0] == 0x55);
    fb_set(fb, 0, 0, FB_RED);       // bits 7:6
    CHECK(fb[0] == 0xD5);
    fb_set(fb, 3, 0, FB_YELLOW);    // bits 1:0
    CHECK(fb[0] == 0xD6);
    fb_set(fb, 0, 1, FB_BLACK);     // next row starts at byte 100
    CHECK(fb[FB_ROW] == 0x15);
    fb_fill(fb, FB_RED);
    CHECK(fb[FB_BYTES - 1] == 0xFF);
}

static void test_clipping(void) {
    fb_fill(fb, FB_WHITE);
    memcpy(copy, fb, FB_BYTES);
    fb_set(fb, -1, 0, FB_BLACK);
    fb_set(fb, FB_W, 0, FB_BLACK);
    fb_set(fb, 0, -1, FB_BLACK);
    fb_set(fb, 0, FB_H, FB_BLACK);
    fb_rect(fb, -50, -50, 20, 20, FB_BLACK);
    fb_rect(fb, FB_W, FB_H, 20, 20, FB_BLACK);
    CHECK(memcmp(fb, copy, FB_BYTES) == 0);
    fb_rect(fb, FB_W - 2, FB_H - 2, 20, 20, FB_BLACK);
    CHECK(fb_get(fb, FB_W - 1, FB_H - 1) == FB_BLACK);
    CHECK(fb_get(fb, FB_W - 3, FB_H - 1) == FB_WHITE);
}

static void test_panel_row_order(void) {
    CHECK(fb_panel_row(fb, 0) == fb + (FB_H - 1) * FB_ROW);
    CHECK(fb_panel_row(fb, FB_H - 1) == fb);
}

static void test_bars_pattern(void) {
    CHECK(strcmp(fb_pattern(fb, 0), "bars") == 0);
    CHECK(fb_get(fb, 50, 150) == FB_BLACK);
    CHECK(fb_get(fb, 150, 150) == FB_WHITE);
    CHECK(fb_get(fb, 250, 150) == FB_YELLOW);
    CHECK(fb_get(fb, 350, 150) == FB_RED);
    CHECK(fb_get(fb, 20, 20) == FB_WHITE);            // orientation marker
    CHECK(fb_get(fb, 20, FB_H - 20) == FB_BLACK);
    CHECK(strcmp(fb_pattern(fb, FB_PATTERN_COUNT), "bars") == 0);   // wraps
    CHECK(strcmp(fb_pattern(fb, -1), "white") == 0);
}

int main(void) {
    test_set_get_roundtrip();
    test_bit_layout();
    test_clipping();
    test_panel_row_order();
    test_bars_pattern();
    if (failures) { printf("%d check(s) failed\n", failures); return EXIT_FAILURE; }
    printf("fb: all checks passed\n");
    return EXIT_SUCCESS;
}
