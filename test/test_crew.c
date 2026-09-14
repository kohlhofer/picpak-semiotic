// Host test for the Crew Manifest: schedule states, duty rotation and orders.
#include "crew.h"
#include "gfx.h"
#include "orders.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)
#define STR_IS(a, b) do { if (strcmp((a), (b)) != 0) { printf("FAIL %s:%d: [%s] != [%s]\n", __FILE__, __LINE__, (a), (b)); failures++; } } while (0)

enum { SUN = 0, MON = 1, SAT = 6 };

static crew_t make(void) {
    return (crew_t){
        .ship = "USCSS TEST", .company = "ACME", .duty = true, .sleep = true, .count = 5,
        .member = {
            { "ONE", "COMMAND", PI_MAN, true, HM(23, 0), HM(6, 30), "ON DUTY", HM(9, 0), HM(17, 0), CREW_WEEKDAYS },
            { "TWO", "COMMAND", PI_WOMAN, true, HM(23, 0), HM(6, 30), NULL, 0, 0, 0 },
            { "THREE", "CADET", PI_GIRL1, true, HM(20, 30), HM(7, 0), "TRAINING", HM(8, 0), HM(15, 30), CREW_WEEKDAYS },
            { "FOUR", "CADET", PI_GIRL2, true, HM(20, 30), HM(7, 0), "TRAINING", HM(8, 0), HM(15, 30), CREW_WEEKDAYS },
            { "REX", "SECURITY", PI_DOG, false, 0, 0, NULL, 0, 0, 0 },
        },
    };
}

static void test_states(void) {
    crew_t c = make();
    // Sunday evening: everyone on deck with a post; the dog is on deck too.
    const char *seen[4];
    for (int i = 0; i < 4; i++) {
        crew_state_t s = crew_state(&c, i, HM(19, 0), SUN, 256);
        CHECK(s.kind == CREW_DUTY && s.sev == 0);
        seen[i] = s.word;
        for (int k = 0; k < i; k++) CHECK(strcmp(seen[k], s.word) != 0);   // four different posts
    }
    CHECK(crew_state(&c, 4, HM(19, 0), SUN, 256).kind == CREW_ON_DECK);
    CHECK(crew_duty(&c, 4, 256) == NULL);
    STR_IS(crew_duty(&c, 0, 257), crew_duty(&c, 1, 256));   // posts rotate by a day

    // Monday 10:00: ONE at work, the cadets in training, TWO home with the dog.
    crew_state_t s = crew_state(&c, 0, HM(10, 0), MON, 257);
    CHECK(s.kind == CREW_AWAY && s.sev == 1);
    STR_IS(s.word, "ON DUTY");
    STR_IS(crew_state(&c, 2, HM(10, 0), MON, 257).word, "TRAINING");
    CHECK(crew_state(&c, 1, HM(10, 0), MON, 257).kind == CREW_DUTY);
    CHECK(crew_state(&c, 4, HM(10, 0), MON, 257).kind == CREW_ON_DECK);
    CHECK(crew_state(&c, 2, HM(10, 0), SAT, 262).kind == CREW_DUTY);   // no training on Saturday
    CHECK(crew_state(&c, 2, HM(15, 30), MON, 257).kind == CREW_DUTY);  // the window ends at 15:30

    // Night: hypersleep on black placards; the dog keeps watch once nobody is up.
    s = crew_state(&c, 0, HM(23, 30), SUN, 256);
    CHECK(s.kind == CREW_ASLEEP && s.sev == 3);
    STR_IS(s.word, "HYPERSLEEP");
    CHECK(crew_state(&c, 2, HM(21, 0), SUN, 256).kind == CREW_ASLEEP);
    CHECK(crew_state(&c, 2, HM(6, 59), MON, 257).kind == CREW_ASLEEP);   // wraps past midnight
    CHECK(crew_state(&c, 2, HM(7, 0), MON, 257).kind == CREW_DUTY);
    CHECK(crew_state(&c, 4, HM(21, 0), SUN, 256).kind == CREW_ON_DECK);   // adults still up
    s = crew_state(&c, 4, HM(23, 30), SUN, 256);
    CHECK(s.kind == CREW_WATCH);
    STR_IS(s.word, "ON WATCH");

    c.sleep = false;
    STR_IS(crew_state(&c, 0, HM(23, 30), SUN, 256).word, "OFF DUTY");
    c.duty = false;
    CHECK(crew_duty(&c, 0, 256) == NULL);
    STR_IS(crew_state(&c, 0, HM(19, 0), SUN, 256).word, "ON DECK");
}

static void test_orders(void) {
    crew_t c = make();
    char out[200];
    int with_dog = 0;
    for (int d = 1; d <= ORDER_COUNT; d++) {
        size_t n = crew_order(&c, d, out, sizeof out);
        CHECK(n == strlen(out) && n > 0 && n < sizeof out - 1);
        CHECK(strchr(out, '{') == NULL && strchr(out, '}') == NULL);
        if (strstr(ORDERS[d - 1], "{DOG}")) {
            with_dog++;
            CHECK(strstr(out, "REX") != NULL);
        }
    }
    CHECK(with_dog > 0);
    crew_order(&c, 0, out, sizeof out);   // clamped to day 1
    char day1[200];
    crew_order(&c, 1, day1, sizeof day1);
    STR_IS(out, day1);
    CHECK(crew_order(&c, 1, out, 8) == 7 && strlen(out) == 7);
    c.member[4].human = true;   // no dog aboard: the token still reads sensibly
    for (int d = 1; d <= ORDER_COUNT; d++)
        if (strstr(ORDERS[d - 1], "{DOG}")) { crew_order(&c, d, out, sizeof out); CHECK(strstr(out, "SECURITY") != NULL); break; }
}

// Every order must fit the roster's plate, four lines of Jersey 10 at 352 px,
// with names as long as the config allows.
static void test_orders_fit(void) {
    crew_t c = make();
    c.ship = "USCSS PICPAK";
    c.company = "WEYLAND-YUTANI";
    c.member[4].name = "WWWWWWWWWW";
    char out[200], lines[5][128];
    int worst = 0;
    for (int d = 1; d <= ORDER_COUNT; d++) {
        crew_order(&c, d, out, sizeof out);
        int n = gfx_wrap(&FONT_JR19, out, 352, lines, 5);
        if (n > 4 || strstr(lines[n - 1], "...")) { printf("FAIL order %03d needs %d lines: %s\n", d, n, out); failures++; }
        if (n > worst) worst = n;
    }
    printf("crew: longest order takes %d lines\n", worst);
}

int main(void) {
    test_states();
    test_orders();
    test_orders_fit();
    if (failures) { printf("%d check(s) failed\n", failures); return EXIT_FAILURE; }
    printf("crew: all checks passed\n");
    return EXIT_SUCCESS;
}
