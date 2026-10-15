#include "theory.h"
#include <stdio.h>

/* квинтовый круг по часовой стрелке от верхней кнопки: C G D A E B F# Db Ab Eb Bb F */
const int ROOT_ORDER[N_ROOTS] = { 0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5 };

static const char *ROOT_LABEL[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
static const char *SHARP_NAME[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

/* модификаторы по часовой стрелке от верхней кнопки внутреннего круга */
typedef struct { const char *label, *suffix; int n; int iv[MAX_CHORD]; } ModDef;
static const ModDef MODS[N_MODS] = {
    { "maj",  "",     3, { 0, 4, 7 } },
    { "min",  "m",    3, { 0, 3, 7 } },
    { "dim",  "dim",  3, { 0, 3, 6 } },
    { "sus2", "sus2", 3, { 0, 2, 7 } },
    { "7",    "7",    4, { 0, 4, 7, 10 } },
    { "maj7", "maj7", 4, { 0, 4, 7, 11 } },
    { "m7",   "m7",   4, { 0, 3, 7, 10 } },
    { "6",    "6",    4, { 0, 4, 7, 9 } },
    { "add9", "add9", 4, { 0, 4, 7, 14 } },
};

const char *root_label(int pc) { return ROOT_LABEL[((pc % 12) + 12) % 12]; }
const char *mod_label(int mod) { return (mod >= 0 && mod < N_MODS) ? MODS[mod].label : "?"; }
const char *mod_suffix(int mod) { return (mod >= 0 && mod < N_MODS) ? MODS[mod].suffix : ""; }

int chord_intervals(int mod, int *iv) {
    if (mod < 0 || mod >= N_MODS) mod = 0;
    for (int i = 0; i < MODS[mod].n; i++) iv[i] = MODS[mod].iv[i];
    return MODS[mod].n;
}

int chord_mask(int root, int mod) {
    int iv[MAX_CHORD], n = chord_intervals(mod, iv), m = 0;
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

int pad_voicing(int root, int mod, int *notes) {
    int iv[MAX_CHORD], n = chord_intervals(mod, iv);
    int base = 48 + root;                 /* C3..B3 */
    if (base > 54) base -= 12;            /* держим тонику в районе F#2..F#3 */
    for (int i = 0; i < n; i++) notes[i] = base + iv[i];
    return n;
}

void chord_name(char *out, size_t cap, int root, int mod) {
    snprintf(out, cap, "%s%s", root_label(root), mod_suffix(mod));
}

void note_name(char *out, size_t cap, int midi) {
    snprintf(out, cap, "%s%d", SHARP_NAME[((midi % 12) + 12) % 12], midi / 12 - 1);
}

const char *mod_long(int mod) {
    static const char *L[N_MODS] = { "major", "minor", "diminished", "suspended 2", "dominant 7",
                                     "major 7", "minor 7", "sixth", "added 9" };
    return (mod >= 0 && mod < N_MODS) ? L[mod] : "";
}

void stringmap_build(StringMap *m, int chord_on, int root, int mod, int base) {
    int mask = chord_on ? chord_mask(root, mod) : 0xFFF;
    m->nzones = 0;
    for (int i = 0; i < N_STRINGS; i++) {
        int nt = snap_to_mask(base + i, mask);
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
