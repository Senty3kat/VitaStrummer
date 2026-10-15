/* Программная отрисовка в буфер пикселей со сглаживанием (из VitaSampler) + "капли" для кнопок.
 * Рисуется процессором только при изменениях, потом целиком копируется в текстуру. */
#ifndef CANVAS_H
#define CANVAS_H

#include <stdint.h>

#ifndef RGBA8
#define RGBA8(r, g, b, a) ((((a) & 0xFF) << 24) | (((b) & 0xFF) << 16) | (((g) & 0xFF) << 8) | (((r) & 0xFF) << 0))
#endif

typedef struct { uint32_t *px; int w, h; } Canvas;

/* форма "капли": радиус по углу r(t) = r * (1 + a2*cos(2t + p2) + a3*cos(3t + p3)) */
typedef struct { float a2, p2, a3, p3; } BlobShape;

void cv_clear(Canvas *c, uint32_t col);
void cv_rect(Canvas *c, int x, int y, int w, int h, uint32_t col);
void cv_rrect(Canvas *c, float x, float y, float w, float h, float r, uint32_t col);
void cv_rrect_grad(Canvas *c, float x, float y, float w, float h, float r, uint32_t top, uint32_t bot);
void cv_rrect_stroke(Canvas *c, float x, float y, float w, float h, float r, float t, uint32_t col);
void cv_circle(Canvas *c, float cx, float cy, float r, uint32_t col);
void cv_ring(Canvas *c, float cx, float cy, float r, float t, uint32_t col);
void cv_tri(Canvas *c, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t col);
void cv_noise(Canvas *c, int x, int y, int w, int h, int amp);

/* капля с вертикальным градиентом (top -> bot) */
void cv_blob(Canvas *c, float cx, float cy, float r, const BlobShape *s, uint32_t top, uint32_t bot);
/* мягкая тень под каплей */
void cv_blob_shadow(Canvas *c, float cx, float cy, float r, const BlobShape *s, float dy, uint32_t col);

/* плавная "лента" от (x0, y0) к (x1, y1) с толщиной от t0 до t1 (S-образная, как у струн в концепте) */
void cv_ribbon(Canvas *c, float x0, float y0, float t0, float x1, float y1, float t1, uint32_t col);
/* то же с вертикальным градиентом поперёк ленты (сверху top, снизу bot) */
void cv_ribbon_grad(Canvas *c, float x0, float y0, float t0, float x1, float y1, float t1, uint32_t top, uint32_t bot);

/* "прядь" струны: от (x0, y0) толщиной t0 S-образно к высоте y1 (достигает её в x1),
 * толщина начинает расти с доли tstart пути и плавно доходит до t1 в x1; дальше до x2 -
 * ровная полоса толщиной t1 со скруглённым концом. Одна фигура - без швов. */
void cv_strand(Canvas *c, float x0, float y0, float t0, float x1, float y1, float t1, float tstart, float x2, uint32_t col);

/* четырёхлучевая звезда */
void cv_star(Canvas *c, float cx, float cy, float r, float waist, uint32_t col);

/* ---------- фигуры через "расстояние до контура" (SDF) ----------
 * Функция возвращает расстояние со знаком: < 0 внутри фигуры. По ним рисуются
 * объёмные кнопки: тень, кромка, тело с градиентом, блик, внутренняя тень. */
typedef float (*SdfFn)(float x, float y, const void *ctx);
typedef struct { float x0, y0, x1, y1; } Box;

typedef struct { float cx, cy, r; } SdfCircle;
typedef struct { float cx, cy, r0, r1; } SdfRing;
typedef struct { float x, y, w, h, r; } SdfRRect;
typedef struct { SdfRRect rr; float ccx, ccy, cr; } SdfCarved;        /* скруглённый прямоугольник минус круг */
/* кольцевой сегмент: радиусы r0..r1, середина под углом a (радианы, 0 - вправо, по часовой),
 * половина угла ha, зазор gap (ровный по всей длине), скругление углов round */
typedef struct { float cx, cy, r0, r1, a, ha, gap, round; } SdfSector;

float sdf_circle(float x, float y, const void *ctx);
float sdf_ring(float x, float y, const void *ctx);
float sdf_rrect(float x, float y, const void *ctx);
float sdf_carved(float x, float y, const void *ctx);
float sdf_sector(float x, float y, const void *ctx);

/* заливка фигуры. bb - габарит фигуры (для градиента и обхода), grow - раздуть/сжать,
 * (dx, dy) - сдвиг, soft - ширина размытия края (0 - чёткий край со сглаживанием) */
void cv_sdf_fill(Canvas *c, SdfFn f, const void *ctx, Box bb, float grow, float dx, float dy,
                 float soft, uint32_t top, uint32_t bot);
/* внутренняя тень у верхнего края (dy > 0) или нижнего (dy < 0) */
void cv_sdf_inner_shadow(Canvas *c, SdfFn f, const void *ctx, Box bb, float dy, float soft, uint32_t col);

#endif
