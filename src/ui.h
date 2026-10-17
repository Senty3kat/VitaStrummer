/* Главный экран: колесо аккордов слева, струны справа, полоса управления сверху. */
#ifndef UI_H
#define UI_H

#include "theory.h"

enum { MODE_STRUM = 0, MODE_PAD = 1, MODE_GUITAR = 2, N_MODES = 3 };

typedef struct {
    int mode;        /* MODE_STRUM / MODE_PAD / MODE_GUITAR */
    int chord_on;    /* аккорд "залип" */
    int root;        /* высота тоники 0..11 */
    int minor;       /* качество: 0 мажор, 1 минор (кнопки в центре колеса) */
    int mod;         /* модификатор 0..N_MODS-1, выбран всегда */
    int octave;      /* сдвиг струн и пэда, OCT_MIN..OCT_MAX */
    int vol_strings; /* громкость струн (страммер и гитара), 0..100 */
    int vol_pad;     /* громкость пэда, 0..100 */
    int quick[8];    /* быстрые аккорды (D-pad вверх/вправо/вниз/влево, стик так же): 0 - пусто,
                        иначе quick_code() */
    int key_on;      /* режим тональности */
    int key_root;    /* тоника тональности 0..11 */
    int key_minor;   /* 0 мажор, 1 натуральный минор */
    int palm;        /* ладонь на струнах (задняя панель, правая рука) */
    int sus;         /* сустейн (задняя панель, левая рука) */
    int gpos;        /* гитара: позиция руки на грифе, 0 - авто, 1..GT_POS_MAX - с этого лада */
    int transpose;   /* транспозиция звука в полутонах, TRANSPOSE_MIN..MAX (как каподастр) */
} AppState;

typedef struct {
    int xruns, voices, peak, error;
    float fps;
    int show;        /* показывать отладку */
} UiDebug;

enum { HIT_NONE = 0, HIT_ROOT, HIT_MOD, HIT_QUAL, HIT_PLATE, HIT_MODE, HIT_OCT, HIT_VOL, HIT_QUICK, HIT_KEY, HIT_GPOS, HIT_TRN };

#define TRANSPOSE_MIN (-6)
#define TRANSPOSE_MAX 6

/* код быстрого аккорда (0 - пусто): тоника, качество, модификатор, октава и позиция на грифе.
 * Октава хранится как (oct + 5) % 5, позиция 0..GT_POS_MAX (0 - авто): у старых кодов обе получаются 0 */
#define QUICK_MAX (1080 * (GT_POS_MAX + 1))
static inline int quick_code(int root, int minor, int mod, int oct, int gpos) {
    return 1 + root + 12 * minor + 24 * mod + 216 * ((oct + 5) % 5) + 1080 * gpos;
}
static inline void quick_decode(int c, int *root, int *minor, int *mod, int *oct, int *gpos) {
    c -= 1; *root = c % 12; *minor = (c / 12) % 2; *mod = (c / 24) % 9;
    int oi = (c / 216) % 5;
    *oct = oi <= 2 ? oi : oi - 5;
    *gpos = c / 1080;
}

int  ui_init(void);
void ui_recreate(void);                              /* после "сна": текстуры заново */
void ui_set_state(const AppState *s, const StringMap *m);
/* палец на струнной панели (каждый кадр): от (x0,y0) прошлого кадра к (x1,y1) текущего,
 * down = 1 в первый кадр касания. Задетые струны загораются, по ним растекается цвет */
void ui_touch_glow(float x0, float y0, float x1, float y1, int down);
/* index: тоника (высота 0..11), модификатор, качество (0 мажор / 1 минор), режим, шаг октавы (-1 / +1),
 * громкость 0..100 (HIT_VOL) или номер быстрого аккорда (HIT_QUICK); HIT_KEY - касание табло;
 * HIT_GPOS - полоска позиций под грифом: 0 авто, 1..GT_POS_MAX лад */
int  ui_hit(float x, float y, int *index);
/* какие кнопки сейчас зажаты пальцами (для "нажатого" вида) */
void ui_set_held(const int *kinds, const int *indices, int n);
void ui_draw(float dt, const UiDebug *dbg);

#endif
