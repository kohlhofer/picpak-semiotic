// SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
// SPDX-License-Identifier: AGPL-3.0-or-later
// Drawing on the framebuffer with the generated placards and fonts. Pure C,
// so screens render the same on the Mac as on the panel.
#pragma once
#include "assets.h"
#include "fb.h"

typedef enum { GFX_LEFT, GFX_CENTER, GFX_RIGHT } gfx_align_t;

// Placard with its top-left corner at (x, y).
void gfx_tile(uint8_t *fb, int x, int y, cond_t c, tile_size_t s, sev_t sev);

// Any 2 bpp sprite, w by h, top-left at (x, y).
void gfx_sprite(uint8_t *fb, int x, int y, const uint8_t *px, int w, int h);
// The same, leaving pixels of one colour untouched (a placard's white corners on a dark plate).
void gfx_sprite_key(uint8_t *fb, int x, int y, const uint8_t *px, int w, int h, fb_color_t key);

// Up to max_lines lines of s that fit width, broken at spaces (a word longer
// than the width is cut). If text is left over, the last line ends in "...".
// Each line is copied into lines[i] (cap bytes each). Returns the line count.
int gfx_wrap(const font_t *f, const char *s, int width, char lines[][128], int max_lines);

// Text with its baseline at y. Characters the font lacks advance like a space
// the width of '0'. Returns the width drawn.
int gfx_text(uint8_t *fb, int x, int y, const font_t *f, fb_color_t color, const char *s, gfx_align_t align);
int gfx_text_width(const font_t *f, const char *s);

// Filled rectangle with corners of radius r (pixel centres inside the arc).
void gfx_rrect(uint8_t *fb, int x, int y, int w, int h, int r, fb_color_t color);

// Trims s in place until it fits width. Returns its final width.
int gfx_fit(const font_t *f, char *s, int width);

// Shapes tested at pixel centres, so they look the same at any position.
// Filled disc of radius r centred at (cx, cy).
void gfx_disc(uint8_t *fb, float cx, float cy, float r, fb_color_t color);
// Ring whose outer edge is r, w pixels thick.
void gfx_ring(uint8_t *fb, float cx, float cy, float r, float w, fb_color_t color);
// Line w pixels wide with round ends.
void gfx_line(uint8_t *fb, float x0, float y0, float x1, float y1, float w, fb_color_t color);
// Filled triangle.
void gfx_tri(uint8_t *fb, float x0, float y0, float x1, float y1, float x2, float y2, fb_color_t color);

// Every other pixel from y0 up to, not including, y1.
void gfx_dotted_vline(uint8_t *fb, int x, int y0, int y1, fb_color_t color);
