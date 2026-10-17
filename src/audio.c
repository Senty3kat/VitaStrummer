#include "audio.h"
#include "synth.h"

#include <stdint.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/audioout.h>

/* ---------- очередь событий ---------- */
enum { EV_STRUM = 1, EV_PAD = 2, EV_GUITAR = 3, EV_GDAMP = 4, EV_VOL = 5, EV_PALM = 6, EV_SUS = 7 };
typedef struct { uint8_t type, n; uint16_t delay; uint8_t notes[8]; } Ev;

#define QSIZE 256                    /* степень двойки */
static Ev q[QSIZE];
static volatile unsigned q_head = 0; /* пишет интерфейс */
static volatile unsigned q_tail = 0; /* читает аудио */

static void q_push(const Ev *e) {
    unsigned h = q_head;
    if (h - q_tail >= QSIZE) return;          /* переполнено - событие теряется, звук не встаёт */
    q[h & (QSIZE - 1)] = *e;
    __sync_synchronize();
    q_head = h + 1;
}

void audio_strum(int note, int delay_samples) {
    Ev e;
    memset(&e, 0, sizeof(e));
    e.type = EV_STRUM;
    e.notes[0] = (uint8_t)note;
    if (delay_samples < 0) delay_samples = 0;
    if (delay_samples > 4800) delay_samples = 4800;
    e.delay = (uint16_t)delay_samples;
    q_push(&e);
}

void audio_pad(const int *notes, int n) {
    Ev e;
    memset(&e, 0, sizeof(e));
    e.type = EV_PAD;
    if (n > 8) n = 8;
    e.n = (uint8_t)n;
    for (int i = 0; i < n; i++) e.notes[i] = (uint8_t)notes[i];
    q_push(&e);
}

/* гитара: notes[0] - струна, notes[1] - нота, notes[2] - 1 если заглушена, notes[3] - сила 0..255 */
void audio_guitar(int string, int note, int muted, float vel, int delay_samples) {
    Ev e;
    memset(&e, 0, sizeof(e));
    e.type = EV_GUITAR;
    e.notes[0] = (uint8_t)string;
    e.notes[1] = (uint8_t)note;
    e.notes[2] = (uint8_t)(muted ? 1 : 0);
    if (vel < 0.0f) vel = 0.0f;
    if (vel > 1.0f) vel = 1.0f;
    e.notes[3] = (uint8_t)(vel * 255.0f);
    if (delay_samples < 0) delay_samples = 0;
    if (delay_samples > 4800) delay_samples = 4800;
    e.delay = (uint16_t)delay_samples;
    q_push(&e);
}

void audio_guitar_damp(int mask) {
    Ev e;
    memset(&e, 0, sizeof(e));
    e.type = EV_GDAMP;
    e.notes[0] = (uint8_t)mask;
    q_push(&e);
}

void audio_sustain(int on) {
    Ev e;
    memset(&e, 0, sizeof(e));
    e.type = EV_SUS;
    e.notes[0] = (uint8_t)(on ? 1 : 0);
    q_push(&e);
}

void audio_palm(int on) {
    Ev e;
    memset(&e, 0, sizeof(e));
    e.type = EV_PALM;
    e.notes[0] = (uint8_t)(on ? 1 : 0);
    q_push(&e);
}

/* громкость 0..100: notes[0] - струны, notes[1] - пэд */
void audio_volume(int strings, int pad) {
    Ev e;
    memset(&e, 0, sizeof(e));
    e.type = EV_VOL;
    e.notes[0] = (uint8_t)(strings < 0 ? 0 : (strings > 100 ? 100 : strings));
    e.notes[1] = (uint8_t)(pad < 0 ? 0 : (pad > 100 ? 100 : pad));
    q_push(&e);
}

/* ---------- поток ---------- */
static volatile int g_running = 0;
static volatile int g_reset = 0;
static volatile int g_xruns = 0, g_voices = 0, g_peak = 0, g_error = 0;
static SceUID g_thread = -1;

#define N_BUFS 3   /* драйвер читает отданный буфер асинхронно - пишем по кругу в три */

static int open_port(int *channels) {
    int p = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, AUDIO_GRAIN, SYNTH_RATE, SCE_AUDIO_OUT_MODE_MONO);
    if (p >= 0) { *channels = 1; return p; }
    int p2 = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, AUDIO_GRAIN, SYNTH_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    if (p2 >= 0) { *channels = 2; return p2; }
    return p;
}

static void drain_events(void) {
    while (q_tail != q_head) {
        __sync_synchronize();
        Ev e = q[q_tail & (QSIZE - 1)];
        __sync_synchronize();
        q_tail = q_tail + 1;
        if (e.type == EV_STRUM) synth_strum(e.notes[0], e.delay);
        else if (e.type == EV_PAD) {
            int nt[8];
            for (int i = 0; i < e.n; i++) nt[i] = e.notes[i];
            synth_pad(nt, e.n);
        } else if (e.type == EV_GUITAR) {
            int note = e.notes[1];
            synth_guitar(e.notes[0], e.notes[2] ? -note : note, e.delay, (float)e.notes[3] / 255.0f);
        } else if (e.type == EV_GDAMP) {
            synth_guitar_damp(e.notes[0]);
        } else if (e.type == EV_PALM) {
            synth_palm(e.notes[0]);
        } else if (e.type == EV_SUS) {
            synth_sustain(e.notes[0]);
        } else if (e.type == EV_VOL) {
            synth_volume((float)e.notes[0] / 100.0f, (float)e.notes[1] / 100.0f);
        }
    }
}

static int audio_thread(SceSize args, void *argp) {
    static int16_t bufs[N_BUFS][AUDIO_GRAIN * 2] __attribute__((aligned(64)));
    static int16_t mono[AUDIO_GRAIN];
    int port = -1, ch = 1, bi = 0, errs = 0;
    uint64_t t_last = 0;
    const uint64_t grain_us = (uint64_t)AUDIO_GRAIN * 1000000ULL / SYNTH_RATE;

    synth_init();
    while (g_running) {
        if (g_reset) {
            if (port >= 0) { sceAudioOutReleasePort(port); port = -1; }
            g_reset = 0;
            t_last = 0;
        }
        if (port < 0) {
            port = open_port(&ch);
            if (port < 0) { g_error = port; port = -1; sceKernelDelayThread(20000); continue; }
            errs = 0;
            t_last = 0;
        }

        drain_events();
        synth_render(mono, AUDIO_GRAIN);

        int16_t *ob = bufs[bi];
        if (ch == 1) memcpy(ob, mono, sizeof(mono));
        else for (int i = 0; i < AUDIO_GRAIN; i++) { ob[2 * i] = mono[i]; ob[2 * i + 1] = mono[i]; }

        int r = sceAudioOutOutput(port, ob);
        bi = (bi + 1) % N_BUFS;

        uint64_t tn = sceKernelGetProcessTimeWide();
        if (t_last && tn - t_last > grain_us * 2) g_xruns++;
        t_last = tn;

        if (r < 0) {
            sceKernelDelayThread(2000);
            if (++errs > 100) { g_error = r; sceAudioOutReleasePort(port); port = -1; }
        } else errs = 0;

        g_voices = synth_active_voices() + synth_guitar_active();
        g_peak = synth_peak();
    }
    if (port >= 0) sceAudioOutReleasePort(port);
    return 0;
}

int audio_start(void) {
    g_running = 1;
    /* высокий приоритет и отдельное ядро, чтобы отрисовка не вытесняла звук (как в сэмплере) */
    g_thread = sceKernelCreateThread("audio_out", audio_thread, 64, 0x10000, 0, 0x40000, NULL);
    if (g_thread < 0) g_thread = sceKernelCreateThread("audio_out", audio_thread, 96, 0x10000, 0, 0x40000, NULL);
    if (g_thread < 0) g_thread = sceKernelCreateThread("audio_out", audio_thread, 96, 0x10000, 0, 0, NULL);
    if (g_thread < 0) g_thread = sceKernelCreateThread("audio_out", audio_thread, 0x10000100, 0x10000, 0, 0, NULL);
    if (g_thread < 0) { g_running = 0; return (int)g_thread; }
    sceKernelStartThread(g_thread, 0, NULL);
    return 0;
}

void audio_stop(void) {
    if (g_thread < 0) return;
    g_running = 0;
    sceKernelWaitThreadEnd(g_thread, NULL, NULL);
    sceKernelDeleteThread(g_thread);
    g_thread = -1;
}

void audio_reset_port(void) { g_reset = 1; }
int audio_xruns(void) { return g_xruns; }
int audio_voices(void) { return g_voices; }
int audio_peak(void) { return g_peak; }
int audio_error(void) { return g_error; }
