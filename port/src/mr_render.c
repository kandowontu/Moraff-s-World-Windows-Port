#include "mr_render.h"
#include "mr_bios_font.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void mr_render_error(char *error, size_t error_size,
                            const char *message, const char *path) {
    if (!error || !error_size) return;
    if (path) snprintf(error, error_size, "%s: %s", message, path);
    else snprintf(error, error_size, "%s", message);
}

static void mr_render_path(char *out, size_t out_size, const char *directory,
                           const char *name) {
    size_t length = strlen(directory);
    const char *separator = length &&
        (directory[length - 1] == '/' || directory[length - 1] == '\\')
        ? "" : "/";
    snprintf(out, out_size, "%s%s%s", directory, separator, name);
}

static int mr_render_parse_record(char *line, double *values,
                                  int expected_values) {
    char *cursor = line;
    for (int index = 0; index < expected_values; index++) {
        char *end = NULL;
        errno = 0;
        values[index] = strtod(cursor, &end);
        if (end == cursor || errno == ERANGE || !isfinite(values[index]))
            return 0;
        cursor = end;
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (index + 1 < expected_values) {
            if (*cursor != ',') return 0;
            cursor++;
        }
    }
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n')
        cursor++;
    return *cursor == '\0' || (unsigned char)*cursor == 0x1A;
}

static int mr_render_integer(double value, int *result) {
    int converted = (int)value;
    if ((double)converted != value) return 0;
    *result = converted;
    return 1;
}

int mr_render_layout_load(MRRenderLayout *layout, const char *directory,
                          char *error, size_t error_size) {
    if (!layout || !directory) {
        mr_render_error(error, error_size,
                        "Invalid Revenge renderer-layout request", NULL);
        return 0;
    }

    char path[512];
    mr_render_path(path, sizeof(path), directory, "F2.COM");
    FILE *file = fopen(path, "rb");
    if (!file) {
        mr_render_error(error, error_size,
                        "Missing original Revenge renderer data", path);
        return 0;
    }

    MRRenderLayout parsed;
    memset(&parsed, 0, sizeof(parsed));
    char line[256];
    double values[MR_RENDER_PROJECTION_VALUES];
    int ok = 1;
    for (int row = 0; row < MR_RENDER_DEPTH_ROWS && ok; row++) {
        ok = fgets(line, sizeof(line), file) != NULL &&
             mr_render_parse_record(line, values, 2) &&
             mr_render_integer(values[0], &parsed.depth_geometry[row][0]) &&
             mr_render_integer(values[1], &parsed.depth_geometry[row][1]);
    }
    for (int row = 0; row < MR_RENDER_VIEWPORT_ORIGINS && ok; row++) {
        ok = fgets(line, sizeof(line), file) != NULL &&
             mr_render_parse_record(line, values, 2) &&
             mr_render_integer(values[0], &parsed.viewport_origins[row].x) &&
             mr_render_integer(values[1], &parsed.viewport_origins[row].y);
    }
    for (int row = 0; row < MR_RENDER_PROJECTION_ROWS && ok; row++) {
        ok = fgets(line, sizeof(line), file) != NULL &&
             mr_render_parse_record(line, values,
                                    MR_RENDER_PROJECTION_VALUES);
        if (ok)
            memcpy(parsed.projection[row], values, sizeof(values));
    }
    fclose(file);

    if (!ok) {
        mr_render_error(error, error_size,
                        "Invalid original Revenge F2.COM numeric records",
                        path);
        return 0;
    }
    *layout = parsed;
    return 1;
}

void mr_render_map_pixel(int player_x, int player_y, int *pixel_x,
                         int *pixel_y) {
    if (pixel_x) *pixel_x = player_x * 8 - 7;
    if (pixel_y) *pixel_y = player_y * 8 + 38;
}

int mr_render_direction_for_pass(int facing, int pass) {
    if (facing < 1 || facing > 4 || pass < 0) return 0;
    return ((facing - 1 + pass) % 4) + 1;
}

static void mr_render_raster_pixel(uint8_t *pixels, size_t stride,
                                   int x, int y, uint8_t color) {
    if (!pixels || stride < MR_RENDER_WIDTH || x < 0 ||
        x >= MR_RENDER_WIDTH || y < 0 || y >= MR_RENDER_HEIGHT)
        return;
    pixels[(size_t)y * stride + (size_t)x] = color & 3U;
}

static void mr_render_raster_hline(uint8_t *pixels, size_t stride,
                                   int x0, int x1, int y, uint8_t color) {
    if (x0 > x1) { int swap = x0; x0 = x1; x1 = swap; }
    for (int x = x0; x <= x1; x++)
        mr_render_raster_pixel(pixels, stride, x, y, color);
}

static void mr_render_raster_vline(uint8_t *pixels, size_t stride,
                                   int x, int y0, int y1, uint8_t color) {
    if (y0 > y1) { int swap = y0; y0 = y1; y1 = swap; }
    for (int y = y0; y <= y1; y++)
        mr_render_raster_pixel(pixels, stride, x, y, color);
}

static void mr_render_raster_line(uint8_t *pixels, size_t stride,
                                  int x0, int y0, int x1, int y1,
                                  uint8_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        mr_render_raster_pixel(pixels, stride, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int twice = error * 2;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

static void mr_render_raster_box(uint8_t *pixels, size_t stride,
                                 int x0, int y0, int x1, int y1,
                                 uint8_t color, int filled) {
    if (filled) {
        for (int y = y0; y <= y1; y++)
            mr_render_raster_hline(pixels, stride, x0, x1, y, color);
        return;
    }
    mr_render_raster_hline(pixels, stride, x0, x1, y0, color);
    mr_render_raster_hline(pixels, stride, x0, x1, y1, color);
    mr_render_raster_vline(pixels, stride, x0, y0, y1, color);
    mr_render_raster_vline(pixels, stride, x1, y0, y1, color);
}

static void mr_render_raster_circle_three(uint8_t *pixels, size_t stride,
                                          int cx, int cy, uint8_t color) {
    /* SCREEN 1's integer CIRCLE at radius three visits this symmetric set.
     * Keeping it explicit also preserves the QB3 result at this tiny size
     * without depending on an SDL ellipse implementation. */
    static const int8_t points[][2] = {
        {0, -3}, {1, -3}, {2, -2}, {3, -1}, {3, 0}, {3, 1},
        {2, 2}, {1, 3}, {0, 3}, {-1, 3}, {-2, 2}, {-3, 1},
        {-3, 0}, {-3, -1}, {-2, -2}, {-1, -3}
    };
    for (size_t index = 0; index < sizeof(points) / sizeof(points[0]);
         index++)
        mr_render_raster_pixel(pixels, stride, cx + points[index][0],
                               cy + points[index][1], color);
}

static int mr_render_map_revealed(const MRSave *save, int level,
                                  int x, int y) {
    if (!save || level < 0 || level >= MR_EXPLORED_LEVELS ||
        x < 1 || x > 20 || y < 0 || y >= MR_EXPLORED_ROWS)
        return 0;
    return (save->explored[level][y] & (1U << (20 - x))) != 0;
}

static void mr_render_map_horizontal_edge(uint8_t *pixels, size_t stride,
                                          int x0, int y0, int wall,
                                          uint8_t color, int solid_border) {
    if (wall < 6 && !solid_border) return;
    mr_render_raster_hline(pixels, stride, x0, x0 + 8, y0, color);
    if (!solid_border && wall <= 7)
        mr_render_raster_hline(pixels, stride, x0 + 3, x0 + 5, y0, 0);
}

static void mr_render_map_vertical_edge(uint8_t *pixels, size_t stride,
                                        int x0, int y0, int wall,
                                        uint8_t color, int solid_border) {
    if (wall < 6 && !solid_border) return;
    mr_render_raster_vline(pixels, stride, x0, y0, y0 + 8, color);
    if (!solid_border && wall <= 7)
        mr_render_raster_vline(pixels, stride, x0, y0 + 2, y0 + 6, 0);
}

static void mr_render_map_feature(uint8_t *pixels, size_t stride,
                                  int x0, int y0, int feature,
                                  uint8_t ladder_color,
                                  uint8_t chute_color) {
    /* DUNSMALL 52A2-5393 suppresses values above three. In particular, 25
     * is a still-hidden false floor and deliberately has no map mark. */
    if (feature > 3) return;
    if (feature == MR_VERTICAL_CHUTE) {
        mr_render_raster_circle_three(pixels, stride, x0 + 4, y0 + 4,
                                      chute_color);
    } else if (feature < 0) {
        mr_render_raster_box(pixels, stride, x0 + 2, y0 + 2,
                             x0 + 6, y0 + 6, ladder_color, 0);
    } else {
        mr_render_raster_box(pixels, stride, x0 + 2, y0 + 2,
                             x0 + 6, y0 + 6, ladder_color, 1);
    }
}

static void mr_render_map_town_marker(uint8_t *pixels, size_t stride,
                                      int x0, int y0, int world_x,
                                      int world_y, uint8_t color) {
    /* DUNSMALL 040D-0494 prints SBTIW with the BIOS 8x8 font and GETs a
     * seven-by-seven crop of each glyph. C102-C331 PUTs one of those buffers
     * at the cell-interior origin for these exact level-zero coordinates.
     * Rows below use bit 6 as the leftmost pixel of that recovered crop. */
    int glyph = -1;
    if ((world_x == 18 && world_y == 3) ||
        (world_x == 13 && world_y == 18) ||
        (world_x == 2 && world_y == 8))
        glyph = 0;
    else if (world_x == 13 && world_y == 3)
        glyph = 1;
    else if ((world_x == 7 && world_y == 15) ||
             (world_x == 14 && world_y == 12))
        glyph = 2;
    else if ((world_x == 7 && world_y == 3) ||
             (world_x == 3 && world_y == 2) ||
             (world_x == 18 && world_y == 17))
        glyph = 3;
    else if (world_x == 6 && world_y == 14)
        glyph = 4;
    if (glyph < 0) return;

    const uint8_t *rows = mr_bios_font_glyph((uint8_t)"SBTIW"[glyph]);
    int left = x0 + 1;
    int top = y0 + 1;
    /* The three inn locations use PUT/PSET (mode 0); all other captured
     * glyphs use PUT/XOR (mode 3). PSET copies zero source pixels too. */
    if (glyph == 3)
        for (int row = 0; row < 7; row++)
            for (int column = 0; column < 7; column++)
                mr_render_raster_pixel(pixels, stride, left + column,
                                       top + row, 0);
    for (int row = 0; row < 7; row++) {
        for (int column = 0; column < 7; column++) {
            /* mr_bios_font_glyph uses the LSB-left representation consumed
             * by the native BIOS text path.  DUNSMALL GETs source columns
             * 0..6, so use those same seven columns instead of a second,
             * incompatible hand-written font and a reversed bit order. */
            if ((rows[row] & (uint8_t)(1U << column)) == 0)
                continue;
            size_t offset = (size_t)(top + row) * stride +
                            (size_t)(left + column);
            if (glyph == 3)
                pixels[offset] = color & 3U;
            else
                pixels[offset] ^= color & 3U;
        }
    }
}

static void mr_render_map_player(uint8_t *pixels, size_t stride,
                                 int x, int y, int facing, uint8_t color) {
    /* DUNSMALL 049D-050C prints CP437 18h,19h,1Ah,1Bh, GETs columns 0..6
     * and rows 0..6, then maps them N,S,E,W at 486D-48F4. These are the
     * corresponding IBM BIOS 8x8 rows before that exact crop. */
    /* Rows are kept MSB-left here because these are the actual CP437 ROM
     * bytes captured by GET, unlike mr_bios_font.c's native LSB-left table. */
    static const uint8_t arrow[4][7] = {
        {0x18, 0x3c, 0x7e, 0x18, 0x18, 0x18, 0x18},
        {0x00, 0x18, 0x0c, 0xfe, 0x0c, 0x18, 0x00},
        {0x18, 0x18, 0x18, 0x18, 0x7e, 0x3c, 0x18},
        {0x00, 0x30, 0x60, 0xfe, 0x60, 0x30, 0x00}
    };
    if (x < 1 || x > 20 || y < 1 || y > 19 ||
        facing < MR_DIRECTION_NORTH || facing > MR_DIRECTION_WEST)
        return;
    int left = x * 8 - 7;
    int top = y * 8 + 38;
    const uint8_t *rows = arrow[facing - 1];
    for (int row = 0; row < 7; row++)
        for (int column = 0; column < 7; column++)
            if (rows[row] & (uint8_t)(0x80U >> column)) {
                int pixel_x = left + column;
                int pixel_y = top + row;
                if (pixel_x < 0 || pixel_x >= MR_RENDER_WIDTH ||
                    pixel_y < 0 || pixel_y >= MR_RENDER_HEIGHT)
                    continue;
                size_t offset = (size_t)pixel_y * stride +
                                (size_t)pixel_x;
                /* Unqualified PUT is XOR (mode 3), as selected at
                 * 486D-48F4.  This is observable over town glyphs, ladders,
                 * and chutes; assigning color erased their captured pixels. */
                pixels[offset] ^= color & 3U;
            }
}

int mr_render_draw_dungeon_map(const MRSave *save, const MRWorldData *world,
                               uint8_t *pixels, size_t stride,
                               int color_enabled, int draw_player,
                               MRDirection facing) {
    if (!save || !world || !pixels || stride < MR_RENDER_WIDTH) return 0;
    const MRCharacter *character = &save->character;
    int level = (int)floor(character->dungeon_level);
    if (level < 0 || level >= MR_EXPLORED_LEVELS) return 0;
    float seed = (float)character->dungeon_generation_seed;
    /* Startup stores the exact BASIC colors used by the redraw: DS:19BC is
     * logical red for map walls, DS:19C8 is logical green for ladders and
     * perspective walls, while default-color circles/captured glyphs use 3.
     * Monochrome mode initializes both stored colors to logical 3. */
    uint8_t wall_color = color_enabled ? 2U : 3U;   /* DS:19BC */
    uint8_t ladder_color = color_enabled ? 1U : 3U; /* DS:19C8 */
    uint8_t glyph_color = 3U;

    /* Full redraw at 4D52-53ED: x is outer, y is inner. Cell interiors are
     * x0+1..7/y0+1..7, matching the separate player-background GET. */
    for (int x = 1; x <= 20; x++) {
        int x0 = (x - 1) * 8;
        for (int y = 1; y <= 19; y++) {
            if (!mr_render_map_revealed(save, level, x, y)) continue;
            int y0 = y * 8 + 37;

            /* 4DF3-4F52 skips this cell's north edge when the already
             * revealed northern cell will emit the same seam as its south
             * edge.  An unrevealed neighbor instead requires this edge to
             * close the visible cell.  The former port had this predicate
             * reversed, leaving isolated/newly revealed cells open. */
            if (y == 1 || !mr_render_map_revealed(save, level, x, y - 1))
                mr_render_map_horizontal_edge(
                    pixels, stride, x0, y0,
                    mr_world_wall_value_for_step(level, seed, x, y,
                                                 MR_DIRECTION_NORTH),
                    wall_color, y == 1);
            /* 4F52-5151 applies the identical ownership rule to the west
             * seam.  East and south remain the unconditional owners below. */
            if (x == 1 || !mr_render_map_revealed(save, level, x - 1, y))
                mr_render_map_vertical_edge(
                    pixels, stride, x0, y0,
                    mr_world_wall_value_for_step(level, seed, x, y,
                                                 MR_DIRECTION_WEST),
                    wall_color, x == 1);

            mr_render_map_vertical_edge(
                pixels, stride, x0 + 8, y0,
                mr_world_wall_value_for_step(level, seed, x, y,
                                             MR_DIRECTION_EAST),
                wall_color, x == 20);
            mr_render_map_horizontal_edge(
                pixels, stride, x0, y0 + 8,
                mr_world_wall_value_for_step(level, seed, x, y,
                                             MR_DIRECTION_SOUTH),
                wall_color, y == 19);

            mr_render_map_feature(
                pixels, stride, x0, y0,
                mr_world_resolve_vertical_feature(world, level, x, y),
                ladder_color, glyph_color);
            if (level == 0)
                mr_render_map_town_marker(pixels, stride, x0, y0, x, y,
                                          glyph_color);
        }
    }

    if (draw_player)
        mr_render_map_player(pixels, stride,
                             (int)floor(character->player_x),
                             (int)floor(character->player_y),
                             facing,
                             glyph_color);
    return 1;
}

static MRDirection mr_render_turn_left(MRDirection direction) {
    return direction == MR_DIRECTION_NORTH ? MR_DIRECTION_WEST
                                           : (MRDirection)(direction - 1);
}

static MRDirection mr_render_turn_right(MRDirection direction) {
    return direction == MR_DIRECTION_WEST ? MR_DIRECTION_NORTH
                                          : (MRDirection)(direction + 1);
}

static void mr_render_step(int *x, int *y, MRDirection direction) {
    if (direction == MR_DIRECTION_NORTH) (*y)--;
    else if (direction == MR_DIRECTION_EAST) (*x)++;
    else if (direction == MR_DIRECTION_SOUTH) (*y)++;
    else if (direction == MR_DIRECTION_WEST) (*x)--;
}

int mr_render_trace_view(const MRCharacter *character,
                         const int occupancy[21][22],
                         MRDirection direction, MRRenderView *view) {
    if (!character || !view || direction < MR_DIRECTION_NORTH ||
        direction > MR_DIRECTION_WEST)
        return 0;
    memset(view, 0, sizeof(*view));
    view->direction = direction;
    int level = (int)floor(character->dungeon_level);
    int x = (int)floor(character->player_x);
    int y = (int)floor(character->player_y);
    float seed = (float)character->dungeon_generation_seed;
    MRDirection left = mr_render_turn_left(direction);
    MRDirection right = mr_render_turn_right(direction);
    for (int depth = 0; depth < MR_RENDER_DEPTH_ROWS; depth++) {
        if (x < 1 || x > 20 || y < 1 || y > 19) break;
        MRRenderDepth *row = &view->depth[view->depth_count++];
        row->x = x;
        row->y = y;
        row->center_wall = mr_world_wall_value_for_step(
            level, seed, x, y, direction);
        row->left_wall = mr_world_wall_value_for_step(
            level, seed, x, y, left);
        row->right_wall = mr_world_wall_value_for_step(
            level, seed, x, y, right);
        /* DUNSMALL records the occupant one cell beyond the current center
         * edge (5C5A-5C77 et al.). Values 6..9 terminate visibility before
         * that lookup, including the passable 6/7 door values. */
        if (row->center_wall >= 6) break;
        mr_render_step(&x, &y, direction);
        if (occupancy && y >= 0 && y < 21 && x >= 0 && x < 22)
            row->monster_global_index = occupancy[y][x];
    }
    return view->depth_count;
}

#define MR_RENDER_VIEW_MAX_X 52
#define MR_RENDER_VIEW_MAX_Y 53

static int mr_render_qb_cint(double value) {
    /* QB3's $CINFAC rounds a positive half upward (A942-A947), rather than
     * truncating the decimal F2.COM projection records. */
    return value >= 0.0 ? (int)floor(value + 0.5)
                        : (int)ceil(value - 0.5);
}

static int mr_render_projection_int(const MRRenderLayout *layout,
                                    int depth, int column) {
    if (!layout || depth < 1 || depth > MR_RENDER_PROJECTION_ROWS ||
        column < 0 || column >= MR_RENDER_PROJECTION_VALUES)
        return 0;
    return mr_render_qb_cint((double)(float)layout->projection[depth - 1]
                                                     [column]);
}

static void mr_render_view_pixel(uint8_t *pixels, size_t stride,
                                 int origin_x, int origin_y,
                                 int x, int y, uint8_t color) {
    if (x < 0 || x > MR_RENDER_VIEW_MAX_X ||
        y < 0 || y > MR_RENDER_VIEW_MAX_Y)
        return;
    mr_render_raster_pixel(pixels, stride, origin_x + x, origin_y + y,
                           color);
}

static void mr_render_view_line(uint8_t *pixels, size_t stride,
                                int origin_x, int origin_y,
                                int x0, int y0, int x1, int y1,
                                uint8_t color) {
    /* All recovered endpoints lie inside the active VIEW rectangle, so the
     * global Bresenham result is identical to drawing in local coordinates. */
    mr_render_raster_line(pixels, stride, origin_x + x0, origin_y + y0,
                          origin_x + x1, origin_y + y1, color);
}

static void mr_render_view_box(uint8_t *pixels, size_t stride,
                               int origin_x, int origin_y,
                               int x0, int y0, int x1, int y1,
                               uint8_t color, int filled) {
    mr_render_raster_box(pixels, stride, origin_x + x0, origin_y + y0,
                         origin_x + x1, origin_y + y1, color, filled);
}

static void mr_render_view_paint(uint8_t *pixels, size_t stride,
                                 int origin_x, int origin_y,
                                 int seed_x, int seed_y, uint8_t color) {
    typedef struct MRRenderPaintPoint { int8_t x, y; } MRRenderPaintPoint;
    MRRenderPaintPoint pending[(MR_RENDER_VIEW_MAX_X + 1) *
                               (MR_RENDER_VIEW_MAX_Y + 1)];
    size_t count = 0;
    if (seed_x < 0 || seed_x > MR_RENDER_VIEW_MAX_X ||
        seed_y < 0 || seed_y > MR_RENDER_VIEW_MAX_Y)
        return;
    size_t seed_offset = (size_t)(origin_y + seed_y) * stride +
                         (size_t)(origin_x + seed_x);
    if ((pixels[seed_offset] & 3U) == (color & 3U)) return;
    pending[count++] = (MRRenderPaintPoint){(int8_t)seed_x,
                                            (int8_t)seed_y};
    mr_render_view_pixel(pixels, stride, origin_x, origin_y,
                         seed_x, seed_y, color);
    while (count) {
        MRRenderPaintPoint point = pending[--count];
        static const int8_t step[4][2] = {
            {-1, 0}, {1, 0}, {0, -1}, {0, 1}
        };
        for (int index = 0; index < 4; index++) {
            int x = point.x + step[index][0];
            int y = point.y + step[index][1];
            if (x < 0 || x > MR_RENDER_VIEW_MAX_X ||
                y < 0 || y > MR_RENDER_VIEW_MAX_Y)
                continue;
            size_t offset = (size_t)(origin_y + y) * stride +
                            (size_t)(origin_x + x);
            if ((pixels[offset] & 3U) == (color & 3U)) continue;
            pixels[offset] = color & 3U;
            pending[count++] = (MRRenderPaintPoint){(int8_t)x, (int8_t)y};
        }
    }
}

static int mr_render_view_wall(const MRRenderView *view, int depth,
                               int side) {
    if (!view || depth < 1 || depth > view->depth_count) return 0;
    const MRRenderDepth *row = &view->depth[depth - 1];
    if (side < 0) return row->left_wall;
    if (side > 0) return row->right_wall;
    return row->center_wall;
}

static int mr_render_view_inset(const MRRenderLayout *layout, int depth,
                                int axis) {
    if (!layout || depth < 1 || depth > MR_RENDER_DEPTH_ROWS) return 0;
    return layout->depth_geometry[depth - 1][axis];
}

static void mr_render_view_side_door(const MRRenderLayout *layout,
                                     uint8_t *pixels, size_t stride,
                                     int origin_x, int origin_y,
                                     int depth, int mirror,
                                     uint8_t color) {
    int a = mr_render_projection_int(layout, depth, 2);
    int b = mr_render_projection_int(layout, depth, 3);
    int c = mr_render_projection_int(layout, depth, 4);
    int d = mr_render_projection_int(layout, depth, 5);
    int e = mr_render_projection_int(layout, depth, 6);
    int f = mr_render_projection_int(layout, depth, 7);
    int x0 = mirror ? MR_RENDER_VIEW_MAX_X - a : a;
    int x1 = mirror ? MR_RENDER_VIEW_MAX_X - b : b;
    mr_render_view_line(pixels, stride, origin_x, origin_y,
                        x0, MR_RENDER_VIEW_MAX_Y - e, x0, c, color);
    mr_render_view_line(pixels, stride, origin_x, origin_y,
                        x0, c, x1, d, color);
    mr_render_view_line(pixels, stride, origin_x, origin_y,
                        x1, d, x1, MR_RENDER_VIEW_MAX_Y - f, color);
    mr_render_view_line(pixels, stride, origin_x, origin_y,
                        x1, MR_RENDER_VIEW_MAX_Y - f,
                        x0, MR_RENDER_VIEW_MAX_Y - e, color);
    if (depth < 4)
        mr_render_view_paint(pixels, stride, origin_x, origin_y,
                             mirror ? x1 + 1 : x0 + 1, c + 3, color);
}

int mr_render_draw_first_person(const MRRenderLayout *layout,
                                const MRRenderView *view,
                                int dungeon_level,
                                uint8_t *pixels, size_t stride,
                                int origin_x, int origin_y,
                                int color_enabled) {
    if (!layout || !view || !pixels || stride < MR_RENDER_WIDTH ||
        view->depth_count < 1 || origin_x < 0 || origin_y < 0 ||
        origin_x + MR_RENDER_VIEW_MAX_X >= MR_RENDER_WIDTH ||
        origin_y + MR_RENDER_VIEW_MAX_Y >= MR_RENDER_HEIGHT)
        return 0;
    uint8_t wall_color = color_enabled ? 1U : 3U; /* DS:19C8 */
    uint8_t door_color = 2U;                      /* DS:19C6 */
    mr_render_view_box(pixels, stride, origin_x, origin_y,
                       0, 0, MR_RENDER_VIEW_MAX_X,
                       MR_RENDER_VIEW_MAX_Y, 0, 1);

    /* DUNSMALL's BASIC depth index is one-based. The loop exits as soon as
     * a blocking center edge has emitted its final face. */
    for (int depth = 1; depth <= view->depth_count &&
                            depth <= MR_RENDER_DEPTH_ROWS; depth++) {
        int x = mr_render_view_inset(layout, depth, 0);
        int y = mr_render_view_inset(layout, depth, 1);
        int center = mr_render_view_wall(view, depth, 0);
        int right = mr_render_view_wall(view, depth, 1);
        int left = mr_render_view_wall(view, depth, -1);

        if (right >= 6 || mr_render_view_wall(view, depth + 1, 1) >= 6 ||
            center >= 6)
            mr_render_view_line(pixels, stride, origin_x, origin_y,
                                x, y, x, MR_RENDER_VIEW_MAX_Y - y,
                                wall_color);
        if (left >= 6 || mr_render_view_wall(view, depth + 1, -1) >= 6 ||
            center >= 6)
            mr_render_view_line(pixels, stride, origin_x, origin_y,
                                MR_RENDER_VIEW_MAX_X - x, y,
                                MR_RENDER_VIEW_MAX_X - x,
                                MR_RENDER_VIEW_MAX_Y - y, wall_color);
        if (dungeon_level > 0 || center >= 6)
            mr_render_view_line(pixels, stride, origin_x, origin_y,
                                MR_RENDER_VIEW_MAX_X - x, y, x, y,
                                wall_color);

        if (depth > 1) {
            int previous_x = mr_render_view_inset(layout, depth - 1, 0);
            int previous_y = mr_render_view_inset(layout, depth - 1, 1);
            if (right <= 5) {
                mr_render_view_box(pixels, stride, origin_x, origin_y,
                                   previous_x, y, x,
                                   MR_RENDER_VIEW_MAX_Y - y,
                                   wall_color, 0);
            } else {
                mr_render_view_line(pixels, stride, origin_x, origin_y,
                                    previous_x, previous_y, x, y,
                                    wall_color);
                mr_render_view_line(
                    pixels, stride, origin_x, origin_y,
                    previous_x, MR_RENDER_VIEW_MAX_Y - previous_y,
                    x, MR_RENDER_VIEW_MAX_Y - y, wall_color);
                if (right <= 7 && depth <= 5)
                    mr_render_view_side_door(layout, pixels, stride,
                                             origin_x, origin_y, depth, 0,
                                             door_color);
            }

            if (left <= 5) {
                mr_render_view_box(
                    pixels, stride, origin_x, origin_y,
                    MR_RENDER_VIEW_MAX_X - previous_x, y,
                    MR_RENDER_VIEW_MAX_X - x,
                    MR_RENDER_VIEW_MAX_Y - y, wall_color, 0);
            } else {
                mr_render_view_line(
                    pixels, stride, origin_x, origin_y,
                    MR_RENDER_VIEW_MAX_X - previous_x, previous_y,
                    MR_RENDER_VIEW_MAX_X - x, y, wall_color);
                mr_render_view_line(
                    pixels, stride, origin_x, origin_y,
                    MR_RENDER_VIEW_MAX_X - previous_x,
                    MR_RENDER_VIEW_MAX_Y - previous_y,
                    MR_RENDER_VIEW_MAX_X - x,
                    MR_RENDER_VIEW_MAX_Y - y, wall_color);
                if (left <= 7 && depth <= 5)
                    mr_render_view_side_door(layout, pixels, stride,
                                             origin_x, origin_y, depth, 1,
                                             door_color);
            }
        }

        if (center > 7) {
            mr_render_view_line(pixels, stride, origin_x, origin_y,
                                x, MR_RENDER_VIEW_MAX_Y - y,
                                MR_RENDER_VIEW_MAX_X - x,
                                MR_RENDER_VIEW_MAX_Y - y, wall_color);
            break;
        }
        if (center >= 6) {
            mr_render_view_line(pixels, stride, origin_x, origin_y,
                                x, MR_RENDER_VIEW_MAX_Y - y,
                                MR_RENDER_VIEW_MAX_X - x,
                                MR_RENDER_VIEW_MAX_Y - y, wall_color);
            float inner_x = (float)x;
            inner_x = (float)(inner_x +
                              (float)((MR_RENDER_VIEW_MAX_X - 2 * x) /
                                      3.0f));
            float inner_y = (float)y;
            inner_y = (float)(inner_y +
                              (float)((MR_RENDER_VIEW_MAX_Y - 2 * y) /
                                      3.0f));
            int offset = depth == 6 ? 1 : 0;
            int x0 = mr_render_qb_cint((double)(inner_x + offset));
            int y0 = mr_render_qb_cint((double)inner_y);
            int x1 = mr_render_qb_cint(
                (double)(MR_RENDER_VIEW_MAX_X - inner_x + offset));
            mr_render_view_box(pixels, stride, origin_x, origin_y,
                               x0, y0, x1,
                               MR_RENDER_VIEW_MAX_Y - y,
                               door_color, 1);
            break;
        }
    }
    return 1;
}

int mr_render_projected_sprite_placement(int basic_depth, int *use_large,
                                         int *variant, int *x, int *y) {
    /* DUNSMALL's five PUT cases are deliberately not centered. The first
     * two select the two 125-word large images; the remaining three select
     * the three 45-word compact images. VIEW performs the required crop. */
    static const int placement[5][4] = {
        {1, 0, 11, 25},
        {1, 1, 13, 26},
        {0, 0, 15, 25},
        {0, 1, 17, 24},
        {0, 2, 18, 24}
    };
    if (basic_depth < 1 || basic_depth > 5 || !use_large || !variant ||
        !x || !y)
        return 0;
    const int *row = placement[basic_depth - 1];
    *use_large = row[0];
    *variant = row[1];
    *x = row[2];
    *y = row[3];
    return 1;
}

static int mr_render_viewport_cache_height(int index) {
    return index == 1 || index == 3 ? 55 : 54;
}

void mr_render_viewport_cache_capture(MRViewportCache *cache,
                                      const MRRenderLayout *layout,
                                      const uint8_t *pixels, size_t stride,
                                      int dungeon_level, int player_x,
                                      int player_y, MRDirection facing) {
    if (!cache || !layout || !pixels || stride < MR_RENDER_WIDTH) return;
    memset(cache->pixels, 0, sizeof(cache->pixels));
    for (int pane = 0; pane < MR_RENDER_VIEWPORT_CACHE_COUNT; pane++) {
        int source_x = layout->viewport_origins[pane].x;
        int source_y = layout->viewport_origins[pane].y;
        int height = mr_render_viewport_cache_height(pane);
        for (int y = 0; y < height; y++)
            memcpy(&cache->pixels[pane]
                         [y * MR_RENDER_VIEWPORT_CACHE_WIDTH],
                   &pixels[(size_t)(source_y + y) * stride + source_x],
                   MR_RENDER_VIEWPORT_CACHE_WIDTH);
    }
    cache->dungeon_level = dungeon_level;
    cache->player_x = player_x;
    cache->player_y = player_y;
    cache->facing = facing;
    cache->valid = 1;
}

int mr_render_viewport_cache_restore(const MRViewportCache *cache,
                                     const MRRenderLayout *layout,
                                     uint8_t *pixels, size_t stride,
                                     int dungeon_level, int player_x,
                                     int player_y, MRDirection facing) {
    if (!cache || !layout || !pixels || stride < MR_RENDER_WIDTH ||
        !cache->valid || cache->dungeon_level != dungeon_level ||
        cache->player_x != player_x || cache->player_y != player_y)
        return 0;

    /* 580F stores (previous direction - current facing) MOD 4. The seven
     * origin records let each four-buffer PUT window wrap without a branch.
     * DS:52F2 and 5300 use XOR; 530A and 58D4 use PSET. */
    int rotation = (int)cache->facing - (int)facing;
    while (rotation < 0) rotation += 4;
    while (rotation >= 4) rotation -= 4;
    for (int pane = 0; pane < MR_RENDER_VIEWPORT_CACHE_COUNT; pane++) {
        int destination = rotation + pane;
        int destination_x = layout->viewport_origins[destination].x;
        int destination_y = layout->viewport_origins[destination].y;
        int height = mr_render_viewport_cache_height(pane);
        int xor_mode = pane == 0 || pane == 3;
        for (int y = 0; y < height; y++) {
            const uint8_t *source = &cache->pixels[pane]
                [y * MR_RENDER_VIEWPORT_CACHE_WIDTH];
            uint8_t *target = &pixels[(size_t)(destination_y + y) * stride +
                                      destination_x];
            for (int x = 0; x < MR_RENDER_VIEWPORT_CACHE_WIDTH; x++)
                target[x] = xor_mode ? (target[x] ^ source[x]) & 3U
                                     : source[x];
        }
    }
    return 1;
}

static int mr_render_near(double left, double right) {
    return fabs(left - right) < 0.00001;
}

static uint64_t mr_render_raster_hash(const uint8_t *pixels, size_t size) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t index = 0; index < size; index++) {
        hash ^= pixels[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

int mr_render_self_test(const char *directory, char *error,
                        size_t error_size) {
    MRRenderLayout layout;
    if (!mr_render_layout_load(&layout, directory, error, error_size))
        return 0;

    int map_x = 0, map_y = 0;
    mr_render_map_pixel(10, 10, &map_x, &map_y);
    int failed = MR_RENDER_WIDTH != 320 || MR_RENDER_HEIGHT != 200 ||
                 MR_RENDER_TEXT_COLUMNS != 40 || MR_RENDER_TEXT_ROWS != 25 ||
                 layout.depth_geometry[0][0] != 0 ||
                 layout.depth_geometry[5][1] != 24 ||
                 layout.viewport_origins[0].x != 214 ||
                 layout.viewport_origins[0].y != 57 ||
                 layout.viewport_origins[1].x != 267 ||
                 layout.viewport_origins[1].y != 95 ||
                 layout.viewport_origins[2].x != 214 ||
                 layout.viewport_origins[2].y != 137 ||
                 layout.viewport_origins[3].x != 161 ||
                 layout.viewport_origins[3].y != 95 ||
                 !mr_render_near(layout.projection[1][4], 16.25) ||
                 !mr_render_near(layout.projection[3][2], 16.33333) ||
                 !mr_render_near(layout.projection[4][5], 25.08333) ||
                 map_x != 73 || map_y != 118 ||
                 mr_render_direction_for_pass(4, 1) != 1 ||
                 mr_render_direction_for_pass(2, 3) != 1 ||
                 mr_render_direction_for_pass(0, 0) != 0;
    MRCharacter character;
    memset(&character, 0, sizeof(character));
    character.dungeon_level = 1;
    character.dungeon_generation_seed = 3;
    character.player_x = 10;
    character.player_y = 10;
    int occupancy[21][22] = {{0}};
    MRDirection trace_direction = MR_DIRECTION_NORTH;
    for (int candidate = MR_DIRECTION_NORTH;
         candidate <= MR_DIRECTION_WEST; candidate++) {
        if (mr_world_wall_value_for_step(1, 3.0f, 10, 10,
                                         (MRDirection)candidate) < 6) {
            trace_direction = (MRDirection)candidate;
            break;
        }
    }
    int monster_x = 10, monster_y = 10;
    mr_render_step(&monster_x, &monster_y, trace_direction);
    occupancy[monster_y][monster_x] = 77;
    MRRenderView view;
    int traced = mr_render_trace_view(&character, occupancy,
                                      trace_direction, &view);
    failed |= traced < 1 || traced > MR_RENDER_DEPTH_ROWS ||
              view.depth[0].x != 10 || view.depth[0].y != 10 ||
              view.depth[0].monster_global_index != 77 ||
              view.depth[0].center_wall !=
                  mr_world_wall_value_for_step(1, 3.0f, 10, 10,
                                               trace_direction) ||
              view.depth[0].left_wall !=
                  mr_world_wall_value_for_step(1, 3.0f, 10, 10,
                                               mr_render_turn_left(
                                                   trace_direction)) ||
              view.depth[0].right_wall !=
                  mr_world_wall_value_for_step(1, 3.0f, 10, 10,
                                               mr_render_turn_right(
                                                   trace_direction));

    int use_large = 0, variant = -1, sprite_x = -1, sprite_y = -1;
    failed |= !mr_render_projected_sprite_placement(
                  1, &use_large, &variant, &sprite_x, &sprite_y) ||
              !use_large || variant != 0 || sprite_x != 11 ||
              sprite_y != 25;
    failed |= !mr_render_projected_sprite_placement(
                  2, &use_large, &variant, &sprite_x, &sprite_y) ||
              !use_large || variant != 1 || sprite_x != 13 ||
              sprite_y != 26;
    failed |= !mr_render_projected_sprite_placement(
                  5, &use_large, &variant, &sprite_x, &sprite_y) ||
              use_large || variant != 2 || sprite_x != 18 ||
              sprite_y != 24;
    failed |= mr_render_projected_sprite_placement(
                  6, &use_large, &variant, &sprite_x, &sprite_y);

    /* The original projection treats passable 6/7 doors as opaque. Find a
     * deterministic procedural door and prove an occupant immediately behind
     * it is not recorded in the view. */
    int found_opaque_door = 0;
    for (int seed = 1; seed <= 100 && !found_opaque_door; seed++) {
        for (int candidate = MR_DIRECTION_NORTH;
             candidate <= MR_DIRECTION_WEST && !found_opaque_door;
             candidate++) {
            int wall = mr_world_wall_value_for_step(
                1, (float)seed, 10, 10, (MRDirection)candidate);
            if (wall != 6 && wall != 7) continue;
            memset(occupancy, 0, sizeof(occupancy));
            int hidden_x = 10, hidden_y = 10;
            mr_render_step(&hidden_x, &hidden_y, (MRDirection)candidate);
            occupancy[hidden_y][hidden_x] = 88;
            character.dungeon_generation_seed = seed;
            traced = mr_render_trace_view(&character, occupancy,
                                          (MRDirection)candidate, &view);
            failed |= traced != 1 || view.depth[0].center_wall != wall ||
                      view.depth[0].monster_global_index != 0;
            found_opaque_door = 1;
        }
    }
    failed |= !found_opaque_door;

    uint8_t viewport_raster[MR_RENDER_WIDTH * MR_RENDER_HEIGHT];
    MRRenderView synthetic_view;
    memset(&synthetic_view, 0, sizeof(synthetic_view));
    memset(viewport_raster, 0, sizeof(viewport_raster));
    synthetic_view.depth_count = 1;
    synthetic_view.depth[0].center_wall = 8;
    failed |= !mr_render_draw_first_person(
                  &layout, &synthetic_view, 1, viewport_raster,
                  MR_RENDER_WIDTH, 0, 0, 1) ||
              viewport_raster[0] != 1 ||
              viewport_raster[26 * MR_RENDER_WIDTH] != 1 ||
              viewport_raster[26 * MR_RENDER_WIDTH + 26] != 0 ||
              viewport_raster[53 * MR_RENDER_WIDTH + 52] != 1;
    /* Full-frame fixtures freeze every LINE/B/BF/PAINT pixel, rather than
     * allowing the point samples above to miss a tie-breaking regression. */
    failed |= mr_render_raster_hash(viewport_raster,
                                    sizeof(viewport_raster)) !=
              UINT64_C(2569178156930840165);

    /* A center value 6/7 produces the BF door slab computed from the first
     * F2 inset: rounded (17.333,17.666) through (34.666,53). */
    memset(&synthetic_view, 0, sizeof(synthetic_view));
    memset(viewport_raster, 0, sizeof(viewport_raster));
    synthetic_view.depth_count = 1;
    synthetic_view.depth[0].center_wall = 6;
    mr_render_draw_first_person(&layout, &synthetic_view, 1,
                                viewport_raster, MR_RENDER_WIDTH,
                                0, 0, 1);
    failed |= viewport_raster[18 * MR_RENDER_WIDTH + 17] != 2 ||
              viewport_raster[30 * MR_RENDER_WIDTH + 26] != 2 ||
              viewport_raster[18 * MR_RENDER_WIDTH + 16] != 0 ||
              viewport_raster[53 * MR_RENDER_WIDTH + 35] != 2;
    failed |= mr_render_raster_hash(viewport_raster,
                                    sizeof(viewport_raster)) !=
              UINT64_C(5891452182116051032);

    /* At depth two a side value 6/7 uses all six integer-coerced projection
     * columns, closes a four-line door polygon, and PAINTs its interior. */
    memset(&synthetic_view, 0, sizeof(synthetic_view));
    memset(viewport_raster, 0, sizeof(viewport_raster));
    synthetic_view.depth_count = 2;
    synthetic_view.depth[1].right_wall = 6;
    mr_render_draw_first_person(&layout, &synthetic_view, 1,
                                viewport_raster, MR_RENDER_WIDTH,
                                0, 0, 1);
    failed |= mr_render_projection_int(&layout, 4, 3) != 17 ||
              mr_render_projection_int(&layout, 5, 4) != 24 ||
              viewport_raster[20 * MR_RENDER_WIDTH + 4] != 2 ||
              viewport_raster[20 * MR_RENDER_WIDTH + 2] != 0 ||
              viewport_raster[16 * MR_RENDER_WIDTH + 3] != 2;
    failed |= mr_render_raster_hash(viewport_raster,
                                    sizeof(viewport_raster)) !=
              UINT64_C(13745996384088542784);

    memset(viewport_raster, 0, sizeof(viewport_raster));
    memset(&synthetic_view, 0, sizeof(synthetic_view));
    synthetic_view.depth_count = 1;
    synthetic_view.depth[0].center_wall = 8;
    mr_render_draw_first_person(&layout, &synthetic_view, 1,
                                viewport_raster, MR_RENDER_WIDTH,
                                0, 0, 0);
    failed |= viewport_raster[26 * MR_RENDER_WIDTH] != 3;

    /* Exercise the original four-GET turning fast path independently of the
     * wall tracer. Facing north captures panes 1..4; turning east produces a
     * rotation of three and restores them at left/front/right/back. */
    MRViewportCache viewport_cache;
    uint8_t cache_source[MR_RENDER_WIDTH * MR_RENDER_HEIGHT];
    uint8_t cache_destination[MR_RENDER_WIDTH * MR_RENDER_HEIGHT];
    memset(&viewport_cache, 0, sizeof(viewport_cache));
    memset(cache_source, 0, sizeof(cache_source));
    for (int pane = 0; pane < MR_RENDER_VIEWPORT_CACHE_COUNT; pane++) {
        int source_x = layout.viewport_origins[pane].x;
        int source_y = layout.viewport_origins[pane].y;
        int height = mr_render_viewport_cache_height(pane);
        uint8_t color = (uint8_t)(pane == 3 ? 1 : pane + 1);
        for (int y = 0; y < height; y++)
            memset(&cache_source[(source_y + y) * MR_RENDER_WIDTH + source_x],
                   color, MR_RENDER_VIEWPORT_CACHE_WIDTH);
    }
    mr_render_viewport_cache_capture(
        &viewport_cache, &layout, cache_source, MR_RENDER_WIDTH,
        7, 8, 9, MR_DIRECTION_NORTH);
    memset(cache_destination, 3, sizeof(cache_destination));
    failed |= !mr_render_viewport_cache_restore(
                  &viewport_cache, &layout, cache_destination,
                  MR_RENDER_WIDTH, 7, 8, 9, MR_DIRECTION_EAST) ||
              cache_destination[
                  layout.viewport_origins[3].y * MR_RENDER_WIDTH +
                  layout.viewport_origins[3].x] != 2 ||
              cache_destination[
                  layout.viewport_origins[4].y * MR_RENDER_WIDTH +
                  layout.viewport_origins[4].x] != 2 ||
              cache_destination[
                  layout.viewport_origins[5].y * MR_RENDER_WIDTH +
                  layout.viewport_origins[5].x] != 3 ||
              cache_destination[
                  layout.viewport_origins[6].y * MR_RENDER_WIDTH +
                  layout.viewport_origins[6].x] != 2 ||
              mr_render_viewport_cache_restore(
                  &viewport_cache, &layout, cache_destination,
                  MR_RENDER_WIDTH, 7, 9, 9, MR_DIRECTION_EAST);
    failed |= mr_render_raster_hash(cache_destination,
                                    sizeof(cache_destination)) !=
              UINT64_C(6411683595360545801);

    MRWorldData world;
    if (!mr_world_load(&world, directory, error, error_size)) return 0;
    MRSave save;
    uint8_t raster[MR_RENDER_WIDTH * MR_RENDER_HEIGHT];
    memset(&save, 0, sizeof(save));
    memset(raster, 0, sizeof(raster));
    save.character.dungeon_level = 2;
    save.character.dungeon_generation_seed = 3;
    save.character.player_x = 10;
    save.character.player_y = 10;
    save.explored[2][2] |= 1U << 19; /* (1,2), known upward ladder. */
    failed |= !mr_render_draw_dungeon_map(
        &save, &world, raster, MR_RENDER_WIDTH, 1, 0,
        MR_DIRECTION_NORTH);
    /* Cell (1,2) begins at (0,53): west boundary plus the unfilled B box
     * emitted for vertical feature -3. */
    failed |= raster[53 * MR_RENDER_WIDTH] != 2 ||
              raster[55 * MR_RENDER_WIDTH + 2] != 1 ||
              raster[56 * MR_RENDER_WIDTH + 3] != 0;

    memset(&save, 0, sizeof(save));
    memset(raster, 0, sizeof(raster));
    save.character.dungeon_level = 1;
    save.character.dungeon_generation_seed = 3;
    save.explored[1][1] |= 1U << (20 - 14); /* Known down-one ladder. */
    mr_render_draw_dungeon_map(&save, &world, raster, MR_RENDER_WIDTH,
                               1, 0, MR_DIRECTION_NORTH);
    /* Cell (14,1) begins at (104,45); positive feature values use BF. */
    failed |= raster[47 * MR_RENDER_WIDTH + 106] != 1 ||
              raster[49 * MR_RENDER_WIDTH + 108] != 1;
    memset(raster, 0, sizeof(raster));
    save.character.player_x = 14;
    save.character.player_y = 1;
    mr_render_draw_dungeon_map(&save, &world, raster, MR_RENDER_WIDTH,
                               1, 1, MR_DIRECTION_NORTH);
    /* PUT/XOR turns the filled green ladder pixel into logical red here;
     * PSET or an invented transparent sprite would leave logical white. */
    failed |= raster[47 * MR_RENDER_WIDTH + 107] != 2;

    /* DUNSMALL 55DD-561C returns zero for a level-zero endpoint whose
     * matching deeper ladder overshoots the surface.  The feature overlay
     * consequently draws the same circle as a chute even though level zero
     * makes it inert.  The first marked 7.NUM cell is one such fixture. */
    memset(&save, 0, sizeof(save));
    memset(raster, 0, sizeof(raster));
    save.character.dungeon_level = 0;
    save.character.dungeon_generation_seed = 3;
    save.explored[0][2] |= 1U << 19; /* (1,2), inert surface circle. */
    mr_render_draw_dungeon_map(&save, &world, raster, MR_RENDER_WIDTH,
                               1, 0, MR_DIRECTION_NORTH);
    failed |= raster[54 * MR_RENDER_WIDTH + 4] != 3 ||
              raster[57 * MR_RENDER_WIDTH + 4] != 0 ||
              raster[57 * MR_RENDER_WIDTH + 7] != 3;

    /* Exercise the two remaining feature classes with an otherwise empty
     * special-cell table. The hash decides the original feature type; 7.NUM
     * merely decides whether that deterministic result is active. */
    MRWorldData synthetic_world;
    int chute_level = 0, chute_x = 0, chute_y = 0;
    int false_level = 0, false_x = 0, false_y = 0;
    for (int level = 0; level < 70 && (!chute_x || !false_x); level++) {
        for (int y = 1; y <= 19 && (!chute_x || !false_x); y++) {
            for (int x = 1; x <= 20; x++) {
                int feature = mr_world_vertical_feature_hash(level, x, y, 0);
                if (!chute_x && feature == 0) {
                    chute_level = level;
                    chute_x = x;
                    chute_y = y;
                }
                if (!false_x && feature < 0) {
                    false_level = level;
                    false_x = x;
                    false_y = y;
                }
            }
        }
    }
    if (!chute_x || !false_x) {
        failed = 1;
    } else {
        memset(&synthetic_world, 0, sizeof(synthetic_world));
        memset(&save, 0, sizeof(save));
        memset(raster, 0, sizeof(raster));
        synthetic_world.special_cells[chute_level][chute_y] |=
            1U << (20 - chute_x);
        save.character.dungeon_level = chute_level;
        save.character.dungeon_generation_seed = 3;
        save.explored[chute_level][chute_y] |= 1U << (20 - chute_x);
        mr_render_draw_dungeon_map(&save, &synthetic_world, raster,
                                   MR_RENDER_WIDTH, 1, 0,
                                   MR_DIRECTION_NORTH);
        int chute_cx = (chute_x - 1) * 8 + 4;
        int chute_cy = chute_y * 8 + 37 + 4;
        failed |= raster[(chute_cy - 3) * MR_RENDER_WIDTH + chute_cx] != 3 ||
                  raster[chute_cy * MR_RENDER_WIDTH + chute_cx] != 0 ||
                  raster[chute_cy * MR_RENDER_WIDTH + chute_cx + 3] != 3;

        memset(&synthetic_world, 0, sizeof(synthetic_world));
        memset(&save, 0, sizeof(save));
        memset(raster, 0, sizeof(raster));
        synthetic_world.special_cells[false_level][false_y] |=
            1U << (20 - false_x);
        save.character.dungeon_level = false_level;
        save.character.dungeon_generation_seed = 3;
        save.explored[false_level][false_y] |= 1U << (20 - false_x);
        mr_render_draw_dungeon_map(&save, &synthetic_world, raster,
                                   MR_RENDER_WIDTH, 1, 0,
                                   MR_DIRECTION_NORTH);
        int false_cx = (false_x - 1) * 8 + 4;
        int false_cy = false_y * 8 + 37 + 4;
        failed |= raster[false_cy * MR_RENDER_WIDTH + false_cx] != 0 ||
                  raster[(false_cy - 2) * MR_RENDER_WIDTH + false_cx] != 0;
    }

    /* Locate one statically generated east door and prove its full line is
     * cut back to the original five-pixel vertical opening. */
    int door_x = 0, door_y = 0;
    for (int y = 2; y <= 18 && !door_x; y++) {
        for (int x = 2; x <= 19; x++) {
            int wall = mr_world_wall_value_for_step(
                1, 3.0f, x, y, MR_DIRECTION_EAST);
            if (wall == 6 || wall == 7) {
                door_x = x;
                door_y = y;
                break;
            }
        }
    }
    memset(&save, 0, sizeof(save));
    memset(raster, 0, sizeof(raster));
    save.character.dungeon_level = 1;
    save.character.dungeon_generation_seed = 3;
    if (!door_x) {
        failed = 1;
    } else {
        save.explored[1][door_y] |= 1U << (20 - door_x);
        mr_render_draw_dungeon_map(&save, &world, raster, MR_RENDER_WIDTH,
                                   1, 0, MR_DIRECTION_NORTH);
        int edge_x = door_x * 8;
        int edge_y = door_y * 8 + 37;
        failed |= raster[edge_y * MR_RENDER_WIDTH + edge_x] != 2 ||
                  raster[(edge_y + 1) * MR_RENDER_WIDTH + edge_x] != 2 ||
                  raster[(edge_y + 2) * MR_RENDER_WIDTH + edge_x] != 0 ||
                  raster[(edge_y + 6) * MR_RENDER_WIDTH + edge_x] != 0 ||
                  raster[(edge_y + 7) * MR_RENDER_WIDTH + edge_x] != 2;
    }

    /* An isolated revealed cell owns its north seam. Once the northern cell
     * is revealed, that neighbor owns the identical seam as its south edge.
     * This is the source predicate at 4DF3-4F52, and specifically guards
     * against the old inverted test that produced open, fragmented maps. */
    int seam_x = 0, seam_y = 0;
    for (int y = 2; y <= 18 && !seam_x; y++) {
        for (int x = 2; x <= 19; x++) {
            if (mr_world_wall_value_for_step(
                    1, 3.0f, x, y, MR_DIRECTION_NORTH) >= 8) {
                seam_x = x;
                seam_y = y;
                break;
            }
        }
    }
    memset(&save, 0, sizeof(save));
    memset(raster, 0, sizeof(raster));
    save.character.dungeon_level = 1;
    save.character.dungeon_generation_seed = 3;
    if (!seam_x) {
        failed = 1;
    } else {
        int seam_px = (seam_x - 1) * 8 + 4;
        int seam_py = seam_y * 8 + 37;
        save.explored[1][seam_y] |= 1U << (20 - seam_x);
        mr_render_draw_dungeon_map(&save, &world, raster, MR_RENDER_WIDTH,
                                   1, 0, MR_DIRECTION_NORTH);
        failed |= raster[seam_py * MR_RENDER_WIDTH + seam_px] != 2;
        memset(raster, 0, sizeof(raster));
        save.explored[1][seam_y - 1] |= 1U << (20 - seam_x);
        mr_render_draw_dungeon_map(&save, &world, raster, MR_RENDER_WIDTH,
                                   1, 0, MR_DIRECTION_NORTH);
        failed |= raster[seam_py * MR_RENDER_WIDTH + seam_px] != 2;
    }

    int west_seam_x = 0, west_seam_y = 0;
    for (int y = 2; y <= 18 && !west_seam_x; y++) {
        for (int x = 2; x <= 19; x++) {
            if (mr_world_wall_value_for_step(
                    1, 3.0f, x, y, MR_DIRECTION_WEST) >= 8) {
                west_seam_x = x;
                west_seam_y = y;
                break;
            }
        }
    }
    memset(&save, 0, sizeof(save));
    memset(raster, 0, sizeof(raster));
    save.character.dungeon_level = 1;
    save.character.dungeon_generation_seed = 3;
    if (!west_seam_x) {
        failed = 1;
    } else {
        int seam_px = (west_seam_x - 1) * 8;
        int seam_py = west_seam_y * 8 + 37 + 4;
        save.explored[1][west_seam_y] |= 1U << (20 - west_seam_x);
        mr_render_draw_dungeon_map(&save, &world, raster, MR_RENDER_WIDTH,
                                   1, 0, MR_DIRECTION_NORTH);
        failed |= raster[seam_py * MR_RENDER_WIDTH + seam_px] != 2;
        memset(raster, 0, sizeof(raster));
        save.explored[1][west_seam_y] |=
            1U << (20 - (west_seam_x - 1));
        mr_render_draw_dungeon_map(&save, &world, raster, MR_RENDER_WIDTH,
                                   1, 0, MR_DIRECTION_NORTH);
        failed |= raster[seam_py * MR_RENDER_WIDTH + seam_px] != 2;
    }

    /* The player PUT is constrained to the original 20x19 playable map.
     * Storage row 20 exists in the BSAVE array but is not a screen cell. */
    memset(&save, 0, sizeof(save));
    memset(raster, 0, sizeof(raster));
    save.character.dungeon_level = 1;
    save.character.dungeon_generation_seed = 3;
    save.character.player_x = 10;
    save.character.player_y = 20;
    mr_render_draw_dungeon_map(&save, &world, raster, MR_RENDER_WIDTH,
                               1, 1, MR_DIRECTION_NORTH);
    failed |= mr_render_raster_hash(raster, sizeof(raster)) !=
              UINT64_C(15930635710634961701);

    /* Level zero uses the five statically captured SBTIW glyph buffers.
     * Check one coordinate from every dispatch arm and the exact crop rows. */
    static const int town_cells[5][2] = {
        {18, 3}, {13, 3}, {7, 15}, {7, 3}, {6, 14}
    };
    memset(&save, 0, sizeof(save));
    memset(raster, 0, sizeof(raster));
    save.character.dungeon_level = 0;
    save.character.dungeon_generation_seed = 3;
    for (int marker = 0; marker < 5; marker++)
        save.explored[0][town_cells[marker][1]] |=
            1U << (20 - town_cells[marker][0]);
    mr_render_draw_dungeon_map(&save, &world, raster, MR_RENDER_WIDTH,
                               1, 0, MR_DIRECTION_NORTH);
    /* S top row 0111100. */
    map_x = (18 - 1) * 8 + 1;
    map_y = 3 * 8 + 38;
    failed |= raster[map_y * MR_RENDER_WIDTH + map_x] != 0 ||
              raster[map_y * MR_RENDER_WIDTH + map_x + 1] != 3 ||
              raster[map_y * MR_RENDER_WIDTH + map_x + 4] != 3 ||
              raster[map_y * MR_RENDER_WIDTH + map_x + 5] != 0;
    /* B top row 1111110; T's second row is 1011010. */
    map_x = (13 - 1) * 8 + 1;
    map_y = 3 * 8 + 38;
    failed |= raster[map_y * MR_RENDER_WIDTH + map_x] != 3 ||
              raster[map_y * MR_RENDER_WIDTH + map_x + 5] != 3 ||
              raster[map_y * MR_RENDER_WIDTH + map_x + 6] != 0;
    map_x = (7 - 1) * 8 + 1;
    map_y = 15 * 8 + 38;
    failed |= raster[(map_y + 1) * MR_RENDER_WIDTH + map_x] != 3 ||
              raster[(map_y + 1) * MR_RENDER_WIDTH + map_x + 1] != 0 ||
              raster[(map_y + 1) * MR_RENDER_WIDTH + map_x + 2] != 3;
    /* I top row 0111100; W top row 1100011. */
    map_x = (7 - 1) * 8 + 1;
    map_y = 3 * 8 + 38;
    failed |= raster[map_y * MR_RENDER_WIDTH + map_x] != 0 ||
              raster[map_y * MR_RENDER_WIDTH + map_x + 1] != 3 ||
              raster[(map_y + 1) * MR_RENDER_WIDTH + map_x + 3] != 3;
    map_x = (6 - 1) * 8 + 1;
    map_y = 14 * 8 + 38;
    failed |= raster[map_y * MR_RENDER_WIDTH + map_x] != 3 ||
              raster[map_y * MR_RENDER_WIDTH + map_x + 1] != 3 ||
              raster[map_y * MR_RENDER_WIDTH + map_x + 2] != 0 ||
              raster[map_y * MR_RENDER_WIDTH + map_x + 5] != 3;

    /* The player image is the statically recovered, cropped CP437 arrow,
     * not a replacement square. Check the seven-pixel shaft of the east
     * glyph and color-disabled foreground selection. */
    memset(&save, 0, sizeof(save));
    memset(raster, 0, sizeof(raster));
    save.character.dungeon_level = 1;
    save.character.dungeon_generation_seed = 3;
    save.character.player_x = 10;
    save.character.player_y = 10;
    mr_render_draw_dungeon_map(&save, &world, raster, MR_RENDER_WIDTH,
                               0, 1, MR_DIRECTION_EAST);
    map_x = 10 * 8 - 7;
    map_y = 10 * 8 + 38;
    failed |= raster[(map_y + 1) * MR_RENDER_WIDTH + map_x + 3] != 3 ||
              raster[(map_y + 3) * MR_RENDER_WIDTH + map_x] != 3 ||
              raster[(map_y + 3) * MR_RENDER_WIDTH + map_x + 6] != 3 ||
              raster[map_y * MR_RENDER_WIDTH + map_x + 3] != 0;
    failed |= mr_render_raster_hash(raster, sizeof(raster)) !=
              UINT64_C(18307004334959797150);
    if (failed) {
        mr_render_error(error, error_size,
                        "DUNSMALL renderer-layout transcription mismatch",
                        NULL);
        return 0;
    }
    return 1;
}
