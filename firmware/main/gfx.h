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

// Up to max_lines lines of s that fit width, broken at spaces (a word longer
// than the width is cut). If text is left over, the last line ends in "...".
// Each line is copied into lines[i] (cap bytes each). Returns the line count.
int gfx_wrap(const font_t *f, const char *s, int width, char lines[][128], int max_lines);

// Text with its baseline at y. Characters the font lacks advance like a space
// the width of '0'. Returns the width drawn.
int gfx_text(uint8_t *fb, int x, int y, const font_t *f, fb_color_t color, const char *s, gfx_align_t align);
int gfx_text_width(const font_t *f, const char *s);

// Every other pixel from y0 up to, not including, y1.
void gfx_dotted_vline(uint8_t *fb, int x, int y0, int y1, fb_color_t color);
