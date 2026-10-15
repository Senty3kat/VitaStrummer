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

/* положение пальца в "струнах" (0..N_STRINGS): над полосами - по полосам, над клавишами -
 * по клавишам, между ними плавный переход (без скачков при движении вбок) */
float strum_pos_from_xy(float x, float y);

typedef struct {
    int id;          /* id касания от тачпада, -1 - свободно */
    float pos;
    float x, y;      /* последняя экранная точка (для подсветки от точки касания) */
    int zone;
} StrumFinger;

/* zone - какая зона зазвучала, frac - доля кадра (0..1), где произошло пересечение */
typedef void (*StrumHit)(int zone, float frac, void *user);

void strum_down(StrumFinger *f, int id, float pos, const StringMap *m, StrumHit hit, void *user);
void strum_move(StrumFinger *f, float pos, const StringMap *m, StrumHit hit, void *user);
/* аккорд сменился: зона пальца пересчитывается без звука */
void strum_remap(StrumFinger *f, const StringMap *m);

#endif
