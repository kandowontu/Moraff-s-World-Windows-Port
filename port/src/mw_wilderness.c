#include "mw_wilderness.h"
#include "mw_game.h"
#include "mw_trainer.h"
#include "mw_model_viewer.h"
#include <stdio.h>
#include <stdlib.h>

/* MW_PORT: WORLD func_1A765/1A786/1ADB1 (signed midpoint terrain),
 * func_1B169 (the 256-colour wilderness DAC ramp), func_1B5DB (the original
 * 1024x768 landscape rasterizer), func_1C92C/1CB3D (projected labels), and
 * func_1CCB5 (wrapped outdoor movement/boat/entrance loop).
 *
 * The 1024x768 game does not use a top-down wilderness map.  It generates a
 * 257x129 height page, projects each terrain cell to three horizontal by four
 * depth pixels, and subtracts six screen pixels per height unit.  Visible
 * ridges are colour-indexed and the hidden parts of each vertical scan are
 * erased to black, producing the distinctive overlapping wireframe ranges. */

enum {
    WILD_X_MIN = 0x40, WILD_X_MAX = 0x3F80,
    WILD_Y_MIN = 0x40, WILD_Y_MAX = 0x1F80,
    WILD_X_SPAN = 0x3F80, WILD_Y_SPAN = 0x1F80
};

static int wrap_coord(int value, int low, int high, int span) {
    if (value < low) value += span;
    if (value > high) value -= span;
    return value;
}

static int map_height_code(u8 value) {
    if (value == 0x4F) return 14;
    if (value == 0x50) return 28;
    return -20;
}

static u32 terrain_hash(int x, int y) {
    u32 v = (u32)x * 0x45D9F3Bu ^ (u32)y * 0x119DE1F3u ^ 0xA765u;
    v ^= v >> 16;
    v *= 0x45D9F3Bu;
    v ^= v >> 16;
    return v;
}

enum {
    WILD_PAGE_W = 257,
    WILD_PAGE_H = 129,
    WILD_RENDER_X = 0,
    WILD_RENDER_Y = 255,
    WILD_RENDER_W = 768,
    WILD_RENDER_H = 512
};

typedef struct WildernessPage {
    const u8 *worldmap;
    int worldmap_size;
    int base_x;
    int base_y;
    int valid;
    int height[WILD_PAGE_H][WILD_PAGE_W];
} WildernessPage;

static WildernessPage s_local_page;

static int world_tile(const Game *g, int tx, int ty) {
    if (!g->worldmap_data || g->worldmap_data_size < 4096) return 0x20;
    tx &= 63;
    ty &= 63;
    return g->worldmap_data[ty * 64 + tx];
}

static int world_rand_step(u32 *state) {
    /* WORLD _rand at 0x16A7: state = state * 0x015A4E35 + 1, then return the
     * low 15 bits of the high word. */
    *state = *state * 0x015A4E35u + 1u;
    return (int)((*state >> 16) & 0x7FFFu);
}

/* WORLD func_1A786: average two endpoints, add its coordinate-seeded signed
 * displacement (capped at 36), then reflect values which leave -125..125.
 * This includes the original runtime's two-stage srand/rand sequence. */
static int displaced_midpoint(int a, int b, int world_x, int world_y, int step) {
    int amplitude = step > 36 ? 36 : step;
    u32 state = (u16)(world_x + 100);
    int first = world_rand_step(&state);
    int mixed_seed = first * (world_y + 100) / 0x8000;
    state = (u16)mixed_seed;
    int jitter = world_rand_step(&state) * amplitude / 0x8000 - amplitude / 2;
    int middle = (a + b) / 2;
    int value = middle + jitter;
    if (value > 125 || value < -125) value = middle - jitter;
    return value;
}

/* WORLD func_1ADB1.  Four coarse WORLDMAP.BIN anchors seed a rectangular
 * diamond/midpoint subdivision at 128,64,...,1 cell spacing. */
static void build_local_height_page(const Game *g, int base_x, int base_y,
                                    int out[WILD_PAGE_H][WILD_PAGE_W]) {
    int tx = base_x >> 8;
    int ty = base_y >> 7;
    out[0][0] = map_height_code((u8)world_tile(g, tx, ty));
    out[0][256] = map_height_code((u8)world_tile(g, tx + 1, ty));
    out[128][0] = map_height_code((u8)world_tile(g, tx, ty + 1));
    out[128][256] = map_height_code((u8)world_tile(g, tx + 1, ty + 1));

    out[0][128] = displaced_midpoint(out[0][0], out[0][256],
                                     base_x + 128, base_y, 128);
    out[128][128] = displaced_midpoint(out[128][0], out[128][256],
                                       base_x + 128, base_y + 128, 128);

    for (int half = 64; half >= 1; half >>= 1) {
        int span = half * 2;
        for (int y = 0; y < 128; y += span) {
            for (int x = 0; x < 256; x += span) {
                int top = displaced_midpoint(out[y][x], out[y][x + span],
                                              base_x + x + half, base_y + y,
                                              half);
                int bottom = displaced_midpoint(out[y + span][x],
                                                 out[y + span][x + span],
                                                 base_x + x + half,
                                                 base_y + y + span, half);
                out[y][x + half] = top;
                out[y + span][x + half] = bottom;
                out[y + half][x + half] = displaced_midpoint(
                    top, bottom, base_x + x + half, base_y + y + half, half);
                out[y + half][x] = displaced_midpoint(
                    out[y][x], out[y + span][x], base_x + x,
                    base_y + y + half, half);
                out[y + half][x + span] = displaced_midpoint(
                    out[y][x + span], out[y + span][x + span],
                    base_x + x + span, base_y + y + half, half);
            }
        }
    }
}

static const int (*local_height_page(const Game *g, int base_x,
                                      int base_y))[WILD_PAGE_W] {
    if (!s_local_page.valid || s_local_page.worldmap != g->worldmap_data ||
        s_local_page.worldmap_size != g->worldmap_data_size ||
        s_local_page.base_x != base_x || s_local_page.base_y != base_y) {
        build_local_height_page(g, base_x, base_y, s_local_page.height);
        s_local_page.worldmap = g->worldmap_data;
        s_local_page.worldmap_size = g->worldmap_data_size;
        s_local_page.base_x = base_x;
        s_local_page.base_y = base_y;
        s_local_page.valid = 1;
    }
    return s_local_page.height;
}

static int wilderness_height(const Game *g, int x, int y) {
    x &= 0x3FFF;
    y &= 0x1FFF;
    int base_x = x & ~255;
    int base_y = y & ~127;
    const int (*page)[WILD_PAGE_W] = local_height_page(g, base_x, base_y);
    return page[y - base_y][x - base_x];
}

static void dungeon_marker_for_chunk(const Game *g, int chunk_x, int chunk_y,
                                     int *out_x, int *out_y) {
    long long a = (long long)chunk_x * chunk_x * chunk_y * 173;
    long long d = llabs((long long)chunk_x * (chunk_y + 1)) + 1;
    int local_x = (int)(llabs(a / d) % 250) + 3;
    a = (long long)chunk_x * chunk_y * chunk_x * chunk_y;
    d = llabs((long long)chunk_x + chunk_y + 1);
    if (!d) d = 1;
    int local_y = (int)(llabs(a / d) % 122) + 3;
    int x = chunk_x * 256 + local_x;
    int y = chunk_y * 128 + local_y;
    if (wilderness_height(g, x, y) < 1) x = y = -1;
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

static int wilderness_dungeon_number(int chunk_x, int chunk_y) {
    u32 v = terrain_hash(chunk_x + 91, chunk_y + 37);
    return 1 + (int)(v % 31000u);
}

static void set_wilderness_palette(Video *v) {
    int rgb[256][3] = {{0}};

    /* Literal func_1B169 256-colour branch.  Components are six-bit VGA DAC
     * values; DOSBox's output uses the original value * 4 conversion. */
    rgb[1][0] = 0;  rgb[1][1] = 5;  rgb[1][2] = 53;
    rgb[2][0] = 63; rgb[2][1] = 63; rgb[2][2] = 23;
    rgb[3][0] = 63; rgb[3][1] = 63; rgb[3][2] = 23;
    for (int i = 4; i < 16; i++) rgb[i][0] = 63 - i * 4;
    for (int i = 48; i < 64; i++) rgb[i][0] = 40;
    for (int i = 16; i < 32; i++) rgb[i][0] = i - 6;
    for (int i = 4; i < 16; i++) rgb[i][1] = i * 2 + 32;
    for (int i = 16; i < 48; i++) rgb[i][1] = 64 - i / 2;
    for (int i = 48; i < 64; i++) rgb[i][1] = 90 - i;
    for (int i = 64; i < 96; i++)
        rgb[i][0] = rgb[i][1] = rgb[i][2] = i - 32;
    for (int i = 96; i < 128; i++)
        rgb[i][0] = rgb[i][1] = rgb[i][2] = 159 - i;
    for (int i = 128; i < 256; i++) {
        int fade = (512 - i) / 96;
        for (int c = 0; c < 3; c++)
            rgb[i][c] = rgb[i - 128][c] > 10 ?
                        rgb[i - 128][c] - fade : 0;
    }

    /* func_1B169 finishes by forcing colour 255 to the foreground soil. */
    rgb[255][0] = 9;
    rgb[255][1] = 8;
    rgb[255][2] = 8;
    for (int i = 0; i < 256; i++)
        video_set_palette(v, i, (u8)(rgb[i][0] * 4),
                          (u8)(rgb[i][1] * 4), (u8)(rgb[i][2] * 4));
}

/* Bilinear form of func_1B5DB's four phase-specific interpolation blocks.
 * Horizontal thirds sum to 3 and depth quarters sum to 4.  Dividing their
 * product by two returns the original six-pixels-per-height projection. */
static int projected_height6(const int page[WILD_PAGE_H][WILD_PAGE_W],
                             int x, int y, int third, int phase) {
    int wx0 = 3 - third, wx1 = third;
    int wy0 = 4 - phase, wy1 = phase;
    int value = page[y][x] * wx0 * wy0 +
                page[y][x + 1] * wx1 * wy0 +
                page[y + 1][x] * wx0 * wy1 +
                page[y + 1][x + 1] * wx1 * wy1;
    return value / 2;
}

static void draw_projected_height_page(
    Video *v, const int page[WILD_PAGE_H][WILD_PAGE_W]) {
    video_clear(v, 0);
    video_fill_rect(v, WILD_RENDER_X, WILD_RENDER_Y,
                    WILD_RENDER_W, WILD_RENDER_H, 1);

    for (int x = 0; x < 256; x++) {
        int skyline[3] = {511, 511, 511};
        for (int depth = 511; depth >= 0; depth--) {
            int y = depth >> 2;
            int phase = depth & 3;
            for (int third = 0; third < 3; third++) {
                int height6 = projected_height6(page, x, y, third, phase);
                int surface = depth - height6;
                if (surface >= skyline[third]) continue;

                int sx = WILD_RENDER_X + x * 3 + third;
                int sy = WILD_RENDER_Y + surface;
                if (depth == 511) {
                    /* The nearest ridge is extended to the bottom in colour
                     * 255, producing the dark-grey foreground silhouette. */
                    video_vline(v, sx, sy, 513 - surface, 255);
                } else if (height6 > 0) {
                    int colour = height6 / 4 + 2;
                    if (colour > 254) colour = 254;
                    video_put_pixel(v, sx, sy, (u8)colour);
                    if (skyline[third] - surface > 1)
                        video_vline(v, sx, sy + 1,
                                    skyline[third] - surface - 1, 0);
                }
                skyline[third] = surface;
            }
        }
    }
}

static void draw_terrain_label(Video *v,
                               const int page[WILD_PAGE_H][WILD_PAGE_W],
                               int local_x, int local_y, const char *label,
                               u8 colour, int boat) {
    if (local_x < 0 || local_x > 255 || local_y < 0 || local_y > 127) return;
    int sx = local_x * 3 - 5;
    int sy = WILD_RENDER_Y + local_y * 4 - 4;
    if (!boat) sy -= page[local_y][local_x] * 6;
    video_draw_text(v, sx + 1, sy + 1, label, 0);
    video_draw_text(v, sx, sy, label, colour);
}

static void draw_wilderness(Game *g, Character *p, int test_mode) {
    Video *v = &g->video;
    int base_x = g->wilderness_x & ~255;
    int base_y = g->wilderness_y & ~127;
    const int (*page)[WILD_PAGE_W] = local_height_page(g, base_x, base_y);
    int marker_x, marker_y;
    dungeon_marker_for_chunk(g, base_x >> 8, base_y >> 7,
                             &marker_x, &marker_y);

    set_wilderness_palette(v);
    draw_projected_height_page(v, page);

    if (marker_x >= 0)
        draw_terrain_label(v, page, marker_x - base_x, marker_y - base_y,
                           "D", 1, 0);

    static unsigned marker_phase;
    u8 player_colour = (marker_phase++ & 1u) ? 2 : 1;
    draw_terrain_label(v, page, g->wilderness_x - base_x,
                       g->wilderness_y - base_y,
                       g->wilderness_boat ? "O" : "X", player_colour,
                       g->wilderness_boat);

    (void)p;
    (void)test_mode;
    video_present(v);
}

static int wilderness_click_direction(Game *g) {
    int x, y;
    if (!game_mouse_click_logical(g, &x, &y)) return -1;
    int dx = x - WILD_RENDER_W / 2;
    int dy = y - (WILD_RENDER_Y + WILD_RENDER_H / 2);
    if (abs(dx) > abs(dy)) return dx < 0 ? 2 : 3;
    if (dy < 0) return 0;
    if (dy > 0) return 1;
    return -1;
}

static int wilderness_movement_delta(int key, int scan, SDL_Keymod mods,
                                     int *out_dx, int *out_dy) {
    int dx = 0, dy = 0;
    if (key >= '1' && key <= '9' && key != '5') {
        /* WORLD receives the fast cursor forms as numeric-keypad digits. */
        int column = (key - '1') % 3;
        int row = (key - '1') / 3;
        dx = (column - 1) * 3;
        dy = (1 - row) * 3;
    } else if (key == 0) {
        int distance = (mods & KMOD_SHIFT) ? 3 : 1;
        switch (scan) {
        case 0x47: dx = -distance; dy = -distance; break;
        case 0x48:                 dy = -distance; break;
        case 0x49: dx =  distance; dy = -distance; break;
        case 0x4B: dx = -distance;                 break;
        case 0x4D: dx =  distance;                 break;
        case 0x4F: dx = -distance; dy =  distance; break;
        case 0x50:                 dy =  distance; break;
        case 0x51: dx =  distance; dy =  distance; break;
        default: return 0;
        }
    } else {
        return 0;
    }
    if (out_dx) *out_dx = dx;
    if (out_dy) *out_dy = dy;
    return dx != 0 || dy != 0;
}

static int prompt_key(Game *g, const char *a, const char *b,
                      const char *c, int wanted) {
    Video *v = &g->video;
    video_fill_rect(v, 0, 0, 570, 124, 0);
    video_draw_text_scaled_xy(v, 8, 4, a, 5, 2, 3, 2, 3);
    video_draw_text_scaled_xy(v, 8, 36, b, 5, 2, 3, 2, 3);
    video_draw_text_scaled_xy(v, 8, 68, c, 4, 2, 3, 2, 3);
    video_present(v);
    int key = input_wait_any_key(&g->input);
    if (key == INPUT_MOUSE_CLICK) {
        int x, y;
        if (game_mouse_click_logical(g, &x, &y) && x < 570 && y < 124)
            return wanted;
    }
    if (wanted && (key == wanted ||
                   (wanted >= 'a' && wanted <= 'z' && key == wanted - 32)))
        return wanted;
    return 0;
}

static u32 boat_price(const Character *p) {
    unsigned long long level = p->level ? p->level : 1;
    unsigned long long price = 10000ull + (level / 2ull + 1ull) *
                               (level + 1ull) * (level + 1ull);
    return price > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)price;
}

static int wilderness_terrain_is_land(int height) {
    return height > 0;
}

static int wilderness_terrain_is_water(int height) {
    return height < 0;
}

static int try_move(Game *g, Character *p, int dx, int dy) {
    int nx = wrap_coord(g->wilderness_x + dx, WILD_X_MIN, WILD_X_MAX, WILD_X_SPAN);
    int ny = wrap_coord(g->wilderness_y + dy, WILD_Y_MIN, WILD_Y_MAX, WILD_Y_SPAN);
    int target_height = wilderness_height(g, nx, ny);
    int target_land = wilderness_terrain_is_land(target_height);
    int target_water = wilderness_terrain_is_water(target_height);

    if (g->wilderness_boat && target_land) {
        if (!prompt_key(g, "DO YOU WISH TO LEAVE YOUR BOAT?",
                        "THE CREW MAY STEAL IT WHILE YOU'RE GONE.",
                        "HIT 'L' TO LEAVE THE BOAT; ANY OTHER KEY TO REMAIN.", 'l'))
            return 0;
        g->wilderness_boat = 0;
    } else if (!g->wilderness_boat && target_water) {
        u32 price = boat_price(p);
        char cost[96];
        snprintf(cost, sizeof(cost), "A BOAT WILL COST YOU %u JEWEL STONES.", price);
        if (p->jewels_pocket < price) {
            mw_audio_play(&g->audio, MW_SFX_ERROR);
            prompt_key(g, "YOU WILL HAVE TO BUY A BOAT TO TRAVEL IN WATER.", cost,
                       "COME BACK WHEN YOU HAVE MORE MONEY. HIT ANY KEY...", 0);
            return 0;
        }
        if (!prompt_key(g, "YOU WILL HAVE TO BUY A BOAT TO TRAVEL IN WATER.", cost,
                        "HIT 'B' TO BUY A BOAT; ANY OTHER KEY TO STAY ON LAND.", 'b'))
            return 0;
        p->jewels_pocket -= price;
        g->wilderness_boat = 1;
        mw_audio_play(&g->audio, MW_SFX_COIN);
    }

    g->wilderness_x = nx;
    g->wilderness_y = ny;
    mw_audio_play(&g->audio, MW_SFX_STEP);
    return 1;
}

static void build_world_overview_page(
    const Game *g, int out[WILD_PAGE_H][WILD_PAGE_W]) {
    /* WORLD func_1A841 expands the 64x64 WORLDMAP.BIN code field into the
     * same 257x129 page used by the local landscape renderer. */
    for (int y = 0; y < WILD_PAGE_H; y++) {
        int ty = (y >> 1) & 63;
        int fy = y & 1;
        for (int x = 0; x < WILD_PAGE_W; x++) {
            int tx = (x >> 2) & 63;
            int fx = x & 3;
            int h00 = map_height_code((u8)world_tile(g, tx, ty));
            int h10 = map_height_code((u8)world_tile(g, tx + 1, ty));
            int h01 = map_height_code((u8)world_tile(g, tx, ty + 1));
            int h11 = map_height_code((u8)world_tile(g, tx + 1, ty + 1));
            int top = h00 * (4 - fx) + h10 * fx;
            int bottom = h01 * (4 - fx) + h11 * fx;
            out[y][x] = (top * (2 - fy) + bottom * fy) / 8;
        }
    }
}

static void draw_world_overview(Game *g) {
    Video *v = &g->video;
    static int overview[WILD_PAGE_H][WILD_PAGE_W];
    build_world_overview_page(g, overview);
    set_wilderness_palette(v);
    draw_projected_height_page(v, overview);
    int px = (g->wilderness_x & 0x3FFF) >> 6;
    int py = (g->wilderness_y & 0x1FFF) >> 6;
    if (px > 255) px = 255;
    if (py > 127) py = 127;
    draw_terrain_label(v, overview, px, py, "O", 1, 0);
    draw_terrain_label(v, overview, px, py, "X", 2, 0);
    video_present(v);
    input_wait_any_key(&g->input);
}

static void wilderness_help(Game *g) {
    Video *v = &g->video;
    video_clear(v, 0);
    const char *lines[] = {
        "TRAVELLING IN THE WILDERNESS",
        "CURSOR KEYS MOVE ONE STEP; HOLD A CURSOR KEY TO KEEP MOVING.",
        "SHIFT-CURSOR MOVES THREE STEPS; HOME/END/PAGE KEYS MOVE DIAGONALLY.",
        "THE WORLD WRAPS AT EVERY EDGE, EXACTLY AS THE ORIGINAL WORLD DOES.",
        "BLUE TERRAIN IS WATER. YOU MUST BUY A BOAT BEFORE ENTERING IT.",
        "LEAVING A BOAT ON SHORE MAY FORFEIT IT, JUST AS THE ORIGINAL WARNS.",
        "A YELLOW D MARKS THE DUNGEON GENERATED FOR THE CURRENT REGION.",
        "MOVE CLOSE TO THE D AND PRESS E TO ENTER ITS FLOOR-ZERO TOWN.",
        "MONSTERS ARE ONLY FOUND IN DUNGEONS; NONE SPAWN IN THE WILDERNESS.",
        "W SHOWS THE COMPLETE WORLD MAP. Q RETURNS TO THE DUNGEON.",
        "HIT ANY KEY TO RETURN..."
    };
    for (int i = 0; i < 11; i++)
        video_draw_text_scaled_xy(v, 16, 16 + i * 44, lines[i], i ? 5 : 8,
                                  2, 3, 2, 3);
    video_present(v);
    input_wait_any_key(&g->input);
}

typedef struct {
    Character player;
    int x;
    int y;
    int boat;
    int initialized;
} WildernessTestState;

static void wilderness_test_state_save(WildernessTestState *state,
                                       const Game *g, const Character *p) {
    state->player = *p;
    state->x = g->wilderness_x;
    state->y = g->wilderness_y;
    state->boat = g->wilderness_boat;
    state->initialized = g->wilderness_initialized;
}

static void wilderness_test_state_restore(const WildernessTestState *state,
                                          Game *g, Character *p) {
    *p = state->player;
    g->wilderness_x = state->x;
    g->wilderness_y = state->y;
    g->wilderness_boat = state->boat;
    g->wilderness_initialized = state->initialized;
}

static void wilderness_test_entrance_notice(Game *g) {
    Video *v = &g->video;
    video_fill_rect(v, 0, 0, LOGICAL_W, 112, 0);
    video_draw_text_scaled_xy(v, 8, 5,
        "WILDERNESS TEST: DUNGEON ENTRANCE FOUND.", 4, 2, 3, 2, 3);
    video_draw_text_scaled_xy(v, 8, 39,
        "ENTRY IS DISABLED SO YOUR CURRENT DUNGEON IS PRESERVED.", 5,
        2, 3, 2, 3);
    video_draw_text_scaled_xy(v, 8, 73, "HIT ANY KEY TO CONTINUE...", 8,
                              2, 3, 2, 3);
    video_present(v);
    input_wait_any_key(&g->input);
}

static int wilderness_run_internal(Game *g, Character *p, int test_mode) {
    if (!g || !p || !g->worldmap_data || g->worldmap_data_size < 4096) return 0;
    if (!g->wilderness_initialized) {
        g->wilderness_x = 0x862;
        g->wilderness_y = 0x597;
        g->wilderness_boat = 0;
        g->wilderness_initialized = 1;
    }

    int entered = 0, running = 1;
    while (running && !input_poll_quit(&g->input)) {
        draw_wilderness(g, p, test_mode);
        int key = input_getch(&g->input);
        int dx = 0, dy = 0;
        if (key == INPUT_MOUSE_CLICK) {
            int dir = wilderness_click_direction(g);
            if (dir == 0) dy = -1;
            else if (dir == 1) dy = 1;
            else if (dir == 2) dx = -1;
            else if (dir == 3) dx = 1;
        } else if (key == INPUT_MAX_CHARACTER && !test_mode) {
            game_debug_max_character(g, p);
        } else if (key == INPUT_MODEL_VIEWER) {
            model_viewer_run(g);
        } else if (key == INPUT_TRAINER && !test_mode) {
            trainer_run(g, p);
        } else if (key == INPUT_WILDERNESS_TEST && test_mode) {
            running = 0;
        } else if (key == 0) {
            int scan = input_getch(&g->input);
            if (scan == 0x3B) wilderness_help(g);
            else wilderness_movement_delta(
                0, scan, input_last_key_modifiers(&g->input), &dx, &dy);
        } else if (key == 'w' || key == 'W') {
            draw_world_overview(g);
        } else if (key == 'h' || key == 'H') {
            wilderness_help(g);
        } else if (key == 'q' || key == 'Q' || key == 0x1B) {
            running = 0;
        } else if (key == 'e' || key == 'E') {
            int marker_x, marker_y;
            int cx = g->wilderness_x >> 8, cy = g->wilderness_y >> 7;
            dungeon_marker_for_chunk(g, cx, cy, &marker_x, &marker_y);
            if (marker_x >= 0 && abs(marker_x - g->wilderness_x) < 15 &&
                abs(marker_y - g->wilderness_y) < 14) {
                if (test_mode) {
                    mw_audio_play(&g->audio, MW_SFX_COIN);
                    wilderness_test_entrance_notice(g);
                } else {
                    mw_audio_play(&g->audio, MW_SFX_LADDER);
                    game_begin_new_dungeon(g, p,
                                           wilderness_dungeon_number(cx, cy));
                    entered = 1;
                    running = 0;
                }
            } else {
                mw_audio_play(&g->audio, MW_SFX_ERROR);
            }
        } else {
            wilderness_movement_delta(key, -1, KMOD_NONE, &dx, &dy);
        }
        if (dx || dy) try_move(g, p, dx, dy);
    }
    game_refresh_world_palette(g);
    return entered;
}

int wilderness_run(Game *g, Character *p) {
    return wilderness_run_internal(g, p, 0);
}

int wilderness_test_run(Game *g, Character *p) {
    if (!g || !p || !g->worldmap_data || g->worldmap_data_size < 4096)
        return 0;
    WildernessTestState state;
    wilderness_test_state_save(&state, g, p);
    (void)wilderness_run_internal(g, p, 1);
    wilderness_test_state_restore(&state, g, p);
    game_refresh_world_palette(g);
    return 1;
}

void wilderness_draw_test(Game *g, Character *p) {
    if (!g || !p) return;
    g->wilderness_x = 0x862;
    g->wilderness_y = 0x597;
    g->wilderness_boat = 0;
    g->wilderness_initialized = 1;
    draw_wilderness(g, p, 0);
}

int wilderness_self_test(void) {
    int failures = 0;
    int dx = 0, dy = 0;
    if (map_height_code(0x4F) != 14 || map_height_code(0x50) != 28 ||
        map_height_code(0x20) != -20) failures++;
    /* Literal WORLD seam behavior: 0x3F80 is a duplicate boundary sample. */
    if (wrap_coord(0x3F81, WILD_X_MIN, WILD_X_MAX, WILD_X_SPAN) != 0x01)
        failures++;
    if (wrap_coord(0x3F, WILD_X_MIN, WILD_X_MAX, WILD_X_SPAN) != 0x3F)
        failures++;
    if (wilderness_dungeon_number(8, 11) < 1) failures++;
    if (!wilderness_movement_delta(0, 0x48, KMOD_NONE, &dx, &dy) ||
        dx != 0 || dy != -1 ||
        !wilderness_movement_delta(0, 0x51, KMOD_LSHIFT, &dx, &dy) ||
        dx != 3 || dy != 3 ||
        !wilderness_movement_delta('7', -1, KMOD_NONE, &dx, &dy) ||
        dx != -3 || dy != -3)
        failures++;
    if (!wilderness_terrain_is_water(-1) ||
        wilderness_terrain_is_water(0) ||
        !wilderness_terrain_is_land(1) ||
        wilderness_terrain_is_land(0))
        failures++;
    {
        Character price_test = {0};
        price_test.level = 10;
        if (boat_price(&price_test) != 10726u) failures++;
    }
    {
        Input input = {0};
        input.keys[0] = 0;
        input.keys[1] = 0x48;
        input.key_mods[0] = KMOD_LSHIFT;
        input.key_mods[1] = KMOD_LSHIFT;
        input.tail = 2;
        if (input_getch(&input) != 0 ||
            input_getch(&input) != 0x48 ||
            !(input_last_key_modifiers(&input) & KMOD_SHIFT))
            failures++;
    }

    Game *g = (Game *)calloc(1, sizeof(*g));
    if (!g) {
        failures++;
    } else {
        Character p;
        WildernessTestState state;
        p = (Character){0};
        p.level = 77;
        p.jewels_pocket = 123456;
        g->wilderness_x = 111;
        g->wilderness_y = 222;
        g->wilderness_boat = 1;
        g->wilderness_initialized = 1;
        wilderness_test_state_save(&state, g, &p);
        p.level = 1;
        p.jewels_pocket = 0;
        g->wilderness_x = g->wilderness_y = 999;
        g->wilderness_boat = g->wilderness_initialized = 0;
        wilderness_test_state_restore(&state, g, &p);
        if (p.level != 77 || p.jewels_pocket != 123456 ||
            g->wilderness_x != 111 || g->wilderness_y != 222 ||
            g->wilderness_boat != 1 || g->wilderness_initialized != 1)
            failures++;
        free(g);
    }
    return failures;
}
