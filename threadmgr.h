/* Превью экрана на компьютере: несколько состояний в out/ (ppm + подписи) */
#include "../src/ui.h"
#include "../src/theory.h"
#include <stdio.h>
#include <time.h>

extern const char *host_frame_name;

static void shot(const char *name, int mode, int chord_on, int root, int mod, int octave,
                 const int *flash_zones, int nf, int held_kind, int held_idx) {
    AppState s = { mode, chord_on, root, mod, octave };
    StringMap m;
    stringmap_build(&m, chord_on, root, mod, STRING_BASE_NOTE + 12 * octave);
    ui_set_state(&s, &m);
    for (int i = 0; i < nf; i++) ui_flash_zone(flash_zones[i]);
    ui_set_held(&held_kind, &held_idx, held_kind ? 1 : 0);
    UiDebug d = { 0, nf, 0, 0, 60.0f, 1 };
    host_frame_name = name;
    ui_draw(0.001f, &d);
    for (int i = 0; i < 60; i++) { host_frame_name = "scratch"; ui_draw(0.05f, &d); }   /* погасить вспышки */
    printf("%s: zones=%d\n", name, m.nzones);
}

int main(void) {
    clock_t t0 = clock();
    if (!ui_init()) { printf("ui_init failed\n"); return 1; }
    printf("ui_init: %.0f ms on this PC\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    int f1[] = { 4, 5, 6, 7 };
    shot("screen_chromatic", MODE_STRUM, 0, 0, 0, 0, f1, 4, 0, 0);
    int f2[] = { 1, 2, 3 };
    shot("screen_C", MODE_STRUM, 1, 0, 0, 0, f2, 3, 0, 0);
    int f3[] = { 6 };
    shot("screen_Am7_pad", MODE_PAD, 1, 9, 6, 1, f3, 1, HIT_MOD, 6);
    return 0;
}
