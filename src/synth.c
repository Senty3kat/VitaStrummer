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
typedef struct { int kind, note, gstr, fret; float vel; uint32_t at; } Pending;

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
static int g_palm = 0;                /* ладонь на струнах (задняя панель): звук короткий и глухой */
static int g_sus = 0;                 /* сустейн (задняя панель под левой рукой): звучит дольше, но гаснет */
#define SUS_STRUM_MUL 2.6f            /* во сколько раз дольше струны страммера */
#define SUS_PAD_MUL 5.0f              /* во сколько раз дольше отпускание пэда */
#define SUS_GTR_MUL 1.9f              /* во сколько раз дольше гитара */
#define VOL_SCALE (1.0f / 0.7f)

static float note_hz(int note) { return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f); }
static float exp_mul(float tau_s) { return expf(-1.0f / (tau_s * (float)SYNTH_RATE)); }

static void guitar_init(void);
static float guitar_render_sample(void);
static void guitar_pluck(int s, int note, float vel, int fret);
void synth_guitar_damp_tau(int mask, float tau);

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

/* затухание струны страммера: нижние звучат дольше верхних; ладонь - короткий щипок, сустейн - дольше */
static float strum_tau(int note) {
    float tau = 0.9f * powf(2.0f, -(float)(note - 60) / 24.0f);
    if (g_palm) return 0.09f;
    return g_sus ? tau * SUS_STRUM_MUL : tau;
}
static float pad_release(void) { return (g_sus ? PAD_RELEASE_S * SUS_PAD_MUL : PAD_RELEASE_S) / 4.0f; }

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
    t->dec_mul = exp_mul(strum_tau(note));
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
        t->rel_mul = exp_mul(pad_release());
        t->gain = PAD_GAIN;
    }
}

void synth_palm(int on) {
    on = on ? 1 : 0;
    if (on && !g_palm) {                       /* ладонь легла на струны - звенящее быстро гаснет */
        for (int i = 0; i < SYNTH_VOICES; i++)
            if (v[i].kind == V_STRUM) v[i].dec_mul = exp_mul(0.06f);
        synth_guitar_damp_tau(0x3F, 0.07f);
    }
    g_palm = on;
}

/* сустейн: звучащие ноты страммера и пэда сразу переходят на новое затухание (без щелчков -
 * меняется только скорость спада); гитара - для следующих щипков, и аккорд не глушит струны */
void synth_sustain(int on) {
    on = on ? 1 : 0;
    if (on == g_sus) return;
    g_sus = on;
    for (int i = 0; i < SYNTH_VOICES; i++) {
        if (v[i].kind == V_STRUM && !g_palm) v[i].dec_mul = exp_mul(strum_tau(v[i].note));
        if (v[i].kind == V_PAD) v[i].rel_mul = exp_mul(pad_release());
    }
}

void synth_volume(float strings, float pad) {
    g_vol_strings = strings < 0.0f ? 0.0f : (strings > 1.0f ? 1.0f : strings);
    g_vol_pad = pad < 0.0f ? 0.0f : (pad > 1.0f ? 1.0f : pad);
}

/* ---------- гитара ----------
 * Каждая из 6 струн - щипковая модель (Карплус-Стронг) с двумя "поляризациями": две линии задержки
 * с чуть разной высотой и затуханием - их медленные биения дают живой, "дышащий" звук.
 * В петле - мягкий фильтр (у толстых сильнее): верха гаснут быстрее низов, звук тёплый, без звона.
 * Щипок - не голый шум, а форма оттянутой струны (треугольник с вершиной в месте щипка) плюс немного
 * шума: так получается круглая атака. На струне один звук: новый щипок заменяет старый (без щелчка).
 * У каждой струны свой "эквалайзер" и характер - толщина, оплётка, место щипка, длительность,
 * корпусный резонанс, срез верхов, расстройка. Поэтому одна нота на разных струнах звучит по-разному.
 * Общий выход струн проходит через "корпус" (три резонанса деки и воздуха) и небольшую комнату. */
#define G_BUF 2600                       /* период E0 (сдвиг на -2 октавы) ~2330 сэмплов */
#define G_SLOTS 2

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } Biquad;

typedef struct {                         /* одна линия (поляризация) */
    float buf[G_BUF];
    int n, idx;
    float lp, damp_c;                    /* фильтр в петле */
    float ap_c, ap_x1, ap_y1;            /* дробная задержка (фазовращатель) */
    float g;                             /* потери за проход */
} KsLine;

typedef struct {
    KsLine a, b;
    float mix_b;                         /* доля второй поляризации */
    int active;
    float level, level_mul;              /* оценка громкости - чтобы вовремя выключить */
    float env, damp;                     /* огибающая гашения: 1 - звучит, <1 - гасится */
    float out_gain;
} KsVoice;

typedef struct {
    float t60, bright, pick, body_f, body_db, body_q, lp_f, cents, gain, loop_damp, beat_cents;
} GStrParam;

/*                       t60  щипок место корпус Гц дБ   Q    срез Гц центы громк. петля биения */
static const GStrParam GP[N_GSTR_SYNTH] = {
    /* 0 E2 - толстая, оплётка */ { 5.0f, 0.40f, 0.13f,  110.0f, 4.0f, 0.9f, 2800.0f, -1.6f, 1.00f, 0.30f, 1.1f },
    /* 1 A2 - оплётка          */ { 4.5f, 0.44f, 0.14f,  165.0f, 3.5f, 0.9f, 3300.0f,  1.1f, 0.96f, 0.27f, 0.9f },
    /* 2 D3 - оплётка          */ { 4.0f, 0.50f, 0.15f,  230.0f, 3.0f, 1.0f, 3900.0f, -0.8f, 0.92f, 0.24f, 1.2f },
    /* 3 G3 - оплётка, тонкая  */ { 3.6f, 0.56f, 0.16f,  380.0f, 2.5f, 1.0f, 4700.0f,  1.3f, 0.88f, 0.20f, 0.8f },
    /* 4 B3 - гладкая          */ { 3.2f, 0.68f, 0.18f, 1400.0f, 1.5f, 1.1f, 6200.0f, -1.0f, 0.82f, 0.13f, 1.0f },
    /* 5 E4 - гладкая, тонкая  */ { 2.8f, 0.76f, 0.20f, 2200.0f, 1.5f, 1.2f, 7500.0f,  0.7f, 0.76f, 0.10f, 1.3f },
};

static KsVoice gv[N_GSTR_SYNTH][G_SLOTS];
static int gcur[N_GSTR_SYNTH];
static Biquad geq[N_GSTR_SYNTH][2];
static uint32_t g_rng = 0x12345678u;
static const float GUITAR_GAIN = 0.23f;

/* корпус: резонанс воздуха (~100 Гц), деки (~200 Гц) и верхней деки (~400 Гц) */
static Biquad gbody[3];
static const float BODY_GAIN[3] = { 0.55f, 0.40f, 0.22f };

/* маленькая комната: 4 гребёнки с затуханием верхов и 2 фазовращателя (схема Шрёдера) */
#define RV_C 4
#define RV_A 2
static const int RV_CLEN[RV_C] = { 1693, 1759, 1621, 1547 };
static const int RV_ALEN[RV_A] = { 245, 605 };
static float rv_cbuf[RV_C][1800], rv_abuf[RV_A][620];
static int rv_ci[RV_C], rv_ai[RV_A];
static float rv_clp[RV_C];
static const float RV_FB = 0.80f, RV_DAMP = 0.35f, RV_WET = 0.16f;

static float rnd(void) {                 /* шум -1..1 */
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)(int32_t)g_rng * (1.0f / 2147483648.0f);
}

/* формулы RBJ: пиковый фильтр, ФНЧ и полосовой 2-го порядка */
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
static void bq_bandpass(Biquad *q, float f, float Q) {           /* пик = 1 на частоте f */
    float w = 2.0f * 3.14159265f * f / (float)SYNTH_RATE, al = sinf(w) / (2.0f * Q), c = cosf(w), a0 = 1.0f + al;
    q->b0 = al / a0; q->b1 = 0.0f; q->b2 = -al / a0;
    q->a1 = -2.0f * c / a0; q->a2 = (1.0f - al) / a0;
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
    bq_bandpass(&gbody[0], 98.0f, 5.0f);
    bq_bandpass(&gbody[1], 196.0f, 7.0f);
    bq_bandpass(&gbody[2], 410.0f, 4.0f);
    memset(rv_cbuf, 0, sizeof(rv_cbuf));
    memset(rv_abuf, 0, sizeof(rv_abuf));
    memset(rv_ci, 0, sizeof(rv_ci));
    memset(rv_ai, 0, sizeof(rv_ai));
    memset(rv_clp, 0, sizeof(rv_clp));
}

/* настроить линию на частоту hz: длина + задержка фильтра в петле + дробная часть фазовращателем */
static void line_tune(KsLine *L, float hz, float damp_c, float t60) {
    float w = 2.0f * 3.14159265f * hz / (float)SYNTH_RATE;
    float lp_delay = atan2f(damp_c * sinf(w), 1.0f - damp_c * cosf(w)) / w;   /* фазовая задержка фильтра */
    float P = (float)SYNTH_RATE / hz - lp_delay;
    int n = (int)(P - 0.6f);
    if (n > G_BUF - 1) n = G_BUF - 1;
    if (n < 8) n = 8;
    float d = P - (float)n;
    if (d < 0.05f) d = 0.05f;
    L->n = n; L->idx = 0; L->lp = 0.0f; L->damp_c = damp_c;
    L->ap_c = (1.0f - d) / (1.0f + d); L->ap_x1 = L->ap_y1 = 0.0f;
    /* потери за проход: на основной частоте фильтр тоже чуть гасит - учитываем */
    float lp_gain = (1.0f - damp_c) / sqrtf(1.0f - 2.0f * damp_c * cosf(w) + damp_c * damp_c);
    float g = powf(10.0f, -3.0f / (t60 * hz)) / lp_gain;
    L->g = g > 0.9999f ? 0.9999f : g;
}

/* щипок струны s: note < 0 - заглушенная струна (короткий глухой стук открытой нотой) */
static void guitar_pluck(int s, int note, float vel, int fret) {
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
    float t60 = muted ? 0.05f : p->t60 * (note < 52 ? 1.0f : 52.0f / (float)note);   /* выше - короче */
    if (g_palm && !muted) t60 = 0.16f + 0.03f * (float)(N_GSTR_SYNTH - 1 - s);     /* palm mute: басы чуть дольше */
    else if (g_sus && !muted) t60 *= SUS_GTR_MUL;                                    /* сустейн */
    /* Лад: та же нота выше по грифу на толстой струне звучит иначе - струна короче и зажата
     * пальцем: меньше верхов, круглее (щипок ближе к середине звучащей части), короче звенит.
     * Открытая струна - самая звонкая и долгая. */
    if (fret < 0) fret = 0;
    if (fret > 15) fret = 15;
    float fr = (float)fret;
    if (!muted && fret > 0) t60 *= 0.82f - 0.022f * fr;
    if (t60 < 0.05f) t60 = 0.05f;
    float damp_c = p->loop_damp + (muted || g_palm ? 0.25f : 0.0f) + 0.022f * fr;
    if (damp_c > 0.7f) damp_c = 0.7f;
    line_tune(&k->a, hz, damp_c, t60);
    line_tune(&k->b, hz * powf(2.0f, p->beat_cents / 1200.0f), damp_c + 0.04f, t60 * 0.7f);
    k->mix_b = 0.42f;
    k->level_mul = powf(10.0f, -3.0f / (t60 * (float)SYNTH_RATE));

    /* возбуждение: форма оттянутой струны (вершина в месте щипка) + немного шума,
     * сглаживание - чем слабее удар, тем мягче; потом убрать постоянную составляющую */
    int n = k->a.n;
    float pick = p->pick * powf(2.0f, fr / 12.0f);         /* до бриджа то же, струна короче */
    if (pick > 0.5f) pick = 0.5f;
    int pk = (int)(pick * (float)n);
    if (pk < 1) pk = 1;
    float bright = p->bright * (0.55f + 0.45f * vel) * (muted ? 0.4f : (g_palm ? 0.55f : 1.0f))
                 * (1.0f / (1.0f + 0.07f * fr));
    float noise_amt = 0.22f + 0.18f * vel, lp = 0.0f, mean = 0.0f, mx = 0.0f;
    static float exc[G_BUF];
    for (int i = 0; i < n; i++) {
        float shape = i < pk ? (float)i / (float)pk : (float)(n - i) / (float)(n - pk);
        float x = shape + noise_amt * rnd();
        lp += bright * (x - lp);
        exc[i] = lp;
    }
    for (int pass = 0; pass < 2; pass++)                   /* сгладить шов в начале (lp разгонялся) */
        for (int i = 0; i < n; i++) { lp += bright * (exc[i] - lp); exc[i] = lp; }
    for (int i = 0; i < n; i++) mean += exc[i];
    mean /= (float)n;
    for (int i = 0; i < n; i++) { exc[i] -= mean; if (fabsf(exc[i]) > mx) mx = fabsf(exc[i]); }
    float amp = (muted ? 0.35f : 1.0f) * (0.35f + 0.65f * vel) / (mx > 1e-6f ? mx : 1.0f);
    for (int i = 0; i < n; i++) k->a.buf[i] = exc[i] * amp;
    int nb = k->b.n;                                       /* вторая поляризация: то же, растянутое */
    for (int i = 0; i < nb; i++) k->b.buf[i] = exc[(int)((float)i * (float)n / (float)nb)] * amp;
    k->level = 1.0f;
    k->env = 1.0f;
    k->damp = 1.0f;
    k->out_gain = p->gain;
    k->active = 1;
}

void synth_guitar_damp_tau(int mask, float tau) {
    for (int s = 0; s < N_GSTR_SYNTH; s++)
        if (mask & (1 << s))
            for (int j = 0; j < G_SLOTS; j++) if (gv[s][j].active) gv[s][j].damp = exp_mul(tau);
}

void synth_guitar_damp(int mask) { synth_guitar_damp_tau(mask, 0.05f); }

void synth_guitar(int s, int note, int fret, int delay, float vel) {
    if (delay <= 0 || npend >= MAX_PENDING) { guitar_pluck(s, note, vel, fret); return; }
    pend[npend].fret = fret;
    pend[npend].kind = 1;
    pend[npend].gstr = s;
    pend[npend].note = note;
    pend[npend].vel = vel;
    pend[npend].at = clock_s + (uint32_t)delay;
    npend++;
}

static int g_active_strings = 0;
static int g_tail = 0;                   /* сколько ещё сэмплов досчитывать корпус и комнату */

static inline float line_step(KsLine *L) {
    float x = L->buf[L->idx];
    L->lp += (1.0f - L->damp_c) * (x - L->lp);             /* фильтр в петле */
    float y = L->g * L->lp;
    float z = L->ap_c * y + L->ap_x1 - L->ap_c * L->ap_y1; /* дробная задержка */
    L->ap_x1 = y; L->ap_y1 = z;
    L->buf[L->idx] = z;
    if (++L->idx >= L->n) L->idx = 0;
    return x;
}

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
            float x = line_step(&k->a) + k->mix_b * line_step(&k->b);
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
    if (act) g_tail = SYNTH_RATE * 2;                     /* хвост комнаты после последней струны */
    else if (g_tail > 0) g_tail--;
    else return 0.0f;

    /* корпус */
    float body = 0.85f * mix;
    for (int i = 0; i < 3; i++) body += BODY_GAIN[i] * bq_run(&gbody[i], mix);

    /* комната */
    float rin = body * 0.25f, wet = 0.0f;
    for (int i = 0; i < RV_C; i++) {
        float *b = rv_cbuf[i];
        float y = b[rv_ci[i]];
        rv_clp[i] += (1.0f - RV_DAMP) * (y - rv_clp[i]);
        b[rv_ci[i]] = rin + RV_FB * rv_clp[i];
        if (++rv_ci[i] >= RV_CLEN[i]) rv_ci[i] = 0;
        wet += y;
    }
    for (int i = 0; i < RV_A; i++) {
        float *b = rv_abuf[i];
        float d = b[rv_ai[i]], y = d - 0.5f * wet;
        b[rv_ai[i]] = wet + 0.5f * y;
        if (++rv_ai[i] >= RV_ALEN[i]) rv_ai[i] = 0;
        wet = y;
    }
    return (body + RV_WET * wet) * GUITAR_GAIN;
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
                if (pend[p].kind == 1) guitar_pluck(pend[p].gstr, pend[p].note, pend[p].vel, pend[p].fret);
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
