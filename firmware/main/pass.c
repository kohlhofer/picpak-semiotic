#include "pass.h"

#include <math.h>

#define DEG (M_PI / 180.0)
#define RE  6378.137

typedef struct { double az, el, psi, r; bool lit; } look_t;

static bool look_at(const sgp4_t *sat, const site_t *site, double t, look_t *l) {
    double r[3], v[3], ecef[3], o[3], sun[3];
    if (!sgp4_at(sat, t, r, v)) return false;
    sky_to_ecef(t, r, ecef);
    sky_look(site, ecef, &l->az, &l->el);
    sky_site_ecef(site, o);
    double rr = sqrt(ecef[0] * ecef[0] + ecef[1] * ecef[1] + ecef[2] * ecef[2]);
    double oo = sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
    double c = (ecef[0] * o[0] + ecef[1] * o[1] + ecef[2] * o[2]) / (rr * oo);
    l->psi = acos(c > 1 ? 1 : c < -1 ? -1 : c) / DEG;
    l->r = rr;
    // Cylindrical shadow: in shadow when behind Earth and within its radius of the axis.
    sky_sun_dir(t, sun);
    double along = r[0] * sun[0] + r[1] * sun[1] + r[2] * sun[2];
    l->lit = along > 0 || rr * rr - along * along > RE * RE;
    return true;
}

bool pass_look(const sgp4_t *sat, const site_t *site, double unix_s, double *az, double *el, bool *sunlit) {
    look_t l;
    if (!look_at(sat, site, unix_s, &l)) return false;
    *az = l.az;
    *el = l.el;
    *sunlit = l.lit;
    return true;
}

// Time in [lo, hi] where elevation crosses min_el, to under a second.
static double crossing(const sgp4_t *sat, const site_t *site, double lo, double hi, double min_el, bool rising) {
    look_t l;
    while (hi - lo > 0.5) {
        double mid = (lo + hi) / 2;
        if (!look_at(sat, site, mid, &l)) break;
        if ((l.el >= min_el) == rising) hi = mid; else lo = mid;
    }
    return rising ? hi : lo;
}

int pass_find(const sgp4_t *sat, const site_t *site, int64_t from, int64_t to, double min_el, bool visible_only,
              pass_t *out, int max) {
    int count = 0;
    double rate = sat->no_unkozai / DEG + 0.3;   // degrees of arc per minute, with Earth's turn
    double t = (double)from;
    look_t l;
    if (!look_at(sat, site, t, &l)) return 0;
    while (t < (double)to && count < max) {
        double rise;
        if (l.el >= min_el) {
            rise = t;   // under way at the start
        } else {
            // Far from the site, jump to where it could first be in view.
            double reach = acos(RE * cos(min_el * DEG) / l.r) / DEG - min_el + 5.0;
            double step = l.psi > reach ? (l.psi - reach) / rate * 60.0 : 20.0;
            if (step < 20.0) step = 20.0;
            double t2 = t + step;
            look_t l2;
            if (!look_at(sat, site, t2, &l2)) return count;
            if (l2.el < min_el) { t = t2; l = l2; continue; }
            rise = crossing(sat, site, t, t2, min_el, true);
        }
        // Follow the pass to its end.
        double tp = rise, best_t = rise, best_el = -90;
        look_t lp;
        for (;;) {
            if (!look_at(sat, site, tp, &lp)) return count;
            if (lp.el > best_el) { best_el = lp.el; best_t = tp; }
            if (lp.el < min_el && tp > rise) break;
            tp += 10.0;
        }
        double set = crossing(sat, site, tp - 10.0, tp, min_el, false);
        // Refine the highest point around the best sample.
        double a = best_t - 10.0, b = best_t + 10.0;
        while (b - a > 0.5) {
            look_t la, lb;
            double m1 = a + (b - a) / 3, m2 = b - (b - a) / 3;
            if (!look_at(sat, site, m1, &la) || !look_at(sat, site, m2, &lb)) break;
            if (la.el < lb.el) a = m1; else b = m2;
        }
        look_t peak;
        if (look_at(sat, site, (a + b) / 2, &peak) && peak.el > best_el) best_el = peak.el;

        pass_t p = { .rise = (int64_t)llround(rise), .set = (int64_t)llround(set), .max_el = (int16_t)lround(best_el * 10) };
        for (int i = 0; i < PASS_ARC; i++) {
            double ti = rise + (set - rise) * i / (PASS_ARC - 1);
            look_t li;
            if (!look_at(sat, site, ti, &li)) return count;
            p.az[i] = (int16_t)lround(li.az * 10);
            p.el[i] = (int16_t)lround(li.el * 10);
            if (li.lit && sky_sun_el(site, ti) < -6.0) p.visible = true;
        }
        p.rise_az = (int16_t)lround(p.az[0] / 10.0);
        p.set_az = (int16_t)lround(p.az[PASS_ARC - 1] / 10.0);
        if ((!visible_only || p.visible) && p.set > from && p.rise < to) out[count++] = p;

        t = set + 60.0;
        if (!look_at(sat, site, t, &l)) return count;
    }
    return count;
}
