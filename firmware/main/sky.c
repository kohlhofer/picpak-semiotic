// SPDX-FileCopyrightText: 2026 Alexander Kohlhofer
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sky.h"

#include <math.h>

#define DEG (M_PI / 180.0)

static double days_j2000(double unix_s) { return unix_s / 86400.0 + 2440587.5 - 2451545.0; }

double sky_gmst(double unix_s) {
    double d = days_j2000(unix_s), t = d / 36525.0;
    double sec = 67310.54841 + (876600.0 * 3600.0 + 8640184.812866) * t + 0.093104 * t * t - 6.2e-6 * t * t * t;
    double g = fmod(sec * DEG / 240.0, 2.0 * M_PI);
    return g < 0 ? g + 2.0 * M_PI : g;
}

static void sun_ecliptic(double d, double *lon, double *obl) {
    double g = (357.529 + 0.98560028 * d) * DEG;
    double q = 280.459 + 0.98564736 * d;   // mean longitude
    *lon = (q + 1.915 * sin(g) + 0.020 * sin(2.0 * g)) * DEG;
    *obl = (23.439 - 0.00000036 * d) * DEG;
}

void sky_sun_dir(double unix_s, double dir[3]) {
    double lon, obl;
    sun_ecliptic(days_j2000(unix_s), &lon, &obl);
    dir[0] = cos(lon);
    dir[1] = cos(obl) * sin(lon);
    dir[2] = sin(obl) * sin(lon);
}

void sky_to_ecef(double unix_s, const double eci[3], double ecef[3]) {
    double g = sky_gmst(unix_s), c = cos(g), s = sin(g);
    ecef[0] = c * eci[0] + s * eci[1];
    ecef[1] = -s * eci[0] + c * eci[1];
    ecef[2] = eci[2];
}

typedef struct { site_t site; double o[3], sl, cl, so, co; } site_cache_t;

// The pass search looks from one site thousands of times; keep its position
// and trigonometry from the last call.
static const site_cache_t *cached(const site_t *site) {
    static site_cache_t c = { .site = { 1e9, 1e9, 1e9 } };
    if (c.site.lat != site->lat || c.site.lon != site->lon || c.site.h_km != site->h_km) {
        c.site = *site;
        const double a = 6378.137, e2 = 0.00669437999014;
        double lat = site->lat * DEG, lon = site->lon * DEG;
        c.sl = sin(lat); c.cl = cos(lat); c.so = sin(lon); c.co = cos(lon);
        double n = a / sqrt(1.0 - e2 * c.sl * c.sl);
        c.o[0] = (n + site->h_km) * c.cl * c.co;
        c.o[1] = (n + site->h_km) * c.cl * c.so;
        c.o[2] = (n * (1.0 - e2) + site->h_km) * c.sl;
    }
    return &c;
}

void sky_site_ecef(const site_t *site, double ecef[3]) {
    const site_cache_t *c = cached(site);
    for (int i = 0; i < 3; i++) ecef[i] = c->o[i];
}

void sky_look(const site_t *site, const double p[3], double *az, double *el) {
    const site_cache_t *c = cached(site);
    const double *o = c->o;
    double dx = p[0] - o[0], dy = p[1] - o[1], dz = p[2] - o[2];
    double sl = c->sl, cl = c->cl, so = c->so, co = c->co;
    double e = -so * dx + co * dy;
    double n = -sl * co * dx - sl * so * dy + cl * dz;
    double u = cl * co * dx + cl * so * dy + sl * dz;
    double a = atan2(e, n) / DEG;
    *az = a < 0 ? a + 360.0 : a;
    *el = atan2(u, sqrt(e * e + n * n)) / DEG;
}

double sky_sun_el(const site_t *site, double unix_s) {
    double dir[3], ecef[3], far[3], az, el;
    sky_sun_dir(unix_s, dir);
    sky_to_ecef(unix_s, dir, ecef);
    for (int i = 0; i < 3; i++) far[i] = ecef[i] * 1.496e8;
    sky_look(site, far, &az, &el);
    return el;
}

void sky_sun_times(const site_t *site, int64_t day_start, int64_t *rise, int64_t *set) {
    const double H = -0.833;   // refraction and the Sun's radius
    *rise = *set = 0;
    double prev = sky_sun_el(site, (double)day_start) - H;
    for (int64_t t = day_start + 600; t <= day_start + 86400; t += 600) {
        double cur = sky_sun_el(site, (double)t) - H;
        if ((prev < 0) != (cur < 0)) {
            double lo = (double)(t - 600), hi = (double)t;
            for (int i = 0; i < 12; i++) {
                double mid = (lo + hi) / 2, m = sky_sun_el(site, mid) - H;
                if ((m < 0) == (prev < 0)) lo = mid; else hi = mid;
            }
            int64_t at = (int64_t)llround((lo + hi) / 2);
            if (prev < 0 && !*rise) *rise = at;
            if (prev >= 0 && !*set) *set = at;
        }
        prev = cur;
    }
}

void sky_moon(double unix_s, double *fraction, double *elongation) {
    double d = days_j2000(unix_s);
    double lm = 218.316 + 13.176396 * d, mm = (134.963 + 13.064993 * d) * DEG;
    double ls = 280.459 + 0.98564736 * d, ms = (357.529 + 0.98560028 * d) * DEG;
    double moon = lm + 6.289 * sin(mm);
    double sun = ls + 1.915 * sin(ms) + 0.020 * sin(2.0 * ms);
    double e = fmod(moon - sun, 360.0);
    if (e < 0) e += 360.0;
    *elongation = e;
    *fraction = (1.0 - cos(e * DEG)) / 2.0;
}
