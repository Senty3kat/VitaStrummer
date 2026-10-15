/* Превью экрана на компьютере: несколько состояний и анимация жеста в out/ (ppm + подписи) */
#include "../src/ui.h"
#include "../src/theory.h"
#include "../src/strum.h"
#include <stdio.h>
#include <time.h>

extern const char *host_frame_name;

static StringMap g_m;
static float g_x0, g_x1;
static void on_hit(int zone, float frac, void *u) { (void)u; ui_flash_zone(zone, g_x0 + (g_x1 - g_x0) * frac); }

static void set_state(int mode, int chord_on, int root, int mod, int octave) {
    AppState s = { mode, chord_on, root, mod, octave };
    stringmap_build(&g_m, chord_on, root, mod, STRING_BASE_NOTE + 12 * octave);
    ui_set_state(&s, &g_m);
}

static void shot(const char *name, int held_kind, int held_idx) {
    ui_set_held(&held_kind, &held_idx, held_kind ? 1 : 0);
    UiDebug d = { 0, 0, 0, 0, 60.0f, 1 };
    host_frame_name = name;
    ui_draw(0.001f, &d);
    printf("%s: zones=%d\n", name, g_m.nzones);
}

/* жест: касание внизу справа и быстрое проведение вверх-влево, 60 кадров в секунду */
static void gesture(const char *prefix, int frames_total) {
    static char name[64];
    UiDebug d = { 0, 0, 0, 0, 60.0f, 1 };
    StrumFinger f;
    const int swipe = 14;
    for (int fr = 0; fr < frames_total; fr++) {
        float t = (float)fr / (float)swipe;
        if (fr <= swipe) {
            float x = 860.0f - 90.0f * t, y = 470.0f - 360.0f * t * t * (3.0f - 2.0f * t);
            if (fr == 0) { g_x0 = g_x1 = x; f.x = x; strum_down(&f, 1, strum_pos_from_xy(x, y), &g_m, on_hit, NULL); ui_touch_glow(x, y, 1); }
            else { g_x0 = f.x; g_x1 = x; strum_move(&f, strum_pos_from_xy(x, y), &g_m, on_hit, NULL); f.x = x; ui_touch_glow(x, y, 0); }
        }
        snprintf(name, sizeof(name), "%s_%02d", prefix, fr);
        host_frame_name = name;
        ui_draw(1.0f / 60.0f, &d);
    }
}

int main(void) {
    clock_t t0 = clock();
    if (!ui_init()) { printf("ui_init failed\n"); return 1; }
    printf("ui_init: %.0f ms on this PC\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    set_state(MODE_STRUM, 0, 0, 0, 0);
    shot("screen_chromatic", 0, 0);
    set_state(MODE_STRUM, 1, 0, 0, 0);
    shot("screen_C", 0, 0);
    set_state(MODE_PAD, 1, 9, 6, 1);
    shot("screen_Am7_pad", HIT_MOD, 6);
    set_state(MODE_STRUM, 1, 9, 6, 0);
    gesture("anim", 48);
    /* одиночное касание без движения: видно расходящееся кольцо */
    {
        UiDebug d = { 0, 0, 0, 0, 60.0f, 1 };
        static char name[32];
        set_state(MODE_STRUM, 0, 0, 0, 0);
        StrumFinger f;
        g_x0 = g_x1 = 820.0f; f.x = 820.0f;
        strum_down(&f, 2, strum_pos_from_xy(820, 300), &g_m, on_hit, NULL);
        ui_touch_glow(820, 300, 1);
        for (int fr = 0; fr < 30; fr++) {
            snprintf(name, sizeof(name), "tap_%02d", fr);
            host_frame_name = name;
            ui_draw(1.0f / 60.0f, &d);
        }
    }
    /* касание по клавишам слева - волна идёт и по клавишам */
    {
        UiDebug d = { 0, 0, 0, 0, 60.0f, 1 };
        static char name[32];
        set_state(MODE_STRUM, 1, 7, 4, 0);
        StrumFinger f;
        g_x0 = g_x1 = 600.0f; f.x = 600.0f;
        strum_down(&f, 3, strum_pos_from_xy(600, 330), &g_m, on_hit, NULL);
        ui_touch_glow(600, 330, 1);
        for (int fr = 0; fr < 14; fr++) {
            snprintf(name, sizeof(name), "keys_%02d", fr);
            host_frame_name = name;
            ui_draw(1.0f / 60.0f, &d);
        }
    }
    return 0;
}
