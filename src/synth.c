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

/* отложенный удар: kind 0 - струна страммера, 1 - гитарная струна */
typedef struct { int kind, note, gstr; float vel; uint32_t at; } Pending;

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

/* громкость слоёв 0..1 (ручка на экране); 0.7 - уровень по умолчанию */
static float g_vol_strings = 0.7f, g_vol_pad = 0.7f;
#define VOL_SCALE (1.0f / 0.7f)

static float note_hz(int note) { return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f); }
static float exp_mul(float tau_s) { return expf(-1.0f / (tau_s * (float)SYNTH_RATE)); }

static void guitar_init(void);
static float guitar_render_sample(void);
static void guitar_pluck(int s, int note, float vel);

void synth_init(void) {
    memset(v, 0, sizeof(v));
    guitar_init();
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
    pend[npend].kind = 0;
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

void synth_volume(float strings, float pad) {
    g_vol_strings = strings < 0.0f ? 0.0f : (strings > 1.0f ? 1.0f : strings);
    g_vol_pad = pad < 0.0f ? 0.0f : (pad > 1.0f ? 1.0f : pad);
}

/* ---------- гитара ----------
 * Каждая из 6 струн - щипковая модель Карплуса-Стронга (линия задержки длиной в период +
 * усредняющий фильтр в петле: высокие гармоники гаснут быстрее низких, как у настоящей струны).
 * На струне один звук: новый щипок заменяет старый (старый быстро гасится, без щелчка).
 * У каждой струны свой "эквалайзер" и характер - толщина, оплётка, место щипка:
 *   - яркость щипка (фильтр шума возбуждения) и место щипка (гребёнка: у толстых ближе к подставке);
 *   - длительность звучания: басовые звенят дольше;
 *   - корпусный резонанс (пиковый фильтр) и срез верхов (ФНЧ) - у толстых глуше и "телесней",
 *     у тонких звонче;
 *   - небольшая расстройка в центах.
 * Поэтому одна и та же нота на разных струнах звучит по-разному. */
#define G_BUF 2600                       /* период E0 (сдвиг на -2 октавы) ~2330 сэмплов */
#define G_SLOTS 2

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } Biquad;

typedef struct {
    float buf[G_BUF];
    int n, idx, active;
    float last, ap_c, ap_x1, ap_y1;      /* усреднение в петле и дробная задержка (фазовращатель) */
    float g;                             /* потери за период */
    float level, level_mul;              /* оценка громкости - чтобы вовремя выключить */
    float env, damp;                     /* огибающая гашения: множитель за сэмпл 1 - звучит, <1 - гасится */
    float out_gain;
} KsVoice;

typedef struct {
    float t60, exc_lp, pick, body_f, body_db, body_q, lp_f, cents, gain;
} GStrParam;

/*                       t60   щипок место  корпус Гц  дБ   Q    срез Гц  центы громк. */
static const GStrParam GP[N_GSTR_SYNTH] = {
    /* 0 E2 - толстая, оплётка */ { 4.2f, 0.24f, 0.11f,  110.0f, 6.0f, 0.9f, 1700.0f, -1.6f, 1.00f },
    /* 1 A2 - оплётка          */ { 3.8f, 0.28f, 0.12f,  170.0f, 5.0f, 0.9f, 2100.0f,  1.1f, 0.96f },
    /* 2 D3 - оплётка          */ { 3.4f, 0.33f, 0.13f,  240.0f, 4.0f, 1.0f, 2600.0f, -0.8f, 0.92f },
    /* 3 G3 - оплётка, тонкая  */ { 3.0f, 0.40f, 0.15f,  420.0f, 3.0f, 1.0f, 3300.0f,  1.3f, 0.88f },
    /* 4 B3 - гладкая          */ { 2.6f, 0.62f, 0.18f, 1900.0f, 3.0f, 1.2f, 6000.0f, -1.0f, 0.82f },
    /* 5 E4 - гладкая, тонкая  */ { 2.2f, 0.80f, 0.20f, 2900.0f, 4.0f, 1.3f, 9000.0f,  0.7f, 0.76f },
};

static KsVoice gv[N_GSTR_SYNTH][G_SLOTS];
static int gcur[N_GSTR_SYNTH];
static Biquad geq[N_GSTR_SYNTH][2];
static uint32_t g_rng = 0x12345678u;
static const float GUITAR_GAIN = 0.34f;

static float rnd(void) {                 /* шум -1..1 */
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)(int32_t)g_rng * (1.0f / 2147483648.0f);
}

/* формулы RBJ: пиковый фильтр и ФНЧ 2-го порядка */
static void bq_peak(Biquad *q, float f, float db, float Q) {
    float A = powf(10.0f, db / 40.0f), w = 2.0f * 3.14159265f * f / (float)SYNTH_RATE;
    float al = sinf(w) / (2.0f * Q), c = cosf(w), a0 = 1.0f + al / A;
    q->b0 = (1.0f + al * A) / a0; q->b1 = -2.0f * c / a0; q->b2 = (1.0f - al * A) / a0;
    q->a1 = -2.0f * c / a0;        q->a2 = (1.0f - al / A) / a0;
    q->z1 = q->z2 = 0.0f;
}
static void bq_lowpass(Biquad *q, float f, float Q) {
    float w = 2.0f * 3.14159265f * f / (float)SYNTH_RATE, al = sinf(w) / (2.0f * Q), c = cosf(w), a0 = 1.0f + al;
    q->b0 = (1.0f - c) * 0.5f / a0; q->b1 = (1.0f - c) / a0; q->b2 = q->b0;
    q->a1 = -2.0f * c / a0;         q->a2 = (1.0f - al) / a0;
    q->z1 = q->z2 = 0.0f;
}
static inline float bq_run(Biquad *q, float x) {
    float y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

static void guitar_init(void) {
    memset(gv, 0, sizeof(gv));
    memset(gcur, 0, sizeof(gcur));
    for (int s = 0; s < N_GSTR_SYNTH; s++) {
        bq_peak(&geq[s][0], GP[s].body_f, GP[s].body_db, GP[s].body_q);
        bq_lowpass(&geq[s][1], GP[s].lp_f, 0.707f);
    }
}

/* щипок струны s: note < 0 - заглушенная струна (короткий глухой стук открытой нотой) */
static void guitar_pluck(int s, int note, float vel) {
    if (s < 0 || s >= N_GSTR_SYNTH) return;
    const GStrParam *p = &GP[s];
    int muted = note < 0;
    if (muted) note = -note;
    /* старый звук на этой струне быстро гаснет */
    KsVoice *old = &gv[s][gcur[s]];
    if (old->active) old->damp = exp_mul(0.012f);
    gcur[s] = (gcur[s] + 1) % G_SLOTS;
    KsVoice *k = &gv[s][gcur[s]];

    float hz = note_hz(note) * powf(2.0f, p->cents / 1200.0f);
    float P = (float)SYNTH_RATE / hz;
    int n = (int)(P - 0.6f);
    if (n > G_BUF - 1) n = G_BUF - 1;
    if (n < 8) n = 8;
    float d = P - 0.5f - (float)n;                       /* остаток задержки - фазовращателем */
    if (d < 0.05f) d = 0.05f;
    k->n = n; k->idx = 0; k->last = 0.0f;
    k->ap_c = (1.0f - d) / (1.0f + d); k->ap_x1 = k->ap_y1 = 0.0f;
    float t60 = muted ? 0.05f : p->t60 * (note < 52 ? 1.0f : 52.0f / (float)note);   /* выше - короче */
    if (t60 < 0.05f) t60 = 0.05f;
    k->g = powf(10.0f, -3.0f / (t60 * hz));
    k->level_mul = powf(10.0f, -3.0f / (t60 * (float)SYNTH_RATE));

    /* возбуждение: шум через фильтр (яркость растёт с силой удара) и гребёнка места щипка */
    float a = p->exc_lp * (0.6f + 0.4f * vel) * (muted ? 0.4f : 1.0f), lp = 0.0f, mean = 0.0f, mx = 0.0f;
    for (int i = 0; i < n; i++) { lp += a * (rnd() - lp); k->buf[i] = lp; }
    int pk = (int)(p->pick * (float)n);
    if (pk < 1) pk = 1;
    static float tmp[G_BUF];
    for (int i = 0; i < n; i++) tmp[i] = k->buf[i] - k->buf[(i - pk + n) % n];
    for (int i = 0; i < n; i++) mean += tmp[i];
    mean /= (float)n;
    for (int i = 0; i < n; i++) { k->buf[i] = tmp[i] - mean; if (fabsf(k->buf[i]) > mx) mx = fabsf(k->buf[i]); }
    float amp = (muted ? 0.35f : 1.0f) * (0.35f + 0.65f * vel) / (mx > 1e-6f ? mx : 1.0f);
    for (int i = 0; i < n; i++) k->buf[i] *= amp;
    k->level = 1.0f;
    k->env = 1.0f;
    k->damp = 1.0f;
    k->out_gain = p->gain;
    k->active = 1;
}

/* заглушить струны по маске (сменился аккорд - пальцы ушли со струн) */
void synth_guitar_damp(int mask) {
    for (int s = 0; s < N_GSTR_SYNTH; s++)
        if (mask & (1 << s))
            for (int j = 0; j < G_SLOTS; j++) if (gv[s][j].active) gv[s][j].damp = exp_mul(0.05f);
}

void synth_guitar(int s, int note, int delay, float vel) {
    if (delay <= 0 || npend >= MAX_PENDING) { guitar_pluck(s, note, vel); return; }
    pend[npend].kind = 1;
    pend[npend].gstr = s;
    pend[npend].note = note;
    pend[npend].vel = vel;
    pend[npend].at = clock_s + (uint32_t)delay;
    npend++;
}

static int g_active_strings = 0;

static float guitar_render_sample(void) {
    float mix = 0.0f;
    int act = 0;
    for (int s = 0; s < N_GSTR_SYNTH; s++) {
        float sum = 0.0f;
        int any = 0;
        for (int j = 0; j < G_SLOTS; j++) {
            KsVoice *k = &gv[s][j];
            if (!k->active) continue;
            any = 1;
            float x = k->buf[k->idx];
            float y = k->g * 0.5f * (x + k->last);       /* усреднение: верха гаснут быстрее */
            k->last = x;
            float z = k->ap_c * y + k->ap_x1 - k->ap_c * k->ap_y1;
            k->ap_x1 = y; k->ap_y1 = z;
            k->buf[k->idx] = z;
            if (++k->idx >= k->n) k->idx = 0;
            k->env *= k->damp;
            k->level *= k->level_mul;
            sum += x * k->env * k->out_gain;
            if (k->level * k->env < 3e-4f) k->active = 0;
        }
        if (!any) {                                      /* струна молчит - фильтры в ноль */
            geq[s][0].z1 = geq[s][0].z2 = geq[s][1].z1 = geq[s][1].z2 = 0.0f;
            continue;
        }
        act++;
        mix += bq_run(&geq[s][1], bq_run(&geq[s][0], sum));
    }
    g_active_strings = act;
    return mix * GUITAR_GAIN;
}

int synth_guitar_active(void) { return g_active_strings; }

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
                if (pend[p].kind == 1) guitar_pluck(pend[p].gstr, pend[p].note, pend[p].vel);
                else start_strum(pend[p].note);
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
            mix += tri * t->env * t->gain * (t->kind == V_PAD ? g_vol_pad : g_vol_strings) * VOL_SCALE;
        }
        mix += guitar_render_sample() * g_vol_strings * VOL_SCALE;
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
