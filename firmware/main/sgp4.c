#include "sgp4.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// WGS-72, the constants the element sets are fitted with.
#define MU        398600.8
#define RE        6378.135
#define J2        0.001082616
#define J3        -0.00000253881
#define J4        -0.00000165597
#define J3OJ2     (J3 / J2)
#define TWOPI     (2.0 * M_PI)
#define DEG       (M_PI / 180.0)
#define X2O3      (2.0 / 3.0)

static double xke(void) { return 60.0 / sqrt(RE * RE * RE / MU); }

// Columns are 1-based and inclusive, as the format is documented.
static bool field(const char *line, int from, int to, double *out) {
    char buf[24];
    int n = to - from + 1;
    if ((int)strlen(line) < to || n >= (int)sizeof buf) return false;
    memcpy(buf, line + from - 1, n);
    buf[n] = 0;
    char *end;
    *out = strtod(buf, &end);
    while (*end == ' ') end++;
    return end != buf && *end == 0;
}

// "+12345-4" style: implied decimal point before the mantissa.
static bool implied(const char *line, int from, double *out) {
    if ((int)strlen(line) < from + 7) return false;
    char m[8] = { '0', '.', 0 };
    memcpy(m + 2, line + from, 5);
    for (int i = 2; i < 7; i++) if (m[i] == ' ') m[i] = '0';
    double mant = strtod(m, NULL);
    char sign = line[from - 1];
    int ex = line[from + 6] - '0';
    if (ex < 0 || ex > 9) return false;
    if (line[from + 5] == '-') ex = -ex;
    *out = (sign == '-' ? -mant : mant) * pow(10.0, ex);
    return true;
}

static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

bool sgp4_parse(const char *l1, const char *l2, sgp4_t *s) {
    memset(s, 0, sizeof *s);
    double yy, day, incl, node, ecc, argp, mo, n;
    if (l1[0] != '1' || l2[0] != '2') return false;
    if (!field(l1, 19, 20, &yy) || !field(l1, 21, 32, &day) || !implied(l1, 54, &s->bstar)) return false;
    if (!field(l2, 9, 16, &incl) || !field(l2, 18, 25, &node) || !field(l2, 35, 42, &argp) ||
        !field(l2, 44, 51, &mo) || !field(l2, 53, 63, &n)) return false;
    char e[9] = "0.";
    memcpy(e + 2, l2 + 26, 7);
    e[8] = 0;
    ecc = strtod(e, NULL);
    int year = (int)yy < 57 ? 2000 + (int)yy : 1900 + (int)yy;
    s->epoch = (double)days_from_civil(year, 1, 1) * 86400.0 + (day - 1.0) * 86400.0;
    s->inclo = incl * DEG;
    s->nodeo = node * DEG;
    s->ecco = ecc;
    s->argpo = argp * DEG;
    s->mo = mo * DEG;
    s->no_kozai = n * TWOPI / 1440.0;
    if (s->no_kozai <= 0 || ecc >= 1.0) return false;

    // initl
    const double XKE = xke();
    double eccsq = ecc * ecc, omeosq = 1.0 - eccsq, rteosq = sqrt(omeosq);
    double cosio = cos(s->inclo), cosio2 = cosio * cosio;
    double ak = pow(XKE / s->no_kozai, X2O3);
    double d1 = 0.75 * J2 * (3.0 * cosio2 - 1.0) / (rteosq * omeosq);
    double del = d1 / (ak * ak);
    double adel = ak * (1.0 - del * del - del * (1.0 / 3.0 + 134.0 * del * del / 81.0));
    del = d1 / (adel * adel);
    s->no_unkozai = s->no_kozai / (1.0 + del);
    if (TWOPI / s->no_unkozai >= 225.0) return false;   // deep space: not supported
    double ao = pow(XKE / s->no_unkozai, X2O3);
    double sinio = sin(s->inclo), po = ao * omeosq, con42 = 1.0 - 5.0 * cosio2;
    s->con41 = -con42 - cosio2 - cosio2;
    double posq = po * po, rp = ao * (1.0 - ecc);

    // sgp4init, near-Earth
    double ss = 78.0 / RE + 1.0, qzms2t = pow((120.0 - 78.0) / RE, 4);
    s->isimp = rp < 220.0 / RE + 1.0;
    double sfour = ss, qzms24 = qzms2t, perige = (rp - 1.0) * RE;
    if (perige < 156.0) {
        sfour = perige - 78.0;
        if (perige < 98.0) sfour = 20.0;
        qzms24 = pow((120.0 - sfour) / RE, 4);
        sfour = sfour / RE + 1.0;
    }
    double pinvsq = 1.0 / posq, tsi = 1.0 / (ao - sfour);
    s->eta = ao * ecc * tsi;
    double etasq = s->eta * s->eta, eeta = ecc * s->eta, psisq = fabs(1.0 - etasq);
    double coef = qzms24 * pow(tsi, 4), coef1 = coef / pow(psisq, 3.5);
    double cc2 = coef1 * s->no_unkozai *
                 (ao * (1.0 + 1.5 * etasq + eeta * (4.0 + etasq)) +
                  0.375 * J2 * tsi / psisq * s->con41 * (8.0 + 3.0 * etasq * (8.0 + etasq)));
    s->cc1 = s->bstar * cc2;
    double cc3 = ecc > 1.0e-4 ? -2.0 * coef * tsi * J3OJ2 * s->no_unkozai * sinio / ecc : 0.0;
    s->x1mth2 = 1.0 - cosio2;
    s->cc4 = 2.0 * s->no_unkozai * coef1 * ao * omeosq *
             (s->eta * (2.0 + 0.5 * etasq) + ecc * (0.5 + 2.0 * etasq) -
              J2 * tsi / (ao * psisq) *
                  (-3.0 * s->con41 * (1.0 - 2.0 * eeta + etasq * (1.5 - 0.5 * eeta)) +
                   0.75 * s->x1mth2 * (2.0 * etasq - eeta * (1.0 + etasq)) * cos(2.0 * s->argpo)));
    s->cc5 = 2.0 * coef1 * ao * omeosq * (1.0 + 2.75 * (etasq + eeta) + eeta * etasq);
    double cosio4 = cosio2 * cosio2;
    double temp1 = 1.5 * J2 * pinvsq * s->no_unkozai, temp2 = 0.5 * temp1 * J2 * pinvsq;
    double temp3 = -0.46875 * J4 * pinvsq * pinvsq * s->no_unkozai;
    s->mdot = s->no_unkozai + 0.5 * temp1 * rteosq * s->con41 +
              0.0625 * temp2 * rteosq * (13.0 - 78.0 * cosio2 + 137.0 * cosio4);
    s->argpdot = -0.5 * temp1 * con42 + 0.0625 * temp2 * (7.0 - 114.0 * cosio2 + 395.0 * cosio4) +
                 temp3 * (3.0 - 36.0 * cosio2 + 49.0 * cosio4);
    double xhdot1 = -temp1 * cosio;
    s->nodedot = xhdot1 + (0.5 * temp2 * (4.0 - 19.0 * cosio2) + 2.0 * temp3 * (3.0 - 7.0 * cosio2)) * cosio;
    s->omgcof = s->bstar * cc3 * cos(s->argpo);
    s->xmcof = ecc > 1.0e-4 ? -X2O3 * coef * s->bstar / eeta : 0.0;
    s->nodecf = 3.5 * omeosq * xhdot1 * s->cc1;
    s->t2cof = 1.5 * s->cc1;
    double den = fabs(cosio + 1.0) > 1.5e-12 ? 1.0 + cosio : 1.5e-12;
    s->xlcof = -0.25 * J3OJ2 * sinio * (3.0 + 5.0 * cosio) / den;
    s->aycof = -0.5 * J3OJ2 * sinio;
    s->delmo = pow(1.0 + s->eta * cos(s->mo), 3);
    s->sinmao = sin(s->mo);
    s->x7thm1 = 7.0 * cosio2 - 1.0;
    if (!s->isimp) {
        double cc1sq = s->cc1 * s->cc1;
        s->d2 = 4.0 * ao * tsi * cc1sq;
        double temp = s->d2 * tsi * s->cc1 / 3.0;
        s->d3 = (17.0 * ao + sfour) * temp;
        s->d4 = 0.5 * temp * ao * tsi * (221.0 * ao + 31.0 * sfour) * s->cc1;
        s->t3cof = s->d2 + 2.0 * cc1sq;
        s->t4cof = 0.25 * (3.0 * s->d3 + s->cc1 * (12.0 * s->d2 + 10.0 * cc1sq));
        s->t5cof = 0.2 * (3.0 * s->d4 + 12.0 * s->cc1 * s->d3 + 6.0 * s->d2 * s->d2 + 15.0 * cc1sq * (2.0 * s->d2 + cc1sq));
    }
    return true;
}

bool sgp4_at(const sgp4_t *s, double unix_s, double r[3], double v[3]) {
    const double XKE = xke(), VKMPS = RE * XKE / 60.0;
    double t = (unix_s - s->epoch) / 60.0;

    double xmdf = s->mo + s->mdot * t;
    double argpdf = s->argpo + s->argpdot * t;
    double nodedf = s->nodeo + s->nodedot * t;
    double argpm = argpdf, mm = xmdf, t2 = t * t;
    double nodem = nodedf + s->nodecf * t2;
    double tempa = 1.0 - s->cc1 * t, tempe = s->bstar * s->cc4 * t, templ = s->t2cof * t2;
    if (!s->isimp) {
        double delomg = s->omgcof * t;
        double delm = s->xmcof * (pow(1.0 + s->eta * cos(xmdf), 3) - s->delmo);
        double temp = delomg + delm;
        mm = xmdf + temp;
        argpm = argpdf - temp;
        double t3 = t2 * t, t4 = t3 * t;
        tempa = tempa - s->d2 * t2 - s->d3 * t3 - s->d4 * t4;
        tempe = tempe + s->bstar * s->cc5 * (sin(mm) - s->sinmao);
        templ = templ + s->t3cof * t3 + t4 * (s->t4cof + t * s->t5cof);
    }
    double nm = s->no_unkozai, em = s->ecco, inclm = s->inclo;
    double am = pow(XKE / nm, X2O3) * tempa * tempa;
    nm = XKE / pow(am, 1.5);
    em = em - tempe;
    if (em >= 1.0 || em < -0.001 || am < 0.95) return false;
    if (em < 1.0e-6) em = 1.0e-6;
    mm = mm + s->no_unkozai * templ;
    double xlm = mm + argpm + nodem;
    nodem = fmod(nodem, TWOPI);
    argpm = fmod(argpm, TWOPI);
    xlm = fmod(xlm, TWOPI);
    mm = fmod(xlm - argpm - nodem, TWOPI);

    double sinip = sin(inclm), cosip = cos(inclm);
    double axnl = em * cos(argpm);
    double temp = 1.0 / (am * (1.0 - em * em));
    double aynl = em * sin(argpm) + temp * s->aycof;
    double xl = mm + argpm + nodem + temp * s->xlcof * axnl;

    // Kepler's equation
    double u = fmod(xl - nodem, TWOPI), eo1 = u, tem5 = 9999.9, sineo1 = 0, coseo1 = 0;
    for (int k = 0; fabs(tem5) >= 1.0e-12 && k < 10; k++) {
        sineo1 = sin(eo1);
        coseo1 = cos(eo1);
        tem5 = 1.0 - coseo1 * axnl - sineo1 * aynl;
        tem5 = (u - aynl * coseo1 + axnl * sineo1 - eo1) / tem5;
        if (fabs(tem5) >= 0.95) tem5 = tem5 > 0 ? 0.95 : -0.95;
        eo1 += tem5;
    }

    double ecose = axnl * coseo1 + aynl * sineo1;
    double esine = axnl * sineo1 - aynl * coseo1;
    double el2 = axnl * axnl + aynl * aynl;
    double pl = am * (1.0 - el2);
    if (pl < 0.0) return false;
    double rl = am * (1.0 - ecose);
    double rdotl = sqrt(am) * esine / rl;
    double rvdotl = sqrt(pl) / rl;
    double betal = sqrt(1.0 - el2);
    temp = esine / (1.0 + betal);
    double sinu = am / rl * (sineo1 - aynl - axnl * temp);
    double cosu = am / rl * (coseo1 - axnl + aynl * temp);
    double su = atan2(sinu, cosu);
    double sin2u = (cosu + cosu) * sinu, cos2u = 1.0 - 2.0 * sinu * sinu;
    temp = 1.0 / pl;
    double temp1 = 0.5 * J2 * temp, temp2 = temp1 * temp;

    double mrt = rl * (1.0 - 1.5 * temp2 * betal * s->con41) + 0.5 * temp1 * s->x1mth2 * cos2u;
    su = su - 0.25 * temp2 * s->x7thm1 * sin2u;
    double xnode = nodem + 1.5 * temp2 * cosip * sin2u;
    double xinc = inclm + 1.5 * temp2 * cosip * sinip * cos2u;
    double mvt = rdotl - nm * temp1 * s->x1mth2 * sin2u / XKE;
    double rvdot = rvdotl + nm * temp1 * (s->x1mth2 * cos2u + 1.5 * s->con41) / XKE;

    double sinsu = sin(su), cossu = cos(su), snod = sin(xnode), cnod = cos(xnode);
    double sini = sin(xinc), cosi = cos(xinc);
    double xmx = -snod * cosi, xmy = cnod * cosi;
    double ux = xmx * sinsu + cnod * cossu, uy = xmy * sinsu + snod * cossu, uz = sini * sinsu;
    double vx = xmx * cossu - cnod * sinsu, vy = xmy * cossu - snod * sinsu, vz = sini * cossu;
    r[0] = mrt * ux * RE;
    r[1] = mrt * uy * RE;
    r[2] = mrt * uz * RE;
    v[0] = (mvt * ux + rvdot * vx) * VKMPS;
    v[1] = (mvt * uy + rvdot * vy) * VKMPS;
    v[2] = (mvt * uz + rvdot * vz) * VKMPS;
    return mrt >= 1.0;
}
