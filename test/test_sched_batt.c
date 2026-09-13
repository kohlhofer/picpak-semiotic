// Host test for the HTTP date parser, fetch schedule and battery curve.
#include "batt.h"
#include "sched.h"
#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_http_date(void) {
    int64_t t = -1;
    CHECK(sched_http_date("Sun, 13 Sep 2026 18:35:27 GMT", &t) && t == 1789324527);
    CHECK(sched_http_date("Thu, 01 Jan 1970 00:00:00 GMT", &t) && t == 0);
    CHECK(sched_http_date("Tue, 29 Feb 2028 23:59:59 GMT", &t) && t == 1835481599);
    CHECK(!sched_http_date("13 Sep 2026 18:35:27", &t));
    CHECK(!sched_http_date("Sun, 13 Sp 2026 18:35:27 GMT", &t));
    CHECK(!sched_http_date("Sun, 13 anF 2026 18:35:27 GMT", &t));   // found in the table, but mid-name
    CHECK(!sched_http_date("Sun, 13 Sep 2026 25:35:27 GMT", &t));
    CHECK(!sched_http_date("", &t));
    CHECK(!sched_http_date(NULL, &t));
}

static void test_schedule(void) {
    const int64_t h14 = 1789322400;   // 14:00 EDT
    CHECK(sched_next_fetch(h14) == h14 + 3600 + 90);
    CHECK(sched_next_fetch(h14 + 95) == h14 + 3600 + 90);
    CHECK(sched_next_fetch(h14 + 3599) == h14 + 3600 + 90);
    CHECK(!sched_time_valid(12345));
    CHECK(sched_time_valid(h14));
}

static void test_battery(void) {
    CHECK(batt_pct(-1) == -1);
    CHECK(batt_pct(0) == -1);
    CHECK(batt_pct(3000) == 0);
    CHECK(batt_pct(3400) == 0);
    CHECK(batt_pct(4180) == 100);
    CHECK(batt_pct(4300) == 100);
    CHECK(batt_pct(3800) == 48);
    int prev = 0;
    for (int mv = 3400; mv <= 4200; mv += 5) {
        int p = batt_pct(mv);
        CHECK(p >= prev && p <= 100);
        prev = p;
    }
}

int main(void) {
    test_http_date();
    test_schedule();
    test_battery();
    if (failures) { printf("%d check(s) failed\n", failures); return EXIT_FAILURE; }
    printf("sched+batt: all checks passed\n");
    return EXIT_SUCCESS;
}
