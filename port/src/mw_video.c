#include "mw_video.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Standard VGA 256-color palette (first 16 colors = CGA/EGA compatible) */
static const PaletteEntry vga_default_palette[256] = {
    {0x00,0x00,0x00}, {0x00,0x00,0xAA}, {0x00,0xAA,0x00}, {0x00,0xAA,0xAA},
    {0xAA,0x00,0x00}, {0xAA,0x00,0xAA}, {0xAA,0x55,0x00}, {0xAA,0xAA,0xAA},
    {0x55,0x55,0x55}, {0x55,0x55,0xFF}, {0x55,0xFF,0x55}, {0x55,0xFF,0xFF},
    {0xFF,0x55,0x55}, {0xFF,0x55,0xFF}, {0xFF,0xFF,0x55}, {0xFF,0xFF,0xFF},
    /* 16-31: grayscale ramp */
    {0x00,0x00,0x00}, {0x14,0x14,0x14}, {0x20,0x20,0x20}, {0x2C,0x2C,0x2C},
    {0x38,0x38,0x38}, {0x45,0x45,0x45}, {0x51,0x51,0x51}, {0x61,0x61,0x61},
    {0x71,0x71,0x71}, {0x82,0x82,0x82}, {0x92,0x92,0x92}, {0xA2,0xA2,0xA2},
    {0xB6,0xB6,0xB6}, {0xCB,0xCB,0xCB}, {0xE3,0xE3,0xE3}, {0xFF,0xFF,0xFF},
    /* 32-255: standard VGA color cube + ramps */
    {0x00,0x00,0xFF}, {0x41,0x00,0xFF}, {0x7D,0x00,0xFF}, {0xBE,0x00,0xFF},
    {0xFF,0x00,0xFF}, {0xFF,0x00,0xBE}, {0xFF,0x00,0x7D}, {0xFF,0x00,0x41},
    {0xFF,0x00,0x00}, {0xFF,0x41,0x00}, {0xFF,0x7D,0x00}, {0xFF,0xBE,0x00},
    {0xFF,0xFF,0x00}, {0xBE,0xFF,0x00}, {0x7D,0xFF,0x00}, {0x41,0xFF,0x00},
    {0x00,0xFF,0x00}, {0x00,0xFF,0x41}, {0x00,0xFF,0x7D}, {0x00,0xFF,0xBE},
    {0x00,0xFF,0xFF}, {0x00,0xBE,0xFF}, {0x00,0x7D,0xFF}, {0x00,0x41,0xFF},
    /* Fill rest with grayscale ramp for now — real palette loaded from game */
    [56 ... 255] = {0x80, 0x80, 0x80}
};

int video_init(Video *v, const char *title, int scale) {
    memset(v, 0, sizeof(*v));

    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;

    v->window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        LOGICAL_W * scale, LOGICAL_H * scale,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!v->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }

    v->renderer = SDL_CreateRenderer(v->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!v->renderer) {
        v->renderer = SDL_CreateRenderer(v->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!v->renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }

    SDL_RenderSetLogicalSize(v->renderer, LOGICAL_W, LOGICAL_H);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    v->framebuffer = SDL_CreateTexture(v->renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        LOGICAL_W, LOGICAL_H);
    if (!v->framebuffer) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return -1;
    }

    video_load_vga_default_palette(v);
    v->dirty = 1;
    return 0;
}

void video_shutdown(Video *v) {
    free(v->font_data);
    if (v->framebuffer) SDL_DestroyTexture(v->framebuffer);
    if (v->renderer) SDL_DestroyRenderer(v->renderer);
    if (v->window) SDL_DestroyWindow(v->window);
    memset(v, 0, sizeof(*v));
}

void video_present(Video *v) {
    if (!v->dirty) return;

    u32 argb_buf[LOGICAL_W * LOGICAL_H];
    for (int i = 0; i < LOGICAL_W * LOGICAL_H; i++) {
        PaletteEntry *c = &v->palette[v->pixels[i]];
        argb_buf[i] = 0xFF000000 | ((u32)c->r << 16) | ((u32)c->g << 8) | c->b;
    }

    SDL_UpdateTexture(v->framebuffer, NULL, argb_buf, LOGICAL_W * sizeof(u32));
    SDL_RenderClear(v->renderer);
    SDL_RenderCopy(v->renderer, v->framebuffer, NULL, NULL);
    SDL_RenderPresent(v->renderer);
    v->dirty = 0;
}

void video_set_palette(Video *v, int index, u8 r, u8 g, u8 b) {
    if (index < 0 || index > 255) return;
    v->palette[index].r = r;
    v->palette[index].g = g;
    v->palette[index].b = b;
    v->dirty = 1;
}

void video_load_vga_default_palette(Video *v) {
    memcpy(v->palette, vga_default_palette, sizeof(vga_default_palette));
    v->dirty = 1;
}

void video_clear(Video *v, u8 color) {
    memset(v->pixels, color, sizeof(v->pixels));
    v->dirty = 1;
}

void video_put_pixel(Video *v, int x, int y, u8 color) {
    if (x < 0 || x >= LOGICAL_W || y < 0 || y >= LOGICAL_H) return;
    v->pixels[y * LOGICAL_W + x] = color;
    v->dirty = 1;
}

u8 video_get_pixel(Video *v, int x, int y) {
    if (x < 0 || x >= LOGICAL_W || y < 0 || y >= LOGICAL_H) return 0;
    return v->pixels[y * LOGICAL_W + x];
}

void video_fill_rect(Video *v, int x, int y, int w, int h, u8 color) {
    /* Clip */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LOGICAL_W) w = LOGICAL_W - x;
    if (y + h > LOGICAL_H) h = LOGICAL_H - y;
    if (w <= 0 || h <= 0) return;

    for (int row = y; row < y + h; row++) {
        memset(&v->pixels[row * LOGICAL_W + x], color, w);
    }
    v->dirty = 1;
}

void video_hline(Video *v, int x, int y, int w, u8 color) {
    video_fill_rect(v, x, y, w, 1, color);
}

void video_vline(Video *v, int x, int y, int h, u8 color) {
    video_fill_rect(v, x, y, 1, h, color);
}

void video_blit(Video *v, int dx, int dy, int w, int h,
                const u8 *src, int src_stride) {
    for (int row = 0; row < h; row++) {
        int sy = dy + row;
        if (sy < 0 || sy >= LOGICAL_H) continue;
        for (int col = 0; col < w; col++) {
            int sx = dx + col;
            if (sx < 0 || sx >= LOGICAL_W) continue;
            v->pixels[sy * LOGICAL_W + sx] = src[row * src_stride + col];
        }
    }
    v->dirty = 1;
}

/* ── Font loading and text rendering ── */

/* .FNT format (reverse-engineered from WORLD.EXE func_240C0/func_23FA6):
 *   - File stores 3 font variants concatenated (small, medium, large)
 *   - Variant 2 (largest) sits at the end of the file
 *   - 46 glyph slots per variant, accessed via remap table from DS:0x7F0D
 *   - 16 pixels/row (wpr=1), LSB-first bit order, rows top-to-bottom
 *   - Remap: A-Z=1-26, 0-9=27-36, punct=37-45, space=skip
 */

#define FONT_REMAP_SLOTS 46

static void font_build_remap(int *remap) {
    for (int i = 0; i < 128; i++) remap[i] = -1;
    for (int i = 0; i < 26; i++) remap['A' + i] = 1 + i;
    for (int i = 0; i < 26; i++) remap['a' + i] = 1 + i;
    for (int i = 0; i < 10; i++) remap['0' + i] = 27 + i;
    remap[','] = 37; remap['.'] = 38; remap['?'] = 39; remap['!'] = 40;
    remap['('] = 41; remap[')'] = 42; remap['\''] = 43; remap['$'] = 44;
    remap[':'] = 45;
}

static int font_detect_height(const u8 *data, long fsize) {
    for (int h = 48; h >= 8; h--) {
        int bpc = h * 2;
        long variant_size = (long)FONT_REMAP_SLOTS * bpc;
        if (variant_size > fsize) continue;
        long start = fsize - variant_size;

        long a_off = start + 1 * bpc;
        if (a_off + bpc > fsize) continue;

        int nonempty = 0, trailing_blank = 0;
        for (int row = h - 1; row >= 0; row--) {
            long off = a_off + row * 2;
            u16 word = data[off] | ((u16)data[off + 1] << 8);
            if (word == 0) { if (nonempty == 0) trailing_blank++; }
            else nonempty++;
        }
        if (trailing_blank < 2 || nonempty < h / 2) continue;

        int bits_set = 0;
        for (int row = 0; row < h; row++) {
            long off = a_off + row * 2;
            u16 word = data[off] | ((u16)data[off + 1] << 8);
            for (int b = 0; b < 16; b++)
                if (word & (1 << b)) bits_set++;
        }
        float density = (float)bits_set / (float)(h * 16);
        if (density > 0.15f && density < 0.45f)
            return h;
    }
    return -1;
}

int video_load_font(Video *v, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open font: %s\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    u8 *raw = malloc(fsize);
    if (!raw) { fclose(f); return -1; }
    fread(raw, 1, fsize, f);
    fclose(f);

    int char_h = font_detect_height(raw, fsize);
    if (char_h < 0) {
        fprintf(stderr, "Cannot detect font height: %s\n", path);
        free(raw);
        return -1;
    }

    int bpc = char_h * 2;
    long variant_start = fsize - (long)FONT_REMAP_SLOTS * bpc;

    v->font_char_w = 16;
    v->font_char_h = char_h;
    v->font_slots = FONT_REMAP_SLOTS;
    font_build_remap(v->font_remap);

    int expanded_size = FONT_REMAP_SLOTS * char_h * 16;
    v->font_data = calloc(1, expanded_size);
    if (!v->font_data) { free(raw); return -1; }

    for (int slot = 0; slot < FONT_REMAP_SLOTS; slot++) {
        long src_base = variant_start + (long)slot * bpc;
        for (int row = 0; row < char_h; row++) {
            long src_off = src_base + row * 2;
            int dst_off = (slot * char_h + row) * 16;
            u8 lo = raw[src_off], hi = raw[src_off + 1];
            for (int bit = 0; bit < 8; bit++) {
                v->font_data[dst_off + bit]     = (lo & (1 << bit)) ? 0xFF : 0;
                v->font_data[dst_off + 8 + bit] = (hi & (1 << bit)) ? 0xFF : 0;
            }
        }
    }

    int max_used = 0;
    for (int slot = 1; slot < FONT_REMAP_SLOTS; slot++) {
        for (int col = 15; col >= 0; col--) {
            int used = 0;
            for (int row = 0; row < char_h; row++) {
                if (v->font_data[(slot * char_h + row) * 16 + col]) { used = 1; break; }
            }
            if (used) { if (col + 2 > max_used) max_used = col + 2; break; }
        }
    }
    v->font_advance = (max_used > 4) ? max_used : v->font_char_w;

    free(raw);
    printf("Loaded font: %s (%dx%d, advance=%d, variant at %ld)\n",
           path, v->font_char_w, v->font_char_h, v->font_advance, variant_start);
    return 0;
}

void video_draw_char(Video *v, int x, int y, char ch, u8 color) {
    if (!v->font_data) return;

    int idx = v->font_remap[(u8)ch & 0x7F];
    if (idx < 0 || idx >= v->font_slots) return;

    int glyph_off = idx * v->font_char_h * 16;
    for (int row = 0; row < v->font_char_h; row++) {
        int py = y + row;
        if (py < 0 || py >= LOGICAL_H) continue;
        for (int col = 0; col < v->font_char_w; col++) {
            int px = x + col;
            if (px < 0 || px >= LOGICAL_W) continue;
            if (v->font_data[glyph_off + row * 16 + col]) {
                v->pixels[py * LOGICAL_W + px] = color;
            }
        }
    }
    v->dirty = 1;
}

void video_draw_text(Video *v, int x, int y, const char *str, u8 color) {
    if (!str) return;
    int adv = v->font_advance ? v->font_advance : v->font_char_w;
    int cx = x;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            y += v->font_char_h;
        } else if (*str == ' ') {
            cx += adv;
        } else {
            video_draw_char(v, cx, y, *str, color);
            cx += adv;
        }
        str++;
    }
}

void video_draw_char_scaled(Video *v, int x, int y, char ch, u8 color, int sn, int sd) {
    if (!v->font_data) return;
    int idx = v->font_remap[(u8)ch & 0x7F];
    if (idx < 0 || idx >= v->font_slots) return;

    int glyph_off = idx * v->font_char_h * 16;
    int sh = v->font_char_h * sn / sd;
    int sw = v->font_char_w * sn / sd;

    for (int row = 0; row < sh; row++) {
        int py = y + row;
        if (py < 0 || py >= LOGICAL_H) continue;
        int src_row = row * sd / sn;
        for (int col = 0; col < sw; col++) {
            int px = x + col;
            if (px < 0 || px >= LOGICAL_W) continue;
            if (v->font_data[glyph_off + src_row * 16 + col * sd / sn])
                v->pixels[py * LOGICAL_W + px] = color;
        }
    }
    v->dirty = 1;
}

void video_draw_text_scaled(Video *v, int x, int y, const char *str, u8 color, int sn, int sd) {
    if (!str) return;
    int adv = v->font_advance ? v->font_advance : v->font_char_w;
    int sadv = adv * sn / sd;
    int sfh = v->font_char_h * sn / sd;
    int cx = x;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            y += sfh;
        } else if (*str == ' ') {
            cx += sadv;
        } else {
            video_draw_char_scaled(v, cx, y, *str, color, sn, sd);
            cx += sadv;
        }
        str++;
    }
}
