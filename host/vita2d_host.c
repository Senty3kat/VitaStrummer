/* vita2d на обычном компьютере: рисует в память, текст записывает списком,
 * на swap_buffers сохраняет кадр в host/out/<имя>.ppm и <имя>.txt (подписи накладывает preview.py) */
#include <vita2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

struct vita2d_texture { uint32_t *px; unsigned w, h; };
struct vita2d_pgf { int dummy; };

#define FB_W 960
#define FB_H 544
static uint32_t fb[FB_W * FB_H];
static unsigned clear_col = 0xFF000000u;
static FILE *text_out = NULL;
const char *host_frame_name = "frame";

static void blend(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return;
    float a = (float)(c >> 24) / 255.0f;
    uint32_t d = fb[y * FB_W + x];
    int r = (int)((c & 255) * a + (d & 255) * (1 - a));
    int g = (int)(((c >> 8) & 255) * a + ((d >> 8) & 255) * (1 - a));
    int b = (int)(((c >> 16) & 255) * a + ((d >> 16) & 255) * (1 - a));
    fb[y * FB_W + x] = RGBA8(r, g, b, 255);
}

int vita2d_init_advanced(unsigned int temp_pool_size) { (void)temp_pool_size; return 1; }
int vita2d_fini(void) { return 1; }
void vita2d_set_clear_color(unsigned int color) { clear_col = color; }
void vita2d_wait_rendering_done(void) {}

void vita2d_start_drawing(void) {
    char path[256];
    snprintf(path, sizeof(path), "out/%s.txt", host_frame_name);
    text_out = fopen(path, "w");
}
void vita2d_clear_screen(void) { for (int i = 0; i < FB_W * FB_H; i++) fb[i] = clear_col; }
void vita2d_end_drawing(void) { if (text_out) fclose(text_out); text_out = NULL; }

void vita2d_swap_buffers(void) {
    char path[256];
    snprintf(path, sizeof(path), "out/%s.ppm", host_frame_name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", FB_W, FB_H);
    for (int i = 0; i < FB_W * FB_H; i++) {
        unsigned char p[3] = { (unsigned char)(fb[i] & 255), (unsigned char)((fb[i] >> 8) & 255), (unsigned char)((fb[i] >> 16) & 255) };
        fwrite(p, 1, 3, f);
    }
    fclose(f);
}

vita2d_texture *vita2d_create_empty_texture(unsigned int w, unsigned int h) {
    vita2d_texture *t = calloc(1, sizeof(*t));
    t->w = w; t->h = h;
    t->px = calloc((size_t)w * h, 4);
    return t;
}
void vita2d_free_texture(vita2d_texture *t) { if (t) { free(t->px); free(t); } }
void *vita2d_texture_get_datap(const vita2d_texture *t) { return t->px; }
unsigned int vita2d_texture_get_stride(const vita2d_texture *t) { return t->w * 4; }

void vita2d_draw_texture(const vita2d_texture *t, float x, float y) {
    int ix = (int)lroundf(x), iy = (int)lroundf(y);
    for (unsigned j = 0; j < t->h; j++)
        for (unsigned i = 0; i < t->w; i++) blend(ix + (int)i, iy + (int)j, t->px[j * t->w + i]);
}

static uint32_t mul_col(uint32_t a, uint32_t b) {
    int r = (int)((a & 255) * (b & 255) / 255), g = (int)(((a >> 8) & 255) * ((b >> 8) & 255) / 255);
    int bl = (int)(((a >> 16) & 255) * ((b >> 16) & 255) / 255), al = (int)((a >> 24) * (b >> 24) / 255);
    return RGBA8(r, g, bl, al);
}

void vita2d_draw_texture_tint_part(const vita2d_texture *t, float x, float y, float tx, float ty,
                                   float tw, float th, unsigned int color) {
    int ix = (int)lroundf(x), iy = (int)lroundf(y), itx = (int)lroundf(tx), ity = (int)lroundf(ty);
    for (int j = 0; j < (int)lroundf(th); j++)
        for (int i = 0; i < (int)lroundf(tw); i++) {
            int sx = itx + i, sy = ity + j;
            if (sx < 0 || sy < 0 || sx >= (int)t->w || sy >= (int)t->h) continue;
            blend(ix + i, iy + j, mul_col(t->px[sy * t->w + sx], color));
        }
}

void vita2d_draw_texture_tint(const vita2d_texture *t, float x, float y, unsigned int color) {
    vita2d_draw_texture_tint_part(t, x, y, 0, 0, (float)t->w, (float)t->h, color);
}

void vita2d_draw_rectangle(float x, float y, float w, float h, unsigned int color) {
    for (int j = (int)y; j < (int)(y + h); j++)
        for (int i = (int)x; i < (int)(x + w); i++) blend(i, j, color);
}

vita2d_pgf *vita2d_load_default_pgf(void) { return calloc(1, sizeof(vita2d_pgf)); }
void vita2d_free_pgf(vita2d_pgf *font) { free(font); }

/* ширина как у системного шрифта примерно: ~9.6 px на символ при масштабе 1 (оценка из сэмплера) */
int vita2d_pgf_text_width(vita2d_pgf *font, float scale, const char *text) {
    (void)font;
    return (int)((float)strlen(text) * 9.6f * scale);
}

int vita2d_pgf_draw_text(vita2d_pgf *font, int x, int y, unsigned int color, float scale, const char *text) {
    (void)font;
    if (text_out) fprintf(text_out, "%d\t%d\t%u\t%.3f\t%s\n", x, y, color, scale, text);
    return 0;
}
