// Crew Manifest: the household as a ship's crew. Status comes from the clock and
// each member's schedule, duties rotate daily, and the Special Order is picked
// by day of the year. Pure C; the crew itself is set in config.h.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "assets.h"

#define CREW_MAX 6
#define HM(h, m) ((h) * 60 + (m))

typedef struct {
    const char *name, *rank;
    picto_t     figure;
    bool        human;
    int16_t     sleep_from, sleep_to;   // minutes after local midnight; the window may wrap
    const char *away;                   // status while away, e.g. "TRAINING"; NULL for none
    int16_t     away_from, away_to;
    uint8_t     away_days;              // bit 0 Sunday to bit 6 Saturday
} crew_member_t;

typedef struct {
    const char *ship, *company;
    uint8_t     count;
    crew_member_t member[CREW_MAX];
    bool        duty, sleep;            // duty rotation; hypersleep at night
} crew_t;

typedef enum { CREW_ON_DECK, CREW_DUTY, CREW_AWAY, CREW_ASLEEP, CREW_WATCH } crew_kind_t;

typedef struct {
    crew_kind_t kind;
    const char *word;     // what the card's chip says
    uint8_t     sev;      // placard ground: 0 white, 1 yellow, 3 black
} crew_state_t;

#define CREW_WEEKDAYS 0x3E   // Monday to Friday

// Member i at a local minute of the day, weekday 0 Sunday, day of year 1-366.
crew_state_t crew_state(const crew_t *c, int i, int minute, int weekday, int yday);

// Today's post for a human (NULL for the others or with duties off).
const char *crew_duty(const crew_t *c, int i, int yday);

// Special Order for a day of the year (1-366) with {DOG}, {SHIP} and {CO}
// filled in. Returns the length written.
size_t crew_order(const crew_t *c, int yday, char *out, size_t cap);
