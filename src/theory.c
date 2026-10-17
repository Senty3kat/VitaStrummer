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
static void voicing_search(GuitarChord *g, int chord_on, int root, int t, int shift, int pos, int strict,
                           const GuitarChord *excl, int nexcl, int *found);

static int same_shape(const GuitarChord *a, const int *fr) {
    for (int s = 0; s < N_GSTR; s++) if (a->fret[s] != fr[s]) return 0;
    return 1;
}

/* Позиции: у каждой своя форма. Со 2-го лада указательный палец стоит ровно на ладу позиции.
 * Если на этом ладу нет ни одного звука аккорда - рука всё равно в позиции (лады pos..pos+3),
 * но форма берётся та, которой нет в других позициях. Совсем ничего - ближайшая соседняя, потом лучшая. */
#define GT_FRETS_MAX 12                   /* на экране 12 ладов */

/* ---- мажор и минор: привычные формы ----
 * Каждая форма - лады относительно лада тоники на басовой струне (f), -1 - глушить. */
typedef struct { int bass_str; int open_only; int rel[N_GSTR]; } Shape;
#define X (-99)
static const Shape MAJ_SHAPES[] = {
    { 0, 0, { 0, 2, 2, 1, 0, 0 } },          /* форма "ми", баррэ, тоника на 6-й (f = 0 - открытый E) */
    { 1, 0, { X, 0, 2, 2, 2, 0 } },          /* форма "ля", баррэ, тоника на 5-й (f = 0 - открытый A) */
    { 1, 0, { X, 0, -1, -3, -2, -3 } },      /* форма "до", тоника на 5-й (f = 3 - открытый C) */
    { 0, 1, { 0, -1, -3, -3, -3, 0 } },      /* открытый G: 3 2 0 0 0 3 */
    { 2, 1, { X, X, 0, 2, 3, 2 } },          /* открытый D: x x 0 2 3 2 (бас на 4-й - так играют все) */
};
static const Shape MIN_SHAPES[] = {
    { 0, 0, { 0, 2, 2, 0, 0, 0 } },          /* форма "ми минор", тоника на 6-й */
    { 1, 0, { X, 0, 2, 2, 1, 0 } },          /* форма "ля минор", тоника на 5-й */
    { 2, 1, { X, X, 0, 2, 3, 1 } },          /* открытый Dm */
};
#undef X

/* трезвучие: 0 - мажор, 1 - минор, -1 - другой аккорд */
static int triad_kind(int t) {
    if (t == chord_type(0, 0)) return 0;
    if (t == chord_type(1, 0)) return 1;
    return -1;
}

/* все привычные формы аккорда: лады и позиция (открытые - позиция 1), по возрастанию позиции */
static int triad_shapes(int root, int kind, int frets[][N_GSTR], int *pos) {
    const Shape *sh = kind ? MIN_SHAPES : MAJ_SHAPES;
    int ns = kind ? (int)(sizeof(MIN_SHAPES) / sizeof(MIN_SHAPES[0])) : (int)(sizeof(MAJ_SHAPES) / sizeof(MAJ_SHAPES[0]));
    int n = 0;
    for (int k = 0; k < ns; k++) {
        int f = ((root - GT_TUNING[sh[k].bass_str]) % 12 + 12) % 12;   /* лад тоники на басовой струне */
        if (sh[k].open_only && f != 0 && !(sh[k].bass_str == 0 && f == 3)) continue;
        int fr[N_GSTR], ok = 1, mn = 99, open = 0;
        for (int s = 0; s < N_GSTR; s++) {
            if (sh[k].rel[s] <= -50) { fr[s] = -1; continue; }
            fr[s] = f + sh[k].rel[s];
            if (fr[s] < 0 || fr[s] > GT_FRETS_MAX) ok = 0;
            if (fr[s] == 0) open = 1;
            if (fr[s] > 0 && fr[s] < mn) mn = fr[s];
        }
        if (!ok) continue;
        /* открытые струны - только у открытых форм (у баррэ с f > 0 их нет) */
        int p = open ? 1 : mn;
        if (p > GT_POS_MAX) continue;
        int dup = 0;
        for (int j = 0; j < n; j++) if (pos[j] == p) dup = 1;
        if (dup) continue;
        int at = n;                                       /* вставка по возрастанию позиции */
        while (at > 0 && pos[at - 1] > p) {
            pos[at] = pos[at - 1];
            for (int s = 0; s < N_GSTR; s++) frets[at][s] = frets[at - 1][s];
            at--;
        }
        pos[at] = p;
        for (int s = 0; s < N_GSTR; s++) frets[at][s] = fr[s];
        n++;
    }
    return n;
}

int guitar_positions(int root, int t, int *out) {
    int kind = triad_kind(t);
    if (kind < 0) {
        for (int p = 1; p <= GT_POS_CX; p++) out[p - 1] = p;
        return GT_POS_CX;
    }
    int fr[8][N_GSTR];
    return triad_shapes(root, kind, fr, out);
}

int guitar_snap_pos(int root, int t, int pos) {
    int ps[GT_POS_MAX], n = guitar_positions(root, t, ps), best = pos, bd = 99;
    for (int i = 0; i < n; i++) {
        int d = ps[i] > pos ? ps[i] - pos : pos - ps[i];
        if (d < bd) { bd = d; best = ps[i]; }           /* при равенстве - нижняя */
    }
    return best;
}

void guitar_voicing(GuitarChord *g, int chord_on, int root, int t, int shift, int pos) {
    int found = 0;
    g->pos = 0;
    int kind = chord_on ? triad_kind(t) : -1;
    if (kind >= 0) {                                      /* мажор / минор - привычные формы */
        int fr[8][N_GSTR], ps[8], n = triad_shapes(root, kind, fr, ps);
        if (n > 0) {
            int k = 0;                                    /* авто - самая нижняя (открытая, если есть) */
            if (pos > 0) {
                int bd = 99;
                for (int i = 0; i < n; i++) {
                    int d = ps[i] > pos ? ps[i] - pos : pos - ps[i];
                    if (d < bd) { bd = d; k = i; }
                }
            }
            for (int s = 0; s < N_GSTR; s++) {
                g->fret[s] = fr[k][s];
                g->note[s] = fr[k][s] >= 0 ? GT_TUNING[s] + fr[k][s] + shift : -1;
            }
            g->pos = ps[k];
            return;
        }
    }
    if (pos > GT_POS_CX) pos = GT_POS_CX;
    if (pos >= 1 && chord_on) {
        /* таблица всех позиций: сначала строгие формы, потом остальные позиции по порядку -
         * каждая берёт форму, которой ещё нет в таблице */
        GuitarChord tab[GT_POS_CX + 1];
        int ok[GT_POS_CX + 1] = { 0 }, nused = 0;
        GuitarChord used[GT_POS_CX];
        for (int q = 1; q <= GT_POS_CX; q++) {
            voicing_search(&tab[q], 1, root, t, shift, q, q >= 2, NULL, 0, &ok[q]);
            if (ok[q] && q == 1) {                   /* 1-я позиция: открытая форма или палец на 1-м ладу */
                int open = 0, mn = 99;
                for (int s = 0; s < N_GSTR; s++) {
                    if (tab[q].fret[s] == 0) open = 1;
                    if (tab[q].fret[s] > 0 && tab[q].fret[s] < mn) mn = tab[q].fret[s];
                }
                if (!open && mn != 1) ok[q] = 0;
            }
            if (ok[q]) used[nused++] = tab[q];
        }
        for (int q = 1; q <= GT_POS_CX; q++) {
            if (ok[q]) continue;
            voicing_search(&tab[q], 1, root, t, shift, q, 0, used, nused, &ok[q]);
            if (ok[q]) used[nused++] = tab[q];
        }
        if (ok[pos]) { *g = tab[pos]; g->pos = pos; return; }
    }
    static const int NEAR[7] = { 0, 1, -1, 2, -2, 3, -3 };
    for (int k = 0; pos > 0 && k < 7 && !found; k++) {
        int p = pos + NEAR[k];
        if (p >= 1 && p <= GT_POS_CX) { voicing_search(g, chord_on, root, t, shift, p, 0, NULL, 0, &found); g->pos = p; }
    }
    if (!found) voicing_search(g, chord_on, root, t, shift, 0, 0, NULL, 0, &found);
}

/* pos = 0 - лучшая позиция; pos > 0 - рука стоит с лада pos: зажатые лады только pos..pos+3;
 * strict - самый нижний зажатый лад ровно pos; excl - формы, которые брать нельзя */
static void voicing_search(GuitarChord *g, int chord_on, int root, int t, int shift, int pos, int strict,
                           const GuitarChord *excl, int nexcl, int *found) {
    if (!chord_on) {                            /* без аккорда - открытые струны */
        for (int s = 0; s < N_GSTR; s++) { g->fret[s] = 0; g->note[s] = GT_TUNING[s] + shift; }
        *found = 1;
        return;
    }
    int iv[MAX_CHORD], n = chord_intervals(t, iv), mask = chord_mask(root, t);
    int best[N_GSTR] = { 0 }, have = 0;
    float best_score = -1e9f;
    int fa = pos > 0 ? pos : 1, fb = pos > 0 ? pos : 6;
    for (int f0 = fa; f0 <= fb; f0++) {
        int opt[N_GSTR][6], nopt[N_GSTR];
        for (int s = 0; s < N_GSTR; s++) {
            nopt[s] = 0;
            /* открытые струны - в авто и в первой позиции; со 2-го лада форма закрытая */
            if (pos <= 1 && (mask & (1 << (GT_TUNING[s] % 12)))) opt[s][nopt[s]++] = 0;
            for (int f = f0; f < f0 + 4; f++)
                if (mask & (1 << ((GT_TUNING[s] + f) % 12))) opt[s][nopt[s]++] = f;
            if (s < 2) opt[s][nopt[s]++] = -1;  /* две басовые можно заглушить */
            else if ((pos >= 2 && !strict) || excl) opt[s][nopt[s]++] = -1;  /* запасная форма: глушить можно любые */
            else if (s >= 4 && pos >= 2) opt[s][nopt[s]++] = -1;   /* в позиции - и две верхние (реже) */
        }
        int pick[N_GSTR] = { 0 };
        for (;;) {
            int fr[N_GSTR], ok = 1;
            for (int s = 0; s < N_GSTR; s++) {
                if (!nopt[s]) { ok = 0; break; }
                fr[s] = opt[s][pick[s]];
            }
            if (ok && fr[1] < 0 && fr[0] >= 0) ok = 0;                 /* глушим только снизу подряд */
            if (ok && strict && fr[4] < 0 && fr[5] >= 0) ok = 0;       /* и сверху подряд */
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
                /* в позиции указательный палец стоит ровно на ладу pos - у каждой позиции своя форма */
                if (pos > 0 && nfret && mn < pos) ok = 0;
                if (strict && mn != pos) ok = 0;
                for (int e = 0; ok && e < nexcl; e++) if (same_shape(&excl[e], fr)) ok = 0;
                if (sounding < 3) ok = 0;
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
                    for (int s = 2; s < N_GSTR; s++)              /* заглушённые не снизу - запасной вариант */
                        if (fr[s] < 0) sc -= 8.0f;
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
    *found = have;
    if (!have) return;
    for (int s = 0; s < N_GSTR; s++) {
        g->fret[s] = best[s];
        g->note[s] = g->fret[s] >= 0 ? GT_TUNING[s] + g->fret[s] + shift : -1;
    }
}

void guitar_shape_str(const GuitarChord *g, char *out, size_t cap) {
    size_t k = 0;
    for (int s = 0; s < N_GSTR && k + 4 < cap; s++) {
        if (s) out[k++] = ' ';
        int f = g->fret[s];
        if (f < 0) out[k++] = 'x';
        else if (f >= 10) { out[k++] = (char)('0' + f / 10); out[k++] = (char)('0' + f % 10); }
        else out[k++] = (char)('0' + f);
    }
    out[k] = 0;
}

/* ---------- тональность ---------- */
static const int SCALE_STEPS[2][7] = { { 0, 2, 4, 5, 7, 9, 11 },     /* мажор */
                                       { 0, 2, 3, 5, 7, 8, 10 } };   /* натуральный минор */
/* качество аккорда на ступени: 0 мажор, 1 минор, 2 уменьшённый */
static const int DEG_QUAL[2][7] = { { 0, 1, 1, 0, 0, 1, 2 },
                                    { 1, 2, 0, 1, 1, 0, 0 } };
static const char *DEG_NAME[2][7] = { { "I", "ii", "iii", "IV", "V", "vi", "vii" },
                                      { "i", "ii", "III", "iv", "v", "VI", "VII" } };

int key_scale_mask(int key_root, int key_minor) {
    int m = 0;
    for (int i = 0; i < 7; i++) m |= 1 << ((key_root + SCALE_STEPS[key_minor ? 1 : 0][i]) % 12);
    return m;
}

int key_degree(int key_root, int key_minor, int pc) {
    for (int i = 0; i < 7; i++)
        if ((key_root + SCALE_STEPS[key_minor ? 1 : 0][i]) % 12 == ((pc % 12) + 12) % 12) return i;
    return -1;
}

int key_degree_quality(int key_minor, int deg) { return (deg >= 0 && deg < 7) ? DEG_QUAL[key_minor ? 1 : 0][deg] : 0; }
const char *key_degree_name(int key_minor, int deg) { return (deg >= 0 && deg < 7) ? DEG_NAME[key_minor ? 1 : 0][deg] : ""; }

void stringmap_build_scale(StringMap *m, int mask, int base) {
    /* каждая струна - следующий звук лада вверх: 25 струн - около трёх с половиной октав */
    int cur = base;
    while (!(mask & (1 << (cur % 12)))) cur++;
    m->nzones = 0;
    for (int i = 0; i < N_STRINGS; i++) {
        if (i > 0) cur = next_in_mask(cur, mask);
        m->note[i] = cur;
        m->zone_of[i] = i;
        m->zone_lo[i] = m->zone_hi[i] = i;
        m->zone_note[i] = cur;
    }
    m->nzones = N_STRINGS;
}
