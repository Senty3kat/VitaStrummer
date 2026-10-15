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

static void paint_strings(Canvas *c, float ox, float oy, const PlateSkin *k) {
    for (int i = 0; i < N_STRINGS; i++) paint_string(c, ox, oy, i, 1.6f, DARK_A(45));
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

    paint_strings(c, 0, 0, &PS_NORMAL);
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


static int build_all(void) {
    if (!canvas_alloc(&g_cv_bg, SCR_W, SCR_H)) return 0;
    paint_bg(&g_cv_bg);

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
 * Из точки касания расходится мягкое кольцо-рябь, а струны показывают его срез: на панели
 * видна круглая волна, нарезанная струнами (как на эскизах). За фронтом волна гаснет.
 * Каждая струна рисуется кусками "горящей" панели с прозрачностью по яркости ряби. */
#define FX_X0 (BULB_X1 - 30.0f)            /* где на струне может гореть подсветка */
#define FX_X1 (BAR_X1 + 1.0f)
#define FX_SLICE 6.0f                      /* шаг кусков вдоль струны */
#define MAX_RIPPLES 24

typedef struct { float x, y, age, amp, speed, width, tau; } Ripple;
static Ripple g_rip[MAX_RIPPLES];
static int g_nrip = 0;
static float g_trail_t = 0.0f;             /* время с последней ряби следа */

static void ripple_add(float x, float y, float amp, float speed, float width, float tau) {
    Ripple *r;
    if (g_nrip < MAX_RIPPLES) r = &g_rip[g_nrip++];
    else {                                 /* места нет - заменяем самую слабую */
        r = &g_rip[0];
        for (int i = 1; i < MAX_RIPPLES; i++)
            if (g_rip[i].amp * expf(-g_rip[i].age / g_rip[i].tau) < r->amp * expf(-r->age / r->tau)) r = &g_rip[i];
    }
    r->x = x; r->y = y; r->age = 0.0f; r->amp = amp; r->speed = speed; r->width = width; r->tau = tau;
}

/* яркость ряби в точке: мягкая сердцевина + расходящееся кольцо */
static float ripple_at(float x, float y) {
    float b = 0.0f;
    for (int i = 0; i < g_nrip; i++) {
        const Ripple *r = &g_rip[i];
        float dx = x - r->x, dy = y - r->y, d = sqrtf(dx * dx + dy * dy);
        float fade = r->amp * expf(-r->age / r->tau);
        float R = r->speed * r->age, w = r->width + 0.25f * R;
        float ring = (d - R) / w;
        float core = d / (r->width * 1.4f);
        float v = fade * (expf(-ring * ring) + 0.6f * expf(-r->age / (r->tau * 0.4f)) * expf(-core * core));
        if (v > b) b = v;                    /* перекрытия не складываются - видны отдельные кольца */
    }
    if (b > 1.0f) b = 1.0f;
    return powf(b, 0.7f);                    /* насыщеннее в середине волны, мягко у краёв */
}

static void plate_span(const float br[4], float a, float b, unsigned alpha) {
    if (b - a < 0.5f || alpha == 0) return;
    float r[4] = { a, br[1], b - a, br[3] };
    draw_plate_part(PL_LIT, r, alpha);
}

static void draw_plate(void) {
    for (int i = 0; i < N_STRINGS; i++) {
        float br[4];
        bar_rect(i, br);
        /* в аккорде слабо горят струны с тоникой */
        unsigned base_a = (g_st.chord_on && ((g_map.note[i] % 12 + 12) % 12) == g_st.root) ? 55 : 0;
        if (g_nrip == 0) { plate_span(br, FX_X0, FX_X1, base_a); continue; }
        float ys = strum_string_y(i);
        /* соседние куски с одинаковой яркостью склеиваются в один */
        float run_a = FX_X0;
        unsigned run_alpha = 0;
        for (float x = FX_X0; x < FX_X1; x += FX_SLICE) {
            float xe = x + FX_SLICE > FX_X1 ? FX_X1 : x + FX_SLICE;
            unsigned a = (unsigned)(ripple_at((x + xe) * 0.5f, ys) * 255.0f);
            if (a < base_a) a = base_a;
            a &= ~7u;                       /* ступени по 8 - меньше кусков, глазом не видно */
            if (a != run_alpha) { plate_span(br, run_a, x, run_alpha); run_a = x; run_alpha = a; }
        }
        plate_span(br, run_a, FX_X1, run_alpha);
    }
}

static void fx_update(float dt) {
    int n = 0;
    for (int i = 0; i < g_nrip; i++) {
        g_rip[i].age += dt;
        if (g_rip[i].amp * expf(-g_rip[i].age / g_rip[i].tau) > 0.02f) g_rip[n++] = g_rip[i];
    }
    g_nrip = n;
    g_trail_t += dt;
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
    g_nrip = 0;
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

void ui_flash_zone(int zone, float x) {
    if (zone < 0 || zone >= g_map.nzones) return;
    float y = (strum_string_y(g_map.zone_lo[zone]) + strum_string_y(g_map.zone_hi[zone])) * 0.5f;
    if (x < FX_X0) x = FX_X0;                /* струна звенит от места удара (или от начала полосы) */
    ripple_add(x, y, 0.5f, 230.0f, 7.0f, 0.26f);
}

void ui_touch_glow(float x, float y, int down) {
    if (x < FX_X0) x = FX_X0;
    if (down) {                              /* касание: сильный всплеск */
        ripple_add(x, y, 0.95f, 280.0f, 11.0f, 0.38f);
        g_trail_t = 0.0f;
        return;
    }
    if (g_trail_t >= 0.05f) {                /* след за пальцем: небольшая рябь каждые 50 мс */
        ripple_add(x, y, 0.3f, 150.0f, 8.0f, 0.2f);
        g_trail_t = 0.0f;
    }
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
