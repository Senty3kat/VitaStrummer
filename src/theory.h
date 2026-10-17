/* Аккорды: тоники, модификаторы, раскладка струн. Без зависимостей от Vita. */
#ifndef THEORY_H
#define THEORY_H

#include <stddef.h>

#define N_ROOTS 12
#define N_MODS 9                 /* кнопок модификаторов */
#define N_TYPES (2 * N_MODS)     /* типов аккордов: мажор и минор x модификатор */
#define N_STRINGS 25
#define STRING_BASE_NOTE 60      /* нижняя струна при октаве 0 - C4 (MIDI 60), верхняя - C6 */
#define CHORD_SPREAD 2           /* в аккорде: сколько соседних струн играют один звук */
#define OCT_MIN (-2)
#define OCT_MAX 2
#define MAX_CHORD 6

/* высота тоники (0 = C) для кнопки колеса i, по часовой стрелке от верхней */
extern const int ROOT_ORDER[N_ROOTS];

const char *root_label(int pc);       /* как на колесе: C G D ... Db Ab Eb Bb F */
/* Тип аккорда t = качество (0 мажор, 1 минор) x модификатор (кнопка внутреннего круга) */
int chord_type(int minor, int mod);
const char *type_label(int t);        /* подпись на кнопке: maj, 7, sus4 ... / min, m7, mM7 ... */
const char *type_suffix(int t);       /* для названия аккорда: "", "m7", "sus4" ... */
const char *type_long(int t);         /* для табло: "major", "minor 7" ... */

/* интервалы от тоники (могут быть >= 12, например add9), возвращает количество */
int chord_intervals(int t, int *iv);

/* 12-битная маска звуковысотных классов аккорда */
int chord_mask(int root, int t);

/* ближайший к note звук из маски; при равенстве - нижний */
int snap_to_mask(int note, int mask);

/* ноты пэда: тесное расположение вокруг середины малой октавы */
int pad_voicing(int root, int t, int *notes);

void chord_name(char *out, size_t cap, int root, int t);
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
void stringmap_build(StringMap *m, int chord_on, int root, int t, int base);

/* ---------- тональность ----------
 * мажор и натуральный минор: ступени, их аккорды (мажор / минор / уменьшённый), римские цифры */
int key_scale_mask(int key_root, int key_minor);
int key_degree(int key_root, int key_minor, int pc);          /* 0..6, -1 - не из лада */
int key_degree_quality(int key_minor, int deg);               /* 0 мажор, 1 минор, 2 уменьшённый */
const char *key_degree_name(int key_minor, int deg);          /* "I", "ii", "vii" ... */
/* без аккорда в тональности: струны идут по звукам лада, по одной */
void stringmap_build_scale(StringMap *m, int mask, int base);

/* ---------- гитара ---------- */
#define N_GSTR 6
extern const int GT_TUNING[N_GSTR];       /* MIDI открытых струн, 0 - самая толстая (E2) */
typedef struct {
    int fret[N_GSTR];                     /* лад, 0 - открытая, -1 - заглушена */
    int note[N_GSTR];                     /* звучащая нота (MIDI), -1 - заглушена */
    int pos;                              /* позиция, в которой взята (0 - авто без позиции) */
} GuitarChord;
/* аппликатура аккорда; без аккорда - открытые струны. shift - сдвиг в полутонах (октава).
 * pos = 0 - лучшая позиция (как сыграл бы гитарист), 1..GT_POS_MAX - рука с этого лада.
 * Мажор и минор (трезвучия) - только 2-4 привычные формы с басом на 5-6 струне (открытая,
 * баррэ от 6-й, баррэ от 5-й, у мажора ещё форма "до" от 5-й); pos - ближайшая из них.
 * Остальные аккорды - свои формы в каждой позиции 1..GT_POS_CX. */
#define GT_POS_MAX 11
#define GT_POS_CX 9
void guitar_voicing(GuitarChord *g, int chord_on, int root, int t, int shift, int pos);
/* какие позиции есть у аккорда (по возрастанию), возвращает число; out - до GT_POS_MAX штук */
int guitar_positions(int root, int t, int *out);
/* ближайшая доступная позиция к pos (pos > 0) */
int guitar_snap_pos(int root, int t, int pos);
void guitar_voicing(GuitarChord *g, int chord_on, int root, int t, int shift, int pos);
/* "x 3 2 0 1 0" */
void guitar_shape_str(const GuitarChord *g, char *out, size_t cap);

#endif
