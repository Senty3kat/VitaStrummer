/*
 * Vita Strummer - версия 0.6
 *
 * Колесо слева, аккорд выбирается в три шага:
 *   1. внешнее кольцо - тоника. Касание - аккорд "залипает"; повторное касание той же тоники - сброс;
 *   2. центр - качество: верхняя половина maj, нижняя min;
 *   3. внутреннее кольцо - модификатор (подписи показывают итог: 7 / m7, maj7 / mM7, sus4 ...).
 *   Кнопка Круг - сброс аккорда.
 * Быстрые аккорды (8 штук, видны в углах слева от колеса):
 *   D-pad / левый стик - вызвать; SELECT + D-pad / SELECT + стик - сохранить текущий (без аккорда - очистить).
 *   Можно и просто коснуться "кнопки" быстрого аккорда.
 * Справа (Strum, Pad) 25 клавиш C4..C6, каждая переходит в свою струну. Без аккорда - хроматика,
 *   с аккордом струны идут парами: каждая пара - следующий звук аккорда вверх (около 4 октав).
 *   Играть можно и по клавишам, и по струнам.
 * Сверху:
 *   MODE (или короткое нажатие SELECT): Strum - звучат струны; Pad - аккорд звучит, пока выбран,
 *     струны тоже играют; Guitar - 6 гитарных струн, аккорд в гитарной аппликатуре, у каждой струны
 *     свой тембр; сила удара зависит от скорости проведения
 *   VOLUME (справа сверху): в Pad - громкость пэда, в остальных режимах - струн
 *   OCTAVE (или L / R): сдвиг струн и пэда на октаву, -2..+2
 *   START - показать/скрыть отладку (кадры, голоса, опоздания аудио)
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
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
#define SET_PATH "ux0:data/Strummer/settings.bin"
#define SET_TMP  "ux0:data/Strummer/settings.tmp"

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
static StringMap smap;                /* 25 струн страммера */
static StringMap gmap;                /* 6 гитарных струн: зона = струна */
static GuitarChord gch;               /* текущая гитарная аппликатура */
static int gch_valid = 0;
static StrumFinger fingers[MAX_FINGERS];
static int pad_sent[MAX_CHORD], pad_sent_n = -1;
static float g_frame_dt = 1.0f / 60.0f;
static float g_vel = 0.75f;           /* сила текущего удара (гитара) */

/* касания кнопок: пока палец лежит, кнопка выглядит нажатой */
typedef struct { int id, kind, idx; } Held;
static Held held[MAX_FINGERS];

static int guitar(void) { return st.mode == MODE_GUITAR; }
static const StringMap *cur_map(void) { return guitar() ? &gmap : &smap; }
static float finger_pos(float x, float y) { return guitar() ? gtr_pos_from_y(y) : strum_pos_from_xy(x, y); }
static int base_note(void) { return STRING_BASE_NOTE + 12 * st.octave; }

/* ---------- настройки: режим, октава, громкость, быстрые аккорды ---------- */
#define SET_MAGIC 0x53545231u          /* "STR1" */
typedef struct { uint32_t magic, size; int mode, octave, vol_strings, vol_pad, quick[QS_N]; } Settings;
static int set_dirty = 0;
static uint64_t set_dirty_t = 0;

static void settings_load(void) {
    Settings s;
    FILE *f = fopen(SET_PATH, "rb");
    if (!f) return;
    size_t n = fread(&s, 1, sizeof(s), f);
    fclose(f);
    if (n != sizeof(s) || s.magic != SET_MAGIC || s.size != sizeof(s)) return;
    if (s.mode >= 0 && s.mode < N_MODES) st.mode = s.mode;
    if (s.octave >= OCT_MIN && s.octave <= OCT_MAX) st.octave = s.octave;
    if (s.vol_strings >= 0 && s.vol_strings <= 100) st.vol_strings = s.vol_strings;
    if (s.vol_pad >= 0 && s.vol_pad <= 100) st.vol_pad = s.vol_pad;
    for (int k = 0; k < QS_N; k++)
        if (s.quick[k] >= 0 && s.quick[k] <= quick_code(11, 1, N_MODS - 1)) st.quick[k] = s.quick[k];
}

static void settings_save(void) {
    Settings s = { SET_MAGIC, sizeof(Settings), st.mode, st.octave, st.vol_strings, st.vol_pad, { 0 } };
    for (int k = 0; k < QS_N; k++) s.quick[k] = st.quick[k];
    FILE *f = fopen(SET_TMP, "wb");             /* сначала во временный файл - сбой не испортит старый */
    if (!f) return;
    size_t n = fwrite(&s, 1, sizeof(s), f);
    fclose(f);
    if (n != sizeof(s)) return;
    sceIoRemove(SET_PATH);
    sceIoRename(SET_TMP, SET_PATH);
}

static void settings_touch(void) { set_dirty = 1; set_dirty_t = sceKernelGetProcessTimeWide(); }

static void send_pad(void) {
    int notes[MAX_CHORD], n = 0;
    if (st.mode == MODE_PAD && st.chord_on) {
        n = pad_voicing(st.root, chord_type(st.minor, st.mod), notes);
        for (int i = 0; i < n; i++) notes[i] += 12 * st.octave;
    }
    if (n == pad_sent_n && memcmp(notes, pad_sent, sizeof(int) * n) == 0) return;
    audio_pad(notes, n);
    memcpy(pad_sent, notes, sizeof(int) * n);
    pad_sent_n = n;
}

/* гитара: аппликатура и 6 зон; струны, у которых сменилась нота, глушатся - как когда пальцы
 * переставляют на новый аккорд, а общие ноты продолжают звучать */
static void build_guitar(void) {
    GuitarChord ng;
    guitar_voicing(&ng, st.chord_on, st.root, chord_type(st.minor, st.mod), 12 * st.octave);
    if (gch_valid && guitar()) {
        int mask = 0;
        for (int s = 0; s < N_GSTR; s++) if (ng.note[s] != gch.note[s]) mask |= 1 << s;
        if (mask) audio_guitar_damp(mask);
    }
    gch = ng;
    gch_valid = 1;
    gmap.nzones = N_GSTR;
    for (int s = 0; s < N_GSTR; s++) {
        gmap.note[s] = gch.note[s];
        gmap.zone_of[s] = s;
        gmap.zone_lo[s] = gmap.zone_hi[s] = s;
        gmap.zone_note[s] = gch.note[s];
    }
}

static void apply_state(void) {
    stringmap_build(&smap, st.chord_on, st.root, chord_type(st.minor, st.mod), base_note());
    build_guitar();
    for (int i = 0; i < MAX_FINGERS; i++) strum_remap(&fingers[i], cur_map());
    ui_set_state(&st, &smap);
    send_pad();
}

static void on_strum(int zone, float frac, void *user) {
    (void)user;
    int delay = (int)(frac * g_frame_dt * (float)SYNTH_RATE);
    if (guitar()) {
        int s = zone;
        if (gch.note[s] >= 0) audio_guitar(s, gch.note[s], 0, g_vel, delay);
        else audio_guitar(s, GT_TUNING[s] + 12 * st.octave, 1, g_vel, delay);   /* заглушённая - глухой стук */
        return;
    }
    audio_strum(smap.zone_note[zone], delay);
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
    if (o != st.octave) settings_touch();
    st.octave = o;
}

static void set_mode(int m) {
    if (m == st.mode) return;
    st.mode = m;
    for (int i = 0; i < MAX_FINGERS; i++) fingers[i].id = -1;   /* у режимов разные струны */
    settings_touch();
}

/* громкость по положению пальца на дорожке */
static void set_volume_x(float x) {
    float v = (x - VOL_TX0) / (float)(VOL_TX1 - VOL_TX0) * 100.0f;
    int iv = v < 0.0f ? 0 : (v > 100.0f ? 100 : (int)(v + 0.5f));
    int *dst = st.mode == MODE_PAD ? &st.vol_pad : &st.vol_strings;
    if (*dst == iv) return;
    *dst = iv;
    audio_volume(st.vol_strings, st.vol_pad);
    ui_set_state(&st, &smap);
    settings_touch();
}

static void quick_recall(int k) {
    if (k < 0 || k >= QS_N || !st.quick[k]) return;
    quick_decode(st.quick[k], &st.root, &st.minor, &st.mod);
    st.chord_on = 1;
    apply_state();
}

static void quick_store(int k) {
    if (k < 0 || k >= QS_N) return;
    st.quick[k] = st.chord_on ? quick_code(st.root, st.minor, st.mod) : 0;
    ui_set_state(&st, &smap);
    settings_touch();
}

/* новое касание вне струн: колесо, полоса сверху, быстрые аккорды */
static int touch_down_ui(int kind, int idx, float x) {
    switch (kind) {
    case HIT_ROOT:
        if (st.chord_on && st.root == idx) st.chord_on = 0;
        else { st.chord_on = 1; st.root = idx; }
        break;
    case HIT_MOD:    st.mod = idx; break;
    case HIT_QUAL:   st.minor = idx; break;
    case HIT_MODE:   set_mode(idx); break;
    case HIT_OCT:    octave_step(idx); break;
    case HIT_VOL:    set_volume_x(x); return 1;
    case HIT_QUICK:  quick_recall(idx); return 1;
    default:         return 0;
    }
    apply_state();
    return 1;
}

/* направление стика с гистерезисом: 0 вверх, 1 вправо, 2 вниз, 3 влево, -1 в центре */
static int stick_dir(int lx, int ly, int prev) {
    int dx = lx - 128, dy = ly - 128, m2 = dx * dx + dy * dy;
    if (m2 < 50 * 50) return -1;
    if (prev >= 0 || m2 < 85 * 85) return prev;     /* направление меняется только через центр */
    if (abs(dx) > abs(dy)) return dx > 0 ? 1 : 3;
    return dy > 0 ? 2 : 0;
}

int main(void) {
    sceIoMkdir(LOG_DIR, 0777);
    dbg_log("start v0.6");

    vita2d_init_advanced(4 * 1024 * 1024);
    vita2d_set_clear_color(RGBA8(60, 86, 110, 255));
    uint64_t t0 = sceKernelGetProcessTimeWide();
    if (!ui_init()) { dbg_log("ui_init failed"); sceKernelExitProcess(0); }
    dbg_log("ui_init %d ms", (int)((sceKernelGetProcessTimeWide() - t0) / 1000));

    memset(&st, 0, sizeof(st));
    st.mode = MODE_STRUM;
    st.vol_strings = 70;
    st.vol_pad = 70;
    settings_load();
    touches_reset();

    int ar = audio_start();
    dbg_log("audio_start=%d grain=%d", ar, AUDIO_GRAIN);
    audio_volume(st.vol_strings, st.vol_pad);
    apply_state();

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

    SceCtrlData pad;
    SceTouchData touch;
    unsigned prev_buttons = 0;
    int prev_ids[MAX_FINGERS], prev_n = 0;
    uint64_t last_t = sceKernelGetProcessTimeWide();
    uint64_t last_sys = sceKernelGetSystemTimeWide();
    float fps = 60.0f;
    int show_debug = 0;
    int sel_combo = 0;                 /* пока зажат SELECT, им сохраняли аккорд - режим не переключать */
    int stick = -1;

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
        unsigned pressed = pad.buttons & ~prev_buttons, released = prev_buttons & ~pad.buttons;
        prev_buttons = pad.buttons;
        int sel = (pad.buttons & SCE_CTRL_SELECT) != 0;
        if (pressed & SCE_CTRL_SELECT) sel_combo = 0;

        /* D-pad: вызвать / с SELECT - сохранить быстрый аккорд 0..3 */
        static const unsigned DPAD[4] = { SCE_CTRL_UP, SCE_CTRL_RIGHT, SCE_CTRL_DOWN, SCE_CTRL_LEFT };
        for (int d = 0; d < 4; d++)
            if (pressed & DPAD[d]) {
                if (sel) { quick_store(d); sel_combo = 1; }
                else quick_recall(d);
            }
        /* левый стик: то же для 4..7 */
        int sd = stick_dir(pad.lx, pad.ly, stick);
        if (sd >= 0 && stick < 0) {
            if (sel) { quick_store(4 + sd); sel_combo = 1; }
            else quick_recall(4 + sd);
        }
        stick = sd;

        if ((released & SCE_CTRL_SELECT) && !sel_combo) { set_mode((st.mode + 1) % N_MODES); apply_state(); }
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
            if (f) {
                float p = finger_pos(x, y);
                /* сила удара: быстрее проведение - громче и ярче (струн в секунду) */
                float speed = fabsf(p - f->pos) / dt;
                float v = speed / (guitar() ? 30.0f : 120.0f);
                g_vel = 0.45f + 0.55f * (v > 1.0f ? 1.0f : v);
                strum_move(f, p, cur_map(), on_strum, NULL);
                ui_touch_glow(f->x, f->y, x, y, 0);
                f->x = x; f->y = y;
                continue;
            }
            Held *hv = held_find(id);
            if (hv && hv->kind == HIT_VOL) { set_volume_x(x); continue; }   /* ручку громкости тянут */
            if (!is_new) continue;

            int idx, kind = ui_hit(x, y, &idx);
            if (kind == HIT_PLATE) {
                StrumFinger *fr = finger_find(-1);
                if (fr) {
                    fr->x = x; fr->y = y;
                    g_vel = 0.75f;
                    strum_down(fr, id, finger_pos(x, y), cur_map(), on_strum, NULL);
                    ui_touch_glow(x, y, x, y, 1);
                }
            } else if (touch_down_ui(kind, idx, x)) {
                Held *h = held_find(-1);
                if (h) { h->id = id; h->kind = kind; h->idx = kind == HIT_VOL ? 0 : idx; }
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

        /* ---- настройки: сохранить через секунду после последнего изменения ---- */
        if (set_dirty && now_t - set_dirty_t > 1000000ULL) { settings_save(); set_dirty = 0; }

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
