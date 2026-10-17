#include "strum.h"
#include "layout.h"
#include <math.h>

/* насколько надо зайти за границу зоны, чтобы она сменилась (в долях струны):
 * палец, лежащий на границе, не "дребезжит" */
#define HYST 0.18f

#define N_WHITE 15
#define WHITE_GAP 3.0f

float strum_pitch(void) { return (float)(STR_BOTTOM - STR_TOP) / (float)N_STRINGS; }
float strum_string_y(int i) { return (float)STR_BOTTOM - ((float)i + 0.5f) * strum_pitch(); }

int key_is_black(int i) {
    int pc = i % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

static int whites_below(int i) {
    int n = 0;
    for (int k = 0; k < i; k++) if (!key_is_black(k)) n++;
    return n;
}

static float white_h(void) { return (float)(KB_BOTTOM - KB_TOP) / (float)N_WHITE; }

float key_center_y(int i) {
    float wh = white_h();
    int w = whites_below(i);
    if (key_is_black(i)) return (float)KB_BOTTOM - (float)w * wh;          /* на стыке белых */
    return (float)KB_BOTTOM - ((float)w + 0.5f) * wh;
}

void white_key_extents(int i, float *y0, float *y1, float *b0, float *b1) {
    float wh = white_h(), c = key_center_y(i);
    int black_above = i + 1 < N_STRINGS && key_is_black(i + 1);
    int black_below = i - 1 >= 0 && key_is_black(i - 1);
    /* передняя часть: на стыке с соседней белой - обычный зазор, а где между белыми
     * проходит нить чёрной клавиши - зазор шире, чтобы нить шла по чистому месту */
    *y0 = c - wh / 2.0f + (black_above ? LINE_CLEAR : WHITE_GAP / 2.0f);
    *y1 = c + wh / 2.0f - (black_below ? LINE_CLEAR : WHITE_GAP / 2.0f);
    /* задняя часть: между чёрными клавишами */
    *b0 = black_above ? c - wh / 2.0f + BK_H / 2.0f + WHITE_GAP : *y0;
    *b1 = black_below ? c + wh / 2.0f - BK_H / 2.0f - WHITE_GAP : *y1;
}

static float pos_by_keys(float y) {
    /* центры клавиш идут снизу вверх; между соседними - линейно */
    float c0 = key_center_y(0);
    if (y >= c0) return 0.5f - (y - c0) / (white_h() * 0.5f) * 0.5f;
    for (int i = 0; i + 1 < N_STRINGS; i++) {
        float a = key_center_y(i), b = key_center_y(i + 1);
        if (y <= a && y >= b) return (float)i + 0.5f + (a - y) / (a - b);
    }
    float cl = key_center_y(N_STRINGS - 1);
    return (float)N_STRINGS - 0.5f + (cl - y) / (white_h() * 0.5f) * 0.5f;
}

/* ---- хитбоксы клавиш (по просьбе автора) ----
 * Зона K (x < KZ_BLACK) - где выступают чёрные: только чёрные, каждая до середины до соседней
 *   чёрной. Белые здесь не нажимаются, даже в щелях между чёрными.
 * Зона A (KZ_BLACK..KZ_WHITE) - узкий переход: по высоте, как нарисовано (чёрная - своя полоса).
 * Зона B (KZ_WHITE..WK_X1) - середина и передняя часть белых: чёрных нет, белые делят высоту
 *   между собой. Глиссандо здесь играет только белые.
 * Полоса клавиши k в "струнах" - отрезок [k, k+1]: положение внутри полосы линейно по высоте.
 * Дальше (нити, полосы струн) - как раньше: плавный переход к положению по струнам. */
#define BK_MARGIN 1.0f
#define KZ_BLACK ((float)WK_X0 + 12.0f)  /* левее - только чёрные */
#define KZ_WHITE ((float)WK_X0 + 28.0f)  /* правее - только белые */
#define KEY_EDGE 18.0f                 /* крайние клавиши - с запасом за край клавиатуры */

typedef struct { float top, bot; } KeyBand;     /* y: top < bot; ключ - индекс массива */
static KeyBand g_band_a[N_STRINGS], g_band_b[N_STRINGS], g_band_k[N_STRINGS];
static int g_bands_ready = 0;

static void bands_init(void) {
    float wh = white_h();
    for (int i = 0; i < N_STRINGS; i++) {
        float c = key_center_y(i);
        /* зона B: белая от середины до соседней белой снизу до середины до соседней сверху */
        if (!key_is_black(i)) {
            int lo = i - 1, hi = i + 1;
            while (lo >= 0 && key_is_black(lo)) lo--;
            while (hi < N_STRINGS && key_is_black(hi)) hi++;
            g_band_b[i].bot = lo >= 0 ? (c + key_center_y(lo)) * 0.5f : c + wh * 0.5f + KEY_EDGE;
            g_band_b[i].top = hi < N_STRINGS ? (c + key_center_y(hi)) * 0.5f : c - wh * 0.5f - KEY_EDGE;
        } else {
            g_band_b[i].top = g_band_b[i].bot = c;      /* в зоне B чёрных нет */
        }
        /* зона K: чёрная - до середины до соседней чёрной (крайние - до края клавиатуры) */
        if (key_is_black(i)) {
            int lo = i - 1, hi = i + 1;
            while (lo >= 0 && !key_is_black(lo)) lo--;
            while (hi < N_STRINGS && !key_is_black(hi)) hi++;
            g_band_k[i].bot = lo >= 0 ? (c + key_center_y(lo)) * 0.5f : (float)KB_BOTTOM + KEY_EDGE;
            g_band_k[i].top = hi < N_STRINGS ? (c + key_center_y(hi)) * 0.5f : (float)KB_TOP - KEY_EDGE;
        } else {
            g_band_k[i].top = g_band_k[i].bot = c;      /* в зоне K белых нет */
        }
        /* зона A */
        if (key_is_black(i)) {
            g_band_a[i].top = c - BK_H * 0.5f - BK_MARGIN;
            g_band_a[i].bot = c + BK_H * 0.5f + BK_MARGIN;
        }
    }
    for (int i = 0; i < N_STRINGS; i++) {
        if (key_is_black(i)) continue;
        float c = key_center_y(i);
        if (i > 0 && key_is_black(i - 1)) g_band_a[i].bot = g_band_a[i - 1].top;
        else g_band_a[i].bot = g_band_b[i].bot;
        if (i + 1 < N_STRINGS && key_is_black(i + 1)) g_band_a[i].top = g_band_a[i + 1].bot;
        else g_band_a[i].top = g_band_b[i].top;
        (void)c;
    }
    g_bands_ready = 1;
}

/* положение по полосам клавиш (зона A или B) */
/* only: 0 - все клавиши, 1 - только белые, 2 - только чёрные */
static float pos_by_bands(const KeyBand *b, int only, float y) {
    int first = -1, last = -1;
    for (int i = 0; i < N_STRINGS; i++) {
        if ((only == 1 && key_is_black(i)) || (only == 2 && !key_is_black(i))) continue;
        if (first < 0) first = i;
        last = i;
        if (y <= b[i].bot && y >= b[i].top) {
            float t = (b[i].bot - y) / (b[i].bot - b[i].top);
            return (float)i + (t < 0.999f ? t : 0.999f);   /* верхний край - ещё эта клавиша, не следующая */
        }
    }
    return y > b[first].bot ? (float)first : (float)last + 0.999f;     /* за краем клавиатуры */
}

int strum_key_kind(float x) {
    if (x < KZ_BLACK) return 2;
    if (x < KZ_WHITE) return 0;
    return x < (float)WK_X1 ? 1 : 0;
}

float strum_pos_from_xy(float x, float y) {
    if (!g_bands_ready) bands_init();
    float p;
    if (x < KZ_BLACK) p = pos_by_bands(g_band_k, 2, y);
    else if (x < KZ_WHITE) p = pos_by_bands(g_band_a, 0, y);
    else if (x < (float)WK_X1) p = pos_by_bands(g_band_b, 1, y);
    else {
        /* нити: от полос белых к прежней плавной интерполяции, потом к положению по струнам */
        float t = (x - (float)WK_X1) / (float)(NECK_X1 - WK_X1);
        float pk;
        if (t < 1.0f) pk = pos_by_bands(g_band_b, 1, y) + (pos_by_keys(y) - pos_by_bands(g_band_b, 1, y)) * t;
        else pk = pos_by_keys(y);
        float ps = ((float)STR_BOTTOM - y) / strum_pitch();
        float w = (x - (float)NECK_X1) / (float)(BULB_X1 - NECK_X1);
        if (w < 0.0f) w = 0.0f;
        if (w > 1.0f) w = 1.0f;
        p = pk + (ps - pk) * w;
    }
    if (p < 0.0f) p = 0.0f;
    if (p > (float)N_STRINGS - 0.001f) p = (float)N_STRINGS - 0.001f;
    return p;
}

int strum_key_at(float x, float y) {
    int k = (int)strum_pos_from_xy(x, y);
    return k < 0 ? 0 : (k >= N_STRINGS ? N_STRINGS - 1 : k);
}

/* какие клавиши (струны) палец задел по пути от (x0,y0) к (x1,y1), начиная от клавиши *cur.
 * Граница засчитывается, только если палец зашёл за неё на HYST (дребезг на стыке не играет).
 * На передней части белых чёрные пропускаются. Возвращает число клавиш, *cur - последняя. */
int strum_cross(int *cur, float x0, float y0, float x1, float y1, int *keys, float *fracs, int max) {
    (void)x0;
    float p0 = strum_pos_from_xy(x1, y0), p1 = strum_pos_from_xy(x1, y1);
    int only = strum_key_kind(x1), k = *cur, n = 0;
#define SKIP(i) ((only == 1 && key_is_black(i)) || (only == 2 && !key_is_black(i)))
    if (k < 0 || k >= N_STRINGS || SKIP(k)) {        /* перешёл в другую зону - клавиша по месту, без звука */
        k = (int)p0;
        if (k >= N_STRINGS) k = N_STRINGS - 1;
    }
    for (int guard = 0; guard < N_STRINGS && n < max; guard++) {
        int nx;
        float edge;
        if (p1 >= (float)(k + 1) + HYST) {                     /* вверх */
            nx = k + 1;
            while (nx < N_STRINGS && SKIP(nx)) nx++;
            if (nx >= N_STRINGS || p1 < (float)nx + HYST) break;
            edge = (float)nx;
        } else if (p1 <= (float)k - HYST) {                    /* вниз */
            nx = k - 1;
            while (nx >= 0 && SKIP(nx)) nx--;
            if (nx < 0 || p1 > (float)(nx + 1) - HYST) break;
            edge = (float)(nx + 1);
        } else break;
        float fr = (p1 != p0) ? (edge - p0) / (p1 - p0) : 1.0f;
        keys[n] = nx;
        fracs[n] = fr < 0.0f ? 0.0f : (fr > 1.0f ? 1.0f : fr);
        n++;
        k = nx;
    }
#undef SKIP
    *cur = k;
    return n;
}

void strum_down_xy(StrumFinger *f, int id, float x, float y, const StringMap *m, StrumHit hit, void *user) {
    f->id = id;
    f->key = strum_key_at(x, y);
    f->pos = strum_pos_from_xy(x, y);
    f->zone = m->zone_of[f->key];
    hit(f->zone, 0.0f, user);
}

void strum_move_xy(StrumFinger *f, float x, float y, const StringMap *m, StrumHit hit, void *user) {
    int keys[N_STRINGS];
    float fr[N_STRINGS];
    int n = strum_cross(&f->key, f->x, f->y, x, y, keys, fr, N_STRINGS);
    for (int i = 0; i < n; i++) {
        int z = m->zone_of[keys[i]];
        if (z != f->zone) { f->zone = z; hit(z, fr[i], user); }   /* пара одинаковых струн звучит один раз */
    }
    f->pos = strum_pos_from_xy(x, y);
}

/* ---- гитара: пересечение линии струны ---- */
static int gtr_side(int i, float y) {
    float c = gtr_string_y(i);
    return y > c + GTR_LINE_HYST ? 1 : (y < c - GTR_LINE_HYST ? -1 : 0);
}

void strum_gtr_down(StrumFinger *f, int id, float x, float y, StrumHit hit, void *user) {
    (void)x;
    f->id = id;
    f->pos = gtr_pos_from_y(y);
    int s = (int)f->pos;
    for (int i = 0; i < N_GSTR; i++) f->gside[i] = (signed char)gtr_side(i, y);
    f->zone = s;
    f->gfresh = 4;                       /* ~65 мс: удар касанием уже прозвучал */
    f->gfresh_s = s;
    hit(s, 0.0f, user);
}

void strum_gtr_move(StrumFinger *f, float x, float y, StrumHit hit, void *user) {
    (void)x;
    float y0 = f->y;
    int ns = 0, order[N_GSTR];
    float fr[N_GSTR];
    for (int i = 0; i < N_GSTR; i++) {
        int sd = gtr_side(i, y);
        if (sd == 0) continue;           /* на линии - сторону не меняем */
        int was = f->gside[i];
        f->gside[i] = (signed char)sd;
        if (was == 0 || was == sd) continue;
        if (f->gfresh > 0 && i == f->gfresh_s) { f->gfresh = 0; continue; }   /* продолжение удара касанием - один раз */
        float c = gtr_string_y(i), t = (y != y0) ? (c - y0) / (y - y0) : 1.0f;
        int k = ns++;
        while (k > 0 && fr[k - 1] > t) { fr[k] = fr[k - 1]; order[k] = order[k - 1]; k--; }
        fr[k] = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        order[k] = i;
    }
    if (f->gfresh > 0) f->gfresh--;
    for (int k = 0; k < ns; k++) { f->zone = order[k]; hit(order[k], fr[k], user); }
    f->pos = gtr_pos_from_y(y);
}

float gtr_pitch(void) { return (float)(GT_BOTTOM - GT_TOP) / (float)N_GSTR; }
float gtr_string_y(int i) { return (float)GT_BOTTOM - ((float)i + 0.5f) * gtr_pitch(); }   /* 0 - толстая, внизу */
float gtr_thick(int i) {
    static const float T[N_GSTR] = { 15.0f, 12.5f, 10.5f, 8.5f, 6.5f, 5.0f };
    return T[i < 0 ? 0 : (i >= N_GSTR ? N_GSTR - 1 : i)];
}
float gtr_fret_x(int f) {                /* как на настоящем грифе: каждый лад короче предыдущего */
    return (float)GT_NUT_X1 + GT_SCALE * (1.0f - powf(2.0f, -(float)f / 12.0f));
}
float gtr_pos_from_y(float y) {
    float p = ((float)GT_BOTTOM - y) / gtr_pitch();
    if (p < 0.0f) p = 0.0f;
    if (p > (float)N_GSTR - 0.001f) p = (float)N_GSTR - 0.001f;
    return p;
}

static int zone_at(const StringMap *m, float pos) {
    int s = (int)pos;
    if (s < 0) s = 0;
    if (s >= N_STRINGS) s = N_STRINGS - 1;
    return m->zone_of[s];
}

void strum_down(StrumFinger *f, int id, float pos, const StringMap *m, StrumHit hit, void *user) {
    f->id = id;
    f->pos = pos;
    f->zone = zone_at(m, pos);
    hit(f->zone, 0.0f, user);
}

void strum_move(StrumFinger *f, float pos, const StringMap *m, StrumHit hit, void *user) {
    float p0 = f->pos, span = pos - p0;
    for (int guard = 0; guard < N_STRINGS + 1; guard++) {
        int z = f->zone;
        float lo = (float)m->zone_lo[z], hi = (float)m->zone_hi[z] + 1.0f;
        float boundary;
        if (pos >= hi + HYST && z < m->nzones - 1) { boundary = hi; z++; }
        else if (pos < lo - HYST && z > 0)         { boundary = lo; z--; }
        else break;
        float frac = (span != 0.0f) ? (boundary - p0) / span : 0.0f;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        f->zone = z;
        hit(z, frac, user);
    }
    f->pos = pos;
}

void strum_remap(StrumFinger *f, const StringMap *m) {
    if (f->id >= 0) f->zone = zone_at(m, f->pos);
}
