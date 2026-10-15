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
static Canvas g_cv_fluid;
static vita2d_texture *g_tex_fluid[N_FLUID_TEX];
static int g_fluid_next = 0;
/* пиксели каждой струны (клавиша + нить + полоса): индекс в панели и покрытие 0..255 */
typedef struct { uint32_t idx; uint8_t cov; } StrPix;
static StrPix *g_spix[N_STRINGS];
static int g_spix_n[N_STRINGS];
static Sprite g_root_spr[N_ROOTS][2], g_mod_spr[N_MODS][ST_N], g_center_spr, g_led[2];
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
    cv_rect(c, 506, 22, 96, 1, RGBA8(120, 110, 96, 200));                   /* линия после OCTAVE */
    cv_rect(c, 438, 18, 1, 32, RGBA8(150, 132, 108, 200));                  /* разделитель */
    cv_rect(c, 439, 18, 1, 32, WHITE_A(160));
    SdfRRect oct = { OCT_X, OCT_Y, OCT_W, OCT_H, OCT_H / 2.0f };
    Box bb_oct = { OCT_X, OCT_Y, OCT_X + OCT_W, OCT_Y + OCT_H };
    bevel(c, sdf_rrect, &oct, bb_oct, &SK_CREAM, 2.0f);

    /* табло аккорда */
    SdfRRect lcd = { LCD_X, LCD_Y, LCD_W, LCD_H, LCD_H / 2.0f };
    Box bb_lcd = { LCD_X, LCD_Y, LCD_X + LCD_W, LCD_Y + LCD_H };
    engrave(c, sdf_rrect, &lcd, bb_lcd, RGBA8(78, 102, 126, 255), RGBA8(58, 80, 102, 255), 1);

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

static int make_center_sprite(Sprite *sp) {
    Box bb = { WHEEL_CX - CENTER_R, WHEEL_CY - CENTER_R, WHEEL_CX + CENTER_R, WHEEL_CY + CENTER_R };
    if (!sprite_alloc(sp, bb, 10)) return 0;
    SdfCircle cc = { WHEEL_CX - sp->x, WHEEL_CY - sp->y, CENTER_R };
    Skin sk = { RGBA8(30, 48, 66, 255), RGBA8(92, 120, 144, 255), RGBA8(58, 84, 106, 255) };
    Box lb = box_shift(bb, -sp->x, -sp->y);
    bevel(&sp->cv, sdf_circle, &cc, lb, &sk, 3.5f);
    SdfRing rim = { cc.cx, cc.cy, CENTER_R - 10.0f, CENTER_R - 8.5f };
    cv_sdf_fill(&sp->cv, sdf_ring, &rim, lb, 0, 0, 0, 0, RGBA8(36, 56, 76, 160), RGBA8(36, 56, 76, 160));
    cv_star(&sp->cv, cc.cx, cc.cy + 1.0f, 23, 6.0f, DARK_A(70));
    cv_star(&sp->cv, cc.cx, cc.cy, 23, 6.0f, RGBA8(240, 230, 210, 255));
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


/* Какие пиксели панели принадлежат какой струне. Каждая струна рисуется отдельно в маску;
 * пиксель на стыке двух струн достаётся той, что покрывает его сильнее. */
static int build_string_pixels(void) {
    Canvas mask;
    uint8_t *own_cov = (uint8_t *)calloc((size_t)PLATE_W * PLATE_H, 1);
    int8_t *own = (int8_t *)malloc((size_t)PLATE_W * PLATE_H);
    if (!own_cov || !own || !canvas_alloc(&mask, PLATE_W, PLATE_H)) return 0;
    memset(own, -1, (size_t)PLATE_W * PLATE_H);
    for (int i = 0; i < N_STRINGS; i++) {
        float ya = key_center_y(i), yb = strum_string_y(i);
        int y0 = (int)fminf(ya, yb) - 16 - PLATE_Y, y1 = (int)fmaxf(ya, yb) + 16 - PLATE_Y;
        if (y0 < 0) y0 = 0;
        if (y1 > PLATE_H) y1 = PLATE_H;
        for (int y = y0; y < y1; y++) memset(mask.px + (size_t)y * PLATE_W, 0, (size_t)PLATE_W * 4);
        paint_string(&mask, PLATE_X, PLATE_Y, i, 0.0f, RGBA8(255, 255, 255, 255));
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < PLATE_W; x++) {
                int k = y * PLATE_W + x;
                unsigned c = mask.px[k] >> 24;
                if (c > own_cov[k]) { own_cov[k] = (uint8_t)c; own[k] = (int8_t)i; }
            }
    }
    for (int k = 0; k < PLATE_W * PLATE_H; k++) if (own[k] >= 0) g_spix_n[own[k]]++;
    for (int i = 0; i < N_STRINGS; i++) {
        g_spix[i] = (StrPix *)malloc(sizeof(StrPix) * (size_t)(g_spix_n[i] ? g_spix_n[i] : 1));
        if (!g_spix[i]) return 0;
        g_spix_n[i] = 0;
    }
    for (int k = 0; k < PLATE_W * PLATE_H; k++)
        if (own[k] >= 0) { StrPix *p = &g_spix[own[k]][g_spix_n[own[k]]++]; p->idx = (uint32_t)k; p->cov = own_cov[k]; }
    free(mask.px);
    free(own);
    free(own_cov);
    return 1;
}

static int build_all(void) {
    if (!canvas_alloc(&g_cv_bg, SCR_W, SCR_H)) return 0;
    paint_bg(&g_cv_bg);

    paint_strings(&g_cv_bg, 0, 0, &PS_NORMAL);
    if (!build_string_pixels()) return 0;
    if (!canvas_alloc(&g_cv_fluid, PLATE_W, PLATE_H)) return 0;

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
    if (!make_center_sprite(&g_center_spr)) return 0;
    if (!make_led(&g_led[0], 0) || !make_led(&g_led[1], 1)) return 0;
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
    if (g_center_spr.tex) vita2d_free_texture(g_center_spr.tex);
    g_center_spr.tex = NULL;
    for (int k = 0; k < 2; k++) { if (g_led[k].tex) vita2d_free_texture(g_led[k].tex); g_led[k].tex = NULL; }
}

static void create_textures(void) {
    g_tex_bg = tex_from(&g_cv_bg);
    for (int v = PL_LIT; v < PL_N; v++) g_tex_plate[v] = tex_from(&g_cv_plate[v]);
    for (int i = 0; i < N_ROOTS; i++) for (int k = 0; k < 2; k++) g_root_spr[i][k].tex = tex_from(&g_root_spr[i][k].cv);
    for (int i = 0; i < N_MODS; i++) for (int k = 0; k < ST_N; k++) g_mod_spr[i][k].tex = tex_from(&g_mod_spr[i][k].cv);
    g_center_spr.tex = tex_from(&g_center_spr.cv);
    for (int k = 0; k < N_FLUID_TEX; k++) g_tex_fluid[k] = vita2d_create_empty_texture(PLATE_W, PLATE_H);
    for (int k = 0; k < 2; k++) g_led[k].tex = tex_from(&g_led[k].cv);
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
 * Цвет зависит от густоты, как слои на постерах Retoka: густое ядро вишнёвое, дальше
 * красный, оранжевый, янтарь, бледно-жёлтый, а на самом фронте - бирюза.
 * Поле - по клеткам FL_CELL пикселей вдоль x (путь струны идёт слева направо), между
 * клетками плавно; рисуется в полном разрешении точно по пикселям своей струны. */
#define FL_X0 ((float)BK_X0 - 4.0f)
#define FL_X1 ((float)BAR_X1 + 4.0f)
#define FL_CELL 4.0f
#define FL_NC ((BAR_X1 - BK_X0 + 8) / 4 + 2)       /* клеток по 4 пикселя */
#define MAX_DROPS 160

typedef struct { int s; float x, v, w, amt; int feed; } FDrop;
static FDrop g_fd[MAX_DROPS];
static int g_nfd = 0;
static unsigned char g_str_active[N_STRINGS];     /* струна горела в прошлом кадре */

/* палитра по густоте 0..1 и прозрачность (таблицы на 256 значений) */
static uint32_t g_pal_lut[256];
static void fluid_init_palette(void) {
    static const float T[7] = { 0.00f, 0.10f, 0.28f, 0.48f, 0.66f, 0.84f, 1.00f };
    static const int C[7][3] = { { 120, 212, 206 },   /* бирюзовый фронт */
                                 { 250, 232, 168 },   /* бледно-жёлтый */
                                 { 250, 178, 72 },    /* янтарь */
                                 { 243, 112, 34 },    /* оранжевый */
                                 { 214, 54, 38 },     /* красный */
                                 { 168, 32, 52 },     /* вишнёвый */
                                 { 110, 26, 58 } };   /* глубокое ядро */
    for (int i = 0; i < 256; i++) {
        float v = (float)i / 255.0f;
        int k = 0;
        while (k < 5 && v > T[k + 1]) k++;
        float t = (v - T[k]) / (T[k + 1] - T[k]);
        int r = (int)(C[k][0] + (C[k + 1][0] - C[k][0]) * t);
        int g = (int)(C[k][1] + (C[k + 1][1] - C[k][1]) * t);
        int b = (int)(C[k][2] + (C[k + 1][2] - C[k][2]) * t);
        float a = v / 0.16f;                         /* фронт мягко проявляется */
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
static void fluid_hit(int s, float x, float amt) {
    x = clampx(x);
    FDrop *d;
    d = fdrop_new(); d->s = s; d->x = x; d->v = 0.0f;    d->w = 12.0f; d->amt = amt;         d->feed = 0;
    d = fdrop_new(); d->s = s; d->x = x; d->v = -430.0f; d->w = 9.0f;  d->amt = amt * 0.75f; d->feed = 0;
    d = fdrop_new(); d->s = s; d->x = x; d->v = 430.0f;  d->w = 9.0f;  d->amt = amt * 0.75f; d->feed = 0;
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
    float vk = expf(-dt / 0.22f), ak = expf(-dt / 0.40f);
    for (int i = 0; i < g_nfd; i++) {
        FDrop *d = &g_fd[i];
        d->x += d->v * dt;
        if (d->x < FL_X0 || d->x > FL_X1) { d->x = clampx(d->x); d->v = 0.0f; }
        d->v *= vk;
        d->w += (58.0f - d->w) * (1.0f - expf(-dt / 0.5f));   /* расплывается */
        if (d->feed == 2) d->feed = 1;                          /* подпитка держит, пока палец здесь */
        else { d->feed = 0; d->amt *= ak; }
        if (d->amt > 0.015f) g_fd[n++] = *d;
    }
    g_nfd = n;
}

/* поле одной струны по клеткам */
static void fluid_cells(int s, float *cell) {
    for (int c = 0; c < FL_NC; c++) cell[c] = 0.0f;
    for (int i = 0; i < g_nfd; i++) {
        const FDrop *d = &g_fd[i];
        if (d->s != s) continue;
        float reach = 2.6f * d->w;
        int c0 = (int)((d->x - reach - FL_X0) / FL_CELL), c1 = (int)((d->x + reach - FL_X0) / FL_CELL) + 1;
        if (c0 < 0) c0 = 0;
        if (c1 > FL_NC) c1 = FL_NC;
        float inv = 1.0f / d->w;
        for (int c = c0; c < c1; c++) {
            float t = (FL_X0 + (float)c * FL_CELL - d->x) * inv;
            cell[c] += d->amt * expf(-t * t);
        }
    }
    /* мягкое насыщение: слияние капель не "выжигает" цвет */
    for (int c = 0; c < FL_NC; c++) cell[c] = 1.0f - expf(-1.25f * cell[c]);
}

/* перерисовать подсветку; 0 - ничего не горит */
static int fluid_render(void) {
    static float cell[FL_NC + 1];
    int any = 0;
    for (int s = 0; s < N_STRINGS; s++) {
        int has = 0;
        for (int i = 0; i < g_nfd && !has; i++) has = g_fd[i].s == s;
        if (!has) {
            if (g_str_active[s]) {                     /* погасла - стираем её пиксели */
                for (int k = 0; k < g_spix_n[s]; k++) g_cv_fluid.px[g_spix[s][k].idx] = 0;
                g_str_active[s] = 0;
            }
            continue;
        }
        any = 1;
        g_str_active[s] = 1;
        fluid_cells(s, cell);
        cell[FL_NC] = cell[FL_NC - 1];
        for (int k = 0; k < g_spix_n[s]; k++) {
            const StrPix *p = &g_spix[s][k];
            float fx = ((float)(p->idx % PLATE_W) + PLATE_X + 0.5f - FL_X0) / FL_CELL;
            int c = (int)fx;
            if (c < 0) c = 0;
            if (c >= FL_NC) c = FL_NC - 1;
            float t = fx - (float)c;
            float v = cell[c] + (cell[c + 1] - cell[c]) * t;
            uint32_t col = g_pal_lut[(int)(v * 255.0f)];
            unsigned a = ((col >> 24) * p->cov) / 255u;
            g_cv_fluid.px[p->idx] = (col & 0x00FFFFFFu) | (a << 24);
        }
    }
    return any;
}

static void plate_span(const float br[4], float a, float b, unsigned alpha) {
    if (b - a < 0.5f || alpha == 0) return;
    float r[4] = { a, br[1], b - a, br[3] };
    draw_plate_part(PL_LIT, r, alpha);
}

static void draw_plate(void) {
    /* в аккорде слабо горят струны с тоникой */
    if (g_st.chord_on)
        for (int i = 0; i < N_STRINGS; i++) {
            if (((g_map.note[i] % 12 + 12) % 12) != g_st.root) continue;
            float br[4];
            bar_rect(i, br);
            plate_span(br, BULB_X1 - 30.0f, BAR_X1 + 1.0f, 55);
        }
    if (!fluid_render() || !g_tex_fluid[0]) return;
    vita2d_texture *t = g_tex_fluid[g_fluid_next];
    g_fluid_next = (g_fluid_next + 1) % N_FLUID_TEX;
    upload(t, &g_cv_fluid);
    vita2d_draw_texture(t, PLATE_X, PLATE_Y);
}

static void draw_wheel(void) {
    for (int i = 0; i < N_ROOTS; i++) {
        int pc = ROOT_ORDER[i];
        int sel = g_st.chord_on && g_st.root == pc;
        draw_sprite(&g_root_spr[i][sel ? ST_SEL : ST_NORMAL], 0, 0, is_held(HIT_ROOT, pc));
    }
    for (int i = 0; i < N_MODS; i++) {
        int st = ST_NORMAL;
        if (g_st.mod == i) st = g_st.chord_on ? ST_SEL : ST_ARMED;
        draw_sprite(&g_mod_spr[i][st], 0, 0, is_held(HIT_MOD, i));
    }
    draw_sprite(&g_center_spr, 0, 0, is_held(HIT_CENTER, 0));

    for (int i = 0; i < N_ROOTS; i++) {
        float x, y;
        seg_center(i, N_ROOTS, (ROOT_R0 + ROOT_R1) / 2.0f, &x, &y);
        float d = is_held(HIT_ROOT, ROOT_ORDER[i]) ? 1.5f : 0.0f;
        text_c(x, y + d, C_NAVY, 1.45f, root_label(ROOT_ORDER[i]), 1);
    }
    for (int i = 0; i < N_MODS; i++) {
        float x, y;
        seg_center(i, N_MODS, (MOD_R0 + MOD_R1) / 2.0f + 2.0f, &x, &y);
        float d = is_held(HIT_MOD, i) ? 1.5f : 0.0f;
        text_c(x, y + d, C_NAVY, 0.92f, mod_label(i), 1);
    }
}

static void draw_header(const UiDebug *dbg) {
    char buf[64];

    text_spaced(58, 32, C_NAVY, 1.2f, 5.0f, "STRUMMER", 1);
    text_spaced(59, 49, C_NAVY, 0.56f, 3.0f, "POCKET CHORD HARP", 1);

    /* MODE */
    text_spaced(256, 27, C_NAVY, 0.6f, 1.5f, "MODE", 1);
    draw_sprite(&g_led[g_st.mode == MODE_STRUM], RADIO_STRUM_X, RADIO_Y, 0);
    draw_sprite(&g_led[g_st.mode == MODE_PAD], RADIO_PAD_X, RADIO_Y, 0);
    text_at(RADIO_STRUM_X + 13, RADIO_Y + 6, C_NAVY, 0.82f, "Strum", 0);
    text_at(RADIO_PAD_X + 13, RADIO_Y + 6, C_NAVY, 0.82f, "Pad", 0);

    /* OCTAVE */
    text_spaced(452, 27, C_NAVY, 0.6f, 1.5f, "OCTAVE", 1);
    float oy = OCT_Y + OCT_H / 2.0f;
    text_c(OCT_X + 16, oy, is_held(HIT_OCT, -1) ? C_ORANGE : C_NAVY, 0.95f, "<", 1);
    text_c(OCT_X + OCT_W - 16, oy, is_held(HIT_OCT, 1) ? C_ORANGE : C_NAVY, 0.95f, ">", 1);
    if (g_st.octave > 0) snprintf(buf, sizeof(buf), "+%d", g_st.octave);
    else snprintf(buf, sizeof(buf), "%d", g_st.octave);
    text_c(OCT_X + OCT_W / 2.0f, oy, C_NAVY, 1.0f, buf, 1);

    /* табло аккорда */
    draw_sprite(&g_led[g_st.chord_on], LCD_X + 22, LCD_Y + LCD_H / 2.0f, 0);
    text_spaced(LCD_X + 42, LCD_Y + 13, C_LCD_DIM, 0.48f, 1.5f, "CHORD", 0);
    if (g_st.chord_on) {
        chord_name(buf, sizeof(buf), g_st.root, g_st.mod);
        text_at(LCD_X + 42, LCD_Y + 37, C_LCD_TEXT, 1.3f, buf, 1);
        const char *ln = mod_long(g_st.mod);
        text_at(LCD_X + LCD_W - 18 - text_w(0.62f, ln), LCD_Y + 35, C_LCD_DIM, 0.62f, ln, 0);
    } else {
        text_at(LCD_X + 42, LCD_Y + 35, C_LCD_DIM, 0.9f, "chromatic", 0);
    }

    if (dbg->show) {
        snprintf(buf, sizeof(buf), "%.0f fps  v%d", dbg->fps, dbg->voices);
        text_at(856, 29, C_DEBUG, 0.6f, buf, 0);
        snprintf(buf, sizeof(buf), "xrun %d", dbg->xruns);
        if (dbg->error) snprintf(buf, sizeof(buf), "err %08X", (unsigned)dbg->error);
        text_at(856, 46, C_DEBUG, 0.6f, buf, 0);
    }
}

/* ---------- внешний интерфейс ---------- */
int ui_init(void) {
    g_nfd = 0;
    memset(g_str_active, 0, sizeof(g_str_active));
    fluid_init_palette();
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
    g_st = *s;
    g_map = *m;
}

void ui_set_held(const int *kinds, const int *indices, int n) {
    if (n > MAX_FINGERS) n = MAX_FINGERS;
    for (int i = 0; i < n; i++) { g_held_kind[i] = kinds[i]; g_held_idx[i] = indices[i]; }
    g_held_n = n;
}

void ui_flash_string(int s, float x) {
    if (s < 0 || s >= N_STRINGS) return;
    fluid_hit(s, x, 1.0f);
}

void ui_touch_glow(float x0, float y0, float x1, float y1) {
    /* подпитать все струны, через которые палец прошёл за кадр */
    float p0 = strum_pos_from_xy(x0, y0), p1 = strum_pos_from_xy(x1, y1);
    int s0 = (int)p0, s1 = (int)p1;
    int step = s1 >= s0 ? 1 : -1;
    for (int s = s0 + step; s != s1 + step && s0 != s1; s += step) {
        if (s == s1) break;
        float t = (((float)s + 0.5f) - p0) / (p1 - p0);    /* где палец пересёк струну */
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        FDrop *d = fdrop_new();                             /* пройденная по пути: спокойная капля */
        d->s = s; d->x = clampx(x0 + (x1 - x0) * t); d->v = 0.0f; d->w = 10.0f; d->amt = 0.4f; d->feed = 0;
    }
    fluid_feed(s1, x1, 0.7f);                               /* струна под пальцем */
}

int ui_hit(float x, float y, int *index) {
    *index = 0;
    /* верхняя полоса */
    if (y >= 28 && y <= CAP_Y + CAP_H + 4) {
        if (x >= 250 && x < 342)  { *index = MODE_STRUM; return HIT_MODE; }
        if (x >= 342 && x < 436)  { *index = MODE_PAD; return HIT_MODE; }
        if (x >= OCT_X - 6 && x < OCT_X + OCT_W / 2.0f)     { *index = -1; return HIT_OCT; }
        if (x >= OCT_X + OCT_W / 2.0f && x < OCT_X + OCT_W + 8) { *index = 1; return HIT_OCT; }
    }
    /* струны */
    SdfCarved pl = plate_shape();
    if (sdf_carved(x, y, &pl) < 4.0f) return HIT_PLATE;

    /* колесо: вся долька круга - кнопка */
    float dx = x - WHEEL_CX, dy = y - WHEEL_CY, d = sqrtf(dx * dx + dy * dy);
    if (d > BEZEL_R + 6) return HIT_NONE;
    if (d < CENTER_R + 4) return HIT_CENTER;
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

void ui_draw(float dt, const UiDebug *dbg) {
    fx_update(dt);

    vita2d_start_drawing();
    vita2d_clear_screen();
    vita2d_draw_texture(g_tex_bg, 0, 0);
    draw_plate();
    draw_wheel();
    draw_header(dbg);
    vita2d_end_drawing();
    vita2d_swap_buffers();
}
