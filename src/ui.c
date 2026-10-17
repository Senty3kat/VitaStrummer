/* Экран в стиле концептов: кремовый корпус, объёмные кнопки, синие панели.
 *
 * Всё объёмное рисуется процессором один раз при запуске:
 *   - фон целиком (корпус, капсула, табло, колесо, струнная панель со струнами);
 *   - "горящий" (оранжевый) вариант струнной панели для подсветки и вспышек струн;
 *   - спрайты каждой кнопки колеса во всех состояниях, светодиоды.
 * В кадре GPU только накладывает готовые текстуры (и куски "горящей" панели для
 * струн), поэтому смена аккорда не стоит ни миллисекунды процессора. */
#include "ui.h"
#include "canvas.h"
#include "layout.h"
#include "strum.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vita2d.h>

/* ---------- палитра (снята с концепта Sonara) ---------- */
#define C_RIM        RGBA8(60, 86, 110, 255)
#define C_BODY_TOP   RGBA8(236, 225, 204, 255)
#define C_BODY_BOT   RGBA8(224, 210, 186, 255)
#define C_CREAM_HI   RGBA8(243, 234, 216, 255)
#define C_CREAM      RGBA8(229, 215, 191, 255)
#define C_CREAM_LO   RGBA8(214, 198, 172, 255)
#define C_CREAM_EDGE RGBA8(170, 152, 126, 255)
#define C_BLUE_TOP   RGBA8(96, 126, 150, 255)
#define C_BLUE_BOT   RGBA8(72, 101, 125, 255)
#define C_BLUE2_TOP  RGBA8(70, 98, 121, 255)
#define C_BLUE2_BOT  RGBA8(54, 80, 102, 255)
#define C_PLATE_TOP  RGBA8(72, 100, 124, 255)
#define C_PLATE_BOT  RGBA8(62, 88, 111, 255)
#define C_OR_TOP     RGBA8(242, 170, 108, 255)
#define C_OR_BOT     RGBA8(214, 124, 60, 255)
#define C_OR_EDGE    RGBA8(166, 86, 34, 255)
#define C_PEACH_TOP  RGBA8(246, 220, 190, 255)
#define C_PEACH_BOT  RGBA8(234, 192, 150, 255)
#define C_PEACH_EDGE RGBA8(186, 146, 106, 255)
#define C_SHADOW     RGBA8(16, 26, 38, 120)
#define C_NAVY       RGBA8(24, 54, 78, 255)
#define C_NAVY_SOFT  RGBA8(80, 96, 112, 255)
#define C_LCD_TEXT   RGBA8(246, 236, 214, 255)
#define C_LCD_DIM    RGBA8(150, 172, 190, 255)
#define C_DEBUG      RGBA8(150, 132, 110, 255)
#define C_ORANGE     RGBA8(222, 122, 50, 255)
#define WHITE_A(a)   RGBA8(255, 255, 255, (a))
#define DARK_A(a)    RGBA8(20, 28, 40, (a))

#define PI_F 3.14159265f

typedef struct { uint32_t edge, top, bot; } Skin;
static const Skin SK_CREAM = { C_CREAM_EDGE, C_CREAM_HI, C_CREAM_LO };
static const Skin SK_OR = { C_OR_EDGE, C_OR_TOP, C_OR_BOT };
static const Skin SK_PEACH = { C_PEACH_EDGE, C_PEACH_TOP, C_PEACH_BOT };

/* ---------- текстуры ---------- */
typedef struct { Canvas cv; vita2d_texture *tex; float x, y; } Sprite;

enum { PL_NORMAL = 0, PL_LIT, PL_N };
enum { ST_NORMAL = 0, ST_SEL, ST_ARMED, ST_N };

static Canvas g_cv_bg;
static vita2d_texture *g_tex_bg = NULL;
static Canvas g_cv_plate[PL_N];                 /* PL_NORMAL не нужен отдельно - он уже в фоне */
static vita2d_texture *g_tex_plate[PL_N];
/* "жидкая" подсветка рисуется в полном разрешении в свою текстуру поверх фона.
 * Три текстуры по кругу - процессор пишет в ту, которую GPU уже не читает. */
#define N_FLUID_TEX 3
static vita2d_texture *g_tex_fluid[N_FLUID_TEX];
static int g_fluid_next = 0;
static unsigned char g_tex_lit[N_FLUID_TEX][N_STRINGS];   /* какие струны записаны в текстуру */

/* Мягкая подсветка клавиш аккорда (Strum / Pad): две текстуры по очереди, процессор пишет их
 * только при смене аккорда - пиксели клавиш тех струн, чья нота входит в аккорд */
static vita2d_texture *g_tex_khl[2];
static int g_khl_strings[2];     /* битовая маска струн, записанных в текстуру */
static int g_khl_cur = -1;       /* какую показываем (-1 - ничего) */
static int g_khl_want, g_khl_have = -1;   /* ключ состояния: маска нот | тоника | вкл */
/* пиксели каждой струны (клавиша + нить + полоса): индекс в панели и покрытие 0..255 */
/* пиксель струны: координаты в панели, покрытие (сглаживание края), клетка поля и доля до следующей
 * (считаются один раз при запуске - в кадре только целочисленная арифметика) */
typedef struct { uint16_t x, y, cell; uint8_t cov, frac; } StrPix;
/* поле "жидкости" считается по клеткам вдоль x (путь струны идёт слева направо) */
#define FL_X0 ((float)BK_X0 - 4.0f)
#define FL_X1 ((float)BAR_X1 + 4.0f)
#define FL_CELL 4.0f
#define FL_NC ((BAR_X1 - BK_X0 + 8) / 4 + 2)       /* клеток по 4 пикселя */

static StrPix *g_spix[N_STRINGS];
static int g_spix_n[N_STRINGS];
static Sprite g_root_spr[N_ROOTS][2], g_mod_spr[N_MODS][ST_N], g_qual_spr[2][ST_N], g_led[2];
static Sprite g_knob, g_dot, g_qs_on;            /* ручка громкости, точка аппликатуры, выбранный быстрый аккорд */
static Sprite g_set_panel, g_chk[2];              /* страница настроек: панель, галочка выкл/вкл */
/* гитарный гриф: 0 - обычный, 1 - струны приглушены (для заглушенных струн аккорда) */
static Canvas g_cv_gplate[2];
static vita2d_texture *g_tex_gplate[2];
static StrPix *g_gpix[N_GSTR];
static int g_gpix_n[N_GSTR];
static GuitarChord g_gch;                        /* аппликатура текущего аккорда (в режиме гитары) */
static vita2d_pgf *g_pgf = NULL;

static AppState g_st;
static StringMap g_map;
static int g_held_kind[MAX_FINGERS], g_held_idx[MAX_FINGERS], g_held_n = 0;

/* ---------- геометрия колеса ---------- */
static float seg_angle(int i, int n) { return -PI_F / 2.0f + 2.0f * PI_F * (float)i / (float)n; }

static SdfSector make_sector(int i, int n, float r0, float r1, float round) {
    SdfSector s = { WHEEL_CX, WHEEL_CY, r0, r1, seg_angle(i, n), PI_F / (float)n, SEG_GAP, round };
    return s;
}

static Box sector_box(const SdfSector *s) {
    Box b = { 1e9f, 1e9f, -1e9f, -1e9f };
    for (int k = 0; k <= 16; k++) {
        float a = s->a - s->ha + 2.0f * s->ha * (float)k / 16.0f;
        for (int j = 0; j < 2; j++) {
            float r = j ? s->r1 : s->r0;
            float x = s->cx + r * cosf(a), y = s->cy + r * sinf(a);
            if (x < b.x0) b.x0 = x;
            if (y < b.y0) b.y0 = y;
            if (x > b.x1) b.x1 = x;
            if (y > b.y1) b.y1 = y;
        }
    }
    return b;
}

static void seg_center(int i, int n, float r, float *x, float *y) {
    float a = seg_angle(i, n);
    *x = WHEEL_CX + r * cosf(a);
    *y = WHEEL_CY + r * sinf(a);
}

/* ---------- текстуры из холстов ---------- */
static void upload(vita2d_texture *tex, const Canvas *c) {
    if (!tex) return;
    uint32_t *dst = (uint32_t *)vita2d_texture_get_datap(tex);
    int stride = (int)vita2d_texture_get_stride(tex) / 4;
    for (int y = 0; y < c->h; y++) memcpy(dst + (size_t)y * stride, c->px + (size_t)y * c->w, (size_t)c->w * 4);
}

static vita2d_texture *tex_from(const Canvas *c) {
    vita2d_texture *t = vita2d_create_empty_texture((unsigned)c->w, (unsigned)c->h);
    upload(t, c);
    return t;
}

static int canvas_alloc(Canvas *c, int w, int h) {
    c->w = w;
    c->h = h;
    c->px = (uint32_t *)calloc((size_t)w * h, 4);
    return c->px != NULL;
}

/* спрайт под габарит bb (в экранных координатах) с полями */
static int sprite_alloc(Sprite *s, Box bb, int margin) {
    s->x = floorf(bb.x0) - (float)margin;
    s->y = floorf(bb.y0) - (float)margin;
    int w = (int)ceilf(bb.x1 - s->x) + margin, h = (int)ceilf(bb.y1 - s->y) + margin;
    return canvas_alloc(&s->cv, w, h);
}

static Box box_shift(Box b, float dx, float dy) { Box r = { b.x0 + dx, b.y0 + dy, b.x1 + dx, b.y1 + dy }; return r; }

/* ---------- объёмная деталь: тень, кромка, тело, блик ---------- */
static void bevel(Canvas *c, SdfFn f, const void *ctx, Box bb, const Skin *s, float shadow) {
    if (shadow > 0.0f) cv_sdf_fill(c, f, ctx, bb, 1.0f, 0.0f, shadow, shadow * 2.0f, C_SHADOW, C_SHADOW);
    cv_sdf_fill(c, f, ctx, bb, 0.0f, 0.0f, 0.0f, 0.0f, s->edge, s->edge);
    cv_sdf_fill(c, f, ctx, bb, -1.6f, 0.0f, -0.6f, 0.0f, s->top, s->bot);
    cv_sdf_inner_shadow(c, f, ctx, bb, -3.0f, 5.0f, RGBA8(110, 70, 30, 34));     /* мягкая тень у нижнего края */
    cv_sdf_fill(c, f, ctx, bb, -4.5f, 0.0f, -2.0f, 4.0f, WHITE_A(80), WHITE_A(0)); /* блик сверху */
}

/* утопленная деталь: светлая кромка снизу, заливка, внутренняя тень сверху */
static void engrave(Canvas *c, SdfFn f, const void *ctx, Box bb, uint32_t top, uint32_t bot, int dark) {
    cv_sdf_fill(c, f, ctx, bb, 1.0f, 0.0f, 1.2f, 0.0f, WHITE_A(dark ? 90 : 150), WHITE_A(dark ? 90 : 150));
    cv_sdf_fill(c, f, ctx, bb, 0.0f, 0.0f, 0.0f, 0.0f, top, bot);
    cv_sdf_inner_shadow(c, f, ctx, bb, dark ? 5.0f : 3.0f, dark ? 10.0f : 6.0f, DARK_A(dark ? 130 : 70));
}

/* ---------- струнная панель ---------- */
/* Всё на панели одного ровного цвета, как в первом концепте. Рисуется в два прохода:
 * сначала тени всех струн, потом сами струны - так тень одной клавиши не ложится на соседнюю. */
typedef struct { uint32_t col; } PlateSkin;

/* одна струна: клавиша -> сужение в нить -> S-переход -> плавное расширение -> полоса.
 * dy - сдвиг (для тени), col - цвет */
static void paint_string(Canvas *c, float ox, float oy, int i, float dy, uint32_t col) {
    float yc = key_center_y(i) - oy + dy, ys = strum_string_y(i) - oy + dy;
    if (key_is_black(i)) {
        cv_rrect(c, BK_X0 - ox, yc - BK_H / 2.0f, BK_X1 - BK_X0 + 3, BK_H, 2.5f, col);
        cv_ribbon(c, BK_X1 - ox, yc, BK_H, BK_NECK - ox, yc, LINE_T, col);
        cv_ribbon(c, BK_NECK - 1 - ox, yc, LINE_T, NECK_X1 + 1 - ox, yc, LINE_T, col);
    } else {
        float y0, y1, b0, b1;
        white_key_extents(i, &y0, &y1, &b0, &b1);
        cv_rrect(c, WK_X0 - ox, b0 - oy + dy, WK_XF - WK_X0 + 4, b1 - b0, 2.5f, col);    /* задняя часть */
        cv_rrect(c, WK_XF - ox, y0 - oy + dy, WK_X1 - WK_XF + 3, y1 - y0, 2.5f, col);     /* передняя */
        cv_ribbon(c, WK_X1 - ox, yc, y1 - y0, NECK_X1 - ox, yc, LINE_T, col);
    }
    cv_strand(c, NECK_X1 - 1 - ox, yc, LINE_T, BULB_X1 - ox, ys, BAR_H, BULB_TSTART, BAR_X1 - ox, col);
}

static void paint_string_shadows(Canvas *c, float ox, float oy) {
    for (int i = 0; i < N_STRINGS; i++) paint_string(c, ox, oy, i, 1.6f, DARK_A(45));
}

static void paint_strings(Canvas *c, float ox, float oy, const PlateSkin *k) {
    paint_string_shadows(c, ox, oy);
    for (int i = 0; i < N_STRINGS; i++) paint_string(c, ox, oy, i, 0.0f, k->col);
}

static const PlateSkin PS_NORMAL = { RGBA8(233, 221, 198, 255) };
static const PlateSkin PS_LIT = { RGBA8(228, 132, 64, 255) };
/* прямоугольник полосы струны (для подсветки куском "горящей" панели), экранные координаты */
static void bar_rect(int i, float r[4]) {
    float ys = strum_string_y(i);
    r[0] = BULB_X1 - 34; r[1] = ys - BAR_H / 2.0f - 1.0f; r[2] = BAR_X1 + 2 - r[0]; r[3] = BAR_H + 3;
}

/* центр "кнопки" быстрого аккорда k: 0..3 - D-pad (вверх, вправо, вниз, влево), 4..7 - стик */
static void quick_pos(int k, float *x, float *y) {
    static const int DX[4] = { 0, 1, 0, -1 }, DY[4] = { -1, 0, 1, 0 };
    float cx = k < 4 ? QS_DPAD_CX : QS_STICK_CX, cy = k < 4 ? QS_DPAD_CY : QS_STICK_CY;
    *x = cx + DX[k & 3] * QS_DX;
    *y = cy + DY[k & 3] * QS_DY;
}

/* ---------- фон ---------- */
static SdfCarved plate_shape(void) {
    SdfCarved s = { { PLATE_X, PLATE_Y, PLATE_W, PLATE_H, 26 }, WHEEL_CX, WHEEL_CY, PLATE_CARVE_R };
    return s;
}

static void paint_bg(Canvas *c) {
    cv_clear(c, C_RIM);

    /* корпус */
    SdfRRect body = { BODY_X, BODY_Y, BODY_W, BODY_H, BODY_R };
    Box bb_body = { BODY_X, BODY_Y, BODY_X + BODY_W, BODY_Y + BODY_H };
    cv_sdf_fill(c, sdf_rrect, &body, bb_body, 0, 0, 0, 0, RGBA8(40, 62, 84, 255), RGBA8(40, 62, 84, 255));
    cv_sdf_fill(c, sdf_rrect, &body, bb_body, -1.5f, 0, 0, 0, C_BODY_TOP, C_BODY_BOT);
    cv_noise(c, BODY_X, BODY_Y, BODY_W, BODY_H, 2);
    cv_sdf_inner_shadow(c, sdf_rrect, &body, bb_body, 3.0f, 3.0f, WHITE_A(120));     /* светлая фаска сверху */
    cv_sdf_inner_shadow(c, sdf_rrect, &body, bb_body, -3.0f, 4.0f, DARK_A(40));      /* тень снизу */

    /* логотип: звезда */
    cv_star(c, 34, 33, 15, 4.0f, C_ORANGE);

    /* капсула MODE | OCTAVE */
    SdfRRect cap = { CAP_X, CAP_Y, CAP_W, CAP_H, CAP_H / 2.0f };
    Box bb_cap = { CAP_X, CAP_Y, CAP_X + CAP_W, CAP_Y + CAP_H };
    engrave(c, sdf_rrect, &cap, bb_cap, RGBA8(226, 212, 188, 255), RGBA8(232, 220, 198, 255), 0);
    cv_rect(c, 300, 22, 124, 1, RGBA8(120, 110, 96, 200));                  /* линия после MODE */
    cv_rect(c, 500, 22, OCT_X + OCT_W - 500, 1, RGBA8(120, 110, 96, 200));  /* линия после OCTAVE */
    cv_rect(c, 572, 22, TRN_X + TRN_W - 572, 1, RGBA8(120, 110, 96, 200));  /* линия после TRANS */
    cv_rect(c, 438, 18, 1, 32, RGBA8(150, 132, 108, 200));                  /* разделитель */
    cv_rect(c, 439, 18, 1, 32, WHITE_A(160));
    SdfRRect oct = { OCT_X, OCT_Y, OCT_W, OCT_H, OCT_H / 2.0f };
    Box bb_oct = { OCT_X, OCT_Y, OCT_X + OCT_W, OCT_Y + OCT_H };
    bevel(c, sdf_rrect, &oct, bb_oct, &SK_CREAM, 2.0f);
    SdfRRect trn = { TRN_X, OCT_Y, TRN_W, OCT_H, OCT_H / 2.0f };
    Box bb_trn = { TRN_X, OCT_Y, TRN_X + TRN_W, OCT_Y + OCT_H };
    bevel(c, sdf_rrect, &trn, bb_trn, &SK_CREAM, 2.0f);

    /* табло аккорда */
    SdfRRect lcd = { LCD_X, LCD_Y, LCD_W, LCD_H, LCD_H / 2.0f };
    Box bb_lcd = { LCD_X, LCD_Y, LCD_X + LCD_W, LCD_Y + LCD_H };
    engrave(c, sdf_rrect, &lcd, bb_lcd, RGBA8(78, 102, 126, 255), RGBA8(58, 80, 102, 255), 1);

    /* громкость: утопленная капсула с дорожкой */
    SdfRRect vol = { VOL_X, VOL_Y, VOL_W, VOL_H, VOL_H / 2.0f };
    Box bb_vol = { VOL_X, VOL_Y, VOL_X + VOL_W, VOL_Y + VOL_H };
    engrave(c, sdf_rrect, &vol, bb_vol, RGBA8(226, 212, 188, 255), RGBA8(232, 220, 198, 255), 0);
    SdfRRect trk = { VOL_TX0 - 3, VOL_TY - 3, VOL_TX1 - VOL_TX0 + 6, 6, 3 };
    Box bb_trk = { trk.x, trk.y, trk.x + trk.w, trk.y + trk.h };
    engrave(c, sdf_rrect, &trk, bb_trk, RGBA8(62, 86, 108, 255), RGBA8(80, 104, 126, 255), 1);

    /* быстрые аккорды: две крестовины из утопленных "кнопок"; в центре - значок D-pad и стика */
    for (int k = 0; k < QS_N; k++) {
        float x, y;
        quick_pos(k, &x, &y);
        SdfRRect q = { x - QS_W * 0.5f, y - QS_H * 0.5f, QS_W, QS_H, QS_H * 0.5f };
        Box bq = { q.x, q.y, q.x + q.w, q.y + q.h };
        engrave(c, sdf_rrect, &q, bq, RGBA8(218, 204, 178, 255), RGBA8(228, 216, 194, 255), 0);
    }
    {
        float x = QS_DPAD_CX, y = QS_DPAD_CY;    /* крестик D-pad */
        cv_rrect(c, x - 7, y - 2, 14, 4, 1.5f, RGBA8(150, 132, 108, 220));
        cv_rrect(c, x - 2, y - 7, 4, 14, 1.5f, RGBA8(150, 132, 108, 220));
        SdfCircle st = { QS_STICK_CX, QS_STICK_CY, 6.5f };   /* шляпка стика */
        Box bs = { st.cx - 7, st.cy - 7, st.cx + 7, st.cy + 7 };
        bevel(c, sdf_circle, &st, bs, &SK_CREAM, 1.5f);
        cv_ring(c, st.cx, st.cy, 3.5f, 1.0f, RGBA8(150, 132, 108, 160));
    }

    /* колесо: кремовый ободок, синий диск, кольцо-разделитель, внутренний диск */
    SdfCircle bez = { WHEEL_CX, WHEEL_CY, BEZEL_R };
    Box bb_bez = { WHEEL_CX - BEZEL_R, WHEEL_CY - BEZEL_R, WHEEL_CX + BEZEL_R, WHEEL_CY + BEZEL_R };
    bevel(c, sdf_circle, &bez, bb_bez, &SK_CREAM, 4.0f);
    SdfRing groove = { WHEEL_CX, WHEEL_CY, BEZEL_R - 7.0f, BEZEL_R - 5.5f };
    cv_sdf_fill(c, sdf_ring, &groove, bb_bez, 0, 0, 0, 0, RGBA8(150, 130, 104, 150), RGBA8(150, 130, 104, 150));
    SdfCircle disc = { WHEEL_CX, WHEEL_CY, DISC_R };
    Box bb_disc = { WHEEL_CX - DISC_R, WHEEL_CY - DISC_R, WHEEL_CX + DISC_R, WHEEL_CY + DISC_R };
    engrave(c, sdf_circle, &disc, bb_disc, C_BLUE_TOP, C_BLUE_BOT, 1);
    SdfRing sep = { WHEEL_CX, WHEEL_CY, SEP_R0, SEP_R1 };
    Box bb_sep = { WHEEL_CX - SEP_R1, WHEEL_CY - SEP_R1, WHEEL_CX + SEP_R1, WHEEL_CY + SEP_R1 };
    bevel(c, sdf_ring, &sep, bb_sep, &SK_CREAM, 2.5f);
    SdfCircle inner = { WHEEL_CX, WHEEL_CY, SEP_R0 - 0.5f };
    Box bb_in = { WHEEL_CX - SEP_R0, WHEEL_CY - SEP_R0, WHEEL_CX + SEP_R0, WHEEL_CY + SEP_R0 };
    engrave(c, sdf_circle, &inner, bb_in, C_BLUE2_TOP, C_BLUE2_BOT, 1);

    /* струнная панель: приподнятый кремовый борт и утопленное синее поле */
    SdfCarved pl = plate_shape();
    Box bb_pl = { PLATE_X, PLATE_Y, PLATE_X + PLATE_W, PLATE_Y + PLATE_H };
    SdfCarved lip = pl;
    lip.rr.x -= 6; lip.rr.y -= 6; lip.rr.w += 12; lip.rr.h += 12; lip.rr.r += 6; lip.cr -= 6;
    Box bb_lip = { PLATE_X - 6, PLATE_Y - 6, PLATE_X + PLATE_W + 6, PLATE_Y + PLATE_H + 6 };
    bevel(c, sdf_carved, &lip, bb_lip, &SK_CREAM, 3.0f);
    engrave(c, sdf_carved, &pl, bb_pl, C_PLATE_TOP, C_PLATE_BOT, 1);
    /* струны рисует build_all: до них снимается трафарет */
}

/* ---------- спрайты ---------- */
static int make_sector_sprite(Sprite *sp, SdfSector s, const Skin *skin) {
    Box bb = sector_box(&s);
    if (!sprite_alloc(sp, bb, 10)) return 0;
    s.cx -= sp->x;
    s.cy -= sp->y;
    bevel(&sp->cv, sdf_sector, &s, box_shift(bb, -sp->x, -sp->y), skin, 3.5f);
    return 1;
}

/* половина круга в центре колеса: сверху мажор, снизу минор; ровный зазор и скруглённые углы */
typedef struct { float cx, cy, r, gap, round; int bottom; } SdfHalf;
static float sdf_half(float x, float y, const void *ctx) {
    const SdfHalf *h = (const SdfHalf *)ctx;
    float dx = x - h->cx, dy = y - h->cy;
    float a = sqrtf(dx * dx + dy * dy) - h->r;
    float b = (h->bottom ? -dy : dy) + h->gap * 0.5f;
    float ax = a + h->round, ay = b + h->round;
    float ox = fmaxf(ax, 0.0f), oy = fmaxf(ay, 0.0f);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(ax, ay), 0.0f) - h->round;
}

static int make_qual_sprite(Sprite *sp, int bottom, const Skin *skin) {
    Box bb = { WHEEL_CX - CENTER_R, bottom ? WHEEL_CY : WHEEL_CY - CENTER_R,
               WHEEL_CX + CENTER_R, bottom ? WHEEL_CY + CENTER_R : WHEEL_CY };
    if (!sprite_alloc(sp, bb, 10)) return 0;
    SdfHalf h = { WHEEL_CX - sp->x, WHEEL_CY - sp->y, CENTER_R, SEG_GAP, 14.0f, bottom };
    bevel(&sp->cv, sdf_half, &h, box_shift(bb, -sp->x, -sp->y), skin, 3.5f);
    return 1;
}

static int make_led(Sprite *sp, int on) {
    const float r = 7.5f;
    Box bb = { -r, -r, r, r };
    if (!sprite_alloc(sp, bb, 9)) return 0;
    float cx = -sp->x, cy = -sp->y;
    SdfCircle c = { cx, cy, r };
    Box lb = box_shift(bb, -sp->x, -sp->y);
    if (on) cv_sdf_fill(&sp->cv, sdf_circle, &c, lb, 3.0f, 0, 0, 6.0f, RGBA8(255, 140, 50, 110), RGBA8(255, 140, 50, 110));
    Skin sk = on ? (Skin){ RGBA8(160, 74, 24, 255), RGBA8(255, 178, 96, 255), RGBA8(232, 108, 36, 255) }
                 : (Skin){ RGBA8(84, 104, 122, 255), RGBA8(166, 184, 198, 255), RGBA8(116, 138, 156, 255) };
    bevel(&sp->cv, sdf_circle, &c, lb, &sk, on ? 0.0f : 1.5f);
    SdfCircle hl = { cx - 2.0f, cy - 2.5f, 2.2f };
    cv_sdf_fill(&sp->cv, sdf_circle, &hl, lb, 0, 0, 0, 1.5f, WHITE_A(170), WHITE_A(170));
    /* координаты спрайта - относительно центра светодиода */
    return 1;
}


/* круглая объёмная деталь с центром в (0, 0): ручка, точка аппликатуры */
static int make_round(Sprite *sp, float r, const Skin *sk, int glow) {
    Box bb = { -r, -r, r, r };
    if (!sprite_alloc(sp, bb, 9)) return 0;
    float cx = -sp->x, cy = -sp->y;
    SdfCircle c = { cx, cy, r };
    Box lb = box_shift(bb, -sp->x, -sp->y);
    if (glow) cv_sdf_fill(&sp->cv, sdf_circle, &c, lb, 3.0f, 0, 0, 6.0f, RGBA8(255, 140, 50, 90), RGBA8(255, 140, 50, 90));
    bevel(&sp->cv, sdf_circle, &c, lb, sk, 2.5f);
    SdfCircle hl = { cx - r * 0.3f, cy - r * 0.35f, r * 0.28f };
    cv_sdf_fill(&sp->cv, sdf_circle, &hl, lb, 0, 0, 0, 1.5f, WHITE_A(150), WHITE_A(150));
    return 1;
}

/* подсветка выбранного быстрого аккорда: оранжевая "пилюля" с центром в (0, 0) */
/* страница настроек: кремовая объёмная панель, в ней утопленная полоса под каждую строку */
static int make_set_panel(Sprite *sp) {
    Box bb = { SET_X, SET_Y, SET_X + SET_W, SET_Y + SET_H };
    if (!sprite_alloc(sp, bb, 12)) return 0;
    SdfRRect r = { SET_X - sp->x, SET_Y - sp->y, SET_W, SET_H, 26 };
    bevel(&sp->cv, sdf_rrect, &r, box_shift(bb, -sp->x, -sp->y), &SK_CREAM, 6.0f);
    for (int k = 0; k < 3; k++) {
        float cy = SET_ROW_Y0 + k * SET_ROW_DY;
        SdfRRect row = { SET_X + 24 - sp->x, cy - 28 - sp->y, SET_W - 48, 56, 28 };
        Box br = { row.x, row.y, row.x + row.w, row.y + row.h };
        engrave(&sp->cv, sdf_rrect, &row, br, RGBA8(226, 212, 188, 255), RGBA8(232, 220, 198, 255), 0);
    }
    return 1;
}

/* галочка: выкл - утопленная синяя ячейка, вкл - оранжевая объёмная */
static int make_check(Sprite *sp, int on) {
    float h = SET_CHK * 0.5f;
    Box bb = { -h, -h, h, h };
    if (!sprite_alloc(sp, bb, 8)) return 0;
    SdfRRect r = { bb.x0 - sp->x, bb.y0 - sp->y, SET_CHK, SET_CHK, 9 };
    Box b = box_shift(bb, -sp->x, -sp->y);
    if (on) {
        bevel(&sp->cv, sdf_rrect, &r, b, &SK_OR, 2.0f);
        /* сама галочка - две светлые полоски */
        float cx = -sp->x, cy = -sp->y;
        cv_ribbon(&sp->cv, cx - 9, cy + 1, 4.5f, cx - 2, cy + 8, 4.5f, C_LCD_TEXT);
        cv_ribbon(&sp->cv, cx - 3, cy + 8, 4.5f, cx + 10, cy - 8, 4.5f, C_LCD_TEXT);
    } else {
        engrave(&sp->cv, sdf_rrect, &r, b, RGBA8(70, 98, 121, 255), RGBA8(54, 80, 102, 255), 1);
    }
    return 1;
}

static int make_qs_on(Sprite *sp) {
    Box bb = { -QS_W * 0.5f, -QS_H * 0.5f, QS_W * 0.5f, QS_H * 0.5f };
    if (!sprite_alloc(sp, bb, 8)) return 0;
    SdfRRect r = { bb.x0 - sp->x, bb.y0 - sp->y, QS_W, QS_H, QS_H * 0.5f };
    bevel(&sp->cv, sdf_rrect, &r, box_shift(bb, -sp->x, -sp->y), &SK_OR, 2.0f);
    return 1;
}


/* Какие пиксели панели принадлежат какой струне. Каждая струна рисуется отдельно в маску;
 * пиксель на стыке двух струн достаётся той, что покрывает его сильнее.
 * paint(c, i) рисует струну i белым, y_range(i, &y0, &y1) - её полоса по высоте (экран). */
typedef void (*StrPaintFn)(Canvas *c, int i);
typedef void (*StrRangeFn)(int i, float *y0, float *y1);

static int build_pixels(int n, StrPaintFn paint, StrRangeFn range, StrPix **out, int *out_n) {
    Canvas mask;
    uint8_t *own_cov = (uint8_t *)calloc((size_t)PLATE_W * PLATE_H, 1);
    int8_t *own = (int8_t *)malloc((size_t)PLATE_W * PLATE_H);
    if (!own_cov || !own || !canvas_alloc(&mask, PLATE_W, PLATE_H)) return 0;
    memset(own, -1, (size_t)PLATE_W * PLATE_H);
    for (int i = 0; i < n; i++) {
        float ya, yb;
        range(i, &ya, &yb);
        int y0 = (int)ya - 16 - PLATE_Y, y1 = (int)yb + 16 - PLATE_Y;
        if (y0 < 0) y0 = 0;
        if (y1 > PLATE_H) y1 = PLATE_H;
        for (int y = y0; y < y1; y++) memset(mask.px + (size_t)y * PLATE_W, 0, (size_t)PLATE_W * 4);
        paint(&mask, i);
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < PLATE_W; x++) {
                int k = y * PLATE_W + x;
                unsigned c = mask.px[k] >> 24;
                if (c > own_cov[k]) { own_cov[k] = (uint8_t)c; own[k] = (int8_t)i; }
            }
    }
    for (int i = 0; i < n; i++) out_n[i] = 0;
    for (int k = 0; k < PLATE_W * PLATE_H; k++) if (own[k] >= 0) out_n[own[k]]++;
    for (int i = 0; i < n; i++) {
        out[i] = (StrPix *)malloc(sizeof(StrPix) * (size_t)(out_n[i] ? out_n[i] : 1));
        if (!out[i]) return 0;
        out_n[i] = 0;
    }
    for (int k = 0; k < PLATE_W * PLATE_H; k++)
        if (own[k] >= 0) {
            StrPix *p = &out[own[k]][out_n[own[k]]++];
            int x = k % PLATE_W, y = k / PLATE_W;
            float fx = ((float)x + PLATE_X + 0.5f - FL_X0) / FL_CELL;
            int c = (int)fx;
            if (c < 0) c = 0;
            if (c > FL_NC - 2) c = FL_NC - 2;
            float fr = (fx - (float)c) * 256.0f;
            p->x = (uint16_t)x; p->y = (uint16_t)y; p->cell = (uint16_t)c;
            p->frac = (uint8_t)(fr < 0.0f ? 0 : (fr > 255.0f ? 255 : fr));
            p->cov = own_cov[k];
        }
    free(mask.px);
    free(own);
    free(own_cov);
    return 1;
}

static void harp_mask_paint(Canvas *c, int i) { paint_string(c, PLATE_X, PLATE_Y, i, 0.0f, RGBA8(255, 255, 255, 255)); }
static void harp_range(int i, float *y0, float *y1) {
    float ya = key_center_y(i), yb = strum_string_y(i);
    *y0 = fminf(ya, yb); *y1 = fmaxf(ya, yb);
}

/* ---------- гитарный гриф ----------
 * Головка слева, толстая струна сверху (как гитарист видит свой гриф). Лады с настоящими
 * пропорциями, перламутровые точки на 3, 5, 7, 9 ладах, порожек, справа - часть розетки
 * резонаторного отверстия. Струны объёмные: цилиндрическая светотень, у четырёх басовых - оплётка. */
enum { GS_NORMAL = 0, GS_MUTED, GS_MASK };

static void paint_gstring(Canvas *c, float ox, float oy, int i, int mode) {
    float th = gtr_thick(i), yc = gtr_string_y(i) - oy, h = th * 0.5f;
    SdfRRect rr = { GT_STR_X0 - ox, yc - h, GT_STR_X1 - GT_STR_X0, th, h };
    int x0 = (int)(GT_STR_X0 - ox) - 1, x1 = (int)(GT_STR_X1 - ox) + 2;
    int y0 = (int)(yc - h) - 1, y1 = (int)(yc + h) + 2;
    int wound = i <= 3;
    float wpitch = 1.6f + 0.22f * th;                /* шаг оплётки: у толстых крупнее */
    for (int py = y0; py < y1; py++) {
        if (py < 0 || py >= c->h) continue;
        float v = ((float)py + 0.5f - yc) / h;       /* -1 сверху .. 1 снизу */
        if (v < -1.0f) v = -1.0f;
        if (v > 1.0f) v = 1.0f;
        float shade = 1.0f - 0.30f * v * v - (v > 0.0f ? 0.14f * v : 0.0f);
        float hl = 0.38f * expf(-((v + 0.42f) / 0.22f) * ((v + 0.42f) / 0.22f));
        for (int px = x0; px < x1; px++) {
            if (px < 0 || px >= c->w) continue;
            float sd = sdf_rrect((float)px + 0.5f, (float)py + 0.5f, &rr);
            float cov = 0.5f - sd;
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            uint32_t *d = &c->px[py * c->w + px];
            if (mode == GS_MASK) { *d = RGBA8(255, 255, 255, (int)(cov * 255.0f)); continue; }
            float w = 1.0f;
            if (wound) w = 0.82f + 0.18f * sinf(2.0f * PI_F * (((float)px + ox) + v * th * 0.6f) / wpitch);
            float r = 233.0f * shade * w + 255.0f * hl, g = 221.0f * shade * w + 255.0f * hl, b = 198.0f * shade * w + 245.0f * hl;
            if (mode == GS_MUTED) { r = r * 0.45f + 70.0f; g = g * 0.45f + 92.0f; b = b * 0.45f + 112.0f; }
            if (r > 255.0f) r = 255.0f;
            if (g > 255.0f) g = 255.0f;
            if (b > 255.0f) b = 255.0f;
            /* смешивание поверх */
            uint32_t dv = *d;
            float ia = 1.0f - cov;
            *d = RGBA8((int)(r * cov + (float)(dv & 255) * ia), (int)(g * cov + (float)((dv >> 8) & 255) * ia),
                       (int)(b * cov + (float)((dv >> 16) & 255) * ia), 255);
        }
    }
}

typedef struct { SdfCircle c; SdfCarved pl; float inset; } SdfInPlate;
static float sdf_in_plate(float x, float y, const void *ctx) {   /* круг, обрезанный краем панели */
    const SdfInPlate *q = (const SdfInPlate *)ctx;
    return fmaxf(sdf_circle(x, y, &q->c), sdf_carved(x, y, &q->pl) + q->inset);
}

typedef struct { SdfRing r; SdfCarved pl; float inset; } SdfRingInPlate;
static float sdf_ring_in_plate(float x, float y, const void *ctx) {
    const SdfRingInPlate *q = (const SdfRingInPlate *)ctx;
    return fmaxf(sdf_ring(x, y, &q->r), sdf_carved(x, y, &q->pl) + q->inset);
}

static SdfCarved plate_shape(void);

static void paint_fretboard(Canvas *c) {
    const float ox = PLATE_X, oy = PLATE_Y;
    SdfCarved pl = plate_shape();
    pl.rr.x -= ox; pl.rr.y -= oy; pl.ccx -= ox; pl.ccy -= oy;
    float cy = (GT_TOP + GT_BOTTOM) * 0.5f - oy;
    Box all = { 0, 0, (float)c->w, (float)c->h };
    /* резонаторное отверстие и розетка */
    SdfInPlate hole = { { GT_HOLE_CX - ox, cy, GT_HOLE_R }, pl, 5.0f };
    cv_sdf_fill(c, sdf_in_plate, &hole, all, 0, 0, 0, 0, RGBA8(24, 38, 56, 150), RGBA8(24, 38, 56, 150));
    for (int k = 0; k < 3; k++) {                /* три кольца розетки */
        float r0 = GT_HOLE_R + 7.0f + 7.0f * (float)k;
        SdfRingInPlate ring = { { GT_HOLE_CX - ox, cy, r0, r0 + (k == 1 ? 3.0f : 1.6f) }, pl, 5.0f };
        uint32_t col = RGBA8(232, 218, 192, k == 1 ? 80 : 46);
        cv_sdf_fill(c, sdf_ring_in_plate, &ring, all, 0, 0, 0, 0, col, col);
    }
    /* лады: тень и металлическая полоска */
    for (int f = 1; f <= GT_FRETS; f++) {
        float x = gtr_fret_x(f) - ox;
        cv_rrect(c, x - 0.2f, GT_TOP + 6 - oy + 1.5f, 3.2f, GT_BOTTOM - GT_TOP - 12, 1.5f, DARK_A(60));
        cv_rrect_grad(c, x - 1.2f, GT_TOP + 6 - oy, 2.6f, GT_BOTTOM - GT_TOP - 12, 1.3f,
                      RGBA8(236, 226, 206, 190), RGBA8(196, 184, 160, 190));
    }
    /* перламутровые точки: 3, 5, 7, 9 - посередине, 12 - двойная */
    static const int MARK[5] = { 3, 5, 7, 9, 12 };
    for (int k = 0; k < 5; k++) {
        float x = (gtr_fret_x(MARK[k] - 1) + gtr_fret_x(MARK[k])) * 0.5f - ox;
        float ys[2] = { cy, cy };
        int n = 1;
        if (MARK[k] == 12) { ys[0] = (gtr_string_y(1) + gtr_string_y(2)) * 0.5f - oy; ys[1] = (gtr_string_y(3) + gtr_string_y(4)) * 0.5f - oy; n = 2; }
        for (int j = 0; j < n; j++) {
            cv_circle(c, x, ys[j] + 1.5f, 6.5f, DARK_A(40));
            cv_circle(c, x, ys[j], 6.5f, RGBA8(232, 224, 210, 120));
            cv_circle(c, x - 1.5f, ys[j] - 1.5f, 2.5f, WHITE_A(70));
        }
    }
    /* полоска позиций под грифом: утопленная, с перегородками точно под ладами и ячейкой AUTO */
    SdfRRect ps = { GT_NUT_X0 - 4 - ox, GT_POS_Y0 - oy, GT_AUTO_X1 - GT_NUT_X0 + 4, GT_POS_Y1 - GT_POS_Y0, 9 };
    Box bps = { ps.x, ps.y, ps.x + ps.w, ps.y + ps.h };
    engrave(c, sdf_rrect, &ps, bps, RGBA8(46, 68, 90, 255), RGBA8(56, 80, 102, 255), 1);
    for (int f = 1; f <= GT_FRETS; f++) {
        float x = gtr_fret_x(f) - ox;
        cv_rect(c, (int)x, GT_POS_Y0 + 5 - oy, 1, GT_POS_Y1 - GT_POS_Y0 - 10, RGBA8(120, 144, 166, f <= GT_POS_MAX ? 120 : 70));
    }
    cv_rect(c, GT_AUTO_X0 - 4 - ox, GT_POS_Y0 + 4 - oy, 2, GT_POS_Y1 - GT_POS_Y0 - 8, RGBA8(140, 162, 182, 160));
    /* порожек */
    SdfRRect nut = { GT_NUT_X0 - ox, GT_TOP - 4 - oy, GT_NUT_X1 - GT_NUT_X0, GT_BOTTOM - GT_TOP + 8, 4 };
    Box bn = { nut.x, nut.y, nut.x + nut.w, nut.y + nut.h };
    bevel(c, sdf_rrect, &nut, bn, &SK_CREAM, 2.5f);
}

static void paint_gstrings(Canvas *c, int mode) {
    for (int i = 0; i < N_GSTR; i++) {          /* мягкие тени струн на грифе */
        float th = gtr_thick(i), yc = gtr_string_y(i) - PLATE_Y;
        SdfRRect sh = { GT_STR_X0 - PLATE_X, yc - th * 0.5f, GT_STR_X1 - GT_STR_X0, th, th * 0.5f };
        Box b = { sh.x, sh.y, sh.x + sh.w, sh.y + sh.h };
        cv_sdf_fill(c, sdf_rrect, &sh, b, 0.5f, 0, 3.0f, 3.0f, DARK_A(70), DARK_A(70));
    }
    for (int i = 0; i < N_GSTR; i++) paint_gstring(c, PLATE_X, PLATE_Y, i, mode);
}

static void gtr_mask_paint(Canvas *c, int i) { paint_gstring(c, PLATE_X, PLATE_Y, i, GS_MASK); }
static void gtr_range(int i, float *y0, float *y1) { *y0 = *y1 = gtr_string_y(i); }

static int build_all(void) {
    if (!canvas_alloc(&g_cv_bg, SCR_W, SCR_H)) return 0;
    paint_bg(&g_cv_bg);

    /* гриф гитары: копия панели без струн + лады, порожек, розетка, 6 струн (обычные и приглушённые) */
    for (int v = 0; v < 2; v++) {
        if (!canvas_alloc(&g_cv_gplate[v], PLATE_W, PLATE_H)) return 0;
        for (int y = 0; y < PLATE_H; y++)
            memcpy(g_cv_gplate[v].px + (size_t)y * PLATE_W, g_cv_bg.px + (size_t)(y + PLATE_Y) * SCR_W + PLATE_X, (size_t)PLATE_W * 4);
        paint_fretboard(&g_cv_gplate[v]);
        paint_gstrings(&g_cv_gplate[v], v ? GS_MUTED : GS_NORMAL);
    }
    if (!build_pixels(N_GSTR, gtr_mask_paint, gtr_range, g_gpix, g_gpix_n)) return 0;

    paint_strings(&g_cv_bg, 0, 0, &PS_NORMAL);
    if (!build_pixels(N_STRINGS, harp_mask_paint, harp_range, g_spix, g_spix_n)) return 0;

    /* "горящая" панель: копия фона в границах панели + струны оранжевым */
    for (int v = PL_LIT; v < PL_N; v++) {
        if (!canvas_alloc(&g_cv_plate[v], PLATE_W, PLATE_H)) return 0;
        for (int y = 0; y < PLATE_H; y++)
            memcpy(g_cv_plate[v].px + (size_t)y * PLATE_W, g_cv_bg.px + (size_t)(y + PLATE_Y) * SCR_W + PLATE_X, (size_t)PLATE_W * 4);
        paint_strings(&g_cv_plate[v], PLATE_X, PLATE_Y, &PS_LIT);
    }

    for (int i = 0; i < N_ROOTS; i++) {
        SdfSector s = make_sector(i, N_ROOTS, ROOT_R0, ROOT_R1, 18.0f);
        if (!make_sector_sprite(&g_root_spr[i][ST_NORMAL], s, &SK_CREAM)) return 0;
        if (!make_sector_sprite(&g_root_spr[i][ST_SEL], s, &SK_OR)) return 0;
    }
    for (int i = 0; i < N_MODS; i++) {
        SdfSector s = make_sector(i, N_MODS, MOD_R0, MOD_R1, 14.0f);
        if (!make_sector_sprite(&g_mod_spr[i][ST_NORMAL], s, &SK_CREAM)) return 0;
        if (!make_sector_sprite(&g_mod_spr[i][ST_SEL], s, &SK_OR)) return 0;
        if (!make_sector_sprite(&g_mod_spr[i][ST_ARMED], s, &SK_PEACH)) return 0;
    }
    for (int q = 0; q < 2; q++) {
        if (!make_qual_sprite(&g_qual_spr[q][ST_NORMAL], q, &SK_CREAM)) return 0;
        if (!make_qual_sprite(&g_qual_spr[q][ST_SEL], q, &SK_OR)) return 0;
        if (!make_qual_sprite(&g_qual_spr[q][ST_ARMED], q, &SK_PEACH)) return 0;
    }
    if (!make_led(&g_led[0], 0) || !make_led(&g_led[1], 1)) return 0;
    if (!make_round(&g_knob, 8.5f, &SK_CREAM, 0)) return 0;
    Skin dot = { RGBA8(160, 74, 24, 255), RGBA8(250, 168, 92, 255), RGBA8(226, 104, 36, 255) };
    if (!make_round(&g_dot, 9.0f, &dot, 1)) return 0;
    if (!make_qs_on(&g_qs_on)) return 0;
    if (!make_set_panel(&g_set_panel) || !make_check(&g_chk[0], 0) || !make_check(&g_chk[1], 1)) return 0;
    return 1;
}

static void free_textures(void) {
    if (g_tex_bg) vita2d_free_texture(g_tex_bg);
    g_tex_bg = NULL;
    for (int v = 0; v < PL_N; v++) { if (g_tex_plate[v]) vita2d_free_texture(g_tex_plate[v]); g_tex_plate[v] = NULL; }
    for (int i = 0; i < N_ROOTS; i++)
        for (int k = 0; k < 2; k++) { if (g_root_spr[i][k].tex) vita2d_free_texture(g_root_spr[i][k].tex); g_root_spr[i][k].tex = NULL; }
    for (int i = 0; i < N_MODS; i++)
        for (int k = 0; k < ST_N; k++) { if (g_mod_spr[i][k].tex) vita2d_free_texture(g_mod_spr[i][k].tex); g_mod_spr[i][k].tex = NULL; }
    for (int k = 0; k < N_FLUID_TEX; k++) { if (g_tex_fluid[k]) vita2d_free_texture(g_tex_fluid[k]); g_tex_fluid[k] = NULL; }
    for (int k = 0; k < 2; k++) { if (g_tex_khl[k]) vita2d_free_texture(g_tex_khl[k]); g_tex_khl[k] = NULL; }
    for (int q = 0; q < 2; q++)
        for (int k = 0; k < ST_N; k++) { if (g_qual_spr[q][k].tex) vita2d_free_texture(g_qual_spr[q][k].tex); g_qual_spr[q][k].tex = NULL; }
    for (int k = 0; k < 2; k++) { if (g_led[k].tex) vita2d_free_texture(g_led[k].tex); g_led[k].tex = NULL; }
    for (int v = 0; v < 2; v++) { if (g_tex_gplate[v]) vita2d_free_texture(g_tex_gplate[v]); g_tex_gplate[v] = NULL; }
    Sprite *one[6] = { &g_knob, &g_dot, &g_qs_on, &g_set_panel, &g_chk[0], &g_chk[1] };
    for (int k = 0; k < 6; k++) { if (one[k]->tex) vita2d_free_texture(one[k]->tex); one[k]->tex = NULL; }
}

static void fluid_clear_textures(void);

static void create_textures(void) {
    g_tex_bg = tex_from(&g_cv_bg);
    for (int v = PL_LIT; v < PL_N; v++) g_tex_plate[v] = tex_from(&g_cv_plate[v]);
    for (int i = 0; i < N_ROOTS; i++) for (int k = 0; k < 2; k++) g_root_spr[i][k].tex = tex_from(&g_root_spr[i][k].cv);
    for (int i = 0; i < N_MODS; i++) for (int k = 0; k < ST_N; k++) g_mod_spr[i][k].tex = tex_from(&g_mod_spr[i][k].cv);
    for (int q = 0; q < 2; q++) for (int k = 0; k < ST_N; k++) g_qual_spr[q][k].tex = tex_from(&g_qual_spr[q][k].cv);
    for (int k = 0; k < N_FLUID_TEX; k++) g_tex_fluid[k] = vita2d_create_empty_texture(PLATE_W, PLATE_H);
    fluid_clear_textures();
    for (int k = 0; k < 2; k++) {
        g_tex_khl[k] = vita2d_create_empty_texture(PLATE_W, PLATE_H);
        if (!g_tex_khl[k]) continue;
        uint32_t *tex = (uint32_t *)vita2d_texture_get_datap(g_tex_khl[k]);
        int stride = (int)vita2d_texture_get_stride(g_tex_khl[k]) / 4;
        for (int y = 0; y < PLATE_H; y++) memset(tex + (size_t)y * stride, 0, (size_t)PLATE_W * 4);
        g_khl_strings[k] = 0;
    }
    g_khl_cur = -1; g_khl_have = -1;             /* перерисовать при следующем кадре */
    for (int k = 0; k < 2; k++) g_led[k].tex = tex_from(&g_led[k].cv);
    for (int v = 0; v < 2; v++) g_tex_gplate[v] = tex_from(&g_cv_gplate[v]);
    g_knob.tex = tex_from(&g_knob.cv);
    g_dot.tex = tex_from(&g_dot.cv);
    g_qs_on.tex = tex_from(&g_qs_on.cv);
    g_set_panel.tex = tex_from(&g_set_panel.cv);
    for (int k = 0; k < 2; k++) g_chk[k].tex = tex_from(&g_chk[k].cv);
}

/* ---------- текст (рисует GPU каждый кадр) ---------- */
static int text_w(float sc, const char *s) { return vita2d_pgf_text_width(g_pgf, sc, s); }

static void text_at(float x, float y, uint32_t col, float sc, const char *s, int bold) {
    vita2d_pgf_draw_text(g_pgf, (int)x, (int)y, col, sc, s);
    if (bold) vita2d_pgf_draw_text(g_pgf, (int)x + 1, (int)y, col, sc, s);
}
/* по центру: базовая линия чуть ниже середины */
static void text_c(float cx, float cy, uint32_t col, float sc, const char *s, int bold) {
    text_at(cx - (float)text_w(sc, s) / 2.0f, cy + 6.5f * sc, col, sc, s, bold);
}
/* с разрядкой букв, как в логотипе концепта */
static void text_spaced(float x, float y, uint32_t col, float sc, float gap, const char *s, int bold) {
    char ch[2] = { 0, 0 };
    for (; *s; s++) {
        ch[0] = *s;
        text_at(x, y, col, sc, ch, bold);
        x += (float)text_w(sc, ch) + gap;
    }
}

static int is_held(int kind, int idx) {
    for (int i = 0; i < g_held_n; i++) if (g_held_kind[i] == kind && g_held_idx[i] == idx) return 1;
    return 0;
}

static void draw_sprite(const Sprite *s, float dx, float dy, int held) {
    if (!s->tex) return;
    if (held) vita2d_draw_texture_tint(s->tex, s->x + dx, s->y + dy + 1.5f, RGBA8(212, 212, 212, 255));
    else vita2d_draw_texture(s->tex, s->x + dx, s->y + dy);
}

static void draw_plate_part(int v, const float r[4], unsigned alpha) {
    if (!g_tex_plate[v] || alpha == 0) return;
    if (alpha > 255) alpha = 255;
    vita2d_draw_texture_tint_part(g_tex_plate[v], r[0], r[1], r[0] - PLATE_X, r[1] - PLATE_Y, r[2], r[3],
                                  RGBA8(255, 255, 255, alpha));
}

/* ---------- "жидкая" подсветка ----------
 * Светится только задетая струна: от точки касания по ней растекается цвет - по клавише,
 * нити и полосе, в обе стороны. Это капли: каждая расползается, замедляется и тускнеет,
 * а их сумма даёт одно гладкое поле - удары сливаются в единую жидкость.
 * Палец, лежащий на струне, подпитывает каплю под собой, и она идёт за ним.
 * Цвет зависит от густоты, как слои на постерах Retoka: густое ядро вишнёво-синее, дальше
 * красный, оранжевый, янтарь, бледно-жёлтый, на самом фронте - голубой, в самом густом месте - синий.
 * Поле - по клеткам FL_CELL пикселей вдоль x (путь струны идёт слева направо), между
 * клетками плавно; рисуется в полном разрешении точно по пикселям своей струны. */
#define MAX_DROPS 200
/* разлёт: начальная скорость капель и её затухание - от полосы цвет доходит до нитей
 * и края клавиш (около 200 px), от клавиши - до полосы */
#define FL_SPEED 640.0f
#define FL_SPEED_TAU 0.32f
#define FL_WIDTH 74.0f
#define FL_FADE_TAU 0.42f

typedef struct { int s; float x, v, w, amt; int feed; } FDrop;
static FDrop g_fd[MAX_DROPS];
static int g_nfd = 0;

/* палитра по густоте 0..1 и прозрачность (таблицы на 256 значений) */
#define NPAL 9
static uint32_t g_pal_lut[256];
static void fluid_init_palette(void) {
    /* от фронта к ядру: голубой -> бледно-жёлтый -> янтарь -> оранжевый -> красный ->
     * вишнёвый -> глубокий синий (как сменяются слои на постерах) */
    static const float T[NPAL] = { 0.00f, 0.16f, 0.30f, 0.44f, 0.57f, 0.70f, 0.82f, 0.92f, 1.00f };
    static const int C[NPAL][3] = { { 96, 182, 220 },    /* голубая кромка */
                                    { 196, 222, 206 },   /* переход к жёлтому */
                                    { 250, 226, 160 },   /* бледно-жёлтый */
                                    { 250, 176, 70 },    /* янтарь */
                                    { 243, 112, 34 },    /* оранжевый */
                                    { 214, 54, 40 },     /* красный */
                                    { 150, 34, 62 },     /* вишнёвый */
                                    { 60, 48, 112 },     /* индиго */
                                    { 26, 62, 128 } };   /* глубокий синий */
    for (int i = 0; i < 256; i++) {
        float v = (float)i / 255.0f;
        int k = 0;
        while (k < NPAL - 2 && v > T[k + 1]) k++;
        float t = (v - T[k]) / (T[k + 1] - T[k]);
        int r = (int)(C[k][0] + (C[k + 1][0] - C[k][0]) * t);
        int g = (int)(C[k][1] + (C[k + 1][1] - C[k][1]) * t);
        int b = (int)(C[k][2] + (C[k + 1][2] - C[k][2]) * t);
        float a = (v - 0.03f) / 0.30f;               /* слабый цвет почти прозрачен: кромка мягкая, */
        if (a < 0.0f) a = 0.0f;                      /* а при угасании не остаётся пелены */
        if (a > 1.0f) a = 1.0f;
        a = a * a * (3.0f - 2.0f * a);
        g_pal_lut[i] = RGBA8(r, g, b, (int)(a * 238.0f));
    }
}

static FDrop *fdrop_new(void) {
    if (g_nfd < MAX_DROPS) return &g_fd[g_nfd++];
    FDrop *w = &g_fd[0];                              /* места нет - заменяем самую слабую */
    for (int i = 1; i < MAX_DROPS; i++) if (g_fd[i].amt < w->amt) w = &g_fd[i];
    return w;
}

static float clampx(float x) { return x < FL_X0 + 2 ? FL_X0 + 2 : (x > FL_X1 - 2 ? FL_X1 - 2 : x); }

/* удар по струне: капля на месте и две, растекающиеся в стороны */
static int key_lit(int s);

static void fluid_hit(int s, float x, float amt) {
    x = clampx(x);
    FDrop *d;
    /* клавиша струны подсвечена (нота аккорда) - жидкость сильнее и дальше уходит к клавишам */
    int lit = key_lit(s);
    d = fdrop_new(); d->s = s; d->x = x; d->v = 0.0f;    d->w = 12.0f; d->amt = amt;         d->feed = 0;
    d = fdrop_new(); d->s = s; d->x = x; d->v = -FL_SPEED * (lit ? 1.6f : 1.0f); d->w = 10.0f;
    d->amt = amt * (lit ? 1.15f : 0.8f); d->feed = 0;
    d = fdrop_new(); d->s = s; d->x = x; d->v = FL_SPEED;  d->w = 10.0f; d->amt = amt * 0.8f; d->feed = 0;
}

/* палец на струне: капля под ним подпитывается и идёт за пальцем */
static void fluid_feed(int s, float x, float amt) {
    x = clampx(x);
    for (int i = 0; i < g_nfd; i++) {
        FDrop *d = &g_fd[i];
        if (d->s == s && d->feed) {
            d->x += (x - d->x) * 0.6f;
            if (d->amt < amt) d->amt = amt;
            d->feed = 2;                               /* обновлена в этом кадре */
            return;
        }
    }
    FDrop *d = fdrop_new();
    d->s = s; d->x = x; d->v = 0.0f; d->w = 14.0f; d->amt = amt; d->feed = 2;
}

static void fx_update(float dt) {
    int n = 0;
    float vk = expf(-dt / FL_SPEED_TAU), ak = expf(-dt / FL_FADE_TAU);
    for (int i = 0; i < g_nfd; i++) {
        FDrop *d = &g_fd[i];
        d->x += d->v * dt;
        if (d->x < FL_X0 || d->x > FL_X1) { d->x = clampx(d->x); d->v = 0.0f; }
        d->v *= vk;
        d->w += (FL_WIDTH - d->w) * (1.0f - expf(-dt / 0.5f));   /* расплывается */
        if (d->feed == 2) d->feed = 1;                          /* подпитка держит, пока палец здесь */
        else { d->feed = 0; d->amt *= ak; }
        if (d->amt > 0.015f) g_fd[n++] = *d;
    }
    g_nfd = n;
}

/* струны текущего режима: 25 струн страммера или 6 гитарных */
static int is_guitar(void) { return g_st.mode == MODE_GUITAR; }
static int fl_nstr(void) { return is_guitar() ? N_GSTR : N_STRINGS; }
static const StrPix *fl_pix(int s, int *n) {
    if (is_guitar()) { *n = g_gpix_n[s]; return g_gpix[s]; }
    *n = g_spix_n[s];
    return g_spix[s];
}

/* Быстрый расчёт (Vita без аппаратного деления, медленные expf):
 *   - гауссиана капли и мягкое насыщение - таблицами;
 *   - все капли за один проход складываются в клетки своих струн;
 *   - пиксель: клетка и доля посчитаны заранее, цвет - целочисленная интерполяция и таблица палитры;
 *   - пишем сразу в текстуру (одна из трёх по кругу) и только пиксели горящих струн; струну, которая
 *     горела в этой текстуре раньше, а теперь погасла, стираем. Всю панель не копируем. */
#define GLUT_N 512
#define GLUT_R 2.6f                                  /* гауссиана считается до 2.6 ширины */
#define SAT_N 1024
#define SAT_MAX 4.0f
static float g_glut[GLUT_N + 2];
static uint8_t g_sat[SAT_N + 1];
static float g_acc[N_STRINGS][FL_NC];
static uint8_t g_cv8[N_STRINGS][FL_NC + 1];
static uint16_t g_keyfade[FL_NC];            /* 0..256 по клеткам: 0 на клавишах, 256 правее шейки */

static void fluid_init_tables(void) {
    for (int c = 0; c < FL_NC; c++) {           /* клавиши - шейка: плавно от 0 к 1 */
        float x = FL_X0 + (float)c * FL_CELL, t = (x - (float)WK_X1) / (float)(NECK_X1 + 10 - WK_X1);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        g_keyfade[c] = (uint16_t)(256.0f * t * t * (3.0f - 2.0f * t));
    }
    for (int i = 0; i <= GLUT_N + 1; i++) {
        float t = ((float)i / (float)GLUT_N * 2.0f - 1.0f) * GLUT_R;
        g_glut[i] = expf(-t * t);
    }
    for (int i = 0; i <= SAT_N; i++) {
        float v = 1.0f - expf(-1.25f * (float)i / (float)SAT_N * SAT_MAX);   /* мягкое насыщение */
        g_sat[i] = (uint8_t)(v * 255.0f + 0.5f);
    }
}

/* перерисовать подсветку в следующую текстуру; возвращает её, если что-то горит */
static vita2d_texture *fluid_render(void) {
    int nstr = fl_nstr();
    unsigned char lit[N_STRINGS];
    memset(lit, 0, sizeof(lit));
    for (int i = 0; i < g_nfd; i++) lit[g_fd[i].s] = 1;
    int any = 0;
    for (int s = 0; s < nstr; s++) if (lit[s]) { any = 1; memset(g_acc[s], 0, sizeof(g_acc[s])); }

    int ti = g_fluid_next;
    vita2d_texture *t = g_tex_fluid[ti];
    if (!t) return NULL;
    int need = any;
    for (int s = 0; s < nstr && !need; s++) need = g_tex_lit[ti][s];
    if (!need) return NULL;                          /* текстура чистая и ничего не горит */
    g_fluid_next = (ti + 1) % N_FLUID_TEX;

    /* капли -> клетки */
    for (int i = 0; i < g_nfd; i++) {
        const FDrop *d = &g_fd[i];
        float *acc = g_acc[d->s];
        float reach = GLUT_R * d->w;
        int c0 = (int)((d->x - reach - FL_X0) * (1.0f / FL_CELL)) + 1, c1 = (int)((d->x + reach - FL_X0) * (1.0f / FL_CELL));
        if (c0 < 0) c0 = 0;
        if (c1 > FL_NC - 1) c1 = FL_NC - 1;
        float k = (float)GLUT_N / (2.0f * GLUT_R * d->w);           /* пикселей -> индекс таблицы */
        float pos = (FL_X0 + (float)c0 * FL_CELL - d->x) * k + (float)GLUT_N * 0.5f, step = FL_CELL * k;
        for (int c = c0; c <= c1; c++, pos += step) {
            int gi = (int)pos;
            if (gi >= 0 && gi <= GLUT_N) acc[c] += d->amt * g_glut[gi];
        }
    }

    uint32_t *tex = (uint32_t *)vita2d_texture_get_datap(t);
    int stride = (int)vita2d_texture_get_stride(t) / 4;
    for (int s = 0; s < nstr; s++) {
        int npx;
        const StrPix *pix = fl_pix(s, &npx);
        if (!lit[s]) {
            if (g_tex_lit[ti][s]) {                  /* погасла - стираем её пиксели в этой текстуре */
                for (int k = 0; k < npx; k++) tex[pix[k].y * stride + pix[k].x] = 0;
                g_tex_lit[ti][s] = 0;
            }
            continue;
        }
        g_tex_lit[ti][s] = 1;
        uint8_t *cv = g_cv8[s];
        const float *acc = g_acc[s];
        /* в аккорде клавиша не из аккорда («округлённая» к нему струна) - жидкость на неё не заходит:
         * гаснет на шейке клавиши; справа (нити, полосы) - как у всех */
        int keep_off_keys = !is_guitar() && g_khl_want && !key_lit(s);
        for (int c = 0; c < FL_NC; c++) {
            int si = (int)(acc[c] * ((float)SAT_N / SAT_MAX));
            cv[c] = g_sat[si > SAT_N ? SAT_N : si];
            if (keep_off_keys) cv[c] = (uint8_t)((cv[c] * g_keyfade[c]) >> 8);
        }
        cv[FL_NC] = cv[FL_NC - 1];
        for (int k = 0; k < npx; k++) {
            const StrPix *p = &pix[k];
            int v0 = cv[p->cell], v = v0 + (((int)cv[p->cell + 1] - v0) * p->frac >> 8);
            uint32_t col = g_pal_lut[v];
            if (p->cov != 255) col = (col & 0x00FFFFFFu) | ((((col >> 24) * p->cov + 128) >> 8) << 24);
            tex[p->y * stride + p->x] = col;
        }
    }
    return any ? t : NULL;
}

static void plate_span(const float br[4], float a, float b, unsigned alpha) {
    if (b - a < 0.5f || alpha == 0) return;
    float r[4] = { a, br[1], b - a, br[3] };
    draw_plate_part(PL_LIT, r, alpha);
}

static void draw_fluid(void) {
    vita2d_texture *t = fluid_render();
    if (t) vita2d_draw_texture(t, PLATE_X, PLATE_Y);
}

/* очистить текстуры подсветки целиком (после создания и при смене набора струн) */
static void fluid_clear_textures(void) {
    for (int k = 0; k < N_FLUID_TEX; k++) {
        if (!g_tex_fluid[k]) continue;
        uint32_t *tex = (uint32_t *)vita2d_texture_get_datap(g_tex_fluid[k]);
        int stride = (int)vita2d_texture_get_stride(g_tex_fluid[k]) / 4;
        for (int y = 0; y < PLATE_H; y++) memset(tex + (size_t)y * stride, 0, (size_t)PLATE_W * 4);
    }
    memset(g_tex_lit, 0, sizeof(g_tex_lit));
}

/* гриф: заглушённые струны приглушены, на ладах - точки аппликатуры, у порожка "x" / "o" */
static void draw_guitar(void) {
    if (g_tex_gplate[0]) vita2d_draw_texture(g_tex_gplate[0], PLATE_X, PLATE_Y);
    for (int i = 0; i < N_GSTR; i++) {
        if (g_gch.fret[i] >= 0 || !g_tex_gplate[1]) continue;
        float h = gtr_thick(i) * 0.5f + 5.0f, y = gtr_string_y(i);
        vita2d_draw_texture_part(g_tex_gplate[1], GT_STR_X0 - 2, y - h, GT_STR_X0 - 2 - PLATE_X, y - h - PLATE_Y,
                                 GT_STR_X1 - GT_STR_X0 + 4, 2.0f * h);
    }
    /* где стоит рука: светлая полоса над ладами аккорда */
    int lo = 99, hi = 0;
    for (int i = 0; i < N_GSTR; i++) if (g_gch.fret[i] > 0) { if (g_gch.fret[i] < lo) lo = g_gch.fret[i]; if (g_gch.fret[i] > hi) hi = g_gch.fret[i]; }
    if (g_gch.pos > 0 && g_st.chord_on && g_gch.pos < lo) lo = g_gch.pos;
    if (lo < 99) { if (hi < lo + 3) hi = lo + 3; if (hi > GT_FRETS) hi = GT_FRETS; }
    if (lo < 99 && g_st.chord_on)
        vita2d_draw_rectangle(gtr_fret_x(lo - 1) + 2.0f, GT_TOP + 4, gtr_fret_x(hi) - gtr_fret_x(lo - 1) - 3.0f,
                              GT_BOTTOM - GT_TOP - 8, RGBA8(236, 226, 206, 26));
    draw_fluid();
    /* полоска позиций: номера ладов, выбранная позиция - оранжевым, рука аккорда - подсвечена */
    {
        char nb[4];
        float cy = (GT_POS_Y0 + GT_POS_Y1) * 0.5f;
        /* у мажора и минора номера только у привычных форм, у остальных - 1..9 */
        int avail[GT_POS_MAX + 1] = { 0 }, ps[GT_POS_MAX];
        int nps = g_st.chord_on ? guitar_positions(g_st.root, chord_type(g_st.minor, g_st.mod), ps) : 0;
        for (int i = 0; i < nps; i++) avail[ps[i]] = 1;
        for (int p = 1; p <= GT_POS_MAX; p++) {
            if (!avail[p]) continue;
            float x0 = gtr_fret_x(p - 1) + 2.0f, x1 = gtr_fret_x(p) - 1.0f;
            int sel = g_st.gpos > 0 && g_gch.pos == p, in_hand = g_st.chord_on && lo < 99 && p >= lo && p <= hi;
            if (sel) vita2d_draw_rectangle(x0, GT_POS_Y0 + 4, x1 - x0, GT_POS_Y1 - GT_POS_Y0 - 8, C_ORANGE);
            else if (in_hand) vita2d_draw_rectangle(x0, GT_POS_Y0 + 4, x1 - x0, GT_POS_Y1 - GT_POS_Y0 - 8, RGBA8(236, 226, 206, 40));
            snprintf(nb, sizeof(nb), "%d", p);
            text_c((x0 + x1) * 0.5f, cy - 1.0f, sel ? C_LCD_TEXT : (in_hand ? C_LCD_TEXT : C_LCD_DIM), 0.7f, nb, sel);
        }
        int asel = g_st.gpos == 0;
        if (asel) vita2d_draw_rectangle(GT_AUTO_X0, GT_POS_Y0 + 4, GT_AUTO_X1 - GT_AUTO_X0 - 6, GT_POS_Y1 - GT_POS_Y0 - 8, C_ORANGE);
        text_c((GT_AUTO_X0 + GT_AUTO_X1 - 6) * 0.5f, cy - 1.0f, asel ? C_LCD_TEXT : C_LCD_DIM, 0.62f, "AUTO", 1);
    }
    /* баррэ: одним пальцем зажаты несколько струн на нижнем ладу аккорда - рисуем полоску */
    int mn = 99, first = -1, last = -1, cnt = 0;
    for (int i = 0; i < N_GSTR; i++) if (g_gch.fret[i] > 0 && g_gch.fret[i] < mn) mn = g_gch.fret[i];
    for (int i = 0; i < N_GSTR; i++) if (g_gch.fret[i] == mn) { cnt++; if (first < 0) first = i; last = i; }
    if (mn < 99 && cnt >= 2 && last - first >= 2) {
        float bx = (gtr_fret_x(mn - 1) + gtr_fret_x(mn)) * 0.5f;
        float y0 = fminf(gtr_string_y(first), gtr_string_y(last)), y1 = fmaxf(gtr_string_y(first), gtr_string_y(last));
        vita2d_draw_rectangle(bx - 5.0f, y0, 10.0f, y1 - y0, RGBA8(226, 104, 36, 210));
        vita2d_draw_rectangle(bx - 2.0f, y0, 2.5f, y1 - y0, RGBA8(250, 168, 92, 200));
    }
    for (int i = 0; i < N_GSTR; i++) {
        float y = gtr_string_y(i);
        int f = g_gch.fret[i];
        if (f > 0) draw_sprite(&g_dot, (gtr_fret_x(f - 1) + gtr_fret_x(f)) * 0.5f, y, 0);
        text_c(GT_MARK_X, y - 1.0f, f < 0 ? RGBA8(150, 168, 186, 255) : RGBA8(226, 214, 192, 255), 0.9f,
               f < 0 ? "x" : (f == 0 ? "o" : ""), 1);
    }
}

/* Подсветка клавиш аккорда: тёплый оттенок, тоника чуть ярче; к нити плавно гаснет.
 * Нота клавиши - её место на клавиатуре C4..C6, как на настоящем пианино */
#define KHL_A_TONE 72          /* автор просил чуть ярче */
#define KHL_A_ROOT 112
/* клавиша струны s подсвечена как нота выбранного аккорда (Strum / Pad) */
static int key_lit(int s) {
    if (!g_khl_want || is_guitar() || s < 0 || s >= N_STRINGS) return 0;
    return (g_khl_want >> ((STRING_BASE_NOTE + s) % 12)) & 1;
}

static void keys_hl_update(void) {
    if (g_khl_want == g_khl_have) return;
    g_khl_have = g_khl_want;
    if (!g_khl_want) { g_khl_cur = -1; return; }
    int ti = g_khl_cur == 0 ? 1 : 0;              /* пишем в ту, что сейчас не показывается */
    vita2d_texture *t = g_tex_khl[ti];
    if (!t) { g_khl_cur = -1; return; }
    uint32_t *tex = (uint32_t *)vita2d_texture_get_datap(t);
    int stride = (int)vita2d_texture_get_stride(t) / 4;
    int mask = g_khl_want & 0xFFF, root = (g_khl_want >> 12) & 15, written = 0;
    for (int i = 0; i < N_STRINGS; i++) {
        int pc = (STRING_BASE_NOTE + i) % 12, on = (mask >> pc) & 1;
        int npx = g_spix_n[i];
        const StrPix *pix = g_spix[i];
        if (!on) {
            if (g_khl_strings[ti] & (1 << i))
                for (int k = 0; k < npx; k++) tex[pix[k].y * stride + pix[k].x] = 0;
            continue;
        }
        written |= 1 << i;
        int a0 = pc == root ? KHL_A_ROOT : KHL_A_TONE;
        for (int k = 0; k < npx; k++) {
            const StrPix *p = &pix[k];
            int sx = p->x + PLATE_X, a = a0;
            if (sx >= NECK_X1) a = 0;
            else if (sx > WK_X1) a = a0 * (NECK_X1 - sx) / (NECK_X1 - WK_X1);   /* только при смене аккорда */
            a = a * p->cov / 255;
            tex[p->y * stride + p->x] = a ? RGBA8(255, 196, 132, a) : 0;
        }
    }
    g_khl_strings[ti] = written;
    g_khl_cur = ti;
}

static void draw_plate(void) {
    if (is_guitar()) { draw_guitar(); return; }
    keys_hl_update();
    if (g_khl_cur >= 0 && g_tex_khl[g_khl_cur]) vita2d_draw_texture(g_tex_khl[g_khl_cur], PLATE_X, PLATE_Y);
    /* в аккорде слабо горят струны с тоникой */
    if (g_st.chord_on)
        for (int i = 0; i < N_STRINGS; i++) {
            if (((g_map.note[i] % 12 + 12) % 12) != g_st.root) continue;
            float br[4];
            bar_rect(i, br);
            plate_span(br, BULB_X1 - 30.0f, BAR_X1 + 1.0f, 55);
        }
    draw_fluid();
}

/* тональность: ступень тоники кнопки или -1 (не из лада / тональность выключена) */
static int root_degree(int pc) { return g_st.key_on ? key_degree(g_st.key_root, g_st.key_minor, pc) : -1; }

static void draw_wheel(void) {
    for (int i = 0; i < N_ROOTS; i++) {
        int pc = ROOT_ORDER[i];
        int sel = g_st.chord_on && g_st.root == pc;
        const Sprite *sp = &g_root_spr[i][sel ? ST_SEL : ST_NORMAL];
        if (g_st.key_on && root_degree(pc) < 0 && !sel && sp->tex)      /* не из лада - приглушена */
            vita2d_draw_texture_tint(sp->tex, sp->x, sp->y, RGBA8(190, 192, 194, 255));
        else draw_sprite(sp, 0, 0, is_held(HIT_ROOT, pc));
    }
    for (int i = 0; i < N_MODS; i++) {
        int st = ST_NORMAL;
        if (g_st.mod == i) st = g_st.chord_on ? ST_SEL : ST_ARMED;
        draw_sprite(&g_mod_spr[i][st], 0, 0, is_held(HIT_MOD, i));
    }
    for (int q = 0; q < 2; q++) {
        int st = ST_NORMAL;
        if (g_st.minor == q) st = g_st.chord_on ? ST_SEL : ST_ARMED;
        draw_sprite(&g_qual_spr[q][st], 0, 0, is_held(HIT_QUAL, q));
    }

    for (int i = 0; i < N_ROOTS; i++) {
        float x, y;
        seg_center(i, N_ROOTS, (ROOT_R0 + ROOT_R1) / 2.0f, &x, &y);
        float d = is_held(HIT_ROOT, ROOT_ORDER[i]) ? 1.5f : 0.0f;
        int deg = root_degree(ROOT_ORDER[i]);
        if (g_st.key_on && deg < 0) { text_c(x, y + d, C_NAVY_SOFT, 1.45f, root_label(ROOT_ORDER[i]), 0); continue; }
        if (deg < 0) { text_c(x, y + d, C_NAVY, 1.45f, root_label(ROOT_ORDER[i]), 1); continue; }
        /* в тональности: нота чуть выше, под ней ступень (тоника - оранжевым), у уменьшённой - кружок */
        text_c(x, y + d - 6.0f, C_NAVY, 1.3f, root_label(ROOT_ORDER[i]), 1);
        const char *nb = key_degree_name(g_st.key_minor, deg);
        int sel = g_st.chord_on && g_st.root == ROOT_ORDER[i];
        uint32_t nc = deg == 0 && !sel ? C_ORANGE : (sel ? C_LCD_TEXT : C_NAVY_SOFT);
        text_c(x, y + d + 13.0f, nc, 0.72f, nb, 1);
        if (key_degree_quality(g_st.key_minor, deg) == 2)            /* уменьшённая: маленький кружок, как vii° */
            text_at(x + (float)text_w(0.72f, nb) * 0.5f + 1.0f, y + d + 13.0f - 2.0f, nc, 0.5f, "o", 1);
    }
    for (int i = 0; i < N_MODS; i++) {
        float x, y;
        seg_center(i, N_MODS, (MOD_R0 + MOD_R1) / 2.0f + 2.0f, &x, &y);
        float d = is_held(HIT_MOD, i) ? 1.5f : 0.0f;
        text_c(x, y + d, C_NAVY, 0.92f, type_label(chord_type(g_st.minor, i)), 1);
    }
    for (int q = 0; q < 2; q++) {
        float d = is_held(HIT_QUAL, q) ? 1.5f : 0.0f;
        text_c(WHEEL_CX, WHEEL_CY + (q ? 1.0f : -1.0f) * CENTER_R * 0.45f + d, C_NAVY, 1.15f, q ? "min" : "maj", 1);
    }
}

static void draw_header(const UiDebug *dbg) {
    char buf[64];

    text_spaced(58, 32, C_NAVY, 1.2f, 5.0f, "STRUMMER", 1);
    if (dbg->show) {                                 /* отладка - вместо подписи под логотипом */
        if (dbg->error) snprintf(buf, sizeof(buf), "err %08X", (unsigned)dbg->error);
        else snprintf(buf, sizeof(buf), "%.0f fps  v%d  xrun %d", dbg->fps, dbg->voices, dbg->xruns);
        text_at(59, 50, C_DEBUG, 0.6f, buf, 0);
    } else {
        text_spaced(59, 49, C_NAVY, 0.56f, 3.0f, "POCKET CHORD HARP", 1);
    }

    /* MODE */
    static const char *MODE_NAME[N_MODES] = { "Strum", "Pad", "Guitar" };
    static const int MODE_X[N_MODES] = { RADIO_STRUM_X, RADIO_PAD_X, RADIO_GTR_X };
    text_spaced(256, 27, C_NAVY, 0.6f, 1.5f, "MODE", 1);
    for (int m = 0; m < N_MODES; m++) {
        draw_sprite(&g_led[g_st.mode == m], MODE_X[m], RADIO_Y, 0);
        text_at(MODE_X[m] + 11, RADIO_Y + 6, C_NAVY, 0.76f, MODE_NAME[m], 0);
    }

    /* OCTAVE */
    text_spaced(452, 27, C_NAVY, 0.6f, 1.5f, "OCTAVE", 1);
    float oy = OCT_Y + OCT_H / 2.0f;
    text_c(OCT_X + 12, oy, is_held(HIT_OCT, -1) ? C_ORANGE : C_NAVY, 0.95f, "<", 1);
    text_c(OCT_X + OCT_W - 12, oy, is_held(HIT_OCT, 1) ? C_ORANGE : C_NAVY, 0.95f, ">", 1);
    if (g_st.octave > 0) snprintf(buf, sizeof(buf), "+%d", g_st.octave);
    else snprintf(buf, sizeof(buf), "%d", g_st.octave);
    text_c(OCT_X + OCT_W / 2.0f, oy, C_NAVY, 1.0f, buf, 1);

    /* TRANSPOSE (полутоны) */
    text_spaced(TRN_X, 27, C_NAVY, 0.6f, 1.5f, "TRANS", 1);
    text_c(TRN_X + 12, oy, is_held(HIT_TRN, -1) ? C_ORANGE : C_NAVY, 0.95f, "<", 1);
    text_c(TRN_X + TRN_W - 12, oy, is_held(HIT_TRN, 1) ? C_ORANGE : C_NAVY, 0.95f, ">", 1);
    if (g_st.transpose > 0) snprintf(buf, sizeof(buf), "+%d", g_st.transpose);
    else snprintf(buf, sizeof(buf), "%d", g_st.transpose);
    text_c(TRN_X + TRN_W / 2.0f, oy, g_st.transpose ? C_ORANGE : C_NAVY, 1.0f, buf, 1);

    /* табло аккорда; в режиме гитары справа - аппликатура */
    draw_sprite(&g_led[g_st.chord_on], LCD_X + 22, LCD_Y + LCD_H / 2.0f, 0);
    text_spaced(LCD_X + 42, LCD_Y + 13, C_LCD_DIM, 0.48f, 1.5f, "CHORD", 0);
    if (g_st.key_on) {                               /* тональность - в верхней строке табло */
        snprintf(buf, sizeof(buf), "KEY %s%s", root_label(g_st.key_root), g_st.key_minor ? "m" : "");
        text_at(LCD_X + 96, LCD_Y + 14, C_OR_TOP, 0.56f, buf, 1);
    } else {
        text_at(LCD_X + 96, LCD_Y + 14, RGBA8(110, 134, 156, 255), 0.5f, "key off", 0);
    }
    if (g_st.palm) text_at(LCD_X + LCD_W - 58, LCD_Y + 14, C_OR_TOP, 0.56f, "PALM", 1);
    else if (g_st.sus) text_at(LCD_X + LCD_W - 54, LCD_Y + 14, C_OR_TOP, 0.56f, "SUS", 1);
    if (g_st.chord_on) {
        chord_name(buf, sizeof(buf), g_st.root, chord_type(g_st.minor, g_st.mod));
        text_at(LCD_X + 42, LCD_Y + 37, C_LCD_TEXT, 1.3f, buf, 1);
    } else {
        text_at(LCD_X + 42, LCD_Y + 35, C_LCD_DIM, 0.9f, is_guitar() ? "open" : "chromatic", 0);
    }
    if (is_guitar()) {                               /* аппликатура: по струне в своей колонке, от толстой */
        for (int i = 0; i < N_GSTR; i++) {
            int f = g_gch.fret[i];
            if (f < 0) snprintf(buf, sizeof(buf), "x");
            else snprintf(buf, sizeof(buf), "%d", f);
            text_c(LCD_X + LCD_W - 108 + 17.0f * (float)i, LCD_Y + 30, f < 0 ? C_LCD_DIM : C_LCD_TEXT, 0.68f, buf, 1);
        }
    } else if (g_st.chord_on) {
        const char *ln = type_long(chord_type(g_st.minor, g_st.mod));
        text_at(LCD_X + LCD_W - 18 - text_w(0.62f, ln), LCD_Y + 35, C_LCD_DIM, 0.62f, ln, 0);
    }

    /* громкость: в Pad - пэда, в остальных режимах - струн */
    /* громкость пэда (баланс пэда и струн) - только в Pad; в других режимах ручки нет */
    int pad = g_st.mode == MODE_PAD;
    text_spaced(VOL_X + 16, VOL_Y + 15, pad ? C_NAVY : RGBA8(170, 156, 134, 255), 0.5f, 1.3f, "PAD VOL", 1);
    if (pad) {
        float kx = VOL_TX0 + (VOL_TX1 - VOL_TX0) * (float)g_st.vol_pad / 100.0f;
        vita2d_draw_rectangle(VOL_TX0 - 1, VOL_TY - 2, kx - VOL_TX0 + 1, 4, C_ORANGE);
        draw_sprite(&g_knob, kx, VOL_TY, is_held(HIT_VOL, 0));
    }
}

/* быстрые аккорды: название на "кнопке", выбранный - оранжевый */
static void draw_quick(void) {
    char buf[16];
    int cur = g_st.chord_on ? quick_code(g_st.root, g_st.minor, g_st.mod, 0, 0) : -1;   /* октаву и позицию не сравниваем */
    for (int k = 0; k < QS_N; k++) {
        float x, y;
        quick_pos(k, &x, &y);
        int c = g_st.quick[k];
        int r, mi, md, oc, gp, sel = 0;
        if (c) {
            quick_decode(c, &r, &mi, &md, &oc, &gp);
            sel = quick_code(r, mi, md, 0, 0) == cur;
        }
        if (sel) draw_sprite(&g_qs_on, x, y, 0);
        if (!c) { text_c(x, y - 1.0f, RGBA8(176, 160, 136, 255), 0.6f, "-", 0); continue; }
        chord_name(buf, sizeof(buf), r, chord_type(mi, md));
        float sc = 0.56f, w = (float)text_w(sc, buf);
        if (w > QS_W - 3) sc *= (float)(QS_W - 3) / w;
        text_c(x, y - 1.0f, sel ? C_LCD_TEXT : C_NAVY, sc, buf, 1);
    }
}

/* ---------- внешний интерфейс ---------- */
int ui_init(void) {
    g_nfd = 0;
    fluid_init_palette();
    fluid_init_tables();
    memset(&g_st, 0, sizeof(g_st));
    stringmap_build(&g_map, 0, 0, 0, STRING_BASE_NOTE);
    if (!build_all()) return 0;
    g_pgf = vita2d_load_default_pgf();
    create_textures();
    return g_tex_bg != NULL && g_pgf != NULL;
}

void ui_recreate(void) {
    vita2d_wait_rendering_done();
    free_textures();
    create_textures();
    if (g_pgf) vita2d_free_pgf(g_pgf);
    g_pgf = vita2d_load_default_pgf();
}

void ui_set_state(const AppState *s, const StringMap *m) {
    int was_guitar = is_guitar();
    g_st = *s;
    g_map = *m;
    if (is_guitar() != was_guitar) {                 /* сменился набор струн - подсветку с нуля */
        g_nfd = 0;
        fluid_clear_textures();
    }
    if (is_guitar())
        guitar_voicing(&g_gch, g_st.chord_on, g_st.root, chord_type(g_st.minor, g_st.mod), 12 * g_st.octave, g_st.gpos);
    /* клавиши аккорда - по выбранному аккорду (без транспозиции: как на табло) */
    g_khl_want = g_st.chord_on && !is_guitar()
               ? (chord_mask(g_st.root, chord_type(g_st.minor, g_st.mod)) | (g_st.root << 12) | (1 << 16)) : 0;
}

void ui_set_held(const int *kinds, const int *indices, int n) {
    if (n > MAX_FINGERS) n = MAX_FINGERS;
    for (int i = 0; i < n; i++) { g_held_kind[i] = kinds[i]; g_held_idx[i] = indices[i]; }
    g_held_n = n;
}

/* нота пришла по MIDI: струна загорается от клавиши */
void ui_note_glow(int s) {
    if (is_guitar() || s < 0 || s >= N_STRINGS) return;
    fluid_hit(s, (float)(WK_X0 + WK_X1) * 0.5f, 1.0f);
}

void ui_touch_glow(float x0, float y0, float x1, float y1, int down) {
    if (!is_guitar()) {
        /* клавиши и струны страммера: те же хитбоксы, что у звука (чёрные не горят от белых) */
        int cur = strum_key_at(x0, y0), keys[N_STRINGS];
        float fr[N_STRINGS];
        if (down) { cur = strum_key_at(x1, y1); fluid_hit(cur, x1, 1.0f); }
        int n = strum_cross(&cur, x0, y0, x1, y1, keys, fr, N_STRINGS);
        for (int i = 0; i < n; i++) fluid_hit(keys[i], x0 + (x1 - x0) * fr[i], 1.0f);
        fluid_feed(strum_key_at(x1, y1), x1, 0.7f);
        return;
    }
    int s1 = (int)gtr_pos_from_y(y1);
    if (down) fluid_hit(s1, x1, 1.0f);                      /* касание: удар по струне под пальцем */
    /* струна загорается, когда палец пересёк её линию (как звучит) - и при тремоло туда-обратно */
    for (int s = 0; s < N_GSTR; s++) {
        float c = gtr_string_y(s);
        if ((y0 - c) * (y1 - c) < 0.0f) {
            float t = (c - y0) / (y1 - y0);
            fluid_hit(s, x0 + (x1 - x0) * t, 1.0f);
        }
    }
    fluid_feed(s1, x1, 0.7f);                               /* палец на струне подпитывает каплю */
}

int ui_hit(float x, float y, int *index) {
    *index = 0;
    if (g_st.settings) {                         /* страница настроек: только её кнопки */
        *index = -1;
        if (x < SET_X || x > SET_X + SET_W || y < SET_Y || y > SET_Y + SET_H) return HIT_SET;   /* мимо - закрыть */
        if (x > SET_X + SET_W - 70 && y < SET_Y + 64) return HIT_SET;
        for (int k = 0; k < 3; k++) {
            float cy = SET_ROW_Y0 + k * SET_ROW_DY;
            if (fabsf(y - cy) <= 30.0f && x >= SET_X + 20 && x <= SET_X + SET_W - 20) { *index = k; return HIT_SET; }
        }
        *index = -2;                             /* внутри панели, но не кнопка */
        return HIT_SET;
    }
    if (x < LOGO_X1 && y < LOGO_Y1) return HIT_LOGO;
    /* верхняя полоса */
    if (y >= 28 && y <= CAP_Y + CAP_H + 4) {
        if (x >= 248 && x < 320)  { *index = MODE_STRUM; return HIT_MODE; }
        if (x >= 320 && x < 366)  { *index = MODE_PAD; return HIT_MODE; }
        if (x >= 366 && x < 437)  { *index = MODE_GUITAR; return HIT_MODE; }
        if (x >= OCT_X - 6 && x < OCT_X + OCT_W / 2.0f)     { *index = -1; return HIT_OCT; }
        if (x >= OCT_X + OCT_W / 2.0f && x < (OCT_X + OCT_W + TRN_X) / 2.0f) { *index = 1; return HIT_OCT; }
        if (x >= (OCT_X + OCT_W + TRN_X) / 2.0f && x < TRN_X + TRN_W / 2.0f) { *index = -1; return HIT_TRN; }
        if (x >= TRN_X + TRN_W / 2.0f && x < TRN_X + TRN_W + 8) { *index = 1; return HIT_TRN; }
    }
    /* табло: включить / выключить тональность */
    if (x >= LCD_X && x <= LCD_X + LCD_W && y >= LCD_Y - 4 && y <= LCD_Y + LCD_H + 6) return HIT_KEY;
    /* громкость: вся капсула, значение - по положению пальца на дорожке */
    if (g_st.mode == MODE_PAD && x >= VOL_X - 4 && x <= VOL_X + VOL_W + 4 && y >= VOL_Y - 4 && y <= VOL_Y + VOL_H + 8) {
        float v = (x - VOL_TX0) / (float)(VOL_TX1 - VOL_TX0) * 100.0f;
        *index = v < 0.0f ? 0 : (v > 100.0f ? 100 : (int)(v + 0.5f));
        return HIT_VOL;
    }
    /* быстрые аккорды (касание - вызвать) */
    for (int k = 0; k < QS_N; k++) {
        float qx, qy;
        quick_pos(k, &qx, &qy);
        if (fabsf(x - qx) <= QS_W * 0.5f + 1.0f && fabsf(y - qy) <= QS_H * 0.5f + 1.0f) { *index = k; return HIT_QUICK; }
    }
    /* гитара: полоска позиций под грифом */
    if (is_guitar() && y >= GT_POS_Y0 - 4 && y <= GT_POS_Y1 + 6 && x >= GT_NUT_X0 - 6 && x <= GT_AUTO_X1 + 6) {
        *index = -1;
        if (x >= GT_AUTO_X0 - 4) *index = 0;
        else for (int p = 1; p <= GT_POS_MAX; p++) if (x < gtr_fret_x(p)) { *index = p; break; }
        return HIT_GPOS;
    }
    /* струны */
    SdfCarved pl = plate_shape();
    if (sdf_carved(x, y, &pl) < 4.0f) return HIT_PLATE;

    /* колесо: вся долька круга - кнопка */
    float dx = x - WHEEL_CX, dy = y - WHEEL_CY, d = sqrtf(dx * dx + dy * dy);
    if (d > BEZEL_R + 6) return HIT_NONE;
    if (d < CENTER_R + 4) { *index = dy > 0.0f; return HIT_QUAL; }   /* сверху мажор, снизу минор */
    float a = atan2f(dy, dx) * 180.0f / PI_F + 90.0f;    /* от верхней точки по часовой, 0..360 */
    while (a < 0.0f) a += 360.0f;
    while (a >= 360.0f) a -= 360.0f;
    if (d < SEP_R0 + 3) {
        float step = 360.0f / N_MODS;
        *index = (int)floorf((a + step / 2) / step) % N_MODS;
        return HIT_MOD;
    }
    float step = 360.0f / N_ROOTS;
    *index = ROOT_ORDER[(int)floorf((a + step / 2) / step) % N_ROOTS];
    return HIT_ROOT;
}

/* страница настроек поверх всего (GPU: готовые спрайты и текст) */
static void draw_settings(void) {
    vita2d_draw_rectangle(0, 0, 960, 544, RGBA8(14, 24, 36, 150));
    draw_sprite(&g_set_panel, 0, 0, 0);
    text_spaced(SET_X + 34, SET_Y + 40, C_NAVY, 0.95f, 4.0f, "SETTINGS", 1);
    text_c(SET_X + SET_W - 34, SET_Y + 32, is_held(HIT_SET, -1) ? C_ORANGE : C_NAVY, 1.3f, "x", 1);
    static const char *TITLE[3] = { "MIDI IN  (USB)", "MIDI OUT  (USB)", "WIDE SOUND" };
    static const char *HINT[3] = { "notes from computer - chromatic strings (no chord)",
                                   "Strum notes go to computer, channel 1",
                                   "right channel 10 ms later - wider, more space" };
    int val[3] = { g_st.midi_rx, g_st.midi_tx, g_st.wide };
    for (int k = 0; k < 3; k++) {
        float cy = SET_ROW_Y0 + k * SET_ROW_DY;
        draw_sprite(&g_chk[val[k] ? 1 : 0], SET_CHK_X, cy, is_held(HIT_SET, k));
        text_at(SET_CHK_X + 34, cy - 3, C_NAVY, 1.0f, TITLE[k], 1);
        text_at(SET_CHK_X + 34, cy + 17, C_NAVY_SOFT, 0.62f, HINT[k], 0);
    }
    /* состояние USB-MIDI */
    static const char *ST[] = { "MIDI off", "plugin not found: psvita_usb_audio_midi.skprx in ur0:tai",
                                "USB busy: turn off VitaShell USB / other USB plugins",
                                "waiting for USB cable...", "connected to computer" };
    int ms = g_st.midi_state;
    if (ms < 0 || ms > 4) ms = 0;
    uint32_t sc = ms == 4 ? RGBA8(60, 140, 80, 255) : (ms == 3 || ms == 0 ? C_NAVY_SOFT : C_OR_EDGE);
    text_at(SET_X + 34, SET_Y + SET_H - 22, sc, 0.66f, ST[ms], 1);
}

void ui_draw(float dt, const UiDebug *dbg) {
    fx_update(dt);

    vita2d_start_drawing();
    vita2d_clear_screen();
    vita2d_draw_texture(g_tex_bg, 0, 0);
    draw_plate();
    draw_wheel();
    draw_header(dbg);
    draw_quick();
    if (g_st.settings) draw_settings();
    vita2d_end_drawing();
    vita2d_swap_buffers();
}
