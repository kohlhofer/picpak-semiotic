// Full-screen layouts. Pure C: the preview tool renders them on the Mac.
#pragma once
#include <stdint.h>
#include "news.h"
#include "status.h"
#include "wx.h"

typedef struct {
    int mode;           // 0-based mode on screen
    int modes;          // how many modes exist
    int batt_pct;       // -1 when unknown
    int64_t sync_utc;   // when the forecast was fetched, 0 when never
} screen_ctx_t;

// Mode 1: the environmental panel.
void screen_env(uint8_t *fb, const wx_t *w, const screen_ctx_t *ctx);

// Mode 2: System Updates, headlines with orbit placards. Ages are measured
// against now_utc; utc_offset formats the header and sync times.
void screen_news(uint8_t *fb, const news_t *n, int64_t now_utc, int32_t utc_offset, const screen_ctx_t *ctx);

// Mode 3: System Status, the console layout. device is the short name for the
// header; wake and reset label how the board came up.
void screen_status(uint8_t *fb, const status_t *s, const char *device, const char *wake, const char *reset,
                   const screen_ctx_t *ctx);

// Any mode that has no screen yet.
void screen_placeholder(uint8_t *fb, const screen_ctx_t *ctx);
