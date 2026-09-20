#ifndef MR_RENDER_H
#define MR_RENDER_H

#include <stddef.h>

#include "mr_save.h"
#include "mr_world.h"

/* DUNSMALL executes SCREEN 1.  These are original logical coordinates, not
 * coordinates inferred from a screenshot or from a scaled native window. */
#define MR_RENDER_WIDTH 320
#define MR_RENDER_HEIGHT 200
#define MR_RENDER_TEXT_COLUMNS 40
#define MR_RENDER_TEXT_ROWS 25
#define MR_RENDER_TEXT_CELL_WIDTH 8
#define MR_RENDER_TEXT_CELL_HEIGHT 8
#define MR_RENDER_DEPTH_ROWS 6
#define MR_RENDER_VIEWPORT_ORIGINS 7
#define MR_RENDER_PROJECTION_ROWS 5
#define MR_RENDER_PROJECTION_VALUES 8
#define MR_RENDER_VIEWPORT_CACHE_COUNT 4
#define MR_RENDER_VIEWPORT_CACHE_WIDTH 53
#define MR_RENDER_VIEWPORT_CACHE_HEIGHT 55

typedef struct MRViewportOrigin {
    int x;
    int y;
} MRViewportOrigin;

typedef struct MRRenderLayout {
    int depth_geometry[MR_RENDER_DEPTH_ROWS][2];
    MRViewportOrigin viewport_origins[MR_RENDER_VIEWPORT_ORIGINS];
    double projection[MR_RENDER_PROJECTION_ROWS]
                     [MR_RENDER_PROJECTION_VALUES];
} MRRenderLayout;

typedef struct MRRenderDepth {
    int x;
    int y;
    int center_wall;
    int left_wall;
    int right_wall;
    int monster_global_index;
} MRRenderDepth;

typedef struct MRRenderView {
    MRDirection direction;
    int depth_count;
    MRRenderDepth depth[MR_RENDER_DEPTH_ROWS];
} MRRenderView;

/* DUNSMALL's four GET buffers retain the last fully rendered cardinal panes.
 * Front/back captures are 53x54; right/left captures include the adjacent
 * 55th scanline. Turning in place PUTs these buffers through the seven-entry
 * wraparound origin table without changing their captured orientation. */
typedef struct MRViewportCache {
    uint8_t pixels[MR_RENDER_VIEWPORT_CACHE_COUNT]
                  [MR_RENDER_VIEWPORT_CACHE_WIDTH *
                   MR_RENDER_VIEWPORT_CACHE_HEIGHT];
    int dungeon_level;
    int player_x;
    int player_y;
    MRDirection facing;
    int valid;
} MRViewportCache;

/* Load the numeric renderer records read by the original executable from
 * F2.COM.  The file remains external original-game data. */
int mr_render_layout_load(MRRenderLayout *layout, const char *directory,
                          char *error, size_t error_size);

/* Direct transcriptions of the coordinate/direction expressions at
 * DUNSMALL:4A52-4A79 and 59DA-5A19. */
void mr_render_map_pixel(int player_x, int player_y, int *pixel_x,
                         int *pixel_y);
int mr_render_direction_for_pass(int facing, int pass);

/* Static translation of DUNSMALL 4CC3-53ED and 485C-48F4. The original
 * redraw walks the revealed 20x19 map, draws only the boundary fragments
 * justified by adjacent revealed cells, cuts the characteristic doorway
 * gaps for procedural values 6..7, overlays vertical-feature marks and the
 * floor-zero S/B/T/I/W town glyphs, and finally XORs one of the four cropped
 * BIOS arrow glyphs into the player's 7x7 cell interior. The destination is
 * a SCREEN 1 color-index raster and is intentionally not cleared by this
 * routine. */
int mr_render_draw_dungeon_map(const MRSave *save, const MRWorldData *world,
                               uint8_t *pixels, size_t stride,
                               int color_enabled, int draw_player,
                               MRDirection facing);

/* Static translation of DUNSMALL 59DA-6144. It follows one cardinal ray,
 * retains the original 0..9 wall values for all three edges, records the
 * first six occupancy cells, and stops after the first blocking center edge. */
int mr_render_trace_view(const MRCharacter *character,
                         const int occupancy[21][22],
                         MRDirection direction, MRRenderView *view);

/* Pure SCREEN 1 wall/door compositor translated from DUNSMALL 62E2-699D.
 * It consumes an already traced cardinal view and the original F2.COM
 * geometry, clears the 53x54 local viewport, and emits the exact LINE B/BF
 * and PAINT shapes. Monster PUTs remain a separate layer. */
int mr_render_draw_first_person(const MRRenderLayout *layout,
                                const MRRenderView *view,
                                int dungeon_level,
                                uint8_t *pixels, size_t stride,
                                int origin_x, int origin_y,
                                int color_enabled);

/* Exact local PUT placement and source-bitmap selection from
 * DUNSMALL 69FA-6B3B. basic_depth is one-based. */
int mr_render_projected_sprite_placement(int basic_depth, int *use_large,
                                         int *variant, int *x, int *y);

/* Exact GET/PUT fast path at DUNSMALL 580F-58F6 and 6DE7-6EC9. Capture after
 * a complete four-ray render; restore returns zero when movement/floor state
 * invalidates the cache and one after rotating it into the current panes. */
void mr_render_viewport_cache_capture(MRViewportCache *cache,
                                      const MRRenderLayout *layout,
                                      const uint8_t *pixels, size_t stride,
                                      int dungeon_level, int player_x,
                                      int player_y, MRDirection facing);
int mr_render_viewport_cache_restore(const MRViewportCache *cache,
                                     const MRRenderLayout *layout,
                                     uint8_t *pixels, size_t stride,
                                     int dungeon_level, int player_x,
                                     int player_y, MRDirection facing);

int mr_render_self_test(const char *directory, char *error,
                        size_t error_size);

#endif
