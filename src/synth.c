#include "synth.h"
#include <math.h>
#include <string.h>

enum { V_FREE = 0, V_STRUM, V_PAD };
enum { ST_ATTACK, ST_DECAY, ST_SUSTAIN, ST_RELEASE };

typedef struct {
    int kind, stage, note;
    float phase, inc;            /* фаза 0..1 и шаг за сэмпл */
    float env, att_inc, dec_mul, rel_mul, gain;
    /* dreamy: FM-язычок (модулятор 1:1, индекс гаснет) + обертон вибрафона 4:1 */
    int dream;
    float mph, midx, midx_mul, bph, binc, bamp, bmul;
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
static float PAD_ATTACK_S = 0.064f;           /* меняется фейдером ATTACK (Pad only) */
/* параметры пэда с фейдеров Pad only: срез фильтра шины пэда (1 - открыт) и глубина вибрато */
static float g_pad_lpc = 1.0f, g_pad_vib = 0.0f, g_plp1 = 0.0f, g_plp2 = 0.0f;
void synth_pad_params(int attack, int cutoff, int mod) {
    PAD_ATTACK_S = 0.005f * powf(600.0f, (float)attack / 100.0f);         /* 5 мс .. 3 с (40 - как было) */
    float fc = 120.0f * powf(100.0f, (float)cutoff / 100.0f);             /* 120 Гц .. 12 кГц */
    g_pad_lpc = cutoff >= 100 ? 1.0f : 1.0f - expf(-2.0f * 3.14159265f * fc / (float)SYNTH_RATE);
    g_pad_vib = 0.012f * (float)mod / 100.0f;                              /* до ~20 центов */
}
static const float PAD_RELEASE_S = 0.180f;

/* громкость слоёв 0..1 (ручка на экране); 0.7 - уровень по умолчанию */
static float g_vol_strings = 0.7f, g_vol_pad = 0.7f;      /* текущие (плавно идут к целевым) */
static float g_vol_strings_t = 0.7f, g_vol_pad_t = 0.7f;  /* целевые: ручка/фейдер - без ступенек и треска */
static int g_palm = 0;                /* ладонь на струнах (задняя панель): звук короткий и глухой */
static int g_sus = 0;                 /* сустейн (задняя панель под левой рукой): звучит дольше, но гаснет */
#define SUS_STRUM_MUL 2.6f            /* во сколько раз дольше струны страммера */
#define SUS_PAD_MUL 5.0f              /* во сколько раз дольше отпускание пэда */
#define SUS_GTR_MUL 2.6f              /* во сколько раз дольше гитара */
#define GTR_RING 1.45f                /* общий сустейн гитары (автор просил дольше) */
#define GTR_OPEN_RING 1.3f            /* открытые струны звенят ещё дольше */
#define VOL_SCALE (1.0f / 0.7f)

static float note_hz(int note) { return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f); }
static float exp_mul(float tau_s) { return expf(-1.0f / (tau_s * (float)SYNTH_RATE)); }

static void guitar_init(void);
static float guitar_render_sample(void);
static void guitar_pluck(int s, int note, float vel, int fret);
void synth_guitar_damp_tau(int mask, float tau);

/* ---- dreamy-звук струн: калимба / вибрафон / электропиано ----
 * Дёшево: синус - таблицей; на голос две таблицы за сэмпл. Общие на все голоса: тремоло (как мотор
 * вибрафона), лёгкое вибрато и небольшой ревер (4 гребёнки + 2 всепропускающих, как Freeverb). */
#define DREAM_GAIN 0.19f                /* синусы складываются плотнее треугольников - тише */
#define SIN_N 1024
static float g_sin[SIN_N + 1];
static int g_dream = 0;
static float g_dlp = 0.0f;
static int g_bus_tail = 0;                           /* сэмплов до остановки шин эффектов */                           /* фильтр dreamy-шины */
static float g_lfo_t = 0.0f, g_lfo_v = 0.0f;          /* фазы тремоло и вибрато 0..1 */
static inline float sinlut(float x) {                 /* sin(2 pi x), x в пределах -1..2 (без floorf) */
    if (x < 0.0f) x += 1.0f;
    if (x >= 1.0f) x -= 1.0f;
    float f = x * (float)SIN_N;
    int i = (int)f;
    return g_sin[i] + (g_sin[i + 1] - g_sin[i]) * (f - (float)i);
}
#define DR_NC 4
#define DR_NA 2
static const int DR_CL[DR_NC] = { 1214, 1293, 1389, 1475 };   /* длины Freeverb, пересчитаны на 48 кГц */
static const int DR_AL[DR_NA] = { 605, 480 };
static float dr_c[DR_NC][1475], dr_a[DR_NA][605], dr_lp[DR_NC];
static int dr_ci[DR_NC], dr_ai[DR_NA];
#define DR_FB 0.82f                                  /* длина хвоста */
#define DR_DAMP 0.45f                                /* хвост тёмный */
#define DR_WET 0.34f
static float dream_reverb(float x) {
    float y = 0.0f;
    for (int k = 0; k < DR_NC; k++) {
        float o = dr_c[k][dr_ci[k]];
        dr_lp[k] = o + (dr_lp[k] - o) * DR_DAMP;
        dr_c[k][dr_ci[k]] = x + dr_lp[k] * DR_FB;
        if (++dr_ci[k] >= DR_CL[k]) dr_ci[k] = 0;
        y += o;
    }
    y *= 0.25f;
    for (int k = 0; k < DR_NA; k++) {
        float b = dr_a[k][dr_ai[k]], o = b - y;
        dr_a[k][dr_ai[k]] = y + b * 0.5f;
        if (++dr_ai[k] >= DR_AL[k]) dr_ai[k] = 0;
        y = o;
    }
    return y;
}

void synth_strum_sound(int sound) { g_dream = (sound >= 0 && sound <= 2) ? sound : 0; }

/* ---- звук 3 «Ancient»: по пресету Ableton Wavetable «Ancient Dream» (автору нравится) ----
 * Голос: две пилы (сдвиг +-6 центов, как unison) + саб-синус на октаву ниже. Шина: «голос» -
 * два полосовых фильтра на формантах «а» (как осциллятор Formant / Real Voice), НЧ ~4 кГц,
 * дилей 1/8 на 120 BPM, потом общий ревер. */
#define ANC_GAIN 0.085f
static inline float polyblep(float t, float dt) {     /* сглаживание скачка пилы (меньше алиасинга) */
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
static float a_bp1l, a_bp1b, a_bp2l, a_bp2b, a_lp;  /* состояния фильтров шины */
#define ADL_N 12000                                   /* 250 мс */
static float a_dl[ADL_N];
static int a_dli;
static float ancient_bus(float x) {
    /* форманты 800 и 1150 Гц (Chamberlin SVF, Q ~ 4) */
    const float f1 = 0.1045f, f2 = 0.1500f, q = 0.25f;
    float h1 = x - a_bp1l - q * a_bp1b; a_bp1b += f1 * h1; a_bp1l += f1 * a_bp1b;
    float h2 = x - a_bp2l - q * a_bp2b; a_bp2b += f2 * h2; a_bp2l += f2 * a_bp2b;
    float y = x * 0.7f + (a_bp1b + a_bp2b) * 0.45f;
    a_lp += (y - a_lp) * 0.41f;                        /* ~4 кГц */
    y = a_lp;
    float d = a_dl[a_dli];
    a_dl[a_dli] = y + d * 0.35f;
    if (++a_dli >= ADL_N) a_dli = 0;
    return y + d * 0.28f;
}

void synth_init(void) {
    memset(v, 0, sizeof(v));
    for (int i = 0; i <= SIN_N; i++) g_sin[i] = sinf(2.0f * 3.14159265f * (float)i / (float)SIN_N);
    g_bus_tail = 0; memset(a_dl, 0, sizeof(a_dl)); a_dli = 0; a_bp1l = a_bp1b = a_bp2l = a_bp2b = a_lp = 0.0f;
    memset(dr_c, 0, sizeof(dr_c)); memset(dr_a, 0, sizeof(dr_a)); memset(dr_lp, 0, sizeof(dr_lp));
    memset(dr_ci, 0, sizeof(dr_ci)); memset(dr_ai, 0, sizeof(dr_ai));
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
static float strum_tau(int note, int dream) {
    /* dreamy звенит дольше и мягче гаснет вверх по диапазону */
    float tau = dream == 2 ? 1.5f * powf(2.0f, -(float)(note - 60) / 30.0f)
              : dream ? 1.1f * powf(2.0f, -(float)(note - 60) / 28.0f) : 0.9f * powf(2.0f, -(float)(note - 60) / 24.0f);
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
    t->dream = g_dream;
    if (t->dream == 2) {                                 /* ancient: фазы двух пил и саба */
        if (t->env == 0.0f) { t->mph = 0.37f; t->bph = 0.0f; }
        t->binc = t->inc * 0.5f;
    }
    t->dec_mul = exp_mul(strum_tau(note, t->dream));
    t->gain = t->dream == 2 ? ANC_GAIN : (t->dream ? DREAM_GAIN : STRUM_GAIN);
    if (t->dream == 1) {
        float hz = note_hz(note);
        t->midx = 0.22f;                                   /* мягкий удар язычка (автор: «слишком много верхов») */
        t->midx_mul = exp_mul(0.12f);                      /* ... и сразу округляется */
        t->binc = 4.0f * t->inc;                           /* обертон вибрафона/калимбы */
        t->bamp = hz < 700.0f ? 0.10f : 0.04f;             /* обертон - лёгкий, у верхних почти нет */
        t->bmul = exp_mul(0.09f);
        if (t->kind == V_STRUM && t->env == 0.0f) { t->mph = 0.0f; t->bph = 0.0f; }
    }
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
        if (v[i].kind == V_STRUM && !g_palm) v[i].dec_mul = exp_mul(strum_tau(v[i].note, v[i].dream));
        if (v[i].kind == V_PAD) v[i].rel_mul = exp_mul(pad_release());
    }
}

void synth_volume(float strings, float pad) {
    g_vol_strings_t = strings < 0.0f ? 0.0f : (strings > 1.0f ? 1.0f : strings);
    g_vol_pad_t = pad < 0.0f ? 0.0f : (pad > 1.0f ? 1.0f : pad);
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
    float t60 = muted ? 0.05f : GTR_RING * p->t60 * (note < 52 ? 1.0f : sqrtf(52.0f / (float)note));   /* выше - короче */
    if (g_palm && !muted) t60 = 0.16f + 0.03f * (float)(N_GSTR_SYNTH - 1 - s);     /* palm mute: басы чуть дольше */
    else if (g_sus && !muted) t60 *= SUS_GTR_MUL;                                    /* сустейн */
    /* Лад: та же нота выше по грифу на толстой струне звучит иначе - струна короче и зажата
     * пальцем: меньше верхов, круглее (щипок ближе к середине звучащей части), короче звенит.
     * Открытая струна - самая звонкая и долгая. */
    if (fret < 0) fret = 0;
    if (fret > 15) fret = 15;
    float fr = (float)fret;
    if (!muted && fret > 0) t60 *= 0.9f - 0.014f * fr;
    if (!muted && fret == 0 && !g_palm) t60 *= GTR_OPEN_RING;
    if (t60 < 0.05f) t60 = 0.05f;
    /* открытая струна: меньше потерь в петле - богаче обертонами, "глубже" */
    float damp_c = p->loop_damp + (muted ? 0.45f : (g_palm ? 0.25f : 0.0f)) + 0.022f * fr - (fret == 0 && !muted ? 0.05f : 0.0f);
    if (damp_c < 0.02f) damp_c = 0.02f;
    if (damp_c > 0.7f) damp_c = 0.7f;
    line_tune(&k->a, hz, damp_c, t60);
    line_tune(&k->b, hz * powf(2.0f, p->beat_cents / 1200.0f), damp_c + 0.04f, t60 * 0.7f);
    k->mix_b = fret == 0 && !muted ? 0.5f : 0.42f;     /* открытая - сильнее вторая поляризация: живые биения */
    k->level_mul = powf(10.0f, -3.0f / (t60 * (float)SYNTH_RATE));

    /* возбуждение: форма оттянутой струны (вершина в месте щипка) + немного шума,
     * сглаживание - чем слабее удар, тем мягче; потом убрать постоянную составляющую */
    int n = k->a.n;
    float pick = p->pick * powf(2.0f, fr / 12.0f);         /* до бриджа то же, струна короче */
    if (pick > 0.5f) pick = 0.5f;
    int pk = (int)(pick * (float)n);
    if (pk < 1) pk = 1;
    float bright = p->bright * (0.55f + 0.45f * vel) * (muted ? 0.16f : (g_palm ? 0.55f : 1.0f))   /* глушёная - глухо */
                 * (1.0f / (1.0f + 0.07f * fr));
    float noise_amt = muted ? 0.06f : 0.22f + 0.18f * vel, lp = 0.0f, mean = 0.0f, mx = 0.0f;   /* у глушёной - без «щелчка» шума */
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
    float amp = (muted ? 0.16f : 1.0f) * (0.35f + 0.65f * vel) / (mx > 1e-6f ? mx : 1.0f);   /* глушёная - тихий стук */
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
        float mix = 0.0f, dmix = 0.0f, pmix = 0.0f, amix = 0.0f;
        g_vol_pad += (g_vol_pad_t - g_vol_pad) * 0.0021f;            /* ~10 мс */
        g_vol_strings += (g_vol_strings_t - g_vol_strings) * 0.0021f;
        /* общие LFO dreamy: тремоло 4.6 Гц, вибрато 5.3 Гц (+-2 цента) */
        g_lfo_t += 4.6f / (float)SYNTH_RATE; if (g_lfo_t >= 1.0f) g_lfo_t -= 1.0f;
        g_lfo_v += 5.3f / (float)SYNTH_RATE; if (g_lfo_v >= 1.0f) g_lfo_v -= 1.0f;
        float vib = 1.0f + 0.0012f * sinlut(g_lfo_v);
        float pvib = 1.0f + g_pad_vib * sinlut(g_lfo_v);
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
            if (t->dream == 2) {                           /* ancient: две пилы + саб, в свою шину */
                float i1 = t->inc * 1.0035f * vib, i2 = t->inc * 0.9965f * vib;
                float s1 = 2.0f * t->phase - 1.0f - polyblep(t->phase, i1);
                float s2 = 2.0f * t->mph - 1.0f - polyblep(t->mph, i2);
                float sub = sinlut(t->bph);
                t->phase += i1; if (t->phase >= 1.0f) t->phase -= 1.0f;
                t->mph += i2;   if (t->mph >= 1.0f) t->mph -= 1.0f;
                t->bph += t->binc * vib; if (t->bph >= 1.0f) t->bph -= 1.0f;
                amix += (s1 + s2 + 0.55f * sub) * t->env * t->gain;
                continue;
            }
            if (t->dream) {                                /* dreamy-струна - в свою шину (тремоло + ревер) */
                float m = t->midx != 0.0f ? sinlut(t->mph) * t->midx : 0.0f;   /* погасшее - не считаем */
                float smp = sinlut(t->phase + m) + (t->bamp != 0.0f ? sinlut(t->bph) * t->bamp : 0.0f);
                float di = t->inc * vib;
                t->phase += di; if (t->phase >= 1.0f) t->phase -= 1.0f;
                t->mph += di;   if (t->mph >= 1.0f) t->mph -= 1.0f;
                t->bph += t->binc * vib; if (t->bph >= 1.0f) t->bph -= 1.0f;
                t->midx *= t->midx_mul;                    /* гаснут - а крошечные обнуляем (денормалы медленные) */
                t->bamp *= t->bmul;
                if (t->midx < 1e-5f) { t->midx = 0.0f; t->midx_mul = 1.0f; }
                if (t->bamp < 1e-5f) { t->bamp = 0.0f; t->bmul = 1.0f; }
                dmix += smp * t->env * t->gain;
                continue;
            }
            /* треугольник: 1 -> -1 -> 1 за период */
            float tri = 4.0f * fabsf(t->phase - 0.5f) - 1.0f;
            if (t->kind == V_PAD) {                        /* пэд - в свою шину (фильтр с фейдера), вибрато */
                t->phase += t->inc * pvib;
                if (t->phase >= 1.0f) t->phase -= 1.0f;
                pmix += tri * t->env * t->gain;
                continue;
            }
            t->phase += t->inc;
            if (t->phase >= 1.0f) t->phase -= 1.0f;
            mix += tri * t->env * t->gain * g_vol_strings * VOL_SCALE;
        }
        /* шина пэда: два однополюсных НЧ (на 100% фильтр открыт - звук как был) */
        g_plp1 += (pmix - g_plp1) * g_pad_lpc;
        g_plp2 += (g_plp1 - g_plp2) * g_pad_lpc;
        mix += g_plp2 * g_vol_pad * VOL_SCALE;
        /* dreamy-шина: тремоло, потом ревер (ревер считается всегда - хвост доигрывает после выключения) */
        dmix *= (1.0f - 0.16f * (0.5f + 0.5f * sinlut(g_lfo_t))) * g_vol_strings * VOL_SCALE;
        g_dlp += (dmix - g_dlp) * 0.32f;                   /* мягкий НЧ-фильтр шины (~2.9 кГц): округло */
        dmix = g_dlp;
        /* шины dreamy / ancient и ревер считаются, только пока звучат (+5 с на хвост) - классика не платит */
        if (amix != 0.0f || dmix != 0.0f) g_bus_tail = 5 * SYNTH_RATE;
        if (g_bus_tail > 0) {
            g_bus_tail--;
            float an = ancient_bus(amix * g_vol_strings * VOL_SCALE + 1e-20f);   /* +1e-20: без денормалов */
            mix += dmix + an + DR_WET * dream_reverb(dmix * 0.6f + an * 0.5f + 1e-20f);
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
