// Host test for the RSS reader, text cleaning, date parsing and line wrapping.
#include "gfx.h"
#include "news.h"
#include "sched.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void clean_is(const char *in, const char *want) {
    char out[NEWS_TITLE];
    news_clean(in, strlen(in), out, sizeof out);
    if (strcmp(out, want) != 0) { printf("FAIL clean(%s) = [%s], want [%s]\n", in, out, want); failures++; }
}

static void test_clean(void) {
    clean_is("Trump&apos;s $5,000 &amp; &quot;more&quot; &lt;b&gt;", "Trump's $5,000 & \"more\" <b>");
    clean_is("  <![CDATA[Inside & raw]]>  ", "Inside & raw");
    clean_is("ISIS \xE2\x80\x94 and how", "ISIS - and how");
    clean_is("\xE2\x80\x9CQuoted\xE2\x80\x9D \xE2\x80\x98too\xE2\x80\x99\xE2\x80\xA6", "\"Quoted\" 'too'...");
    clean_is("Caf\xC3\xA9 in S\xC3\xA3o Paulo, Stra\xC3\x9F" "e", "Cafe in Sao Paulo, Strasse");
    clean_is("&#8217;numeric&#x2014;hex&#65;", "'numeric-hexA");
    clean_is("a\n\t  b   c ", "a b c");
    clean_is("emoji \xF0\x9F\x9A\x80 here", "emoji ? here");
    clean_is("broken \xE2\x80", "broken ?");
    clean_is("&unknown; &#;", "&unknown; &#;");

    char small[8];
    CHECK(news_clean("abcdefghijk", 11, small, sizeof small) == 7);
    CHECK(strcmp(small, "abcdefg") == 0);
}

static void test_dates(void) {
    int64_t t;
    CHECK(sched_rfc822_date("Sun, 13 Sep 2026 13:45:33 -0400", &t) && t == 1789321533);
    CHECK(sched_rfc822_date("Sun, 13 Sep 2026 17:45:33 GMT", &t) && t == 1789321533);
    CHECK(sched_rfc822_date("Sun, 13 Sep 2026 23:15:33 +0530", &t) && t == 1789321533);
    CHECK(!sched_rfc822_date("Sun, 13 Sep 2026 13:45:33 EDT", &t));
    CHECK(!sched_rfc822_date("Sun, 13 Sep 2026 13:45:33 -04x0", &t));
    CHECK(!sched_http_date("Sun, 13 Sep 2026 13:45:33 -0400", &t));   // HTTP dates are always GMT
}

static void test_fixture(const char *path) {
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (!f) return;
    static char buf[65536];
    size_t len = fread(buf, 1, sizeof buf, f);
    fclose(f);
    static news_t n;
    CHECK(news_parse(buf, len, &n));
    CHECK(n.count == NEWS_MAX);   // the feed has 10
    CHECK(strcmp(n.item[0].title, "Wisconsin congressman survives emergency plane landing, swims to safety") == 0);
    CHECK(n.item[0].published == 1789321533);
    CHECK(strcmp(n.item[2].title, "Politics chat: Trump's $5,000 promise to voters, Vance invokes Charlie Kirk") == 0);
    CHECK(strcmp(n.item[6].title, "How a young woman was recruited to ISIS - and how she eventually escaped") == 0);
    for (int i = 1; i < n.count; i++) CHECK(n.item[i].published <= n.item[i - 1].published);

    news_t none;
    CHECK(!news_parse("<rss><channel></channel></rss>", 30, &none));
    const char *untitled = "<item><pubDate>Sun, 13 Sep 2026 13:45:33 -0400</pubDate></item><item><title>Kept</title></item>";
    CHECK(news_parse(untitled, strlen(untitled), &none) && none.count == 1 && strcmp(none.item[0].title, "Kept") == 0);
    CHECK(none.item[0].published == 0);
    const char *cut = "<item><title>Never closed";
    CHECK(!news_parse(cut, strlen(cut), &none));
}

static void test_wrap(void) {
    char lines[2][128];
    int one = gfx_text_width(&FONT_PX16, "Wisconsin congressman");
    int n = gfx_wrap(&FONT_PX16, "Wisconsin congressman survives emergency plane landing, swims to safety", one + 2, lines, 2);
    CHECK(n == 2);
    CHECK(strcmp(lines[0], "Wisconsin congressman") == 0);
    CHECK(strlen(lines[1]) > 3 && strcmp(lines[1] + strlen(lines[1]) - 3, "...") == 0);
    CHECK(gfx_text_width(&FONT_PX16, lines[1]) <= one + 2);

    n = gfx_wrap(&FONT_PX16, "Short headline", 300, lines, 2);
    CHECK(n == 1 && strcmp(lines[0], "Short headline") == 0);

    // A word wider than the line is cut rather than lost.
    n = gfx_wrap(&FONT_PX16, "Supercalifragilistic", 30, lines, 2);
    CHECK(n == 2);
    CHECK(gfx_text_width(&FONT_PX16, lines[0]) <= 30 && strlen(lines[0]) > 0);

    CHECK(gfx_wrap(&FONT_PX16, "", 100, lines, 2) == 0);
    CHECK(gfx_wrap(&FONT_PX16, "   ", 100, lines, 2) == 0);
}

int main(int argc, char **argv) {
    test_clean();
    test_dates();
    test_fixture(argc > 1 ? argv[1] : "test/fixtures/npr-news-2026-09-13.xml");
    test_wrap();
    if (failures) { printf("%d check(s) failed\n", failures); return EXIT_FAILURE; }
    printf("news: all checks passed\n");
    return EXIT_SUCCESS;
}
