/*
 * Vita Strummer - версия 0.2
 *
 * Слева колесо: большой круг - тоника, малый - модификатор аккорда.
 *   Касание тоники - аккорд "залипает"; повторное касание той же тоники - сброс.
 *   Касание модификатора - сменить тип аккорда (выбран всегда, по умолчанию maj).
 *   Касание звезды в середине или кнопка Круг - сброс аккорда.
 * Справа 25 клавиш C4..C6, каждая переходит в свою струну. Без аккорда - хроматика,
 *   с аккордом каждая струна подтягивается к ближайшему звуку аккорда, одинаковые соседние
 *   струны сливаются в одну зону. Играть можно и по клавишам, и по струнам.
 * Сверху:
 *   MODE (или SELECT): Strum - звучат струны; Pad - аккорд звучит, пока выбран, струны тоже играют
 *   OCTAVE (или L / R): сдвиг струн и пэда на октаву, -2..+2
 *   START - показать/скрыть отладку (кадры, голоса, опоздания аудио)
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/io/stat.h>
#include <vita2d.h>

#include "audio.h"
#include "synth.h"
#include "theory.h"
#include "strum.h"
#include "ui.h"
#include "layout.h"

unsigned int _newlib_heap_size_user = 48 * 1024 * 1024;

#define LOG_DIR  "ux0:data/Strummer"
#define LOG_PATH "ux0:data/Strummer/log.txt"

static void dbg_log(const char *fmt, ...) {
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* ---------- состояние ---------- */
static AppState st;
static StringMap smap;
static StrumFinger fingers[MAX_FINGERS];
static int pad_sent[MAX_CHORD], pad_sent_n = -1;
static float g_frame_dt = 1.0f / 60.0f;

/* касания кнопок: пока палец лежит, кнопка выглядит нажатой */
typedef struct { int id, kind, idx; } Held;
static Held held[MAX_FINGERS];

static int base_note(void) { return STRING_BASE_NOTE + 12 * st.octave; }

static void send_pad(void) {
    int notes[MAX_CHORD], n = 0;
    if (st.mode == MODE_PAD && st.chord_on) {
        n = pad_voicing(st.root, st.mod, notes);
        for (int i = 0; i < n; i++) notes[i] += 12 * st.octave;
    }
    if (n == pad_sent_n && memcmp(notes, pad_sent, sizeof(int) * n) == 0) return;
    audio_pad(notes, n);
    memcpy(pad_sent, notes, sizeof(int) * n);
    pad_sent_n = n;
}

static void apply_state(void) {
    stringmap_build(&smap, st.chord_on, st.root, st.mod, base_note());
    for (int i = 0; i < MAX_FINGERS; i++) strum_remap(&fingers[i], &smap);
    ui_set_state(&st, &smap);
    send_pad();
}

static void on_strum(int zone, float frac, void *user) {
    (void)user;
    int delay = (int)(frac * g_frame_dt * (float)SYNTH_RATE);
    audio_strum(smap.zone_note[zone], delay);
    ui_flash_zone(zone);
}

static StrumFinger *finger_find(int id) {
    for (int i = 0; i < MAX_FINGERS; i++) if (fingers[i].id == id) return &fingers[i];
    return NULL;
}

static Held *held_find(int id) {
    for (int i = 0; i < MAX_FINGERS; i++) if (held[i].id == id) return &held[i];
    return NULL;
}

static void touches_reset(void) {
    for (int i = 0; i < MAX_FINGERS; i++) { fingers[i].id = -1; held[i].id = -1; }
}

static void octave_step(int d) {
    int o = st.octave + d;
    if (o < OCT_MIN) o = OCT_MIN;
    if (o > OCT_MAX) o = OCT_MAX;
    st.octave = o;
}

/* новое касание вне струн: колесо и полоса сверху */
static int touch_down_ui(int kind, int idx) {
    switch (kind) {
    case HIT_ROOT:
        if (st.chord_on && st.root == idx) st.chord_on = 0;
        else { st.chord_on = 1; st.root = idx; }
        break;
    case HIT_MOD:    st.mod = idx; break;
    case HIT_CENTER: st.chord_on = 0; break;
    case HIT_MODE:   st.mode = idx; break;
    case HIT_OCT:    octave_step(idx); break;
    default:         return 0;
    }
    apply_state();
    return 1;
}

int main(void) {
    sceIoMkdir(LOG_DIR, 0777);
    dbg_log("start v0.2");

    vita2d_init_advanced(4 * 1024 * 1024);
    vita2d_set_clear_color(RGBA8(60, 86, 110, 255));
    uint64_t t0 = sceKernelGetProcessTimeWide();
    if (!ui_init()) { dbg_log("ui_init failed"); sceKernelExitProcess(0); }
    dbg_log("ui_init %d ms", (int)((sceKernelGetProcessTimeWide() - t0) / 1000));

    memset(&st, 0, sizeof(st));
    st.mode = MODE_STRUM;
    touches_reset();

    int ar = audio_start();
    dbg_log("audio_start=%d grain=%d", ar, AUDIO_GRAIN);
    apply_state();

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

    SceCtrlData pad;
    SceTouchData touch;
    unsigned prev_buttons = 0;
    int prev_ids[MAX_FINGERS], prev_n = 0;
    uint64_t last_t = sceKernelGetProcessTimeWide();
    uint64_t last_sys = sceKernelGetSystemTimeWide();
    float fps = 60.0f;
    int show_debug = 1;

    for (;;) {
        uint64_t now_t = sceKernelGetProcessTimeWide();
        uint64_t sys_now = sceKernelGetSystemTimeWide();
        float dt = (float)(now_t - last_t) * 1e-6f;
        last_t = now_t;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.1f) dt = 0.1f;
        g_frame_dt = dt > 1.0f / 30.0f ? 1.0f / 30.0f : dt;
        fps += (1.0f / dt - fps) * 0.05f;

        /* ---- возврат из "сна": порт звука и текстуры заново ---- */
        if (sys_now - last_sys > 1500000ULL) {
            dbg_log("resume gap=%d ms", (int)((sys_now - last_sys) / 1000));
            audio_reset_port();
            ui_recreate();
            touches_reset();
            prev_n = 0;
        }
        last_sys = sys_now;

        /* ---- кнопки ---- */
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned pressed = pad.buttons & ~prev_buttons;
        prev_buttons = pad.buttons;
        if (pressed & SCE_CTRL_SELECT)   { st.mode = st.mode == MODE_STRUM ? MODE_PAD : MODE_STRUM; apply_state(); }
        if (pressed & SCE_CTRL_CIRCLE)   { st.chord_on = 0; apply_state(); }
        if (pressed & SCE_CTRL_LTRIGGER) { octave_step(-1); apply_state(); }
        if (pressed & SCE_CTRL_RTRIGGER) { octave_step(1); apply_state(); }
        if (pressed & SCE_CTRL_START)    show_debug = !show_debug;

        /* ---- тач ---- */
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
        int nrep = (int)touch.reportNum;
        if (nrep > MAX_FINGERS) nrep = MAX_FINGERS;
        int ids[MAX_FINGERS];
        for (int k = 0; k < nrep; k++) {
            int id = touch.report[k].id;
            float x = touch.report[k].x / 2.0f, y = touch.report[k].y / 2.0f;
            ids[k] = id;
            int is_new = 1;
            for (int j = 0; j < prev_n; j++) if (prev_ids[j] == id) is_new = 0;

            StrumFinger *f = finger_find(id);
            if (f) { strum_move(f, strum_pos_from_xy(x, y), &smap, on_strum, NULL); continue; }
            if (!is_new) continue;

            int idx, kind = ui_hit(x, y, &idx);
            if (kind == HIT_PLATE) {
                StrumFinger *fr = finger_find(-1);
                if (fr) strum_down(fr, id, strum_pos_from_xy(x, y), &smap, on_strum, NULL);
            } else if (touch_down_ui(kind, idx)) {
                Held *h = held_find(-1);
                if (h) { h->id = id; h->kind = kind; h->idx = idx; }
            }
        }
        /* отпущенные пальцы */
        for (int i = 0; i < MAX_FINGERS; i++) {
            int alive_f = 0, alive_h = 0;
            for (int k = 0; k < nrep; k++) {
                if (fingers[i].id >= 0 && ids[k] == fingers[i].id) alive_f = 1;
                if (held[i].id >= 0 && ids[k] == held[i].id) alive_h = 1;
            }
            if (!alive_f) fingers[i].id = -1;
            if (!alive_h) held[i].id = -1;
        }
        for (int k = 0; k < nrep; k++) prev_ids[k] = ids[k];
        prev_n = nrep;

        int hk[MAX_FINGERS], hi[MAX_FINGERS], hn = 0;
        for (int i = 0; i < MAX_FINGERS; i++)
            if (held[i].id >= 0) { hk[hn] = held[i].kind; hi[hn] = held[i].idx; hn++; }
        ui_set_held(hk, hi, hn);

        /* ---- кадр ---- */
        UiDebug dbg;
        dbg.xruns = audio_xruns();
        dbg.voices = audio_voices();
        dbg.peak = audio_peak();
        dbg.error = audio_error();
        dbg.fps = fps;
        dbg.show = show_debug;
        ui_draw(dt, &dbg);
    }

    audio_stop();
    vita2d_fini();
    return 0;
}
