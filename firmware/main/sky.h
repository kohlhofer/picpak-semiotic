// SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
// SPDX-License-Identifier: AGPL-3.0-or-later
// Sun and Moon from low-precision almanac formulas (about a minute of time, a
// fraction of a degree), sidereal time, and an observer on the WGS-84 ellipsoid.
// Pure C.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { double lat, lon, h_km; } site_t;   // degrees, degrees east, km

// Greenwich mean sidereal time in radians.
double sky_gmst(double unix_s);

// Unit vector towards the Sun in the equatorial frame of date.
void sky_sun_dir(double unix_s, double dir[3]);

// Topocentric azimuth (0 north, clockwise) and elevation in degrees of an
// Earth-fixed point given in km; pass the Sun far away along its direction.
void sky_look(const site_t *site, const double ecef[3], double *az, double *el);

// Sun elevation above the horizon in degrees, without refraction.
double sky_sun_el(const site_t *site, double unix_s);

// Sunrise and sunset in the 24 h from day_start (both 0 when the Sun does not
// cross -0.833 degrees that day).
void sky_sun_times(const site_t *site, int64_t day_start, int64_t *rise, int64_t *set);

// Moon: illuminated fraction 0-1, and the Sun-Moon elongation in degrees
// (0-360; under 180 is waxing).
void sky_moon(double unix_s, double *fraction, double *elongation);

// Rotates TEME/equatorial coordinates into Earth-fixed ones at a time.
void sky_to_ecef(double unix_s, const double eci[3], double ecef[3]);

// The site's position in Earth-fixed km.
void sky_site_ecef(const site_t *site, double ecef[3]);
