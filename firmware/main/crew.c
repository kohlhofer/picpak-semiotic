// SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "crew.h"
#include "orders.h"

#include <string.h>

static const char *DUTIES[] = { "GALLEY", "HYDRO BAY", "CARGO BAY", "SANITATION" };
#define DUTY_COUNT (int)(sizeof DUTIES / sizeof DUTIES[0])

static bool within(int minute, int from, int to) {
    if (from == to) return false;
    return from < to ? minute >= from && minute < to : minute >= from || minute < to;
}

static bool asleep(const crew_member_t *m, int minute) { return m->human && within(minute, m->sleep_from, m->sleep_to); }

static bool away(const crew_member_t *m, int minute, int weekday) {
    return m->away && (m->away_days >> weekday & 1) && within(minute, m->away_from, m->away_to);
}

const char *crew_duty(const crew_t *c, int i, int yday) {
    if (!c->duty || i < 0 || i >= c->count || !c->member[i].human) return NULL;
    int n = 0;   // position among the humans
    for (int k = 0; k < i; k++) n += c->member[k].human;
    return DUTIES[(yday + n) % DUTY_COUNT];
}

crew_state_t crew_state(const crew_t *c, int i, int minute, int weekday, int yday) {
    const crew_member_t *m = &c->member[i];
    if (!m->human) {
        // The dog keeps watch whenever no human is up and about.
        int up = 0;
        for (int k = 0; k < c->count; k++) {
            const crew_member_t *h = &c->member[k];
            if (!h->human) continue;
            if (!asleep(h, minute) && !away(h, minute, weekday)) up++;
        }
        if (up) return (crew_state_t){ CREW_ON_DECK, "ON DECK", 0 };
        return (crew_state_t){ CREW_WATCH, "ON WATCH", 0 };
    }
    if (asleep(m, minute)) {
        return c->sleep ? (crew_state_t){ CREW_ASLEEP, "HYPERSLEEP", 3 } : (crew_state_t){ CREW_ON_DECK, "OFF DUTY", 0 };
    }
    if (away(m, minute, weekday)) return (crew_state_t){ CREW_AWAY, m->away, 1 };
    const char *duty = crew_duty(c, i, yday);
    if (duty) return (crew_state_t){ CREW_DUTY, duty, 0 };
    return (crew_state_t){ CREW_ON_DECK, "ON DECK", 0 };
}

size_t crew_order(const crew_t *c, int yday, char *out, size_t cap) {
    if (!cap) return 0;
    if (yday < 1) yday = 1;
    if (yday > ORDER_COUNT) yday = ORDER_COUNT;
    const char *dog = "SECURITY";
    for (int k = 0; k < c->count; k++)
        if (!c->member[k].human) { dog = c->member[k].name; break; }
    size_t n = 0;
    for (const char *p = ORDERS[yday - 1]; *p && n + 1 < cap; ) {
        const char *sub = NULL;
        size_t skip = 0;
        if (strncmp(p, "{DOG}", 5) == 0) { sub = dog; skip = 5; }
        else if (strncmp(p, "{SHIP}", 6) == 0) { sub = c->ship; skip = 6; }
        else if (strncmp(p, "{CO}", 4) == 0) { sub = c->company; skip = 4; }
        if (sub) {
            for (; *sub && n + 1 < cap; sub++) out[n++] = *sub;
            p += skip;
        } else {
            out[n++] = *p++;
        }
    }
    out[n] = 0;
    return n;
}
