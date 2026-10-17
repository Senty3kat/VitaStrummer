/* MIDI по USB (см. midi.h). Клиент плагина подгружается при первом включении: без плагина
 * (нет psvita_usb_audio_midi.skprx в ur0:tai) приложение работает как обычно, на странице
 * настроек - "плагин не найден". Без VS_MIDI (хост-сборка, CI без клиента) - заглушки. */
#include "midi.h"
#include <string.h>

#ifdef VS_MIDI
#include "psvita_usb_audio_midi.h"

#define MAX_EV PSVITA_USB_MIDI_MAX_EVENTS

static int g_loaded = 0, g_load_res = 0;  /* клиент загружен (1) / не вышло (-1) */
static int g_acquired = 0, g_rx = 0, g_tx = 0, g_err = 0;
static float g_off_t[2][128];             /* сколько ещё звучит нота, переданная на компьютер (по каналам) */
static PsvitaUsbMidiEvent g_out[MAX_EV];
static unsigned g_nout = 0;

static void flush(void);
static void put3(int st, int d1, int d2) {
    if (g_nout >= MAX_EV) flush();
    if (g_nout >= MAX_EV) return;
    PsvitaUsbMidiEvent *e = &g_out[g_nout++];
    memset(e, 0, sizeof(*e));
    e->size = 3;
    e->data[0] = (uint8_t)st;               /* статус уже с каналом */
    e->data[1] = (uint8_t)(d1 & 0x7f);
    e->data[2] = (uint8_t)(d2 & 0x7f);
}

static void flush(void) {
    if (g_nout && g_acquired) {
        int r = psvitaUsbMidiWrite(g_out, g_nout);
        if (r < 0) g_err = r;
    }
    g_nout = 0;
}

void midi_all_off(int ch) {
    for (int n = 0; n < 128; n++)
        if (g_off_t[ch][n] > 0.0f) { put3(0x80 | ch, n, 0); g_off_t[ch][n] = 0.0f; }
    flush();
}

static void all_off(void) { midi_all_off(MIDI_CH_STR); midi_all_off(MIDI_CH_PAD); }

void midi_off(int ch, int note) {
    if (note < 0 || note > 127 || g_off_t[ch][note] <= 0.0f) return;
    g_off_t[ch][note] = 0.0f;
    if (g_acquired) put3(0x80 | ch, note, 0);
}

void midi_set(int rx, int tx) {
    g_rx = rx; g_tx = tx;
    int want = rx || tx;
    if (want && !g_loaded) {
        int r = psvitaUsbAudioMidiClientLoad("app0:psvita_usb_audio_midi_client.suprx");
        g_load_res = r;
        g_loaded = r >= 0 ? 1 : -1;
    }
    if (want && g_loaded == 1 && !g_acquired) {
        int r = psvitaUsbAudioMidiAcquire(PSVITA_USB_AUDIO_MIDI_ACQUIRE_REPLACE_MTP);
        if (r >= 0) g_acquired = 1;
        else g_err = r;
    }
    if (!tx && g_acquired) all_off();
    if (!want && g_acquired) {
        psvitaUsbAudioMidiRelease();
        g_acquired = 0;
    }
}

int midi_state(void) {
    if (!g_rx && !g_tx) return MIDI_OFF;
    if (g_loaded != 1) return MIDI_NO_PLUGIN;
    if (!g_acquired) return MIDI_ERROR;
    PsvitaUsbAudioMidiStatus st;
    memset(&st, 0, sizeof(st));
    st.size = sizeof(st);
    if (psvitaUsbAudioMidiGetStatus(&st) < 0) return MIDI_ERROR;
    return (st.flags & PSVITA_USB_AUDIO_MIDI_STATUS_CONNECTED) ? MIDI_CONNECTED : MIDI_WAIT_CABLE;
}

int midi_error(void) { return g_err ? g_err : g_load_res; }

void midi_note(int ch, int note, int vel, float dur) {
    if (!g_tx || !g_acquired || note < 0 || note > 127) return;
    if (vel < 1) vel = 1;
    if (vel > 127) vel = 127;
    if (g_off_t[ch][note] > 0.0f) put3(0x80 | ch, note, 0);    /* та же нота ещё звучит - сначала off */
    put3(0x90 | ch, note, vel);
    g_off_t[ch][note] = dur > 0.02f ? dur : 0.02f;
}

void midi_frame(float dt) {
    if (!g_acquired) return;
    for (int c = 0; c < 2; c++)
        for (int n = 0; n < 128; n++)
            if (g_off_t[c][n] > 0.0f && g_off_t[c][n] < MIDI_HOLD * 0.5f && (g_off_t[c][n] -= dt) <= 0.0f) {
                g_off_t[c][n] = 0.0f;
                put3(0x80 | c, n, 0);
            }
    flush();
}

int midi_read(int *notes, int *vels, int max) {
    if (!g_rx || !g_acquired) return 0;
    PsvitaUsbMidiEvent ev[32];
    int n = psvitaUsbMidiRead(ev, 32), k = 0;
    for (int i = 0; i < n && k < max; i++) {
        uint8_t st = ev[i].data[0] & 0xf0;
        if (ev[i].size == 3 && st == 0x90 && ev[i].data[2]) { notes[k] = ev[i].data[1]; vels[k] = ev[i].data[2]; k++; }
    }
    return k;
}

void midi_shutdown(void) {
    if (g_acquired) { all_off(); psvitaUsbAudioMidiRelease(); g_acquired = 0; }
}

#else  /* без клиента плагина - MIDI нет */

static int g_rx = 0, g_tx = 0;
void midi_set(int rx, int tx) { g_rx = rx; g_tx = tx; }
int  midi_state(void) { return (g_rx || g_tx) ? MIDI_NO_PLUGIN : MIDI_OFF; }
int  midi_error(void) { return 0; }
void midi_note(int ch, int note, int vel, float dur) { (void)ch; (void)note; (void)vel; (void)dur; }
void midi_off(int ch, int note) { (void)ch; (void)note; }
void midi_all_off(int ch) { (void)ch; }
void midi_frame(float dt) { (void)dt; }
int  midi_read(int *notes, int *vels, int max) { (void)notes; (void)vels; (void)max; return 0; }
void midi_shutdown(void) { }

#endif
