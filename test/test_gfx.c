// SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
// SPDX-License-Identifier: AGPL-3.0-or-later
// Host test for placard and text drawing against the generated assets.
#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static uint8_t fb[FB_BYTES];

static int count(fb_color_t c, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) n += fb_get(fb, x, y) == c;
    return n;
}

static void test_tiles(void) {
    CHECK(TILE_PX[TILE_16] == 16 && TILE_PX[TILE_88] == 88);
    CHECK(asset_tile(COND_COUNT, TILE_16, 0) == NULL);
    CHECK(asset_tile(COND_HEAT, TILE_SIZES, 0) == NULL);
    CHECK(asset_tile(COND_HEAT, TILE_48, 3) == NULL);

    for (int c = 0; c < COND_COUNT; c++)
        for (int s = 0; s < TILE_SIZES; s++) {
            int n = TILE_PX[s];
            fb_fill(fb, FB_WHITE);
            gfx_tile(fb, 100, 100, (cond_t)c, (tile_size_t)s, SEV_CAUTION);
            // Rounded frame: the corner pixel stays white, mid-edge is black.
            CHECK(fb_get(fb, 100, 100) == FB_WHITE);
            CHECK(fb_get(fb, 100 + n / 2, 100) == FB_BLACK);
            CHECK(fb_get(fb, 100, 100 + n / 2) == FB_BLACK);
            // Caution ground is yellow and there is a pictogram in black.
            CHECK(count(FB_YELLOW, 100, 100, 100 + n, 100 + n) > n * n / 5);
            CHECK(count(FB_BLACK, 100 + n / 4, 100 + n / 4, 100 + 3 * n / 4, 100 + 3 * n / 4) > 0);
            CHECK(count(FB_RED, 0, 0, FB_W, FB_H) == 0);
            // Nothing escapes the square.
            CHECK(count(FB_WHITE, 0, 0, FB_W, FB_H) + count(FB_YELLOW, 100, 100, 100 + n, 100 + n)
                  + count(FB_BLACK, 100, 100, 100 + n, 100 + n) == FB_W * FB_H);
        }

    fb_fill(fb, FB_WHITE);
    gfx_tile(fb, 0, 0, COND_ELEC, TILE_48, SEV_DANGER);
    CHECK(count(FB_RED, 0, 0, 48, 48) > 48 * 48 / 5);
    CHECK(count(FB_WHITE, 12, 12, 36, 36) > 0);   // danger ink is white

    fb_fill(fb, FB_WHITE);
    gfx_tile(fb, FB_W - 30, FB_H - 30, COND_NOMINAL, TILE_48, SEV_NOTED);   // runs off the edge
    CHECK(fb_get(fb, FB_W - 10, FB_H - 30) == FB_BLACK);                      // visible top edge
}

static void test_text(void) {
    CHECK(gfx_text_width(&FONT_SK8, "") == 0);
    CHECK(gfx_text_width(&FONT_BS24, "88") == 2 * gfx_text_width(&FONT_BS24, "8"));
    CHECK(gfx_text_width(&FONT_BS24, "8\xB0") > gfx_text_width(&FONT_BS24, "8"));
    // Unknown characters advance like '0' and draw nothing.
    CHECK(gfx_text_width(&FONT_BS24, "Q") == gfx_text_width(&FONT_BS24, "0"));

    fb_fill(fb, FB_WHITE);
    int w = gfx_text(fb, 200, 150, &FONT_SK8, FB_BLACK, "NOW", GFX_CENTER);
    CHECK(w > 0);
    int ink = count(FB_BLACK, 0, 0, FB_W, FB_H);
    CHECK(ink > 0);
    CHECK(ink == count(FB_BLACK, 200 - w / 2 - 1, 140, 200 + w / 2 + 2, 152));   // inside its box

    fb_fill(fb, FB_WHITE);
    gfx_text(fb, 390, 150, &FONT_SK16, FB_RED, "1004", GFX_RIGHT);
    CHECK(count(FB_RED, 391, 0, FB_W, FB_H) == 0);   // right-aligned text ends at x
    CHECK(count(FB_RED, 0, 0, FB_W, FB_H) > 0);

    fb_fill(fb, FB_WHITE);
    gfx_dotted_vline(fb, 10, 0, 10, FB_BLACK);
    CHECK(count(FB_BLACK, 0, 0, FB_W, FB_H) == 5);
}

static void test_shapes(void) {
    fb_fill(fb, FB_WHITE);
    gfx_disc(fb, 50, 50, 10, FB_BLACK);
    int n = count(FB_BLACK, 0, 0, FB_W, FB_H);
    CHECK(n > 300 && n < 330);   // pi r^2 = 314
    CHECK(fb_get(fb, 50, 50) == FB_BLACK && fb_get(fb, 50, 39) == FB_WHITE && fb_get(fb, 50, 40) == FB_BLACK);

    fb_fill(fb, FB_WHITE);
    gfx_ring(fb, 100, 100, 20, 2, FB_RED);
    CHECK(fb_get(fb, 100, 100) == FB_WHITE && fb_get(fb, 100, 81) == FB_RED && fb_get(fb, 100, 84) == FB_WHITE);

    fb_fill(fb, FB_WHITE);
    gfx_line(fb, 10, 10.5f, 30, 10.5f, 3, FB_BLACK);
    CHECK(count(FB_BLACK, 0, 9, FB_W, 12) >= 20 * 3 && count(FB_BLACK, 0, 0, FB_W, 9) == 0);
    gfx_line(fb, -50, -50, 500, 500, 2, FB_BLACK);   // clipped, no crash

    fb_fill(fb, FB_WHITE);
    gfx_tri(fb, 0, 0, 10, 0, 0, 10, FB_YELLOW);
    gfx_tri(fb, 20, 0, 20, 10, 30, 0, FB_YELLOW);   // either winding
    n = count(FB_YELLOW, 0, 0, FB_W, FB_H);
    CHECK(n >= 90 && n <= 120);

    CHECK(PICTO_PX[PI_MAN] == 44 && PICTO_PX[PI_MOON] == 22);
    CHECK(asset_picto(PI_COUNT, 0, 0) == NULL && asset_picto(PI_SAT, 0, 4) == NULL);
    fb_fill(fb, FB_WHITE);
    gfx_sprite(fb, 0, 0, asset_picto(PI_DOG, 0, 3), 44, 44);
    CHECK(count(FB_BLACK, 0, 0, 44, 44) > count(FB_WHITE, 0, 0, 44, 44));   // dormant: black ground
    fb_fill(fb, FB_WHITE);
    gfx_sprite(fb, 0, 0, asset_picto(PI_MOON, 0, 0), 22, 22);
    int dark = count(FB_BLACK, 0, 0, 22, 22);
    gfx_sprite(fb, 0, 0, asset_picto(PI_MOON, 4, 0), 22, 22);
    CHECK(count(FB_BLACK, 0, 0, 22, 22) > dark + 60);   // a full moon has far more ink than a new one
}

int main(void) {
    test_tiles();
    test_text();
    test_shapes();
    if (failures) { printf("%d check(s) failed\n", failures); return EXIT_FAILURE; }
    printf("gfx: all checks passed\n");
    return EXIT_SUCCESS;
}
