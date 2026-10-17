/* Превью экрана на компьютере: состояния и анимации жестов в out/ (ppm + подписи) */
#include "../src/ui.h"
#include "../src/theory.h"
#include "../src/strum.h"
#include <stdio.h>
#include <time.h>
#include <string.h>

extern const char *host_frame_name;

static StringMap g_m, g_gm;
static int g_key_on, g_key_root, g_key_minor, g_palm, g_gpos, g_trn;
static int g_mode;
static void on_hit(int zone, float frac, void *u) { (void)zone; (void)frac; (void)u; }

/* быстрые аккорды для превью: C, G, Am, F на D-pad; Dm7, Esus4, Cmaj7 на стике, один пустой */
static void fill_quick(AppState *s) {
    s->quick[0] = quick_code(0, 0, 0, 0, 0); s->quick[1] = quick_code(7, 0, 0, 1, 0);
    s->quick[2] = quick_code(9, 1, 0, -1, 0); s->quick[3] = quick_code(5, 0, 0, 0, 0);
    s->quick[4] = quick_code(2, 1, 1, 0, 0); s->quick[5] = quick_code(4, 0, 7, 0, 0);
    s->quick[6] = quick_code(0, 0, 2, 2, 0); s->quick[7] = 0;
}

static void set_state(int mode, int chord_on, int root, int minor, int mod, int octave) {
    AppState s;
    memset(&s, 0, sizeof(s));
    s.mode = mode; s.chord_on = chord_on; s.root = root; s.minor = minor; s.mod = mod; s.octave = octave;
    s.vol_strings = 70; s.vol_pad = 45;
    s.key_on = g_key_on; s.key_root = g_key_root; s.key_minor = g_key_minor; s.palm = g_palm; s.gpos = g_gpos; s.transpose = g_trn;
    fill_quick(&s);
    g_mode = mode;
    stringmap_build(&g_m, chord_on, root, chord_type(minor, mod), STRING_BASE_NOTE + 12 * octave);
    g_gm.nzones = N_GSTR;                         /* гитарные зоны: зона = струна */
    for (int i = 0; i < N_GSTR; i++) { g_gm.zone_of[i] = i; g_gm.zone_lo[i] = g_gm.zone_hi[i] = i; }
    ui_set_state(&s, &g_m);
    ui_set_held(NULL, NULL, 0);
}

static float pos_of(float x, float y) { return g_mode == MODE_GUITAR ? gtr_pos_from_y(y) : strum_pos_from_xy(x, y); }
static const StringMap *map_of(void) { return g_mode == MODE_GUITAR ? &g_gm : &g_m; }

static void shot(const char *name, int held_kind, int held_idx) {
    ui_set_held(&held_kind, &held_idx, held_kind ? 1 : 0);
    UiDebug d = { 0, 0, 0, 0, 60.0f, 0 };
    host_frame_name = name;
    ui_draw(0.001f, &d);
    ui_set_held(NULL, NULL, 0);
    printf("%s: zones=%d\n", name, g_m.nzones);
}

/* палец по траектории: кадры 0..touch_frames касаются, дальше палец отпущен */
typedef void (*Path)(int fr, float *x, float *y);

static void run(const char *prefix, Path path, int touch_frames, int frames_total) {
    static char name[64];
    UiDebug d = { 0, 0, 0, 0, 60.0f, 0 };
    StrumFinger f;
    for (int fr = 0; fr < frames_total; fr++) {
        if (fr < touch_frames) {
            float x, y;
            path(fr, &x, &y);
            float p = pos_of(x, y);
            int gtr = g_mode == MODE_GUITAR;
            if (fr == 0) {
                f.x = x; f.y = y;
                if (gtr) strum_down(&f, 1, p, map_of(), on_hit, NULL);
                else strum_down_xy(&f, 1, x, y, map_of(), on_hit, NULL);
                ui_touch_glow(x, y, x, y, 1);
            } else {
                if (gtr) strum_move(&f, p, map_of(), on_hit, NULL);
                else strum_move_xy(&f, x, y, map_of(), on_hit, NULL);
                ui_touch_glow(f.x, f.y, x, y, 0);
                f.x = x; f.y = y;
            }
        }
        snprintf(name, sizeof(name), "%s_%02d", prefix, fr);
        host_frame_name = name;
        ui_draw(1.0f / 60.0f, &d);
    }
}

/* быстрое проведение вверх-влево по струнам */
static void path_swipe(int fr, float *x, float *y) {
    float t = (float)fr / 14.0f;
    if (t > 1.0f) t = 1.0f;
    *x = 860.0f - 90.0f * t;
    *y = 470.0f - 360.0f * t * t * (3.0f - 2.0f * t);
}
/* касание полосы и сразу отпустить */
static void path_tap(int fr, float *x, float *y) { (void)fr; *x = 820.0f; *y = 300.0f; }
/* касание клавиши */
static void path_key(int fr, float *x, float *y) { (void)fr; *x = 596.0f; *y = 330.0f; }
/* проведение по передней части белых клавиш сверху вниз */
static void path_whites(int fr, float *x, float *y) { *x = 598.0f; *y = 120.0f + 30.0f * (float)fr; }
/* палец лежит на струне и медленно ведёт вдоль неё */
static void path_hold(int fr, float *x, float *y) { *x = 700.0f + 6.0f * (float)fr; *y = 250.0f; }

/* гитара: удар вниз - от толстой струны (внизу) к тонкой */
static void path_gdown(int fr, float *x, float *y) {
    float t = (float)fr / 10.0f;
    if (t > 1.0f) t = 1.0f;
    *x = 900.0f - 40.0f * t;
    *y = 478.0f - 400.0f * t;                     /* снизу вверх: от толстой к тонкой */
}

int main(void) {
    clock_t t0 = clock();
    if (!ui_init()) { printf("ui_init failed\n"); return 1; }
    printf("ui_init: %.0f ms on this PC\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    set_state(MODE_STRUM, 0, 0, 0, 0, 0);
    shot("screen_chromatic", 0, 0);
    set_state(MODE_STRUM, 1, 0, 0, 0, 0);
    shot("screen_C", 0, 0);
    g_trn = 2;                                        /* транспозиция +2 */
    set_state(MODE_PAD, 1, 9, 1, 1, 1);
    shot("screen_Am7_pad", HIT_QUAL, 1);
    g_trn = 0;
    set_state(MODE_STRUM, 1, 5, 0, 7, 0);
    shot("screen_Fsus4", 0, 0);

    set_state(MODE_STRUM, 1, 9, 1, 1, 0);
    run("anim", path_swipe, 15, 60);
    set_state(MODE_STRUM, 0, 0, 0, 0, 0);
    run("tap", path_tap, 2, 50);
    set_state(MODE_STRUM, 1, 7, 0, 1, 0);
    run("keys", path_key, 2, 45);
    set_state(MODE_STRUM, 0, 0, 0, 0, 0);
    run("hold", path_hold, 30, 55);
    /* проведение по передней части белых клавиш: горят только белые */
    set_state(MODE_STRUM, 1, 2, 0, 1, 0);
    run("whites", path_whites, 12, 30);

    /* тональность до мажор, выбран Am (vi); ладонь на струнах */
    g_key_on = 1; g_key_root = 0; g_key_minor = 0; g_palm = 1;
    set_state(MODE_STRUM, 1, 9, 1, 0, 0);
    shot("screen_key_C_Am", 0, 0);
    g_key_root = 9; g_key_minor = 1; g_palm = 0;      /* ля минор, гитара, аккорд G (VII) */
    set_state(MODE_GUITAR, 1, 7, 0, 0, 0);
    shot("screen_key_Am_guitar", 0, 0);
    g_key_on = 0;

    /* гитара: G (открытая позиция), F (баррэ), Am7, без аккорда; проведение вниз по струнам */
    set_state(MODE_GUITAR, 1, 7, 0, 0, 0);
    shot("screen_guitar_G", 0, 0);
    set_state(MODE_GUITAR, 1, 5, 0, 0, 0);
    shot("screen_guitar_F", HIT_ROOT, 5);
    g_gpos = 8;                                       /* C в 8-й позиции (баррэ) */
    set_state(MODE_GUITAR, 1, 0, 0, 0, 0);
    shot("screen_guitar_C_pos8", 0, 0);
    g_gpos = 6;                                       /* C в 6-й позиции: средние струны заглушены */
    set_state(MODE_GUITAR, 1, 0, 0, 0, 0);
    shot("screen_guitar_C_pos6", 0, 0);
    g_gpos = 2;                                       /* F во 2-й и 3-й - разные формы */
    set_state(MODE_GUITAR, 1, 5, 0, 0, 0);
    shot("screen_guitar_F_pos2", 0, 0);
    g_gpos = 0;
    set_state(MODE_GUITAR, 0, 0, 0, 0, 0);
    shot("screen_guitar_open", 0, 0);
    set_state(MODE_GUITAR, 1, 9, 1, 1, 0);
    run("gtr", path_gdown, 12, 60);
    return 0;
}
