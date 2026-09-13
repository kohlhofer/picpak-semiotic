// Headlines for System Updates, read from an RSS feed. Pure C.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEWS_URL   "https://feeds.npr.org/1001/rss.xml"
#define NEWS_MAX   8
#define NEWS_TITLE 120   // bytes including the terminator

typedef struct {
    int64_t published;   // unix seconds, 0 when the feed gave no usable date
    char title[NEWS_TITLE];
} news_item_t;

typedef struct {
    uint8_t count;
    news_item_t item[NEWS_MAX];
} news_t;

// The first NEWS_MAX <item>s that have a title, in feed order. False when none.
bool news_parse(const char *xml, size_t len, news_t *out);

// Feed text to plain ASCII for the bitmap fonts: unwraps CDATA, decodes XML
// entities, maps typographic quotes, dashes and accented letters to plain
// ones, collapses whitespace, and truncates to fit. Returns the length written.
size_t news_clean(const char *in, size_t len, char *out, size_t cap);
