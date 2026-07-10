#ifndef MW_VIDEO_H
#define MW_VIDEO_H

#include "mw_types.h"
#include <SDL.h>

/* The original supported 7 VGA modes. We render to a logical framebuffer
 * at 640x480 (the most common mode) and let SDL scale to the window. */
#define LOGICAL_W  640
#define LOGICAL_H  480

/* VGA 256-color palette */
typedef struct {
    u8 r, g, b;
} PaletteEntry;

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *framebuffer;

    u8  pixels[LOGICAL_W * LOGICAL_H];  /* 8-bit indexed framebuffer */
    PaletteEntry palette[256];
    int dirty;                          /* needs redraw */

    /* Bitmap font (remap-indexed, 46 slots matching original game's table) */
    u8  *font_data;
    int  font_char_w;
    int  font_char_h;
    int  font_advance;
    int  font_slots;
    int  font_remap[128];
} Video;

int  video_init(Video *v, const char *title, int scale);
void video_shutdown(Video *v);
void video_present(Video *v);

/* Palette */
void video_set_palette(Video *v, int index, u8 r, u8 g, u8 b);
void video_load_vga_default_palette(Video *v);

/* Drawing primitives — replacements for the 6 VGA dispatch functions */
void video_clear(Video *v, u8 color);
void video_put_pixel(Video *v, int x, int y, u8 color);
u8   video_get_pixel(Video *v, int x, int y);
void video_fill_rect(Video *v, int x, int y, int w, int h, u8 color);
void video_hline(Video *v, int x, int y, int w, u8 color);
void video_vline(Video *v, int x, int y, int h, u8 color);

/* Bitmap blitting (for .PIC image data) */
void video_blit(Video *v, int dx, int dy, int w, int h,
                const u8 *src, int src_stride);

/* Text rendering */
int  video_load_font(Video *v, const char *path);
void video_draw_char(Video *v, int x, int y, char ch, u8 color);
void video_draw_text(Video *v, int x, int y, const char *str, u8 color);
void video_draw_char_scaled(Video *v, int x, int y, char ch, u8 color, int sn, int sd);
void video_draw_text_scaled(Video *v, int x, int y, const char *str, u8 color, int sn, int sd);

#endif /* MW_VIDEO_H */
