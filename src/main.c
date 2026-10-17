/*
 * Vita Strummer - версия 0.9
 *
 * Колесо слева, аккорд выбирается в три шага:
 *   1. внешнее кольцо - тоника. Касание - аккорд "залипает"; повторное касание той же тоники - сброс;
 *   2. центр - качество: верхняя половина maj, нижняя min;
 *   3. внутреннее кольцо - модификатор (подписи показывают итог: 7 / m7, maj7 / mM7, sus4 ...).
 *   Кнопка Круг - сброс аккорда.
 * Тональность (Треугольник или касание табло - вкл/выкл): берётся от текущего аккорда (Am - ля минор).
 *   На колесе - ступени лада (I ii iii ...), чужие ноты приглушены; касание тоники лада само ставит
 *   мажор / минор / уменьшённый. Без аккорда струны Strum/Pad идут по звукам лада.
 * Задняя панель: под правой рукой - ладонь на струнах (звенящее гаснет, удары короткие и глухие),
 * под левой - сустейн (струны и пэд звучат дольше, но не бесконечно; гитара не глушит струны при смене аккорда).
 * Быстрые аккорды (8 штук, видны в углах слева от колеса):
 *   D-pad / левый стик - вызвать; SELECT + D-pad / SELECT + стик - сохранить текущий вместе с октавой
 *   (без аккорда - очистить).
 *   Можно и просто коснуться "кнопки" быстрого аккорда.
 * Справа (Strum, Pad) 25 клавиш C4..C6, каждая переходит в свою струну. Без аккорда - хроматика,
 *   с аккордом струны идут парами: каждая пара - следующий звук аккорда вверх (около 4 октав).
 *   Играть можно и по клавишам, и по струнам.
 * Сверху:
 *   MODE (или короткое нажатие SELECT): Strum - звучат струны; Pad - аккорд звучит, пока выбран,
 *     струны тоже играют; Guitar - 6 гитарных струн, аккорд в гитарной аппликатуре, у каждой струны
 *     свой тембр; сила удара зависит от скорости проведения. Под грифом - полоска позиций:
 *     номер лада - сыграть аккорд в этой позиции (рука с этого лада), AUTO - лучшая позиция
 *   VOLUME (справа сверху): в Pad - громкость пэда, в остальных режимах - струн
 *   OCTAVE (или L / R): сдвиг струн и пэда на октаву, -2..+2
 *   TRANSPOSE: сдвиг звука на полутоны, -6..+6 (у гитары - как каподастр, аппликатура та же)
 *   START - показать/скрыть отладку (кадры, голоса, опоздания аудио)
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stddef.h>

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
/* задняя панель: x от 0 до ~1920 (как смотреть на экран); ладонью считается касание справа */
#define BACK_PALM_X0 1000                  /* под правой рукой: правая половина панели */
#define BACK_PALM_X1 1760                  /* у самого края лежат пальцы, которыми держат консоль */
#define BACK_SUS_X0 160                    /* под левой рукой - сустейн */
#define BACK_SUS_X1 920
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
static float g_now = 0.0f;           /* секунды с запуска */
static float g_play_t = -10.0f;      /* когда последний раз палец был на струнах */
/* Защита от случайных касаний во время игры: кнопки верхней полосы (режим, октава, транспозиция,
 * табло, громкость) не срабатывают, пока палец на струнах и ещё PLAY_GUARD_S после. */
#define PLAY_GUARD_S 0.7f
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
typedef struct { uint32_t magic, size; int mode, octave, vol_strings, vol_pad, quick[QS_N], key_on, key_root, key_minor, gpos, transpose; } Settings;
static int set_dirty = 0;
static uint64_t set_dirty_t = 0;

static void settings_load(void) {
    Settings s;
    memset(&s, 0, sizeof(s));
    FILE *f = fopen(SET_PATH, "rb");
    if (!f) return;
    size_t n = fread(&s, 1, sizeof(s), f);
    fclose(f);
    /* файл старой версии короче: поля, которых в нём нет, остаются по умолчанию */
    if (n < offsetof(Settings, gpos) || s.magic != SET_MAGIC || s.size != n) return;
    if (s.mode >= 0 && s.mode < N_MODES) st.mode = s.mode;
    if (s.octave >= OCT_MIN && s.octave <= OCT_MAX) st.octave = s.octave;
    /* громкость струн больше не регулируется (автору нужен только баланс в Pad) - всегда 70 */
    if (s.vol_pad >= 0 && s.vol_pad <= 100) st.vol_pad = s.vol_pad;
    for (int k = 0; k < QS_N; k++)
        if (s.quick[k] >= 0 && s.quick[k] <= QUICK_MAX) st.quick[k] = s.quick[k];
    st.key_on = s.key_on ? 1 : 0;
    if (s.key_root >= 0 && s.key_root < 12) st.key_root = s.key_root;
    st.key_minor = s.key_minor ? 1 : 0;
    if (n >= offsetof(Settings, gpos) + sizeof(int) && s.gpos >= 0 && s.gpos <= GT_POS_MAX) st.gpos = s.gpos;
    if (n >= offsetof(Settings, transpose) + sizeof(int) && s.transpose >= TRANSPOSE_MIN && s.transpose <= TRANSPOSE_MAX)
        st.transpose = s.transpose;
}

static void settings_save(void) {
    Settings s;
    memset(&s, 0, sizeof(s));
    s.magic = SET_MAGIC; s.size = sizeof(Settings);
    s.mode = st.mode; s.octave = st.octave; s.vol_strings = st.vol_strings; s.vol_pad = st.vol_pad;
    for (int k = 0; k < QS_N; k++) s.quick[k] = st.quick[k];
    s.key_on = st.key_on; s.key_root = st.key_root; s.key_minor = st.key_minor; s.gpos = st.gpos; s.transpose = st.transpose;
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
        for (int i = 0; i < n; i++) notes[i] += 12 * st.octave + st.transpose;
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
    /* транспозиция у гитары - как каподастр: аппликатура та же, звук выше/ниже */
    guitar_voicing(&ng, st.chord_on, st.root, chord_type(st.minor, st.mod), 12 * st.octave + st.transpose, st.gpos);
    if (gch_valid && guitar()) {
        int mask = 0;
        for (int s = 0; s < N_GSTR; s++) if (ng.note[s] != gch.note[s]) mask |= 1 << s;
        if (mask && !st.sus) audio_guitar_damp(mask);   /* с сустейном старые ноты звенят дальше */
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
    if (!st.chord_on && st.key_on)                   /* тональность без аккорда - струны по ладу */
        stringmap_build_scale(&smap, key_scale_mask(st.key_root, st.key_minor), base_note());
    else
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
        if (gch.note[s] >= 0) audio_guitar(s, gch.note[s], gch.fret[s], 0, g_vel, delay);
        else audio_guitar(s, GT_TUNING[s] + 12 * st.octave + st.transpose, 0, 1, g_vel, delay);   /* заглушённая - глухой стук */
        return;
    }
    audio_strum(smap.zone_note[zone] + st.transpose, delay);
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

static void transpose_step(int d) {
    int t = st.transpose + d;
    if (t < TRANSPOSE_MIN) t = TRANSPOSE_MIN;
    if (t > TRANSPOSE_MAX) t = TRANSPOSE_MAX;
    if (t != st.transpose) settings_touch();
    st.transpose = t;
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
    if (st.mode != MODE_PAD) return;               /* ручка громкости есть только в Pad */
    int *dst = &st.vol_pad;
    if (*dst == iv) return;
    *dst = iv;
    audio_volume(st.vol_strings, st.vol_pad);
    ui_set_state(&st, &smap);
    settings_touch();
}

static void quick_recall(int k) {
    if (k < 0 || k >= QS_N || !st.quick[k]) return;
    quick_decode(st.quick[k], &st.root, &st.minor, &st.mod, &st.octave, &st.gpos);   /* и октава, и позиция на грифе */
    st.chord_on = 1;
    apply_state();
}

static void quick_store(int k) {
    if (k < 0 || k >= QS_N) return;
    st.quick[k] = st.chord_on ? quick_code(st.root, st.minor, st.mod, st.octave, st.gpos) : 0;
    ui_set_state(&st, &smap);
    settings_touch();
}

/* тональность: ступень тоники сама задаёт мажор / минор; на уменьшённой ступени трезвучие - dim */
#define MOD_DIM_AUG 8                  /* кнопка модификатора: у минора - dim, у мажора - aug */
static void key_auto_quality(void) {
    if (!st.key_on) return;
    int deg = key_degree(st.key_root, st.key_minor, st.root);
    if (deg < 0) return;                       /* чужая нота - как выбрано вручную */
    int q = key_degree_quality(st.key_minor, deg);
    st.minor = q != 0;
    if (q == 2 && st.mod == 0) st.mod = MOD_DIM_AUG;
    else if (q != 2 && st.mod == MOD_DIM_AUG) st.mod = 0;
}

/* включить тональность от текущего (или последнего выбранного) аккорда / выключить */
static void key_toggle(void) {
    if (st.key_on) st.key_on = 0;
    else { st.key_on = 1; st.key_root = st.root; st.key_minor = st.minor; }
    settings_touch();
}

/* новое касание вне струн: колесо, полоса сверху, быстрые аккорды */
static int touch_down_ui(int kind, int idx, float x) {
    switch (kind) {
    case HIT_ROOT:
        if (st.chord_on && st.root == idx) st.chord_on = 0;
        else { st.chord_on = 1; st.root = idx; key_auto_quality(); }
        break;
    case HIT_KEY:    key_toggle(); break;
    case HIT_GPOS:                              /* позиция на грифе; повторное касание - снова авто */
        if (idx < 0) return 1;
        /* у мажора и минора - только привычные формы: касание пустой ячейки берёт ближайшую */
        if (idx > 0 && st.chord_on) idx = guitar_snap_pos(st.root, chord_type(st.minor, st.mod), idx);
        st.gpos = (idx > 0 && idx == gch.pos) ? 0 : idx;
        settings_touch();
        break;
    case HIT_MOD:    st.mod = idx; break;
    case HIT_QUAL:   st.minor = idx; break;
    case HIT_MODE:   set_mode(idx); break;
    case HIT_OCT:    octave_step(idx); break;
    case HIT_TRN:    transpose_step(idx); break;
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
    dbg_log("start v0.9.3");

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
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

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
        g_now += dt;
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
        if (pressed & SCE_CTRL_TRIANGLE) { key_toggle(); apply_state(); }
        if (pressed & SCE_CTRL_LTRIGGER) { octave_step(-1); apply_state(); }
        if (pressed & SCE_CTRL_RTRIGGER) { octave_step(1); apply_state(); }
        if (pressed & SCE_CTRL_START)    show_debug = !show_debug;

        /* ---- задняя панель: справа - ладонь на струнах, слева - сустейн ----
         * Самые края не считаются: там лежат пальцы, которыми держат консоль. */
        {
            SceTouchData back;
            sceTouchPeek(SCE_TOUCH_PORT_BACK, &back, 1);
            int palm = 0, sus = 0;
            for (int k = 0; k < (int)back.reportNum && k < 4; k++) {
                int bx = back.report[k].x;
                if (bx > BACK_PALM_X0 && bx < BACK_PALM_X1) palm = 1;
                if (bx >= BACK_SUS_X0 && bx < BACK_SUS_X1) sus = 1;
            }
            if (palm != st.palm) { st.palm = palm; audio_palm(palm); ui_set_state(&st, &smap); }
            if (sus != st.sus) { st.sus = sus; audio_sustain(sus); ui_set_state(&st, &smap); }
        }

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
                g_play_t = g_now;
                float p = finger_pos(x, y);
                /* сила удара: быстрее проведение - громче и ярче (струн в секунду) */
                float speed = fabsf(p - f->pos) / dt;
                float v = speed / (guitar() ? 30.0f : 120.0f);
                g_vel = 0.45f + 0.55f * (v > 1.0f ? 1.0f : v);
                if (guitar()) strum_gtr_move(f, x, y, on_strum, NULL);       /* каждое пересечение струны */
                else strum_move_xy(f, x, y, cur_map(), on_strum, NULL);    /* хитбоксы клавиш */
                ui_touch_glow(f->x, f->y, x, y, 0);
                f->x = x; f->y = y;
                continue;
            }
            Held *hv = held_find(id);
            if (hv && hv->kind == HIT_VOL) { set_volume_x(x); continue; }   /* ручку громкости тянут */
            if (!is_new) continue;

            int idx, kind = ui_hit(x, y, &idx);
            int top = kind == HIT_MODE || kind == HIT_OCT || kind == HIT_TRN || kind == HIT_VOL || kind == HIT_KEY;
            if (top && g_now - g_play_t < PLAY_GUARD_S) continue;     /* играют - верхние кнопки спят */
            if (kind == HIT_PLATE) {
                g_play_t = g_now;
                StrumFinger *fr = finger_find(-1);
                if (fr) {
                    fr->x = x; fr->y = y;
                    g_vel = 0.75f;
                    if (guitar()) strum_gtr_down(fr, id, x, y, on_strum, NULL);
                    else strum_down_xy(fr, id, x, y, cur_map(), on_strum, NULL);
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
