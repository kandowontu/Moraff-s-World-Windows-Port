#include "mw_model_viewer.h"
#include "mw_game.h"
#include "mw_combat.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* WORLD.PIC and WALL.PIC both contain 256x200 scanline-RLE records.  The
 * viewer decodes copies into a neutral surface and never changes the source
 * files or the copies held by Game. */
#define ASSET_W 256
#define ASSET_H 200
#define ASSET_PIXELS (ASSET_W * ASSET_H)
#define PIC_SCAN_TABLE 0x190

#define VIEW_X 8
#define VIEW_Y 94
#define VIEW_W 1008
#define VIEW_H 568

enum {
    VIEW_COLOR_BG_A = 216,
    VIEW_COLOR_BG_B = 217,
    VIEW_COLOR_RULE = 218,
    VIEW_COLOR_TEXT = 219,
    VIEW_COLOR_ACCENT = 220
};

typedef struct ViewerAsset {
    u8 pixels[ASSET_PIXELS];
    int min_x, min_y, max_x, max_y;
    int occupied;
    int compressed_size;
    char label[160];
} ViewerAsset;

static int viewer_set_count(const Game *g, int set) {
    if (!g) return 0;
    switch (set) {
    case MODEL_VIEWER_WORLD: return g->world_pic_count;
    case MODEL_VIEWER_WALL:  return g->wall_pic_count;
    case MODEL_VIEWER_FONT:  return g->video.font_data ? g->video.font_slots : 0;
    default: return 0;
    }
}

static const char *viewer_set_name(int set) {
    switch (set) {
    case MODEL_VIEWER_WORLD: return "WORLD.PIC";
    case MODEL_VIEWER_WALL:  return "WALL.PIC";
    case MODEL_VIEWER_FONT:  return "1024X768 FONT";
    default: return "UNKNOWN";
    }
}

static int viewer_is_transparent(u8 color) {
    return color == 0 || color == 16;
}

static int decode_pic_record(const u8 *data, int size, u8 *out) {
    u16 scanline[ASSET_H];
    const u8 *commands;
    int command_size;
    int occupied = 0;

    if (!data || !out || size < PIC_SCAN_TABLE + 2) return 0;
    memset(out, 0, ASSET_PIXELS);
    commands = data + PIC_SCAN_TABLE;
    command_size = size - PIC_SCAN_TABLE;

    for (int y = 0; y < ASSET_H; y++)
        scanline[y] = (u16)(data[y * 2] | ((u16)data[y * 2 + 1] << 8));

    for (int y = 0; y < ASSET_H; y++) {
        int start = scanline[y];
        int end = command_size;
        for (int next = y + 1; next < ASSET_H; next++) {
            if (scanline[next] != scanline[y]) {
                end = scanline[next];
                break;
            }
        }
        if (start < 0 || start >= command_size || end <= start) continue;
        if (end > command_size) end = command_size;

        int pos = start;
        int x = commands[pos++];
        while (pos < end) {
            int command = commands[pos++];
            int run;
            int color;
            if (command >= 0x20) {
                run = command >> 5;
                color = command & 0x1F;
            } else {
                color = command;
                if (pos >= end) break;
                run = commands[pos++];
                if (!run) run = 255;
            }
            if (run <= 0) break;
            for (int i = 0; i < run && x + i < ASSET_W; i++) {
                if (x + i >= 0) {
                    out[y * ASSET_W + x + i] = (u8)color;
                    if (!viewer_is_transparent((u8)color)) occupied++;
                }
            }
            x += run;
            if (x >= ASSET_W) break;
        }
    }
    return occupied;
}

static char viewer_font_character(const Video *v, int slot) {
    if (!v) return 0;
    for (int ch = 32; ch < 127; ch++)
        if (v->font_remap[ch] == slot) return (char)ch;
    return 0;
}

static void describe_world_record(int index, char *out, size_t out_size) {
    int first_type = -1;
    int variants = 0;
    if (index == 0) {
        snprintf(out, out_size, "LADDER / SHOP ENTRANCE ART");
        return;
    }
    if (index == 1) {
        snprintf(out, out_size, "TRAPDOOR / PIT ART");
        return;
    }
    for (int type = 0; type < MONSTER_TYPE_COUNT; type++) {
        if (get_monster_pic_index_ext(type) == index) {
            if (first_type < 0) first_type = type;
            variants++;
        }
    }
    if (first_type >= 0) {
        snprintf(out, out_size, "%s MONSTER ART%s",
                 monster_types[first_type].name,
                 variants > 1 ? " (SHARED BY VARIANTS)" : "");
    } else {
        snprintf(out, out_size, "UNASSIGNED WORLD.PIC RECORD");
    }
}

static void viewer_find_bounds(ViewerAsset *asset) {
    asset->min_x = ASSET_W;
    asset->min_y = ASSET_H;
    asset->max_x = -1;
    asset->max_y = -1;
    asset->occupied = 0;
    for (int y = 0; y < ASSET_H; y++) {
        for (int x = 0; x < ASSET_W; x++) {
            if (viewer_is_transparent(asset->pixels[y * ASSET_W + x])) continue;
            if (x < asset->min_x) asset->min_x = x;
            if (y < asset->min_y) asset->min_y = y;
            if (x > asset->max_x) asset->max_x = x;
            if (y > asset->max_y) asset->max_y = y;
            asset->occupied++;
        }
    }
    if (!asset->occupied) {
        asset->min_x = 0;
        asset->min_y = 0;
        asset->max_x = ASSET_W - 1;
        asset->max_y = ASSET_H - 1;
    }
}

static int viewer_load_asset(Game *g, int set, int index, ViewerAsset *asset) {
    int count = viewer_set_count(g, set);
    if (!asset || index < 0 || index >= count) return 0;
    memset(asset, 0, sizeof(*asset));

    if (set == MODEL_VIEWER_WORLD) {
        if (!g->world_pic_data[index]) return 0;
        decode_pic_record(g->world_pic_data[index], g->world_pic_sizes[index],
                          asset->pixels);
        asset->compressed_size = g->world_pic_sizes[index];
        describe_world_record(index, asset->label, sizeof(asset->label));
    } else if (set == MODEL_VIEWER_WALL) {
        if (!g->wall_pic_data[index]) return 0;
        decode_pic_record(g->wall_pic_data[index], g->wall_pic_sizes[index],
                          asset->pixels);
        asset->compressed_size = g->wall_pic_sizes[index];
        snprintf(asset->label, sizeof(asset->label), "DUNGEON WALL TEXTURE %d",
                 index + 1);
    } else if (set == MODEL_VIEWER_FONT) {
        Video *v = &g->video;
        int x0 = (ASSET_W - v->font_char_w) / 2;
        int y0 = (ASSET_H - v->font_char_h) / 2;
        int glyph_offset = index * v->font_char_h * 16;
        for (int y = 0; y < v->font_char_h; y++) {
            for (int x = 0; x < v->font_char_w; x++) {
                if (v->font_data[glyph_offset + y * 16 + x])
                    asset->pixels[(y0 + y) * ASSET_W + x0 + x] = 15;
            }
        }
        char ch = viewer_font_character(v, index);
        if (ch)
            snprintf(asset->label, sizeof(asset->label),
                     "FONT GLYPH SLOT %d - CHARACTER '%c'", index, ch);
        else
            snprintf(asset->label, sizeof(asset->label),
                     "FONT GLYPH SLOT %d - BLANK / INTERNAL", index);
        asset->compressed_size = v->font_char_h * 2;
    } else {
        return 0;
    }

    viewer_find_bounds(asset);
    return 1;
}

static void viewer_prepare_palette(Video *v) {
    video_set_palette(v, VIEW_COLOR_BG_A, 22, 22, 28);
    video_set_palette(v, VIEW_COLOR_BG_B, 36, 36, 46);
    video_set_palette(v, VIEW_COLOR_RULE, 78, 78, 96);
    video_set_palette(v, VIEW_COLOR_TEXT, 70, 255, 90);
    video_set_palette(v, VIEW_COLOR_ACCENT, 255, 192, 30);
}

static void viewer_draw_checkerboard(Video *v) {
    const int cell = 32;
    video_fill_rect(v, VIEW_X, VIEW_Y, VIEW_W, VIEW_H, VIEW_COLOR_BG_A);
    for (int y = VIEW_Y; y < VIEW_Y + VIEW_H; y += cell) {
        for (int x = VIEW_X; x < VIEW_X + VIEW_W; x += cell) {
            if ((((x - VIEW_X) / cell) + ((y - VIEW_Y) / cell)) & 1)
                video_fill_rect(v, x, y,
                                x + cell > VIEW_X + VIEW_W ? VIEW_X + VIEW_W - x : cell,
                                y + cell > VIEW_Y + VIEW_H ? VIEW_Y + VIEW_H - y : cell,
                                VIEW_COLOR_BG_B);
        }
    }
    video_hline(v, VIEW_X, VIEW_Y, VIEW_W, VIEW_COLOR_RULE);
    video_hline(v, VIEW_X, VIEW_Y + VIEW_H - 1, VIEW_W, VIEW_COLOR_RULE);
    video_vline(v, VIEW_X, VIEW_Y, VIEW_H, VIEW_COLOR_RULE);
    video_vline(v, VIEW_X + VIEW_W - 1, VIEW_Y, VIEW_H, VIEW_COLOR_RULE);
}

static void viewer_draw_transformed(Video *v, const ViewerAsset *asset,
                                    float zoom, float angle_degrees) {
    float width = (float)(asset->max_x - asset->min_x + 1);
    float height = (float)(asset->max_y - asset->min_y + 1);
    float fit_x = (float)(VIEW_W - 80) / width;
    float fit_y = (float)(VIEW_H - 60) / height;
    float scale = (fit_x < fit_y ? fit_x : fit_y) * zoom;
    float angle = angle_degrees * 0.01745329251994329577f;
    float cosine = cosf(angle);
    float sine = sinf(angle);
    float src_cx = (asset->min_x + asset->max_x) * 0.5f;
    float src_cy = (asset->min_y + asset->max_y) * 0.5f;
    float dst_cx = VIEW_X + VIEW_W * 0.5f;
    float dst_cy = VIEW_Y + VIEW_H * 0.5f;

    if (scale < 0.001f) return;
    for (int y = VIEW_Y + 1; y < VIEW_Y + VIEW_H - 1; y++) {
        float dy = ((float)y + 0.5f - dst_cy) / scale;
        for (int x = VIEW_X + 1; x < VIEW_X + VIEW_W - 1; x++) {
            float dx = ((float)x + 0.5f - dst_cx) / scale;
            float source_x = cosine * dx + sine * dy + src_cx;
            float source_y = -sine * dx + cosine * dy + src_cy;
            int sx = (int)floorf(source_x + 0.5f);
            int sy = (int)floorf(source_y + 0.5f);
            if (sx < asset->min_x || sx > asset->max_x ||
                sy < asset->min_y || sy > asset->max_y)
                continue;
            u8 color = asset->pixels[sy * ASSET_W + sx];
            if (!viewer_is_transparent(color))
                v->pixels[y * LOGICAL_W + x] = color;
        }
    }
    v->dirty = 1;
}

static void viewer_draw(Game *g, int set, int index,
                        float zoom, float angle_degrees) {
    Video *v = &g->video;
    ViewerAsset *asset = malloc(sizeof(*asset));
    char line[256];
    int count = viewer_set_count(g, set);

    viewer_prepare_palette(v);
    video_clear(v, 0);
    video_draw_text(v, 12, 7, "MORAFF'S WORLD GRAPHICS / MODEL VIEWER",
                    VIEW_COLOR_TEXT);
    snprintf(line, sizeof(line), "SET: %s   RECORD: %d OF %d",
             viewer_set_name(set), count ? index + 1 : 0, count);
    video_draw_text(v, 12, 34, line, 15);
    snprintf(line, sizeof(line), "ZOOM: %.2fX FIT   ROTATION: %.2f DEGREES",
             zoom, angle_degrees);
    video_draw_text(v, 12, 61, line, VIEW_COLOR_ACCENT);

    viewer_draw_checkerboard(v);
    if (asset && viewer_load_asset(g, set, index, asset)) {
        viewer_draw_transformed(v, asset, zoom, angle_degrees);
        snprintf(line, sizeof(line), "%s   BOUNDS: %dX%d   SOURCE BYTES: %d",
                 asset->label, asset->max_x - asset->min_x + 1,
                 asset->max_y - asset->min_y + 1, asset->compressed_size);
        video_draw_text_scaled_xy(v, 12, 670, line, 15, 3, 4, 1, 1);
        if (!asset->occupied)
            video_draw_text(v, 390, 360, "THIS RECORD IS BLANK.", 8);
    } else {
        video_draw_text(v, 360, 360, "GRAPHIC RECORD COULD NOT BE DECODED.", 8);
    }
    free(asset);

    video_draw_text_scaled_xy(v, 12, 700,
        "PGUP/PGDN OR A/D: GRAPHIC   TAB: NEXT SET   HOME/END: FIRST/LAST",
        VIEW_COLOR_TEXT, 3, 4, 1, 1);
    video_draw_text_scaled_xy(v, 12, 729,
        "WHEEL OR UP/DOWN: ZOOM   LEFT/RIGHT OR [ ]: ROTATE   R: RESET   ESC: RETURN",
        VIEW_COLOR_ACCENT, 3, 4, 1, 1);
}

static int viewer_wrap_index(int index, int count) {
    if (count <= 0) return 0;
    while (index < 0) index += count;
    while (index >= count) index -= count;
    return index;
}

static float viewer_clamp_zoom(float zoom) {
    if (zoom < 0.05f) return 0.05f;
    if (zoom > 20.0f) return 20.0f;
    return zoom;
}

void model_viewer_run(Game *g) {
    int set = MODEL_VIEWER_WORLD;
    int indices[MODEL_VIEWER_SET_COUNT] = {0, 0, 0};
    float zoom = 1.0f;
    float angle = 0.0f;

    if (!g) return;
    game_refresh_world_palette(g);
    while (!input_poll_quit(&g->input)) {
        int count = viewer_set_count(g, set);
        indices[set] = viewer_wrap_index(indices[set], count);
        viewer_draw(g, set, indices[set], zoom, angle);
        video_present(&g->video);

        int key = input_getch(&g->input);
        int scan = -1;
        if (key == 0) scan = input_getch(&g->input);

        if (key == 0x1B || key == INPUT_MODEL_VIEWER) break;
        if (key == INPUT_MOUSE_WHEEL_UP) {
            zoom = viewer_clamp_zoom(zoom + 0.05f);
        } else if (key == INPUT_MOUSE_WHEEL_DOWN) {
            zoom = viewer_clamp_zoom(zoom - 0.05f);
        } else if (key == '\t') {
            do {
                set = (set + 1) % MODEL_VIEWER_SET_COUNT;
            } while (!viewer_set_count(g, set));
            zoom = 1.0f;
            angle = 0.0f;
        } else if (key == 'a' || key == 'A' || scan == 0x49) {
            indices[set] = viewer_wrap_index(indices[set] - 1, count);
        } else if (key == 'd' || key == 'D' || scan == 0x51) {
            indices[set] = viewer_wrap_index(indices[set] + 1, count);
        } else if (scan == 0x47) {
            indices[set] = 0;
        } else if (scan == 0x4F) {
            indices[set] = count > 0 ? count - 1 : 0;
        } else if (scan == 0x48 || key == '+' || key == '=') {
            zoom = viewer_clamp_zoom(zoom + 0.05f);
        } else if (scan == 0x50 || key == '-' || key == '_') {
            zoom = viewer_clamp_zoom(zoom - 0.05f);
        } else if (scan == 0x4B) {
            angle -= 1.0f;
        } else if (scan == 0x4D) {
            angle += 1.0f;
        } else if (key == '[' || key == '{') {
            angle -= 5.0f;
        } else if (key == ']' || key == '}') {
            angle += 5.0f;
        } else if (key == ',' || key == '<') {
            angle -= 0.1f;
        } else if (key == '.' || key == '>') {
            angle += 0.1f;
        } else if (key == 'r' || key == 'R') {
            zoom = 1.0f;
            angle = 0.0f;
        }
        if (angle >= 360.0f || angle <= -360.0f)
            angle = fmodf(angle, 360.0f);
    }
    game_refresh_world_palette(g);
}

void model_viewer_draw_test(Game *g, int set, int index,
                            float zoom, float angle_degrees) {
    if (!g) return;
    if (set < 0 || set >= MODEL_VIEWER_SET_COUNT) set = MODEL_VIEWER_WORLD;
    index = viewer_wrap_index(index, viewer_set_count(g, set));
    game_refresh_world_palette(g);
    viewer_draw(g, set, index, viewer_clamp_zoom(zoom), angle_degrees);
}

int model_viewer_self_test(Game *g) {
    ViewerAsset *asset;
    int failures = 0;
    if (!g) return 1;
    if (viewer_set_count(g, MODEL_VIEWER_WORLD) != g->world_pic_count ||
        viewer_set_count(g, MODEL_VIEWER_WALL) != g->wall_pic_count ||
        viewer_set_count(g, MODEL_VIEWER_FONT) != g->video.font_slots)
        failures++;
    if (g->world_pic_count <= 0 || g->wall_pic_count <= 0 ||
        g->video.font_slots <= 0)
        failures++;
    if (viewer_wrap_index(-1, 7) != 6 || viewer_wrap_index(7, 7) != 0)
        failures++;
    if (viewer_clamp_zoom(-1.0f) != 0.05f ||
        viewer_clamp_zoom(50.0f) != 20.0f)
        failures++;

    asset = malloc(sizeof(*asset));
    if (!asset) return failures + 1;
    if (!viewer_load_asset(g, MODEL_VIEWER_WORLD, 0, asset) ||
        !asset->occupied || asset->max_x < asset->min_x)
        failures++;
    if (!viewer_load_asset(g, MODEL_VIEWER_WALL, 0, asset) ||
        !asset->occupied)
        failures++;
    if (!viewer_load_asset(g, MODEL_VIEWER_FONT, 1, asset) ||
        !asset->occupied)
        failures++;
    free(asset);
    return failures;
}
