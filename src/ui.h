/* Главный экран: колесо аккордов слева, струны справа, полоса управления сверху. */
#ifndef UI_H
#define UI_H

#include "theory.h"

enum { MODE_STRUM = 0, MODE_PAD = 1 };

typedef struct {
    int mode;        /* MODE_STRUM / MODE_PAD */
    int chord_on;    /* аккорд "залип" */
    int root;        /* высота тоники 0..11 */
    int mod;         /* модификатор 0..N_MODS-1, выбран всегда */
    int octave;      /* сдвиг струн и пэда, OCT_MIN..OCT_MAX */
} AppState;

typedef struct {
    int xruns, voices, peak, error;
    float fps;
    int show;        /* показывать отладку */
} UiDebug;

enum { HIT_NONE = 0, HIT_ROOT, HIT_MOD, HIT_CENTER, HIT_PLATE, HIT_MODE, HIT_OCT };

int  ui_init(void);
void ui_recreate(void);                              /* после "сна": текстуры заново */
void ui_set_state(const AppState *s, const StringMap *m);
/* струна s задета в точке x: от неё по струне растекается цвет */
void ui_flash_string(int s, float x);
/* палец на струнной панели (каждый кадр): от (x0,y0) прошлого кадра к (x1,y1) текущего.
 * Струна под пальцем подпитывается, пройденные по пути загораются */
void ui_touch_glow(float x0, float y0, float x1, float y1);
/* index: тоника (высота 0..11), модификатор, режим, или шаг октавы (-1 / +1) */
int  ui_hit(float x, float y, int *index);
/* какие кнопки сейчас зажаты пальцами (для "нажатого" вида) */
void ui_set_held(const int *kinds, const int *indices, int n);
void ui_draw(float dt, const UiDebug *dbg);

#endif
