/* Аккорды: тоники, модификаторы, раскладка струн. Без зависимостей от Vita. */
#ifndef THEORY_H
#define THEORY_H

#include <stddef.h>

#define N_ROOTS 12
#define N_MODS 9
#define N_STRINGS 25
#define STRING_BASE_NOTE 60      /* нижняя струна при октаве 0 - C4 (MIDI 60), верхняя - C6 */
#define CHORD_SPREAD 2           /* в аккорде: сколько соседних струн играют один звук */
#define OCT_MIN (-2)
#define OCT_MAX 2
#define MAX_CHORD 6

/* высота тоники (0 = C) для кнопки колеса i, по часовой стрелке от верхней */
extern const int ROOT_ORDER[N_ROOTS];

const char *root_label(int pc);       /* как на колесе: C G D ... Db Ab Eb Bb F */
const char *mod_label(int mod);       /* maj, min, dim ... */
const char *mod_suffix(int mod);      /* "", "m", "dim" ... для названия аккорда */

/* интервалы от тоники (могут быть >= 12, например add9), возвращает количество */
int chord_intervals(int mod, int *iv);

/* 12-битная маска звуковысотных классов аккорда */
int chord_mask(int root, int mod);

/* ближайший к note звук из маски; при равенстве - нижний */
int snap_to_mask(int note, int mask);

/* ноты пэда: тесное расположение вокруг середины малой октавы */
int pad_voicing(int root, int mod, int *notes);

void chord_name(char *out, size_t cap, int root, int mod);
void note_name(char *out, size_t cap, int midi);   /* "C#4" */

/* Раскладка струн. Без аккорда каждая струна - своя нота (хроматика от base).
 * С аккордом струны идут парами (CHORD_SPREAD), каждая пара - следующий звук аккорда вверх,
 * так диапазон получается шире клавиатуры. Соседние струны с одинаковой нотой - одна "зона":
 * при стрaме она звучит один раз. */
typedef struct {
    int note[N_STRINGS];          /* какая нота звучит на струне i (0 - нижняя) */
    int zone_of[N_STRINGS];
    int nzones;
    int zone_lo[N_STRINGS];       /* первая струна зоны */
    int zone_hi[N_STRINGS];       /* последняя струна зоны (включительно) */
    int zone_note[N_STRINGS];
} StringMap;

/* base - нота нижней струны (всегда C, сдвиг по октавам) */
void stringmap_build(StringMap *m, int chord_on, int root, int mod, int base);

/* полное название типа аккорда для табло: "major", "minor 7" ... */
const char *mod_long(int mod);

#endif
