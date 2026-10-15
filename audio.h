#include "canvas.h"
#include <math.h>

static inline float fclampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* плитки для обхода фигур; TILE_R - полудиагональ с запасом (расстояния у сегментов приблизительные) */
#define TILE 8
#define TILE_R 8.5f

static inline uint32_t lerp_col(uint32_t a, uint32_t b, float t) {
    int ar = a & 255, ag = (a >> 8) & 255, ab = (a >> 16) & 255, aa = a >> 24;
    int br = b & 255, bg = (b >> 8) & 255, bb = (b >> 16) & 255, ba = b >> 24;
    return RGBA8((int)(ar + (br - ar) * t + 0.5f), (int)(ag + (bg - ag) * t + 0.5f),
                 (int)(ab + (bb - ab) * t + 0.5f), (int)(aa + (ba - aa) * t + 0.5f));
}

/* смешивание "поверх" (цвета не предумножены - так же смешивает и GPU) */
static inline void cv_put(Canvas *c, int x, int y, uint32_t src, float cov) {
    if (x < 0 || y < 0 || x >= c->w || y >= c->h || cov <= 0.0f) return;
    float sa = (float)(src >> 24) * (1.0f / 255.0f) * cov;
    if (sa <= 0.0f) return;
    uint32_t *d = &c->px[y * c->w + x];
    if (sa >= 0.998f) { *d = (src & 0x00FFFFFFu) | 0xFF000000u; return; }
    uint32_t dv = *d;
    float da = (float)(dv >> 24) * (1.0f / 255.0f);
    float oa = sa + da * (1.0f - sa);
    float k = da * (1.0f - sa);
    float r = ((float)(src & 255) * sa + (float)(dv & 255) * k) / oa;
    float g = ((float)((src >> 8) & 255) * sa + (float)((dv >> 8) & 255) * k) / oa;
    float b = ((float)((src >> 16) & 255) * sa + (float)((dv >> 16) & 255) * k) / oa;
    *d = RGBA8((int)(r + 0.5f), (int)(g + 0.5f), (int)(b + 0.5f), (int)(oa * 255.0f + 0.5f));
}

void cv_clear(Canvas *c, uint32_t col) {
    for (int i = 0; i < c->w * c->h; i++) c->px[i] = col;
}

void cv_rect(Canvas *c, int x, int y, int w, int h, uint32_t col) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) cv_put(c, i, j, col, 1.0f);
}

/* скруглённый прямоугольник через "расстояние до контура" - сглаженные края.
 * Середина (где покрытие заведомо 100%) закрашивается быстро, математика - только у краёв. */
static void cv_rrect_ex(Canvas *c, float x, float y, float w, float h, float r,
                        uint32_t ctop, uint32_t cbot, float stroke) {
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f - r, hh = h * 0.5f - r;
    int x0 = (int)floorf(x) - 1, x1 = (int)ceilf(x + w) + 1;
    int y0 = (int)floorf(y) - 1, y1 = (int)ceilf(y + h) + 1;
    const int fast = (r >= 1.0f) && (stroke <= 0.0f || r > stroke + 0.5f);
    for (int py = y0; py < y1; py++) {
        float t = fclampf(((float)py + 0.5f - y) / h, 0.0f, 1.0f);
        uint32_t col = (ctop == cbot) ? ctop : lerp_col(ctop, cbot, t);
        float qy = fabsf((float)py + 0.5f - cy) - hh;
        int sk0 = x1, sk1 = x1;
        if (fast && qy < 0.0f) {
            sk0 = (int)ceilf(cx - hw); sk1 = (int)floorf(cx + hw);
            if (sk0 < x0) sk0 = x0;
            if (sk1 > x1) sk1 = x1;
            if (sk1 <= sk0) { sk0 = sk1 = x1; }
            else if (stroke <= 0.0f && py >= 0 && py < c->h) {
                int a0 = sk0 < 0 ? 0 : sk0, a1 = sk1 > c->w ? c->w : sk1;
                if ((col >> 24) == 255) { uint32_t *row = &c->px[py * c->w]; for (int px = a0; px < a1; px++) row[px] = col; }
                else for (int px = a0; px < a1; px++) cv_put(c, px, py, col, 1.0f);
            }
        }
        for (int px = x0; px < x1; px++) {
            if (px >= sk0 && px < sk1) { px = sk1 - 1; continue; }
            float qx = fabsf((float)px + 0.5f - cx) - hw;
            float ox = qx > 0.0f ? qx : 0.0f, oy = qy > 0.0f ? qy : 0.0f;
            float sd = (ox > 0.0f && oy > 0.0f ? sqrtf(ox * ox + oy * oy) : (ox > oy ? ox : oy))
                     + fminf(fmaxf(qx, qy), 0.0f) - r;
            float cov = fclampf(0.5f - sd, 0.0f, 1.0f);
            if (stroke > 0.0f) cov *= fclampf(sd + stroke + 0.5f, 0.0f, 1.0f);
            if (cov > 0.0f) cv_put(c, px, py, col, cov);
        }
    }
}

void cv_rrect(Canvas *c, float x, float y, float w, float h, float r, uint32_t col) { cv_rrect_ex(c, x, y, w, h, r, col, col, 0.0f); }
void cv_rrect_grad(Canvas *c, float x, float y, float w, float h, float r, uint32_t top, uint32_t bot) { cv_rrect_ex(c, x, y, w, h, r, top, bot, 0.0f); }
void cv_rrect_stroke(Canvas *c, float x, float y, float w, float h, float r, float t, uint32_t col) { cv_rrect_ex(c, x, y, w, h, r, col, col, t); }
void cv_circle(Canvas *c, float cx, float cy, float r, uint32_t col) { cv_rrect_ex(c, cx - r, cy - r, 2 * r, 2 * r, r, col, col, 0.0f); }
void cv_ring(Canvas *c, float cx, float cy, float r, float t, uint32_t col) { cv_rrect_ex(c, cx - r, cy - r, 2 * r, 2 * r, r, col, col, t); }

/* треугольник со сглаживанием (супервыборка 4x4) */
void cv_tri(Canvas *c, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t col) {
    int bx0 = (int)floorf(fminf(x0, fminf(x1, x2))) - 1, bx1 = (int)ceilf(fmaxf(x0, fmaxf(x1, x2))) + 1;
    int by0 = (int)floorf(fminf(y0, fminf(y1, y2))) - 1, by1 = (int)ceilf(fmaxf(y0, fmaxf(y1, y2))) + 1;
    float den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    if (fabsf(den) < 1e-6f) return;
    for (int py = by0; py < by1; py++)
        for (int px = bx0; px < bx1; px++) {
            int hit = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float fx = (float)px + (sx + 0.5f) * 0.25f, fy = (float)py + (sy + 0.5f) * 0.25f;
                    float a = ((y1 - y2) * (fx - x2) + (x2 - x1) * (fy - y2)) / den;
                    float b = ((y2 - y0) * (fx - x2) + (x0 - x2) * (fy - y2)) / den;
                    if (a >= 0.0f && b >= 0.0f && 1.0f - a - b >= 0.0f) hit++;
                }
            if (hit) cv_put(c, px, py, col, (float)hit / 16.0f);
        }
}

/* лёгкая "фактура пластика": случайный шум по яркости */
void cv_noise(Canvas *c, int x, int y, int w, int h, int amp) {
    uint32_t seed = 12345u;
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) {
            seed = seed * 1664525u + 1013904223u;
            if (i < 0 || j < 0 || i >= c->w || j >= c->h) continue;
            int n = (int)((seed >> 24) % (uint32_t)(2 * amp + 1)) - amp;
            uint32_t *d = &c->px[j * c->w + i];
            int r = (int)(*d & 255) + n, g = (int)((*d >> 8) & 255) + n, b = (int)((*d >> 16) & 255) + n;
            *d = RGBA8(r < 0 ? 0 : (r > 255 ? 255 : r), g < 0 ? 0 : (g > 255 ? 255 : g),
                       b < 0 ? 0 : (b > 255 ? 255 : b), (int)(*d >> 24));
        }
}

/* ---------- капли ---------- */
static void blob_fill(Canvas *c, float cx, float cy, float r, const BlobShape *s,
                      uint32_t top, uint32_t bot, float soft) {
    float amp = fabsf(s->a2) + fabsf(s->a3);
    float rmin = r * (1.0f - amp), rmax = r * (1.0f + amp);
    float reach = rmax + soft + 1.0f;
    int x0 = (int)floorf(cx - reach), x1 = (int)ceilf(cx + reach);
    int y0 = (int)floorf(cy - reach), y1 = (int)ceilf(cy + reach);
    float inner = rmin - soft - 1.0f;
    float inner2 = inner > 0.0f ? inner * inner : 0.0f, outer2 = reach * reach;
    for (int py = y0; py < y1; py++) {
        float fy = (float)py + 0.5f - cy;
        float t = fclampf((fy + rmax) / (2.0f * rmax), 0.0f, 1.0f);
        uint32_t col = (top == bot) ? top : lerp_col(top, bot, t);
        for (int px = x0; px < x1; px++) {
            float fx = (float)px + 0.5f - cx;
            float d2 = fx * fx + fy * fy;
            if (d2 > outer2) continue;
            if (d2 < inner2) { cv_put(c, px, py, col, 1.0f); continue; }
            float d = sqrtf(d2);
            float th = atan2f(fy, fx);
            float rr = r * (1.0f + s->a2 * cosf(2.0f * th + s->p2) + s->a3 * cosf(3.0f * th + s->p3));
            float sd = d - rr;
            float cov = (soft > 0.0f) ? fclampf(0.5f - sd / soft, 0.0f, 1.0f) : fclampf(0.5f - sd, 0.0f, 1.0f);
            if (cov > 0.0f) cv_put(c, px, py, col, cov);
        }
    }
}

void cv_blob(Canvas *c, float cx, float cy, float r, const BlobShape *s, uint32_t top, uint32_t bot) {
    blob_fill(c, cx, cy, r, s, top, bot, 0.0f);
}

void cv_blob_shadow(Canvas *c, float cx, float cy, float r, const BlobShape *s, float dy, uint32_t col) {
    blob_fill(c, cx, cy + dy, r + 1.0f, s, col, col, 6.0f);
}

/* ---------- лента ---------- */
void cv_ribbon(Canvas *c, float x0, float y0, float t0, float x1, float y1, float t1, uint32_t col) {
    cv_ribbon_grad(c, x0, y0, t0, x1, y1, t1, col, col);
}

void cv_ribbon_grad(Canvas *c, float x0, float y0, float t0, float x1, float y1, float t1, uint32_t top, uint32_t bot) {
    if (x1 <= x0) return;
    int ix0 = (int)floorf(x0), ix1 = (int)ceilf(x1);
    for (int px = ix0; px < ix1; px++) {
        float u = fclampf(((float)px + 0.5f - x0) / (x1 - x0), 0.0f, 1.0f);
        float sm = u * u * (3.0f - 2.0f * u);
        float dsm = 6.0f * u * (1.0f - u) / (x1 - x0);       /* производная по x */
        float yc = y0 + (y1 - y0) * sm;
        float slope = (y1 - y0) * dsm;
        float half = 0.5f * (t0 + (t1 - t0) * sm) * sqrtf(1.0f + slope * slope);
        int py0 = (int)floorf(yc - half) - 1, py1 = (int)ceilf(yc + half) + 1;
        for (int py = py0; py < py1; py++) {
            float cov = fclampf(half - fabsf((float)py + 0.5f - yc) + 0.5f, 0.0f, 1.0f);
            if (cov <= 0.0f) continue;
            uint32_t col = top;
            if (top != bot) col = lerp_col(top, bot, fclampf(((float)py + 0.5f - (yc - half)) / (2.0f * half), 0.0f, 1.0f));
            cv_put(c, px, py, col, cov);
        }
    }
}

void cv_star(Canvas *c, float cx, float cy, float r, float waist, uint32_t col) {
    /* многоугольник: 4 вершины-луча и 4 внутренние точки по диагоналям, веером от центра */
    float tip[4][2], in[4][2];
    for (int k = 0; k < 4; k++) {
        float a = -1.5707963f + (float)k * 1.5707963f;
        tip[k][0] = cx + r * cosf(a);
        tip[k][1] = cy + r * sinf(a);
        in[k][0] = cx + waist * cosf(a + 0.7853982f);
        in[k][1] = cy + waist * sinf(a + 0.7853982f);
    }
    for (int k = 0; k < 4; k++) {
        int n = (k + 1) & 3;
        cv_tri(c, cx, cy, tip[k][0], tip[k][1], in[k][0], in[k][1], col);
        cv_tri(c, cx, cy, in[k][0], in[k][1], tip[n][0], tip[n][1], col);
    }
}

/* ---------- SDF ---------- */
float sdf_circle(float x, float y, const void *ctx) {
    const SdfCircle *s = (const SdfCircle *)ctx;
    float dx = x - s->cx, dy = y - s->cy;
    return sqrtf(dx * dx + dy * dy) - s->r;
}

float sdf_ring(float x, float y, const void *ctx) {
    const SdfRing *s = (const SdfRing *)ctx;
    float dx = x - s->cx, dy = y - s->cy;
    return fabsf(sqrtf(dx * dx + dy * dy) - (s->r0 + s->r1) * 0.5f) - (s->r1 - s->r0) * 0.5f;
}

float sdf_rrect(float x, float y, const void *ctx) {
    const SdfRRect *s = (const SdfRRect *)ctx;
    float r = fminf(s->r, fminf(s->w, s->h) * 0.5f);
    float qx = fabsf(x - (s->x + s->w * 0.5f)) - (s->w * 0.5f - r);
    float qy = fabsf(y - (s->y + s->h * 0.5f)) - (s->h * 0.5f - r);
    float ox = fmaxf(qx, 0.0f), oy = fmaxf(qy, 0.0f);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - r;
}

float sdf_carved(float x, float y, const void *ctx) {
    const SdfCarved *s = (const SdfCarved *)ctx;
    float a = sdf_rrect(x, y, &s->rr);
    float dx = x - s->ccx, dy = y - s->ccy;
    float b = s->cr - sqrtf(dx * dx + dy * dy);
    return fmaxf(a, b);
}

float sdf_sector(float x, float y, const void *ctx) {
    const SdfSector *s = (const SdfSector *)ctx;
    float px = x - s->cx, py = y - s->cy;
    float r = sqrtf(px * px + py * py);
    float d = atan2f(py, px) - s->a;
    while (d > 3.14159265f) d -= 6.2831853f;
    while (d < -3.14159265f) d += 6.2831853f;
    float e = fabsf(d) - s->ha;                       /* угол за боковой линией */
    float qs = (e > 1.5707963f) ? r : r * sinf(e);    /* расстояние до радиальной боковой линии */
    qs += s->gap * 0.5f;
    float qr = fabsf(r - (s->r0 + s->r1) * 0.5f) - (s->r1 - s->r0) * 0.5f;
    float ax = qr + s->round, ay = qs + s->round;
    float ox = fmaxf(ax, 0.0f), oy = fmaxf(ay, 0.0f);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(ax, ay), 0.0f) - s->round;
}

void cv_sdf_fill(Canvas *c, SdfFn f, const void *ctx, Box bb, float grow, float dx, float dy,
                 float soft, uint32_t top, uint32_t bot) {
    float m = fabsf(grow) + soft + 2.0f;
    int x0 = (int)floorf(bb.x0 + fminf(dx, 0.0f) - m), x1 = (int)ceilf(bb.x1 + fmaxf(dx, 0.0f) + m);
    int y0 = (int)floorf(bb.y0 + fminf(dy, 0.0f) - m), y1 = (int)ceilf(bb.y1 + fmaxf(dy, 0.0f) + m);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;
    float gh = (bb.y1 - bb.y0) > 1.0f ? (bb.y1 - bb.y0) : 1.0f;
    float edge = (soft > 0.0f ? soft : 1.0f) + 1.0f;   /* где покрытие уже не 0 и не 1 */
    /* обход плитками: по расстоянию в центре плитки видно, что она целиком внутри
     * или снаружи фигуры - тогда считать каждую точку не нужно */
    for (int ty = y0; ty < y1; ty += TILE) {
        int ty1 = ty + TILE < y1 ? ty + TILE : y1;
        for (int tx = x0; tx < x1; tx += TILE) {
            int tx1 = tx + TILE < x1 ? tx + TILE : x1;
            float sc = f((float)(tx + tx1) * 0.5f - dx, (float)(ty + ty1) * 0.5f - dy, ctx) - grow;
            if (sc > TILE_R + edge) continue;                         /* целиком снаружи */
            int full = sc < -(TILE_R + edge);                         /* целиком внутри */
            for (int py = ty; py < ty1; py++) {
                uint32_t col = top;
                if (top != bot) col = lerp_col(top, bot, fclampf(((float)py + 0.5f - dy - bb.y0) / gh, 0.0f, 1.0f));
                if (full) {
                    if ((col >> 24) == 255) { uint32_t *row = &c->px[py * c->w]; for (int px = tx; px < tx1; px++) row[px] = col; }
                    else for (int px = tx; px < tx1; px++) cv_put(c, px, py, col, 1.0f);
                    continue;
                }
                for (int px = tx; px < tx1; px++) {
                    float sd = f((float)px + 0.5f - dx, (float)py + 0.5f - dy, ctx) - grow;
                    float cov = (soft > 0.0f) ? fclampf(0.5f - sd / soft, 0.0f, 1.0f) : fclampf(0.5f - sd, 0.0f, 1.0f);
                    if (cov > 0.0f) cv_put(c, px, py, col, cov);
                }
            }
        }
    }
}

void cv_sdf_inner_shadow(Canvas *c, SdfFn f, const void *ctx, Box bb, float dy, float soft, uint32_t col) {
    int x0 = (int)floorf(bb.x0) - 1, x1 = (int)ceilf(bb.x1) + 1;
    int y0 = (int)floorf(bb.y0) - 1, y1 = (int)ceilf(bb.y1) + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;
    float deep = TILE_R + fabsf(dy) + soft + 1.0f;     /* глубже этого тени уже нет */
    for (int ty = y0; ty < y1; ty += TILE) {
        int ty1 = ty + TILE < y1 ? ty + TILE : y1;
        for (int tx = x0; tx < x1; tx += TILE) {
            int tx1 = tx + TILE < x1 ? tx + TILE : x1;
            float sc = f((float)(tx + tx1) * 0.5f, (float)(ty + ty1) * 0.5f, ctx);
            if (sc > TILE_R + 1.0f || sc < -deep) continue;          /* снаружи или глубоко внутри */
            for (int py = ty; py < ty1; py++)
                for (int px = tx; px < tx1; px++) {
                    float fx = (float)px + 0.5f, fy = (float)py + 0.5f;
                    float in = fclampf(0.5f - f(fx, fy, ctx), 0.0f, 1.0f);
                    if (in <= 0.0f) continue;
                    float s2 = f(fx, fy - dy, ctx);                 /* фигура, сдвинутая на dy */
                    float a = fclampf((s2 + soft) / soft, 0.0f, 1.0f);
                    if (a > 0.0f) cv_put(c, px, py, col, in * a);
                }
        }
    }
}
