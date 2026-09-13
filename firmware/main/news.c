#include "news.h"
#include "sched.h"

#include <stdio.h>
#include <string.h>

static const char *find(const char *hay, const char *end, const char *needle) {
    size_t n = strlen(needle);
    for (const char *p = hay; p + n <= end; p++)
        if (*p == *needle && memcmp(p, needle, n) == 0) return p;
    return NULL;
}

// Text between <tag> and </tag> inside [from, end), or false.
static bool element(const char *from, const char *end, const char *tag, const char **s, size_t *len) {
    char open[24], close[24];
    snprintf(open, sizeof open, "<%s>", tag);
    snprintf(close, sizeof close, "</%s>", tag);
    const char *a = find(from, end, open);
    if (!a) return false;
    a += strlen(open);
    const char *b = find(a, end, close);
    if (!b) return false;
    *s = a;
    *len = (size_t)(b - a);
    return true;
}

// Latin-1 letters U+00C0 to U+00FF with their accents removed.
static const char LATIN1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";

static const char *plain(uint32_t cp, char *one) {
    switch (cp) {
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return "'";
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: return "\"";
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: return "-";
    case 0x2026: return "...";
    case 0x00A0: case 0x2002: case 0x2003: case 0x2009: return " ";
    case 0x00DF: return "ss";
    default: break;
    }
    if (cp >= 0x20 && cp < 0x7F) { one[0] = (char)cp; one[1] = 0; return one; }
    if (cp == '\t' || cp == '\n' || cp == '\r') return " ";
    if (cp >= 0xC0 && cp <= 0xFF) { one[0] = LATIN1[cp - 0xC0]; one[1] = 0; return one; }
    return "?";
}

static uint32_t utf8(const unsigned char **p, const unsigned char *end) {
    const unsigned char *s = *p;
    uint32_t cp = *s;
    int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;
    if (extra) cp &= (uint32_t)(0x3F >> extra);
    if (s + extra >= end + (extra ? 0 : 1)) { *p = end; return '?'; }
    for (int i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0) != 0x80) { *p = s + 1; return '?'; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *p = s + 1 + extra;
    return cp;
}

static uint32_t entity(const char **p, const char *end) {
    static const struct { const char *name; uint32_t cp; } E[] = {
        { "amp;", '&' }, { "lt;", '<' }, { "gt;", '>' }, { "quot;", '"' }, { "apos;", '\'' }, { "nbsp;", 0xA0 },
    };
    const char *s = *p + 1;   // past '&'
    for (size_t i = 0; i < sizeof E / sizeof E[0]; i++) {
        size_t n = strlen(E[i].name);
        if (s + n <= end && memcmp(s, E[i].name, n) == 0) { *p = s + n; return E[i].cp; }
    }
    if (s < end && *s == '#') {
        uint32_t v = 0;
        const char *q = s + 1;
        bool hex = q < end && (*q == 'x' || *q == 'X');
        if (hex) q++;
        const char *digits = q;
        for (; q < end && *q != ';'; q++) {
            int d = *q >= '0' && *q <= '9' ? *q - '0'
                  : hex && *q >= 'a' && *q <= 'f' ? *q - 'a' + 10
                  : hex && *q >= 'A' && *q <= 'F' ? *q - 'A' + 10 : -1;
            if (d < 0 || q - digits > 7) break;
            v = v * (hex ? 16 : 10) + (uint32_t)d;
        }
        if (q < end && *q == ';' && q > digits) { *p = q + 1; return v; }
    }
    *p = *p + 1;
    return '&';
}

size_t news_clean(const char *in, size_t len, char *out, size_t cap) {
    if (!cap) return 0;
    const char *p = in, *end = in + len;
    // Trim and unwrap one CDATA section.
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
    bool cdata = end - p >= 12 && memcmp(p, "<![CDATA[", 9) == 0;
    if (cdata) {
        const char *close = find(p + 9, end, "]]>");
        if (close) { p += 9; end = close; }
        else cdata = false;
    }
    size_t n = 0;
    bool space = true;   // swallows leading spaces
    while (p < end) {
        uint32_t cp;
        if (!cdata && *p == '&') cp = entity(&p, end);
        else if ((unsigned char)*p >= 0x80) { const unsigned char *u = (const unsigned char *)p; cp = utf8(&u, (const unsigned char *)end); p = (const char *)u; }
        else cp = (unsigned char)*p++;
        char one[2];
        const char *s = plain(cp, one);
        for (; *s; s++) {
            if (*s == ' ') { if (space) continue; space = true; }
            else space = false;
            if (n + 1 >= cap) { out[n] = 0; return n; }
            out[n++] = *s;
        }
    }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
    return n;
}

bool news_parse(const char *xml, size_t len, news_t *out) {
    const char *p = xml, *end = xml + len;
    *out = (news_t){ 0 };
    while (out->count < NEWS_MAX) {
        const char *a = find(p, end, "<item>");
        if (!a) break;
        const char *b = find(a, end, "</item>");
        if (!b) break;
        const char *s;
        size_t n;
        news_item_t *it = &out->item[out->count];
        if (element(a, b, "title", &s, &n) && news_clean(s, n, it->title, sizeof it->title) > 0) {
            char date[48];
            it->published = 0;
            if (element(a, b, "pubDate", &s, &n) && n < sizeof date) {
                memcpy(date, s, n);
                date[n] = 0;
                if (!sched_rfc822_date(date, &it->published)) it->published = 0;
            }
            out->count++;
        }
        p = b + 7;
    }
    return out->count > 0;
}
