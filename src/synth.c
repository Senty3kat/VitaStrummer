#include "synth.h"
#include <math.h>
#include <string.h>

enum { V_FREE = 0, V_STRUM, V_PAD };
enum { ST_ATTACK, ST_DECAY, ST_SUSTAIN, ST_RELEASE };

typedef struct {
    int kind, stage, note;
    float phase, inc;            /* фаза 0..1 и шаг за сэмпл */
    float env, att_inc, dec_mul, rel_mul, gain;
} Voice;

typedef struct { int note; uint32_t at; } Pending;

#define MAX_PENDING 64
#define ENV_OFF 0.0002f

static Voice v[SYNTH_VOICES];
static Pending pend[MAX_PENDING];
static int npend = 0;
static uint32_t clock_s = 0;          /* номер текущего сэмпла */
static int peak = 0;

static const float STRUM_GAIN = 0.26f;
static const float PAD_GAIN = 0.11f;
static const float STRUM_ATTACK_S = 0.002f;
static const float PAD_ATTACK_S = 0.060f;
static const float PAD_RELEASE_S = 0.180f;

static float note_hz(int note) { return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f); }
static float exp_mul(float tau_s) { return expf(-1.0f / (tau_s * (float)SYNTH_RATE)); }

void synth_init(void) {
    memset(v, 0, sizeof(v));
    npend = 0;
    clock_s = 0;
    peak = 0;
}

/* свободный голос, иначе самый тихий щипок, иначе самый тихий вообще */
static Voice *alloc_voice(void) {
    Voice *best = NULL;
    for (int i = 0; i < SYNTH_VOICES; i++) if (v[i].kind == V_FREE) return &v[i];
    for (int i = 0; i < SYNTH_VOICES; i++)
        if (v[i].kind == V_STRUM && (!best || v[i].env < best->env)) best = &v[i];
    if (best) return best;
    for (int i = 0; i < SYNTH_VOICES; i++) if (!best || v[i].env < best->env) best = &v[i];
    return best;
}

static void start_strum(int note) {
    Voice *t = NULL;
    /* та же нота ещё звучит - перезапускаем её же голос: фаза непрерывна, щелчка нет */
    for (int i = 0; i < SYNTH_VOICES; i++)
        if (v[i].kind == V_STRUM && v[i].note == note) { t = &v[i]; break; }
    if (!t) {
        t = alloc_voice();
        /* у отнятого голоса фаза и громкость продолжаются - так меньше щелчков */
        if (t->kind == V_FREE) { t->phase = 0.0f; t->env = 0.0f; }
    }
    t->kind = V_STRUM;
    t->note = note;
    t->inc = note_hz(note) / (float)SYNTH_RATE;
    t->stage = ST_ATTACK;
    t->att_inc = 1.0f / (STRUM_ATTACK_S * (float)SYNTH_RATE);
    /* нижние струны звучат дольше верхних */
    float tau = 0.9f * powf(2.0f, -(float)(note - 60) / 24.0f);
    t->dec_mul = exp_mul(tau);
    t->gain = STRUM_GAIN;
}

void synth_strum(int note, int delay) {
    if (delay <= 0) { start_strum(note); return; }
    if (npend >= MAX_PENDING) { start_strum(note); return; }
    pend[npend].note = note;
    pend[npend].at = clock_s + (uint32_t)delay;
    npend++;
}

void synth_pad(const int *notes, int n) {
    /* отпустить то, чего нет в новом аккорде */
    for (int i = 0; i < SYNTH_VOICES; i++) {
        if (v[i].kind != V_PAD || v[i].stage == ST_RELEASE) continue;
        int keep = 0;
        for (int k = 0; k < n; k++) if (notes[k] == v[i].note) keep = 1;
        if (!keep) v[i].stage = ST_RELEASE;
    }
    /* добавить новые ноты (или вернуть затухающие) */
    for (int k = 0; k < n; k++) {
        Voice *t = NULL;
        for (int i = 0; i < SYNTH_VOICES; i++)
            if (v[i].kind == V_PAD && v[i].note == notes[k]) { t = &v[i]; break; }
        if (!t) {
            t = alloc_voice();
            if (t->kind == V_FREE) { t->env = 0.0f; t->phase = 0.0f; }
            t->kind = V_PAD;
            t->note = notes[k];
            t->inc = note_hz(notes[k]) / (float)SYNTH_RATE;
        }
        t->stage = ST_ATTACK;
        t->att_inc = 1.0f / (PAD_ATTACK_S * (float)SYNTH_RATE);
        t->rel_mul = exp_mul(PAD_RELEASE_S / 4.0f);
        t->gain = PAD_GAIN;
    }
}

void synth_all_off(void) {
    npend = 0;
    for (int i = 0; i < SYNTH_VOICES; i++)
        if (v[i].kind != V_FREE) { v[i].stage = ST_RELEASE; v[i].rel_mul = exp_mul(0.01f); }
}

/* мягкое ограничение: почти линейно до ~0.5, дальше плавно к 1 */
static inline float soft_clip(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

void synth_render(int16_t *out, int n) {
    int pk = 0;
    for (int s = 0; s < n; s++, clock_s++) {
        /* отложенные щипки, чей момент настал */
        for (int p = 0; p < npend; ) {
            if ((int32_t)(clock_s - pend[p].at) >= 0) {
                start_strum(pend[p].note);
                pend[p] = pend[--npend];
            } else p++;
        }
        float mix = 0.0f;
        for (int i = 0; i < SYNTH_VOICES; i++) {
            Voice *t = &v[i];
            if (t->kind == V_FREE) continue;
            switch (t->stage) {
            case ST_ATTACK:
                t->env += t->att_inc;
                if (t->env >= 1.0f) { t->env = 1.0f; t->stage = (t->kind == V_PAD) ? ST_SUSTAIN : ST_DECAY; }
                break;
            case ST_DECAY:
                t->env *= t->dec_mul;
                break;
            case ST_SUSTAIN:
                break;
            case ST_RELEASE:
                t->env *= t->rel_mul;
                break;
            }
            if (t->stage != ST_ATTACK && t->stage != ST_SUSTAIN && t->env < ENV_OFF) { t->kind = V_FREE; continue; }
            /* треугольник: 1 -> -1 -> 1 за период */
            float tri = 4.0f * fabsf(t->phase - 0.5f) - 1.0f;
            t->phase += t->inc;
            if (t->phase >= 1.0f) t->phase -= 1.0f;
            mix += tri * t->env * t->gain;
        }
        int o = (int)(soft_clip(mix) * 32000.0f);
        out[s] = (int16_t)o;
        if (o < 0) o = -o;
        if (o > pk) pk = o;
    }
    peak = pk;
}

int synth_active_voices(void) {
    int c = 0;
    for (int i = 0; i < SYNTH_VOICES; i++) if (v[i].kind != V_FREE) c++;
    return c;
}

int synth_peak(void) { return peak; }
