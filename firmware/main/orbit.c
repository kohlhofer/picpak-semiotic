#include "orbit.h"
#include "cJSON.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AU_KM 149597870.7
#define LD_KM 384400.0

static bool take_line(const char *start, char *out) {
    size_t n = strcspn(start, "\r\n");
    while (n && start[n - 1] == ' ') n--;
    if (n < 64 || n > 69) return false;
    memcpy(out, start, n);
    out[n] = 0;
    return true;
}

bool orbit_parse_tle(const char *text, char *l1, char *l2) {
    bool got1 = false, got2 = false;
    for (const char *p = text; *p; ) {
        if (p[0] == '1' && p[1] == ' ') got1 = take_line(p, l1) || got1;
        else if (p[0] == '2' && p[1] == ' ' && got1) { got2 = take_line(p, l2); break; }
        p += strcspn(p, "\n");
        if (*p) p++;
    }
    return got1 && got2;
}

static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

// "2026-Sep-15 07:36"
static bool cad_date(const char *s, int64_t *out) {
    static const char *MON[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int y, d, hh, mm;
    char mon[4];
    if (sscanf(s, "%d-%3s-%d %d:%d", &y, mon, &d, &hh, &mm) != 5) return false;
    for (int m = 0; m < 12; m++)
        if (strcmp(mon, MON[m]) == 0) {
            *out = days_from_civil(y, m + 1, d) * 86400 + hh * 3600 + mm * 60;
            return true;
        }
    return false;
}

bool orbit_parse_neo(const char *json, size_t len, neo_t *out) {
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return false;
    bool ok = false;
    const cJSON *fields = cJSON_GetObjectItemCaseSensitive(root, "fields");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    const cJSON *row = cJSON_IsArray(data) ? cJSON_GetArrayItem(data, 0) : NULL;
    if (cJSON_IsArray(fields) && cJSON_IsArray(row)) {
        const char *des = NULL, *cd = NULL, *dist = NULL, *v = NULL, *h = NULL;
        int i = 0;
        const cJSON *f;
        cJSON_ArrayForEach(f, fields) {
            const cJSON *cell = cJSON_GetArrayItem(row, i++);
            if (!cJSON_IsString(f) || !cJSON_IsString(cell)) continue;
            if (strcmp(f->valuestring, "des") == 0) des = cell->valuestring;
            else if (strcmp(f->valuestring, "cd") == 0) cd = cell->valuestring;
            else if (strcmp(f->valuestring, "dist") == 0) dist = cell->valuestring;
            else if (strcmp(f->valuestring, "v_rel") == 0) v = cell->valuestring;
            else if (strcmp(f->valuestring, "h") == 0) h = cell->valuestring;
        }
        neo_t n = { 0 };
        if (des && cd && dist && v && cad_date(cd, &n.approach)) {
            snprintf(n.des, sizeof n.des, "%s", des);
            n.dist_au = strtof(dist, NULL);
            n.v_kms = strtof(v, NULL);
            n.h = h ? strtof(h, NULL) : 0;
            ok = n.dist_au > 0;
            if (ok) *out = n;
        }
    }
    cJSON_Delete(root);
    return ok;
}

const pass_t *orbit_next_pass(const orbit_t *o, int64_t now) {
    for (int i = 0; i < o->count; i++)
        if (o->pass[i].set > now) return &o->pass[i];
    return NULL;
}

int orbit_moon_level(double elongation) { return (int)lround(elongation / 45.0) % 8; }

const char *orbit_moon_name(double fraction, double elongation) {
    bool waxing = elongation < 180.0;
    if (fraction < 0.03) return "NEW MOON";
    if (fraction > 0.97) return "FULL MOON";
    if (fabs(fraction - 0.5) < 0.04) return waxing ? "FIRST QUARTER" : "LAST QUARTER";
    if (fraction < 0.5) return waxing ? "WAXING CRESCENT" : "WANING CRESCENT";
    return waxing ? "WAXING GIBBOUS" : "WANING GIBBOUS";
}

const char *orbit_compass(int az) {
    static const char *P[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    az %= 360;
    if (az < 0) az += 360;
    return P[((az + 22) / 45) % 8];
}

void orbit_size_ft(float h, int *lo, int *hi) {
    double km = pow(10.0, -h / 5.0) * 1329.0;
    *lo = (int)lround(km / sqrt(0.25) * 3280.84);
    *hi = (int)lround(km / sqrt(0.05) * 3280.84);
}

double orbit_ld(float au) { return au * AU_KM / LD_KM; }
double orbit_miles(float au) { return au * AU_KM * 0.621371; }
double orbit_mph(float kms) { return kms * 2236.94; }
