// SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
// SPDX-License-Identifier: AGPL-3.0-or-later
// Orbital Tracking data: ISS elements from CelesTrak, the nearest asteroid this
// week from JPL's close-approach API, and the passes worked out from them.
// Pure C.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pass.h"
#include "sky.h"

#define TLE_URL      "https://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=TLE"
#define NEO_URL      "https://ssd-api.jpl.nasa.gov/cad.api?date-min=now&date-max=%2B7&dist-max=0.2&sort=dist&limit=1"
#define ORBIT_PASSES 4
#define ORBIT_REFRESH_S (6 * 3600)
#define PASS_MIN_EL  10.0

typedef struct {
    char    des[16];      // designation, e.g. "2026 RG15"
    int64_t approach;     // closest approach, unix seconds
    float   dist_au, v_kms, h;
} neo_t;

typedef struct {
    char    l1[70], l2[70];       // latest elements; empty when never fetched
    int64_t tle_sync, neo_sync;   // 0 when never
    neo_t   neo;
    bool    neo_ok;
    uint8_t count;                // passes found from searched_at on
    int64_t searched_at;
    pass_t  pass[ORBIT_PASSES];
} orbit_t;

// The two element lines from a CelesTrak response (name line optional).
bool orbit_parse_tle(const char *text, char *l1, char *l2);

// The first row of a cad.api response.
bool orbit_parse_neo(const char *json, size_t len, neo_t *out);

// First stored pass that has not ended by now, or NULL.
const pass_t *orbit_next_pass(const orbit_t *o, int64_t now);

// Moon phase level for the placard (0 new, 2 first quarter, 4 full, 6 last
// quarter) and its name.
int orbit_moon_level(double elongation);
const char *orbit_moon_name(double fraction, double elongation);

// Eight-point compass name for an azimuth in degrees.
const char *orbit_compass(int az);

// Rough diameter range in feet from absolute magnitude, for albedo 0.25 to 0.05.
void orbit_size_ft(float h, int *lo, int *hi);

// Lunar distances and miles from astronomical units; miles per hour from km/s.
double orbit_ld(float au);
double orbit_miles(float au);
double orbit_mph(float kms);
