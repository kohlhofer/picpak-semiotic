// SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
// SPDX-License-Identifier: AGPL-3.0-or-later
// 2 bits per pixel framebuffer for the 400x300 BWRY panel. Pure C, no ESP-IDF
// dependencies, so it builds and tests on the host.
//
// Layout: rows top to bottom, 100 bytes per row, pixel x=0 in bits 7:6 of the
// row's first byte. The panel scans bottom to top, which fb_panel_row hides.
#pragma once
#include <stdint.h>

#define FB_W      400
#define FB_H      300
#define FB_ROW    (FB_W / 4)
#define FB_BYTES  (FB_ROW * FB_H)

typedef enum { FB_BLACK = 0, FB_WHITE = 1, FB_YELLOW = 2, FB_RED = 3 } fb_color_t;

void       fb_fill(uint8_t *fb, fb_color_t c);
void       fb_set(uint8_t *fb, int x, int y, fb_color_t c);   // out of bounds is ignored
fb_color_t fb_get(const uint8_t *fb, int x, int y);
void       fb_rect(uint8_t *fb, int x, int y, int w, int h, fb_color_t c);   // clipped

// The i-th row in the order the panel expects it (i=0 is the bottom row).
const uint8_t *fb_panel_row(const uint8_t *fb, int i);

// Bring-up test patterns. n wraps modulo FB_PATTERN_COUNT.
#define FB_PATTERN_COUNT 3
const char *fb_pattern(uint8_t *fb, int n);   // draws pattern n, returns its name
