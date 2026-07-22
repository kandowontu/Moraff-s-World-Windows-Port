#ifndef MW_VIDEO_H
#define MW_VIDEO_H

#include "mw_types.h"
#include <SDL.h>

/* Native match for the original game's 1024x768x256 SVGA mode.  SDL only
 * expands the indexed pixels to ARGB for presentation; all game drawing is
 * still performed against an 8-bit, 256-entry logical framebuffer. */
#define LOGICAL_W  1024
#define LOGICAL_H  768

/* VGA 256-color palette */
typedef struct {
    u8 r, g, b;
} PaletteEntry;

/* Native-only semantic colors.  Keep these above the original renderer's
 * 0x00..0xBF palette range so WORLD's depth-dependent DAC bands remain exact. */
enum {
    MW_COLOR_WALL_FACE = 224,
    MW_COLOR_WALL_HIGHLIGHT,
    MW_COLOR_WALL_CRACK,
    MW_COLOR_CEILING_1,
    MW_COLOR_CEILING_2,
    MW_COLOR_DUNGEON_1,
    MW_COLOR_DUNGEON_2,
    MW_COLOR_DUNGEON_3,
    MW_COLOR_CEILING_3,
    MW_COLOR_FLOOR_1,
    MW_COLOR_FLOOR_2,
    MW_COLOR_FLOOR_3,
    MW_COLOR_FLOOR_4,
    MW_COLOR_FLOOR_5,
    MW_COLOR_MAP_WHITE,
    MW_COLOR_STATUS_CYAN,
    MW_COLOR_PROMPT_ORANGE,
    MW_COLOR_WALL_TINT
};

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *framebuffer;

    u8  pixels[LOGICAL_W * LOGICAL_H];  /* 8-bit indexed framebuffer */
    u32 *argb_pixels;                    /* presentation conversion buffer */
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
void video_load_world_palette(Video *v, int floor,
                              u8 background_r, u8 background_g,
                              u8 background_b);
int  video_world_palette_self_test(void);

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

/* PIC sprite rendering (scanline-table format from WORLD.PIC) */
void video_blit_pic_sprite(Video *v, int cx, int cy, int draw_h,
                           const u8 *pic_data, int pic_size, u8 transparent);

/* Text rendering */
int  video_load_font(Video *v, const char *path);
void video_draw_char(Video *v, int x, int y, char ch, u8 color);
void video_draw_text(Video *v, int x, int y, const char *str, u8 color);
void video_draw_char_scaled(Video *v, int x, int y, char ch, u8 color, int sn, int sd);
void video_draw_text_scaled(Video *v, int x, int y, const char *str, u8 color, int sn, int sd);
void video_draw_char_scaled_xy(Video *v, int x, int y, char ch, u8 color,
                               int xsn, int xsd, int ysn, int ysd);
void video_draw_text_scaled_xy(Video *v, int x, int y, const char *str, u8 color,
                               int xsn, int xsd, int ysn, int ysd);

#endif /* MW_VIDEO_H */
