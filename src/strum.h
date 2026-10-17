/* Струнная панель: геометрия клавиш и струн, детекция стрaма.
 * Палец ведёт по струнам, каждая пересечённая зона звучит один раз.
 * Тач опрашивается ~60 раз в секунду, а быстрый свайп за кадр проходит несколько струн,
 * поэтому для каждого пересечения считается доля пути внутри кадра - по ней аудио
 * разносит удары во времени, и вместо "пачки" получается настоящее арпеджио. */
#ifndef STRUM_H
#define STRUM_H

#include "theory.h"

#define MAX_FINGERS 6

/* ---- геометрия: струна i (0 - нижняя) соединена с клавишей i ---- */
float strum_pitch(void);                 /* шаг между полосами справа */
float strum_string_y(int i);             /* центр полосы струны */
int   key_is_black(int i);
float key_center_y(int i);               /* центр клавиши слева */
/* белая клавиша: передняя часть [y0, y1], задняя (между чёрными) [b0, b1] */
void  white_key_extents(int i, float *y0, float *y1, float *b0, float *b1);

/* ---- гитара: 6 струн, 0 - самая толстая, внизу ---- */
float gtr_pitch(void);
float gtr_string_y(int i);
float gtr_thick(int i);                  /* толщина струны на экране */
float gtr_fret_x(int f);                 /* x лада f (0 - порожек) */
float gtr_pos_from_y(float y);           /* положение пальца в "струнах" 0..N_GSTR */

/* положение пальца в "струнах" (0..N_STRINGS): над полосами - по полосам, над клавишами -
 * по клавишам, между ними плавный переход (без скачков при движении вбок) */
float strum_pos_from_xy(float x, float y);

typedef struct {
    int id;          /* id касания от тачпада, -1 - свободно */
    float pos;
    float x, y;      /* последняя экранная точка (для подсветки от точки касания) */
    int key;         /* клавиша (струна) под пальцем - для хитбоксов клавиш */
    signed char gside[8];   /* гитара: с какой стороны линии каждой струны палец (-1 выше, +1 ниже, 0 на линии) */
    int gfresh, gfresh_s;   /* гитара: струна от касания - её первое пересечение в ближайшие кадры не звучит */
    int zone;
} StrumFinger;

/* zone - какая зона зазвучала, frac - доля кадра (0..1), где произошло пересечение */
typedef void (*StrumHit)(int zone, float frac, void *user);

void strum_down(StrumFinger *f, int id, float pos, const StringMap *m, StrumHit hit, void *user);
void strum_move(StrumFinger *f, float pos, const StringMap *m, StrumHit hit, void *user);
/* страммер (25 струн): то же по экранным точкам - с хитбоксами клавиш (см. strum.c) */
void strum_down_xy(StrumFinger *f, int id, float x, float y, const StringMap *m, StrumHit hit, void *user);
void strum_move_xy(StrumFinger *f, float x, float y, const StringMap *m, StrumHit hit, void *user);
int  strum_key_at(float x, float y);
/* гитара: струна звучит при каждом пересечении её линии пальцем (вниз и вверх) - тремоло
 * коротким движением через одну струну. Касание - струна под пальцем. */
#define GTR_LINE_HYST 4.0f       /* "мёртвая" полоса у линии струны (px), чтобы дрожание пальца не играло */
void strum_gtr_down(StrumFinger *f, int id, float x, float y, StrumHit hit, void *user);
void strum_gtr_move(StrumFinger *f, float x, float y, StrumHit hit, void *user);
int  strum_key_kind(float x);            /* 2 - только чёрные (слева), 1 - только белые, 0 - все */
/* клавиши, задетые по пути (с гистерезисом; в зоне только чёрных/белых другие пропускаются) */
int  strum_cross(int *cur, float x0, float y0, float x1, float y1, int *keys, float *fracs, int max);

/* аккорд сменился: зона пальца пересчитывается без звука */
void strum_remap(StrumFinger *f, const StringMap *m);

#endif
