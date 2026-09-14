// SPDX-FileCopyrightText: 2026 Field Bureau & Werkstatt LLC
// SPDX-License-Identifier: AGPL-3.0-or-later
// SGP4 orbit propagation for near-Earth satellites (period under 225 min), from
// the two-line elements CelesTrak publishes. A port of the near-Earth branch of
// Vallado's reference code with WGS-72 constants, as SGP4 elements require.
// Pure C.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    double epoch;               // element epoch, unix seconds
    double bstar, inclo, nodeo, ecco, argpo, mo, no_kozai;

    // Set from the elements by sgp4_parse.
    int    isimp;
    double aycof, con41, cc1, cc4, cc5, d2, d3, d4, delmo, eta, argpdot, omgcof, sinmao,
           t2cof, t3cof, t4cof, t5cof, x1mth2, x7thm1, mdot, nodedot, xlcof, xmcof, nodecf,
           no_unkozai;
} sgp4_t;

// Parses the two element lines (checksums are not verified). False when a field
// does not parse or the orbit is not near-Earth.
bool sgp4_parse(const char *line1, const char *line2, sgp4_t *s);

// Position (km) and velocity (km/s) in the TEME frame at a unix time. False when
// the elements have decayed past use at that time.
bool sgp4_at(const sgp4_t *s, double unix_s, double r[3], double v[3]);
