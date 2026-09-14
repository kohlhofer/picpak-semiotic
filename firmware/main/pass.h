// SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
// SPDX-License-Identifier: AGPL-3.0-or-later
// Satellite passes over a site: when it climbs above a minimum elevation, how
// high it gets, where it rises and sets, and whether it can be seen (sunlit
// while the sky at the site is dark). Pure C.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sgp4.h"
#include "sky.h"

#define PASS_ARC 16

typedef struct {
    int64_t rise, set;              // unix seconds, above min_el between them
    int16_t max_el;                 // tenths of a degree
    int16_t rise_az, set_az;        // degrees
    bool    visible;
    int16_t az[PASS_ARC], el[PASS_ARC];   // tenths of a degree, rise to set
} pass_t;

// Passes that end after from and start before to, in time order. With
// visible_only, passes that can't be seen are skipped. Returns the count.
// A pass already under way at from starts at from.
int pass_find(const sgp4_t *sat, const site_t *site, int64_t from, int64_t to, double min_el, bool visible_only,
              pass_t *out, int max);

// Called every few dozen propagations during pass_find, so a caller on a
// single-core RTOS can let other tasks run. NULL (the default) for none.
void pass_set_yield(void (*fn)(void));

// Where the satellite is: azimuth and elevation (degrees), and whether sunlit.
bool pass_look(const sgp4_t *sat, const site_t *site, double unix_s, double *az, double *el, bool *sunlit);
