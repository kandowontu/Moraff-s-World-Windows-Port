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
#define WALL_PALETTE_PERIOD 77
#define MODEL_VIEWER_MAX_ENTRIES (64 * WALL_PALETTE_PERIOD)

#define VIEW_X 8
#define VIEW_Y 94
#define VIEW_W 1008
#define VIEW_H 568
#define VIEW_FULLSCREEN_X 828
#define VIEW_FULLSCREEN_Y 56
#define VIEW_FULLSCREEN_W 188
#define VIEW_FULLSCREEN_H 34

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

typedef struct ViewerEntry {
    int source_index;
    int monster_type;
    int replace_color;
    int tint;
    int palette_floor;
    int group_index;
    int variant_index;
    int variant_count;
    int shared_count;
} ViewerEntry;

typedef struct ViewerCatalog {
    ViewerEntry entries[MODEL_VIEWER_MAX_ENTRIES];
    int count;
    int group_count;
} ViewerCatalog;

static int viewer_source_count(const Game *g, int set) {
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
    return color == 0 || color == 16 || color == 32;
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

static ViewerEntry *viewer_catalog_add(ViewerCatalog *catalog) {
    if (!catalog || catalog->count >= MODEL_VIEWER_MAX_ENTRIES) return NULL;
    ViewerEntry *entry = &catalog->entries[catalog->count++];
    memset(entry, 0, sizeof(*entry));
    entry->monster_type = -1;
    entry->replace_color = -1;
    return entry;
}

static void viewer_catalog_finish_group(ViewerCatalog *catalog,
                                        int first, int group_index) {
    int variants = catalog->count - first;
    for (int i = first; i < catalog->count; i++) {
        catalog->entries[i].group_index = group_index;
        catalog->entries[i].variant_index = i - first;
        catalog->entries[i].variant_count = variants;
    }
}

/* Build display order by source model first, then by every distinct palette
 * combination actually assigned to that model.  Multiple monsters that are
 * graphically identical share one entry instead of showing duplicate frames. */
static int viewer_build_world_catalog(const Game *g, ViewerCatalog *catalog) {
    for (int source = 0; source < g->world_pic_count; source++) {
        int first = catalog->count;
        for (int type = 0; type < MONSTER_TYPE_COUNT; type++) {
            if (!combat_monster_type_spawnable(type) ||
                get_monster_pic_index_ext(type) != source)
                continue;
            int replace = get_monster_color_ext(type);
            int tint = get_monster_tint_ext(type);
            int duplicate = -1;
            for (int i = first; i < catalog->count; i++) {
                if (catalog->entries[i].replace_color == replace &&
                    catalog->entries[i].tint == tint) {
                    duplicate = i;
                    break;
                }
            }
            if (duplicate >= 0) {
                catalog->entries[duplicate].shared_count++;
                continue;
            }
            ViewerEntry *entry = viewer_catalog_add(catalog);
            if (!entry) return 0;
            entry->source_index = source;
            entry->monster_type = type;
            entry->replace_color = replace;
            entry->tint = tint;
            entry->shared_count = 1;
        }
        if (catalog->count == first) {
            ViewerEntry *entry = viewer_catalog_add(catalog);
            if (!entry) return 0;
            entry->source_index = source;
            entry->shared_count = 1;
        }
        viewer_catalog_finish_group(catalog, first, catalog->group_count++);
    }
    return 1;
}

static int viewer_build_catalog(const Game *g, int set,
                                ViewerCatalog *catalog) {
    if (!g || !catalog) return 0;
    memset(catalog, 0, sizeof(*catalog));
    if (set == MODEL_VIEWER_WORLD)
        return viewer_build_world_catalog(g, catalog);

    if (set == MODEL_VIEWER_WALL) {
        for (int source = 0; source < g->wall_pic_count; source++) {
            int first = catalog->count;
            /* WORLD's two independent floor palette switches repeat together
             * every LCM(11,7) = 77 depths. */
            for (int floor = 0; floor < WALL_PALETTE_PERIOD; floor++) {
                ViewerEntry *entry = viewer_catalog_add(catalog);
                if (!entry) return 0;
                entry->source_index = source;
                entry->palette_floor = floor;
                entry->shared_count = 1;
            }
            viewer_catalog_finish_group(catalog, first,
                                        catalog->group_count++);
        }
        return 1;
    }

    if (set == MODEL_VIEWER_FONT) {
        int count = viewer_source_count(g, set);
        for (int source = 0; source < count; source++) {
            int first = catalog->count;
            ViewerEntry *entry = viewer_catalog_add(catalog);
            if (!entry) return 0;
            entry->source_index = source;
            entry->shared_count = 1;
            viewer_catalog_finish_group(catalog, first,
                                        catalog->group_count++);
        }
        return 1;
    }
    return 0;
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

static int viewer_load_asset(Game *g, int set, const ViewerEntry *entry,
                             ViewerAsset *asset) {
    int count = viewer_source_count(g, set);
    int index;
    if (!asset || !entry) return 0;
    index = entry->source_index;
    if (index < 0 || index >= count) return 0;
    memset(asset, 0, sizeof(*asset));

    if (set == MODEL_VIEWER_WORLD) {
        if (!g->world_pic_data[index]) return 0;
        decode_pic_record(g->world_pic_data[index], g->world_pic_sizes[index],
                          asset->pixels);
        asset->compressed_size = g->world_pic_sizes[index];
        if (entry->monster_type >= 0) {
            for (int pixel = 0; pixel < ASSET_PIXELS; pixel++)
                asset->pixels[pixel] = (u8)combat_remap_monster_color(
                    asset->pixels[pixel], entry->replace_color, entry->tint);
            if (entry->shared_count > 1)
                snprintf(asset->label, sizeof(asset->label),
                         "%s - RECORD %d - COLOR %d - TINT %d - SHARED BY %d MONSTERS",
                         monster_types[entry->monster_type].name, index + 1,
                         entry->replace_color, entry->tint,
                         entry->shared_count);
            else
                snprintf(asset->label, sizeof(asset->label),
                         "%s - RECORD %d - COLOR %d - TINT %d",
                         monster_types[entry->monster_type].name, index + 1,
                         entry->replace_color, entry->tint);
        } else {
            describe_world_record(index, asset->label, sizeof(asset->label));
        }
    } else if (set == MODEL_VIEWER_WALL) {
        if (!g->wall_pic_data[index]) return 0;
        decode_pic_record(g->wall_pic_data[index], g->wall_pic_sizes[index],
                          asset->pixels);
        asset->compressed_size = g->wall_pic_sizes[index];
        snprintf(asset->label, sizeof(asset->label),
                 "%s MODEL - RECORD %d - FLOOR PALETTE %d OF %d",
                 index == 0 ? "DOOR" : "DUNGEON WALL", index + 1,
                 entry->palette_floor + 1,
                 WALL_PALETTE_PERIOD);
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

static void viewer_prepare_entry_palette(Game *g, int set,
                                         const ViewerEntry *entry) {
    int floor = set == MODEL_VIEWER_WALL && entry ?
                entry->palette_floor : 0;
    video_load_world_palette(&g->video, floor, 0, 0, 0);
    viewer_prepare_palette(&g->video);
}

static void viewer_draw_checkerboard_rect(Video *v, int view_x, int view_y,
                                          int view_w, int view_h) {
    const int cell = 32;
    video_fill_rect(v, view_x, view_y, view_w, view_h, VIEW_COLOR_BG_A);
    for (int y = view_y; y < view_y + view_h; y += cell) {
        for (int x = view_x; x < view_x + view_w; x += cell) {
            if ((((x - view_x) / cell) + ((y - view_y) / cell)) & 1)
                video_fill_rect(v, x, y,
                                x + cell > view_x + view_w ?
                                view_x + view_w - x : cell,
                                y + cell > view_y + view_h ?
                                view_y + view_h - y : cell,
                                VIEW_COLOR_BG_B);
        }
    }
    video_hline(v, view_x, view_y, view_w, VIEW_COLOR_RULE);
    video_hline(v, view_x, view_y + view_h - 1, view_w, VIEW_COLOR_RULE);
    video_vline(v, view_x, view_y, view_h, VIEW_COLOR_RULE);
    video_vline(v, view_x + view_w - 1, view_y, view_h, VIEW_COLOR_RULE);
}

static void viewer_draw_transformed_rect(Video *v, const ViewerAsset *asset,
                                         float zoom, float angle_degrees,
                                         int view_x, int view_y,
                                         int view_w, int view_h,
                                         int horizontal_padding,
                                         int vertical_padding) {
    float width = (float)(asset->max_x - asset->min_x + 1);
    float height = (float)(asset->max_y - asset->min_y + 1);
    float fit_x = (float)(view_w - horizontal_padding) / width;
    float fit_y = (float)(view_h - vertical_padding) / height;
    float scale = (fit_x < fit_y ? fit_x : fit_y) * zoom;
    float angle = angle_degrees * 0.01745329251994329577f;
    float cosine = cosf(angle);
    float sine = sinf(angle);
    float src_cx = (asset->min_x + asset->max_x) * 0.5f;
    float src_cy = (asset->min_y + asset->max_y) * 0.5f;
    float dst_cx = view_x + view_w * 0.5f;
    float dst_cy = view_y + view_h * 0.5f;

    if (scale < 0.001f) return;
    for (int y = view_y + 1; y < view_y + view_h - 1; y++) {
        float dy = ((float)y + 0.5f - dst_cy) / scale;
        for (int x = view_x + 1; x < view_x + view_w - 1; x++) {
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

static void viewer_draw_fullscreen_button(Video *v) {
    const char *label = "F FULLSCREEN";
    int text_width = (int)strlen(label) *
                     (v->font_advance ? v->font_advance : v->font_char_w);
    int text_x = VIEW_FULLSCREEN_X + (VIEW_FULLSCREEN_W - text_width) / 2;
    video_fill_rect(v, VIEW_FULLSCREEN_X, VIEW_FULLSCREEN_Y,
                    VIEW_FULLSCREEN_W, VIEW_FULLSCREEN_H, VIEW_COLOR_BG_B);
    video_hline(v, VIEW_FULLSCREEN_X, VIEW_FULLSCREEN_Y,
                VIEW_FULLSCREEN_W, VIEW_COLOR_ACCENT);
    video_hline(v, VIEW_FULLSCREEN_X,
                VIEW_FULLSCREEN_Y + VIEW_FULLSCREEN_H - 1,
                VIEW_FULLSCREEN_W, VIEW_COLOR_ACCENT);
    video_vline(v, VIEW_FULLSCREEN_X, VIEW_FULLSCREEN_Y,
                VIEW_FULLSCREEN_H, VIEW_COLOR_ACCENT);
    video_vline(v, VIEW_FULLSCREEN_X + VIEW_FULLSCREEN_W - 1,
                VIEW_FULLSCREEN_Y, VIEW_FULLSCREEN_H, VIEW_COLOR_ACCENT);
    video_draw_text(v, text_x, VIEW_FULLSCREEN_Y + 4, label, 15);
}

static int viewer_fullscreen_button_hit(int x, int y) {
    return x >= VIEW_FULLSCREEN_X &&
           x < VIEW_FULLSCREEN_X + VIEW_FULLSCREEN_W &&
           y >= VIEW_FULLSCREEN_Y &&
           y < VIEW_FULLSCREEN_Y + VIEW_FULLSCREEN_H;
}

static void viewer_draw_fullscreen_frame(Game *g, int set,
                                         const ViewerCatalog *catalog,
                                         int index, float zoom,
                                         float angle_degrees) {
    Video *v = &g->video;
    ViewerAsset *asset = malloc(sizeof(*asset));
    int count = catalog ? catalog->count : 0;
    const ViewerEntry *entry =
        catalog && index >= 0 && index < count ?
        &catalog->entries[index] : NULL;

    viewer_prepare_entry_palette(g, set, entry);
    video_clear(v, 0);
    viewer_draw_checkerboard_rect(v, 0, 0, LOGICAL_W, LOGICAL_H);
    if (asset && entry && viewer_load_asset(g, set, entry, asset)) {
        viewer_draw_transformed_rect(v, asset, zoom, angle_degrees,
                                     0, 0, LOGICAL_W, LOGICAL_H,
                                     32, 32);
    } else {
        video_draw_text(v, 360, 360,
                        "GRAPHIC RECORD COULD NOT BE DECODED.", 8);
    }
    free(asset);

    video_fill_rect(v, 0, LOGICAL_H - 38, LOGICAL_W, 38, 0);
    video_hline(v, 0, LOGICAL_H - 38, LOGICAL_W, VIEW_COLOR_ACCENT);
    video_draw_text_scaled_xy(v, 12, LOGICAL_H - 31,
        "FULLSCREEN MODEL VIEW - PRESS ANY KEY OR MOUSE BUTTON",
        15, 3, 4, 1, 1);
}

static void viewer_show_fullscreen(Game *g, int set,
                                   const ViewerCatalog *catalog,
                                   int index, float zoom,
                                   float angle_degrees) {
    viewer_draw_fullscreen_frame(g, set, catalog, index, zoom,
                                 angle_degrees);
    video_present(&g->video);
    input_wait_any_key(&g->input);
}

static void viewer_draw(Game *g, int set, const ViewerCatalog *catalog,
                        int index,
                        float zoom, float angle_degrees) {
    Video *v = &g->video;
    ViewerAsset *asset = malloc(sizeof(*asset));
    char line[256];
    int count = catalog ? catalog->count : 0;
    const ViewerEntry *entry =
        catalog && index >= 0 && index < count ?
        &catalog->entries[index] : NULL;

    viewer_prepare_entry_palette(g, set, entry);
    video_clear(v, 0);
    video_draw_text(v, 12, 7, "MORAFF'S WORLD GRAPHICS / MODEL VIEWER",
                    VIEW_COLOR_TEXT);
    snprintf(line, sizeof(line),
             "SET: %s   MODEL: %d OF %d   VARIANT: %d OF %d",
             viewer_set_name(set),
             entry ? entry->group_index + 1 : 0,
             catalog ? catalog->group_count : 0,
             entry ? entry->variant_index + 1 : 0,
             entry ? entry->variant_count : 0);
    video_draw_text(v, 12, 34, line, 15);
    snprintf(line, sizeof(line), "ZOOM: %.2fX FIT   ROTATION: %.2f DEGREES",
             zoom, angle_degrees);
    video_draw_text(v, 12, 61, line, VIEW_COLOR_ACCENT);
    viewer_draw_fullscreen_button(v);

    viewer_draw_checkerboard_rect(v, VIEW_X, VIEW_Y, VIEW_W, VIEW_H);
    if (asset && entry && viewer_load_asset(g, set, entry, asset)) {
        viewer_draw_transformed_rect(v, asset, zoom, angle_degrees,
                                     VIEW_X, VIEW_Y, VIEW_W, VIEW_H,
                                     80, 60);
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
        "PGUP OR PGDN OR A OR D: VARIANT   W OR S: MODEL GROUP   TAB: NEXT SET",
        VIEW_COLOR_TEXT, 3, 4, 1, 1);
    video_draw_text_scaled_xy(v, 12, 729,
        "WHEEL OR UP/DOWN: ZOOM   LEFT/RIGHT: ROTATE   R: RESET   F: FULLSCREEN   ESC: RETURN",
        VIEW_COLOR_ACCENT, 3, 4, 1, 1);
}

static int viewer_wrap_index(int index, int count) {
    if (count <= 0) return 0;
    while (index < 0) index += count;
    while (index >= count) index -= count;
    return index;
}

static int viewer_step_group(const ViewerCatalog *catalog, int index,
                             int direction) {
    if (!catalog || catalog->count <= 0 || catalog->group_count <= 0)
        return 0;
    index = viewer_wrap_index(index, catalog->count);
    int group = catalog->entries[index].group_index +
                (direction < 0 ? -1 : 1);
    if (group < 0) group = catalog->group_count - 1;
    if (group >= catalog->group_count) group = 0;
    for (int i = 0; i < catalog->count; i++)
        if (catalog->entries[i].group_index == group)
            return i;
    return 0;
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
    ViewerCatalog *catalog;

    if (!g) return;
    catalog = malloc(sizeof(*catalog));
    if (!catalog) return;
    if (!viewer_build_catalog(g, set, catalog)) {
        free(catalog);
        return;
    }
    game_refresh_world_palette(g);
    while (!input_poll_quit(&g->input)) {
        int count = catalog->count;
        indices[set] = viewer_wrap_index(indices[set], count);
        viewer_draw(g, set, catalog, indices[set], zoom, angle);
        video_present(&g->video);

        int key = input_getch(&g->input);
        int scan = -1;
        if (key == 0) scan = input_getch(&g->input);

        if (key == 0x1B || key == INPUT_MODEL_VIEWER) break;
        if (key == INPUT_MOUSE_WHEEL_UP) {
            zoom = viewer_clamp_zoom(zoom + 0.05f);
        } else if (key == INPUT_MOUSE_WHEEL_DOWN) {
            zoom = viewer_clamp_zoom(zoom - 0.05f);
        } else if (key == INPUT_MOUSE_CLICK) {
            int x, y;
            if (game_mouse_click_logical(g, &x, &y) &&
                viewer_fullscreen_button_hit(x, y))
                viewer_show_fullscreen(g, set, catalog, indices[set],
                                       zoom, angle);
        } else if (key == 'f' || key == 'F') {
            viewer_show_fullscreen(g, set, catalog, indices[set],
                                   zoom, angle);
        } else if (key == '\t') {
            do {
                set = (set + 1) % MODEL_VIEWER_SET_COUNT;
            } while (!viewer_source_count(g, set));
            if (!viewer_build_catalog(g, set, catalog)) break;
            zoom = 1.0f;
            angle = 0.0f;
        } else if (key == 'a' || key == 'A' || scan == 0x49) {
            indices[set] = viewer_wrap_index(indices[set] - 1, count);
        } else if (key == 'd' || key == 'D' || scan == 0x51) {
            indices[set] = viewer_wrap_index(indices[set] + 1, count);
        } else if (key == 'w' || key == 'W') {
            indices[set] = viewer_step_group(catalog, indices[set], -1);
        } else if (key == 's' || key == 'S') {
            indices[set] = viewer_step_group(catalog, indices[set], 1);
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
    free(catalog);
    game_refresh_world_palette(g);
}

void model_viewer_draw_test(Game *g, int set, int index,
                            float zoom, float angle_degrees) {
    ViewerCatalog *catalog;
    if (!g) return;
    if (set < 0 || set >= MODEL_VIEWER_SET_COUNT) set = MODEL_VIEWER_WORLD;
    catalog = malloc(sizeof(*catalog));
    if (!catalog) return;
    if (!viewer_build_catalog(g, set, catalog)) {
        free(catalog);
        return;
    }
    index = viewer_wrap_index(index, catalog->count);
    game_refresh_world_palette(g);
    viewer_draw(g, set, catalog, index, viewer_clamp_zoom(zoom),
                angle_degrees);
    free(catalog);
}

void model_viewer_draw_fullscreen_test(Game *g, int set, int index,
                                       float zoom, float angle_degrees) {
    ViewerCatalog *catalog;
    if (!g) return;
    if (set < 0 || set >= MODEL_VIEWER_SET_COUNT) set = MODEL_VIEWER_WORLD;
    catalog = malloc(sizeof(*catalog));
    if (!catalog) return;
    if (!viewer_build_catalog(g, set, catalog)) {
        free(catalog);
        return;
    }
    index = viewer_wrap_index(index, catalog->count);
    game_refresh_world_palette(g);
    viewer_draw_fullscreen_frame(g, set, catalog, index,
                                 viewer_clamp_zoom(zoom), angle_degrees);
    free(catalog);
}

int model_viewer_self_test(Game *g) {
    ViewerAsset *asset;
    ViewerCatalog *catalog;
    int failures = 0;
    if (!g) return 1;
    if (g->world_pic_count <= 0 || g->wall_pic_count <= 0 ||
        g->video.font_slots <= 0)
        failures++;
    if (viewer_wrap_index(-1, 7) != 6 || viewer_wrap_index(7, 7) != 0)
        failures++;
    if (viewer_clamp_zoom(-1.0f) != 0.05f ||
        viewer_clamp_zoom(50.0f) != 20.0f)
        failures++;
    if (!viewer_fullscreen_button_hit(
            VIEW_FULLSCREEN_X + VIEW_FULLSCREEN_W / 2,
            VIEW_FULLSCREEN_Y + VIEW_FULLSCREEN_H / 2) ||
        viewer_fullscreen_button_hit(VIEW_FULLSCREEN_X - 1,
                                     VIEW_FULLSCREEN_Y) ||
        viewer_fullscreen_button_hit(
            VIEW_FULLSCREEN_X + VIEW_FULLSCREEN_W,
            VIEW_FULLSCREEN_Y + VIEW_FULLSCREEN_H - 1))
        failures++;
    if (combat_remap_monster_color(17, 4, 0) != 4 ||
        combat_remap_monster_color(6, -1, 9) != 1 ||
        combat_remap_monster_color(14, -1, 12) != 12 ||
        combat_remap_monster_color(15, -1, 12) != 15 ||
        combat_remap_monster_color(32, -1, 12) != 32)
        failures++;

    catalog = malloc(sizeof(*catalog));
    asset = malloc(sizeof(*asset));
    if (!catalog || !asset) {
        free(catalog);
        free(asset);
        return failures + 1;
    }

    if (!viewer_build_catalog(g, MODEL_VIEWER_WORLD, catalog) ||
        catalog->count <= g->world_pic_count ||
        catalog->group_count != g->world_pic_count)
        failures++;
    for (int i = 1; i < catalog->count; i++) {
        const ViewerEntry *previous = &catalog->entries[i - 1];
        const ViewerEntry *current = &catalog->entries[i];
        if (current->group_index < previous->group_index ||
            (current->group_index == previous->group_index &&
             current->source_index != previous->source_index))
            failures++;
    }
    if (!viewer_load_asset(g, MODEL_VIEWER_WORLD, &catalog->entries[0],
                           asset) ||
        !asset->occupied || asset->max_x < asset->min_x)
        failures++;

    int recolored = -1;
    for (int i = 0; i < catalog->count; i++) {
        if (catalog->entries[i].monster_type >= 0 &&
            (catalog->entries[i].replace_color != 0 ||
             catalog->entries[i].tint != 0)) {
            recolored = i;
            break;
        }
    }
    if (recolored < 0 ||
        !viewer_load_asset(g, MODEL_VIEWER_WORLD,
                           &catalog->entries[recolored], asset))
        failures++;

    if (!viewer_build_catalog(g, MODEL_VIEWER_WALL, catalog) ||
        catalog->count != g->wall_pic_count * WALL_PALETTE_PERIOD ||
        catalog->group_count != g->wall_pic_count ||
        !viewer_load_asset(g, MODEL_VIEWER_WALL, &catalog->entries[0],
                           asset) ||
        !asset->occupied)
        failures++;

    if (!viewer_build_catalog(g, MODEL_VIEWER_FONT, catalog) ||
        catalog->count != g->video.font_slots ||
        catalog->group_count != g->video.font_slots ||
        !viewer_load_asset(g, MODEL_VIEWER_FONT, &catalog->entries[1],
                           asset) ||
        !asset->occupied)
        failures++;
    free(catalog);
    free(asset);
    return failures;
}
