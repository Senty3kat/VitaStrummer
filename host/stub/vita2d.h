/* хост-заглушка vita2d: сигнатуры как в библиотеке */
#ifndef STUB_VITA2D_H
#define STUB_VITA2D_H
#define RGBA8(r, g, b, a) ((((a) & 0xFF) << 24) | (((b) & 0xFF) << 16) | (((g) & 0xFF) << 8) | (((r) & 0xFF) << 0))
typedef struct vita2d_texture vita2d_texture;
typedef struct vita2d_pgf vita2d_pgf;
int vita2d_init_advanced(unsigned int temp_pool_size);
int vita2d_fini(void);
void vita2d_set_clear_color(unsigned int color);
void vita2d_start_drawing(void);
void vita2d_end_drawing(void);
void vita2d_clear_screen(void);
void vita2d_swap_buffers(void);
void vita2d_wait_rendering_done(void);
vita2d_texture *vita2d_create_empty_texture(unsigned int w, unsigned int h);
void vita2d_free_texture(vita2d_texture *texture);
void *vita2d_texture_get_datap(const vita2d_texture *texture);
unsigned int vita2d_texture_get_stride(const vita2d_texture *texture);
void vita2d_draw_texture(const vita2d_texture *texture, float x, float y);
void vita2d_draw_texture_tint(const vita2d_texture *texture, float x, float y, unsigned int color);
void vita2d_draw_texture_tint_part(const vita2d_texture *texture, float x, float y, float tex_x, float tex_y,
                                   float tex_w, float tex_h, unsigned int color);
void vita2d_draw_rectangle(float x, float y, float w, float h, unsigned int color);
vita2d_pgf *vita2d_load_default_pgf(void);
void vita2d_free_pgf(vita2d_pgf *font);
int vita2d_pgf_draw_text(vita2d_pgf *font, int x, int y, unsigned int color, float scale, const char *text);
int vita2d_pgf_text_width(vita2d_pgf *font, float scale, const char *text);
#endif
