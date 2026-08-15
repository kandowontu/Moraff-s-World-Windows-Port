#include "mw_video.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * MW_PLATFORM_REPLACEMENT: SDL framebuffer/palette/text code replaces the
 * DOS/BGI/VGA implementation in WORLD func_23FA6..func_25943 and
 * func_28066..func_2B98A. Observable 1024x768 palette, font and PIC behavior
 * is retained; hardware-bank switching and chipset drivers are not backlog.
 * See PORT_STATUS.md.
 */

static u8 vga6_to_8(int v) {
    return (u8)((v * 255 + 31) / 63);
}

/* Exact WORLD func_06095 driver table plus the map configuration assigned in
 * the block at WORLD 0x086FF.  Repeated resolutions remain separate because
 * their palette, wall rasterizer, font, and framebuffer organization differ.
 * Window dimensions correct the DOS modes' non-square pixels to 4:3. */
static const MwDisplayModeInfo display_modes[MW_DISPLAY_MODE_COUNT] = {
    /* resolution, adapter, raster, corrected window, WORLD mode, mono,
       palette, wall path, font, normal-map cell/columns/rows,
       expanded-map cell, wilderness baseline/x-step/y-step/height ratio */
    {"720X348",  "HERCULES 2-COLOR",       720,348, 720,540, 0,1,
        MW_PALETTE_MONO, MW_WALL_HERCULES, 2, 8,16,22, 3, 94,2,2,2,1},
    {"320X200",  "CGA 4-COLOR",             320,200, 640,480, 1,0,
        MW_PALETTE_CGA4, MW_WALL_CGA, 0, 4,13,24, 4, 72,1,1,1,1},
    {"320X200",  "EGA 16-COLOR",            320,200, 640,480, 2,0,
        MW_PALETTE_EGA16, MW_WALL_PLANAR16, 0, 4,13,24, 4, 72,1,1,1,1},
    {"320X200",  "MCGA/VGA 256-COLOR",      320,200, 640,480, 3,0,
        MW_PALETTE_VGA256, MW_WALL_CHUNKY256, 0, 4,13,24, 4, 72,1,1,1,1},
    {"360X480",  "VGA 256-COLOR",            360,480, 640,480, 4,0,
        MW_PALETTE_VGA256, MW_WALL_CHUNKY256, 1, 8,8,30, 4,224,1,2,4,1},
    {"640X350",  "EGA 16-COLOR",             640,350, 640,480, 5,0,
        MW_PALETTE_EGA16, MW_WALL_PLANAR16, 2, 8,14,22, 3, 94,2,2,2,1},
    {"640X480",  "VGA PLANAR 16-COLOR",      640,480, 640,480, 6,0,
        MW_PALETTE_EGA16, MW_WALL_PLANAR16, 2, 8,14,30, 4,224,2,2,4,1},
    {"800X600",  "SVGA PLANAR 16-COLOR",     800,600, 800,600, 7,0,
        MW_PALETTE_EGA16, MW_WALL_PLANAR16, 2, 8,16,37, 5,217,3,3,9,2},
    {"1024X768", "SVGA PLANAR 16-COLOR",    1024,768,1024,768, 8,0,
        MW_PALETTE_EGA16, MW_WALL_PLANAR16, 2,10,18,38, 7,255,3,4,6,1},
    {"1024X768", "SVGA 256-COLOR (CHIPSET)",1024,768,1024,768, 9,0,
        MW_PALETTE_VGA256, MW_WALL_CHUNKY256, 2,10,18,38, 7,255,3,4,6,1},
    {"1024X768", "SVGA 256-COLOR (VESA)",   1024,768,1024,768,10,0,
        MW_PALETTE_VGA256, MW_WALL_CHUNKY256, 2,10,18,38, 7,255,3,4,6,1},
    {"640X480",  "VESA 256-COLOR",           640,480, 640,480,11,0,
        MW_PALETTE_VGA256, MW_WALL_CHUNKY256, 2, 8,14,30, 4,224,2,2,4,1}
};

const MwDisplayModeInfo *video_display_mode_info(int mode) {
    if (mode < 0 || mode >= MW_DISPLAY_MODE_COUNT) return NULL;
    return &display_modes[mode];
}

int video_set_display_mode(Video *v, int mode, int resize_window) {
    const MwDisplayModeInfo *info = video_display_mode_info(mode);
    if (!v || !v->renderer || !info) return -1;

    SDL_Texture *replacement = SDL_CreateTexture(v->renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        info->raster_w, info->raster_h);
    if (!replacement) {
        fprintf(stderr, "SDL_CreateTexture (%s): %s\n",
                info->resolution, SDL_GetError());
        return -1;
    }
    SDL_SetTextureScaleMode(replacement, SDL_ScaleModeNearest);

    if (v->framebuffer) SDL_DestroyTexture(v->framebuffer);
    v->framebuffer = replacement;
    v->display_mode = mode;
    v->output_w = info->raster_w;
    v->output_h = info->raster_h;
    if (resize_window && v->window) {
        SDL_SetWindowSize(v->window, info->window_w, info->window_h);
        SDL_SetWindowPosition(v->window,
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED);
    }
    v->dirty = 1;
    return 0;
}

int video_display_mode_self_test(Video *v) {
    static const int expected[MW_DISPLAY_MODE_COUNT][8] = {
        {720,348, 8,16,22, 1,3,94}, {320,200, 4,13,24, 4,4,72},
        {320,200, 4,13,24,16,4,72}, {320,200, 4,13,24,256,4,72},
        {360,480, 8, 8,30,256,4,224}, {640,350,8,14,22,16,3,94},
        {640,480, 8,14,30,16,4,224}, {800,600,8,16,37,16,5,217},
        {1024,768,10,18,38,16,7,255}, {1024,768,10,18,38,256,7,255},
        {1024,768,10,18,38,256,7,255}, {640,480,8,14,30,256,4,224}
    };
    int failures = 0;
    for (int i = 0; i < MW_DISPLAY_MODE_COUNT; i++) {
        const MwDisplayModeInfo *info = video_display_mode_info(i);
        if (!info || info->raster_w != expected[i][0] ||
            info->raster_h != expected[i][1] ||
            info->world_mode != i ||
            info->map_cell_px != expected[i][2] ||
            info->map_cols != expected[i][3] ||
            info->map_rows != expected[i][4] ||
            info->palette_colors != expected[i][5] ||
            info->expanded_map_cell_px != expected[i][6] ||
            info->wilderness_baseline != expected[i][7] ||
            info->window_w * 3 != info->window_h * 4)
            failures++;
    }
    if (video_display_mode_info(-1) ||
        video_display_mode_info(MW_DISPLAY_MODE_COUNT)) failures++;

    if (v && v->renderer) {
        int saved_mode = v->display_mode;
        for (int i = 0; i < MW_DISPLAY_MODE_COUNT; i++) {
            int texture_w = 0, texture_h = 0;
            if (video_set_display_mode(v, i, 0) != 0 ||
                SDL_QueryTexture(v->framebuffer, NULL, NULL,
                                 &texture_w, &texture_h) != 0 ||
                texture_w != expected[i][0] || texture_h != expected[i][1])
                failures++;
        }
        if (video_set_display_mode(v, saved_mode, 0) != 0) failures++;
    }
    return failures;
}

static void build_floor_low_ramp(u8 pal6[256][3], int floor) {
    static const u8 surface_palette[16][3] = {
        {15,  7,  7}, {20, 12, 12}, { 7, 15,  7}, {12, 20, 12},
        { 7,  7, 15}, {12, 12, 20}, { 7,  7,  0}, {12, 12,  0},
        {18, 18,  0}, { 0, 12,  0}, { 0, 20,  0}, { 7,  7,  7},
        {12, 12, 12}, {17, 17, 17}, {22, 22, 22}, {27, 27, 27}
    };
    int family = floor % 11;
    if (family < 0) family += 11;

    /* WORLD far_2478E has a special hand-authored surface palette for
     * floors divisible by eleven.  The other ten reachable switch arms are
     * the generated RGB ramps below.  (Its eleventh generated blue arm is
     * unreachable because the original divides by eleven.) */
    if (family == 0) {
        for (int i = 0; i < 16; i++)
            for (int c = 0; c < 3; c++)
                pal6[16 + i][c] = surface_palette[i][c];
        return;
    }

    for (int i = 0; i < 8; i++) {
        int even = 16 + i * 2;
        int odd = even + 1;
        u8 v = (u8)(i * 8);
        u8 inverse = (u8)(60 - v);
        u8 a[3] = {0, 0, 0};
        u8 b[3] = {0, 0, 0};

        switch (family) {
        case 1: /* gray, inverse gray */
            a[0] = a[1] = a[2] = v;
            b[0] = b[1] = b[2] = inverse;
            break;
        case 2: /* gray, red */
            a[0] = a[1] = a[2] = v;
            b[0] = v;
            break;
        case 3: /* gray, green */
            a[0] = a[1] = a[2] = v;
            b[1] = v;
            break;
        case 4: /* gray, yellow */
            a[0] = a[1] = a[2] = v;
            b[0] = b[1] = v;
            break;
        case 5: /* gray, blue */
            a[0] = a[1] = a[2] = v;
            b[2] = v;
            break;
        case 6: /* green, red */
            a[1] = v;
            b[0] = v;
            break;
        case 7: /* green, blue */
            a[1] = v;
            b[2] = v;
            break;
        case 8: /* green */
            a[1] = v;
            b[1] = inverse;
            break;
        case 9: /* red */
            a[0] = v;
            b[0] = inverse;
            break;
        case 10: /* yellow */
            a[0] = a[1] = v;
            b[0] = b[1] = inverse;
            break;
        }
        memcpy(pal6[even], a, sizeof(a));
        memcpy(pal6[odd], b, sizeof(b));
    }
}

static void build_floor_high_ramp(u8 pal6[256][3], int floor) {
    int family = floor % 7;
    if (family < 0) family += 7;
    int step = family == 0 ? 1 : (family == 1 ? 2 : 3);
    u8 *flat = &pal6[0][0];

    /* These hexadecimal bounds are byte offsets in WORLD's 768-byte DAC
     * buffer, not palette indices.  They cover the RGB components of colors
     * 48..63.  Components skipped by a stride retain sub_2473D's warm base. */
    for (int offset = 0x90; offset < 0xC0; offset += step) {
        u8 rising = (u8)((offset - 0x82) / 2);
        u8 falling = (u8)((0xC2 - offset) / 2);
        switch (family) {
        case 0:
            flat[offset] = rising;
            break;
        case 1:
        case 2:
            flat[offset] = rising;
            flat[offset + 1] = falling;
            break;
        case 3:
            flat[offset] = rising;
            flat[offset + 1] = rising;
            break;
        case 4:
            flat[offset] = rising;
            break;
        case 5:
            flat[offset + 1] = rising;
            break;
        case 6:
            flat[offset] = rising;
            flat[offset + 1] = rising;
            flat[offset + 2] = falling;
            break;
        }
    }
}

static void build_vga_palette(PaletteEntry pal[256], int floor,
                              u8 background_r, u8 background_g,
                              u8 background_b) {
    u8 pal6[256][3];

    /* Step 1: Base algorithm from sub_2473D (ASM line 70893).
     * Generates a warm-toned gradient for colors 1-255. */
    pal6[0][0] = 0; pal6[0][1] = 0; pal6[0][2] = 0;
    for (int cx = 1; cx < 256; cx++) {
        int val = (cx * 3) / 4;
        pal6[cx][0] = (u8)(63 - (val % 63));
        pal6[cx][1] = (u8)(cx / 4);
        pal6[cx][2] = 0;
    }

    /* Step 2: Explicit color overrides from far_2478E (ASM line 70934).
     * Colors 1-15 are set to specific values matching the game's UI. */
    static const u8 fixed_colors[16][3] = {
        { 0,  0,  0},  /*  0: black */
        { 0,  0, 38},  /*  1: dark blue */
        { 0,  0, 63},  /*  2: bright blue */
        {20,  0, 63},  /*  3: purple */
        {63, 63, 20},  /*  4: yellow */
        {53, 20, 10},  /*  5: brown */
        {63,  0, 10},  /*  6: red */
        {63, 45,  0},  /*  7: orange */
        { 0, 63,  0},  /*  8: green */
        { 0,  0,  0},  /*  9: black (variable) */
        {28,  0,  0},  /* 10: dark red */
        { 0, 28,  0},  /* 11: dark green */
        {16,  0,  0},  /* 12: very dark red */
        {13, 13, 13},  /* 13: dark gray */
        {42, 42, 42},  /* 14: medium gray */
        {63, 63, 63},  /* 15: white */
    };
    for (int i = 0; i < 16; i++) {
        pal6[i][0] = fixed_colors[i][0];
        pal6[i][1] = fixed_colors[i][1];
        pal6[i][2] = fixed_colors[i][2];
    }

    /* Step 3: Player-specific color ramp for colors 16-31.
     * Default: player 0, variation 0 — grayscale pairs. */
    for (int i = 0; i < 8; i++) {
        u8 v = (u8)(i * 8);
        u8 iv = (u8)(60 - v);
        pal6[16 + i * 2][0] = v;
        pal6[16 + i * 2][1] = v;
        pal6[16 + i * 2][2] = v;
        pal6[17 + i * 2][0] = iv;
        pal6[17 + i * 2][1] = iv;
        pal6[17 + i * 2][2] = iv;
    }

    /* The original 256-color path supplies a second hand-authored block at
     * DAC indices 32..47 before it applies the floor%7 component band. */
    static const u8 extended_colors[16][3] = {
        { 0,  0,  0}, {63, 40, 20}, {63, 50, 30}, {63, 60, 40},
        {11, 11,  0}, {24, 24,  0}, {37, 37,  0}, {50, 50,  0},
        {63, 63,  0}, {56, 45,  0}, {49, 30,  0}, {43, 15,  0},
        { 0, 40,  0}, {37, 37, 37}, {50, 50, 50}, {63, 63, 63}
    };
    for (int i = 0; i < 16; i++)
        memcpy(pal6[32 + i], extended_colors[i], 3);

    build_floor_low_ramp(pal6, floor);
    build_floor_high_ramp(pal6, floor);

    /* The *, (, and ) commands increment these preserved DAC bytes by 0x10.
     * Real VGA hardware exposes only the low six bits, so four presses of a
     * key return that channel to black even though WORLD stores a full byte. */
    pal6[0][0] = background_r & 0x3F;
    pal6[0][1] = background_g & 0x3F;
    pal6[0][2] = background_b & 0x3F;

    /* Convert from VGA 6-bit (0-63) to 8-bit (0-255) */
    for (int i = 0; i < 256; i++) {
        pal[i].r = vga6_to_8(pal6[i][0]);
        pal[i].g = vga6_to_8(pal6[i][1]);
        pal[i].b = vga6_to_8(pal6[i][2]);
    }

    /* DOSBox expands the original 6-bit DAC channels by shifting, so the
     * brightest UI values in a captured mode-8 frame are FC rather than FF. */
    pal[4]  = (PaletteEntry){252, 252,  80};
    pal[6]  = (PaletteEntry){252,   0,  40};
    pal[8]  = (PaletteEntry){  0, 252,   0};
    pal[10] = (PaletteEntry){112,   0,   0};

    /* The banked 1024x768 driver replaces part of the generated ramp while
     * drawing the dungeon.  These are the DAC values visible in an original
     * mode-8 capture and are reserved by the native renderer for the same
     * ceiling, floor, wall, and status roles. */
    static const PaletteEntry svga_render_colors[] = {
        {224, 224, 224}, /* cracked wall face */
        {192, 192, 192}, /* wall highlight */
        {  0,   0, 192}, /* blue cracks/mortar */
        { 32,  32,  92}, /* ceiling navy 1 */
        { 28,  28, 100}, /* ceiling navy 2 */
        { 80,  80,  44}, /* dungeon olive 1 */
        { 76,  76,  52}, /* dungeon olive 2 */
        { 68,  68,  56}, /* dungeon olive 3 */
        { 40,  40,  88}, /* ceiling navy 3 */
        { 92,  92,  32}, /* floor olive 1 */
        {100, 100,  28}, /* floor olive 2 */
        {104, 104,  20}, /* floor olive 3 */
        {112, 112,  16}, /* floor olive 4 */
        {116, 116,   8}, /* floor olive 5 */
        {252, 252, 252}, /* map white */
        { 81, 202, 255}, /* status cyan */
        {174,  60,   0}, /* bottom prompt orange */
    };
    for (int i = 0; i < (int)(sizeof(svga_render_colors) / sizeof(svga_render_colors[0])); i++)
        pal[MW_COLOR_WALL_FACE + i] = svga_render_colors[i];

    /* The native wall sampler normalizes WALL.PIC's zero/transparent fill to
     * the captured gray face at the default palette.  Once WORLD's hidden RGB
     * controls are used, route that fill through their live DAC color so the
     * characteristic red/green/blue wash appears in the same wall regions. */
    if (((background_r | background_g | background_b) & 0x3F) != 0)
        pal[MW_COLOR_WALL_TINT] = pal[0];
    else
        pal[MW_COLOR_WALL_TINT] = pal[MW_COLOR_WALL_FACE];
}

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

    v->argb_pixels = malloc((size_t)LOGICAL_W * LOGICAL_H * sizeof(u32));
    if (!v->argb_pixels) {
        fprintf(stderr, "Cannot allocate presentation buffer\n");
        return -1;
    }

    if (video_set_display_mode(v, MW_DISPLAY_SVGA_1024X768_256_A, 0) < 0)
        return -1;

    video_load_vga_default_palette(v);
    v->dirty = 1;
    return 0;
}

void video_shutdown(Video *v) {
    free(v->font_data);
    free(v->argb_pixels);
    if (v->framebuffer) SDL_DestroyTexture(v->framebuffer);
    if (v->renderer) SDL_DestroyRenderer(v->renderer);
    if (v->window) SDL_DestroyWindow(v->window);
    memset(v, 0, sizeof(*v));
}

void video_present(Video *v) {
    if (!v->dirty) return;

    const MwDisplayModeInfo *info = video_display_mode_info(v->display_mode);
    if (!info) info = &display_modes[MW_DISPLAY_SVGA_1024X768_256_A];
    static const u8 bayer4[4][4] = {
        { 0,  8,  2, 10},
        {12,  4, 14,  6},
        { 3, 11,  1,  9},
        {15,  7, 13,  5}
    };

    /* Mode-specific gameplay primitives have already been rounded through
     * the selected native coordinate grid and rasterized with its wall/map
     * path.  This final stage performs only the hardware framebuffer sample
     * and adapter palette restriction before SDL enlarges the result. */
    for (int y = 0; y < info->raster_h; y++) {
        int source_y = y * LOGICAL_H / info->raster_h;
        for (int x = 0; x < info->raster_w; x++) {
            int source_x = x * LOGICAL_W / info->raster_w;
            int source_index = v->pixels[source_y * LOGICAL_W + source_x];
            PaletteEntry *c = &v->palette[source_index];
            u32 argb;
            if (info->monochrome) {
                int luminance = (77 * c->r + 150 * c->g + 29 * c->b) >> 8;
                int threshold = (int)bayer4[y & 3][x & 3] * 16 + 8;
                argb = luminance > threshold ? 0xFFFFFFFFu : 0xFF000000u;
            } else {
                /* The planar branches cannot display arbitrary entries from
                 * the 256-colour DAC.  Resolve every semantic/native colour
                 * to the closest colour actually available to that adapter.
                 * This is intentionally done after native-pixel sampling;
                 * rendering a 256-colour frame and merely shrinking it was
                 * the inaccurate behaviour this table replaces. */
                if (info->palette_colors < 256) {
                    /* WORLD's CGA branch selects the red/green/brown set,
                     * visible in its wireframe corridors, rather than the
                     * cyan/magenta palette used by the previous shortcut. */
                    static const u8 cga_index[4] = {0, 8, 6, 4};
                    int best = 0, best_distance = 0x7fffffff;
                    int count = info->palette_colors == 4 ? 4 : 16;
                    for (int candidate = 0; candidate < count; candidate++) {
                        int pi = info->palette_colors == 4 ?
                                 cga_index[candidate] : candidate;
                        PaletteEntry *pc = &v->palette[pi];
                        int dr = (int)c->r - pc->r;
                        int dg = (int)c->g - pc->g;
                        int db = (int)c->b - pc->b;
                        int distance = dr * dr + dg * dg + db * db;
                        if (distance < best_distance) {
                            best_distance = distance;
                            best = pi;
                        }
                    }
                    c = &v->palette[best];
                }
                argb = 0xFF000000u | ((u32)c->r << 16) |
                       ((u32)c->g << 8) | c->b;
            }
            v->argb_pixels[y * info->raster_w + x] = argb;
        }
    }

    SDL_UpdateTexture(v->framebuffer, NULL, v->argb_pixels,
                      info->raster_w * (int)sizeof(u32));
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
    build_vga_palette(v->palette, 0, 0, 0, 0);
    v->dirty = 1;
}

void video_load_world_palette(Video *v, int floor,
                              u8 background_r, u8 background_g,
                              u8 background_b) {
    build_vga_palette(v->palette, floor,
                      background_r, background_g, background_b);
    v->dirty = 1;
}

int video_world_palette_self_test(void) {
    PaletteEntry pal[256];
    int failures = 0;
    static const u8 high_first[7][3] = {
        { 7,  7,  8}, { 7, 25,  8}, { 7, 25,  0}, { 7,  7,  0},
        { 7, 12,  0}, {27,  7,  0}, { 7,  7, 25}
    };

    for (int floor = 0; floor < 7; floor++) {
        build_vga_palette(pal, floor, 0, 0, 0);
        if (pal[48].r != vga6_to_8(high_first[floor][0]) ||
            pal[48].g != vga6_to_8(high_first[floor][1]) ||
            pal[48].b != vga6_to_8(high_first[floor][2])) failures++;
    }

    build_vga_palette(pal, 0, 0, 0, 0);
    if (pal[16].r != vga6_to_8(15) ||
        pal[16].g != vga6_to_8(7) ||
        pal[16].b != vga6_to_8(7)) failures++;
    if (pal[31].r != vga6_to_8(27) ||
        pal[31].g != vga6_to_8(27) ||
        pal[31].b != vga6_to_8(27)) failures++;

    build_vga_palette(pal, 1, 0, 0, 0);
    if (pal[16].r != 0 || pal[16].g != 0 || pal[16].b != 0) failures++;
    if (pal[17].r != vga6_to_8(60) ||
        pal[17].g != vga6_to_8(60) ||
        pal[17].b != vga6_to_8(60)) failures++;

    build_vga_palette(pal, 3, 0, 0, 0);
    if (pal[31].r != 0 || pal[31].g != vga6_to_8(56) ||
        pal[31].b != 0) failures++;

    build_vga_palette(pal, 9, 0, 0, 0);
    if (pal[31].r != vga6_to_8(4) || pal[31].g != 0 ||
        pal[31].b != 0) failures++;

    build_vga_palette(pal, 6, 0x10, 0x20, 0x30);
    if (pal[0].r != vga6_to_8(0x10) ||
        pal[0].g != vga6_to_8(0x20) ||
        pal[0].b != vga6_to_8(0x30)) failures++;
    if (pal[32].r != 0 || pal[32].g != 0 || pal[32].b != 0) failures++;
    if (pal[47].r != 255 || pal[47].g != 255 || pal[47].b != 255) failures++;
    if (pal[MW_COLOR_WALL_TINT].r != pal[0].r ||
        pal[MW_COLOR_WALL_TINT].g != pal[0].g ||
        pal[MW_COLOR_WALL_TINT].b != pal[0].b) failures++;

    return failures;
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

void video_blit_pic_sprite(Video *v, int cx, int cy, int draw_h,
                           const u8 *pic_data, int pic_size, u8 transparent) {
    if (!pic_data || pic_size < 0x192) return;
    (void)transparent;

    const int PIC_ROWS = 200;
    const int TABLE_SIZE = 0x190;

    u16 scanline[200];
    for (int i = 0; i < PIC_ROWS; i++)
        scanline[i] = (u16)(pic_data[i * 2] | (pic_data[i * 2 + 1] << 8));

    const u8 *pixdata = pic_data + TABLE_SIZE;
    int pixdata_len = pic_size - TABLE_SIZE;

    int draw_w = draw_h * 3 / 4;
    int left_x = cx - draw_w / 2;
    int top_y = cy;

    for (int row = 0; row < PIC_ROWS; row++) {
        int data_start = scanline[row];
        int data_end = pixdata_len;
        for (int j = row + 1; j < PIC_ROWS; j++) {
            if (scanline[j] != data_start) { data_end = scanline[j]; break; }
        }
        if (data_start >= pixdata_len || data_start == data_end) continue;

        int screen_y = top_y + row * draw_h / PIC_ROWS;
        int screen_y_end = top_y + (row + 1) * draw_h / PIC_ROWS;
        if (screen_y >= LOGICAL_H) break;
        if (screen_y_end <= 0) continue;

        int ptr = data_start;
        int x_pos = pixdata[ptr++];

        while (ptr < data_end && ptr < pixdata_len) {
            int cmd = pixdata[ptr++];
            int run_len, color;

            if (cmd >= 0x20) {
                run_len = cmd >> 5;
                color = cmd & 0x1F;
            } else {
                color = cmd;
                if (ptr >= pixdata_len) break;
                int n = pixdata[ptr++];
                run_len = (n == 0) ? 255 : n;
            }

            if (color != 0 && color != 16) {
                int x_start = left_x + x_pos * draw_w / 256;
                int x_end = left_x + (x_pos + run_len) * draw_w / 256;
                if (x_end <= x_start) x_end = x_start + 1;

                for (int sy = screen_y; sy < screen_y_end && sy < LOGICAL_H; sy++) {
                    if (sy < 0) continue;
                    for (int sx = x_start; sx < x_end; sx++) {
                        if (sx >= 0 && sx < LOGICAL_W)
                            v->pixels[sy * LOGICAL_W + sx] = (u8)color;
                    }
                }
            }

            x_pos += run_len;
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
    /* WORLD's table at DS:7F0D maps '-' to font slot zero.  Slot zero is
       therefore a real punctuation glyph, not an unused sentinel. */
    remap['-'] = 0;
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

    /* 320x200 text is drawn through the original double-scan character
       path.  Without this native vertical doubling, a nominal 14-row glyph
       collapses to only three or four rows when the compositor is sampled
       into the 200-line adapter raster. */
    const MwDisplayModeInfo *display =
        video_display_mode_info(v->display_mode);
    int row_repeat = display && display->raster_h <= 200 ? 2 : 1;
    int rendered_h = char_h * row_repeat;
    v->font_char_w = 16;
    v->font_char_h = rendered_h;
    v->font_slots = FONT_REMAP_SLOTS;
    font_build_remap(v->font_remap);

    int expanded_size = FONT_REMAP_SLOTS * rendered_h * 16;
    u8 *new_font_data = calloc(1, expanded_size);
    if (!new_font_data) { free(raw); return -1; }

    for (int slot = 0; slot < FONT_REMAP_SLOTS; slot++) {
        long src_base = variant_start + (long)slot * bpc;
        for (int row = 0; row < char_h; row++) {
            long src_off = src_base + row * 2;
            u8 lo = raw[src_off], hi = raw[src_off + 1];
            for (int repeat = 0; repeat < row_repeat; repeat++) {
                int dst_row = row * row_repeat + repeat;
                int dst_off = (slot * rendered_h + dst_row) * 16;
                for (int bit = 0; bit < 8; bit++) {
                    new_font_data[dst_off + bit] =
                        (lo & (1 << bit)) ? 0xFF : 0;
                    new_font_data[dst_off + 8 + bit] =
                        (hi & (1 << bit)) ? 0xFF : 0;
                }
            }
        }
    }

    int max_used = 0;
    for (int slot = 1; slot < FONT_REMAP_SLOTS; slot++) {
        for (int col = 15; col >= 0; col--) {
            int used = 0;
            for (int row = 0; row < rendered_h; row++) {
                if (new_font_data[(slot * rendered_h + row) * 16 + col]) {
                    used = 1;
                    break;
                }
            }
            if (used) { if (col + 2 > max_used) max_used = col + 2; break; }
        }
    }
    free(v->font_data);
    v->font_data = new_font_data;
    v->font_advance = (max_used > 4) ? max_used : v->font_char_w;

    free(raw);
    printf("Loaded font: %s (%dx%d, advance=%d, variant at %ld)\n",
           path, v->font_char_w, v->font_char_h, v->font_advance,
           variant_start);
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

void video_draw_char_scaled_xy(Video *v, int x, int y, char ch, u8 color,
                               int xsn, int xsd, int ysn, int ysd) {
    if (!v->font_data || xsn <= 0 || xsd <= 0 || ysn <= 0 || ysd <= 0) return;
    int idx = v->font_remap[(u8)ch & 0x7F];
    if (idx < 0 || idx >= v->font_slots) return;

    int glyph_off = idx * v->font_char_h * 16;
    int sh = v->font_char_h * ysn / ysd;
    int sw = v->font_char_w * xsn / xsd;

    for (int row = 0; row < sh; row++) {
        int py = y + row;
        if (py < 0 || py >= LOGICAL_H) continue;
        int src_row = row * ysd / ysn;
        if (src_row >= v->font_char_h) src_row = v->font_char_h - 1;
        for (int col = 0; col < sw; col++) {
            int px = x + col;
            if (px < 0 || px >= LOGICAL_W) continue;
            int src_col = col * xsd / xsn;
            if (src_col >= 16) src_col = 15;
            if (v->font_data[glyph_off + src_row * 16 + src_col])
                v->pixels[py * LOGICAL_W + px] = color;
        }
    }
    v->dirty = 1;
}

void video_draw_text_scaled_xy(Video *v, int x, int y, const char *str, u8 color,
                               int xsn, int xsd, int ysn, int ysd) {
    if (!str || xsn <= 0 || xsd <= 0 || ysn <= 0 || ysd <= 0) return;
    int adv = v->font_advance ? v->font_advance : v->font_char_w;
    int sadv = adv * xsn / xsd;
    int sfh = v->font_char_h * ysn / ysd;
    int cx = x;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            y += sfh;
        } else if (*str == ' ') {
            cx += sadv;
        } else {
            video_draw_char_scaled_xy(v, cx, y, *str, color, xsn, xsd, ysn, ysd);
            cx += sadv;
        }
        str++;
    }
}
