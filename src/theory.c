#include "theory.h"
#include <stdio.h>

/* квинтовый круг по часовой стрелке от верхней кнопки: C G D A E B F# Db Ab Eb Bb F */
const int ROOT_ORDER[N_ROOTS] = { 0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5 };

static const char *ROOT_LABEL[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
static const char *SHARP_NAME[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

/* Тип аккорда = качество (мажор/минор) x модификатор: type = quality * N_MODS + mod.
 * Модификаторы по часовой стрелке от верхней кнопки внутреннего круга. Подпись на кнопке
 * показывает, что получится при текущем качестве (у минора: m7, mM7, m6 ...). */
typedef struct { const char *label, *suffix, *name; int n; int iv[MAX_CHORD]; } TypeDef;
static const TypeDef TYPES[N_TYPES] = {
    /* мажор */
    { "triad", "",     "major",          3, { 0, 4, 7 } },
    { "7",    "7",     "dominant 7",     4, { 0, 4, 7, 10 } },
    { "maj7", "maj7",  "major 7",        4, { 0, 4, 7, 11 } },
    { "6",    "6",     "sixth",          4, { 0, 4, 7, 9 } },
    { "9",    "9",     "ninth",          5, { 0, 4, 7, 10, 14 } },
    { "add9", "add9",  "added 9",        4, { 0, 4, 7, 14 } },
    { "sus2", "sus2",  "suspended 2",    3, { 0, 2, 7 } },
    { "sus4", "sus4",  "suspended 4",    3, { 0, 5, 7 } },
    { "aug",  "aug",   "augmented",      3, { 0, 4, 8 } },
    /* минор */
    { "triad", "m",    "minor",          3, { 0, 3, 7 } },
    { "m7",   "m7",    "minor 7",        4, { 0, 3, 7, 10 } },
    { "mM7",  "mM7",   "minor-major 7",  4, { 0, 3, 7, 11 } },
    { "m6",   "m6",    "minor sixth",    4, { 0, 3, 7, 9 } },
    { "m9",   "m9",    "minor ninth",    5, { 0, 3, 7, 10, 14 } },
    { "madd9","madd9", "minor added 9",  4, { 0, 3, 7, 14 } },
    { "sus2", "sus2",  "suspended 2",    3, { 0, 2, 7 } },
    { "sus4", "sus4",  "suspended 4",    3, { 0, 5, 7 } },
    { "dim",  "dim",   "diminished",     3, { 0, 3, 6 } },
};

static int type_ok(int t) { return t >= 0 && t < N_TYPES ? t : 0; }

int chord_type(int minor, int mod) {
    if (mod < 0 || mod >= N_MODS) mod = 0;
    return (minor ? N_MODS : 0) + mod;
}

const char *root_label(int pc) { return ROOT_LABEL[((pc % 12) + 12) % 12]; }
const char *type_label(int t) { return TYPES[type_ok(t)].label; }
const char *type_suffix(int t) { return TYPES[type_ok(t)].suffix; }
const char *type_long(int t) { return TYPES[type_ok(t)].name; }

int chord_intervals(int t, int *iv) {
    t = type_ok(t);
    for (int i = 0; i < TYPES[t].n; i++) iv[i] = TYPES[t].iv[i];
    return TYPES[t].n;
}

int chord_mask(int root, int t) {
    int iv[MAX_CHORD], n = chord_intervals(t, iv), m = 0;
    for (int i = 0; i < n; i++) m |= 1 << ((root + iv[i]) % 12);
    return m;
}

int snap_to_mask(int note, int mask) {
    if (!(mask & 0xFFF)) return note;
    for (int d = 0; d < 12; d++) {
        if (mask & (1 << (((note - d) % 12 + 12) % 12))) return note - d;
        if (mask & (1 << (((note + d) % 12 + 12) % 12))) return note + d;
    }
    return note;
}

int pad_voicing(int root, int t, int *notes) {
    int iv[MAX_CHORD], n = chord_intervals(t, iv);
    int base = 48 + root;                 /* C3..B3 */
    if (base > 54) base -= 12;            /* держим тонику в районе F#2..F#3 */
    for (int i = 0; i < n; i++) notes[i] = base + iv[i];
    return n;
}

void chord_name(char *out, size_t cap, int root, int t) {
    snprintf(out, cap, "%s%s", root_label(root), type_suffix(t));
}

void note_name(char *out, size_t cap, int midi) {
    snprintf(out, cap, "%s%d", SHARP_NAME[((midi % 12) + 12) % 12], midi / 12 - 1);
}

/* сколько разных звуков в аккорде (add9 - четыре, девятая как вторая ступень) */
static int mask_count(int mask) {
    int n = 0;
    for (int i = 0; i < 12; i++) n += (mask >> i) & 1;
    return n;
}

/* следующий звук аккорда строго выше note */
static int next_in_mask(int note, int mask) {
    for (int d = 1; d <= 12; d++) if (mask & (1 << ((note + d) % 12))) return note + d;
    return note + 12;
}

void stringmap_build(StringMap *m, int chord_on, int root, int t, int base) {
    int mask = chord_on ? chord_mask(root, t) : 0xFFF;
    /* В аккорде струны идут парами: каждая пара - следующий звук аккорда вверх.
     * 25 струн = 13 звуков; у трезвучия это больше четырёх октав (как у Omnichord),
     * середина диапазона - около середины клавиатуры (base + 12). */
    int tones = (N_STRINGS + CHORD_SPREAD - 1) / CHORD_SPREAD;
    int cur = 0;
    if (chord_on) {
        int span = tones * 12 / mask_count(mask);
        cur = snap_to_mask(base + 12 - span / 2, mask);
    }
    m->nzones = 0;
    for (int i = 0; i < N_STRINGS; i++) {
        int nt;
        if (!chord_on) nt = base + i;
        else {
            if (i > 0 && i % CHORD_SPREAD == 0) cur = next_in_mask(cur, mask);
            nt = cur;
        }
        m->note[i] = nt;
        if (m->nzones == 0 || m->zone_note[m->nzones - 1] != nt) {
            m->zone_lo[m->nzones] = i;
            m->zone_note[m->nzones] = nt;
            m->nzones++;
        }
        m->zone_hi[m->nzones - 1] = i;
        m->zone_of[i] = m->nzones - 1;
    }
}

/* ---------- гитара ---------- */
/* стандартный строй: E2 A2 D3 G3 B3 E4, струна 0 - самая толстая */
const int GT_TUNING[N_GSTR] = { 40, 45, 50, 55, 59, 64 };

/* роль интервала в аккорде: сколько стоит его потерять */
static int tone_weight(int interval) {
    switch (interval % 12) {
    case 0: return 40;                          /* тоника */
    case 3: case 4: case 2: case 5: return 25;  /* терция (или sus2/sus4 вместо неё) */
    case 7: return 5;                           /* чистая квинта - можно опустить */
    default: return 18;                         /* септима, секста, нона, ум./ув. квинта */
    }
}

/* Подбор аппликатуры, как сыграл бы гитарист: перебор позиций грифа (окно в 4 лада + открытые
 * струны), на каждой струне - звук аккорда или глушение (глушить можно только басовые, подряд).
 * Оценка: все звуки аккорда на месте, в басу тоника (или квинта), больше звучащих струн,
 * ниже по грифу, хватает четырёх пальцев (баррэ считается одним пальцем). */
void guitar_voicing(GuitarChord *g, int chord_on, int root, int t, int shift) {
    if (!chord_on) {                            /* без аккорда - открытые струны */
        for (int s = 0; s < N_GSTR; s++) { g->fret[s] = 0; g->note[s] = GT_TUNING[s] + shift; }
        return;
    }
    int iv[MAX_CHORD], n = chord_intervals(t, iv), mask = chord_mask(root, t);
    int best[N_GSTR] = { 0 }, have = 0;
    float best_score = -1e9f;
    for (int f0 = 1; f0 <= 6; f0++) {
        int opt[N_GSTR][6], nopt[N_GSTR];
        for (int s = 0; s < N_GSTR; s++) {
            nopt[s] = 0;
            if (mask & (1 << (GT_TUNING[s] % 12))) opt[s][nopt[s]++] = 0;
            for (int f = f0; f < f0 + 4; f++)
                if (mask & (1 << ((GT_TUNING[s] + f) % 12))) opt[s][nopt[s]++] = f;
            if (s < 2) opt[s][nopt[s]++] = -1;  /* две басовые можно заглушить */
        }
        int pick[N_GSTR] = { 0 };
        for (;;) {
            int fr[N_GSTR], ok = 1;
            for (int s = 0; s < N_GSTR; s++) {
                if (!nopt[s]) { ok = 0; break; }
                fr[s] = opt[s][pick[s]];
            }
            if (ok && fr[1] < 0 && fr[0] >= 0) ok = 0;                 /* глушим только снизу подряд */
            if (ok) {
                int mn = 99, mx = 0, nfret = 0, at_min = 0, lo = -1, hi = -1, sounding = 0, opens = 0;
                for (int s = 0; s < N_GSTR; s++) {
                    if (fr[s] >= 0) sounding++;
                    if (fr[s] == 0) opens++;
                    if (fr[s] > 0) { nfret++; if (fr[s] < mn) mn = fr[s]; if (fr[s] > mx) mx = fr[s]; }
                }
                for (int s = 0; s < N_GSTR; s++)
                    if (fr[s] == mn) { at_min++; if (lo < 0) lo = s; hi = s; }
                int fingers = (at_min >= 2) ? nfret - at_min + 1 : nfret;
                if (nfret && mx - mn > 3) ok = 0;
                if (fingers > 4) ok = 0;
                if (at_min >= 2 && nfret > 4)                           /* под баррэ не бывает открытых */
                    for (int s = lo; s <= hi; s++) if (fr[s] == 0) ok = 0;
                if (ok) {
                    float sc = 0.0f;
                    int got = 0, bass = -1;
                    for (int s = 0; s < N_GSTR; s++)
                        if (fr[s] >= 0) {
                            int pc = (GT_TUNING[s] + fr[s]) % 12;
                            got |= 1 << pc;
                            if (bass < 0) bass = pc;
                        }
                    for (int k = 0; k < n; k++)
                        if (!(got & (1 << ((root + iv[k]) % 12)))) sc -= (float)tone_weight(iv[k]);
                    int bi = ((bass - root) % 12 + 12) % 12;
                    sc += bi == 0 ? 25.0f : (bi == 7 ? 6.0f : -20.0f);
                    sc += 4.0f * (float)sounding + 1.0f * (float)opens;
                    if (nfret) sc -= 1.5f * (float)mn + 2.5f * (float)(mx - mn);   /* ниже и без растяжки */
                    if (mx >= 4) {                       /* открытые внутри высокой позиции звучат чужеродно */
                        int first = -1, last = -1;
                        for (int s = 0; s < N_GSTR; s++) if (fr[s] > 0) { if (first < 0) first = s; last = s; }
                        for (int s = first + 1; s < last; s++) if (fr[s] == 0) sc -= 3.0f;
                    }
                    if (sc > best_score) { best_score = sc; have = 1; for (int s = 0; s < N_GSTR; s++) best[s] = fr[s]; }
                }
            }
            int s = 0;                                                 /* следующая комбинация */
            while (s < N_GSTR && ++pick[s] >= nopt[s]) pick[s++] = 0;
            if (s == N_GSTR) break;
        }
    }
    for (int s = 0; s < N_GSTR; s++) {
        g->fret[s] = have ? best[s] : 0;
        g->note[s] = g->fret[s] >= 0 ? GT_TUNING[s] + g->fret[s] + shift : -1;
    }
}

void guitar_shape_str(const GuitarChord *g, char *out, size_t cap) {
    size_t k = 0;
    for (int s = 0; s < N_GSTR && k + 2 < cap; s++) {
        if (s) out[k++] = ' ';
        out[k++] = g->fret[s] < 0 ? 'x' : (char)('0' + (g->fret[s] > 9 ? 9 : g->fret[s]));
    }
    out[k] = 0;
}
