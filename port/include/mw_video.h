#ifndef MW_VIDEO_H
#define MW_VIDEO_H

#include "mw_types.h"
#include <SDL.h>

/* Native match for the original game's 1024x768x256 SVGA mode.  SDL only
 * expands the indexed pixels to ARGB for presentation; all game drawing is
 * still performed against an 8-bit, 256-entry logical framebuffer. */
#define LOGICAL_W  1024
#define LOGICAL_H  768

/* WORLD func_06095 contains twelve genuinely different driver branches.
 * Several share a resolution, but they do not share a renderer: the three
 * 320x200 paths are 4-, 16-, and 256-colour implementations, for example.
 * Keep the enum numerically identical to WORLD's video_mode value. */
typedef enum {
    MW_DISPLAY_HERCULES_720X348 = 0, /* WORLD mode 0 */
    MW_DISPLAY_CGA_320X200,          /* WORLD mode 1 */
    MW_DISPLAY_EGA_320X200,          /* WORLD mode 2 */
    MW_DISPLAY_VGA_320X200,          /* WORLD mode 3 */
    MW_DISPLAY_VGA_360X480,          /* WORLD mode 4 */
    MW_DISPLAY_EGA_640X350,          /* WORLD mode 5 */
    MW_DISPLAY_VGA_640X480_16,       /* WORLD mode 6 */
    MW_DISPLAY_SVGA_800X600_16,      /* WORLD mode 7 */
    MW_DISPLAY_SVGA_1024X768_16,     /* WORLD mode 8 */
    MW_DISPLAY_SVGA_1024X768_256_A,  /* WORLD mode 9 */
    MW_DISPLAY_SVGA_1024X768_256_B,  /* WORLD mode 10 */
    MW_DISPLAY_VESA_640X480_256,     /* WORLD mode 11 */
    MW_DISPLAY_MODE_COUNT
} MwDisplayMode;

enum {
    MW_PALETTE_MONO = 1,
    MW_PALETTE_CGA4 = 4,
    MW_PALETTE_EGA16 = 16,
    MW_PALETTE_VGA256 = 256
};

enum {
    MW_WALL_HERCULES = 0,
    MW_WALL_CGA,
    MW_WALL_PLANAR16,
    MW_WALL_CHUNKY256
};

typedef struct {
    const char *resolution;
    const char *adapter;
    int raster_w;
    int raster_h;
    int window_w;
    int window_h;
    int world_mode;
    int monochrome;
    int palette_colors;  /* native adapter palette size */
    int wall_style;      /* one of MW_WALL_* */
    int font_family;     /* 0=320X200, 1=360X480, 2=high-resolution */
    int map_cell_px;     /* literal WORLD DS:4488 native-pixel value */
    int map_cols;        /* literal WORLD DS:4489 visible cell count */
    int map_rows;        /* literal WORLD DS:448A visible cell count */
    int expanded_map_cell_px; /* WORLD func_086F1(...,0), DS:4488 */
    int wilderness_baseline;  /* WORLD DS:CD7A, native raster y */
    int wilderness_x_step;    /* native pixels per terrain sample */
    int wilderness_y_step;    /* native pixels per terrain sample */
    int wilderness_height_num;/* native height multiplier numerator */
    int wilderness_height_den;/* native height multiplier denominator */
} MwDisplayModeInfo;

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
    int display_mode;                    /* one of MwDisplayMode */
    int output_w;                        /* selected original raster width */
    int output_h;                        /* selected original raster height */
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
const MwDisplayModeInfo *video_display_mode_info(int mode);
int  video_set_display_mode(Video *v, int mode, int resize_window);
int  video_display_mode_self_test(Video *v);

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
