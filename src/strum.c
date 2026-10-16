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

float strum_pos_from_xy(float x, float y) {
    float ps = ((float)STR_BOTTOM - y) / strum_pitch();
    float pk = pos_by_keys(y);
    float w = (x - (float)NECK_X1) / (float)(BULB_X1 - NECK_X1);
    if (w < 0.0f) w = 0.0f;
    if (w > 1.0f) w = 1.0f;
    float p = pk + (ps - pk) * w;
    if (p < 0.0f) p = 0.0f;
    if (p > (float)N_STRINGS - 0.001f) p = (float)N_STRINGS - 0.001f;
    return p;
}

float gtr_pitch(void) { return (float)(GT_BOTTOM - GT_TOP) / (float)N_GSTR; }
float gtr_string_y(int i) { return (float)GT_TOP + ((float)i + 0.5f) * gtr_pitch(); }
float gtr_thick(int i) {
    static const float T[N_GSTR] = { 15.0f, 12.5f, 10.5f, 8.5f, 6.5f, 5.0f };
    return T[i < 0 ? 0 : (i >= N_GSTR ? N_GSTR - 1 : i)];
}
float gtr_fret_x(int f) {                /* как на настоящем грифе: каждый лад короче предыдущего */
    return (float)GT_NUT_X1 + GT_SCALE * (1.0f - powf(2.0f, -(float)f / 12.0f));
}
float gtr_pos_from_y(float y) {
    float p = (y - (float)GT_TOP) / gtr_pitch();
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
