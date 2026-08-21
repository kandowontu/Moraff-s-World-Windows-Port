#include "mw_game.h"
#include "mw_combat.h"
#include "mw_trainer.h"
#include "mw_wilderness.h"
#include "mw_model_viewer.h"
#include "mw_battle_simulator.h"
#include "mw_arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <errno.h>

/*
 * Original-routine coverage
 * -------------------------
 * MW_PORT comments identify the WORLD.C/WORLD.ASM routine whose observable
 * behavior is implemented by the following native code. Several small DOS
 * routines are deliberately represented by one data-driven C subsystem; the
 * complete many-to-many mapping and outstanding work live in PORT_STATUS.md.
 * Functions without an MW_PORT tag are native helpers, tests, or extensions.
 */

static void reveal_around_player(Game *g);
static void reveal_around_player_animated(Game *g, Character *player);
static int select_monster_floor(Game *g, int floor);
static int load_pit_group(Game *g, int group);
static int pit_bit_is_set(Game *g, int x, int y);
static int pitfall_target(Game *g, int x, int y);
static double experience_for_level(int level);
static void draw_trapdoor_notice(Game *g, Character *player);
static void draw_contextual_advice(Game *g, Character *player);
static void game_draw_exploration_base(Game *g, Character *player);
static const u8 race_stat_base[RACE_COUNT][6];
static void roll_character_stats(Game *g, int race, u16 stats[6]);
static float starting_spell_points(const Character *p);
static int character_creation_self_test(void);
static void town_pane_begin(Game *g, Character *p);
static int town_pane_text(Game *g, int y, const char *text, u8 color);
static void left_column_begin(Game *g, Character *p);
static int left_column_text(Game *g, int y, const char *text, u8 color);
static int confirm_town_teleport(Game *g, Character *player);
static int confirm_max_character(Game *g, Character *player);
static int game_try_step(Game *g, Character *player, int direction);
static void game_video_mode_menu(Game *g, int startup);
static int game_load_display_font(Game *g, int display_mode);
static void game_normal_map_rect(const Game *g, int *x, int *y,
                                 int *w, int *h);

#define MW_PORT_CONFIG_FILE "MWPORT.CFG"

/* MW_PORT: WORLD build_filepath (0x277E7)/build_open_file (0x2792F) family,
 * expressed as safe local paths instead of DOS drive/path manipulation. */
/* ── File path helper ── */

void game_make_path(Game *g, char *out, int out_sz, const char *filename) {
    snprintf(out, out_sz, "%s/%s", g->game_dir, filename);
}

static int load_display_mode_setting(Game *g) {
    char path[300], line[96];
    /* MW.EXE option A is the native port's default when no preference has
       been saved yet.  A and B share 1024x768x256 dimensions but remain
       separate original driver paths. */
    int mode = MW_DISPLAY_SVGA_1024X768_256_A;
    int legacy_mode = -1;
    /* Headless visual regression runs can select a WORLD driver without
     * rewriting the player's persistent configuration. */
    const char *test_driver = getenv("MW_TEST_DISPLAY_DRIVER");
    if (test_driver && *test_driver) {
        int parsed = atoi(test_driver);
        if (video_display_mode_info(parsed)) return parsed;
    }
    game_make_path(g, path, sizeof(path), MW_PORT_CONFIG_FILE);
    FILE *f = fopen(path, "rt");
    if (!f) return mode;
    while (fgets(line, sizeof(line), f)) {
        int parsed;
        if (sscanf(line, "display_driver=%d", &parsed) == 1 &&
            video_display_mode_info(parsed)) {
            mode = parsed;
            break;
        }
        if (sscanf(line, "video_mode=%d", &parsed) == 1)
            legacy_mode = parsed;
    }
    fclose(f);
    /* Migrate the first implementation's seven resolution-only indices.
       Prefer each resolution's fullest original renderer. */
    if (legacy_mode >= 0) {
        static const int migrate[7] = {
            MW_DISPLAY_HERCULES_720X348,
            MW_DISPLAY_VGA_320X200,
            MW_DISPLAY_VGA_360X480,
            MW_DISPLAY_EGA_640X350,
            MW_DISPLAY_VESA_640X480_256,
            MW_DISPLAY_SVGA_800X600_16,
            MW_DISPLAY_SVGA_1024X768_256_A
        };
        if (legacy_mode < 7) mode = migrate[legacy_mode];
    }
    return mode;
}

static int save_display_mode_setting(Game *g, int mode) {
    char path[300];
    if (!video_display_mode_info(mode)) return -1;
    game_make_path(g, path, sizeof(path), MW_PORT_CONFIG_FILE);
    FILE *f = fopen(path, "wt");
    if (!f) return -1;
    fprintf(f, "# Moraff's World native-port settings\n");
    fprintf(f, "# WORLD.EXE driver number (0-11); repeated resolutions use\n");
    fprintf(f, "# different native palette, wall, font and map paths.\n");
    fprintf(f, "display_driver=%d\n", mode);
    fclose(f);
    return 0;
}

static int game_load_display_font(Game *g, int display_mode) {
    const MwDisplayModeInfo *info = video_display_mode_info(display_mode);
    char path[260];
    /* The shipped data set contains the same three font families selected by
       WORLD's jump table.  The native text scaler supplies the intermediate
       high-resolution sizes from 360X480.FNT. */
    const char *preferred = info && info->font_family == 0 ?
                            "320X200.FNT" : "360X480.FNT";
    game_make_path(g, path, sizeof(path), preferred);
    if (video_load_font(&g->video, path) == 0) return 0;
    game_make_path(g, path, sizeof(path),
                   info && info->font_family == 0 ?
                   "360X480.FNT" : "320X200.FNT");
    return video_load_font(&g->video, path);
}

/* MW_PORT: WORLD's shared random stream used by map, monster, combat,
 * treasure, and character-generation routines. */
/* ── RNG — exact match of original LCG ── */

void game_srand(Game *g, u32 seed) {
    g->rand_state = seed;
}

int game_rand(Game *g) {
    g->rand_state = g->rand_state * 0x15A4E35u + 1;
    return (int)((g->rand_state >> 16) & 0x7FFF);
}

/* Borland's scaled-random idiom in WORLD multiplies the 15-bit rand() value
   by the requested range.  Modulo gives a different deterministic sequence
   and noticeably changes low-range rolls such as temple prices and levels. */
static int original_rand_scaled(Game *g, int range) {
    if (range <= 0) return 0;
    return (int)(((unsigned long)game_rand(g) * (unsigned long)range) / 0x8000ul);
}

static const GameTraversalRules classic_traversal_rules = {
    CLASSIC_DUNGEON_FLOOR, /* deepest dungeon floor */
    65,                    /* Ascend, Double/Major Ascend depth gate */
    123,                   /* Descend depth gate */
    75,                    /* deepest Major Descend landing */
    120,                   /* deepest floor where D may dig */
    124,                   /* WORLD dig-search reversal floor */
    150,                   /* WORLD dig-search initial direction split */
    16,                    /* shallow extra digging sequence */
    130                    /* WORLD dig-search attempt budget */
};

static const GameTraversalRules enhanced_traversal_rules = {
    MAX_DUNGEON_FLOOR,
    260,
    492,
    300,
    480,
    496,
    600,
    64,
    520
};

const GameTraversalRules *game_traversal_rules(const Game *g) {
    /* Only the exact Classic marker selects original traversal geometry.
     * Zero/uninitialized and pre-mode native saves intentionally retain the
     * Enhanced default established by game_init/load. */
    return g && g->dungeon_max_floor == CLASSIC_DUNGEON_FLOOR ?
           &classic_traversal_rules : &enhanced_traversal_rules;
}

int game_dungeon_max_floor(const Game *g) {
    return game_traversal_rules(g)->max_floor;
}

int game_clamp_dungeon_floor(const Game *g, int floor) {
    if (floor < 0) return 0;
    int max_floor = game_traversal_rules(g)->max_floor;
    return floor > max_floor ? max_floor : floor;
}

u32 game_scaled_delay_ms(const Game *g, u32 milliseconds) {
    if (!g || !g->turbo_enabled) return milliseconds;
    int percent = g->turbo_percent;
    if (percent < 25 || percent > 1000) percent = 100;
    if (!milliseconds) return 0;
    uint64_t scaled = ((uint64_t)milliseconds * 100u +
                       (unsigned)percent - 1u) / (unsigned)percent;
    if (!scaled) scaled = 1;
    return scaled > UINT32_MAX ? UINT32_MAX : (u32)scaled;
}

void game_delay(Game *g, u32 milliseconds) {
    SDL_Delay(game_scaled_delay_ms(g, milliseconds));
}

int game_handle_turbo_key(Game *g, int key) {
    if (!g) return 0;
    if (key == INPUT_TURBO_TOGGLE) {
        g->turbo_enabled = !g->turbo_enabled;
        /* Turning Turbo off always discards the prior multiplier; enabling it
           therefore begins at the documented normal 100-percent baseline. */
        g->turbo_percent = 100;
    } else if (g->turbo_enabled && key == '+') {
        if (g->turbo_percent < 1000) g->turbo_percent += 25;
    } else if (g->turbo_enabled && key == '-') {
        if (g->turbo_percent > 25) g->turbo_percent -= 25;
    } else {
        return 0;
    }
    if (!g->turbo_enabled) g->turbo_percent = 100;
    input_set_timing_percent(&g->input,
        g->turbo_enabled ? g->turbo_percent : 100);
    if (g->video.window) {
        char title[96];
        if (g->turbo_enabled)
            snprintf(title, sizeof(title),
                     "Moraff's World - TURBO %d%% (+/-)", g->turbo_percent);
        else
            snprintf(title, sizeof(title), "Moraff's World");
        SDL_SetWindowTitle(g->video.window, title);
    }
    return 1;
}

/* MW_PORT: WORLD func_0A4CF, func_0A51B, func_0A548 and func_0A60D.
 * Character remains the original packed 0x928-byte save record. */
/* ── Character save/load ── */

int game_load_character(Game *g, int slot) {
    if (slot < 0 || slot >= MAX_PLAYERS) return -1;

    char path[260];
    char fname[8];
    snprintf(fname, sizeof(fname), "%d", slot);
    game_make_path(g, path, sizeof(path), fname);

    FILE *f = fopen(path, "rb");
    if (!f) {
        g->char_exists[slot] = 0;
        return -1;
    }

    size_t n = fread(&g->chars[slot], 1, sizeof(Character), f);
    fclose(f);

    if (n == sizeof(Character)) {
        mw_character_native_ensure(&g->chars[slot]);
        /* A zero-HP save is the original game's permanent-death tombstone.
         * Treat it as an available slot when the launcher is rebuilt instead
         * of allowing an unusable dead character back into exploration. */
        if (mw_hp_cur(&g->chars[slot]) == 0) {
            g->char_exists[slot] = 0;
            return -1;
        }
        g->char_exists[slot] = 1;
        return 0;
    }

    g->char_exists[slot] = 0;
    return -1;
}

int game_save_character(Game *g, int slot) {
    if (slot < 0 || slot >= MAX_PLAYERS) return -1;

    mw_character_native_ensure(&g->chars[slot]);

    char path[260];
    char fname[8];
    snprintf(fname, sizeof(fname), "%d", slot);
    game_make_path(g, path, sizeof(path), fname);

    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    fwrite(&g->chars[slot], 1, sizeof(Character), f);
    fclose(f);
    return 0;
}

/* The DOS character file is intentionally never extended.  Beastiary data
 * is stored in a small versioned sidecar named <slot>BEST.DAT instead. */
static void make_bestiary_name(char *out, int out_sz, int slot) {
    snprintf(out, out_sz, "%dBEST.DAT", slot);
}

static u32 read_le32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static void write_le32(u8 *p, u32 value) {
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
    p[2] = (u8)(value >> 16);
    p[3] = (u8)(value >> 24);
}

/* V2/V3 used raw type order.  Keep it as an import map so rearranging the
   displayed catalog never assigns an old kill count to another monster. */
static int bestiary_type_at_raw_catalog_index(int index) {
    for (int type = 0; type < MONSTER_TYPE_COUNT; type++) {
        if (!combat_monster_type_spawnable(type)) continue;
        if (index-- == 0) return type;
    }
    return -1;
}

/* MWBEST04's 117 records used the completed floor-500 display order. */
static int bestiary_type_at_v4_catalog_index(int index) {
    for (int type = 0; type < 112; type++) {
        if (!combat_monster_type_spawnable(type)) continue;
        if (index-- == 0) return type;
    }
    for (int type = 114; type <= 125; type++)
        if (index-- == 0) return type;
    if (index-- == 0) return 112;
    for (int type = 126; type <= 133; type++)
        if (index-- == 0) return type;
    if (index-- == 0) return 113;
    return -1;
}

static int bestiary_type_at_catalog_index(int index) {
    /* Preserve the original roster, then order every native generation and
       boss by the point at which the player first meets it. */
    for (int type = 0; type < 112; type++) {
        if (!combat_monster_type_spawnable(type)) continue;
        if (index-- == 0) return type;
    }
    for (int type = 114; type <= 125; type++)
        if (index-- == 0) return type;
    if (index-- == 0) return 112; /* Violet Abyss King, floor 375 */
    for (int type = 126; type <= 133; type++)
        if (index-- == 0) return type;
    if (index-- == 0) return 113; /* Prismatic World King, floor 500 */
    for (int type = 134; type <= 145; type++)
        if (index-- == 0) return type;
    if (index-- == 0) return 174; /* Cobalt Rift Tyrant, floor 625 */
    for (int type = 146; type <= 153; type++)
        if (index-- == 0) return type;
    if (index-- == 0) return 175; /* Crimson Star Eater, floor 750 */
    for (int type = 154; type <= 165; type++)
        if (index-- == 0) return type;
    if (index-- == 0) return 176; /* Viridian Eternity Dragon, floor 875 */
    for (int type = 166; type <= 173; type++)
        if (index-- == 0) return type;
    if (index-- == 0) return 177; /* Radiant Moraff Ascendant, floor 1000 */
    return -1;
}

static int bestiary_catalog_index_for_type(int wanted) {
    for (int index = 0; index < BESTIARY_CATALOG_COUNT; index++)
        if (bestiary_type_at_catalog_index(index) == wanted) return index;
    return -1;
}

static int bestiary_type_available_in_mode(Game *g, int type) {
    return type >= 0 && type < MONSTER_TYPE_COUNT &&
           monster_types[type].minL <= game_dungeon_max_floor(g);
}

static int bestiary_mode_catalog_count(Game *g) {
    int count = 0;
    for (int i = 0; i < BESTIARY_CATALOG_COUNT; i++) {
        int type = bestiary_type_at_catalog_index(i);
        if (bestiary_type_available_in_mode(g, type)) count++;
    }
    return count;
}

static int bestiary_type_at_mode_catalog_index(Game *g, int wanted_index) {
    for (int i = 0; i < BESTIARY_CATALOG_COUNT; i++) {
        int type = bestiary_type_at_catalog_index(i);
        if (!bestiary_type_available_in_mode(g, type)) continue;
        if (wanted_index-- == 0) return type;
    }
    return -1;
}

static int bestiary_mode_catalog_index_for_type(Game *g, int wanted_type) {
    int mode_index = 0;
    for (int i = 0; i < BESTIARY_CATALOG_COUNT; i++) {
        int type = bestiary_type_at_catalog_index(i);
        if (!bestiary_type_available_in_mode(g, type)) continue;
        if (type == wanted_type) return mode_index;
        mode_index++;
    }
    return -1;
}

int game_load_bestiary(Game *g, int slot) {
    static const u8 magic_v1[8] = {'M','W','B','E','S','T','0','1'};
    static const u8 magic_v2[8] = {'M','W','B','E','S','T','0','2'};
    static const u8 magic_v3[8] = {'M','W','B','E','S','T','0','3'};
    static const u8 magic_v4[8] = {'M','W','B','E','S','T','0','4'};
    static const u8 magic_v5[8] = {'M','W','B','E','S','T','0','5'};
    enum { HEADER_SIZE = 12, RECORD_SIZE = 4 };
    u8 header[HEADER_SIZE], record[RECORD_SIZE];
    char name[24], path[300];

    memset(g->bestiary_kills, 0, sizeof(g->bestiary_kills));
    g->bestiary_loaded = 1;
    g->bestiary_dirty = 0;
    if (slot < 0 || slot >= MAX_PLAYERS) return -1;

    make_bestiary_name(name, sizeof(name), slot);
    game_make_path(g, path, sizeof(path), name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0; /* Existing DOS/port saves begin with an empty record. */
    if (fread(header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        return -1;
    }

    int legacy = memcmp(header, magic_v1, sizeof(magic_v1)) == 0;
    int compact_v2 = memcmp(header, magic_v2, sizeof(magic_v2)) == 0;
    int compact_v3 = memcmp(header, magic_v3, sizeof(magic_v3)) == 0;
    int compact_v4 = memcmp(header, magic_v4, sizeof(magic_v4)) == 0;
    int current = memcmp(header, magic_v5, sizeof(magic_v5)) == 0;
    u32 count = read_le32(header + 8);
    if ((!legacy && !compact_v2 && !compact_v3 && !compact_v4 && !current) ||
        (legacy && count != 112 && count != 114 &&
         count != 134 && count != BESTIARY_MONSTER_COUNT) ||
        (compact_v2 && count != 95 && count != 97) ||
        (compact_v3 && count != 117) ||
        (compact_v4 && count != 117) ||
        (current && count != BESTIARY_CATALOG_COUNT)) {
        fclose(f);
        return -1;
    }

    for (u32 i = 0; i < count; i++) {
        if (fread(record, 1, sizeof(record), f) != sizeof(record)) {
            fclose(f);
            memset(g->bestiary_kills, 0, sizeof(g->bestiary_kills));
            return -1;
        }
        int type = legacy ? (int)i :
                   (current ? bestiary_type_at_catalog_index((int)i) :
                    (compact_v4 ? bestiary_type_at_v4_catalog_index((int)i) :
                                  bestiary_type_at_raw_catalog_index((int)i)));
        if (type >= 0 && combat_monster_type_spawnable(type))
            g->bestiary_kills[type] = read_le32(record);
    }
    fclose(f);

    /* Import each historical ordering by type, then rewrite V5 so existing
       discoveries remain attached while the new entries begin unknown. */
    if (!current) {
        g->bestiary_dirty = 1;
        return game_save_bestiary(g);
    }
    return 0;
}

int game_save_bestiary(Game *g) {
    static const u8 magic[8] = {'M','W','B','E','S','T','0','5'};
    enum { HEADER_SIZE = 12, RECORD_SIZE = 4 };
    u8 data[HEADER_SIZE + BESTIARY_CATALOG_COUNT * RECORD_SIZE];
    char name[24], path[300];

    if (!g->bestiary_loaded || g->active_save_slot < 0 ||
        g->active_save_slot >= MAX_PLAYERS) return 0;
    memcpy(data, magic, sizeof(magic));
    write_le32(data + 8, BESTIARY_CATALOG_COUNT);
    for (int i = 0; i < BESTIARY_CATALOG_COUNT; i++) {
        int type = bestiary_type_at_catalog_index(i);
        write_le32(data + HEADER_SIZE + i * RECORD_SIZE,
                   type >= 0 ? g->bestiary_kills[type] : 0);
    }

    make_bestiary_name(name, sizeof(name), g->active_save_slot);
    game_make_path(g, path, sizeof(path), name);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t put = fwrite(data, 1, sizeof(data), f);
    int close_error = fclose(f);
    if (put != sizeof(data) || close_error != 0) return -1;
    g->bestiary_dirty = 0;
    return 0;
}

/* MW_PORT: WORLD func_073B5/func_07783 image loading and the scanline-table
 * decoder consumed by func_14B85. */
/* ── .PIC loader ── */

static int load_pic_file(const char *path, u8 **images, int *sizes, int max_images) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open: %s\n", path);
        return -1;
    }

    int count = 0;
    while (count < max_images) {
        int hi = fgetc(f);
        int lo = fgetc(f);
        if (hi == EOF || lo == EOF) break;

        int size_field = (hi << 8) | lo;
        int pixel_count = size_field + 0x190;

        u8 *data = malloc(pixel_count);
        if (!data) break;

        int nread = (int)fread(data, 1, pixel_count, f);
        if (nread < pixel_count) {
            memset(data + nread, 0, pixel_count - nread);
        }

        images[count] = data;
        sizes[count] = pixel_count;
        count++;
    }

    fclose(f);
    printf("Loaded %s: %d images\n", path, count);
    return count;
}

/* WALL.PIC and WORLD.PIC share the original 200-row scanline/RLE format.
 * Keeping a decoded copy of the two wall surfaces makes perspective texture
 * mapping deterministic and avoids walking the RLE stream for every pixel. */
static u8 *decode_pic_surface(const u8 *pic_data, int pic_size) {
    const int tex_w = 256, tex_h = 200, table_size = 0x190;
    if (!pic_data || pic_size < table_size + 2) return NULL;

    u8 *out = calloc((size_t)tex_w * tex_h, 1);
    if (!out) return NULL;

    const u8 *pixdata = pic_data + table_size;
    int pixdata_len = pic_size - table_size;
    u16 scanline[200];
    for (int row = 0; row < tex_h; row++)
        scanline[row] = (u16)(pic_data[row * 2] | (pic_data[row * 2 + 1] << 8));

    for (int row = 0; row < tex_h; row++) {
        int start = scanline[row];
        int end = pixdata_len;
        for (int next = row + 1; next < tex_h; next++) {
            if (scanline[next] != start) { end = scanline[next]; break; }
        }
        if (start < 0 || start >= pixdata_len || start == end) continue;

        int ptr = start;
        int xpos = pixdata[ptr++];
        while (ptr < end && ptr < pixdata_len) {
            int cmd = pixdata[ptr++];
            int run, color;
            if (cmd >= 0x20) {
                run = cmd >> 5;
                color = cmd & 0x1F;
            } else {
                color = cmd;
                if (ptr >= pixdata_len) break;
                int n = pixdata[ptr++];
                run = n ? n : 255;
            }
            for (int x = 0; x < run && xpos + x < tex_w; x++)
                if (xpos + x >= 0) out[row * tex_w + xpos + x] = (u8)color;
            xpos += run;
            if (xpos >= tex_w) break;
        }
    }
    return out;
}

int game_load_pics(Game *g) {
    char path[260];

    game_make_path(g, path, sizeof(path), "WORLD.PIC");
    g->world_pic_count = load_pic_file(path, g->world_pic_data, g->world_pic_sizes, 256);
    if (g->world_pic_count < 0) {
        game_make_path(g, path, sizeof(path), "world.pic");
        g->world_pic_count = load_pic_file(path, g->world_pic_data, g->world_pic_sizes, 256);
    }

    game_make_path(g, path, sizeof(path), "WALL.PIC");
    g->wall_pic_count = load_pic_file(path, g->wall_pic_data, g->wall_pic_sizes, 64);
    if (g->wall_pic_count < 0) {
        game_make_path(g, path, sizeof(path), "wall.pic");
        g->wall_pic_count = load_pic_file(path, g->wall_pic_data, g->wall_pic_sizes, 64);
    }

    for (int i = 0; i < g->wall_pic_count && i < 2; i++)
        g->wall_texture[i] = decode_pic_surface(g->wall_pic_data[i], g->wall_pic_sizes[i]);

    return (g->world_pic_count > 0) ? 0 : -1;
}

/* MW_PORT: WORLD load_dungeon_bin (0x0A3E7), load_monster_map (0x09C8A),
 * and their floor-selection/state helpers 0x09DA6..0x0A39C. */
/* ── Binary data loaders ── */

static u8 *load_binary_file(const char *path, int *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    u8 *buf = malloc(sz);
    if (!buf) { fclose(f); return NULL; }

    *out_size = (int)fread(buf, 1, sz, f);
    fclose(f);
    return buf;
}

int game_load_dungeon(Game *g) {
    char path[260];
    int sz;

    game_make_path(g, path, sizeof(path), "DUNG.BIN");
    g->dungeon_data = load_binary_file(path, &sz);
    if (!g->dungeon_data) {
        game_make_path(g, path, sizeof(path), "dung.bin");
        g->dungeon_data = load_binary_file(path, &sz);
    }
    if (g->dungeon_data) {
        g->dungeon_data_size = sz;
        printf("Loaded DUNG.BIN: %d bytes\n", sz);
    }

    game_make_path(g, path, sizeof(path), "WORLDMAP.BIN");
    int wsz;
    g->worldmap_data = load_binary_file(path, &wsz);
    if (!g->worldmap_data) {
        game_make_path(g, path, sizeof(path), "worldmap.bin");
        g->worldmap_data = load_binary_file(path, &wsz);
    }
    if (g->worldmap_data) {
        g->worldmap_data_size = wsz;
        printf("Loaded WORLDMAP.BIN: %d bytes\n", wsz);
    }

    return (g->dungeon_data) ? 0 : -1;
}

int game_load_monsters(Game *g) {
    char path[260];
    int sz;

    game_make_path(g, path, sizeof(path), "H.BIN");
    g->monster_data = load_binary_file(path, &sz);
    if (!g->monster_data) {
        game_make_path(g, path, sizeof(path), "h.bin");
        g->monster_data = load_binary_file(path, &sz);
    }
    if (g->monster_data) printf("Loaded H.BIN: %d bytes\n", sz);

    return (g->monster_data) ? 0 : -1;
}

/* MW_PORT: WORLD func_1EE04 (template selection), func_1EEC9 (ladders),
 * calc_damage/far_1EFA4 (two-bit edges), and func_1F2D4 (rock cells). */
/* ── Procedural dungeon map access ──
 *
 * WORLD.ASM far_1EFA4 does not index DUNG.BIN as an 80x80 room array.  The
 * file contains eighteen 512-byte packed wall templates after a 0x200-byte
 * header.  far_1EE04 selects one template for every 16x16 region using the
 * floor and saved dungeon number; far_1EFA4 then extracts one two-bit edge
 * value.  The latter is character field +0x7B2 (runtime g_C8A4), not the
 * selected player/save-slot index.
 *
 * axis 0 is a vertical edge at x (west edge of cell x); axis 1 is a
 * horizontal edge at y (north edge of cell y).  Values used by the original:
 *   0,2 = stone wall, 1 = barred door, 3 = open passage.
 */

static s16 dos16(s32 value) {
    return (s16)(u16)value;
}

static s16 dos_mul(s16 a, s16 b) {
    return dos16((s32)a * (s32)b);
}

static s16 dungeon_hash(int block_x, int block_y, int floor, int dungeon_number,
                        int modulus) {
    if (block_x < 0 || block_y < 0) return 0;

    s16 x = dos16(block_x + 9);
    s16 y = dos16(block_y + 7);
    s16 f = dos16(floor + 13);
    s16 p = dos16(dungeon_number + 15);
    if (y == 0 || f == 0 || p == 0) return 0;

    s16 a = (s16)(dos_mul(x, 25) / y);
    s16 b = dos_mul(p, 7);
    s16 result = dos16(a + b);
    result = dos_mul(result, f);

    a = dos_mul(f, 27);
    result = dos16(result + (a % p));
    a = dos_mul(y, 31);
    result = dos16(result + (a % f));

    a = dos_mul(dos_mul(x, y), f);
    result = dos16(result + a / 17);
    result = dos16(result + dos_mul(x, 13));
    result = dos16(result + dos_mul(y, 11));
    result = dos16(result + dos_mul(f, 17));

    /* cwd/xor/sub in far_1EE04: 16-bit absolute value, including -32768. */
    if (result < 0) result = dos16(-(s32)result);
    if (modulus < 1) return 0;
    s16 rem = (s16)(result % modulus);
    if (rem < 0) rem = 0;
    if (rem >= modulus) rem = (s16)(modulus - 1);
    return rem;
}

static int dungeon_edge_at(Game *g, int x, int y, int axis, int floor) {
    if (!g || !g->dungeon_data) return 0;
    if ((axis == 0 && (x < 1 || x >= MAP_W)) ||
        (axis == 1 && (y < 1 || y >= MAP_H)))
        return 0;

    /* One byte holds both axes for an even/odd X pair:
     * even vertical=0, even horizontal=2, odd vertical=4, odd horizontal=6. */
    int shift = (x & 1) ? 4 : 0;
    if (axis != 0) shift += 2;

    int which = dungeon_hash(x >> 4, y >> 4, floor,
                             g->dungeon_number, 18);
    int off = 0x200 + which * 0x200
            + (((x >> 4) & 1) << 8)
            + (((y >> 4) & 1) << 7)
            + (((x >> 1) & 7) << 4)
            + (y & 15);
    if (off < 0 || off >= g->dungeon_data_size) return 0;
    return (g->dungeon_data[off] >> shift) & 3;
}

int map_get_edge(Game *g, int x, int y, int axis) {
    return dungeon_edge_at(g, x, y, axis, g ? g->cur_floor : 0);
}

static int rock_cell_at(Game *g, int x, int y, int floor) {
    return dungeon_edge_at(g, x,     y,     1, floor) == 0 &&
           dungeon_edge_at(g, x + 1, y,     0, floor) == 0 &&
           dungeon_edge_at(g, x,     y + 1, 1, floor) == 0 &&
           dungeon_edge_at(g, x,     y,     0, floor) == 0;
}

/* Exact far_1EEC9 ladder search.  A 1-in-31 coordinate hash selects a shaft;
 * the routine bridges as many as three intervening all-rock floors. */
static int ladder_delta(Game *g, int x, int y) {
    int floor = g->cur_floor;
    const GameTraversalRules *rules = game_traversal_rules(g);

    for (int shaft = floor - 1;
         shaft > floor - 4 && shaft >= 0; shaft--) {
        if (rock_cell_at(g, x, y, shaft)) continue;
        if (dungeon_hash(x, y, shaft, g->dungeon_number, 31) != 1) continue;

        int landing = shaft + 1;
        while (landing <= floor && rock_cell_at(g, x, y, landing))
            landing++;
        if (landing == floor) return shaft - floor;
    }

    if (dungeon_hash(x, y, floor, g->dungeon_number, 31) == 1) {
        for (int landing = floor + 1;
             landing < floor + 3 &&
             landing <= rules->max_floor; landing++) {
            if (!rock_cell_at(g, x, y, landing)) return landing - floor;
        }
    }
    return 0;
}

u8 map_get_cell(Game *g, int x, int y) {
    if (!g || !g->dungeon_data) return 0xFF;
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return 0xFF;

    int north = map_get_edge(g, x,     y,     1);
    int east  = map_get_edge(g, x + 1, y,     0);
    int south = map_get_edge(g, x,     y + 1, 1);
    int west  = map_get_edge(g, x,     y,     0);
    return (u8)(north | (east << 2) | (south << 4) | (west << 6));
}

int map_is_wall(Game *g, int x, int y) {
    (void)g;
    return x < 0 || x >= MAP_W || y < 0 || y >= MAP_H;
}

int map_has_wall_n(u8 cell) { return (cell & 3) != 3; }
int map_has_wall_e(u8 cell) { return ((cell >> 2) & 3) != 3; }
int map_has_wall_s(u8 cell) { return ((cell >> 4) & 3) != 3; }
int map_has_wall_w(u8 cell) { return ((cell >> 6) & 3) != 3; }

static int edge_between_cells(Game *g, int x1, int y1, int x2, int y2) {
    if (x1 < 0 || x1 >= MAP_W || y1 < 0 || y1 >= MAP_H ||
        x2 < 0 || x2 >= MAP_W || y2 < 0 || y2 >= MAP_H)
        return 0;
    if (y2 < y1) return map_get_edge(g, x1,     y1,     1);
    if (y2 > y1) return map_get_edge(g, x1,     y1 + 1, 1);
    if (x2 < x1) return map_get_edge(g, x1,     y1,     0);
    if (x2 > x1) return map_get_edge(g, x1 + 1, y1,     0);
    return 3;
}

/* Check if an actor can move to (nx, ny) from (ox, oy).
   A normal door (edge 1) and a secret door (edge 2) open as part of the move;
   neither has a separate "open" command.  The latter deliberately keeps the
   wall texture, so it is found by walking into it.  Both remain opaque to
   viewport rays and fog-of-war discovery, which require edge 3.  Do not use
   0xFF as an invalid-cell sentinel here: four open (3) edges are also exactly
   0xFF. */
int game_can_move(Game *g, int ox, int oy, int nx, int ny) {
    int wall_val = edge_between_cells(g, ox, oy, nx, ny);
    return wall_val == 1 || wall_val == 2 || wall_val == 3;
}

/* MW_PORT: WORLD func_09185/func_091B1/func_091CD monster identity and spawn
 * rules; load_monster_map plus func_09DA6..func_0A39C persistence; func_0C970
 * adjacency; func_0E8C8 movement timing; func_0EA5A pit generation/history. */
/* ── Persistent MON.MAP and .DUN world state ── */

static int monster_record_hp(const MonsterRecord *m) {
    if (!m) return 0;
    return m->hp > INT32_MAX ? INT32_MAX : (int)m->hp;
}

static void monster_record_set_hp(MonsterRecord *m, int hp) {
    if (hp < 0) hp = 0;
    m->hp = (u32)hp;
}

static int monster_record_alive(Game *g, const MonsterRecord *m) {
    (void)g;
    return m && m->x < MAP_W && m->y < MAP_H &&
           m->type < MONSTER_TYPE_COUNT && monster_record_hp(m) > 0;
}

static void clear_monster_record(MonsterRecord *m) {
    /* This is the dead sentinel used by the original MON.MAP files. */
    m->x = 100;
    m->y = 100;
    m->hp = 0;
    m->type = 0;
    m->level = 0;
}

static void make_monster_map_name(char *out, int out_sz, int slot) {
    snprintf(out, out_sz, "%dMON.MAP", slot);
}

enum {
    QUEST_CHAIN_COUNT = 14,
    LATE_GEAR_TIER_COUNT = 8
};

static const u16 late_gear_floor[LATE_GEAR_TIER_COUNT] = {
    375, 500, 625, 750, 825, 875, 950, 1000
};

static const u16 deep_spell_unlock_floor[MW_DEEP_SPELL_COUNT] = {
    100, 200, 300, 400, 500, 600, 700, 775, 850, 900,
    925, 950, 975, 990, 1000
};

static const u16 quest_floor_by_step[QUEST_CHAIN_COUNT] = {
    4, 8, 12, 16, 125, 150, 175, 200, 375, 500,
    625, 750, 875, 1000
};

static const u8 quest_type_by_step[QUEST_CHAIN_COUNT] = {
    104, 105, 106, 107, 108, 109, 110, 111, 112, 113,
    174, 175, 176, 177
};

static int quest_step_for_type(int type) {
    for (int i = 0; i < QUEST_CHAIN_COUNT; i++)
        if (quest_type_by_step[i] == type) return i;
    return -1;
}

static int quest_boss_type(Game *g, int floor) {
    u16 flags = 0;
    if (g->cur_player >= 0 && g->cur_player < MAX_PLAYERS)
        flags = mw_quest_flags(&g->chars[g->cur_player]);
    for (int i = 0; i < QUEST_CHAIN_COUNT; i++)
        if (floor == quest_floor_by_step[i] && !(flags & (1u << i)))
            return quest_type_by_step[i];
    return -1;
}

static void roll_monster_identity(Game *g, MonsterRecord *m,
                                  int floor, int forced_type) {
    int type = forced_type >= 0 ? forced_type :
               combat_pick_monster_type(g, floor);
    int level = floor + (game_rand(g) % 5) - 2;
    if (level < 1) level = 1;
    if (level > UINT16_MAX) level = UINT16_MAX;
    int cap = combat_calc_monster_hp(&monster_types[type], level);
    u32 roll = ((u32)game_rand(g) << 15) | (u32)game_rand(g);
    int hp = cap > 1 ? 1 + (int)(roll % (u32)cap) : 1;
    monster_record_set_hp(m, hp);
    m->type = (u8)type;
    m->level = (u16)level;
}

/* Repair MON.MAP layers made by older port builds, which could randomly put
   quest dragons on every floor and retain monsters outside their min/max
   range.  Positions and dead records remain untouched. */
static void sanitize_monster_floor(Game *g, int layer, int floor) {
    MonsterRecord *map = g->monster_map[layer];
    int expected_boss = quest_boss_type(g, floor);
    int boss_index = -1;

    if (expected_boss >= 0) {
        for (int i = 0; i < MONSTERS_PER_FLOOR; i++)
            if (monster_record_alive(g, &map[i]) &&
                map[i].type == expected_boss) {
                boss_index = i;
                break;
            }
        if (boss_index < 0) {
            for (int i = 0; i < MONSTERS_PER_FLOOR; i++)
                if (monster_record_alive(g, &map[i])) {
                    boss_index = i;
                    roll_monster_identity(g, &map[i], floor, expected_boss);
                    g->monster_map_dirty = 1;
                    break;
                }
        }
    }

    for (int i = 0; i < MONSTERS_PER_FLOOR; i++) {
        MonsterRecord *m = &map[i];
        if (!monster_record_alive(g, m) || i == boss_index) continue;
        if (quest_step_for_type(m->type) >= 0 ||
            !combat_monster_type_valid(m->type, floor)) {
            roll_monster_identity(g, m, floor, -1);
            g->monster_map_dirty = 1;
        }
    }
}

static int save_monster_map(Game *g) {
    if (!g->monster_map_loaded || g->active_save_slot < 0) return 0;
    char name[24], path[300];
    make_monster_map_name(name, sizeof(name), g->active_save_slot);
    game_make_path(g, path, sizeof(path), name);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    static const u8 magic[8] = {'M','W','M','O','N','0','0','3'};
    fwrite(magic, 1, sizeof(magic), f);
    fwrite(g->monster_floor, sizeof(g->monster_floor[0]),
           MONSTER_MAP_LAYERS, f);
    fwrite(g->monster_map, sizeof(MonsterRecord),
           MONSTER_MAP_LAYERS * MONSTERS_PER_FLOOR, f);
    fclose(f);
    g->monster_map_dirty = 0;
    return 0;
}

static void generate_monster_floor(Game *g, int layer, int floor) {
    MonsterRecord *map = g->monster_map[layer];
    for (int i = 0; i < MONSTERS_PER_FLOOR; i++) clear_monster_record(&map[i]);

    /* The town/surface is a safe floor in the original game. */
    if (floor <= 0) {
        g->monster_floor[layer] = 0;
        g->monster_map_dirty = 1;
        return;
    }

    int made = 0;
    for (int tries = 0; made < MONSTERS_PER_FLOOR && tries < 100000; tries++) {
        int x = game_rand(g) % MAP_W;
        int y = game_rand(g) % MAP_H;
        if (rock_cell_at(g, x, y, floor)) continue;

        int occupied = 0;
        for (int j = 0; j < made; j++)
            if (map[j].x == x && map[j].y == y) { occupied = 1; break; }
        if (occupied || (x == g->cur_x && y == g->cur_y)) continue;

        map[made].x = (u8)x;
        map[made].y = (u8)y;
        roll_monster_identity(g, &map[made], floor,
                              made == 0 ? quest_boss_type(g, floor) : -1);
        made++;
    }
    g->monster_floor[layer] = (u16)floor;
    g->monster_map_dirty = 1;
}

static int select_monster_floor(Game *g, int floor) {
    if (!g->monster_map_loaded) return -1;
    if (floor <= 0) {
        g->monster_layer = -1;
        g->monster_adjacent = 0;
        return -1;
    }
    for (int i = 0; i < MONSTER_MAP_LAYERS; i++) {
        if (g->monster_floor[i] == (u16)floor) {
            g->monster_layer = i;
            sanitize_monster_floor(g, i, floor);
            return i;
        }
    }

    /* MON.MAP is a three-floor cache.  Rotate the oldest layer out exactly
     * as the DOS game does when a newly visited floor is entered. */
    memmove(&g->monster_floor[0], &g->monster_floor[1],
            sizeof(g->monster_floor[0]) * (MONSTER_MAP_LAYERS - 1));
    memmove(&g->monster_map[0], &g->monster_map[1],
            sizeof(g->monster_map[0]) * (MONSTER_MAP_LAYERS - 1));
    g->monster_layer = MONSTER_MAP_LAYERS - 1;
    generate_monster_floor(g, g->monster_layer, floor);
    return g->monster_layer;
}

static void make_pit_name(char *out, int out_sz, int slot, int group) {
    snprintf(out, out_sz, "%d%d.DUN", slot, group);
}

static int save_pit_group(Game *g) {
    if (!g->pit_state_loaded || g->active_save_slot < 0 || g->pit_group < 0)
        return 0;
    char name[24], path[300];
    make_pit_name(name, sizeof(name), g->active_save_slot, g->pit_group);
    game_make_path(g, path, sizeof(path), name);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    u32 mask = g->pit_floor_mask;
    for (int z = 0; z < PIT_GROUP_FLOORS; z++) {
        int any = 0;
        for (int y = 0; y < MAP_H && !any; y++)
            for (int b = 0; b < PIT_ROW_BYTES; b++)
                if (g->pit_used[z][y][b]) { any = 1; break; }
        if (any) mask |= (1u << z);
    }
    u8 hdr[4] = {(u8)mask, (u8)(mask >> 8), (u8)(mask >> 16), (u8)(mask >> 24)};
    fwrite(hdr, 1, 4, f);
    for (int z = 0; z < PIT_GROUP_FLOORS; z++) {
        if (!(mask & (1u << z))) continue;
        u8 rows[16];
        memcpy(rows, g->pit_row_mask[z], sizeof(rows));
        for (int y = 0; y < MAP_H; y++) {
            int any = 0;
            for (int b = 0; b < PIT_ROW_BYTES; b++) any |= g->pit_used[z][y][b];
            if (any) rows[y >> 3] |= (u8)(1u << (y & 7));
        }
        memcpy(g->pit_row_mask[z], rows, sizeof(rows));
        fwrite(rows, 1, sizeof(rows), f);
        for (int y = 0; y < MAP_H; y++)
            if (rows[y >> 3] & (1u << (y & 7)))
                fwrite(g->pit_used[z][y], 1, PIT_ROW_BYTES, f);
    }
    fclose(f);
    g->pit_floor_mask = mask;
    g->pit_state_dirty = 0;
    return 0;
}

static int load_pit_group(Game *g, int group) {
    if (g->pit_state_loaded && g->pit_group == group) return 0;
    if (g->pit_state_dirty) save_pit_group(g);
    memset(g->pit_used, 0, sizeof(g->pit_used));
    memset(g->pit_row_mask, 0, sizeof(g->pit_row_mask));
    g->pit_floor_mask = 0;
    g->pit_group = group;
    g->pit_state_loaded = 1;
    g->pit_state_dirty = 0;

    char name[24], path[300];
    make_pit_name(name, sizeof(name), g->active_save_slot, group);
    game_make_path(g, path, sizeof(path), name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    u8 hdr[4];
    if (fread(hdr, 1, 4, f) != 4) { fclose(f); return -1; }
    g->pit_floor_mask = (u32)hdr[0] | ((u32)hdr[1] << 8) |
                        ((u32)hdr[2] << 16) | ((u32)hdr[3] << 24);
    for (int z = 0; z < PIT_GROUP_FLOORS; z++) {
        if (!(g->pit_floor_mask & (1u << z))) continue;
        u8 rows[16];
        if (fread(rows, 1, sizeof(rows), f) != sizeof(rows)) break;
        memcpy(g->pit_row_mask[z], rows, sizeof(rows));
        for (int y = 0; y < MAP_H; y++) {
            if (!(rows[y >> 3] & (1u << (y & 7)))) continue;
            if (fread(g->pit_used[z][y], 1, PIT_ROW_BYTES, f) != PIT_ROW_BYTES)
                memset(g->pit_used[z][y], 0, PIT_ROW_BYTES);
        }
    }
    fclose(f);
    return 0;
}

int game_load_world_state(Game *g, int slot) {
    g->active_save_slot = slot;
    game_load_bestiary(g, slot);
    g->monster_layer = 0;
    g->monster_map_loaded = 1;
    g->monster_map_dirty = 0;
    memset(g->monster_floor, 0xFF, sizeof(g->monster_floor));
    for (int z = 0; z < MONSTER_MAP_LAYERS; z++)
        for (int i = 0; i < MONSTERS_PER_FLOOR; i++)
            clear_monster_record(&g->monster_map[z][i]);

    char name[24], path[300];
    make_monster_map_name(name, sizeof(name), slot);
    game_make_path(g, path, sizeof(path), name);
    FILE *f = fopen(path, "rb");
    if (f) {
        static const u8 magic_v3[8] = {'M','W','M','O','N','0','0','3'};
        static const u8 magic_v2[8] = {'M','W','M','O','N','0','0','2'};
        u8 header[8];
        size_t h = fread(header, 1, sizeof(header), f);
        size_t a = 0, b = 0;
        if (h == sizeof(header) &&
            memcmp(header, magic_v3, sizeof(magic_v3)) == 0) {
            a = fread(g->monster_floor, sizeof(g->monster_floor[0]),
                      MONSTER_MAP_LAYERS, f);
            b = fread(g->monster_map, sizeof(MonsterRecord),
                      MONSTER_MAP_LAYERS * MONSTERS_PER_FLOOR, f);
        } else if (h == sizeof(header) &&
                   memcmp(header, magic_v2, sizeof(magic_v2)) == 0) {
            /* Native-v2 used a packed seven-byte record with 16-bit HP. */
            u8 old_map[MONSTER_MAP_LAYERS][MONSTERS_PER_FLOOR][7];
            a = fread(g->monster_floor, sizeof(g->monster_floor[0]),
                      MONSTER_MAP_LAYERS, f);
            b = fread(old_map, 7,
                      MONSTER_MAP_LAYERS * MONSTERS_PER_FLOOR, f);
            if (a == MONSTER_MAP_LAYERS &&
                b == MONSTER_MAP_LAYERS * MONSTERS_PER_FLOOR) {
                for (int z = 0; z < MONSTER_MAP_LAYERS; z++)
                    for (int i = 0; i < MONSTERS_PER_FLOOR; i++) {
                        const u8 *src = old_map[z][i];
                        MonsterRecord *dst = &g->monster_map[z][i];
                        dst->x = src[0];
                        dst->y = src[1];
                        dst->hp = (u32)src[2] | ((u32)src[3] << 8);
                        dst->type = src[4];
                        dst->level = (u16)src[5] | ((u16)src[6] << 8);
                    }
                g->monster_map_dirty = 1;
            }
        } else {
            /* Import the original three byte floor IDs plus 6-byte records.
             * The next save rewrites this cache as MWMON003. */
            typedef struct LegacyMonsterRecord {
                u8 x, y, hp_lo, hp_hi, type, level;
            } LegacyMonsterRecord;
            u8 old_floor[MONSTER_MAP_LAYERS];
            LegacyMonsterRecord old_map[MONSTER_MAP_LAYERS][MONSTERS_PER_FLOOR];
            rewind(f);
            a = fread(old_floor, 1, MONSTER_MAP_LAYERS, f);
            b = fread(old_map, sizeof(LegacyMonsterRecord),
                      MONSTER_MAP_LAYERS * MONSTERS_PER_FLOOR, f);
            if (a == MONSTER_MAP_LAYERS &&
                b == MONSTER_MAP_LAYERS * MONSTERS_PER_FLOOR) {
                for (int z = 0; z < MONSTER_MAP_LAYERS; z++) {
                    g->monster_floor[z] = old_floor[z] == 0xFF ?
                                          UINT16_MAX : old_floor[z];
                    for (int i = 0; i < MONSTERS_PER_FLOOR; i++) {
                        LegacyMonsterRecord *src = &old_map[z][i];
                        MonsterRecord *dst = &g->monster_map[z][i];
                        dst->x = src->x;
                        dst->y = src->y;
                        dst->hp = (u32)src->hp_lo | ((u32)src->hp_hi << 8);
                        dst->type = src->type;
                        dst->level = src->level;
                    }
                }
                g->monster_map_dirty = 1;
            }
        }
        fclose(f);
        if (a != MONSTER_MAP_LAYERS ||
            b != MONSTER_MAP_LAYERS * MONSTERS_PER_FLOOR) {
            memset(g->monster_floor, 0xFF, sizeof(g->monster_floor));
        }
    }
    /* Purge dormant raw definitions from every cached floor immediately,
     * not only when that floor is selected later.  This migrates existing
     * <slot>MON.MAP files away from Hobbit and the other unloaded-art rows. */
    for (int layer = 0; layer < MONSTER_MAP_LAYERS; layer++) {
        int cached_floor = g->monster_floor[layer];
        if (cached_floor > 0 &&
            cached_floor <= game_dungeon_max_floor(g))
            sanitize_monster_floor(g, layer, cached_floor);
    }
    select_monster_floor(g, g->cur_floor);
    load_pit_group(g, g->cur_floor / PIT_GROUP_FLOORS);
    return 0;
}

int game_save_world_state(Game *g) {
    int a = save_monster_map(g);
    int b = save_pit_group(g);
    int c = game_save_bestiary(g);
    return (a < 0 || b < 0 || c < 0) ? -1 : 0;
}

int game_find_monster(Game *g, int x, int y) {
    if (!g->monster_map_loaded || g->monster_layer < 0) return -1;
    MonsterRecord *map = g->monster_map[g->monster_layer];
    for (int i = 0; i < MONSTERS_PER_FLOOR; i++)
        if (monster_record_alive(g, &map[i]) && map[i].x == x && map[i].y == y)
            return i;
    return -1;
}

static int game_find_engaged_monster_in_direction(Game *g, int direction) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dy[4] = {-1, 1, 0, 0};
    if (direction < 0 || direction > 3) return -1;
    int x = g->cur_x + dx[direction];
    int y = g->cur_y + dy[direction];
    /* WORLD func_0C970 requires calc_damage's edge result to be exactly
     * three before it considers the actor adjacent.  A value of one is
     * a usable, auto-opening door, but it remains an opaque encounter
     * boundary: the monster beyond it neither appears nor answers F. */
    if (edge_between_cells(g, g->cur_x, g->cur_y, x, y) != 3)
        return -1;
    return game_find_monster(g, x, y);
}

int game_find_adjacent_monster(Game *g) {
    for (int direction = 0; direction < 4; direction++) {
        int index = game_find_engaged_monster_in_direction(g, direction);
        if (index >= 0) return index;
    }
    return -1;
}

int game_monster_hp(Game *g, int index) {
    if (!g->monster_map_loaded || g->monster_layer < 0 ||
        index < 0 || index >= MONSTERS_PER_FLOOR) return 0;
    return monster_record_hp(&g->monster_map[g->monster_layer][index]);
}

void game_set_monster_hp(Game *g, int index, int hp) {
    if (!g->monster_map_loaded || g->monster_layer < 0 ||
        index < 0 || index >= MONSTERS_PER_FLOOR) return;
    monster_record_set_hp(&g->monster_map[g->monster_layer][index], hp);
    g->monster_map_dirty = 1;
}

void game_kill_monster(Game *g, int index) {
    if (!g->monster_map_loaded || g->monster_layer < 0 ||
        index < 0 || index >= MONSTERS_PER_FLOOR) return;
    clear_monster_record(&g->monster_map[g->monster_layer][index]);
    g->monster_map_dirty = 1;
    save_monster_map(g);
    mw_audio_play(&g->audio, MW_SFX_VICTORY);
}

void game_advance_monsters(Game *g, Character *player) {
    g->advice_counter++;
    character_tick_effects(g, player);
    if (!g->monster_map_loaded || g->monster_layer < 0 || g->cur_floor <= 0) {
        g->monster_adjacent = 0;
        return;
    }
    if ((player->eff_fast_move && game_rand(g) % 4 == 0) ||
        (player->eff_invisible == 1 && game_rand(g) % 4 == 0)) {
        g->monster_adjacent = game_find_adjacent_monster(g) >= 0;
        return;
    }
    MonsterRecord *map = g->monster_map[g->monster_layer];
    static const int dx[4] = {0, 0, -1, 1};
    static const int dy[4] = {-1, 1, 0, 0};

    /* WORLD func_0E8C8 turns loaded weight into monster action time.  The
       native movement model uses one pass for each such opportunity, so a
       burdened player is pursued more quickly while Feather removes the
       character's body-weight component from that calculation. */
    int opportunities = game_weight_monster_turns(player);
    for (int opportunity = 0; opportunity < opportunities; opportunity++) {
        for (int i = 0; i < MONSTERS_PER_FLOOR; i++) {
            MonsterRecord *m = &map[i];
            if (!monster_record_alive(g, m)) continue;
            int distance = abs((int)m->x - g->cur_x) + abs((int)m->y - g->cur_y);
            if (distance <= 1) continue;
            int notices = distance <= 12 &&
                          (!player->eff_invisible || game_rand(g) % 3 == 0);
            if (!notices && game_rand(g) % 8) continue;

            int order[4] = {0, 1, 2, 3};
            if (notices) {
                int hx = g->cur_x - (int)m->x;
                int hy = g->cur_y - (int)m->y;
                order[0] = abs(hx) >= abs(hy) ?
                           (hx < 0 ? 2 : 3) : (hy < 0 ? 0 : 1);
                order[1] = abs(hx) >= abs(hy) ?
                           (hy < 0 ? 0 : 1) : (hx < 0 ? 2 : 3);
                order[2] = order[0] ^ 1;
                order[3] = order[1] ^ 1;
            } else {
                int r = game_rand(g) & 3;
                for (int k = 0; k < 4; k++) order[k] = (r + k) & 3;
            }

            for (int k = 0; k < 4; k++) {
                int d = order[k];
                int nx = (int)m->x + dx[d], ny = (int)m->y + dy[d];
                if (nx == g->cur_x && ny == g->cur_y) continue;
                if (!game_can_move(g, m->x, m->y, nx, ny)) continue;
                int occupied = 0;
                for (int j = 0; j < MONSTERS_PER_FLOOR; j++) {
                    if (j != i && monster_record_alive(g, &map[j]) &&
                        map[j].x == nx && map[j].y == ny) {
                        occupied = 1;
                        break;
                    }
                }
                if (occupied) continue;
                m->x = (u8)nx;
                m->y = (u8)ny;
                g->monster_map_dirty = 1;
                break;
            }
        }
    }
    g->monster_adjacent = game_find_adjacent_monster(g) >= 0;
}

int game_ladder_delta(Game *g, int x, int y) {
    return ladder_delta(g, x, y);
}

int game_trapdoor_floor(Game *g, int x, int y) {
    int target = dungeon_hash(x, y, g->cur_floor,
                              g->dungeon_number, 0x960) * 10;
    if (target < 10 || target >= 180 || target / 10 == g->cur_floor / 10)
        return -1;
    /* Original keyed doors only target floors 10..170, but keep the global
     * traversal invariant explicit in case the generator is extended. */
    if (target > game_traversal_rules(g)->max_floor) return -1;
    return target;
}

int game_shop_type(Game *g, int x, int y) {
    if (g->cur_floor != 0 || x <= 0 || x >= MAP_W - 1 ||
        y <= 0 || y >= MAP_H - 1) return 0;
    int type = dungeon_hash(x, y, 0, g->dungeon_number, 110);
    return type >= 1 && type <= 5 ? type : 0;
}

static int pit_bit_is_set(Game *g, int x, int y) {
    int z = g->cur_floor & 31;
    return (g->pit_used[z][y][x >> 3] & (1u << (x & 7))) != 0;
}

static void set_pit_bit(Game *g, int x, int y) {
    int z = g->cur_floor & 31;
    g->pit_used[z][y][x >> 3] |= (u8)(1u << (x & 7));
    g->pit_floor_mask |= (1u << z);
    g->pit_state_dirty = 1;
}

static int pitfall_target(Game *g, int x, int y) {
    int floor = g->cur_floor;
    const GameTraversalRules *rules = game_traversal_rules(g);
    int modulus = 230 - floor / 3;
    if (modulus < 20) modulus = 20;
    /* WORLD func_0EA5A uses a constant 5-in-modulus chance.  The value that
     * changes after level 9 is only the number of deeper floors searched. */
    if (dungeon_hash(x, y, floor, g->dungeon_number, modulus) >= 5)
        return floor;
    int span = floor > 9 ? 5 : 3;
    for (int target = floor + 1;
         target < floor + span &&
         target <= rules->max_floor; target++)
        if (!rock_cell_at(g, x, y, target)) return target;
    return floor;
}

int game_is_known_pitfall(Game *g, int x, int y) {
    if (g->cur_floor <= 0 || x < 0 || x >= MAP_W || y < 0 || y >= MAP_H)
        return 0;
    return pit_bit_is_set(g, x, y) &&
           pitfall_target(g, x, y) != g->cur_floor;
}

void game_refresh_world_palette(Game *g) {
    int palette_floor = g->palette_floor_override >= 0 ?
                        g->palette_floor_override : g->cur_floor;
    video_load_world_palette(&g->video, palette_floor,
                             g->wall_color_r,
                             g->wall_color_g,
                             g->wall_color_b);
}

int game_change_floor(Game *g, Character *player, int new_floor) {
    new_floor = game_clamp_dungeon_floor(g, new_floor);
    if (new_floor == g->cur_floor) return 0;
    save_monster_map(g);
    g->cur_floor = new_floor;
    g->palette_floor_override = -1;
    g->wall_texture_offset = 0;
    player->floor_depth = (u16)new_floor;
    /* WORLD rebuilds both floor-dependent DAC bands as soon as the active
     * depth changes, before any transition or modal frame can be shown. */
    game_refresh_world_palette(g);
    select_monster_floor(g, new_floor);
    load_pit_group(g, new_floor / PIT_GROUP_FLOORS);
    /* Free-placement spells may land on solid rock, and a cached monster can
     * occupy an otherwise valid ladder landing.  Never persist an immobile
     * character: relocate to a legal cell before the new floor is saved. */
    if (rock_cell_at(g, g->cur_x, g->cur_y, new_floor) ||
        game_find_monster(g, g->cur_x, g->cur_y) >= 0)
        game_relocate(g, player);
    memset(g->visited, 0, sizeof(g->visited));
    reveal_around_player(g);
    g->monster_adjacent = game_find_adjacent_monster(g) >= 0;
    mw_audio_play(&g->audio, MW_SFX_LADDER);
    return 1;
}

/* WORLD func_1CCB5's E-key branch derives a fresh dungeon number from the
 * outdoor region, clears the old dungeon actor/cache state, and searches the
 * new floor-zero map for its roof/wilderness shop (type 5). */
void game_begin_new_dungeon(Game *g, Character *player, int dungeon_number) {
    if (!g || !player) return;
    game_save_world_state(g);
    g->dungeon_number = dungeon_number > 0 ? dungeon_number : 1;
    player->dungeon_number = (u16)g->dungeon_number;

    memset(g->monster_map, 0, sizeof(g->monster_map));
    memset(g->monster_floor, 0xFF, sizeof(g->monster_floor));
    g->monster_layer = -1;
    g->monster_map_loaded = 1;
    g->monster_map_dirty = 1;
    g->cur_floor = -1;
    game_change_floor(g, player, 0);

    /* select_monster_floor may have read a previous dungeon's sidecar.  A
     * newly generated world entrance deliberately starts a clean actor set. */
    memset(g->monster_map, 0, sizeof(g->monster_map));
    memset(g->monster_floor, 0xFF, sizeof(g->monster_floor));
    g->monster_layer = 0;
    g->monster_floor[0] = 0;
    g->monster_map_dirty = 1;
    memset(g->pit_used, 0, sizeof(g->pit_used));
    memset(g->pit_row_mask, 0, sizeof(g->pit_row_mask));
    g->pit_floor_mask = 0;
    g->pit_group = 0;
    g->pit_state_loaded = 1;
    g->pit_state_dirty = 1;

    int best_x = -1, best_y = -1, best_dist = 0x7FFFFFFF;
    for (int y = 1; y < MAP_H - 1; y++)
        for (int x = 1; x < MAP_W - 1; x++) {
            if (game_shop_type(g, x, y) != 5 || rock_cell_at(g, x, y, 0))
                continue;
            int dist = abs(x - MAP_W / 2) + abs(y - MAP_H / 2);
            if (dist < best_dist) {
                best_dist = dist;
                best_x = x;
                best_y = y;
            }
        }
    if (best_x < 0) {
        game_relocate(g, player);
    } else {
        g->cur_x = best_x;
        g->cur_y = best_y;
        player->x_pos = (u16)best_x;
        player->y_pos = (u16)best_y;
        memset(g->visited, 0, sizeof(g->visited));
        reveal_around_player(g);
    }
    player->floor_depth = 0;
    g->monster_adjacent = 0;
}

int game_relocate(Game *g, Character *player) {
    for (int tries = 0; tries < 50000; tries++) {
        int x = game_rand(g) % MAP_W, y = game_rand(g) % MAP_H;
        if (rock_cell_at(g, x, y, g->cur_floor)) continue;
        if (game_find_monster(g, x, y) >= 0) continue;
        g->cur_x = x; g->cur_y = y;
        player->x_pos = (u16)x; player->y_pos = (u16)y;
        memset(g->visited, 0, sizeof(g->visited));
        reveal_around_player(g);
        return 1;
    }
    return 0;
}

int game_pass_wall(Game *g, Character *player) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dy[4] = {-1, 1, 0, 0};
    int dir = g->last_move_dir & 3;
    /* The original directional spell searches as far as twenty squares for
     * the first legal landing beyond the intervening wall/rock. */
    for (int distance = 2; distance <= 20; distance++) {
        int x = g->cur_x + dx[dir] * distance;
        int y = g->cur_y + dy[dir] * distance;
        if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) break;
        if (rock_cell_at(g, x, y, g->cur_floor)) continue;
        if (game_find_monster(g, x, y) >= 0) continue;
        g->cur_x = x; g->cur_y = y;
        player->x_pos = (u16)x; player->y_pos = (u16)y;
        memset(g->visited, 0, sizeof(g->visited));
        reveal_around_player(g);
        return 1;
    }
    return 0;
}

static int pitfall_destination(Game *g) {
    if (g->cur_floor <= 0 || ladder_delta(g, g->cur_x, g->cur_y) != 0 ||
        game_trapdoor_floor(g, g->cur_x, g->cur_y) >= 0)
        return g->cur_floor;

    /* This is discovery history, not a one-shot disarm flag.  WORLD checks
     * the same deterministic chute every time the square is crossed, while
     * the saved bit causes a magenta X to appear on later map visits. */
    set_pit_bit(g, g->cur_x, g->cur_y);
    return pitfall_target(g, g->cur_x, g->cur_y);
}

int game_apply_pitfall(Game *g, Character *player) {
    int target = pitfall_destination(g);
    if (target == g->cur_floor) return 0;
    game_change_floor(g, player, target);
    save_pit_group(g);
    return 1;
}

/* MW_PORT: dungeon initialization branch of WORLD func_0F6E5 plus
 * func_0A3E7/load_dungeon_bin and original resource setup. */
/* ── Initialization ── */

int game_init(Game *g, const char *data_dir) {
    memset(g, 0, sizeof(*g));
    g->active_save_slot = -1;
    g->monster_layer = -1;
    g->pit_group = -1;
    g->brick_speed = 3;
    g->sound_enabled = 1;
    g->map_player_visible = 1;
    g->palette_floor_override = -1;
    strncpy(g->game_dir, data_dir, sizeof(g->game_dir) - 1);

    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) < 0) {
            fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return -1;
        }
    }

    if (video_init(&g->video, "Moraff's World", 1) < 0) {
        return -1;
    }

    int saved_display_mode = load_display_mode_setting(g);
    if (video_set_display_mode(&g->video, saved_display_mode, 1) < 0)
        video_set_display_mode(&g->video,
                               MW_DISPLAY_SVGA_1024X768_256_A, 1);

    if (!SDL_WasInit(SDL_INIT_AUDIO)) SDL_InitSubSystem(SDL_INIT_AUDIO);
    mw_audio_init(&g->audio); /* A missing host audio device is non-fatal. */
    mw_audio_set_enabled(&g->audio, g->sound_enabled);

    input_init(&g->input);
    g->turbo_percent = 100;

    game_load_display_font(g, g->video.display_mode);

    game_srand(g, (u32)SDL_GetTicks());

    for (int i = 0; i < MAX_PLAYERS; i++) {
        game_load_character(g, i);
    }

    game_load_pics(g);
    game_load_dungeon(g);
    game_load_monsters(g);

    g->num_players = 1;
    g->cur_player = 0;
    g->dungeon_max_floor = MAX_DUNGEON_FLOOR;

    const MwDisplayModeInfo *display =
        video_display_mode_info(g->video.display_mode);
    g->video_mode = display ? display->world_mode : 8;
    g->screen_w = display ? display->raster_w : LOGICAL_W;
    g->screen_h = display ? display->raster_h : LOGICAL_H;

    return 0;
}

void game_shutdown(Game *g) {
    game_save_world_state(g);
    for (int i = 0; i < 256; i++) free(g->world_pic_data[i]);
    for (int i = 0; i < 64; i++) free(g->wall_pic_data[i]);
    for (int i = 0; i < 2; i++) free(g->wall_texture[i]);
    free(g->dungeon_data);
    free(g->worldmap_data);
    free(g->monster_data);

    mw_audio_shutdown(&g->audio);
    video_shutdown(&g->video);
    SDL_Quit();
}

/* MW_PORT: WORLD func_1F077/func_1F3FD/far_1FAE6 visibility and map memory;
 * closed door edges terminate revelation but do not block traversal. */
/* ── Fog of war: reveal cells around the player ── */

static int edge_from_dir(Game *g, int x, int y, int dir) {
    switch (dir) {
        case 0: return map_get_edge(g, x,     y,     1); /* north */
        case 1: return map_get_edge(g, x,     y + 1, 1); /* south */
        case 2: return map_get_edge(g, x,     y,     0); /* west */
        case 3: return map_get_edge(g, x + 1, y,     0); /* east */
    }
    return 0;
}

static void mark_seen(Game *g, int x, int y) {
    if (x >= 0 && x < MAP_W && y >= 0 && y < MAP_H)
        g->visited[y][x] = 1;
}

static void reveal_around_player(Game *g) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dy[4] = {-1, 1, 0, 0};
    static const int left[4]  = {2, 3, 1, 0};
    static const int right[4] = {3, 2, 0, 1};

    mark_seen(g, g->cur_x, g->cur_y);

    /* The four simultaneous views expose straight passages and the mouths of
     * side passages.  A stone edge or door is visible, but stops discovery of
     * the cells behind it.  Seen bits remain set just like g_CBD8 in WORLD. */
    for (int dir = 0; dir < 4; dir++) {
        int x = g->cur_x, y = g->cur_y;
        for (int depth = 0; depth < 7; depth++) {
            if (depth > 0) {
                int ld = left[dir], rd = right[dir];
                if (edge_from_dir(g, x, y, ld) == 3)
                    mark_seen(g, x + dx[ld], y + dy[ld]);
                if (edge_from_dir(g, x, y, rd) == 3)
                    mark_seen(g, x + dx[rd], y + dy[rd]);
            }
            if (edge_from_dir(g, x, y, dir) != 3) break;
            x += dx[dir];
            y += dy[dir];
            if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) break;
            mark_seen(g, x, y);
        }
    }
}

void game_update_visibility(Game *g) {
    reveal_around_player(g);
}

/* B selects the delay between newly exposed map bricks.  WORLD retains four
 * settings, with the fourth effectively instantaneous on fast machines. */
static void reveal_around_player_animated(Game *g, Character *player) {
    u8 before[MAP_H][MAP_W];
    u8 target[MAP_H][MAP_W];
    static const int delay_ms[4] = {45, 20, 8, 0};
    memcpy(before, g->visited, sizeof(before));
    reveal_around_player(g);
    memcpy(target, g->visited, sizeof(target));
    if (g->brick_speed >= 3) return;

    memcpy(g->visited, before, sizeof(before));
    for (int distance = 0; distance < 14; distance++) {
        int changed = 0;
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) {
                if (before[y][x] || !target[y][x] ||
                    abs(x - g->cur_x) + abs(y - g->cur_y) != distance)
                    continue;
                g->visited[y][x] = 1;
                changed = 1;
        }
        if (changed) {
            int mx, my, mw, mh;
            game_normal_map_rect(g, &mx, &my, &mw, &mh);
            draw_minimap(g, mx, my, mw, mh);
            video_present(&g->video);
            game_delay(g, (u32)delay_ms[g->brick_speed & 3]);
        }
    }
    memcpy(g->visited, target, sizeof(target));
    (void)player;
}

/* ── Check if any adjacent cell has a monster ── */

static int has_adjacent_monster(Game *g) {
    g->monster_adjacent = game_find_adjacent_monster(g) >= 0;
    return g->monster_adjacent;
}

/* Convert WORLD's common 1600x1200 design coordinates through the selected
 * driver's integer raster first.  Drawing directly with SX/SY and shrinking
 * afterward loses the characteristic rounding of the CGA/EGA branches. */
static int display_design_x(const Game *g, int design_x) {
    const MwDisplayModeInfo *info =
        video_display_mode_info(g->video.display_mode);
    if (!info) return design_x * LOGICAL_W / 1600;
    int native = design_x * info->raster_w / 1600;
    return native * LOGICAL_W / info->raster_w;
}

static int display_design_y(const Game *g, int design_y) {
    const MwDisplayModeInfo *info =
        video_display_mode_info(g->video.display_mode);
    if (!info) return design_y * LOGICAL_H / 1200;
    int native = design_y * info->raster_h / 1200;
    return native * LOGICAL_H / info->raster_h;
}

static void game_normal_map_rect(const Game *g, int *x, int *y,
                                 int *w, int *h) {
    const MwDisplayModeInfo *info =
        video_display_mode_info(g->video.display_mode);
    if (!info) {
        *x = 0; *y = 0x1AE * LOGICAL_H / 1200;
        *w = 0x11B * LOGICAL_W / 1600; *h = 380;
        return;
    }
    *x = 0;
    *y = display_design_y(g, 0x1AE);
    *w = info->map_cell_px * info->map_cols * LOGICAL_W / info->raster_w;
    *h = info->map_cell_px * info->map_rows * LOGICAL_H / info->raster_h;
}

/* MW_PORT: WORLD func_1F077, func_1F3FD and far_1FAE6 (all driver maps),
 * including wall, door, ladder, trap-door, known-pit and blinking-player
 * symbols. */
/* ── Drawing: native-driver map window (WORLD.ASM sub_086F1/far_1FAE6) ── */

/* WORLD.ASM sub_1F077 does not recolor a door's wall.  At the ten-pixel
 * 1024-mode scale it draws a small white crossbar symbol perpendicular to
 * that wall.  The five-pixel expanded map takes the routine's compact path
 * and uses one three-pixel crossbar. */
static void map_native_pixel(Video *v, int x, int y, int cw, int ch,
                             int cell, int px, int py, u8 color) {
    int x0 = x + px * cw / cell;
    int y0 = y + py * ch / cell;
    int x1 = x + (px + 1) * cw / cell;
    int y1 = y + (py + 1) * ch / cell;
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    video_fill_rect(v, x0, y0, x1 - x0, y1 - y0, color);
}

static void map_native_hline(Video *v, int x, int y, int cw, int ch,
                             int cell, int px, int py, int length, u8 color) {
    for (int i = 0; i < length; i++)
        map_native_pixel(v, x, y, cw, ch, cell, px + i, py, color);
}

static void map_native_vline(Video *v, int x, int y, int cw, int ch,
                             int cell, int px, int py, int length, u8 color) {
    for (int i = 0; i < length; i++)
        map_native_pixel(v, x, y, cw, ch, cell, px, py + i, color);
}

static void draw_map_door_marker(Video *v, int x, int y, int cw, int ch,
                                 int cell, int horizontal, int far_edge,
                                 u8 color) {
    int half = cell >= 8 ? cell / 3 : 1;
    int cx = cell / 2;
    int cy = cell / 2;
    if (horizontal) {
        int py = far_edge ? cell - 1 - half : 0;
        map_native_vline(v, x, y, cw, ch, cell, cx, py, half + 1, color);
        if (cell >= 8 && cx + 1 < cell)
            map_native_vline(v, x, y, cw, ch, cell, cx + 1, py,
                             half + 1, color);
    } else {
        int px = far_edge ? cell - 1 - half : 0;
        map_native_hline(v, x, y, cw, ch, cell, px, cy, half + 1, color);
        if (cell >= 8 && cy + 1 < cell)
            map_native_hline(v, x, y, cw, ch, cell, px, cy + 1,
                             half + 1, color);
    }
}

void draw_minimap(Game *g, int mx, int my, int mw, int mh) {
    Video *v = &g->video;
    const MwDisplayModeInfo *info = video_display_mode_info(v->display_mode);
    const int cell_px = info ? info->map_cell_px : 10;
    int cols = info ? info->map_cols : 18;
    int rows = info ? info->map_rows : 38;
    int raster_w = info ? info->raster_w : LOGICAL_W;
    int raster_h = info ? info->raster_h : LOGICAL_H;
    mw = cell_px * cols * LOGICAL_W / raster_w;
    mh = cell_px * rows * LOGICAL_H / raster_h;
    int first_x = g->cur_x - cols / 2;
    int first_y = g->cur_y - rows / 2;

    /* The original map is a clipped dark-red field at design y=0x1AE. */
    video_fill_rect(v, mx, my, mw, mh, 10);

    /* First pass: black interiors.  The DOS map represents explored floor as
     * ten-pixel bricks cut into the dark-red unexplored field. */
    for (int gy = 0; gy < rows; gy++) {
        for (int gx = 0; gx < cols; gx++) {
            int wx = first_x + gx;
            int wy = first_y + gy;
            if (wx < 0 || wx >= MAP_W || wy < 0 || wy >= MAP_H) continue;
            if (!g->visited[wy][wx]) continue;

            int x0 = mx + gx * cell_px * LOGICAL_W / raster_w;
            int y0 = my + gy * cell_px * LOGICAL_H / raster_h;
            int x1 = mx + (gx + 1) * cell_px * LOGICAL_W / raster_w;
            int y1 = my + (gy + 1) * cell_px * LOGICAL_H / raster_h;
            video_fill_rect(v, x0, y0, x1 - x0, y1 - y0, 0);
        }
    }

    /* Second pass: white outside edges and red mortar between explored
     * neighbors.  This is the brick/pipe treatment visible in mode 8. */
    for (int gy = 0; gy < rows; gy++) {
        for (int gx = 0; gx < cols; gx++) {
            int wx = first_x + gx;
            int wy = first_y + gy;
            if (wx < 0 || wx >= MAP_W || wy < 0 || wy >= MAP_H) continue;
            if (!g->visited[wy][wx]) continue;

            int x = mx + gx * cell_px * LOGICAL_W / raster_w;
            int y = my + gy * cell_px * LOGICAL_H / raster_h;
            int next_x = mx + (gx + 1) * cell_px * LOGICAL_W / raster_w;
            int next_y = my + (gy + 1) * cell_px * LOGICAL_H / raster_h;
            int cw = next_x - x;
            int ch = next_y - y;
            int edge[4] = {
                map_get_edge(g, wx,     wy,     1),
                map_get_edge(g, wx + 1, wy,     0),
                map_get_edge(g, wx,     wy + 1, 1),
                map_get_edge(g, wx,     wy,     0)
            };

            /* Surface shops are encoded by func_0C83D and colored type+2 in
             * the original map renderer: store, temple, bank, inn, hotel. */
            int shop = game_shop_type(g, wx, wy);
            if (shop)
                video_fill_rect(v, x, y, cw, ch, (u8)(shop + 2));

            /* far_1F3FD calls func_1F077 once for each actual edge.  Open
             * value 3 is left undrawn.  Stone and door edges retain the same
             * white wall; doors receive sub_1F077's perpendicular marker. */
            if (edge[0] != 3) {
                map_native_hline(v, x, y, cw, ch, cell_px,
                                 0, 0, cell_px, MW_COLOR_MAP_WHITE);
                if (edge[0] == 1)
                    draw_map_door_marker(v, x, y, cw, ch, cell_px, 1, 0,
                                         MW_COLOR_MAP_WHITE);
            }
            if (edge[2] != 3) {
                map_native_hline(v, x, y, cw, ch, cell_px,
                                 0, cell_px - 1, cell_px,
                                 MW_COLOR_MAP_WHITE);
                if (edge[2] == 1)
                    draw_map_door_marker(v, x, y, cw, ch,
                                         cell_px, 1, 1,
                                         MW_COLOR_MAP_WHITE);
            }
            if (edge[3] != 3) {
                map_native_vline(v, x, y, cw, ch, cell_px,
                                 0, 0, cell_px, MW_COLOR_MAP_WHITE);
                if (edge[3] == 1)
                    draw_map_door_marker(v, x, y, cw, ch, cell_px, 0, 0,
                                         MW_COLOR_MAP_WHITE);
            }
            if (edge[1] != 3) {
                map_native_vline(v, x, y, cw, ch, cell_px,
                                 cell_px - 1, 0, cell_px,
                                 MW_COLOR_MAP_WHITE);
                if (edge[1] == 1)
                    draw_map_door_marker(v, x, y, cw, ch,
                                         cell_px, 0, 1,
                                         MW_COLOR_MAP_WHITE);
            }

            /* Red corner pixels reproduce the brick joints against the
             * dark-red unexplored field without inventing neighbor walls. */
            map_native_pixel(v, x, y, cw, ch, cell_px, 0, 0, 10);
            map_native_pixel(v, x, y, cw, ch, cell_px,
                             cell_px - 1, cell_px - 1, 10);

            int ladder = ladder_delta(g, wx, wy);
            if (ladder > 0) {
                /* The DOS map uses opposite diagonal strokes for the two
                 * ladder directions.  Down is a backslash. */
                for (int p = 1; p < cell_px - 1; p++) {
                    map_native_pixel(v, x, y, cw, ch, cell_px, p, p,
                                     MW_COLOR_STATUS_CYAN);
                }
            } else if (ladder < 0) {
                /* Up is a forward slash. */
                for (int p = 1; p < cell_px - 1; p++) {
                    map_native_pixel(v, x, y, cw, ch, cell_px,
                                     p, cell_px - 1 - p, 4);
                }
            }

            if (shop) {
                /* The town help calls these "colored squares" and explicitly
                 * identifies them as ladders going up to a location.  Put a
                 * high-contrast ladder inside the authentic type+2 color. */
                map_native_hline(v, x, y, cw, ch, cell_px,
                                 0, 0, cell_px, MW_COLOR_MAP_WHITE);
                map_native_hline(v, x, y, cw, ch, cell_px,
                                 0, cell_px - 1, cell_px,
                                 MW_COLOR_MAP_WHITE);
                map_native_vline(v, x, y, cw, ch, cell_px,
                                 0, 0, cell_px, MW_COLOR_MAP_WHITE);
                map_native_vline(v, x, y, cw, ch, cell_px,
                                 cell_px - 1, 0, cell_px,
                                 MW_COLOR_MAP_WHITE);
                int rail1 = cell_px / 3;
                int rail2 = cell_px - 1 - rail1;
                map_native_vline(v, x, y, cw, ch, cell_px,
                                 rail1, 1, cell_px - 2, 0);
                map_native_vline(v, x, y, cw, ch, cell_px,
                                 rail2, 1, cell_px - 2, 0);
                for (int rung = 2; rung < cell_px - 1; rung += 2)
                    map_native_hline(v, x, y, cw, ch, cell_px,
                                     rail1, rung, rail2 - rail1 + 1, 4);
            }

            int trap = game_trapdoor_floor(g, wx, wy);
            if (!ladder && trap >= 0) {
                /* WORLD describes a trap door with an X, not a generic
                   unexplained square. */
                for (int p = 1; p < cell_px - 1; p++) {
                    map_native_pixel(v, x, y, cw, ch, cell_px, p, p, 4);
                    map_native_pixel(v, x, y, cw, ch, cell_px,
                                     cell_px - 1 - p, p, 4);
                }
            } else if (!ladder && game_is_known_pitfall(g, wx, wy)) {
                /* WORLD draws a discovered chute as a color-3 (magenta) X. */
                for (int p = 1; p < cell_px - 1; p++) {
                    map_native_pixel(v, x, y, cw, ch, cell_px, p, p, 3);
                    map_native_pixel(v, x, y, cw, ch, cell_px,
                                     cell_px - 1 - p, p, 3);
                }
            }

            if (wx == g->cur_x && wy == g->cur_y) {
                /* Like WORLD's map cursor, the party is a blinking square.
                 * Its off phase exposes the feature beneath it. */
                if (g->map_player_visible) {
                    int p0 = cell_px / 3;
                    int p1 = cell_px - p0;
                    for (int py = p0; py < p1; py++)
                        for (int px = p0; px < p1; px++)
                            map_native_pixel(v, x, y, cw, ch, cell_px,
                                             px, py, 15);
                    map_native_pixel(v, x, y, cw, ch, cell_px,
                                     cell_px / 2, cell_px / 2, 6);
                }
            }
        }
    }
}

/* MW_PORT: WORLD func_14B85 (actors), func_14F53 (wall faces), func_16488
 * (perspective scene), func_1F355/func_1F3B2/func_1F9EF (dungeon geometry),
 * and func_0D74F (four-view arrangement). */
/* ── Drawing: 3D first-person view (one viewport, called four times) ── */

/* Direction deltas: index by render direction (0=N, 1=S, 2=W, 3=E) */
static void dir_to_delta(int dir, int *dx, int *dy) {
    switch (dir) {
        case 0: *dx =  0; *dy = -1; break; /* North */
        case 1: *dx =  0; *dy =  1; break; /* South */
        case 2: *dx = -1; *dy =  0; break; /* West */
        case 3: *dx =  1; *dy =  0; break; /* East */
    }
}

static void dir_to_right(int dir, int *dx, int *dy) {
    switch (dir) {
        case 0: *dx =  1; *dy =  0; break; /* North: right is East */
        case 1: *dx = -1; *dy =  0; break; /* South: right is West */
        case 2: *dx =  0; *dy = -1; break; /* West: right is North */
        case 3: *dx =  0; *dy =  1; break; /* East: right is South */
    }
}

static int edge_between(Game *g, int x1, int y1, int x2, int y2) {
    return edge_between_cells(g, x1, y1, x2, y2);
}

static int check_wall_ahead(Game *g, int px, int py, int dir, int dist) {
    int fdx, fdy;
    dir_to_delta(dir, &fdx, &fdy);
    int tx = px + fdx * dist;
    int ty = py + fdy * dist;
    if (map_is_wall(g, tx, ty)) return 0;
    int prev_x = px + fdx * (dist - 1);
    int prev_y = py + fdy * (dist - 1);
    return edge_between(g, prev_x, prev_y, tx, ty);
}

static int check_wall_side(Game *g, int px, int py, int dir, int dist, int side) {
    int fdx, fdy, rdx, rdy;
    dir_to_delta(dir, &fdx, &fdy);
    dir_to_right(dir, &rdx, &rdy);
    int corridor_x = px + fdx * dist;
    int corridor_y = py + fdy * dist;
    int tx = corridor_x + rdx * side;
    int ty = corridor_y + rdy * side;
    if (map_is_wall(g, tx, ty)) return 0;
    return edge_between(g, corridor_x, corridor_y, tx, ty);
}

typedef struct { float x, y, u, t; } WallVertex;
typedef struct { int left, right, top, bottom; } ProjRect;

static void dungeon_line(Video *v, int x0, int y0, int x1, int y1, u8 color,
                         int vx, int vy, int vw, int vh) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= vx && x0 < vx + vw && y0 >= vy && y0 < vy + vh)
            video_put_pixel(v, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static u8 remap_wall_texel(Video *v, u8 src, int door,
                           int screen_x, int screen_y) {
    const MwDisplayModeInfo *info = video_display_mode_info(v->display_mode);
    int style = info ? info->wall_style : MW_WALL_CHUNKY256;
    int nx = info ? screen_x * info->raster_w / LOGICAL_W : screen_x;
    int ny = info ? screen_y * info->raster_h / LOGICAL_H : screen_y;

    if (style == MW_WALL_HERCULES) {
        if (door)
            return src == 1 ? 0 : 15;
        if (src == 13) return 0;
        if (src == 18 || src == 19)
            return ((nx + ny + (src == 19)) & 1) ? 15 : 0;
        return ((nx >> 1) + ny) % 5 == 0 ? 0 : 15;
    }

    if (style == MW_WALL_CGA) {
        if (src == 0 || src == 1 || src == 16) return 0;
        if (door) {
            /* Mode 1's four-colour line writer encodes the WALL.PIC colors
             * as red masonry, a green arch/door face and yellow ironwork.
             * Preserve those semantic groups instead of turning the complete
             * door wall yellow. */
            if (src == 14 || src == 12) return 6;
            if (src == 13) return 0;
            if (src == 6 || src == 11) return 8;
            return 4;
        }
        /* WORLD's mode-1 branch does not retain WALL.PIC's 16-colour
         * texture.  func_16488 reduces an opaque wall plane to the selected
         * CGA red and supplies the corridor construction lines separately. */
        return 6;
    }

    if (style == MW_WALL_PLANAR16) {
        if (door) {
            if (src == 1) return 0;
            if (src == 6 || src == 11 || src == 13) return 2;
            /* WALL.PIC already distinguishes gray stone (14), dark mortar
             * (12) and white ironwork (15).  Flattening all three to white
             * made planar doors look blown out and much denser than DOS. */
            return src < 16 ? src : 0;
        }
        /* Literal func_14F53 planar path: ordinary 0..15 texels remain
         * logical EGA indices, command 16 is transparent, and both gradient
         * commands collapse to logical colour 4. */
        if (src == 16) return 0;
        if (src == 17) return 3;
        if (src == 18 || src == 19) return 4;
        return src < 16 ? src : 0;
    }

    if (door) {
        /* Mode 8 changes these low DAC entries while drawing WALL.PIC. */
        if (src == 1) return 0;                 /* black door boards */
        if (src == 6 || src == 11 || src == 13) return 2; /* bright blue arch */
        return src;                             /* white ironwork */
    }
    if (src == 14) return MW_COLOR_WALL_FACE;
    if (src == 12) return MW_COLOR_WALL_HIGHLIGHT;
    if (src == 13) return MW_COLOR_WALL_CRACK;
    if (src == 0 || src == 16) return MW_COLOR_WALL_TINT;
    /* WORLD's mode-8 wall renderer turns its two gradient commands into
     * screen-position palette indices spanning 0x40..0xBF. */
    if (src == 18) {
        int shift = info && info->raster_w > 1000 ? 1 : 2;
        return (u8)(0x40 + ((nx >> shift) & 0x7F));
    }
    if (src == 19) {
        int width = info ? info->raster_w : LOGICAL_W;
        int shift = width > 1000 ? 3 : 2;
        return (u8)(0x40 + (((width - nx) >> shift) & 0x7F));
    }
    /* Preserve the original floor-colored surface ramp for uncommon wall
     * details instead of flattening every high source color to blue. */
    if (src >= 20 && src <= 31) return src;
    return MW_COLOR_WALL_CRACK;
}

/* WALL.PIC's 256x200 RLE scanlines are stored a quarter-turn counter-clockwise
 * from their on-screen wall orientation.  The DOS wall blitter's coordinate
 * walk supplied that rotation; sampling the decoded surface directly made an
 * arched door into a horizontal capsule. */
static u8 sample_wall_texel(const u8 *tex, int u, int v) {
    if (u < 0) u = 0; if (u > 255) u = 255;
    if (v < 0) v = 0; if (v > 199) v = 199;
    int src_x = v * 255 / 199;
    int src_y = 199 - u * 199 / 255;
    return tex[src_y * 256 + src_x];
}

static void textured_triangle(Video *v, const u8 *tex,
                              WallVertex a, WallVertex b, WallVertex c,
                              int clip_x, int clip_y, int clip_w, int clip_h,
                              int door) {
    float minxf = fminf(a.x, fminf(b.x, c.x));
    float maxxf = fmaxf(a.x, fmaxf(b.x, c.x));
    float minyf = fminf(a.y, fminf(b.y, c.y));
    float maxyf = fmaxf(a.y, fmaxf(b.y, c.y));
    int minx = (int)floorf(minxf), maxx = (int)ceilf(maxxf);
    int miny = (int)floorf(minyf), maxy = (int)ceilf(maxyf);
    if (minx < clip_x) minx = clip_x;
    if (miny < clip_y) miny = clip_y;
    if (maxx >= clip_x + clip_w) maxx = clip_x + clip_w - 1;
    if (maxy >= clip_y + clip_h) maxy = clip_y + clip_h - 1;

    float denom = (b.y - c.y) * (a.x - c.x) +
                  (c.x - b.x) * (a.y - c.y);
    if (fabsf(denom) < 0.001f) return;

    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float wa = ((b.y - c.y) * (px - c.x) +
                        (c.x - b.x) * (py - c.y)) / denom;
            float wb = ((c.y - a.y) * (px - c.x) +
                        (a.x - c.x) * (py - c.y)) / denom;
            float wc = 1.0f - wa - wb;
            if (wa < -0.001f || wb < -0.001f || wc < -0.001f) continue;
            int tx = (int)(wa * a.u + wb * b.u + wc * c.u);
            int ty = (int)(wa * a.t + wb * b.t + wc * c.t);
            if (tx < 0) tx = 0; if (tx > 255) tx = 255;
            if (ty < 0) ty = 0; if (ty > 199) ty = 199;
            v->pixels[y * LOGICAL_W + x] =
                remap_wall_texel(v, sample_wall_texel(tex, tx, ty), door,
                                 x, y);
        }
    }
    v->dirty = 1;
}

static void textured_quad(Video *v, const u8 *tex,
                          WallVertex a, WallVertex b, WallVertex c, WallVertex d,
                          int vx, int vy, int vw, int vh, int door) {
    textured_triangle(v, tex, a, b, c, vx, vy, vw, vh, door);
    textured_triangle(v, tex, a, c, d, vx, vy, vw, vh, door);
}

static ProjRect projection_rect(int vx, int vy, int vw, int vh, int depth) {
    ProjRect p;
    if (depth == 0) {
        p.left = vx; p.right = vx + vw - 1;
        p.top = vy; p.bottom = vy + vh - 1;
        return p;
    }
    /* The original view projects cell boundaries at 1, 1/3, 1/5 ... of
     * the viewport: the eye is at the cell centre and successive walls are
     * 0.5, 1.5, 2.5 ... cells away. */
    float scale = 1.0f / (1.0f + depth * 2.0f);
    int cx = vx + (vw - 1) / 2;
    int cy = vy + (vh - 1) / 2;
    int hw = (int)((vw - 1) * 0.5f * scale);
    int hh = (int)((vh - 1) * 0.5f * scale);
    p.left = cx - hw; p.right = cx + hw;
    p.top = cy - hh; p.bottom = cy + hh;
    return p;
}

static void solid_triangle(Video *v, WallVertex a, WallVertex b, WallVertex c,
                           int clip_x, int clip_y, int clip_w, int clip_h,
                           u8 color) {
    int minx = (int)floorf(fminf(a.x, fminf(b.x, c.x)));
    int maxx = (int)ceilf(fmaxf(a.x, fmaxf(b.x, c.x)));
    int miny = (int)floorf(fminf(a.y, fminf(b.y, c.y)));
    int maxy = (int)ceilf(fmaxf(a.y, fmaxf(b.y, c.y)));
    if (minx < clip_x) minx = clip_x;
    if (miny < clip_y) miny = clip_y;
    if (maxx >= clip_x + clip_w) maxx = clip_x + clip_w - 1;
    if (maxy >= clip_y + clip_h) maxy = clip_y + clip_h - 1;
    float denom = (b.y - c.y) * (a.x - c.x) +
                  (c.x - b.x) * (a.y - c.y);
    if (fabsf(denom) < 0.001f) return;
    for (int y = miny; y <= maxy; y++)
        for (int x = minx; x <= maxx; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float wa = ((b.y - c.y) * (px - c.x) +
                        (c.x - b.x) * (py - c.y)) / denom;
            float wb = ((c.y - a.y) * (px - c.x) +
                        (a.x - c.x) * (py - c.y)) / denom;
            float wc = 1.0f - wa - wb;
            if (wa >= -0.001f && wb >= -0.001f && wc >= -0.001f)
                v->pixels[y * LOGICAL_W + x] = color;
        }
    v->dirty = 1;
}

static void solid_quad(Video *v, WallVertex a, WallVertex b,
                       WallVertex c, WallVertex d,
                       int vx, int vy, int vw, int vh, u8 color) {
    solid_triangle(v, a, b, c, vx, vy, vw, vh, color);
    solid_triangle(v, a, c, d, vx, vy, vw, vh, color);
}

static void draw_dungeon_perspective_tiles(Video *v, int vx, int vy,
                                           int vw, int vh,
                                           u8 first, u8 second) {
    int cx = vx + (vw - 1) / 2;
    for (int depth = 7; depth >= 0; depth--) {
        ProjRect outer = projection_rect(vx, vy, vw, vh, depth);
        ProjRect inner = projection_rect(vx, vy, vw, vh, depth + 1);
        u8 left = (depth & 1) ? first : second;
        u8 right = (depth & 1) ? second : first;
        solid_quad(v,
            (WallVertex){outer.left, outer.top,0,0},
            (WallVertex){cx, outer.top,0,0},
            (WallVertex){cx, inner.top,0,0},
            (WallVertex){inner.left, inner.top,0,0},
            vx,vy,vw,vh,left);
        solid_quad(v,
            (WallVertex){cx, outer.top,0,0},
            (WallVertex){outer.right, outer.top,0,0},
            (WallVertex){inner.right, inner.top,0,0},
            (WallVertex){cx, inner.top,0,0},
            vx,vy,vw,vh,right);
        solid_quad(v,
            (WallVertex){inner.left, inner.bottom,0,0},
            (WallVertex){cx, inner.bottom,0,0},
            (WallVertex){cx, outer.bottom,0,0},
            (WallVertex){outer.left, outer.bottom,0,0},
            vx,vy,vw,vh,left);
        solid_quad(v,
            (WallVertex){cx, inner.bottom,0,0},
            (WallVertex){inner.right, inner.bottom,0,0},
            (WallVertex){outer.right, outer.bottom,0,0},
            (WallVertex){cx, outer.bottom,0,0},
            vx,vy,vw,vh,right);
    }
}

static void draw_dungeon_gradient(Video *v, int vx, int vy, int vw, int vh) {
    static const u8 ceiling[] = {
        MW_COLOR_CEILING_1, MW_COLOR_CEILING_2,
        MW_COLOR_CEILING_1, MW_COLOR_CEILING_3,
        MW_COLOR_CEILING_2, MW_COLOR_CEILING_1,
        MW_COLOR_DUNGEON_3, MW_COLOR_CEILING_3
    };
    static const u8 floor[] = {
        MW_COLOR_FLOOR_1, 0, MW_COLOR_FLOOR_2, 0,
        MW_COLOR_FLOOR_3, 0, MW_COLOR_FLOOR_4, 0,
        MW_COLOR_FLOOR_5, 0
    };
    const MwDisplayModeInfo *info = video_display_mode_info(v->display_mode);
    int style = info ? info->wall_style : MW_WALL_CHUNKY256;
    int world_mode = info ? info->world_mode : 10;
    int horizon = vy + vh / 2;
    video_fill_rect(v, vx, vy, vw, vh, 0);

    if (style == MW_WALL_PLANAR16) {
        draw_dungeon_perspective_tiles(v, vx, vy, vw, vh, 10, 11);
        return;
    }

    if (style == MW_WALL_CHUNKY256 && world_mode != 9) {
        draw_dungeon_perspective_tiles(v, vx, vy, vw, vh, 26, 27);
        return;
    }

    for (int x = vx; x < vx + vw; x++) {
        int nx = info ? x * info->raster_w / LOGICAL_W : x;
        u8 cc, fc;
        if (style == MW_WALL_HERCULES) {
            cc = ((nx >> 1) & 1) ? 0 : 15;
            fc = (nx & 1) ? 0 : 15;
        } else if (style == MW_WALL_CGA) {
            cc = ((nx >> 1) & 1) ? 0 : 3;
            fc = (nx & 1) ? 0 : 2;
        } else if (style == MW_WALL_PLANAR16) {
            cc = ((nx >> 1) & 1) ? 1 : 9;
            fc = (nx & 1) ? 0 : 2;
        } else {
            cc = ceiling[(x - vx) & 7];
            fc = floor[(x - vx) % 10];
        }
        video_vline(v, x, vy, horizon - vy, cc);
        video_vline(v, x, horizon, vy + vh - horizon, fc);
    }
    video_hline(v, vx, horizon - 2, vw, 0);
    video_hline(v, vx, horizon - 1, vw, MW_COLOR_WALL_CRACK);
    video_hline(v, vx, horizon, vw, MW_COLOR_FLOOR_4);
    video_hline(v, vx, horizon + 1, vw, 0);
}

/* Cast one ray for each viewport column against the wall *edges* in DUNG.BIN.
 * WORLD.C's func_16488 is likewise ray based.  The old port only followed the
 * centre cell chain, so it could not see the return walls at intersections;
 * independent trapezoids consequently appeared to float in space.  Edge DDA
 * makes every visible wall meet its neighbours and maps a door to its own
 * upright wall plane, including when that plane is viewed from the side. */
static void draw_ray_walls(Game *g, int vx, int vy, int vw, int vh, int dir,
                           float *wall_depth) {
    Video *v = &g->video;
    static u8 fallback[256 * 200];
    static int fallback_ready;
    if (!fallback_ready) {
        memset(fallback, 14, sizeof(fallback));
        fallback_ready = 1;
    }

    int fdx, fdy, rdx, rdy;
    dir_to_delta(dir, &fdx, &fdy);
    dir_to_right(dir, &rdx, &rdy);
    const float pos_x = (float)g->cur_x + 0.5f;
    const float pos_y = (float)g->cur_y + 0.5f;
    const int horizon = vy + (vh - 1) / 2;

    for (int column = 0; column < vw; column++) {
        if (wall_depth) wall_depth[column] = 1.0e30f;
        /* A 90-degree horizontal field of view.  With the eye at a cell's
         * centre this makes an immediate front wall fill the viewport. */
        float camera = 2.0f * ((column + 0.5f) / (float)vw) - 1.0f;
        float ray_x = (float)fdx + (float)rdx * camera;
        float ray_y = (float)fdy + (float)rdy * camera;
        int map_x = g->cur_x;
        int map_y = g->cur_y;
        int step_x = ray_x < 0.0f ? -1 : 1;
        int step_y = ray_y < 0.0f ? -1 : 1;
        float delta_x = fabsf(ray_x) < 0.00001f ? 1.0e30f : fabsf(1.0f / ray_x);
        float delta_y = fabsf(ray_y) < 0.00001f ? 1.0e30f : fabsf(1.0f / ray_y);
        float side_x = ray_x < 0.0f ?
            (pos_x - floorf(pos_x)) * delta_x :
            (floorf(pos_x) + 1.0f - pos_x) * delta_x;
        float side_y = ray_y < 0.0f ?
            (pos_y - floorf(pos_y)) * delta_y :
            (floorf(pos_y) + 1.0f - pos_y) * delta_y;
        float distance = 0.0f;
        int hit_edge = 3;
        int hit_axis = 0; /* 0 = vertical boundary, 1 = horizontal */

        for (int step = 0; step < 64; step++) {
            int next_x = map_x;
            int next_y = map_y;
            if (side_x < side_y) {
                distance = side_x;
                side_x += delta_x;
                next_x += step_x;
                hit_axis = 0;
            } else {
                distance = side_y;
                side_y += delta_y;
                next_y += step_y;
                hit_axis = 1;
            }

            hit_edge = edge_between(g, map_x, map_y, next_x, next_y);
            if (hit_edge != 3) break;
            map_x = next_x;
            map_y = next_y;
            if (map_x < 0 || map_x >= MAP_W ||
                map_y < 0 || map_y >= MAP_H) {
                hit_edge = 0;
                break;
            }
        }

        if (hit_edge == 3 || distance <= 0.0001f) continue;
        if (wall_depth) wall_depth[column] = distance;
        int door = hit_edge == 1;
        const u8 *tex = g->wall_texture[door ? 0 : 1];
        if (!tex) tex = fallback;

        float along = hit_axis == 0 ?
            pos_y + ray_y * distance : pos_x + ray_x * distance;
        along -= floorf(along);
        /* Keep the bitmap facing consistently as the same boundary is viewed
         * from its opposite side.  Door arches are symmetric, but handles and
         * wall cracks are not. */
        if ((hit_axis == 0 && ray_x > 0.0f) ||
            (hit_axis == 1 && ray_y < 0.0f))
            along = 1.0f - along;
        int tx = (int)(along * 256.0f);
        if (!door && g->wall_texture_offset)
            tx = (tx + g->wall_texture_offset) & 255;
        if (tx < 0) tx = 0;
        if (tx > 255) tx = 255;

        int line_h = (int)((float)vh / (2.0f * distance) + 0.5f);
        if (line_h < 1) line_h = 1;
        int unclipped_top = horizon - line_h / 2;
        int unclipped_bottom = unclipped_top + line_h - 1;
        int top = unclipped_top < vy ? vy : unclipped_top;
        int bottom = unclipped_bottom >= vy + vh ? vy + vh - 1 : unclipped_bottom;
        int sx = vx + column;
        for (int y = top; y <= bottom; y++) {
            int ty = (y - unclipped_top) * 200 / line_h;
            if (ty < 0) ty = 0;
            if (ty > 199) ty = 199;
            v->pixels[y * LOGICAL_W + sx] =
                remap_wall_texel(v, sample_wall_texel(tex, tx, ty), door,
                                 sx, y);
        }
    }
    v->dirty = 1;
}

static void draw_front_wall(Video *v, const u8 *tex, ProjRect p,
                            int vx, int vy, int vw, int vh, int door) {
    WallVertex a = {(float)p.left,  (float)p.top,    0,   0};
    WallVertex b = {(float)p.right, (float)p.top,  255,   0};
    WallVertex c = {(float)p.right, (float)p.bottom,255, 199};
    WallVertex d = {(float)p.left,  (float)p.bottom,  0, 199};
    textured_quad(v, tex, a, b, c, d, vx, vy, vw, vh, door);
}

static void draw_side_wall(Video *v, const u8 *tex, ProjRect outer,
                           ProjRect inner, int right_side,
                           int vx, int vy, int vw, int vh, int door) {
    WallVertex a, b, c, d;
    if (!right_side) {
        a = (WallVertex){(float)outer.left, (float)outer.top, 0, 0};
        b = (WallVertex){(float)inner.left, (float)inner.top, 255, 0};
        c = (WallVertex){(float)inner.left, (float)inner.bottom, 255, 199};
        d = (WallVertex){(float)outer.left, (float)outer.bottom, 0, 199};
    } else {
        a = (WallVertex){(float)inner.right, (float)inner.top, 0, 0};
        b = (WallVertex){(float)outer.right, (float)outer.top, 255, 0};
        c = (WallVertex){(float)outer.right, (float)outer.bottom, 255, 199};
        d = (WallVertex){(float)inner.right, (float)inner.bottom, 0, 199};
    }
    textured_quad(v, tex, a, b, c, d, vx, vy, vw, vh, door);
}

/* Paint the opening before the original ladder sprite.  Up ladders disappear
 * into a ceiling opening at the top of their projection; down ladders widen
 * into a floor opening at the bottom. */
static void draw_ladder_hole(Game *g, ProjRect p, int delta, float depth,
                             int vx, int vy, int vw, int vh,
                             const float *wall_depth) {
    int pw = p.right - p.left + 1;
    int ph = p.bottom - p.top + 1;
    if (pw < 4 || ph < 4) return;
    int cx = (p.left + p.right) / 2;
    int y0, y1, half0, half1;
    if (delta < 0) {
        y0 = p.top;
        y1 = p.top + ph / 4;
        half0 = pw / 5;
        half1 = pw * 2 / 5;
    } else {
        y0 = p.bottom - ph / 3;
        y1 = p.bottom;
        half0 = pw / 5;
        half1 = pw * 2 / 5;
    }
    if (y1 <= y0) y1 = y0 + 1;

    for (int y = y0; y <= y1; y++) {
        int half = half0 + (half1 - half0) * (y - y0) / (y1 - y0);
        for (int x = cx - half; x <= cx + half; x++) {
            if (x < vx || x >= vx + vw || y < vy || y >= vy + vh) continue;
            if (wall_depth && depth >= wall_depth[x - vx] + 0.02f) continue;
            g->video.pixels[y * LOGICAL_W + x] = 0;
        }
    }
    g->video.dirty = 1;
}

/* Draw a WORLD.PIC actor in perspective while respecting the wall depth for
 * every viewport column.  WORLD.PIC index 0 is the original ladder artwork;
 * monster indices are mapped in combat.  Trapdoors are discovered by text at
 * the party's position in the original and have no first-person marker. */
static void draw_pic_billboard_sized(Game *g, int pic_index, int cx, int top,
                                     int draw_w, int draw_h, float depth,
                                     int vx, int vy, int vw, int vh,
                                     const float *wall_depth,
                                     int replace_color, int tint) {
    if (pic_index < 0 || pic_index >= g->world_pic_count ||
        draw_w < 2 || draw_h < 2)
        return;
    const u8 *pic = g->world_pic_data[pic_index];
    int pic_size = g->world_pic_sizes[pic_index];
    if (!pic || pic_size < 0x192) return;
    const int rows_count = 200, table_size = 0x190;
    const u8 *data = pic + table_size;
    int data_len = pic_size - table_size;
    int left = cx - draw_w / 2;

    for (int row = 0; row < rows_count; row++) {
        int start = pic[row * 2] | (pic[row * 2 + 1] << 8);
        int end = data_len;
        for (int next = row + 1; next < rows_count; next++) {
            int off = pic[next * 2] | (pic[next * 2 + 1] << 8);
            if (off != start) { end = off; break; }
        }
        if (start < 0 || start >= data_len || start == end) continue;
        int sy0 = top + row * draw_h / rows_count;
        int sy1 = top + (row + 1) * draw_h / rows_count;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        int ptr = start;
        int xpos = data[ptr++];
        while (ptr < end && ptr < data_len) {
            int cmd = data[ptr++], run, color;
            if (cmd >= 0x20) { run = cmd >> 5; color = cmd & 0x1F; }
            else {
                color = cmd;
                if (ptr >= data_len) break;
                run = data[ptr++];
                if (!run) run = 255;
            }
            color = combat_remap_monster_color(color, replace_color, tint);
            if (color != 0 && color != 16 && color != 32) {
                int sx0 = left + xpos * draw_w / 256;
                int sx1 = left + (xpos + run) * draw_w / 256;
                if (sx1 <= sx0) sx1 = sx0 + 1;
                for (int sy = sy0; sy < sy1; sy++) {
                    if (sy < vy || sy >= vy + vh) continue;
                    for (int sx = sx0; sx < sx1; sx++) {
                        if (sx < vx || sx >= vx + vw) continue;
                        if (wall_depth && depth >= wall_depth[sx - vx] + 0.02f)
                            continue;
                        g->video.pixels[sy * LOGICAL_W + sx] = (u8)color;
                    }
                }
            }
            xpos += run;
        }
    }
    g->video.dirty = 1;
}

static void draw_pic_billboard(Game *g, int pic_index, int cx, int top,
                               int draw_h, float depth,
                               int vx, int vy, int vw, int vh,
                               const float *wall_depth, int replace_color,
                               int tint) {
    draw_pic_billboard_sized(g, pic_index, cx, top, draw_h * 3 / 4, draw_h,
                             depth, vx, vy, vw, vh, wall_depth,
                             replace_color, tint);
}

typedef struct ViewActor {
    float depth, lateral;
    int pic;
    int kind;       /* 0 monster, 1 up ladder/shop, 2 down */
    int color;      /* original replacement color for shared monster art */
    int tint;       /* native full-palette family tint for deep variants */
} ViewActor;

/* The depth buffer clips a sprite against the visible door bitmap, but a
 * wide billboard could otherwise leak around the door's outer columns.
 * Reject the actor as a whole when the centre-to-centre sightline crosses
 * any non-open edge.  Doors (1) are intentionally as opaque as stone here. */
static int actor_cell_visible(Game *g, int target_x, int target_y) {
    int map_x = g->cur_x, map_y = g->cur_y;
    int ray_x = target_x - map_x, ray_y = target_y - map_y;
    if (ray_x == 0 && ray_y == 0) return 0;
    int step_x = ray_x < 0 ? -1 : 1;
    int step_y = ray_y < 0 ? -1 : 1;
    float delta_x = ray_x == 0 ? 1.0e30f : 1.0f / fabsf((float)ray_x);
    float delta_y = ray_y == 0 ? 1.0e30f : 1.0f / fabsf((float)ray_y);
    float side_x = delta_x * 0.5f;
    float side_y = delta_y * 0.5f;

    while (map_x != target_x || map_y != target_y) {
        if (side_x + 0.00001f < side_y) {
            int next_x = map_x + step_x;
            if (edge_between_cells(g, map_x, map_y, next_x, map_y) != 3)
                return 0;
            map_x = next_x;
            side_x += delta_x;
        } else if (side_y + 0.00001f < side_x) {
            int next_y = map_y + step_y;
            if (edge_between_cells(g, map_x, map_y, map_x, next_y) != 3)
                return 0;
            map_y = next_y;
            side_y += delta_y;
        } else {
            int next_x = map_x + step_x;
            int next_y = map_y + step_y;
            if (edge_between_cells(g, map_x, map_y, next_x, map_y) != 3 ||
                edge_between_cells(g, map_x, map_y, map_x, next_y) != 3)
                return 0;
            map_x = next_x;
            map_y = next_y;
            side_x += delta_x;
            side_y += delta_y;
        }
    }
    return 1;
}

static void draw_view_actors(Game *g, int vx, int vy, int vw, int vh, int dir,
                             const float *wall_depth) {
    int fdx, fdy, rdx, rdy;
    dir_to_delta(dir, &fdx, &fdy);
    dir_to_right(dir, &rdx, &rdy);
    ViewActor actors[MONSTERS_PER_FLOOR + 64];
    int count = 0;

    if (g->monster_map_loaded && g->monster_layer >= 0 && g->cur_floor > 0) {
        MonsterRecord *map = g->monster_map[g->monster_layer];
        for (int i = 0; i < MONSTERS_PER_FLOOR && count < (int)(sizeof(actors)/sizeof(actors[0])); i++) {
            if (!monster_record_alive(g, &map[i])) continue;
            if (!actor_cell_visible(g, map[i].x, map[i].y)) continue;
            float dx = (float)map[i].x - (float)g->cur_x;
            float dy = (float)map[i].y - (float)g->cur_y;
            float forward = dx * fdx + dy * fdy;
            float side = dx * rdx + dy * rdy;
            if (forward < 0.35f || forward > 12.0f || fabsf(side) > forward * 1.05f)
                continue;
            int pic = get_monster_pic_index_ext(map[i].type);
            if (pic < 2) pic = 2; /* keep rare text-only DOS types visible */
            actors[count++] = (ViewActor){forward, side, pic, 0,
                                           get_monster_color_ext(map[i].type),
                                           get_monster_tint_ext(map[i].type)};
        }
    }

    /* Ladders and shops use their original WORLD.PIC image.  Scan the visible
     * fan rather than just the centre line, so ladders at a junction appear
     * in the correct side of the viewport. */
    for (int y = g->cur_y - 7; y <= g->cur_y + 7; y++) {
        for (int x = g->cur_x - 7; x <= g->cur_x + 7; x++) {
            if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H ||
                (x == g->cur_x && y == g->cur_y)) continue;
            float dx = (float)x - (float)g->cur_x;
            float dy = (float)y - (float)g->cur_y;
            float forward = dx * fdx + dy * fdy;
            float side = dx * rdx + dy * rdy;
            if (forward < 0.35f || forward > 7.0f || fabsf(side) > forward)
                continue;
            int ladder = ladder_delta(g, x, y);
            int shop = game_shop_type(g, x, y);
            if (shop && count < (int)(sizeof(actors)/sizeof(actors[0])))
                /* Town locations look exactly like ordinary ladders up in
                 * the viewports; their type color belongs only on the map. */
                actors[count++] = (ViewActor){forward, side, 0, 1, -1, 0};
            else if (ladder && count < (int)(sizeof(actors)/sizeof(actors[0])))
                actors[count++] = (ViewActor){forward, side, 0,
                                               ladder < 0 ? 1 : 2, -1, 0};
        }
    }

    for (int a = 0; a < count; a++)
        for (int b = a + 1; b < count; b++)
            if (actors[a].depth < actors[b].depth) {
                ViewActor t = actors[a]; actors[a] = actors[b]; actors[b] = t;
            }

    int horizon = vy + (vh - 1) / 2;
    for (int i = 0; i < count; i++) {
        ViewActor *a = &actors[i];
        int cx = vx + vw / 2 + (int)(a->lateral / a->depth * (vw / 2));
        int wall_h = (int)((float)vh / (2.0f * a->depth));
        int bottom = horizon + wall_h / 2;
        int height;
        if (a->kind == 0) height = wall_h * 9 / 10;
        else height = wall_h * 4 / 5;
        if (height < 6) height = 6;
        int top = bottom - height;
        if (a->kind == 1)
            top = horizon - wall_h * 3 / 5;
        else if (a->kind == 2)
            top = horizon - wall_h / 5;
        int depth_col = cx - vx;
        if (depth_col < 0) depth_col = 0;
        if (depth_col >= vw) depth_col = vw - 1;
        int feature_center_visible =
            a->depth < wall_depth[depth_col] + 0.02f;
        if (a->kind == 0) {
            draw_pic_billboard(g, a->pic, cx, top, height, a->depth,
                               vx, vy, vw, vh, wall_depth, a->color, a->tint);
        } else if (feature_center_visible) {
            ProjRect ladder_box = {
                .left = cx - height * 3 / 8,
                .right = cx + height * 3 / 8,
                .top = top,
                .bottom = top + height - 1
            };
            int delta = a->kind == 1 ? -1 : 1;
            draw_ladder_hole(g, ladder_box, delta, a->depth,
                             vx, vy, vw, vh, wall_depth);
            draw_pic_billboard(g, 0, cx, top, height, a->depth,
                               vx, vy, vw, vh, wall_depth, -1, 0);
        }
    }

    /* Coordinate features at the party's exact position are deliberately
     * absent from every viewport.  WORLD only draws them while approaching;
     * once the party steps onto one, the map and command/status text carry
     * that information instead. */
}

static void draw_low_color_corridor_edges(Video *v, int vx, int vy,
                                          int vw, int vh, u8 color) {
    for (int depth = 0; depth < 7; depth++) {
        ProjRect a = projection_rect(vx, vy, vw, vh, depth);
        ProjRect b = projection_rect(vx, vy, vw, vh, depth + 1);
        dungeon_line(v, a.left, a.top, b.left, b.top,
                     color, vx, vy, vw, vh);
        dungeon_line(v, a.right, a.top, b.right, b.top,
                     color, vx, vy, vw, vh);
        dungeon_line(v, a.left, a.bottom, b.left, b.bottom,
                     color, vx, vy, vw, vh);
        dungeon_line(v, a.right, a.bottom, b.right, b.bottom,
                     color, vx, vy, vw, vh);
    }
}

static void draw_3d_viewport(Game *g, int vx, int vy, int vw, int vh, int dir) {
    Video *v = &g->video;

    draw_dungeon_gradient(v, vx, vy, vw, vh);

    /* The ray pass draws front faces, side faces, corners and door planes as
     * one connected scene. */
    float wall_depth[LOGICAL_W];
    draw_ray_walls(g, vx, vy, vw, vh, dir, wall_depth);

    const MwDisplayModeInfo *mode =
        video_display_mode_info(v->display_mode);
    if (mode && mode->world_mode == 1)
        draw_low_color_corridor_edges(v, vx, vy, vw, vh, 8);
    else if (mode && mode->world_mode == 0)
        draw_low_color_corridor_edges(v, vx, vy, vw, vh, 15);

    draw_view_actors(g, vx, vy, vw, vh, dir, wall_depth);

    int front_depth = -1;
    for (int depth = 0; depth < 7; depth++) {
        int edge = check_wall_ahead(g, g->cur_x, g->cur_y, dir, depth + 1);
        if (edge != 3) {
            front_depth = depth;
            break;
        }
    }

    (void)front_depth;
}

/* Four-viewport layout matching original game's func_0D74F.
 * Endpoints are the inclusive 1600x1200 design coordinates scaled exactly as
 * the 1024x768 driver did (x*1024/1600, y*768/1200).
 * Three size modes cycled by Z key (g_45C9 / view_mode). */

typedef struct {
    int x, y, w, h;
} ViewRect;

typedef struct {
    ViewRect north, west, south, east;
} ViewLayout;

#define SX(x) ((x) * LOGICAL_W / 1600)
#define SY(y) ((y) * LOGICAL_H / 1200)
#define DVR(x1,y1,x2,y2) {x1, y1, x2, y2}

typedef struct { int x1, y1, x2, y2; } DesignViewRect;
typedef struct {
    DesignViewRect north, west, south, east;
} DesignViewLayout;

static const DesignViewLayout view_layout_design[3] = {
    /* Mode 0: Full size */
    {
        DVR(0x2D3, 0x000, 0x484, 0x258), /* North */
        DVR(0x11B, 0x1AE, 0x2CD, 0x406), /* West */
        DVR(0x2D3, 0x25D, 0x484, 0x487), /* South */
        DVR(0x48A, 0x1AE, 0x63E, 0x406), /* East */
    },
    /* Mode 1: Medium */
    {
        DVR(0x2D3, 0x12C, 0x484, 0x258), /* North */
        DVR(0x11B, 0x1AE, 0x2CD, 0x2DA), /* West */
        DVR(0x2D3, 0x25D, 0x484, 0x389), /* South */
        DVR(0x48A, 0x1AE, 0x63E, 0x2DA), /* East */
    },
    /* Mode 2: Small */
    {
        DVR(0x340, 0x12C, 0x417, 0x258), /* North */
        DVR(0x260, 0x1AE, 0x33A, 0x2DA), /* West */
        DVR(0x340, 0x25D, 0x417, 0x384), /* South */
        DVR(0x41D, 0x1AE, 0x4F7, 0x2DA), /* East */
    },
};

static ViewRect game_view_rect(const Game *g, DesignViewRect design) {
    ViewRect result;
    int x2 = display_design_x(g, design.x2);
    int y2 = display_design_y(g, design.y2);
    result.x = display_design_x(g, design.x1);
    result.y = display_design_y(g, design.y1);
    result.w = x2 - result.x + 1;
    result.h = y2 - result.y + 1;
    if (result.w < 1) result.w = 1;
    if (result.h < 1) result.h = 1;
    return result;
}

static void game_view_layout(const Game *g, int mode, ViewLayout *layout) {
    mode %= 3;
    if (mode < 0) mode += 3;
    const DesignViewLayout *design = &view_layout_design[mode];
    layout->north = game_view_rect(g, design->north);
    layout->west  = game_view_rect(g, design->west);
    layout->south = game_view_rect(g, design->south);
    layout->east  = game_view_rect(g, design->east);
}

static void draw_4way_view(Game *g) {
    ViewLayout layout;
    const ViewLayout *vl = &layout;
    game_view_layout(g, g->view_mode, &layout);

    draw_3d_viewport(g, vl->north.x, vl->north.y, vl->north.w, vl->north.h, 0);
    draw_3d_viewport(g, vl->west.x,  vl->west.y,  vl->west.w,  vl->west.h,  2);
    draw_3d_viewport(g, vl->south.x, vl->south.y, vl->south.w, vl->south.h, 1);
    draw_3d_viewport(g, vl->east.x,  vl->east.y,  vl->east.w,  vl->east.h,  3);

}

/* MW_PORT: WORLD func_27112 command legend and clickable native equivalent. */
/* ── Drawing: Command menu (matches func_27112, top-right area) ── */

static void draw_command_menu(Game *g) {
    Video *v = &g->video;
    const int menu_x = SX(0x48C);
    int enhanced = game_dungeon_max_floor(g) > CLASSIC_DUNGEON_FLOOR;
    /* Enhanced needs one additional passive-effect row. Tightening both its
       pitch and glyph height slightly keeps Quit/Help above the east view;
       Classic retains the existing original two-page geometry. */
    const int spacing = SY(enhanced ? 33 : 35);
    const int xsn = 7, xsd = 6;
    const int ysn = enhanced ? 11 : 12, ysd = 17;
    int y = 0;

    video_draw_text_scaled_xy(v, menu_x, y, " RICKS   VIEW  ONEY", 8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "B             M    ", 4, xsn, xsd, ysn, ysd);
    y += spacing;
    video_draw_text_scaled_xy(v, menu_x, y, " EAPONS   IEW STATS", 8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "W        V         ", 4, xsn, xsd, ysn, ysd);
    y += spacing;
    video_draw_text_scaled_xy(v, menu_x, y, " OOM      AST SPELL", 8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "Z        C         ", 4, xsn, xsd, ysn, ysd);
    y += spacing;
    video_draw_text_scaled_xy(v, menu_x, y, "USE  TEM E PAND MAP", 8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "    I     X        ", 4, xsn, xsd, ysn, ysd);
    y += spacing;
    video_draw_text_scaled_xy(v, menu_x, y, " RMOR     OSE ITEM ", 8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "A        L         ", 4, xsn, xsd, ysn, ysd);
    y += spacing;
    video_draw_text_scaled_xy(v, menu_x, y, " IGHT     OCKETS   ", 8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "F        P         ", 4, xsn, xsd, ysn, ysd);
    y += spacing;
    video_draw_text_scaled_xy(v, menu_x, y, "WAI       XP NEEDED", 8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "   T     E         ", 4, xsn, xsd, ysn, ysd);
    y += spacing;
    video_draw_text_scaled_xy(v, menu_x, y,
                              g->sound_enabled ? "TURN SOUND  N      "
                                               : "TURN SOUND  FF     ",
                              8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "           O       ", 4, xsn, xsd, ysn, ysd);
    y += spacing;
    video_draw_text_scaled_xy(v, menu_x, y, "BEASTIARY( )  -STATS", 8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "          J  G      ", 4, xsn, xsd, ysn, ysd);
    y += spacing;
    video_draw_text_scaled_xy(v, menu_x, y, "SPELLS IN EFFECT  ", 8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "                 1", 4, xsn, xsd, ysn, ysd);
    y += spacing;
    video_draw_text_scaled_xy(v, menu_x, y, "SPELLS IN EFFECT  ", 8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "                 2", 4, xsn, xsd, ysn, ysd);
    y += spacing;
    if (enhanced) {
        video_draw_text_scaled_xy(v, menu_x, y, "SPELLS IN EFFECT  ", 8,
                                  xsn, xsd, ysn, ysd);
        video_draw_text_scaled_xy(v, menu_x, y, "                 3", 4,
                                  xsn, xsd, ysn, ysd);
        y += spacing;
    }
    video_draw_text_scaled_xy(v, menu_x, y, " UIT-SAVE  ELP (  )", 8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "Q         H     F1 ", 4, xsn, xsd, ysn, ysd);
}

/* Convert a click on the command legend into the exact same command byte as
 * its keyboard hotkey.  SDL performs the inverse logical-size transform so
 * this remains accurate for resized and letterboxed windows. */
int game_mouse_click_logical(Game *g, int *x, int *y) {
    int window_x = 0, window_y = 0;
    float logical_x = 0.0f, logical_y = 0.0f;
    if (!g || !g->video.renderer) return 0;
    input_last_mouse_click(&g->input, &window_x, &window_y);
    SDL_RenderWindowToLogical(g->video.renderer, window_x, window_y,
                              &logical_x, &logical_y);
    if (x) *x = (int)logical_x;
    if (y) *y = (int)logical_y;
    return logical_x >= 0.0f && logical_y >= 0.0f &&
           logical_x < LOGICAL_W && logical_y < LOGICAL_H;
}

int game_mouse_row(Game *g, int x0, int x1, int y0, int row_height,
                   int row_count) {
    int x, y;
    if (row_height <= 0 || row_count <= 0 ||
        !game_mouse_click_logical(g, &x, &y))
        return -1;
    if (x < x0 || x >= x1 || y < y0 || y >= y0 + row_height * row_count)
        return -1;
    return (y - y0) / row_height;
}

static int command_menu_click_key(Game *g, int window_x, int window_y) {
    static const int classic_command[12][2] = {
        {'b','m'}, {'w','v'}, {'z','c'}, {'i','x'},
        {'a','l'}, {'f','p'}, {'t','e'}, {'o','o'},
        {'j','g'}, {'1','1'}, {'2','2'}, {'q','h'}
    };
    static const int enhanced_command[13][2] = {
        {'b','m'}, {'w','v'}, {'z','c'}, {'i','x'},
        {'a','l'}, {'f','p'}, {'t','e'}, {'o','o'},
        {'j','g'}, {'1','1'}, {'2','2'}, {'3','3'}, {'q','h'}
    };
    int enhanced = game_dungeon_max_floor(g) > CLASSIC_DUNGEON_FLOOR;
    int row_count = enhanced ? 13 : 12;
    float logical_x, logical_y;
    SDL_RenderWindowToLogical(g->video.renderer, window_x, window_y,
                              &logical_x, &logical_y);
    const int menu_x = SX(0x48C);
    const int spacing = SY(enhanced ? 33 : 35);
    int adv = g->video.font_advance ? g->video.font_advance
                                    : g->video.font_char_w;
    int scaled_advance = adv * 7 / 6;
    int scaled_height = g->video.font_char_h * (enhanced ? 11 : 12) / 17;
    int menu_right = menu_x + scaled_advance * 20;
    if (logical_x < menu_x || logical_x >= menu_right ||
        logical_y < 0 ||
        logical_y >= spacing * (row_count - 1) + scaled_height)
        return 0;
    int row = (int)logical_y / spacing;
    if (row >= row_count) row = row_count - 1;
    int right_column = logical_x >= menu_x + scaled_advance * 9;
    return enhanced ? enhanced_command[row][right_column] :
                      classic_command[row][right_column];
}

static int mouse_position_logical(Game *g, int *x, int *y, unsigned *serial) {
    int wx, wy;
    float lx, ly;
    input_mouse_position(&g->input, &wx, &wy, serial);
    if (!g->video.renderer) return 0;
    SDL_RenderWindowToLogical(g->video.renderer, wx, wy, &lx, &ly);
    if (x) *x = (int)lx;
    if (y) *y = (int)ly;
    return lx >= 0 && ly >= 0 && lx < LOGICAL_W && ly < LOGICAL_H;
}

static void hover_frame(Video *v, int x, int y, int w, int h, u8 color) {
    video_hline(v, x, y, w, color);
    video_hline(v, x, y + h - 1, w, color);
    video_vline(v, x, y, h, color);
    video_vline(v, x + w - 1, y, h, color);
}

/* INT 33h mode continuously moved a software cursor through the command and
 * view hit regions.  A thin yellow frame supplies the corresponding native
 * hover feedback without covering the original artwork. */
static void draw_mouse_hover(Game *g) {
    int x, y;
    if (!mouse_position_logical(g, &x, &y, NULL)) return;
    int enhanced = game_dungeon_max_floor(g) > CLASSIC_DUNGEON_FLOOR;
    int row_count = enhanced ? 13 : 12;
    const int menu_x = SX(0x48C);
    const int spacing = SY(enhanced ? 33 : 35);
    int adv = g->video.font_advance ? g->video.font_advance : g->video.font_char_w;
    int half = adv * 7 / 6 * 9;
    if (x >= menu_x && x < menu_x + half * 2 &&
        y >= 0 && y < spacing * row_count) {
        int row = y / spacing;
        int column = x >= menu_x + half;
        hover_frame(&g->video, menu_x + column * half, row * spacing,
                    half, spacing, 4);
        return;
    }
    ViewLayout layout;
    const ViewLayout *vl = &layout;
    game_view_layout(g, g->view_mode, &layout);
    const ViewRect *rects[4] = {&vl->north, &vl->south, &vl->west, &vl->east};
    for (int i = 0; i < 4; i++)
        if (x >= rects[i]->x && x < rects[i]->x + rects[i]->w &&
            y >= rects[i]->y && y < rects[i]->y + rects[i]->h) {
            hover_frame(&g->video, rects[i]->x, rects[i]->y,
                        rects[i]->w, rects[i]->h, 4);
            return;
        }
    if (x >= SX(0x2D3) && x < SX(0x48A) && y > SY(0x487))
        hover_frame(&g->video, SX(0x2D3), SY(0x487),
                    SX(0x48A) - SX(0x2D3), LOGICAL_H - SY(0x487), 4);
}

int game_mouse_command_key(Game *g) {
    int window_x, window_y;
    input_last_mouse_click(&g->input, &window_x, &window_y);
    return command_menu_click_key(g, window_x, window_y);
}

/* WORLD func_0D74F/func_0F6E5: the four rendered views are also the four
 * absolute-direction mouse controls.  Keep their small original gaps so a
 * click on a border cannot accidentally spend a turn. */
int game_mouse_view_direction(Game *g) {
    int x, y;
    ViewLayout layout;
    const ViewLayout *vl = &layout;
    const ViewRect *rects[4];
    static const int direction[4] = {0, 1, 2, 3};
    if (!game_mouse_click_logical(g, &x, &y)) return -1;
    game_view_layout(g, g->view_mode, &layout);
    rects[0] = &vl->north;
    rects[1] = &vl->south;
    rects[2] = &vl->west;
    rects[3] = &vl->east;
    for (int i = 0; i < 4; i++)
        if (x >= rects[i]->x && x < rects[i]->x + rects[i]->w &&
            y >= rects[i]->y && y < rects[i]->y + rects[i]->h)
            return direction[i];
    return -1;
}

/* MW_PORT: status portion of WORLD func_0F5CD/func_0F6E5. */
/* ── Drawing: Status bar (matches original format) ── */

static void draw_status_bar(Game *g, Character *player) {
    Video *v = &g->video;
    const int xsn = 7, xsd = 6;
    const int ysn = 12, ysd = 17;
    const int line_step = SY(53);
    const int bar_y = LOGICAL_H - line_step * 3;
    const int left_w = SX(0x2D3);
    const int right_x = SX(0x48A);
    const int south_bottom = SY(0x487) + 1;

    video_fill_rect(v, 0, bar_y, left_w, LOGICAL_H - bar_y, 0);
    video_fill_rect(v, right_x, bar_y, LOGICAL_W - right_x, LOGICAL_H - bar_y, 0);
    video_fill_rect(v, left_w, south_bottom, right_x - left_w,
                    LOGICAL_H - south_bottom, 0);

    char line[128];
    int y = bar_y;

    /* WORLD's leading L is the character level, not the dungeon depth.
     * Dungeon depth is deliberately reported by Detect Level and the
     * expanded-map/stat views instead of replacing the player's level here. */
    snprintf(line, sizeof(line), "L:%u  X:%d  Y:%d",
             (unsigned)player->level, g->cur_x, g->cur_y);
    video_draw_text_scaled_xy(v, 0, y, line, 6, xsn, xsd, ysn, ysd);
    snprintf(line, sizeof(line), "STR: %d  CON: %d",
             player->stat_str, player->stat_con);
    video_draw_text_scaled_xy(v, right_x + 7, y, line,
                              MW_COLOR_STATUS_CYAN,
                              xsn, xsd, ysn, ysd);
    y += line_step;

    snprintf(line, sizeof(line), "SPELL POINTS: %.0f OF %.0f",
             player->sp_cur, player->sp_max);
    video_draw_text_scaled_xy(v, 0, y, line, 6, xsn, xsd, ysn, ysd);
    snprintf(line, sizeof(line), "INT: %d  DEX: %d",
             player->stat_int, player->stat_agi);
    video_draw_text_scaled_xy(v, right_x + 7, y, line,
                              MW_COLOR_STATUS_CYAN,
                              xsn, xsd, ysn, ysd);
    y += line_step;

    snprintf(line, sizeof(line), "HEALTH POINTS: %u OF %u",
             mw_hp_cur(player), mw_hp_max(player));
    video_draw_text_scaled_xy(v, 0, y, line, 6, xsn, xsd, ysn, ysd);
    char action_line[64];
    const char *action = g->cur_floor <=
                         game_traversal_rules(g)->dig_max_floor ?
                         "HIT 'D' TO DIG A HOLE" :
                         "SOLID ROCK - USE A LADDER";
    int ladder = ladder_delta(g, g->cur_x, g->cur_y);
    int trap = game_trapdoor_floor(g, g->cur_x, g->cur_y);
    int shop = game_shop_type(g, g->cur_x, g->cur_y);
    if (shop) {
        static const char *const location_names[6] = {
            "", "STORE", "TEMPLE", "BANK", "INN", "WILDERNESS EXIT"
        };
        snprintf(action_line, sizeof(action_line), "HIT 'U' FOR %s",
                 location_names[shop]);
        action = action_line;
    }
    else if (ladder < 0) action = "HIT 'U' TO GO UP";
    else if (ladder > 0) action = "HIT 'D' TO GO DOWN";
    else if (trap >= 0) {
        int key_index = trap / 10;
        if (key_index > 0 && key_index < 18 &&
            player->trapdoor_keys[key_index])
            action = "HIT 'K' TO USE TRAP DOOR";
        else {
            snprintf(action_line, sizeof(action_line),
                     "TRAP DOOR NEEDS KEY %d", trap);
            action = action_line;
        }
    }
    video_draw_text_scaled_xy(v, SX(0x2D8), south_bottom,
                              action, MW_COLOR_PROMPT_ORANGE,
                              13, 12, ysn, ysd);
    snprintf(line, sizeof(line), "WIZ: %d  LUCK: %d",
             player->stat_wis, player->stat_luck);
    video_draw_text_scaled_xy(v, right_x + 7, y, line,
                              MW_COLOR_STATUS_CYAN,
                              xsn, xsd, ysn, ysd);
}

/* ── Weapon/armor name tables (from DS:0x1C0 and DS:0x214) ── */

static const char *weapon_names[] = {
    "FIST", "STICK", "CLUB", "MACE", "KNIFE",
    "SHORTSWORD", "LONG SWORD", "GREAT SWORD",
    "POWER WEAPON 1", "POWER WEAPON 2", "POWER WEAPON 3", "POWER WEAPON 4",
    "WORLDFORGED BLADE", "RIFTCARVER", "STARFORGED SABER", "VOIDREAVER",
    "ETERNITY EDGE", "CELESTIAL BRAND", "ASCENDANT EDGE", "MORAFF'S LEGACY"
};
#define WEAPON_COUNT 20

static const char *armor_names[] = {
    "SKIN", "LEATHER", "CHAIN", "SCALE", "PLATE",
    "FIELD PLATE", "TITANIUM", "OGRE",
    "PRISMATIC MAIL", "RIFTWARD PLATE", "STARFORGED MAIL", "VOID BASTION",
    "ETERNITY PLATE", "CELESTIAL AEGIS", "ASCENDANT AEGIS",
    "MORAFF'S BULWARK"
};
#define ARMOR_COUNT 16

typedef struct EnhancedRelicDef {
    const char *name;
    const char *effect;
    int minimum_floor;
} EnhancedRelicDef;

static const EnhancedRelicDef enhanced_relics[MW_RELIC_COUNT] = {
    {"RING OF ARCANE RENEWAL",
     "RESTORES 1 SPELL POINT EVERY 4 PLAYER ACTIONS.", 350},
    {"BLOODSTONE SIGNET",
     "MELEE DAMAGE RESTORES 5 PERCENT HEALTH, WITH A LEVEL-BASED CAP.", 475},
    {"DEEPWARD AMULET",
     "REDUCES MONSTER DAMAGE 15 PERCENT AND DOUBLES TIME BETWEEN STATUS DRAINS.", 600},
    {"SAGE'S PRISM",
     "INCREASES EXPERIENCE FROM EVERY MONSTER KILL BY 25 PERCENT.", 750},
    {"PHOENIX SEAL",
     "SURVIVES ONE LETHAL MONSTER STRIKE; RECHARGES AFTER 300 ACTIONS.", 900}
};

unsigned long long game_loaded_weight(const Character *p) {
    /* The destructive max-character shortcut deliberately grants a
       permanent Feather effect alongside full inventories.  Treat that
       combination as its zero-load sentinel so the shortcut does not turn
       its own inventory grant into maximum encumbrance. */
    if (mw_universal_access(p) && p->eff_feather == 100)
        return 0;

    unsigned long long weight = p->eff_feather ? 0u : p->weight_pounds;

    /* WORLD divides each loose-stone denomination separately.  Jewel stones
       and ordinary jewels are intentionally weightless. */
    weight += p->copper_stones / 16u;
    weight += p->silver_stones / 16u;
    weight += p->ivory_stones / 16u;
    weight += p->gold_stones / 16u;
    weight += p->platinum_stones / 16u;

    for (int i = 0; i < 8; i++)
        weight += (unsigned long long)mw_weapon_inventory_count(p, i) *
                  (unsigned)weapon_stats[i].weight;
    for (int i = 12; i < WEAPON_STAT_COUNT; i++)
        weight += (unsigned long long)mw_weapon_inventory_count(p, i) *
                  (unsigned)weapon_stats[i].weight;
    for (int i = 0; i < ARMOR_STAT_COUNT; i++)
        weight += (unsigned long long)mw_armor_inventory_count(p, i) *
                  (unsigned)combat_armor_weight(i);
    return weight;
}

int game_weight_monster_turns(const Character *p) {
    unsigned long long loaded = game_loaded_weight(p);
    unsigned long long agility = (unsigned long long)p->stat_agi * 10u;
    unsigned long long excess = loaded + 100u > agility ?
                                loaded + 100u - agility : 0u;
    unsigned long long turns = excess / 100u + 1u;

    /* Original saves normally keep this small.  Saturating protects edited
       and trainer-maxed saves from turning one keypress into millions of
       iterations while retaining the full practical encumbrance range. */
    return turns > 8u ? 8 : (int)turns;
}

/* MW_PORT: WORLD func_0DF4A (statistics), func_0E8C8 (carried weight), and
 * associated 0x0DBA5/0x0DBE8 display helpers. */
/* ── Command: View Stats (func_0DF4A) ── */

static void cmd_view_stats(Game *g, Character *p) {
    Video *v = &g->video;
    char line[128];
    int y = 0;

    /* WORLD func_0DF4A uses the long-form left-column page: it replaces the
       map/status column but leaves all three right-hand viewport regions
       intact.  It never clears the complete framebuffer. */
    left_column_begin(g, p);

    snprintf(line, sizeof(line), "VIEW STATS FOR %s", p->name);
    y = left_column_text(g, y, line, 3);

    const char *race_str = (p->race < RACE_COUNT) ? race_names[p->race] : "???";
    snprintf(line, sizeof(line), "RACE: %s", race_str);
    y = left_column_text(g, y, line, 4);

    snprintf(line, sizeof(line), "SEX: %s", p->sex == 0 ? "MALE" : "FEMALE");
    y = left_column_text(g, y, line, 4);

    const char *cls = (p->class_id < CLASS_COUNT) ? class_names[p->class_id] : "???";
    snprintf(line, sizeof(line), "CLASS: %s", cls);
    y = left_column_text(g, y, line, 4);

    snprintf(line, sizeof(line), "MONEY IN POCKET: %u", p->jewels_pocket);
    y = left_column_text(g, y, line, 8);

    snprintf(line, sizeof(line), "MONEY IN BANK: %u", p->jewels_bank);
    y = left_column_text(g, y, line, 8);

    snprintf(line, sizeof(line), "TOTAL MONEY: %u",
             p->jewels_pocket + p->jewels_bank);
    y = left_column_text(g, y, line, 8);

    snprintf(line, sizeof(line), "LOADED WEIGHT: %llu",
             game_loaded_weight(p));
    y = left_column_text(g, y, line, 5);

    snprintf(line, sizeof(line), "NAKED WEIGHT: %u", p->weight_pounds);
    y = left_column_text(g, y, line, 5);

    snprintf(line, sizeof(line), "HEIGHT (INCHES): %u", p->height_inches);
    y = left_column_text(g, y, line, 5);

    snprintf(line, sizeof(line), "STRENGTH: %d", p->stat_str);
    y = left_column_text(g, y, line, 6);

    snprintf(line, sizeof(line), "INTELLIGENCE: %d", p->stat_int);
    y = left_column_text(g, y, line, 6);

    snprintf(line, sizeof(line), "WISDOM: %d", p->stat_wis);
    y = left_column_text(g, y, line, 6);

    snprintf(line, sizeof(line), "CONSTITUTION: %d", p->stat_con);
    y = left_column_text(g, y, line, 6);

    snprintf(line, sizeof(line), "AGILITY: %d", p->stat_agi);
    y = left_column_text(g, y, line, 6);

    snprintf(line, sizeof(line), "LUCK: %d", p->stat_luck);
    y = left_column_text(g, y, line, 6);

    const char *wpn = (p->equipped_weapon < WEAPON_COUNT) ?
        weapon_names[p->equipped_weapon] : "UNKNOWN";
    snprintf(line, sizeof(line), "WEAPON IN HAND: %s", wpn);
    y = left_column_text(g, y, line, 4);

    const char *arm = (p->equipped_armor < ARMOR_COUNT) ?
        armor_names[p->equipped_armor] : "UNKNOWN";
    snprintf(line, sizeof(line), "CURRENT ARMOR: %s", arm);
    y = left_column_text(g, y, line, 4);

    snprintf(line, sizeof(line), "LEVEL: %d", p->level);
    y = left_column_text(g, y, line, 5);

    snprintf(line, sizeof(line), "EXPERIENCE: %.0f", p->experience);
    y = left_column_text(g, y, line, 5);

    if (mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED) {
        snprintf(line, sizeof(line), "DEEP RELICS FOUND: %d OF %d",
                 mw_relic_count(p), MW_RELIC_COUNT);
        y = left_column_text(g, y, line, 11);
    }

    y = left_column_text(g, y,
                         p->raise_x == 0xFFFFu ?
                         "NO RAISE DEAD CONTRACT IS IN EFFECT" :
                         "RAISE DEAD CONTRACT IS IN EFFECT",
                         p->raise_x == 0xFFFFu ? 8 : 3);

    if (p->diseased_turns > 0) {
        snprintf(line, sizeof(line), "YOU ARE DISEASED-MOVES LEFT UNTIL CONSTITUTION DRAINED: %d",
                 p->diseased_turns);
        y = left_column_text(g, y, line, 8);
    }
    if (p->poisoned_turns > 0) {
        snprintf(line, sizeof(line), "YOU ARE POISONED-MOVES LEFT UNTIL STRENGTH DRAINED: %d",
                 p->poisoned_turns);
        y = left_column_text(g, y, line, 6);
    }

    left_column_text(g, y, "HIT ANY KEY TO RETURN TO GAME...", 3);
    video_present(v);
    input_wait_any_key(&g->input);
}

/* MW_EXTENSION: G summarizes persistent and derived state that the original
 * save format keeps but none of WORLD's normal status pages aggregate. */
static int count_bits_u16(u16 value) {
    int count = 0;
    while (value) {
        value &= (u16)(value - 1);
        count++;
    }
    return count;
}

static void draw_game_stats(Game *g, Character *p) {
    Video *v = &g->video;
    char line[160];
    int discovered = 0;
    int quest_total = game_dungeon_max_floor(g) == CLASSIC_DUNGEON_FLOOR ?
                      8 : QUEST_CHAIN_COUNT;
    int quest_count = count_bits_u16(
        (u16)(mw_quest_flags(p) & ((1u << quest_total) - 1u)));
    int known_pits = 0, visited_cells = 0, alive_monsters = 0;
    int learned = 0, weapons = 0, armor = 0;
    unsigned long long kills = 0, scrolls = 0, wand_charges = 0, papers = 0;

    int bestiary_total = bestiary_mode_catalog_count(g);
    for (int i = 0; i < bestiary_total; i++) {
        int type = bestiary_type_at_mode_catalog_index(g, i);
        if (type >= 0 && g->bestiary_kills[type]) {
            discovered++;
            kills += g->bestiary_kills[type];
        }
    }
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            if (g->visited[y][x]) visited_cells++;
    for (int z = 0; z < PIT_GROUP_FLOORS; z++)
        for (int y = 0; y < MAP_H; y++)
            for (int b = 0; b < PIT_ROW_BYTES; b++)
                known_pits += count_bits_u16(g->pit_used[z][y][b]);
    if (g->monster_map_loaded && g->monster_layer >= 0)
        for (int i = 0; i < MONSTERS_PER_FLOOR; i++)
            if (monster_record_alive(g,
                    &g->monster_map[g->monster_layer][i]))
                alive_monsters++;
    int spell_catalog_count = mw_spell_catalog_count(p);
    for (int category = 0; category < 4; category++)
        for (int spell = 0; spell < spell_catalog_count; spell++) {
            if (p->spells[category][spell]) learned++;
            scrolls += p->scrolls[category][spell];
            wand_charges += p->wands[category][spell];
            papers += p->papers[category][spell];
        }
    for (int i = 0; i < 8; i++) {
        weapons += mw_weapon_inventory_count(p, i);
        armor += mw_armor_inventory_count(p, i);
    }
    for (int i = 12; i < WEAPON_STAT_COUNT; i++)
        weapons += mw_weapon_inventory_count(p, i);
    for (int i = 8; i < ARMOR_STAT_COUNT; i++)
        armor += mw_armor_inventory_count(p, i);

    video_clear(v, 0);
    snprintf(line, sizeof(line), "GAME STATS FOR %s", p->name);
    video_draw_text(v, 8, 5, line, 4);
    video_hline(v, 8, 36, LOGICAL_W - 16, 8);

    int left_y = 52, right_y = 52;
    const int row = v->font_char_h + 8;
#define GAME_STAT_LEFT(color, ...) do { \
    snprintf(line, sizeof(line), __VA_ARGS__); \
    video_draw_text_scaled(v, 12, left_y, line, color, 3, 4); left_y += row; \
} while (0)
#define GAME_STAT_RIGHT(color, ...) do { \
    snprintf(line, sizeof(line), __VA_ARGS__); \
    video_draw_text_scaled(v, 520, right_y, line, color, 3, 4); right_y += row; \
} while (0)
    GAME_STAT_LEFT(15, "SAVE SLOT: %d", g->active_save_slot);
    GAME_STAT_LEFT(15, "DUNGEON SEED: %d", g->dungeon_number);
    GAME_STAT_LEFT(14, "EXPERIENCE MODE: %s (FLOORS 0-%d)",
                   game_dungeon_max_floor(g) == CLASSIC_DUNGEON_FLOOR ?
                   "CLASSIC" : "ENHANCED", game_dungeon_max_floor(g));
    GAME_STAT_LEFT(7, "POSITION: FLOOR %d  X:%d Y:%d",
                   g->cur_floor, g->cur_x, g->cur_y);
    GAME_STAT_LEFT(7, "CHARACTER AGE: %u YEARS, %u DAYS",
                   p->age / MW_AGE_YEAR_UNITS,
                   (p->age % MW_AGE_YEAR_UNITS) / MW_AGE_DAY_UNITS);
    GAME_STAT_LEFT(7, "EXPERIENCE: %.0f", p->experience);
    GAME_STAT_LEFT(10, "BESTIARY DISCOVERED: %d / %d",
                   discovered, bestiary_total);
    GAME_STAT_LEFT(10, "TOTAL MONSTERS DEFEATED: %llu", kills);
    GAME_STAT_LEFT(10, "QUEST BOSSES DEFEATED: %d / %d",
                   quest_count, quest_total);
    int key_count = 0;
    for (int i = 1; i < 18; i++) if (p->trapdoor_keys[i]) key_count++;
    GAME_STAT_LEFT(12, "TRAPDOOR KEYS FOUND: %d / 17", key_count);
    GAME_STAT_LEFT(12, "KNOWN PITS IN FLOOR GROUP: %d", known_pits);
    GAME_STAT_LEFT(12, "CURRENT FLOOR CELLS SEEN: %d / %d",
                   visited_cells, MAP_W * MAP_H);
    GAME_STAT_LEFT(12, "LIVING MONSTERS ON FLOOR: %d", alive_monsters);

    GAME_STAT_RIGHT(3, "SPELLS LEARNED: %d / %d", learned,
                    spell_catalog_count * 4);
    GAME_STAT_RIGHT(3, "SCROLLS CARRIED: %llu", scrolls);
    GAME_STAT_RIGHT(3, "WAND CHARGES CARRIED: %llu", wand_charges);
    GAME_STAT_RIGHT(3, "MAGIC PAPERS CARRIED: %llu", papers);
    GAME_STAT_RIGHT(5, "WEAPON ITEMS OWNED: %d", weapons);
    GAME_STAT_RIGHT(5, "ARMOR ITEMS OWNED: %d", armor);
    GAME_STAT_RIGHT(8, "JEWEL PIECES TOTAL: %llu",
                    (unsigned long long)p->jewels_pocket + p->jewels_bank);
    GAME_STAT_RIGHT(8, "LOADED WEIGHT: %llu", game_loaded_weight(p));
    GAME_STAT_RIGHT(8, "MONSTER ACTIONS PER TURN: %d",
                    game_weight_monster_turns(p));
    if (mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED) {
        GAME_STAT_RIGHT(11, "SUPER-RARE RELICS: %d / %d",
                        mw_relic_count(p), MW_RELIC_COUNT);
        if (mw_relic_owned(p, MW_RELIC_PHOENIX_SEAL))
            GAME_STAT_RIGHT(14, "PHOENIX RECHARGE: %u ACTIONS",
                            p->native.relic_phoenix_cooldown);
    }
    GAME_STAT_RIGHT(g->cheat_noclip ? 4 : 7, "NOCLIP: %s",
                    g->cheat_noclip ? "ON" : "OFF");
    GAME_STAT_RIGHT(g->cheat_god_mode ? 4 : 7, "GOD MODE: %s",
                    g->cheat_god_mode ? "ON" : "OFF");
    GAME_STAT_RIGHT(g->cheat_open_floor ? 4 : 7, "OPEN FLOOR MODE: %s",
                    g->cheat_open_floor ? "ON" : "OFF");
#undef GAME_STAT_LEFT
#undef GAME_STAT_RIGHT
    video_draw_text_scaled(v, 255, LOGICAL_H - 35,
                           "HIT ANY KEY TO RETURN...", 15, 3, 4);
    video_present(v);
}

void game_draw_game_stats_test(Game *g, Character *p) {
    if (g && p) draw_game_stats(g, p);
}

static void cmd_game_stats(Game *g, Character *p) {
    draw_game_stats(g, p);
    input_wait_any_key(&g->input);
}

/* MW_PORT: WORLD shop_finances (0x0803D). */
/* ── Command: View Money (shop_finances) ── */

static void cmd_view_money(Game *g, Character *p) {
    Video *v = &g->video;
    char line[128];

    town_pane_begin(g, p);
    int y = 0;

    y = town_pane_text(g, y, "YOUR FINANCIAL STATEMENT:", 4);

    snprintf(line, sizeof(line), "COPPER STONES:   %10u", p->copper_stones);
    y = town_pane_text(g, y, line, 7);

    snprintf(line, sizeof(line), "SILVER STONES:   %10u", p->silver_stones);
    y = town_pane_text(g, y, line, 7);

    snprintf(line, sizeof(line), "IVORY STONES:    %10u", p->ivory_stones);
    y = town_pane_text(g, y, line, 7);

    snprintf(line, sizeof(line), "GOLD STONES:     %10u", p->gold_stones);
    y = town_pane_text(g, y, line, 7);

    snprintf(line, sizeof(line), "PLATINUM STONES: %10u", p->platinum_stones);
    y = town_pane_text(g, y, line, 7);

    snprintf(line, sizeof(line), "JEWEL STONES:    %10u", p->jewel_stones);
    y = town_pane_text(g, y, line, 7);

    snprintf(line, sizeof(line), "JEWELS IN POCKET:%10u", p->jewels_pocket);
    y = town_pane_text(g, y, line, 7);

    snprintf(line, sizeof(line), "JEWELS IN BANK:  %10u", p->jewels_bank);
    y = town_pane_text(g, y, line, 7);

    town_pane_text(g, y, "HIT ANY KEY...", 15);
    video_present(v);
    input_wait_any_key(&g->input);
}

/* ── Spell name tables (extracted from WORLD.EXE pointer tables) ── */
/* The first 30 entries are the exact original catalog.  Enhanced adds fifteen
 * deep-dungeon spells per family in the save format's previously unused
 * slots 30-44. */

static const char *perm_names[MW_ENHANCED_SPELL_COUNT] = {
    "ENCHANT WEAPON LEVEL 1","EXTRA HEALTH POINT","WRITE SCROLL TO LEVEL 3",
    "ENCHANT ARMOR LEVEL 1","EXTRA 3 HEALTH POINTS","ENCHANT WAND LEVEL 3",
    "ENCHANT WEAPON LEVEL 2","EXTRA 5 HEALTH POINTS","ENCHANT RING LEVEL 1",
    "ENCHANT ARMOR LEVEL 2","ANTI-MAGIC RING LEVEL 1","WRITE SCROLL - LEVEL 10",
    "ENCHANT WEAPON LEVEL 3","ENCHANT RING LEVEL 2","BODY ARMOR LEVEL 1",
    "ENCHANT ARMOR LEVEL 3","ANTI-MAGIC RING LEVEL 2","ENCHANT WAND LEVEL 8",
    "ENCHANT RING LEVEL 3","ANTI-MAGIC RING LEVEL 3","BODY ARMOR LEVEL 2",
    "ENCHANT WEAPON LEVEL 4","ENCHANT ARMOR LEVEL 4","ENCHANT WAND ANY LEVEL",
    "PERMANENT FEATHER","ANTI-MAGIC RING LEVEL 5","EXTRA 25 HEALTH POINTS",
    "PERMANENT INVISIBILITY","YOUTH","BODY ARMOR LEVEL 4",
    "ENCHANT WEAPON LEVEL 150","ENCHANT ARMOR LEVEL 100",
    "BODY ARMOR LEVEL 100","WRITE DEEP SCROLL","CHARGE DEEP WAND",
    "ENCHANT WEAPON LEVEL 500","ENCHANT ARMOR LEVEL 350",
    "BODY ARMOR LEVEL 300","WRITE ASCENDANT SCROLL",
    "CHARGE ASCENDANT WAND",
    "ENCHANT WEAPON LEVEL 1000","ENCHANT ARMOR LEVEL 750",
    "BODY ARMOR LEVEL 650","WRITE MYTHIC SCROLL",
    "CHARGE MYTHIC WAND",
};
static const char *prep_names[MW_ENHANCED_SPELL_COUNT] = {
    "ENCHANT ARMOR LEVEL 1","ENCHANT WEAPON LEVEL 1","LITTLE CURE",
    "ENCHANT WEAPON LEVEL 2","RELOCATE","DETECT LEVEL",
    "CURE","ENCHANT ARMOR LEVEL 2","STRENGTH",
    "ENCHANT WEAPON LEVEL 3","AGILITY","DESCEND",
    "ASCEND","DETECT POSITION","FEATHER",
    "BIG CURE","DOUBLE ASCEND","ENCHANT WEAPON LEVEL 4",
    "INVISIBILITY","ENCHANT ARMOR LEVEL 3","FAST MOVE",
    "SUPER STRENGTH","ENCHANT WEAPON LEVEL 5","MAJOR DESCEND",
    "SUPER AGILITY","CURE POISON","HEAL ALL WOUNDS",
    "MAJOR ASCEND","CURE DISEASE","ENCHANT ARMOR LEVEL 4",
    "ABYSS DESCEND","ABYSS ASCEND","DEEP SANCTUARY",
    "CARTOGRAPHER'S EYE","TOWN PORTAL",
    "RIFT DESCEND","RIFT ASCEND","ETERNAL SANCTUARY",
    "WORLD REVEAL","SOUL ANCHOR",
    "TITAN DESCEND","TITAN ASCEND","MYTHIC SANCTUARY",
    "ASTRAL FORM","PERFECT VITALITY",
};
static const char *wiz_names[MW_ENHANCED_SPELL_COUNT] = {
    "SLEEP","MAGIC ZAP","MINOR PROTECTION",
    "SLOW ENEMIES","STRENGTH","MINOR SHOCK",
    "LIGHTNING","MAGIC MISSLE","SPEED",
    "GO AWAY","RELOCATE","POWER WEAPON I",
    "MINOR EXPLOSION","PROTECTION","RESIST POISON",
    "MAGIC ZOT","SHOCK","ANTI-COLD",
    "EXPLOSION","PASS WALL","ANTI-FIRE",
    "MAGIC BOLT","MAJOR PROTECTION","POWER WEAPON II",
    "HOLD MONSTER","DRAIN MONSTER","MAJOR SHOCK",
    "MAJOR EXPLOSION","AUTOKILL","POWER WEAPON III",
    "ABYSSAL LANCE","TIME STOP","VOID NOVA","SOUL REND","OBLIVION",
    "STARFIRE","CHRONO LOCK","REALITY RUPTURE",
    "MANA TEMPEST","ANNIHILATION",
    "POWER WEAPON IV","COSMIC IMPLOSION","POWER WEAPON V",
    "END OF AGES","POWER WEAPON VI",
};
static const char *priest_names[MW_ENHANCED_SPELL_COUNT] = {
    "SLEEP","MINOR PROTECTION","STRENGTH",
    "RESIST POISON","SPEED","FAST CURE",
    "RESIST DISEASE","RELOCATE","SLOW ENEMIES",
    "ANTI-COLD","GO AWAY","POWER WEAPON I",
    "PROTECTION","ANTI-FIRE","PASS WALL",
    "RESIST LEVEL DRAIN","DRAIN MONSTER","FAST BIG CURE",
    "HOLD MONSTER","POWER WEAPON II","SHOCK",
    "MAJOR PROTECTION","EXPLOSION","MAGIC ZOT",
    "AUTOKILL","POWER WEAPON III","STRENGTH AND SPEED",
    "ULTRA PROTECTION","FAST HEAL","MAJOR SHOCK",
    "GREATER RESTORATION","DIVINE AEGIS","HOLY CATACLYSM",
    "CELESTIAL STASIS","FINAL JUDGMENT",
    "LIFE CONVERGENCE","ETERNAL WARD","WRATH OF HEAVEN",
    "PHOENIX PRAYER","DIVINE VERDICT",
    "POWER WEAPON IV","SERAPHIC REPRIEVE","POWER WEAPON V",
    "CREATION'S WRATH","POWER WEAPON VI",
};

static const char **spell_type_names[4] = { perm_names, prep_names, wiz_names, priest_names };
static const char *type_headers[4] = {
    "PERMANENT SPELLS", "PREPARATION SPELLS",
    "WIZARD BATTLE SPELLS", "PRIEST BATTLE SPELLS"
};

/* Color per level: preserve the native cycle through the Enhanced levels. */
static const u8 level_colors[15] = {
    6, 8, 3, 4, 5, 7, 6, 8, 3, 4, 5, 7, 6, 8, 3
};

/* ── Command: Pockets - Spell list display ── */
/* Classic keeps the original two pages. Enhanced adds two deep-spell pages. */
/* is_wand: if true, show charge count next to name instead of just color */

static void cmd_pockets_spells(Game *g, Character *p, u8 data[4][45], int is_wand, const char *title) {
    Video *v = &g->video;
    int fh = v->font_char_h * 3 / 4;
    char line[80];

    int pages = mw_spell_catalog_count(p) > MW_ORIGINAL_SPELL_COUNT ? 4 : 2;
    for (int page = 0; page < pages; page++) {
        int deep = page >= 2;
        int t0 = (page % 2) * 2;
        int t1 = t0 + 1;
        int first_spell = deep ? MW_DEEP_SPELL_FIRST : 0;
        int spell_count = deep ? MW_DEEP_SPELL_COUNT :
                                 MW_ORIGINAL_SPELL_COUNT;
        const char **names0 = spell_type_names[t0];
        const char **names1 = spell_type_names[t1];

        video_clear(v, 0);

        /* WORLD func_1E77B is the intentional full-display POCKETS VIEW.
           Casting or using these items instead uses a full-width top strip
           and retains the lower viewports/status.  These read-only 30-row
           tables use exact 1600x1200 source coordinates. */
        int y = 0;
        int row_h = SY(0x26);
        int col_level = SX(0x1E);
        int col_left = SX(0xB4);
        int col_right = SX(0x384);

        video_draw_text_scaled(v, col_level, y, "LEVEL", 4, 3, 4);
        if (deep) {
            snprintf(line, sizeof(line), "DEEP %s", type_headers[t0]);
            video_draw_text_scaled(v, col_left, y, line, 4, 3, 4);
            snprintf(line, sizeof(line), "DEEP %s", type_headers[t1]);
            video_draw_text_scaled(v, col_right, y, line, 4, 3, 4);
        } else {
            video_draw_text_scaled(v, col_left, y, type_headers[t0], 4, 3, 4);
            video_draw_text_scaled(v, col_right, y, type_headers[t1], 4, 3, 4);
        }
        y = SY(0x3C);

        for (int row = 0; row < spell_count; row++) {
            int i = first_spell + row;
            int lv = i / 3 + 1;
            u8 color = level_colors[lv - 1];

            snprintf(line, sizeof(line), "%2d", lv);
            video_draw_text_scaled(v, col_level, y, line, 10, 3, 4);

            u8 val0 = data[t0][i];
            if (val0) {
                if (is_wand)
                    snprintf(line, sizeof(line), "%s (%d)", names0[i], val0);
                else
                    snprintf(line, sizeof(line), "%s", names0[i]);
                video_draw_text_scaled(v, col_left, y, line, color, 3, 4);
            }

            u8 val1 = data[t1][i];
            if (val1) {
                if (is_wand)
                    snprintf(line, sizeof(line), "%s (%d)", names1[i], val1);
                else
                    snprintf(line, sizeof(line), "%s", names1[i]);
                video_draw_text_scaled(v, col_right, y, line, color, 3, 4);
            }

            y += row_h;
        }

        /* The original two Classic tables simply wait after the final row.
           Only the native Enhanced continuation needs an explicit page cue,
           placed in otherwise-unused space on its short deep-spell pages. */
        if (deep) {
            int cue_y = y + 8;
            if (cue_y < SY(0x1E0)) cue_y = SY(0x1E0);
            if (cue_y > LOGICAL_H - 28) cue_y = LOGICAL_H - 28;
            snprintf(line, sizeof(line), "%s PAGE %d/%d - HIT ANY KEY...",
                     title, page + 1, pages);
            video_draw_text_scaled(v, col_level, cue_y, line,
                                   15, 3, 4);
        }
        video_present(v);
        input_wait_any_key(&g->input);
    }
}

/* ── Command: Pockets - Misc Magic Items subpage (option 5) ── */

static void cmd_pockets_misc(Game *g, Character *p) {
    Video *v = &g->video;
    char line[128];

    /* The original long miscellaneous inventory replaces the left column,
       not the 3-D views and command/status area to its right. */
    left_column_begin(g, p);
    int y = 0;

    y = left_column_text(g, y, "MISC. MAGIC ITEMS:", 14);

    y = left_column_text(g, y, "HIT 'I' AND '5' TO USE THESE:", 14);
    snprintf(line, sizeof(line), "1) HOLY HAND GRENADES: %d", p->holy_grenade);
    y = left_column_text(g, y, line, 7);
    snprintf(line, sizeof(line), "2) STONES OF TELEPORTATION: %d", p->stone_teleport);
    y = left_column_text(g, y, line, 7);
    snprintf(line, sizeof(line), "3) STONES OF SEEING: %d", p->stone_see);
    y = left_column_text(g, y, line, 7);
    snprintf(line, sizeof(line), "4) FLOOR SLOSHERS: %d", p->floor_slosher);
    y = left_column_text(g, y, line, 7);
    snprintf(line, sizeof(line), "5) POTION OF HEALING: %d", p->potion_heal);
    y = left_column_text(g, y, line, 7);

    y = left_column_text(g, y, "HIT 'I' AND '4' TO USE THESE:", 14);
    snprintf(line, sizeof(line), "6) GREEN PILLS: %d", p->green_pill);
    y = left_column_text(g, y, line, 7);
    snprintf(line, sizeof(line), "7) ORANGE PILLS: %d", p->orange_pill);
    y = left_column_text(g, y, line, 7);
    snprintf(line, sizeof(line), "8) YELLOW PILLS: %d", p->yellow_pill);
    y = left_column_text(g, y, line, 7);
    snprintf(line, sizeof(line), "9) RED PILLS: %d", p->red_pill);
    y = left_column_text(g, y, line, 7);
    snprintf(line, sizeof(line), "10) BLUE PILLS: %d", p->blue_pill);
    y = left_column_text(g, y, line, 7);
    snprintf(line, sizeof(line), "11) WHITE PILLS: %d", p->white_pill);
    y = left_column_text(g, y, line, 7);

    y = left_column_text(g, y, "THESE ARE AUTOMATICALLY IN USE:", 14);
    snprintf(line, sizeof(line), "12) RINGS OF REGENERATION: %d", p->ring_regen);
    y = left_column_text(g, y, line, 7);
    snprintf(line, sizeof(line), "13) RING OF PROTECTION, PLUS %d", mw_ring_prot_plus(p));
    y = left_column_text(g, y, line, 7);
    snprintf(line, sizeof(line), "14) ANTI MAGIC RING, PLUS %d", p->antimagic_ring);
    y = left_column_text(g, y, line, 7);
    snprintf(line, sizeof(line), "15) BODY ARMOR, LEVEL %d", mw_body_armor_plus(p));
    y = left_column_text(g, y, line, 7);
    snprintf(line, sizeof(line), "16) GAUNTLET, PLUS %d", mw_gauntlet(p));
    y = left_column_text(g, y, line, 7);

    left_column_text(g, y,
                     mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED ?
                     "ANY KEY: VIEW ENHANCED RELICS..." : "HIT ANY KEY...",
                     15);
    video_present(v);
    input_wait_any_key(&g->input);

    if (mw_experience_mode(p) != MW_EXPERIENCE_ENHANCED) return;
    left_column_begin(g, p);
    y = 0;
    y = left_column_text(g, y, "ENHANCED SUPER-RARE RELICS:", 11);
    for (int relic = 0; relic < MW_RELIC_COUNT; relic++) {
        snprintf(line, sizeof(line), "%d) %s: %s", relic + 1,
                 enhanced_relics[relic].name,
                 mw_relic_owned(p, relic) ? "OWNED" : "NOT FOUND");
        y = left_column_text(g, y, line,
                             mw_relic_owned(p, relic) ? 10 : 8);
        y = left_column_text(g, y, enhanced_relics[relic].effect, 7);
    }
    if (mw_relic_owned(p, MW_RELIC_PHOENIX_SEAL)) {
        snprintf(line, sizeof(line), "PHOENIX RECHARGE: %u ACTIONS",
                 p->native.relic_phoenix_cooldown);
        y = left_column_text(g, y, line,
                             p->native.relic_phoenix_cooldown ? 14 : 10);
    }
    left_column_text(g, y, "HIT ANY KEY...", 15);
    video_present(v);
    input_wait_any_key(&g->input);
}

/* MW_PORT: WORLD func_0DDAA pockets inventory and its spell-item pages. */
/* ── Command: Pockets main menu (func_0DDAA) ── */

static void cmd_pockets(Game *g, Character *p) {
    Video *v = &g->video;
    int fh = v->font_char_h + 2;

    town_pane_begin(g, p);
    int y = 4;

    video_draw_text(v, 8, y, "WHICH DO YOU WISH TO SEE?", 14);
    y += fh + 4;
    int option_y = y;
    video_draw_text(v, 8, y, "1) SPELLBOOKS", 7); y += fh;
    video_draw_text(v, 8, y, "2) SCROLLS", 7); y += fh;
    video_draw_text(v, 8, y, "3) WANDS", 7); y += fh;
    video_draw_text(v, 8, y, "4) PAPERS", 7); y += fh;
    video_draw_text(v, 8, y, "5) MISC. MAGIC ITEMS", 7); y += fh + 4;
    video_draw_text(v, 8, y, "ANY OTHER KEY TO RETURNS...", 7);
    video_present(v);

    /* This menu accepts ASCII choices only.  Drain both bytes of DOS-style
     * extended keys so Page Down's 0x51 scan code cannot escape as 'Q'. */
    int key = input_wait_any_key(&g->input);
    if (key == INPUT_MOUSE_CLICK) {
        int choice = game_mouse_row(g, 0, SX(0x2D3), option_y, fh, 5);
        key = choice >= 0 ? '1' + choice : 0x1B;
    }
    switch (key) {
        case '1': cmd_pockets_spells(g, p, p->spells, 0, "SPELLBOOKS"); break;
        case '2': cmd_pockets_spells(g, p, p->scrolls, 0, "SCROLLS"); break;
        case '3': cmd_pockets_spells(g, p, p->wands, 1, "WANDS"); break;
        case '4': cmd_pockets_spells(g, p, p->papers, 0, "PAPERS"); break;
        case '5': cmd_pockets_misc(g, p); break;
    }
}

/* MW_PORT: WORLD inn_service/func_0A6F2 experience threshold and level-up
 * calculations, exposed by the E command and inn rest. */
/* ── Command: Experience Needed ── */

static void cmd_exp_needed(Game *g, Character *p) {
    Video *v = &g->video;
    char line[128];

    town_pane_begin(g, p);
    int y = 0;

    if (p->level >= MW_PLAYER_LEVEL_MAX) {
        snprintf(line, sizeof(line), "MAXIMUM PLAYER LEVEL REACHED: %u",
                 MW_PLAYER_LEVEL_MAX);
        y = town_pane_text(g, y, line, 4);
        snprintf(line, sizeof(line), "CURRENT EXPERIENCE: %.0f", p->experience);
        y = town_pane_text(g, y, line, 7);
        town_pane_text(g, y, "NO FURTHER LEVELS CAN BE EARNED.", 15);
        video_present(v);
        input_wait_any_key(&g->input);
        return;
    }

    snprintf(line, sizeof(line), "EXPERIENCE NEEDED FOR LEVEL: %d",
             p->level + 1);
    y = town_pane_text(g, y, line, 3);

    snprintf(line, sizeof(line), "CURRENT LEVEL: %d", p->level);
    y = town_pane_text(g, y, line, 7);

    snprintf(line, sizeof(line), "CURRENT EXPERIENCE: %.0f", p->experience);
    y = town_pane_text(g, y, line, 7);

    double target = experience_for_level((int)p->level + 1);
    snprintf(line, sizeof(line), "EXPERIENCE REQUIRED: %.0f", target);
    y = town_pane_text(g, y, line, 7);

    double needed = target - p->experience;
    if (needed < 0.0) needed = 0.0;
    snprintf(line, sizeof(line), "EXPERIENCE STILL NEEDED: %.0f", needed);
    y = town_pane_text(g, y, line, needed <= 0.0 ? 10 : 7);

    snprintf(line, sizeof(line), "HEALTH POINTS: %u OF %u",
             mw_hp_cur(p), mw_hp_max(p));
    y = town_pane_text(g, y, line, 7);

    snprintf(line, sizeof(line), "SPELL POINTS: %.0f OF %.0f",
             p->sp_cur, p->sp_max);
    y = town_pane_text(g, y, line, 7);

    town_pane_text(g, y, "HIT ANY KEY...", 15);
    video_present(v);
    input_wait_any_key(&g->input);
}

/* MW_PORT: WORLD shop_buy_check, shop_weapon, shop_pills, shop_magic,
 * shop_misc, shop_main_menu, shop_buy_item, shop_finances, func_081C1,
 * func_08326, inn_service, func_0A6F2, func_0A751, inn_hotel/inn_full and
 * the ordered post-kill chain ending at func_21F9C. */
/* ── Town locations and post-battle treasure ────────────────────────────
 *
 * Floor zero locations are not conventional dungeon rooms.  In the DOS
 * game each colored square is a ladder up to one service.  Type five is the
 * roof/wilderness exit (H.BIN screen 34), despite several old annotations
 * calling it a hotel. */

static void town_pane_begin(Game *g, Character *p) {
    game_draw_exploration(g, p);
    video_fill_rect(&g->video, 0, 0, SX(0x2D3), SY(0x1AE), 0);
}

static void left_column_begin(Game *g, Character *p) {
    game_draw_exploration(g, p);
    video_fill_rect(&g->video, 0, 0, SX(0x2D3), LOGICAL_H, 0);
}

static int pane_text(Game *g, int y, int bottom,
                     const char *text, u8 color) {
    enum { MAX_CHARS = 32 };
    const int row_h = SY(38);
    const char *p = text;
    char line[MAX_CHARS + 1];

    if (!p || !*p) return y;
    while (*p && y + row_h <= bottom) {
        while (*p == ' ') p++;
        int remaining = (int)strlen(p);
        int take = remaining < MAX_CHARS ? remaining : MAX_CHARS;
        if (remaining > MAX_CHARS) {
            int split = take;
            while (split > 0 && p[split] != ' ') split--;
            if (split > 0) take = split;
        }
        memcpy(line, p, (size_t)take);
        line[take] = '\0';
        while (take > 0 && line[take - 1] == ' ') line[--take] = '\0';
        video_draw_text_scaled_xy(&g->video, 0, y, line, color,
                                  7, 6, 12, 17);
        y += row_h;
        p += take;
        while (*p == ' ') p++;
    }
    return y;
}

static int town_pane_text(Game *g, int y, const char *text, u8 color) {
    return pane_text(g, y, SY(0x1AE), text, color);
}

static int left_column_text(Game *g, int y, const char *text, u8 color) {
    return pane_text(g, y, LOGICAL_H, text, color);
}

static void town_message(Game *g, Character *p, const char *title, const char *line1,
                         const char *line2, u8 color) {
    Video *v = &g->video;
    int y = 0;
    town_pane_begin(g, p);
    y = town_pane_text(g, y, title, color);
    if (line1 && *line1) y = town_pane_text(g, y, line1, 7);
    if (line2 && *line2) y = town_pane_text(g, y, line2, 7);
    town_pane_text(g, y, "HIT ANY KEY...", 15);
    video_present(v);
    input_wait_any_key(&g->input);
}

static int town_menu(Game *g, Character *p, const char *title, const char *subtitle,
                     const char *const *items, int item_count, u8 color,
                     u32 money) {
    Video *v = &g->video;
    char line[128];
    int item_top[16];
    int item_bottom[16];
    int y = 0;
    for (int i = 0; i < 16; i++) item_top[i] = item_bottom[i] = -1;
    town_pane_begin(g, p);
    y = town_pane_text(g, y, title, color);
    if (subtitle && *subtitle) y = town_pane_text(g, y, subtitle, 7);
    if (money != UINT32_MAX) {
        snprintf(line, sizeof(line), "MONEY ON HAND: %u JP", money);
        y = town_pane_text(g, y, line, 8);
    }
    for (int i = 0; i < item_count; i++) {
        snprintf(line, sizeof(line), "%d) %s", i + 1, items[i]);
        if (i < 16) item_top[i] = y;
        y = town_pane_text(g, y, line,
                           (u8)(i == item_count - 1 ? 8 : 7));
        if (i < 16) item_bottom[i] = y;
    }
    town_pane_text(g, y, "SELECT OPTION (ESC LEAVES)", 15);
    video_present(v);
    for (;;) {
        int key = input_wait_any_key(&g->input);
        if (input_poll_quit(&g->input) || key == 0x1B) return -1;
        if (key == INPUT_MOUSE_CLICK) {
            int x, click_y;
            if (game_mouse_click_logical(g, &x, &click_y) &&
                x >= 0 && x < SX(0x2D3)) {
                int limit = item_count < 16 ? item_count : 16;
                for (int i = 0; i < limit; i++)
                    if (item_bottom[i] > item_top[i] &&
                        click_y >= item_top[i] && click_y < item_bottom[i])
                        return i;
            }
            continue;
        }
        if (key >= '1' && key < '1' + item_count) return key - '1';
    }
}

static u32 town_prompt_amount(Game *g, Character *p, const char *title,
                              u32 available) {
    Video *v = &g->video;
    char digits[11] = "";
    int used = 0;
    for (;;) {
        char line[96];
        int y = 0;
        town_pane_begin(g, p);
        y = town_pane_text(g, y, title, 14);
        snprintf(line, sizeof(line), "MONEY AVAILABLE: %u JP", available);
        y = town_pane_text(g, y, line, 7);
        y = town_pane_text(g, y,
                           "PLEASE TYPE THE AMOUNT AND HIT ENTER:", 7);
        y = town_pane_text(g, y, used ? digits : "0", 15);
        town_pane_text(g, y, "ESC CANCELS", 8);
        const int keypad_y = SY(0x180);
        const int keypad_w = SX(0x2D3) / 12;
        video_fill_rect(v, 0, keypad_y, SX(0x2D3), SY(0x2E), 0);
        for (int i = 0; i < 12; i++) {
            char label[4];
            if (i < 9) snprintf(label, sizeof(label), "%d", i + 1);
            else if (i == 9) snprintf(label, sizeof(label), "0");
            else if (i == 10) snprintf(label, sizeof(label), "<");
            else snprintf(label, sizeof(label), "OK");
            video_draw_text_scaled_xy(v, i * keypad_w + 4, keypad_y + 2,
                                      label, i == 11 ? 4 : 15,
                                      2, 3, 2, 3);
            video_vline(v, i * keypad_w, keypad_y, SY(0x2E), 8);
        }
        video_present(v);

        int key = input_wait_any_key(&g->input);
        if (key == INPUT_MOUSE_CLICK) {
            int x, click_y;
            if (game_mouse_click_logical(g, &x, &click_y) &&
                x >= 0 && x < SX(0x2D3) && click_y >= keypad_y &&
                click_y < keypad_y + SY(0x2E)) {
                int button = x / keypad_w;
                if (button < 9) key = '1' + button;
                else if (button == 9) key = '0';
                else if (button == 10) key = 8;
                else key = '\r';
            } else continue;
        }
        if (input_poll_quit(&g->input) || key == 0x1B) return 0;
        if (key == '\r' || key == '\n') {
            unsigned long long value = used ? strtoull(digits, NULL, 10) : 0;
            if (value > available) value = available;
            return (u32)value;
        }
        if (key == 8) {
            if (used > 0) digits[--used] = 0;
        } else if (key >= '0' && key <= '9' && used < 10) {
            digits[used++] = (char)key;
            digits[used] = 0;
        }
    }
}

static int spend_jewels(Game *g, Character *p, u32 price) {
    if (p->jewels_pocket < price) {
        mw_audio_play(&g->audio, MW_SFX_ERROR);
        town_message(g, p, "SORRY, CAN'T BUY ON CREDIT HERE.",
                     "YOU DO NOT HAVE ENOUGH JEWEL PIECES.", "", 12);
        return 0;
    }
    p->jewels_pocket -= price;
    mw_audio_play(&g->audio, MW_SFX_COIN);
    return 1;
}

/* MW_PORT: Original L command (func_0C366): discard armor, weapons, or one complete
 * denomination of carried money.  Negative enchant bytes are cursed items;
 * when equipped they cannot be removed or dropped. */
static void cmd_drop_item(Game *g, Character *p) {
    static const char *const classic_categories[] = {
        "ARMOR", "WEAPON", "MONEY", "RETURN TO GAME"
    };
    static const char *const enhanced_categories[] = {
        "ARMOR", "WEAPON", "DEEP EQUIPMENT", "MONEY", "RETURN TO GAME"
    };
    int enhanced = mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED;
    const char *const *categories =
        enhanced ? enhanced_categories : classic_categories;
    int category_count = enhanced ? 5 : 4;
    int category = town_menu(g, p, "WHICH TYPE OF ITEM WOULD YOU LIKE TO DROP?",
                             "", categories, category_count, 14, UINT32_MAX);
    if (category < 0 || category == category_count - 1) return;

    if (category == 0 || category == 1) {
        char labels[8][64];
        const char *items[8];
        for (int i = 0; i < 8; i++) {
            int count = category == 0 ? p->armor_inventory[i]
                                      : p->weapon_inventory[i];
            int enchant = category == 0 ? mw_armor_enchant(p, i)
                                        : mw_weapon_enchant(p, i);
            const char *name = category == 0 ? armor_names[i] : weapon_names[i];
            if (i == 0)
                snprintf(labels[i], sizeof(labels[i]), "%-14s (IMPLICIT)", name);
            else if (count)
                snprintf(labels[i], sizeof(labels[i]), "%-14s X%d %+d",
                         name, count, enchant);
            else
                snprintf(labels[i], sizeof(labels[i]), "--------");
            items[i] = labels[i];
        }
        int selected = town_menu(g, p,
                                 category == 0 ? "SELECT ARMOR TO DROP"
                                               : "SELECT WEAPON TO DROP",
                                 "", items, 8, category == 0 ? 5 : 4,
                                 UINT32_MAX);
        if (selected <= 0) return; /* Skin and fists are never inventory. */

        u8 *inventory = category == 0 ? p->armor_inventory
                                      : p->weapon_inventory;
        int equipped = category == 0 ? p->equipped_armor
                                     : p->equipped_weapon;
        if (!inventory[selected]) return;
        int selected_enchant = category == 0 ? mw_armor_enchant(p, selected) :
                                               mw_weapon_enchant(p, selected);
        if (equipped == selected && selected_enchant < 0) {
            town_message(g, p, "OWE! IT JUST WON'T COME OFF!",
                         "THE ITEM IS CURSED.", "", 4);
            return;
        }

        inventory[selected]--;
        if (equipped == selected && !inventory[selected]) {
            if (category == 0) p->equipped_armor = 0;
            else p->equipped_weapon = 0;
        }
        char line[96];
        snprintf(line, sizeof(line), "YOU DROP THE %s.",
                 category == 0 ? armor_names[selected] : weapon_names[selected]);
        town_message(g, p, "ITEM DROPPED", line, "", 8);
        return;
    }

    if (enhanced && category == 2) {
        static const char *const kinds[] = {
            "LATE-GAME WEAPONS", "LATE-GAME ARMOR", "RETURN"
        };
        int kind = town_menu(g, p, "SELECT DEEP EQUIPMENT TYPE",
                             "", kinds, 3, 5, UINT32_MAX);
        if (kind < 0 || kind == 2) return;
        int armor = kind == 1;
        char labels[LATE_GEAR_TIER_COUNT][64];
        const char *items[LATE_GEAR_TIER_COUNT];
        for (int i = 0; i < LATE_GEAR_TIER_COUNT; i++) {
            int slot = (armor ? 8 : 12) + i;
            int count = armor ? mw_armor_inventory_count(p, slot) :
                                mw_weapon_inventory_count(p, slot);
            int enchant = armor ? mw_armor_enchant(p, slot) :
                                  mw_weapon_enchant(p, slot);
            const char *name = armor ? armor_names[slot] : weapon_names[slot];
            if (count)
                snprintf(labels[i], sizeof(labels[i]), "%-18s X%d %+d",
                         name, count, enchant);
            else
                snprintf(labels[i], sizeof(labels[i]), "--------");
            items[i] = labels[i];
        }
        int selected = town_menu(g, p, "SELECT DEEP EQUIPMENT TO DROP",
                                 "", items, LATE_GEAR_TIER_COUNT,
                                 armor ? 5 : 4, UINT32_MAX);
        if (selected < 0 || selected >= LATE_GEAR_TIER_COUNT) return;
        int slot = (armor ? 8 : 12) + selected;
        int count = armor ? mw_armor_inventory_count(p, slot) :
                            mw_weapon_inventory_count(p, slot);
        if (!count) return;
        int enchant = armor ? mw_armor_enchant(p, slot) :
                              mw_weapon_enchant(p, slot);
        int equipped = armor ? p->equipped_armor : p->equipped_weapon;
        if (equipped == slot && enchant < 0) {
            town_message(g, p, "OWE! IT JUST WON'T COME OFF!",
                         "THE ITEM IS CURSED.", "", 4);
            return;
        }
        if (armor) mw_set_armor_inventory_count(p, slot, count - 1);
        else mw_set_weapon_inventory_count(p, slot, count - 1);
        if (equipped == slot && count == 1) {
            if (armor) p->equipped_armor = 0;
            else p->equipped_weapon = 0;
        }
        char line[96];
        snprintf(line, sizeof(line), "YOU DROP THE %s.",
                 armor ? armor_names[slot] : weapon_names[slot]);
        town_message(g, p, "ITEM DROPPED", line, "", 8);
        return;
    }

    static const char *const money_items[] = {
        "COPPER STONES", "SILVER STONES", "IVORY STONES",
        "GOLD STONES", "PLATINUM STONES", "JEWEL STONES",
        "JEWEL PIECES", "RETURN TO GAME"
    };
    u32 *money[] = {
        &p->copper_stones, &p->silver_stones, &p->ivory_stones,
        &p->gold_stones, &p->platinum_stones, &p->jewel_stones,
        &p->jewels_pocket
    };
    int selected = town_menu(g, p, "WHICH MONEY DO YOU WISH TO DROP?", "",
                             money_items, 8, 8, UINT32_MAX);
    if (selected < 0 || selected >= 7 || !*money[selected]) return;
    u32 amount = *money[selected];
    *money[selected] = 0;
    char line[96];
    snprintf(line, sizeof(line), "YOU DROP %u %s.", amount,
             money_items[selected]);
    town_message(g, p, "MONEY DROPPED", line, "", 8);
}

typedef struct DigLanding {
    int floor, x, y;
    int relocated;
} DigLanding;

/* WORLD func_0EDAD walks the same coordinate through successive generated
 * floors.  Its original 124/150/130-floor geometry scales with the native
 * dungeon depth; floor 1 remains the absolute surface-side boundary.  It
 * does not create a persistent pit/ladder marker. */
static int find_dig_landing(Game *g, DigLanding *landing) {
    int floor = g->cur_floor;
    const GameTraversalRules *rules = game_traversal_rules(g);
    int direction_floor = rules->dig_direction_floor;
    int reverse_floor = rules->dig_reverse_floor;
    int search_attempts = rules->dig_search_attempts;
    int delta = floor < direction_floor ? 1 : -1;

    for (int attempts = 0; attempts <= search_attempts; attempts++) {
        floor += delta;
        if (floor == reverse_floor) delta = -1;
        if (floor == 1) delta = 1;
        if (!rock_cell_at(g, g->cur_x, g->cur_y, floor)) {
            landing->floor = floor;
            landing->x = g->cur_x;
            landing->y = g->cur_y;
            landing->relocated = 0;
            return 1;
        }
    }

    int fallback_floor = g->cur_floor < direction_floor ?
                         g->cur_floor + 1 : g->cur_floor;
    for (int x = 21; x < 59; x++) {
        for (int y = 21; y < 89; y++) {
            if (rock_cell_at(g, x, y, fallback_floor)) continue;
            landing->floor = fallback_floor;
            landing->x = x;
            landing->y = y;
            landing->relocated = 1;
            return 1;
        }
    }
    return 0;
}

static void dig_timed_message(Game *g, Character *p, const char *message,
                              u32 before_ms, u32 hold_ms) {
    town_pane_begin(g, p);
    video_present(&g->video);
    if (before_ms) game_delay(g, before_ms);
    town_pane_text(g, 0, message, 4);
    video_present(&g->video);
    if (hold_ms) game_delay(g, hold_ms);
}

static int dig_depth_allowed(Game *g, int floor) {
    return floor >= 0 &&
           floor <= game_traversal_rules(g)->dig_max_floor;
}

/* MW_PORT: Original D command (WORLD func_0EDAD). The lengthy pauses are gameplay:
 * monsters and timed effects get six opportunities before the work starts,
 * and shallow floors require a second, slower four-pass digging sequence. */
static int cmd_dig_hole(Game *g, Character *p) {
    if (!dig_depth_allowed(g, g->cur_floor)) {
        char depth_limit[96];
        snprintf(depth_limit, sizeof(depth_limit),
                 "PAST LEVEL %d, YOU MUST USE THE LADDERS.",
                 game_traversal_rules(g)->dig_max_floor);
        town_message(g, p,
                     "THE FLOOR SEEMS TO BE MADE OF SOLID ROCK. IT IS NOT POSSIBLE TO DIG HERE.",
                     depth_limit, "", 4);
        return 0;
    }

    static const char *const choices[] = {
        "DIG A HOLE IN THE FLOOR", "FORGET THE HOLE IDEA"
    };
    if (town_menu(g, p, "DO YOU WISH TO DIG A HOLE IN THE FLOOR?",
                  "IT MAY TAKE SOME TIME, BUT IF YOU ARE TRAPPED, THEN YOU HAVE NO CHOICE.",
                  choices, 2, 14,
                  UINT32_MAX) != 0)
        return 0;

    /* The original temporarily primes all 145 monster action counters, runs
     * six complete world turns, then checks for an open-passage encounter. */
    for (int turn = 0; turn < 6; turn++)
        game_advance_monsters(g, p);
    if (game_find_adjacent_monster(g) >= 0) {
        dig_timed_message(g, p, "A MONSTER WANTS TO HELP", 0, 1000);
        return 0;
    }

    for (int pass = 0; pass < 4; pass++)
        dig_timed_message(g, p, "DIGGING... DIGGING...", 300, 1500);

    town_message(g, p, "BOY THIS IS HARD WORK!",
                 "YOUR HANDS ARE RAW FROM DIGGING. BLISTERS ARE DEVELOPING ON YOUR HANDS",
                 "MAKING IT A LITTLE MORE DIFFICULT TO HOLD YOUR WEAPONS.", 4);

    if (g->cur_floor < game_traversal_rules(g)->dig_slow_floor)
        for (int pass = 0; pass < 4; pass++)
            dig_timed_message(g, p, "DIGGING... DIGGING...", 300, 2000);

    DigLanding landing;
    if (!find_dig_landing(g, &landing)) {
        town_message(g, p, "THE FLOOR SEEMS TO BE MADE OF SOLID ROCK.",
                     "IT IS NOT POSSIBLE TO DIG HERE.", "", 4);
        return 0;
    }

    /* func_0D2C9 removes preparation effects before an ordinary same-column
     * landing.  Preserve the original exceptional relocation distinction. */
    if (!landing.relocated) character_clear_town_effects(p);
    g->cur_x = landing.x;
    g->cur_y = landing.y;
    game_change_floor(g, p, landing.floor);
    p->x_pos = (u16)g->cur_x;
    p->y_pos = (u16)g->cur_y;
    return 1;
}

static void town_store(Game *g, Character *p) {
    static const char *const main_items[] = {
        "WEAPONS", "ARMOR", "EXIT STORE"
    };
    static const char *const weapon_items[] = {
        "STICK..........1 JP", "CLUB..........15 JP",
        "MACE.........300 JP", "KNIFE.........30 JP",
        "SHORTSWORD...250 JP", "LONG SWORD...450 JP",
        "RETURN TO STORE"
    };
    static const u32 weapon_price[6] = {1, 15, 300, 30, 250, 450};
    static const char *const armor_items[] = {
        "ROBES (USELESS).1 JP", "LEATHER........50 JP",
        "CHAIN.........300 JP", "SCALE........1500 JP",
        "PLATE........4000 JP", "FIELD PLATE..9900 JP",
        "RETURN TO STORE"
    };
    static const u32 armor_price[6] = {1, 50, 300, 1500, 4000, 9900};

    for (;;) {
        int choice = town_menu(g, p, "YOU HAVE ENTERED A STORE",
                               "WHAT WOULD YOU LIKE TO BUY?",
                               main_items, 3, 3, p->jewels_pocket);
        if (choice < 0 || choice == 2) return;
        if (choice == 0) {
            int w = town_menu(g, p, "PLEASE SELECT A WEAPON:", "",
                              weapon_items, 7, 3, p->jewels_pocket);
            if (w >= 0 && w < 6 && spend_jewels(g, p, weapon_price[w])) {
                int slot = w + 1; /* Fist is slot zero and is never sold. */
                if (p->weapon_inventory[slot] != 0xFF)
                    p->weapon_inventory[slot]++;
                town_message(g, p, "PURCHASE COMPLETE!", weapon_names[slot],
                             "HIT 'W' IN THE DUNGEON TO EQUIP IT.", 3);
            }
        } else {
            int a = town_menu(g, p, "PLEASE SELECT ARMOR:", "",
                              armor_items, 7, 3, p->jewels_pocket);
            if (a >= 0 && a < 6 && spend_jewels(g, p, armor_price[a])) {
                if (p->armor_inventory[a] != 0xFF) p->armor_inventory[a]++;
                town_message(g, p, "PURCHASE COMPLETE!", a == 0 ? "ROBES" : armor_names[a],
                             "HIT 'A' IN THE DUNGEON TO WEAR IT.", 3);
            }
        }
    }
}

static u32 temple_contract_price(Game *g, Character *p) {
    /* WORLD rolls before the level branch.  Levels 1..60 pay level*500 plus
       0..19; only levels above 60 are forced to the 500,000-JP ceiling. */
    u32 variation = (u32)original_rand_scaled(g, 20);
    if (p->level > 60) return 500000u;
    return (u32)p->level * 500u + variation;
}

static void town_temple(Game *g, Character *p) {
    for (;;) {
        char contract[64];
        const char *items[7] = {
            "CURE WOUNDS..........30 JP",
            "CURE SERIOUS WOUNDS.200 JP",
            "HEAL ALL WOUNDS....2500 JP",
            "CURE POISON.........300 JP",
            "CURE DISEASE........500 JP",
            contract,
            "LEAVE TEMPLE"
        };
        u32 contract_price = temple_contract_price(g, p);
        snprintf(contract, sizeof(contract), "RAISE CONTRACT...%u JP", contract_price);
        int choice = town_menu(g, p, "YOU ARE IN A TEMPLE",
                               "PLEASE SELECT A SPELL", items, 7, 4,
                               p->jewels_pocket);
        if (choice < 0 || choice == 6) return;
        static const u32 prices[5] = {30, 200, 2500, 300, 500};
        u32 price = choice == 5 ? contract_price : prices[choice];
        if (!spend_jewels(g, p, price)) continue;

        char result[96];
        if (choice == 0) {
            int heal = 1 + game_rand(g) % 10;
            uint64_t hp = (uint64_t)mw_hp_cur(p) + (unsigned)heal;
            mw_set_hp_cur(p, hp > mw_hp_max(p) ? mw_hp_max(p) : hp);
            snprintf(result, sizeof(result), "THE TEMPLE RESTORES %d HEALTH POINTS.", heal);
        } else if (choice == 1) {
            int heal = 10;
            for (int i = 0; i < 5; i++) heal += game_rand(g) % 15;
            uint64_t hp = (uint64_t)mw_hp_cur(p) + (unsigned)heal;
            mw_set_hp_cur(p, hp > mw_hp_max(p) ? mw_hp_max(p) : hp);
            snprintf(result, sizeof(result), "THE TEMPLE RESTORES %d HEALTH POINTS.", heal);
        } else if (choice == 2) {
            mw_set_hp_cur(p, mw_hp_max(p));
            snprintf(result, sizeof(result), "ALL OF YOUR WOUNDS ARE HEALED.");
        } else if (choice == 3) {
            p->poisoned_turns = 0;
            snprintf(result, sizeof(result), "THE POISON HAS BEEN CURED.");
        } else if (choice == 4) {
            p->diseased_turns = 0;
            snprintf(result, sizeof(result), "THE DISEASE HAS BEEN CURED.");
        } else {
            p->raise_floor = (u16)g->cur_floor;
            p->raise_x = (u16)g->cur_x;
            p->raise_y = (u16)g->cur_y;
            snprintf(result, sizeof(result), "YOUR RAISE DEAD CONTRACT IS IN EFFECT.");
        }
        town_message(g, p, "TEMPLE SERVICE COMPLETE", result, "", 4);
    }
}

static void add_u32_sat(u32 *value, unsigned long long add) {
    unsigned long long total = (unsigned long long)*value + add;
    *value = total > UINT32_MAX ? UINT32_MAX : (u32)total;
}

static unsigned long long town_convert_stones(Character *p) {
    unsigned long long converted = (unsigned long long)p->platinum_stones * 5u +
        p->jewel_stones + p->gold_stones / 2u + p->ivory_stones / 4u +
        p->silver_stones / 12u + p->copper_stones / 200u;
    p->copper_stones = p->silver_stones = p->ivory_stones = 0;
    p->gold_stones = p->platinum_stones = p->jewel_stones = 0;
    add_u32_sat(&p->jewels_pocket, converted);
    return converted;
}

static void town_view_money(Game *g, Character *p) {
    char line[96];
    int y = 0;
    town_pane_begin(g, p);
    y = town_pane_text(g, y, "YOUR FINANCIAL STATEMENT:", 4);
#define TOWN_MONEY_LINE(label, value) do { \
        snprintf(line, sizeof(line), label "%u", (unsigned)(value)); \
        y = town_pane_text(g, y, line, 7); \
    } while (0)
    TOWN_MONEY_LINE("COPPER STONES: ", p->copper_stones);
    TOWN_MONEY_LINE("SILVER STONES: ", p->silver_stones);
    TOWN_MONEY_LINE("IVORY STONES: ", p->ivory_stones);
    TOWN_MONEY_LINE("GOLD STONES: ", p->gold_stones);
    TOWN_MONEY_LINE("PLATINUM STONES: ", p->platinum_stones);
    TOWN_MONEY_LINE("JEWEL STONES: ", p->jewel_stones);
    TOWN_MONEY_LINE("JEWELS IN POCKET: ", p->jewels_pocket);
    TOWN_MONEY_LINE("JEWELS IN BANK: ", p->jewels_bank);
#undef TOWN_MONEY_LINE
    town_pane_text(g, y, "HIT ANY KEY...", 15);
    video_present(&g->video);
    input_wait_any_key(&g->input);
}

static void town_bank(Game *g, Character *p) {
    static const char *const items[] = {
        "CONVERT TO JEWEL PIECES", "DEPOSIT MONEY", "WITHDRAW MONEY",
        "ROB BANK", "LEAVE BANK"
    };
    for (;;) {
        int choice = town_menu(g, p, "WELCOME TO MORAFF'S FIRST NATIONAL BANK.",
                               "OPTIONS:", items, 5, 5, p->jewels_pocket);
        if (choice < 0 || choice == 4) return;
        if (choice == 0) {
            unsigned long long converted = town_convert_stones(p);
            char line[96];
            snprintf(line, sizeof(line), "THE BANK CONVERTS YOUR STONES INTO %llu JP.", converted);
            town_message(g, p, "STONE CONVERSION COMPLETE", line, "", 5);
        } else if (choice == 1) {
            u32 amount = town_prompt_amount(g, p, "DEPOSIT MONEY", p->jewels_pocket);
            p->jewels_pocket -= amount;
            add_u32_sat(&p->jewels_bank, amount);
            town_view_money(g, p);
        } else if (choice == 2) {
            u32 amount = town_prompt_amount(g, p, "WITHDRAW MONEY", p->jewels_bank);
            p->jewels_bank -= amount;
            add_u32_sat(&p->jewels_pocket, amount);
            town_view_money(g, p);
        } else {
            town_message(g, p, "COME ON! DO YOU REALLY THINK I'D LET YOU",
                         "ROB MY OWN BANK?", "", 12);
        }
    }
}

static double experience_for_level(int level) {
    /* The original curve is fifth-power and is stored/compared as a double. */
    double n = (double)(level < 1 ? 1 : level);
    return n * n * n * n * n;
}

typedef struct LevelGrowth {
    int hp_base;
    int hp_range;
    int sp_gain;
} LevelGrowth;

/* MW_PORT: WORLD far_23075's seven original class branches.  hp_range is the
   exclusive upper bound passed through WORLD's scaled 15-bit random roll. */
static LevelGrowth original_level_growth(const Character *p) {
    LevelGrowth growth = {1 + p->stat_con / 5,
                          p->stat_con / 4 + 1,
                          p->class_id == CLASS_FIGHTER ? 0 :
                          1 + (p->stat_int + p->stat_wis) / 10};
    switch (p->class_id) {
    case CLASS_FIGHTER:
        growth = (LevelGrowth){35,
            2 * p->stat_con + p->stat_luck / 2 + 10, 0};
        break;
    case CLASS_WORSHIPPER:
        growth = (LevelGrowth){15,
            p->stat_con / 2 + p->stat_luck / 2 + 10,
            (2 * p->stat_wis + p->stat_int) / 3};
        break;
    case CLASS_MONK:
        growth = (LevelGrowth){14,
            p->stat_con / 2 + p->stat_luck / 3 + 5,
            (p->stat_wis + p->stat_int) / 13};
        break;
    case CLASS_WIZARD:
        growth = (LevelGrowth){13,
            p->stat_con / 3 + p->stat_luck / 5 + 4,
            (2 * p->stat_int + p->stat_wis) / 5};
        break;
    case CLASS_PRIEST:
        growth = (LevelGrowth){14,
            p->stat_con / 2 + p->stat_luck / 3 + 4,
            (2 * p->stat_wis + p->stat_int) / 5};
        break;
    case CLASS_SAGE:
        growth = (LevelGrowth){55,
            3 * p->stat_con + p->stat_luck + 17,
            (p->stat_wis + p->stat_int) / 14};
        break;
    case CLASS_MAGE:
        growth = (LevelGrowth){14,
            p->stat_con / 2 + p->stat_luck / 3 + 7,
            (2 * p->stat_int + p->stat_wis) / 8};
        break;
    default:
        /* Enhanced-only classes retain the native extension's balanced
           generic curve; they have no branch in the 1993 executable. */
        break;
    }
    if (growth.hp_range < 1) growth.hp_range = 1;
    if (growth.sp_gain < 0) growth.sp_gain = 0;
    return growth;
}

static int inn_apply_levels(Game *g, Character *p) {
    int gained = 0;
    if (!isfinite(p->experience) || p->experience < 0.0) p->experience = 0.0;
    while (p->level < MW_PLAYER_LEVEL_MAX &&
           p->experience >= experience_for_level((int)p->level + 1)) {
        LevelGrowth growth = original_level_growth(p);
        u32 old_hp_max = mw_hp_max(p);
        u32 old_hp_cur = mw_hp_cur(p);
        int hp_gain = growth.hp_base +
                      original_rand_scaled(g, growth.hp_range);
        uint64_t hp = (uint64_t)old_hp_max + (unsigned)hp_gain;
        u32 hp_cap = mw_player_hp_cap(p);
        mw_set_hp_max(p, hp > hp_cap ? hp_cap : hp);
        /* WORLD adds exactly the realized maximum-HP increase to current HP;
           resting at the inn therefore preserves the existing wound deficit. */
        mw_set_hp_cur(p, (uint64_t)old_hp_cur + mw_hp_max(p) - old_hp_max);
        if (growth.sp_gain) {
            float sp_gain = (float)growth.sp_gain;
            p->sp_max = p->sp_max > MW_PLAYER_SP_MAX - sp_gain ?
                        MW_PLAYER_SP_MAX : p->sp_max + sp_gain;
        }
        p->level++;
        gained++;
    }
    return gained;
}

static void town_inn(Game *g, Character *p) {
    static const char *const items[] = {
        "STAY FOR THE NIGHT", "RUN FOR YOUR LIFE"
    };
    int choice = town_menu(g, p, "WELCOME TO THE FLEA BAG INN",
                           "ROOMS COST 10 JEWEL PIECES PER NIGHT.",
                           items, 2, 6, p->jewels_pocket);
    if (choice != 0) return;
    if (!spend_jewels(g, p, 10)) {
        town_message(g, p, "THREE BIG THUGS BEAT YOU UP AND THROW YOU OUT",
                     "BECAUSE YOU CAN'T PAY YOUR BILL.", "", 12);
        return;
    }
    add_u32_sat(&p->age, 8u * 3600u);
    character_clear_town_effects(p);
    int gained = inn_apply_levels(g, p);
    p->sp_cur = p->sp_max;
    if (gained) {
        char line[96];
        snprintf(line, sizeof(line), "YOU GAINED %d LEVEL%s AND ARE NOW LEVEL %u.",
                 gained, gained == 1 ? "" : "S", p->level);
        town_message(g, p, "CONGRATULATIONS! YOU HAVE BECOME MORE POWERFUL.",
                     line, "NEW HEALTH WAS ADDED; SPELL POINTS ARE RESTORED.", 6);
    } else {
        town_message(g, p, "YOU REST FOR THE NIGHT.",
                     "YOUR SPELL POINTS ARE RESTORED.",
                     "NO NEW LEVEL HAS BEEN EARNED YET.", 6);
    }
}

static void town_wilderness_exit(Game *g, Character *p) {
    static const char *const items[] = {
        "EXPLORE THE WILDERNESS", "RETURN TO THE DUNGEON"
    };
    int choice = town_menu(g, p, "YOU ARE STANDING ON TOP OF THE TOWN.",
                           "NOTE: WILDERNESS EXPLORATION REQUIRES A FAST COMPUTER!",
                           items, 2, 7, UINT32_MAX);
    if (choice == 0) wilderness_run(g, p);
}

static void enter_town_location(Game *g, Character *p, int type) {
    switch (type) {
    case 1: town_store(g, p); break;
    case 2: town_temple(g, p); break;
    case 3: town_bank(g, p); break;
    case 4: town_inn(g, p); break;
    case 5: town_wilderness_exit(g, p); break;
    }
}

static void inc_u8_sat(u8 *value, int amount) {
    int total = (int)*value + amount;
    *value = (u8)(total > 255 ? 255 : total);
}

static int grant_enhanced_relic(Character *p, int relic) {
    if (!p || mw_experience_mode(p) != MW_EXPERIENCE_ENHANCED ||
        relic < 0 || relic >= MW_RELIC_COUNT || mw_relic_owned(p, relic))
        return 0;
    mw_set_relic_owned(p, relic, 1);
    if (relic == MW_RELIC_PHOENIX_SEAL)
        p->native.relic_phoenix_cooldown = 0;
    return 1;
}

/* MW_EXTENSION: Enhanced's second equipment page is a complete eight-step
 * progression.  Ownership and unlock state are deliberately separate, so a
 * dropped or acid-destroyed item does not make its one-time cache reappear. */
static int grant_late_gear(Character *p, int tier) {
    if (!p || mw_experience_mode(p) != MW_EXPERIENCE_ENHANCED ||
        tier < 0 || tier >= LATE_GEAR_TIER_COUNT ||
        mw_late_gear_unlocked(p, tier))
        return 0;
    mw_set_late_gear_unlocked(p, tier);
    mw_set_weapon_inventory_count(p, 12 + tier, 1);
    mw_set_armor_inventory_count(p, 8 + tier, 1);
    if (tier == 6)
        mw_set_quest_flags(p, (u16)(mw_quest_flags(p) |
                                    MW_FINAL_GEAR_QUEST_FLAG));
    return 1;
}

/* Retained as a focused test/helper name from the earlier three-item
   implementation; the former "final" cache is now progression tier seven. */
static int grant_final_gear(Character *p, int depth) {
    return depth >= late_gear_floor[6] ? grant_late_gear(p, 6) : 0;
}

static void grant_quest_reward(Character *p, const CombatState *cs,
                               char *out, size_t out_size) {
    int step = quest_step_for_type(cs->monster_type_idx);
    u16 flags = mw_quest_flags(p);
    if (step < 0 || cs->monster_level < 1 ||
        (int)p->floor_depth != quest_floor_by_step[step] ||
        (flags & (1u << step))) return;
    mw_set_quest_flags(p, (u16)(flags | (1u << step)));
    int equipped_weapon =
        p->equipped_weapon < WEAPON_STAT_COUNT &&
        !(p->equipped_weapon >= 8 && p->equipped_weapon <= 11) ?
        p->equipped_weapon : 0;
    switch (step) {
    case 0: mw_set_body_armor_plus(p, 9); snprintf(out, out_size, "QUEST: PLUS 9 BODY ARMOR!"); break;
    case 1: mw_set_gauntlet(p, 12); snprintf(out, out_size, "QUEST: PLUS 12 GAUNTLET!"); break;
    case 2: mw_set_ring_prot_plus(p, 15); snprintf(out, out_size, "QUEST: PLUS 15 RING OF PROTECTION!"); break;
    case 3:
        mw_set_weapon_enchant(p, equipped_weapon, 25);
        snprintf(out, out_size, "QUEST: YOUR EQUIPPED WEAPON IS NOW PLUS 25!");
        break;
    case 4: mw_set_body_armor_plus(p, 25); snprintf(out, out_size, "QUEST: PLUS 25 BODY ARMOR!"); break;
    case 5: mw_set_gauntlet(p, 50); snprintf(out, out_size, "QUEST: PLUS 50 GAUNTLET!"); break;
    case 6: mw_set_ring_prot_plus(p, 50); snprintf(out, out_size, "QUEST: PLUS 50 RING OF PROTECTION!"); break;
    case 7:
        mw_set_weapon_enchant(p, equipped_weapon, 100);
        snprintf(out, out_size, "QUEST: YOUR EQUIPPED WEAPON IS NOW PLUS 100!");
        break;
    case 8:
        mw_set_weapon_enchant(p, equipped_weapon, 200);
        grant_late_gear(p, 0);
        snprintf(out, out_size,
                 "QUEST: PLUS 200 ABYSSAL ORB! WORLDFORGED GEAR UNLOCKED!");
        break;
    case 9:
        mw_set_weapon_enchant(p, equipped_weapon, 300);
        grant_late_gear(p, 1);
        snprintf(out, out_size,
                 "QUEST: PLUS 300 WORLD ORB! RIFTWARD GEAR UNLOCKED!");
        break;
    case 10:
        mw_set_weapon_enchant(p, equipped_weapon, 450);
        grant_late_gear(p, 2);
        snprintf(out, out_size,
                 "QUEST: PLUS 450 RIFT ORB! STARFORGED GEAR UNLOCKED!");
        break;
    case 11:
        mw_set_weapon_enchant(p, equipped_weapon, 600);
        grant_late_gear(p, 3);
        snprintf(out, out_size,
                 "QUEST: PLUS 600 STAR ORB! VOID GEAR UNLOCKED!");
        break;
    case 12:
        mw_set_weapon_enchant(p, equipped_weapon, 800);
        grant_late_gear(p, 5);
        snprintf(out, out_size,
                 "QUEST: PLUS 800 ETERNITY ORB! CELESTIAL GEAR UNLOCKED!");
        break;
    case 13:
        mw_set_weapon_enchant(p, equipped_weapon, 1000);
        grant_late_gear(p, 7);
        snprintf(out, out_size,
                 "QUEST: PLUS 1000 ASCENDANT ORB! MORAFF'S GEAR UNLOCKED!");
        break;
    }
}

static int reward_random_below(Game *g, int limit) {
    return limit > 1 ? game_rand(g) % limit : 0;
}

/* Miscellaneous treasure is a separate stage in WORLD's kill routine.  It
 * is not mixed with armor, stones, pills, scrolls, wands, or papers. */
static int award_random_magic_item(Game *g, Character *p,
                                   char *loot, size_t loot_size) {
    int kind = game_rand(g) % 12;
    switch (kind) {
    case 0:
        inc_u8_sat(&p->holy_grenade, 1);
        snprintf(loot, loot_size, "A HOLY HAND GRENADE!");
        break;
    case 1:
        inc_u8_sat(&p->stone_teleport, 1);
        snprintf(loot, loot_size, "A STONE OF TELEPORTATION!");
        break;
    case 2:
        inc_u8_sat(&p->stone_see, 1);
        snprintf(loot, loot_size, "A STONE OF SEEING.");
        break;
    case 3:
        if (p->floor_slosher) {
            snprintf(loot, loot_size,
                     "YOU ALREADY HAVE A FLOOR SLOSHER; TWO ARE NO BETTER THAN ONE.");
        } else {
            p->floor_slosher = 1;
            snprintf(loot, loot_size, "THE FAMOUS FLOOR SLOSHER!");
        }
        break;
    case 4:
        inc_u8_sat(&p->potion_heal, 1);
        snprintf(loot, loot_size, "A POTION OF HEALING!");
        break;
    case 5:
        inc_u8_sat(&p->ring_regen, 1);
        snprintf(loot, loot_size, "A RING OF REGENERATION!");
        break;
    case 6:
        if (p->stat_str < MW_PLAYER_STAT_MAX) p->stat_str++;
        snprintf(loot, loot_size, "A BOOK OF STRENGTH!");
        break;
    case 7:
        if (p->stat_int < MW_PLAYER_STAT_MAX) p->stat_int++;
        snprintf(loot, loot_size, "A BOOK OF INTELLIGENCE!");
        break;
    case 8:
        if (p->stat_wis < MW_PLAYER_STAT_MAX) p->stat_wis++;
        snprintf(loot, loot_size, "A BOOK OF WISDOM!");
        break;
    case 9:
        if (p->stat_con < MW_PLAYER_STAT_MAX) p->stat_con++;
        snprintf(loot, loot_size, "A BOOK OF CONSTITUTION!");
        break;
    case 10:
        if (p->stat_agi < MW_PLAYER_STAT_MAX) p->stat_agi++;
        snprintf(loot, loot_size, "A BOOK OF DEXTERITY!");
        break;
    default:
        if (p->stat_luck < MW_PLAYER_STAT_MAX) p->stat_luck++;
        snprintf(loot, loot_size, "A BOOK OF LUCK!");
        break;
    }
    return kind;
}

/* source: 0 scroll, 1 wand, 2 paper.  The three original helpers use
 * different depth curves but all choose one of the four spell families and
 * one of the three spells at the selected level. */
static int award_spell_item(Game *g, Character *p, int depth, int source,
                            char *loot, size_t loot_size) {
    int category = game_rand(g) % 4;
    int spell;
    int deep_count = 0;
    if (mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED && depth >= 100) {
        while (deep_count < MW_DEEP_SPELL_COUNT &&
               depth >= deep_spell_unlock_floor[deep_count])
            deep_count++;
    }
    if (deep_count && game_rand(g) % 2 == 0) {
        spell = MW_DEEP_SPELL_FIRST +
                reward_random_below(g, deep_count);
    } else {
        int divisor = source == 0 ? 3 : (source == 1 ? 60 : 8);
        int level = depth / divisor;
        if (level > 9) level = 9;
        if (level < 0) level = 0;
        level = reward_random_below(g, level + 1);
        spell = level * 3 + game_rand(g) % 3;
    }
    if (source == 0) {
        inc_u8_sat(&p->scrolls[category][spell], 1);
        snprintf(loot, loot_size, "YOU HAVE FOUND A SCROLL OF %s.",
                 spell_type_names[category][spell]);
    } else if (source == 1) {
        int charges = 2 + game_rand(g) % 5;
        inc_u8_sat(&p->wands[category][spell], charges);
        snprintf(loot, loot_size, "YOU FOUND A WAND OF %s WITH %d CHARGES.",
                 spell_type_names[category][spell], charges);
    } else {
        inc_u8_sat(&p->papers[category][spell], 1);
        snprintf(loot, loot_size, "YOU FIND A SPELL PAPER OF %s.",
                 spell_type_names[category][spell]);
    }
    return source;
}

static void reward_show_kill(Game *g, Character *p, int xp) {
    char line[96];
    int y = 0;
    town_pane_begin(g, p);
    y = town_pane_text(g, y, "YOU KILLED IT!", 8);
    snprintf(line, sizeof(line), "YOU GAIN %d EXPERIENCE POINTS.", xp);
    town_pane_text(g, y, line, 7);
    video_present(&g->video);
    game_delay(g, 1050);
}

static void reward_pill(Game *g, Character *p, int depth,
                        const CombatState *cs) {
    /* The +0x244 monster field tested by WORLD is the level-drain amount.
     * Only level drainers can leave the six colored pills. */
    if (combat_monster_drain_amount(cs->monster_type_idx) <= 0) return;
    int chance = depth + 175;
    if (chance > 374) chance = 374;
    if (game_rand(g) % 375 >= chance) return;

    static const char *const names[6] = {
        "ORANGE", "GREEN", "BLUE", "RED", "WHITE", "YELLOW"
    };
    u8 *slot[6] = {
        &p->orange_pill, &p->green_pill, &p->blue_pill,
        &p->red_pill, &p->white_pill, &p->yellow_pill
    };
    int which = game_rand(g) % 6;
    char title[96];
    inc_u8_sat(slot[which], 1);
    snprintf(title, sizeof(title), "YOU FOUND AN %s PILL!", names[which]);
    if (which != 0)
        snprintf(title, sizeof(title), "YOU FOUND A %s PILL!", names[which]);
    town_message(g, p, title, "USE THE 'USE ITEM' MENU TO",
                 "TAKE THE PILL (HIT 'I').", 14);
}

static void reward_key(Game *g, Character *p) {
    int key_index = g->cur_floor / 10;
    if (g->cur_floor <= 9 || g->cur_floor >= 179 || key_index <= 0 ||
        key_index >= 18 || p->trapdoor_keys[key_index]) return;
    p->trapdoor_keys[key_index] = 1;
    char line[96];
    snprintf(line, sizeof(line), "IT IS LABELED NUMBER %d.", key_index * 10);
    town_message(g, p, "YOU HAVE FOUND A KEY!", line,
                 "THIS KEY OPENS TRAP DOORS WITH THIS NUMBER.", 14);
}

static void reward_offer_armor(Game *g, Character *p,
                               const CombatState *cs) {
    if (p->class_id == CLASS_MONK) return;
    int armor = 1 + game_rand(g) % 6;
    int roll = reward_random_below(g, armor * 100);
    if (roll > cs->monster_level + 10) return;

    char title[96];
    const char *const choices[] = {"TAKE THE ARMOR", "LEAVE THE ARMOR"};
    snprintf(title, sizeof(title), "YOU FIND A SUIT OF %s ARMOR.",
             armor_names[armor]);
    int choice = town_menu(g, p, title,
                           "YOU MAY CARRY SEVERAL SUITS, BUT THEIR WEIGHT ADDS UP.",
                           choices, 2, 14, UINT32_MAX);
    if (choice == 0) inc_u8_sat(&p->armor_inventory[armor], 1);
}

typedef struct RewardStones {
    u32 amount[6]; /* copper, silver, ivory, gold, platinum, jewel */
} RewardStones;

static u32 reward_stone_amount(Game *g, int level, int depth,
                               int bonus, int cap, int multiplier,
                               int extra) {
    unsigned long long a = (unsigned)(reward_random_below(g, level + bonus) + 1);
    unsigned long long b = (unsigned)(reward_random_below(g, depth + 1) + 1);
    unsigned long long base = a * b;
    if (base > (unsigned)cap) base = (unsigned)cap;
    unsigned long long value = base * (unsigned)multiplier +
                               (unsigned)reward_random_below(g, extra + 1);
    return value > UINT32_MAX ? UINT32_MAX : (u32)value;
}

static int reward_make_stone_pile(Game *g, int level, int depth,
                                  RewardStones *pile) {
    memset(pile, 0, sizeof(*pile));
    if (game_rand(g) % 3 != 0) return 0;
    if (level < 1) level = 1;
    if (depth < 0) depth = 0;

    if (game_rand(g) % 3 == 0)
        pile->amount[0] = reward_stone_amount(g, level, depth, 61, 140, 200, 500);
    if (game_rand(g) % 3 == 0)
        pile->amount[1] = reward_stone_amount(g, level, depth, 51, 140, 12, 50);
    if (game_rand(g) % 4 == 0)
        pile->amount[2] = reward_stone_amount(g, level, depth, 31, 120, 4, 30);
    if (game_rand(g) % 4 == 0)
        pile->amount[3] = reward_stone_amount(g, level, depth, 11, 100, 2, 12);
    if (game_rand(g) % 5 == 0)
        pile->amount[4] = reward_stone_amount(g, level, depth, 6, 80, 1, 8);
    if (game_rand(g) % 5 == 0)
        pile->amount[5] = reward_stone_amount(g, level, depth, 3, 60, 1, 3);

    if (depth > 10 && reward_random_below(g, 1250) < depth - 10) {
        unsigned long long jackpot =
            (unsigned)(reward_random_below(g, depth) + 1) *
            (unsigned)(reward_random_below(g, depth / 4 + 10) + 1) * 100u +
            (unsigned)reward_random_below(g, 10000);
        add_u32_sat(&pile->amount[5], jackpot);
    }
    for (int i = 0; i < 6; i++)
        if (pile->amount[i]) return 1;
    return 0;
}

static void reward_take_stones(Character *p, const RewardStones *pile,
                               int key) {
    int first = 6;
    if (key == 'A') first = 0;
    else if (key == 'I') first = 2;
    else if (key == 'G') first = 3;
    else if (key == 'P') first = 4;
    else if (key == 'J') first = 5;
    u32 *pocket[6] = {
        &p->copper_stones, &p->silver_stones, &p->ivory_stones,
        &p->gold_stones, &p->platinum_stones, &p->jewel_stones
    };
    for (int i = first; i < 6; i++) add_u32_sat(pocket[i], pile->amount[i]);
}

static void reward_offer_stones(Game *g, Character *p,
                                const RewardStones *pile) {
    static const char *const names[6] = {
        "COPPER", "SILVER", "IVORY", "GOLD", "PLATINUM", "JEWEL"
    };
    unsigned long long count = 0;
    for (int i = 0; i < 6; i++) count += pile->amount[i];
    unsigned long long value = pile->amount[0] / 200u +
        pile->amount[1] / 12u + pile->amount[2] / 4u +
        pile->amount[3] / 2u + (unsigned long long)pile->amount[4] * 5u +
        pile->amount[5];
    int largest = 0;
    for (int i = 1; i < 6; i++)
        if (pile->amount[i] > pile->amount[largest]) largest = i;

    char line[128];
    int y = 0;
    town_pane_begin(g, p);
    snprintf(line, sizeof(line), "YOU FIND %llu STONES. THE", value);
    y = town_pane_text(g, y, line, 14);
    snprintf(line, sizeof(line), "PILE WEIGHS ABOUT %llu POUNDS.", count / 16u);
    y = town_pane_text(g, y, line, 7);
    snprintf(line, sizeof(line), "THEY ARE MOSTLY %s.", names[largest]);
    y = town_pane_text(g, y, line, 7);

    if (p->weight_pounds &&
        game_loaded_weight(p) > (unsigned long long)p->weight_pounds * 3u) {
        y = town_pane_text(g, y, "IT IS TOO HEAVY FOR YOU TO CARRY.", 12);
        town_pane_text(g, y, "HIT ANY KEY...", 15);
        video_present(&g->video);
        input_wait_any_key(&g->input);
        return;
    }

    int choice_y = y;
    y = town_pane_text(g, y, "A) TAKE ALL    L) LEAVE ALL", 15);
    y = town_pane_text(g, y, "J) JEWELS ONLY P) PL AND JWL", 15);
    y = town_pane_text(g, y, "G) GLD,PL,JWL  I) I,G,PL,JWL", 15);
    town_pane_text(g, y, "NOTE - SORTING TAKES TIME", 8);
    video_present(&g->video);

    int key;
    for (;;) {
        key = input_wait_any_key(&g->input);
        if (input_poll_quit(&g->input) || key == 0x1B) key = 'L';
        if (key == INPUT_MOUSE_CLICK) {
            static const char click_key[3][2] = {
                {'A', 'L'}, {'J', 'P'}, {'G', 'I'}
            };
            int x, click_y;
            key = 0;
            if (game_mouse_click_logical(g, &x, &click_y) &&
                x >= 0 && x < SX(0x2D3) && click_y >= choice_y &&
                click_y < choice_y + SY(38) * 3) {
                int row = (click_y - choice_y) / SY(38);
                key = click_key[row][x >= SX(0x2D3) / 2];
            }
        }
        if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
        if (strchr("IGPAJL", key)) break;
    }
    reward_take_stones(p, pile, key);
    town_view_money(g, p);
}

static void reward_health_cup(Game *g, Character *p) {
    if (p->class_id == CLASS_MONK) return;
    if (game_rand(g) % 5 != 0 || mw_hp_cur(p) >= mw_hp_max(p)) return;
    int gain = 3 + game_rand(g) % 11;
    if (g->cur_floor > 6) gain += game_rand(g) % 4;
    uint64_t hp = (uint64_t)mw_hp_cur(p) + (unsigned)gain;
    mw_set_hp_cur(p, hp > mw_hp_max(p) ? mw_hp_max(p) : hp);
    town_message(g, p, "YOU FOUND A CUP OF HEALTH!",
                 "YOU DRINK THE WONDERFUL LIQUID",
                 "AND GAIN A FEW HEALTH POINTS.", 10);
}

static void reward_spell_orb(Game *g, Character *p) {
    if (game_rand(g) % 7 != 0 || p->sp_max <= 0.0f || p->sp_cur >= p->sp_max)
        return;
    p->sp_cur += 1.0f;
    if (p->sp_cur > p->sp_max) p->sp_cur = p->sp_max;
    town_message(g, p, "YOU FOUND A SHIMMERING BALL OF THOUGHT!",
                 "YOU ABSORB THE ENERGY AND GAIN",
                 "A SPELL POINT.", 11);
}

static void reward_find_pause(Game *g, Character *p) {
    /* WORLD func_21F9C calls far_022A2(0x2EE), then shows only
       "YOU FIND...", and calls far_022A2(0xBB8) before clearing the pane
       for the actual result: 750 ms of lead-in plus a 3000 ms reveal hold. */
    game_delay(g, 0x2EE);
    town_pane_begin(g, p);
    town_pane_text(g, 0, "YOU FIND...", 8);
    video_present(&g->video);
    game_delay(g, 0xBB8);
}

static void reward_misc_stage(Game *g, Character *p, int depth) {
    if (p->class_id == CLASS_MONK) return;
    if (reward_random_below(g, 950) >= depth + 40 ||
        reward_random_below(g, 20) >= depth) return;
    reward_find_pause(g, p);
    if (game_rand(g) & 1) {
        town_message(g, p, "NOTHING!", "", "", 8);
        return;
    }
    char loot[160];
    award_random_magic_item(g, p, loot, sizeof(loot));
    town_message(g, p, loot, "HIT 'I' TO USE MAGIC ITEMS.", "", 14);
}

static void reward_spell_item_stage(Game *g, Character *p, int depth) {
    int source = game_rand(g) % 3;
    if (p->class_id == CLASS_MONK ||
        ((source == 0 || source == 1) && p->class_id == CLASS_FIGHTER))
        return;
    int rarity = 350 - depth;
    if (rarity < 1) rarity = 1;
    if (reward_random_below(g, rarity) > 15) return;
    char loot[160];
    award_spell_item(g, p, depth, source, loot, sizeof(loot));
    const char *kind = source == 0 ? "SCROLL" : (source == 1 ? "WAND" : "SPELL PAPER");
    char description[96];
    snprintf(description, sizeof(description),
             "HIT 'I' TO READ OR CAST THE %s.", kind);
    town_message(g, p, loot, "THE SPELL HAS BEEN ADDED TO YOUR INVENTORY.",
                 description, 14);
}

/* MW_EXTENSION: super-rare Enhanced relics are an additional final random
 * treasure stage.  They never replace or reorder WORLD's original rewards,
 * never duplicate, and cannot enter a Classic character's inventory. */
static void reward_relic_stage(Game *g, Character *p, int depth,
                               const CombatState *cs) {
    if (mw_experience_mode(p) != MW_EXPERIENCE_ENHANCED || depth < 350)
        return;
    int eligible[MW_RELIC_COUNT];
    int eligible_count = 0;
    for (int relic = 0; relic < MW_RELIC_COUNT; relic++)
        if (depth >= enhanced_relics[relic].minimum_floor &&
            !mw_relic_owned(p, relic))
            eligible[eligible_count++] = relic;
    if (!eligible_count) return;

    /* Roughly one roll in 4,200 at the first eligible floor, improving to
       one in ~2,250 at floor 1,000.  Quest bosses receive six rolls' worth
       of opportunity but still do not guarantee a relic. */
    int rarity = 4200 - (depth - 350) * 3;
    if (rarity < 1800) rarity = 1800;
    if (monster_types[cs->monster_type_idx].boss) {
        rarity /= 6;
        if (rarity < 250) rarity = 250;
    }
    if (reward_random_below(g, rarity) != 0) return;

    int relic = eligible[reward_random_below(g, eligible_count)];
    if (!grant_enhanced_relic(p, relic)) return;
    town_message(g, p, "YOU FOUND A SUPER-RARE RELIC!",
                 enhanced_relics[relic].name,
                 enhanced_relics[relic].effect, 11);
}

static void reward_late_gear_stage(Game *g, Character *p, int depth) {
    /* Boss-linked tiers are awarded by grant_quest_reward.  The two
       intermediate forge caches fill the progression gaps at 825 and 950. */
    static const int forge_tier[2] = {4, 6};
    for (int i = 0; i < 2; i++) {
        int tier = forge_tier[i];
        if (depth < late_gear_floor[tier] || !grant_late_gear(p, tier))
            continue;
        char gear[128];
        snprintf(gear, sizeof(gear), "%s AND %s UNLOCKED!",
                 weapon_stats[12 + tier].name,
                 combat_armor_name(8 + tier));
        town_message(g, p, "YOU DISCOVER A DEEP FORGE CACHE!", gear,
                     "HIT 'W' OR 'A', THEN PAGE DOWN, TO EQUIP THEM.", 11);
    }
}

static void reward_deep_spell_stage(Game *g, Character *p, int depth) {
    if (mw_experience_mode(p) != MW_EXPERIENCE_ENHANCED) return;
    int first = -1;
    int last = -1;
    for (int deep = 0; deep < MW_DEEP_SPELL_COUNT; deep++) {
        if (depth < deep_spell_unlock_floor[deep] ||
            (mw_deep_spell_unlocks(p) & (1u << deep)))
            continue;
        mw_unlock_deep_spell_tier(p, deep);
        if (first < 0) first = deep;
        last = deep;
    }
    if (first < 0) return;
    char line[128];
    if (first == last)
        snprintf(line, sizeof(line),
                 "DEEP SPELL TIER %d (LEVEL %d MAGIC) UNLOCKED!",
                 first + 1, (MW_DEEP_SPELL_FIRST + first) / 3 + 1);
    else
        snprintf(line, sizeof(line),
                 "DEEP SPELL TIERS %d THROUGH %d UNLOCKED!",
                 first + 1, last + 1);
    town_message(g, p, "YOUR JOURNEY REVEALS DEEPER MAGIC!",
                 line, "ALL FOUR SPELL FAMILIES HAVE GROWN.", 11);
}

static int battle_reward_experience(const Character *p,
                                    const CombatState *cs) {
    const MonsterType *mt = &monster_types[cs->monster_type_idx];
    int64_t value = (int64_t)cs->monster_level * mt->hpF + mt->atk + mt->def;
    if (value < 1) value = 1;
    if (mw_relic_owned(p, MW_RELIC_SAGE_PRISM))
        value += (value + 3) / 4;
    return value > INT_MAX ? INT_MAX : (int)value;
}

static void grant_battle_rewards(Game *g, Character *p, const CombatState *cs) {
    int xp = battle_reward_experience(p, cs);
    if (!isfinite(p->experience) || p->experience < 0.0) p->experience = 0.0;
    p->experience += (double)xp;
    int depth = g->cur_floor;
    if (depth < 0) depth = 0;

    /* Exact post-kill stage order from WORLD func_21F9C. */
    reward_show_kill(g, p, xp);
    reward_pill(g, p, depth, cs);
    reward_key(g, p);
    reward_offer_armor(g, p, cs);
    RewardStones pile;
    if (reward_make_stone_pile(g, cs->monster_level, depth, &pile))
        reward_offer_stones(g, p, &pile);
    reward_health_cup(g, p);
    reward_spell_orb(g, p);
    reward_misc_stage(g, p, depth);
    reward_spell_item_stage(g, p, depth);
    reward_relic_stage(g, p, depth, cs);
    reward_deep_spell_stage(g, p, depth);
    reward_late_gear_stage(g, p, depth);

    char quest[128] = "";
    p->floor_depth = (u16)g->cur_floor;
    grant_quest_reward(p, cs, quest, sizeof(quest));
    if (*quest) town_message(g, p, quest, "", "", 10);
}

int game_economy_self_test(void) {
    int failures = 0;
    Character p = {0};
    p.copper_stones = 400;
    p.silver_stones = 24;
    p.ivory_stones = 8;
    p.gold_stones = 4;
    p.platinum_stones = 2;
    p.jewel_stones = 3;
    unsigned long long converted = town_convert_stones(&p);
    if (converted != 21 || p.jewels_pocket != 21 || p.copper_stones ||
        p.silver_stones || p.ivory_stones || p.gold_stones ||
        p.platinum_stones || p.jewel_stones) failures++;

    u32 saturated = UINT32_MAX - 2;
    add_u32_sat(&saturated, 10);
    if (saturated != UINT32_MAX) failures++;

    Game g = {0};
    game_srand(&g, 1);
    int seen = 0;
    char loot[128];
    for (int i = 0; i < 4096 && seen != 0xFFF; i++)
        seen |= 1 << award_random_magic_item(&g, &p, loot, sizeof(loot));
    if (seen != 0xFFF) failures++;
    seen = 0;
    for (int source = 0; source < 3; source++)
        seen |= 1 << award_spell_item(&g, &p, 200, source,
                                     loot, sizeof(loot));
    if (seen != 0x7) failures++;
    memset(p.scrolls, 0, sizeof(p.scrolls));
    memset(p.wands, 0, sizeof(p.wands));
    memset(p.papers, 0, sizeof(p.papers));
    mw_set_experience_mode(&p, MW_EXPERIENCE_ENHANCED);
    for (int source = 0; source < 3; source++)
        for (int i = 0; i < 256; i++)
            award_spell_item(&g, &p, 1000, source,
                             loot, sizeof(loot));
    int deep_source_found[3] = {0, 0, 0};
    for (int category = 0; category < 4; category++)
        for (int spell = MW_DEEP_SPELL_FIRST;
             spell < MW_ENHANCED_SPELL_COUNT; spell++) {
            deep_source_found[0] |= p.scrolls[category][spell] != 0;
            deep_source_found[1] |= p.wands[category][spell] != 0;
            deep_source_found[2] |= p.papers[category][spell] != 0;
        }
    if (!deep_source_found[0] || !deep_source_found[1] ||
        !deep_source_found[2])
        failures++;
    memset(p.scrolls, 0, sizeof(p.scrolls));
    memset(p.wands, 0, sizeof(p.wands));
    memset(p.papers, 0, sizeof(p.papers));
    mw_set_experience_mode(&p, MW_EXPERIENCE_CLASSIC);
    for (int source = 0; source < 3; source++)
        for (int i = 0; i < 256; i++)
            award_spell_item(&g, &p, 1000, source,
                             loot, sizeof(loot));
    for (int category = 0; category < 4; category++)
        for (int spell = MW_DEEP_SPELL_FIRST;
             spell < MW_ENHANCED_SPELL_COUNT; spell++)
            if (p.scrolls[category][spell] ||
                p.wands[category][spell] ||
                p.papers[category][spell])
                failures++;

    RewardStones pile = {{200, 12, 4, 2, 1, 1}};
    unsigned long long pile_value = pile.amount[0] / 200u +
        pile.amount[1] / 12u + pile.amount[2] / 4u +
        pile.amount[3] / 2u + (unsigned long long)pile.amount[4] * 5u +
        pile.amount[5];
    if (pile_value != 10) failures++;
    Character stone_taker = {0};
    reward_take_stones(&stone_taker, &pile, 'G');
    if (stone_taker.copper_stones || stone_taker.silver_stones ||
        stone_taker.ivory_stones || stone_taker.gold_stones != 2 ||
        stone_taker.platinum_stones != 1 || stone_taker.jewel_stones != 1)
        failures++;
    memset(&stone_taker, 0, sizeof(stone_taker));
    reward_take_stones(&stone_taker, &pile, 'L');
    if (stone_taker.copper_stones || stone_taker.silver_stones ||
        stone_taker.ivory_stones || stone_taker.gold_stones ||
        stone_taker.platinum_stones || stone_taker.jewel_stones)
        failures++;

    {
        Character contract = {0};
        game_srand(&g, 0xA11CEu);
        contract.level = 1;
        u32 price = temple_contract_price(&g, &contract);
        if (price < 500u || price > 519u) failures++;
        contract.level = 60;
        price = temple_contract_price(&g, &contract);
        if (price < 30000u || price > 30019u) failures++;
        contract.level = 61;
        if (temple_contract_price(&g, &contract) != 500000u) failures++;
    }

    {
        Character growth_player = {0};
        const LevelGrowth expected[MW_CLASSIC_CLASS_COUNT] = {
            {35, 59, 0}, {15, 29, 22}, {14, 21, 3}, {13, 13, 13},
            {14, 20, 13}, {55, 95, 3}, {14, 23, 8}
        };
        growth_player.stat_con = 20;
        growth_player.stat_luck = 18;
        growth_player.stat_int = 24;
        growth_player.stat_wis = 21;
        for (int class_id = 0; class_id < MW_CLASSIC_CLASS_COUNT; class_id++) {
            growth_player.class_id = (u8)class_id;
            LevelGrowth actual = original_level_growth(&growth_player);
            if (actual.hp_base != expected[class_id].hp_base ||
                actual.hp_range != expected[class_id].hp_range ||
                actual.sp_gain != expected[class_id].sp_gain)
                failures++;
        }
    }

    Character leveler = {0};
    leveler.level = 1;
    leveler.experience = 32.0;
    leveler.hp_cur = 4;
    leveler.hp_max = 10;
    leveler.class_id = CLASS_FIGHTER;
    leveler.stat_luck = 10;
    leveler.stat_con = leveler.stat_int = leveler.stat_wis = 10;
    if (inn_apply_levels(&g, &leveler) != 1 || leveler.level != 2 ||
        mw_hp_max(&leveler) - mw_hp_cur(&leveler) != 6 ||
        leveler.sp_max != 0.0f || experience_for_level(3) != 243.0)
        failures++;

    for (int race = 0; race < RACE_COUNT; race++) {
        u16 stats[6];
        int bonus = 0;
        roll_character_stats(&g, race, stats);
        for (int i = 0; i < 6; i++)
            bonus += (int)stats[i] - race_stat_base[race][i];
        if (bonus != 60) failures++;
    }

    Character novice = {0};
    novice.stat_int = 24;
    novice.stat_wis = 18;
    novice.class_id = CLASS_FIGHTER;
    if (starting_spell_points(&novice) != 0.0f) failures++;
    novice.class_id = CLASS_WIZARD;
    if (starting_spell_points(&novice) != 9.0f) failures++;
    novice.class_id = CLASS_MONK;
    if (starting_spell_points(&novice) != 3.0f) failures++;
    {
        Character hero = {0};
        CombatState boss = {0};
        char reward[160] = "";
        mw_set_experience_mode(&hero, MW_EXPERIENCE_ENHANCED);
        hero.equipped_weapon = 1;
        hero.floor_depth = 375;
        boss.monster_type_idx = 112;
        boss.monster_level = 375;
        grant_quest_reward(&hero, &boss, reward, sizeof(reward));
        if (mw_weapon_enchant(&hero, 1) != 200 ||
            !(mw_quest_flags(&hero) & (1u << 8)) ||
            mw_weapon_inventory_count(&hero, 12) != 1 ||
            mw_armor_inventory_count(&hero, 8) != 1 ||
            !strstr(reward, "PLUS 200"))
            failures++;
        hero.floor_depth = 500;
        boss.monster_type_idx = 113;
        boss.monster_level = 500;
        grant_quest_reward(&hero, &boss, reward, sizeof(reward));
        if (mw_weapon_enchant(&hero, 1) != 300 ||
            !(mw_quest_flags(&hero) & (1u << 9)) ||
            mw_weapon_inventory_count(&hero, 13) != 1 ||
            mw_armor_inventory_count(&hero, 9) != 1 ||
            !strstr(reward, "PLUS 300"))
            failures++;
        static const int deep_floor[4] = {625, 750, 875, 1000};
        static const int deep_type[4] = {174, 175, 176, 177};
        static const int deep_orb[4] = {450, 600, 800, 1000};
        static const int deep_gear_tier[4] = {2, 3, 5, 7};
        for (int i = 0; i < 4; i++) {
            hero.floor_depth = (u16)deep_floor[i];
            boss.monster_type_idx = deep_type[i];
            boss.monster_level = deep_floor[i];
            grant_quest_reward(&hero, &boss, reward, sizeof(reward));
            char amount[16];
            snprintf(amount, sizeof(amount), "PLUS %d", deep_orb[i]);
            if (mw_weapon_enchant(&hero, 1) != deep_orb[i] ||
                !(mw_quest_flags(&hero) & (1u << (10 + i))) ||
                mw_weapon_inventory_count(
                    &hero, 12 + deep_gear_tier[i]) != 1 ||
                mw_armor_inventory_count(
                    &hero, 8 + deep_gear_tier[i]) != 1 ||
                !strstr(reward, amount))
                failures++;
        }
        for (int deep = 0; deep < MW_DEEP_SPELL_COUNT; deep++)
            mw_unlock_deep_spell_tier(&hero, deep);
        if (mw_deep_spell_unlocks(&hero) !=
            (u16)((1u << MW_DEEP_SPELL_COUNT) - 1u))
            failures++;
        for (int category = 0; category < 4; category++)
            if (!hero.spells[category][MW_ENHANCED_SPELL_COUNT - 1])
                failures++;
    }
    {
        Character final_hero = {0};
        mw_set_experience_mode(&final_hero, MW_EXPERIENCE_ENHANCED);
        if (grant_final_gear(&final_hero, 949) ||
            mw_weapon_inventory_count(&final_hero, 18) ||
            mw_armor_inventory_count(&final_hero, 14))
            failures++;
        if (!grant_final_gear(&final_hero, 950) ||
            mw_weapon_inventory_count(&final_hero, 18) != 1 ||
            mw_armor_inventory_count(&final_hero, 14) != 1 ||
            !(mw_quest_flags(&final_hero) & MW_FINAL_GEAR_QUEST_FLAG) ||
            grant_final_gear(&final_hero, 1000))
            failures++;
        Character classic_final = {0};
        mw_set_experience_mode(&classic_final, MW_EXPERIENCE_CLASSIC);
        if (grant_final_gear(&classic_final, 1000) ||
            mw_weapon_inventory_count(&classic_final, 18) ||
            mw_armor_inventory_count(&classic_final, 14))
            failures++;
    }
    {
        Character relic_hero = {0};
        CombatState target = {0};
        target.monster_type_idx = 0;
        target.monster_level = 400;
        int plain_xp = battle_reward_experience(&relic_hero, &target);
        mw_set_experience_mode(&relic_hero, MW_EXPERIENCE_ENHANCED);
        for (int relic = 0; relic < MW_RELIC_COUNT; relic++) {
            if (!grant_enhanced_relic(&relic_hero, relic) ||
                grant_enhanced_relic(&relic_hero, relic))
                failures++;
        }
        if (mw_relic_count(&relic_hero) != MW_RELIC_COUNT ||
            battle_reward_experience(&relic_hero, &target) !=
                plain_xp + (plain_xp + 3) / 4)
            failures++;
        Character classic_relic = {0};
        mw_set_experience_mode(&classic_relic, MW_EXPERIENCE_CLASSIC);
        if (grant_enhanced_relic(&classic_relic,
                                 MW_RELIC_ARCANE_RING) ||
            mw_relic_count(&classic_relic))
            failures++;
        for (int relic = 1; relic < MW_RELIC_COUNT; relic++)
            if (enhanced_relics[relic].minimum_floor <=
                enhanced_relics[relic - 1].minimum_floor)
                failures++;
    }
    failures += character_creation_self_test();
    failures += game_weight_self_test();

    printf("Economy/reward/creation self-test: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

int game_weight_self_test(void) {
    int failures = 0;
    Character p = {0};
    p.weight_pounds = 130;
    p.stat_agi = 20;
    p.copper_stones = 1600;       /* 100 pounds */
    p.silver_stones = 15;         /* remainder does not combine */
    p.jewel_stones = UINT32_MAX;  /* deliberately weightless */
    p.weapon_inventory[1] = 2;    /* two 4-pound sticks */
    p.armor_inventory[1] = 1;     /* 14-pound leather armor */
    if (game_loaded_weight(&p) != 252u ||
        game_weight_monster_turns(&p) != 2)
        failures++;

    p.eff_feather = 1;
    if (game_loaded_weight(&p) != 122u ||
        game_weight_monster_turns(&p) != 1)
        failures++;

    p.eff_feather = 100;
    if (game_loaded_weight(&p) != 122u) failures++;
    p.copper_stones = UINT32_MAX;
    p.stat_agi = 0;
    if (game_weight_monster_turns(&p) != 8) failures++;

    return failures;
}

/* MW_EXTENSION: the bestiary has no WORLD.EXE counterpart. It uses original
 * monster records and WORLD.PIC assets but keeps its own discovery save. */
/* ── Beastiary ───────────────────────────────────────────────────────
 *
 * J was unused by the DOS command set (and reads naturally as monster
 * journal), so it opens a persistent record of every graphics-backed random
 * monster and quest boss, including the native floor-375..1000 bosses. */

static void bestiary_floor_text(int type, char *out, size_t out_size) {
    const MonsterType *mt = &monster_types[type];
    int quest_step = quest_step_for_type(type);
    if (quest_step >= 0) {
        snprintf(out, out_size, "FLOOR: %u (QUEST)",
                 quest_floor_by_step[quest_step]);
        return;
    }
    int min_floor = mt->minL < 1 ? 1 : mt->minL;
    int max_floor = combat_monster_max_floor(type);
    if (type < DEEP_MONSTER_FIRST && max_floor > CLASSIC_DUNGEON_FLOOR)
        max_floor = CLASSIC_DUNGEON_FLOOR;
    if (min_floor > max_floor)
        snprintf(out, out_size, "FLOORS: NOT IN RANDOM SPAWNS");
    else if (min_floor == max_floor)
        snprintf(out, out_size, "FLOOR: %d", min_floor);
    else
        snprintf(out, out_size, "FLOORS: %d-%d", min_floor, max_floor);
}

typedef struct BestiaryAverages {
    int minimum_level;
    int maximum_level;
    double average_level;
    double average_hp;
    int maximum_hp_at_average_level;
} BestiaryAverages;

/* MON.MAP generation rolls floor-2 through floor+2 (clamped at level one),
   then rolls HP uniformly from one through that level's HP cap.  Reporting
   those actual distributions is more useful than exposing only hpF. */
static BestiaryAverages bestiary_average_stats(int type) {
    BestiaryAverages result = {INT_MAX, 0, 0.0, 0.0, 0};
    const MonsterType *mt = &monster_types[type];
    int quest_step = quest_step_for_type(type);
    int first_floor = quest_step >= 0 ? quest_floor_by_step[quest_step] :
                      (mt->minL < 1 ? 1 : mt->minL);
    int last_floor = quest_step >= 0 ? first_floor :
                     combat_monster_max_floor(type);
    if (quest_step < 0 && type < DEEP_MONSTER_FIRST &&
        last_floor > CLASSIC_DUNGEON_FLOOR)
        last_floor = CLASSIC_DUNGEON_FLOOR;
    uint64_t level_total = 0;
    double hp_total = 0.0;
    uint64_t samples = 0;
    if (last_floor < first_floor) last_floor = first_floor;
    for (int floor = first_floor; floor <= last_floor; floor++)
        for (int jitter = -2; jitter <= 2; jitter++) {
            int level = floor + jitter;
            if (level < 1) level = 1;
            int hp_cap = combat_calc_monster_hp(mt, level);
            if (level < result.minimum_level) result.minimum_level = level;
            if (level > result.maximum_level) result.maximum_level = level;
            level_total += (unsigned)level;
            hp_total += ((double)hp_cap + 1.0) / 2.0;
            samples++;
        }
    if (!samples) samples = 1;
    result.average_level = (double)level_total / (double)samples;
    result.average_hp = hp_total / (double)samples;
    int rounded_level = (int)(result.average_level + 0.5);
    result.maximum_hp_at_average_level =
        combat_calc_monster_hp(mt, rounded_level);
    if (result.minimum_level == INT_MAX) result.minimum_level = 1;
    return result;
}

static void draw_bestiary_fullscreen(Game *g, int type) {
    Video *v = &g->video;
    const MonsterType *mt = &monster_types[type];
    int adv = v->font_advance ? v->font_advance : v->font_char_w;
    int title_x = (LOGICAL_W - (int)strlen(mt->name) * adv) / 2;
    int pic = get_monster_pic_index_ext(type);
    if (pic < 2) return;

    video_clear(v, 0);
    video_draw_text(v, title_x > 4 ? title_x : 4, 5, mt->name, 14);
    draw_pic_billboard(g, pic, LOGICAL_W / 2,
                       42, LOGICAL_H - 104, 0.0f,
                       0, 0, LOGICAL_W, LOGICAL_H, NULL,
                       get_monster_color_ext(type),
                       get_monster_tint_ext(type));
    video_draw_text_scaled(v, 12, LOGICAL_H - 36,
                           "FULLSCREEN MONSTER VIEW - PRESS ANY KEY", 15,
                           3, 4);
    video_present(v);
    input_wait_any_key(&g->input);
}

static void draw_bestiary_page(Game *g, int selected) {
    enum { ROWS_PER_PAGE = 18, LIST_RIGHT = 408 };
    Video *v = &g->video;
    int page = selected / ROWS_PER_PAGE;
    int first = page * ROWS_PER_PAGE;
    int discovered = 0;
    int catalog_count = bestiary_mode_catalog_count(g);
    char line[160];

    for (int i = 0; i < catalog_count; i++) {
        int type = bestiary_type_at_mode_catalog_index(g, i);
        if (type >= 0 &&
            (g->bestiary_unlock_all || g->bestiary_kills[type])) discovered++;
    }

    video_clear(v, 0);
    video_draw_text(v, 10, 4, "BEASTIARY", 4);
    snprintf(line, sizeof(line), "DISCOVERED: %d OF %d    PAGE %d OF %d",
             discovered, catalog_count, page + 1,
             (catalog_count + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);
    video_draw_text_scaled(v, 205, 9, line, 8, 3, 4);
    video_hline(v, 4, 41, LOGICAL_W - 8, 8);
    video_vline(v, LIST_RIGHT, 42, LOGICAL_H - 84, 8);

    for (int row = 0; row < ROWS_PER_PAGE; row++) {
        int entry = first + row;
        if (entry >= catalog_count) break;
        int type = bestiary_type_at_mode_catalog_index(g, entry);
        if (type < 0) break;
        int y = 53 + row * 35;
        int unlocked = g->bestiary_unlock_all || g->bestiary_kills[type] != 0;
        if (entry == selected)
            video_fill_rect(v, 7, y - 3, LIST_RIGHT - 15, 31, 1);
        if (unlocked)
            snprintf(line, sizeof(line), "%03d  %s", entry + 1,
                     monster_types[type].name);
        else
            snprintf(line, sizeof(line), "%03d  ????", entry + 1);
        video_draw_text_scaled(v, 13, y, line,
                               entry == selected ? 15 : (unlocked ? 7 : 8),
                               3, 4);
    }

    int selected_type = bestiary_type_at_mode_catalog_index(g, selected);
    int unlocked = selected_type >= 0 &&
                   (g->bestiary_unlock_all ||
                    g->bestiary_kills[selected_type] != 0);
    if (!unlocked) {
        video_draw_text(v, 610, 110, "????", 8);
        video_draw_text_scaled(v, 470, 190,
                               "THIS MONSTER HAS NOT BEEN DEFEATED.", 7, 3, 4);
        video_draw_text_scaled(v, 470, 225,
                               "ITS IMAGE, NAME AND STATISTICS", 7, 3, 4);
        video_draw_text_scaled(v, 470, 260,
                               "WILL BE REVEALED AFTER YOUR FIRST KILL.", 7,
                               3, 4);
    } else {
        const MonsterType *mt = &monster_types[selected_type];
        int pic = get_monster_pic_index_ext(selected_type);
        video_draw_text(v, 430, 48, mt->name, 14);
        draw_pic_billboard(g, pic, 720, 80, 300, 0.0f,
                           LIST_RIGHT + 1, 42, LOGICAL_W - LIST_RIGHT - 1,
                           350, NULL, get_monster_color_ext(selected_type),
                           get_monster_tint_ext(selected_type));

        BestiaryAverages average = bestiary_average_stats(selected_type);
        int y = 375;
        const int step = 25;
        bestiary_floor_text(selected_type, line, sizeof(line));
        video_draw_text_scaled(v, 430, y, line, 10, 3, 4); y += step;
        snprintf(line, sizeof(line), "KILLED: %u",
                 g->bestiary_kills[selected_type]);
        video_draw_text_scaled(v, 430, y, line, 15, 3, 4); y += step;
        snprintf(line, sizeof(line), "AVERAGE LEVEL: %.1f",
                 average.average_level);
        video_draw_text_scaled(v, 430, y, line, 11, 3, 4); y += step;
        snprintf(line, sizeof(line), "LEVEL RANGE: %d-%d",
                 average.minimum_level, average.maximum_level);
        video_draw_text_scaled(v, 430, y, line, 11, 3, 4); y += step;
        snprintf(line, sizeof(line), "AVERAGE SPAWN HP: %.1f",
                 average.average_hp);
        video_draw_text_scaled(v, 430, y, line, 10, 3, 4); y += step;
        snprintf(line, sizeof(line), "MAX HP AT AVG LEVEL: %d",
                 average.maximum_hp_at_average_level);
        video_draw_text_scaled(v, 430, y, line, 10, 3, 4); y += step;
        snprintf(line, sizeof(line), "DEFENSE: %d       ATTACK: %d",
                 mt->def, mt->atk);
        video_draw_text_scaled(v, 430, y, line, 7, 3, 4); y += step;
        snprintf(line, sizeof(line), "DAMAGE: %d        AGILITY: %d",
                 mt->dmg, mt->agi);
        video_draw_text_scaled(v, 430, y, line, 7, 3, 4); y += step;
        snprintf(line, sizeof(line), "DEFENSE MOD: %d   HP FACTOR: %d",
                 mt->defMod, mt->hpF);
        video_draw_text_scaled(v, 430, y, line, 7, 3, 4); y += step;
        snprintf(line, sizeof(line), "IMMUNITY: %u      SAVES: %u / %u",
                 mt->imm, mt->saveA, mt->saveB);
        video_draw_text_scaled(v, 430, y, line, 7, 3, 4); y += step;
        int drain = combat_monster_drain_amount(selected_type);
        snprintf(line, sizeof(line), "BOSS: %s       LEVEL DRAIN: %d",
                 mt->boss ? "YES" : "NO", drain);
        video_draw_text_scaled(v, 430, y, line, mt->boss ? 12 : 7, 3, 4);
        y += step;
        int spell_chance = combat_monster_spell_chance(selected_type);
        if (spell_chance)
            snprintf(line, sizeof(line), "MAGIC: %s (1 IN %d RESPONSES)",
                     combat_monster_spell_name(selected_type), spell_chance);
        else
            snprintf(line, sizeof(line), "MAGIC: NONE");
        video_draw_text_scaled(v, 430, y, line,
                               spell_chance ? 3 : 7, 3, 4);
    }

    video_hline(v, 4, LOGICAL_H - 42, LOGICAL_W - 8, 8);
    video_draw_text_scaled(v, 10, LOGICAL_H - 34,
                           "UP/DOWN SELECT  PGUP/PGDN PAGE  F FULLSCREEN  ESC RETURN",
                           15, 3, 4);
}

void game_draw_bestiary_test(Game *g, int selected) {
    int catalog_index = bestiary_mode_catalog_index_for_type(g, selected);
    if (catalog_index < 0) catalog_index = 0;
    draw_bestiary_page(g, catalog_index);
}

static int bestiary_move_selection(int selected, int scan, int count) {
    if (count < 1) return 0;
    switch (scan) {
    case 0x48:
        return selected > 0 ? selected - 1 : count - 1;
    case 0x50:
        return selected + 1 < count ? selected + 1 : 0;
    case 0x49:
    case 0x4B:
        return selected >= 18 ? selected - 18 : count - 1;
    case 0x51:
    case 0x4D:
        return selected + 18 < count ? selected + 18 : 0;
    case 0x47:
        return 0;
    case 0x4F:
        return count - 1;
    default:
        return selected;
    }
}

static int game_door_encounter_self_test(Game *g) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dy[4] = {-1, 1, 0, 0};
    int failures = 0;
    int door_x = -1, door_y = -1, door_d = -1;
    int secret_x = -1, secret_y = -1, secret_d = -1;
    int open_x = -1, open_y = -1, open_d = -1;
    int saved_floor = g->cur_floor;
    int saved_x = g->cur_x, saved_y = g->cur_y;
    int saved_noclip = g->cheat_noclip;
    int saved_loaded = g->monster_map_loaded;
    int saved_layer = g->monster_layer;
    int test_layer = 0;
    MonsterRecord saved_map[MONSTERS_PER_FLOOR];

    memcpy(saved_map, g->monster_map[test_layer], sizeof(saved_map));
    g->cur_floor = 1;
    for (int y = 1; y < MAP_H - 1 &&
                        (door_d < 0 || secret_d < 0 || open_d < 0); y++) {
        for (int x = 1; x < MAP_W - 1 &&
                            (door_d < 0 || secret_d < 0 || open_d < 0); x++) {
            for (int d = 0; d < 4; d++) {
                int edge = edge_between_cells(g, x, y, x + dx[d], y + dy[d]);
                if (edge == 1 && door_d < 0) {
                    door_x = x; door_y = y; door_d = d;
                } else if (edge == 2 && secret_d < 0) {
                    secret_x = x; secret_y = y; secret_d = d;
                } else if (edge == 3 && open_d < 0) {
                    open_x = x; open_y = y; open_d = d;
                }
            }
        }
    }
    if (race_stat_base[RACE_DRAGONKIN][0] != 18 ||
        race_stat_base[RACE_DRAGONKIN][3] != 17 ||
        race_stat_base[RACE_DRAGONKIN][1] >= race_stat_base[RACE_HUMAN][1] ||
        race_stat_base[RACE_DRAGONKIN][4] >= race_stat_base[RACE_HUMAN][4] ||
        race_stat_base[RACE_CELESTIAL][1] != 17 ||
        race_stat_base[RACE_CELESTIAL][2] != 18 ||
        race_stat_base[RACE_CELESTIAL][0] >= race_stat_base[RACE_HUMAN][0] ||
        race_stat_base[RACE_CELESTIAL][3] >= race_stat_base[RACE_HUMAN][3] ||
        race_stat_base[RACE_CELESTIAL][5] >= race_stat_base[RACE_HUMAN][5])
        failures++;

    memset(g->monster_map[test_layer], 0,
           sizeof(g->monster_map[test_layer]));
    g->monster_map_loaded = 1;
    g->monster_layer = test_layer;
    g->cheat_noclip = 0;
    if (door_d < 0 || secret_d < 0 || open_d < 0) {
        failures++;
    } else {
        MonsterRecord *m = &g->monster_map[test_layer][0];
        g->cur_x = door_x; g->cur_y = door_y;
        m->x = (u8)(door_x + dx[door_d]);
        m->y = (u8)(door_y + dy[door_d]);
        monster_record_set_hp(m, 1);
        m->type = 0; m->level = 1;
        if (!game_can_move(g, door_x, door_y, m->x, m->y) ||
            game_find_monster(g, m->x, m->y) != 0 ||
            game_find_engaged_monster_in_direction(g, door_d) >= 0 ||
            game_find_adjacent_monster(g) >= 0 ||
            actor_cell_visible(g, m->x, m->y))
            failures++;

        /* Edge 2 is WORLD's visually hidden, auto-opening secret door.  It
         * permits an empty move but, like a visible door, hides a monster and
         * makes that occupied doorway jam instead of starting combat. */
        g->cur_x = secret_x; g->cur_y = secret_y;
        m->x = (u8)(secret_x + dx[secret_d]);
        m->y = (u8)(secret_y + dy[secret_d]);
        if (!game_can_move(g, secret_x, secret_y, m->x, m->y) ||
            game_find_monster(g, m->x, m->y) != 0 ||
            game_find_engaged_monster_in_direction(g, secret_d) >= 0 ||
            game_find_adjacent_monster(g) >= 0 ||
            actor_cell_visible(g, m->x, m->y))
            failures++;

        g->cur_x = open_x; g->cur_y = open_y;
        m->x = (u8)(open_x + dx[open_d]);
        m->y = (u8)(open_y + dy[open_d]);
        if (!game_can_move(g, open_x, open_y, m->x, m->y) ||
            game_find_engaged_monster_in_direction(g, open_d) != 0 ||
            game_find_adjacent_monster(g) != 0 ||
            !actor_cell_visible(g, m->x, m->y))
            failures++;
        Character walker;
        memset(&walker, 0, sizeof(walker));
        mw_set_hp_max(&walker, 100000);
        mw_set_hp_cur(&walker, 100000);
        if (game_try_step(g, &walker, open_d) != 0 ||
            g->cur_x != open_x || g->cur_y != open_y ||
            game_monster_hp(g, 0) != 1)
            failures++;

        /* Noclip has no actor or outer-edge blockers.  Crossing a finite map
           edge wraps rather than producing invalid saved coordinates. */
        g->cheat_noclip = 1;
        g->cur_x = open_x; g->cur_y = open_y;
        m->x = (u8)(open_x + dx[open_d]);
        m->y = (u8)(open_y + dy[open_d]);
        int noclip_target_x = m->x, noclip_target_y = m->y;
        monster_record_set_hp(m, 1);
        if (game_try_step(g, &walker, open_d) <= 0 ||
            g->cur_x != noclip_target_x || g->cur_y != noclip_target_y)
            failures++;
        clear_monster_record(m);
        g->cur_x = 0; g->cur_y = 1;
        if (game_try_step(g, &walker, 2) <= 0 ||
            g->cur_x != MAP_W - 1 || g->cur_y != 1)
            failures++;
    }

    memcpy(g->monster_map[test_layer], saved_map, sizeof(saved_map));
    g->monster_map_loaded = saved_loaded;
    g->monster_layer = saved_layer;
    g->cur_floor = saved_floor;
    g->cur_x = saved_x; g->cur_y = saved_y;
    g->cheat_noclip = saved_noclip;
    return failures;
}

static int game_dig_self_test(Game *g) {
    int failures = 0;
    int saved_floor = g->cur_floor;
    int saved_x = g->cur_x, saved_y = g->cur_y;
    int saved_max_floor = g->dungeon_max_floor;
    DigLanding landing;

    g->dungeon_max_floor = MAX_DUNGEON_FLOOR;
    const GameTraversalRules *rules = game_traversal_rules(g);
    if (rules->max_floor != 1000 ||
        rules->prep_ascend_max_floor != 260 ||
        rules->prep_descend_max_floor != 492 ||
        rules->prep_major_descend_cap != 300 ||
        rules->dig_max_floor != 480 ||
        rules->dig_reverse_floor != 496 ||
        rules->dig_direction_floor != 600 ||
        rules->dig_slow_floor != 64 ||
        rules->dig_search_attempts != 520 ||
        game_clamp_dungeon_floor(g, -1) != 0 ||
        game_clamp_dungeon_floor(g, 1001) != 1000)
        failures++;
    g->cur_floor = 0;
    g->cur_x = 40;
    g->cur_y = 55;
    if (!find_dig_landing(g, &landing) || landing.floor == g->cur_floor ||
        rock_cell_at(g, landing.x, landing.y, landing.floor))
        failures++;

    if (!dig_depth_allowed(g, rules->dig_max_floor) ||
        dig_depth_allowed(g, rules->dig_max_floor + 1))
        failures++;

    g->cur_floor = rules->dig_max_floor;
    if (!find_dig_landing(g, &landing) || landing.floor < 1 ||
        landing.floor > rules->dig_reverse_floor ||
        rock_cell_at(g, landing.x, landing.y, landing.floor))
        failures++;

    g->dungeon_max_floor = CLASSIC_DUNGEON_FLOOR;
    rules = game_traversal_rules(g);
    if (rules->max_floor != 250 ||
        rules->prep_ascend_max_floor != 65 ||
        rules->prep_descend_max_floor != 123 ||
        rules->prep_major_descend_cap != 75 ||
        rules->dig_max_floor != 120 ||
        rules->dig_reverse_floor != 124 ||
        rules->dig_direction_floor != 150 ||
        rules->dig_slow_floor != 16 ||
        rules->dig_search_attempts != 130 ||
        game_clamp_dungeon_floor(g, -1) != 0 ||
        game_clamp_dungeon_floor(g, 250) != 250 ||
        game_clamp_dungeon_floor(g, 251) != 250 ||
        !dig_depth_allowed(g, 120) || dig_depth_allowed(g, 121))
        failures++;
    g->cur_floor = 120;
    if (!find_dig_landing(g, &landing) || landing.floor < 1 ||
        landing.floor > 124 ||
        rock_cell_at(g, landing.x, landing.y, landing.floor))
        failures++;

    g->cur_floor = saved_floor;
    g->cur_x = saved_x; g->cur_y = saved_y;
    g->dungeon_max_floor = saved_max_floor;
    return failures;
}

/* MW_PORT: WORLD use_item/0x0F4E7.  Despite its decompiler name this is not
 * the inventory command: the map renderer calls it for entity records
 * 0x68..0x6F and it prints a compass hint toward the living quest monster. */
static int quest_compass_direction(int player_x, int player_y,
                                   int target_x, int target_y) {
    int dx = player_x - target_x;
    int dy = player_y - target_y;
    int abs_dx = dx < 0 ? -dx : dx;
    int abs_dy = dy < 0 ? -dy : dy;
    if (abs_dx > abs_dy) return dx > 0 ? 2 : 3; /* west/east */
    return dy > 0 ? 0 : 1;                     /* north/south */
}

static void draw_quest_compass_hint(Game *g) {
    static const char *const hint[4] = {
        "GO NORTH", "GO SOUTH", "GO WEST", "GO EAST"
    };
    if (!g->monster_map_loaded || g->monster_layer < 0) return;
    for (int i = 0; i < MONSTERS_PER_FLOOR; i++) {
        MonsterRecord *m = &g->monster_map[g->monster_layer][i];
        if (quest_step_for_type(m->type) < 0 || game_monster_hp(g, i) <= 0)
            continue;
        int direction = quest_compass_direction(g->cur_x, g->cur_y,
                                                m->x, m->y);
        video_draw_text_scaled_xy(&g->video, SX(0x4B0), SY(0x442),
                                  hint[direction], 4, 7, 6, 12, 17);
        return;
    }
}

/* Consume and apply the character-side portion of a raise-dead contract.
 * The caller performs the actual dungeon-floor switch after receiving the
 * validated return position.  Keeping this separate makes death recovery
 * testable without entering the interactive exploration loop. */
static int character_apply_raise_contract(Character *p,
                                          int *return_floor,
                                          int *return_x,
                                          int *return_y) {
    if (!p || p->raise_x == 0xFFFFu) return 0;

    int max_floor = mw_experience_mode(p) == MW_EXPERIENCE_CLASSIC ?
                    CLASSIC_DUNGEON_FLOOR : MAX_DUNGEON_FLOOR;
    *return_floor = p->raise_floor <= max_floor ? p->raise_floor : 0;
    *return_x = p->raise_x < MAP_W ? p->raise_x : MAP_W / 2;
    *return_y = p->raise_y < MAP_H ? p->raise_y : MAP_H / 2;

    p->raise_x = 0xFFFFu;
    if (p->stat_con > 1) p->stat_con--;
    character_clear_battle_effects(p);
    mw_set_hp_cur(p, mw_hp_max(p));
    p->sp_cur = p->sp_max;
    return 1;
}

static int game_death_recovery_self_test(void) {
    Character p = {0};
    int floor = -1, x = -1, y = -1;
    int failures = 0;

    p.hp_max = 123;
    p.sp_max = 45.0f;
    p.stat_con = 9;
    p.raise_floor = 77;
    p.raise_x = 12;
    p.raise_y = 34;
    p.eff_battle_str = 10;
    p.eff_hold_monster = 5;
    if (!character_apply_raise_contract(&p, &floor, &x, &y) ||
        floor != 77 || x != 12 || y != 34 ||
        p.raise_x != 0xFFFFu || p.stat_con != 8 ||
        mw_hp_cur(&p) != mw_hp_max(&p) || p.sp_cur != p.sp_max ||
        p.eff_battle_str != 0 || p.eff_hold_monster != 0)
        failures++;

    mw_set_hp_cur(&p, 0);
    if (character_apply_raise_contract(&p, &floor, &x, &y) ||
        mw_hp_cur(&p) != 0)
        failures++;

    memset(&p, 0, sizeof(p));
    p.hp_max = 20;
    p.sp_max = 10.0f;
    p.stat_con = 1;
    p.raise_floor = UINT16_MAX;
    p.raise_x = MAP_W + 10;
    p.raise_y = MAP_H + 10;
    if (!character_apply_raise_contract(&p, &floor, &x, &y) ||
        floor != 0 || x != MAP_W / 2 || y != MAP_H / 2 ||
        p.stat_con != 1)
        failures++;

    /* Raise contracts are another vertical traversal and must not allow a
     * corrupt or edited Classic save to re-enter an Enhanced-only floor. */
    memset(&p, 0, sizeof(p));
    mw_set_experience_mode(&p, MW_EXPERIENCE_CLASSIC);
    mw_set_hp_max(&p, 20);
    p.sp_max = 10.0f;
    p.raise_floor = 251;
    p.raise_x = 10;
    p.raise_y = 10;
    if (!character_apply_raise_contract(&p, &floor, &x, &y) || floor != 0)
        failures++;

    memset(&p, 0, sizeof(p));
    mw_set_experience_mode(&p, MW_EXPERIENCE_ENHANCED);
    mw_set_hp_max(&p, 20);
    p.sp_max = 10.0f;
    p.raise_floor = 777;
    p.raise_x = 10;
    p.raise_y = 10;
    if (!character_apply_raise_contract(&p, &floor, &x, &y) || floor != 777)
        failures++;

    return failures;
}

int game_ui_self_test(Game *g) {
    static const int expected_classic[12][2] = {
        {'b','m'}, {'w','v'}, {'z','c'}, {'i','x'},
        {'a','l'}, {'f','p'}, {'t','e'}, {'o','o'},
        {'j','g'}, {'1','1'}, {'2','2'}, {'q','h'}
    };
    static const int expected_enhanced[13][2] = {
        {'b','m'}, {'w','v'}, {'z','c'}, {'i','x'},
        {'a','l'}, {'f','p'}, {'t','e'}, {'o','o'},
        {'j','g'}, {'1','1'}, {'2','2'}, {'3','3'}, {'q','h'}
    };
    int failures = battle_simulator_self_test() + arena_self_test() +
                   input_self_test() + video_display_mode_self_test(&g->video);
    int catalog_count = 0;
    while (bestiary_type_at_catalog_index(catalog_count) >= 0)
        catalog_count++;
    if (catalog_count != BESTIARY_CATALOG_COUNT ||
        bestiary_catalog_index_for_type(6) >= 0 ||
        bestiary_catalog_index_for_type(112) != 107 ||
        bestiary_catalog_index_for_type(113) != 116 ||
        bestiary_catalog_index_for_type(174) != 129 ||
        bestiary_catalog_index_for_type(175) != 138 ||
        bestiary_catalog_index_for_type(176) != 151 ||
        bestiary_catalog_index_for_type(177) != 160 ||
        bestiary_type_at_catalog_index(128) != 145 ||
        bestiary_type_at_catalog_index(130) != 146 ||
        bestiary_type_at_catalog_index(159) != 173)
        failures++;
    {
        BestiaryAverages quest = bestiary_average_stats(112);
        BestiaryAverages ordinary = bestiary_average_stats(0);
        if (quest.minimum_level != 373 || quest.maximum_level != 377 ||
            quest.average_level != 375.0 ||
            quest.average_hp <= 0.0 ||
            quest.maximum_hp_at_average_level < quest.average_hp ||
            ordinary.minimum_level != 1 ||
            ordinary.maximum_level <= ordinary.minimum_level ||
            ordinary.average_level <= 1.0 ||
            ordinary.average_hp <= 0.0)
            failures++;
    }
    {
        int saved_floor = g->cur_floor;
        int saved_max_floor = g->dungeon_max_floor;
        int found_down_251 = 0, found_down_500 = 0;
        int found_down_999 = 0, down_from_1000 = 0;
        const int floors[4] = {251, 500, 999, 1000};
        int *found[4] = {
            &found_down_251, &found_down_500,
            &found_down_999, &down_from_1000
        };
        g->dungeon_max_floor = MAX_DUNGEON_FLOOR;
        for (int f = 0; f < 4; f++) {
            g->cur_floor = floors[f];
            for (int y = 0; y < MAP_H; y++)
                for (int x = 0; x < MAP_W; x++)
                    if (ladder_delta(g, x, y) > 0) *found[f] = 1;
        }
        if (!found_down_251 || !found_down_500 || !found_down_999 ||
            down_from_1000) failures++;

        int found_down_249 = 0, down_from_250 = 0;
        g->dungeon_max_floor = CLASSIC_DUNGEON_FLOOR;
        g->cur_floor = 249;
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++)
                if (ladder_delta(g, x, y) > 0) found_down_249 = 1;
        g->cur_floor = 250;
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++)
                if (ladder_delta(g, x, y) > 0) down_from_250 = 1;
        if (!found_down_249 || down_from_250) failures++;

        /* Chutes and keyed trap doors share the same policy even though the
         * original key-door generator currently stops at floor 170. */
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) {
                if (pitfall_target(g, x, y) > CLASSIC_DUNGEON_FLOOR)
                    failures++;
                int trap = game_trapdoor_floor(g, x, y);
                if (trap > CLASSIC_DUNGEON_FLOOR) failures++;
            }

        g->cur_floor = saved_floor;
        g->dungeon_max_floor = saved_max_floor;
    }
    {
        MonsterRecord saved_map[MONSTER_MAP_LAYERS][MONSTERS_PER_FLOOR];
        u16 saved_monster_floor[MONSTER_MAP_LAYERS];
        Character saved_character = g->chars[0];
        int saved_player = g->cur_player;
        int saved_floor = g->cur_floor;
        int saved_layer = g->monster_layer;
        int saved_dirty = g->monster_map_dirty;
        u32 saved_rand = g->rand_state;
        memcpy(saved_map, g->monster_map, sizeof(saved_map));
        memcpy(saved_monster_floor, g->monster_floor, sizeof(saved_monster_floor));
        g->cur_player = 0;
        mw_set_quest_flags(&g->chars[0], 0);
        static const int boss_floor[] = {375, 500, 625, 750, 875, 1000};
        static const int boss_type[] = {112, 113, 174, 175, 176, 177};
        for (int q = 0; q < 6; q++) {
            generate_monster_floor(g, 0, boss_floor[q]);
            int found = -1;
            for (int i = 0; i < MONSTERS_PER_FLOOR; i++)
                if (g->monster_map[0][i].type == boss_type[q]) {
                    found = i;
                    break;
                }
            if (found < 0 ||
                g->monster_map[0][found].level < boss_floor[q] - 2)
                failures++;
        }
        memcpy(g->monster_map, saved_map, sizeof(saved_map));
        memcpy(g->monster_floor, saved_monster_floor, sizeof(saved_monster_floor));
        g->chars[0] = saved_character;
        g->cur_player = saved_player;
        g->cur_floor = saved_floor;
        g->monster_layer = saved_layer;
        g->monster_map_dirty = saved_dirty;
        g->rand_state = saved_rand;
    }
    {
        int saved_max_floor = g->dungeon_max_floor;
        g->dungeon_max_floor = MAX_DUNGEON_FLOOR;
        int enhanced_count = bestiary_mode_catalog_count(g);
        if (enhanced_count != BESTIARY_CATALOG_COUNT ||
            bestiary_move_selection(0, 0x48, enhanced_count) !=
                enhanced_count - 1 ||
            bestiary_move_selection(enhanced_count - 1, 0x50,
                                    enhanced_count) != 0 ||
            bestiary_move_selection(0, 0x49, enhanced_count) !=
                enhanced_count - 1 ||
            bestiary_move_selection(enhanced_count - 1, 0x51,
                                    enhanced_count) != 0 ||
            bestiary_move_selection(18, 0x49, enhanced_count) != 0 ||
            bestiary_move_selection(0, 0x51, enhanced_count) != 18)
            failures++;
        g->dungeon_max_floor = CLASSIC_DUNGEON_FLOOR;
        int classic_count = bestiary_mode_catalog_count(g);
        if (classic_count <= 0 || classic_count >= enhanced_count ||
            bestiary_mode_catalog_index_for_type(g, 112) >= 0 ||
            bestiary_move_selection(0, 0x48, classic_count) !=
                classic_count - 1)
            failures++;
        g->dungeon_max_floor = saved_max_floor;
    }
    if (input_sdl_to_dos(SDLK_PAGEUP, KMOD_NONE) != -0x49) failures++;
    if (input_sdl_to_dos(SDLK_PAGEDOWN, KMOD_NONE) != -0x51) failures++;
    if (input_sdl_to_dos(SDLK_KP_9, KMOD_NONE) != -0x49) failures++;
    if (input_sdl_to_dos(SDLK_KP_3, KMOD_NONE) != -0x51) failures++;
    if (input_sdl_to_dos(SDLK_8, KMOD_SHIFT) != '*') failures++;
    if (input_sdl_to_dos(SDLK_9, KMOD_SHIFT) != '(') failures++;
    if (input_sdl_to_dos(SDLK_0, KMOD_SHIFT) != ')') failures++;
    if (input_sdl_to_dos(SDLK_F1, KMOD_NONE) != -0x3B ||
        input_sdl_to_dos(SDLK_F1, KMOD_CTRL) != INPUT_TURBO_TOGGLE)
        failures++;
    {
        int saved_enabled = g->turbo_enabled;
        int saved_percent = g->turbo_percent;
        int saved_input_percent = g->input.timing_percent;
        g->turbo_enabled = 0;
        g->turbo_percent = 100;
        input_set_timing_percent(&g->input, 100);
        if (game_scaled_delay_ms(g, 1000) != 1000 ||
            !game_handle_turbo_key(g, INPUT_TURBO_TOGGLE) ||
            !g->turbo_enabled || g->turbo_percent != 100)
            failures++;
        for (int i = 0; i < 50; i++) game_handle_turbo_key(g, '+');
        if (g->turbo_percent != 1000 ||
            game_scaled_delay_ms(g, 1000) != 100)
            failures++;
        for (int i = 0; i < 50; i++) game_handle_turbo_key(g, '-');
        if (g->turbo_percent != 25 ||
            game_scaled_delay_ms(g, 100) != 400)
            failures++;
        game_handle_turbo_key(g, INPUT_TURBO_TOGGLE);
        if (g->turbo_enabled || g->turbo_percent != 100 ||
            g->input.timing_percent != 100)
            failures++;
        g->turbo_enabled = saved_enabled;
        g->turbo_percent = saved_percent;
        input_set_timing_percent(&g->input, saved_input_percent);
    }
    if (input_sdl_to_dos(SDLK_F2, KMOD_CTRL) != INPUT_BATTLE_SIMULATOR ||
        input_sdl_to_dos(SDLK_F3, KMOD_CTRL) != INPUT_RANDOMIZE_FLOOR ||
        input_sdl_to_dos(SDLK_F4, KMOD_CTRL) != INPUT_QUEST_BOSS_WARP)
        failures++;
    if (input_sdl_to_dos(SDLK_v, KMOD_ALT) != INPUT_VIDEO_MODE ||
        input_sdl_to_dos(SDLK_v, KMOD_NONE) != 'v')
        failures++;
    if (input_sdl_to_dos(SDLK_F5, KMOD_CTRL) != INPUT_MODEL_VIEWER)
        failures++;
    if (input_sdl_to_dos(SDLK_F6, KMOD_CTRL) != INPUT_DUNGEON_REROLL)
        failures++;
    if (input_sdl_to_dos(SDLK_F7, KMOD_CTRL) != INPUT_OPEN_FLOOR_TOGGLE)
        failures++;
    if (input_sdl_to_dos(SDLK_F8, KMOD_CTRL) != INPUT_TOWN_TELEPORT)
        failures++;
    if (input_sdl_to_dos(SDLK_F9, KMOD_CTRL) != INPUT_GOD_TOGGLE)
        failures++;
    if (input_sdl_to_dos(SDLK_F10, KMOD_CTRL) != INPUT_NOCLIP_TOGGLE)
        failures++;
    if (input_sdl_to_dos(SDLK_F12,
            (SDL_Keymod)(KMOD_LCTRL | KMOD_LSHIFT | KMOD_LALT)) !=
        INPUT_MAX_CHARACTER ||
        input_sdl_to_dos(SDLK_F12,
            (SDL_Keymod)(KMOD_RCTRL | KMOD_RSHIFT | KMOD_RALT)) !=
        INPUT_MAX_CHARACTER ||
        input_sdl_to_dos(SDLK_F12, KMOD_LCTRL) != INPUT_TRAINER)
        failures++;
    if (input_sdl_to_dos(SDLK_F5, KMOD_NONE) != -0x3F ||
        input_sdl_to_dos(SDLK_F6, KMOD_NONE) != -0x40 ||
        input_sdl_to_dos(SDLK_F10, KMOD_NONE) != -0x44)
        failures++;
    if (quest_compass_direction(10, 10, 2, 9) != 2 ||
        quest_compass_direction(10, 10, 18, 11) != 3 ||
        quest_compass_direction(10, 10, 9, 2) != 0 ||
        quest_compass_direction(10, 10, 11, 18) != 1 ||
        quest_compass_direction(10, 10, 18, 18) != 1)
        failures++;
    failures += video_world_palette_self_test();
    failures += game_door_encounter_self_test(g);
    failures += game_dig_self_test(g);
    failures += game_death_recovery_self_test();

    const int menu_x = SX(0x48C);
    int adv = g->video.font_advance ? g->video.font_advance
                                    : g->video.font_char_w;
    int scaled_advance = adv * 7 / 6;
    {
        int saved_max_floor = g->dungeon_max_floor;
        for (int enhanced = 0; enhanced <= 1; enhanced++) {
            g->dungeon_max_floor = enhanced ? MAX_DUNGEON_FLOOR :
                                              CLASSIC_DUNGEON_FLOOR;
            int row_count = enhanced ? 13 : 12;
            int spacing = SY(enhanced ? 33 : 35);
            for (int row = 0; row < row_count; row++) {
                for (int col = 0; col < 2; col++) {
                    float logical_x = (float)(menu_x + scaled_advance *
                                              (col ? 12 : 2));
                    float logical_y = (float)(row * spacing + 4);
                    int window_x, window_y;
                    SDL_RenderLogicalToWindow(g->video.renderer,
                                              logical_x, logical_y,
                                              &window_x, &window_y);
                    int expected = enhanced ?
                        expected_enhanced[row][col] :
                        expected_classic[row][col];
                    if (command_menu_click_key(g, window_x, window_y) !=
                        expected)
                        failures++;
                }
            }
        }
        g->dungeon_max_floor = saved_max_floor;
    }
    {
        static const int direction[4] = {0, 1, 2, 3};
        int saved_x = g->input.last_mouse_x;
        int saved_y = g->input.last_mouse_y;
        int saved_mode = g->view_mode;
        for (int mode = 0; mode < 3; mode++) {
            ViewLayout layout;
            const ViewLayout *vl = &layout;
            game_view_layout(g, mode, &layout);
            const ViewRect *rects[4] = {
                &vl->north, &vl->south, &vl->west, &vl->east
            };
            g->view_mode = mode;
            for (int i = 0; i < 4; i++) {
                float logical_x = rects[i]->x + rects[i]->w / 2.0f;
                float logical_y = rects[i]->y + rects[i]->h / 2.0f;
                SDL_RenderLogicalToWindow(g->video.renderer,
                                          logical_x, logical_y,
                                          &g->input.last_mouse_x,
                                          &g->input.last_mouse_y);
                if (game_mouse_view_direction(g) != direction[i]) failures++;
            }
        }
        g->view_mode = saved_mode;
        g->input.last_mouse_x = saved_x;
        g->input.last_mouse_y = saved_y;
    }
    {
        /* Ctrl+F8 must require an explicit affirmative answer.  Test both
           cases through the same blocking input path used during play. */
        Input saved_input = g->input;
        Character prompt_player = {0};
        prompt_player.hp_cur = prompt_player.hp_max = 100;
        prompt_player.sp_cur = prompt_player.sp_max = 50.0f;
        prompt_player.floor_depth = (u16)g->cur_floor;
        g->input.head = g->input.tail = 0;
        g->input.quit_requested = 0;
        g->input.keys[g->input.tail] = 'N';
        g->input.tail = (g->input.tail + 1) % KEY_QUEUE_SIZE;
        if (confirm_town_teleport(g, &prompt_player)) failures++;
        g->input.keys[g->input.tail] = 'Y';
        g->input.tail = (g->input.tail + 1) % KEY_QUEUE_SIZE;
        if (!confirm_town_teleport(g, &prompt_player)) failures++;
        g->input = saved_input;
    }
    {
        Character maxed = {0};
        maxed.class_id = CLASS_WIZARD;
        maxed.jewels_pocket = 1234567;
        mw_set_experience_mode(&maxed, MW_EXPERIENCE_ENHANCED);
        game_max_character(g, &maxed);
        if (maxed.level != MW_PLAYER_LEVEL_MAX ||
            mw_hp_cur(&maxed) != MW_PLAYER_HP_MAX ||
            mw_hp_max(&maxed) != MW_PLAYER_HP_MAX ||
            maxed.sp_cur != MW_PLAYER_SP_MAX ||
            maxed.sp_max != MW_PLAYER_SP_MAX ||
            maxed.stat_str != MW_PLAYER_STAT_MAX ||
            maxed.stat_luck != MW_PLAYER_STAT_MAX ||
            maxed.jewels_pocket != 1234567 ||
            maxed.jewel_stones != UINT32_MAX ||
            game_loaded_weight(&maxed) != 0 ||
            !mw_universal_access(&maxed) ||
            !combat_weapon_allowed(&maxed, WEAPON_STAT_COUNT - 1) ||
            !combat_armor_allowed(&maxed, ARMOR_STAT_COUNT - 1) ||
            mw_weapon_inventory_count(
                &maxed, WEAPON_STAT_COUNT - 1) != UINT8_MAX ||
            mw_armor_inventory_count(
                &maxed, ARMOR_STAT_COUNT - 1) != UINT8_MAX ||
            maxed.native.late_gear_unlocks != UINT8_MAX ||
            mw_deep_spell_unlocks(&maxed) !=
                (u16)((1u << MW_DEEP_SPELL_COUNT) - 1u) ||
            maxed.spells[3][MW_ENHANCED_SPELL_COUNT - 1] != 1 ||
            maxed.wands[3][MW_ENHANCED_SPELL_COUNT - 1] != UINT8_MAX ||
            maxed.trapdoor_keys[17] != 1 ||
            maxed.eff_str_bonus != 60 ||
            maxed.eff_agi_bonus != 60 ||
            maxed.eff_super_str != 60 ||
            maxed.eff_super_agi != 60 ||
            maxed.eff_battle_str != UINT16_MAX ||
            maxed.eff_battle_spd != UINT16_MAX ||
            maxed.eff_pwr_weapon != 6 ||
            maxed.eff_protect_lv != 10 ||
            maxed.poisoned_turns != 0 || maxed.diseased_turns != 0)
            failures++;

        /* Max-effect sentinels must not expire or reverse already-capped
           attributes during ordinary turn, combat, or inn cleanup. */
        character_tick_effects(g, &maxed);
        character_clear_battle_effects(&maxed);
        character_clear_town_effects(&maxed);
        if (maxed.stat_str != MW_PLAYER_STAT_MAX ||
            maxed.stat_agi != MW_PLAYER_STAT_MAX ||
            game_loaded_weight(&maxed) != 0 ||
            maxed.eff_str_bonus != 60 ||
            maxed.eff_agi_bonus != 60 ||
            maxed.eff_super_str != 60 ||
            maxed.eff_super_agi != 60 ||
            maxed.eff_battle_str != UINT16_MAX ||
            maxed.eff_battle_spd != UINT16_MAX)
            failures++;

        Character classic = {0};
        classic.class_id = CLASS_WIZARD;
        mw_set_experience_mode(&classic, MW_EXPERIENCE_CLASSIC);
        game_max_character(g, &classic);
        if (mw_hp_cur(&classic) != INT16_MAX ||
            classic.green_pill != INT8_MAX ||
            game_loaded_weight(&classic) != 0 ||
            !mw_universal_access(&classic) ||
            !combat_weapon_allowed(&classic, 7) ||
            !combat_armor_allowed(&classic, 7) ||
            combat_weapon_allowed(&classic, 12) ||
            combat_armor_allowed(&classic, 8) ||
            mw_weapon_inventory_count(&classic, 12) != 0 ||
            classic.spells[0][MW_ORIGINAL_SPELL_COUNT - 1] != 1 ||
            classic.spells[0][MW_ORIGINAL_SPELL_COUNT] != 0 ||
            classic.eff_str_bonus != 60 ||
            classic.eff_super_agi != 60 ||
            classic.eff_battle_str != INT16_MAX ||
            classic.eff_battle_spd != INT16_MAX ||
            classic.eff_pwr_weapon != 3 ||
            (mw_quest_flags(&classic) & MW_FINAL_GEAR_QUEST_FLAG))
            failures++;
    }
    {
        /* The destructive max shortcut uses the same explicit Y/N gate as
           the town teleport and dungeon reroll debug controls. */
        Input saved_input = g->input;
        Character prompt_player = {0};
        prompt_player.hp_cur = prompt_player.hp_max = 100;
        g->input.head = g->input.tail = 0;
        g->input.quit_requested = 0;
        g->input.keys[g->input.tail] = 'N';
        g->input.tail = (g->input.tail + 1) % KEY_QUEUE_SIZE;
        if (confirm_max_character(g, &prompt_player)) failures++;
        g->input.keys[g->input.tail] = 'Y';
        g->input.tail = (g->input.tail + 1) % KEY_QUEUE_SIZE;
        if (!confirm_max_character(g, &prompt_player)) failures++;
        g->input = saved_input;
    }
    return failures;
}

static void cmd_bestiary(Game *g) {
    int selected = 0;
    for (;;) {
        int catalog_count = bestiary_mode_catalog_count(g);
        draw_bestiary_page(g, selected);
        video_present(&g->video);
        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input) || key == 0x1B) return;
        if (key == INPUT_TRAINER) {
            g->bestiary_unlock_all = 1;
            draw_bestiary_page(g, selected);
            video_fill_rect(&g->video, 420, 43, 590, 42, 0);
            video_draw_text_scaled(&g->video, 435, 50,
                                   "ALL BESTIARY ENTRIES UNLOCKED", 4,
                                   3, 4);
            video_present(&g->video);
            game_delay(g, 700);
            continue;
        }
        if (key == INPUT_MOUSE_CLICK) {
            int x, y;
            if (!game_mouse_click_logical(g, &x, &y)) continue;
            if (x < 408 && y >= 50 && y < 50 + 18 * 35) {
                int entry = (selected / 18) * 18 + (y - 50) / 35;
                if (entry < catalog_count) selected = entry;
                continue;
            }
            if (x >= 408 && y >= 42 && y < LOGICAL_H - 42) {
                int type = bestiary_type_at_mode_catalog_index(g, selected);
                if (type >= 0 &&
                    (g->bestiary_unlock_all || g->bestiary_kills[type]))
                    draw_bestiary_fullscreen(g, type);
                continue;
            }
            if (y >= LOGICAL_H - 42) {
                selected = bestiary_move_selection(selected,
                           x < LOGICAL_W / 2 ? 0x49 : 0x51,
                           catalog_count);
            }
            continue;
        }
        int selected_type = bestiary_type_at_mode_catalog_index(g, selected);
        if ((key == 'f' || key == 'F') && selected_type >= 0 &&
            (g->bestiary_unlock_all || g->bestiary_kills[selected_type])) {
            draw_bestiary_fullscreen(g, selected_type);
            continue;
        }
        if (key != 0) continue;
        int scan = input_getch(&g->input);
        selected = bestiary_move_selection(selected, scan, catalog_count);
    }
}

/* MW_PORT: WORLD func_26C03 help menu and its topic dispatch. */
/* ── Command: Help screen (matches original func_26C03 help menu) ── */

typedef struct HelpTopic {
    int key;
    const char *menu;
    const char *detail;
} HelpTopic;

static const HelpTopic help_topics[] = {
    {'A', "CHANGE ARMOR",
     "Select any armor you own. Your class limits the heaviest armor it can use. Equipped cursed armor cannot be dropped until its curse is removed."},
    {'B', "BRICK SPEED (4 SETTINGS)",
     "Cycles the original four brick-animation speed settings and updates the dungeon-display control value."},
    {'C', "CAST SPELL / SPELL HELP",
     "Choose Permanent, Preparation, Wizard Battle, or Priest Battle magic. The selector lists all ten levels at once. A spell costs one spell point per level; permanent magic is cast in town, preparation magic outside battle, and battle magic against a monster. The four Help choices explain every spell individually."},
    {'D', "DOWN LADDER OR DIG HOLE",
     "On a downward ladder, D descends. Elsewhere it digs through the floor using the original timed sequence. Classic permits digging through floor 120; Enhanced proportionally extends it through floor 480. Deeper rock cannot be dug. U uses upward ladders and town shop ladders; K operates a keyed trap door when you carry its matching key."},
    {'E', "EXPERIENCE TO NEXT LEVEL",
     "Shows current experience and the exact amount required for the next level. Level gains are awarded when you use the inn."},
    {'F', "ATTACK MONSTER",
     "Attacks an adjacent visible monster. Moving toward one stops you without attacking; press or hold F to fight. A monster behind a visible or secret door remains hidden and jams that door until it moves away."},
    {'G', "GAME STATS",
     "Summarizes the current dungeon seed and position, age, total recorded kills, Beastiary progress, quest bosses, keys, known pitfalls, explored cells, living floor monsters, magic inventory, equipment, weight pressure, and active runtime test modes."},
    {'H', "THIS HELP SCREEN",
     "Every highlighted help line can be clicked or selected with its shown key. Escape returns to exploration."},
    {'I', "USE ITEM",
     "Use scrolls, wands, magic paper, pills, stones, potions, floor sloshers, or the Holy Hand Grenade. Scrolls and papers are consumed; wands lose one charge. Fighters may cast only from magic paper. Enhanced super-rare relics are passive and appear on the second miscellaneous Pockets page."},
    {'L', "LOSE (DROP) AN ITEM",
     "Drops armor, weapons, or an entire carried money denomination. Fists and skin are implicit and cannot be dropped. Cursed equipped gear refuses to come off."},
    {'M', "VIEW MONETARY BREAKDOWN",
     "Shows jewel pieces and every stone denomination in your pockets and bank. Carried stones have weight; banked wealth and jewel pieces do not."},
    {'O', "SOUND ON/OFF",
     "Toggles the original sound state and changes the command legend between ON and OFF."},
    {'P', "VIEW CONTENTS OF POCKETS",
     "Opens spellbooks, scrolls, wands, papers, miscellaneous magic items, pills, rings, body armor, and gauntlet inventory pages."},
    {'Q', "QUIT AND SAVE POSITION",
     "Writes the character, explored dungeon, monsters, pitfalls, and bestiary record, then returns to the title screen. S performs the same save without leaving the character."},
    {'J', "BEASTIARY",
     "Lists every spawnable monster as ???? until your first kill. Discovered entries show their original picture, statistics, floor range, and kill count. Press F or click the picture for a full-screen view."},
    {'S', "SAVE AND CONTINUE",
     "Saves the complete current game state and immediately resumes play."},
    {'T', "WAIT",
     "Passes one player turn. Monsters move and timed battle effects, poison, and disease advance normally."},
    {'V', "VIEW PLAYER STATISTICS",
     "Shows race, class, level, attributes, health, spell points, age, load, equipment bonuses, drains, and raise-dead contract state."},
    {'W', "SELECT WEAPON",
     "Equips an owned weapon permitted to your class. Damage, hit bonus, temporary spell enchantment, and permanent item enchantment all contribute in combat."},
    {'X', "EXPAND THE 2D MAP",
     "Displays the remembered dungeon map with exact wall and door edges, ladder directions, shops, keyed trap doors, known pitfalls, and the blinking player marker. Quest floors also show the original directional hint toward a living quest monster."},
    {'Z', "ZOOM IN ON A 3D VIEW",
     "Opens the original direction chooser. Press an arrow or click a compass viewport to expand that view across the full 1024x768 screen until another key is pressed. Press Z again in the chooser to cycle the original three four-view sizes."},
    {'1', "SHOW SPELL EFFECT PAGE 1",
     "Shows preparation and battle bonuses including strength, agility, invisibility, feather, fast movement, protection, and weapon power with their remaining state."},
    {'2', "SHOW SPELL EFFECT PAGE 2",
     "Shows poison, disease, elemental and drain resistances, and monster hold/stop effects with their remaining turns."},
    {'3', "SHOW ENHANCED EFFECT PAGE 3",
     "Enhanced only. Shows automatic regeneration, protection, anti-magic, body armor, gauntlet, and every owned passive relic. Arcane Renewal reports actions until its next spell point; Phoenix Seal reports ready or its exact recharge time."}
};

static int wrap_help_detail(const char *detail, char lines[][33],
                            int line_capacity) {
    char copy[1024];
    int count = 0;
    snprintf(copy, sizeof(copy), "%s", detail ? detail : "");
    if (line_capacity <= 0) return 0;
    lines[0][0] = '\0';
    char *word = strtok(copy, " ");
    while (word && count < line_capacity) {
        size_t have = strlen(lines[count]);
        size_t need = strlen(word);
        if (have && have + need + 1 > 31) {
            count++;
            if (count >= line_capacity) break;
            lines[count][0] = '\0';
            have = 0;
        }
        if (have) strncat(lines[count], " ",
                          sizeof(lines[count]) - strlen(lines[count]) - 1);
        strncat(lines[count], word,
                sizeof(lines[count]) - strlen(lines[count]) - 1);
        word = strtok(NULL, " ");
    }
    if (count < line_capacity && lines[count][0]) count++;
    return count;
}

static void draw_help_detail(Game *g, Character *p,
                             const HelpTopic *topic) {
    enum { DETAIL_LINES_PER_PAGE = 26 };
    Video *v = &g->video;
    char lines[48][33];
    char title[96], page_line[48];
    int line_count = wrap_help_detail(topic->detail, lines, 48);
    int pages = (line_count + DETAIL_LINES_PER_PAGE - 1) /
                DETAIL_LINES_PER_PAGE;
    if (pages < 1) pages = 1;

    for (int page = 0; page < pages; page++) {
        int y = 0;
        left_column_begin(g, p);
        snprintf(title, sizeof(title), "%c-%s", topic->key, topic->menu);
        y = left_column_text(g, y, title, 4);
        int first = page * DETAIL_LINES_PER_PAGE;
        int end = first + DETAIL_LINES_PER_PAGE;
        if (end > line_count) end = line_count;
        for (int i = first; i < end; i++)
            y = left_column_text(g, y, lines[i], 7);
        if (page + 1 < pages)
            snprintf(page_line, sizeof(page_line),
                     "ANY KEY: MORE (%d/%d) ESC: HELP", page + 1, pages);
        else
            snprintf(page_line, sizeof(page_line),
                     "HIT ANY KEY TO RETURN TO HELP");
        left_column_text(g, y, page_line, 15);
        video_present(v);
        if (input_wait_any_key(&g->input) == 0x1B) return;
    }
}

static void cmd_help(Game *g, Character *p) {
    Video *v = &g->video;
    const int row_h = SY(38);
    int topic_count = (int)(sizeof(help_topics) / sizeof(help_topics[0]));
    if (mw_experience_mode(p) == MW_EXPERIENCE_CLASSIC)
        topic_count--; /* Enhanced effect page 3 is the final help entry. */
    for (;;) {
        int y = 0;
        int option_top[sizeof(help_topics) / sizeof(help_topics[0])];
        left_column_begin(g, p);
        y = left_column_text(g, y,
                             "HELP MENU-HIT ESC TO RETURN TO GAME", 4);
        y = left_column_text(g, y,
                             "HIT LETTER OR NUMBER FOR MORE HELP", 5);
        int option_y = y;
        for (int i = 0; i < topic_count; i++) {
            char line[40];
            option_top[i] = y;
            snprintf(line, sizeof(line), "%c-%s", help_topics[i].key,
                     help_topics[i].menu);
            left_column_text(g, y, line, 8);
            y += row_h;
        }
        left_column_text(g, y, "ESC RETURNS TO GAME", 15);
        video_present(v);

        int key = input_wait_any_key(&g->input);
        if (input_poll_quit(&g->input) || key == 0x1B) return;
        if (key == INPUT_MOUSE_CLICK) {
            int x, click_y;
            key = -1;
            if (game_mouse_click_logical(g, &x, &click_y) &&
                x >= 0 && x < SX(0x2D3) &&
                click_y >= option_y) {
                int row = (click_y - option_y) / row_h;
                if (row >= 0 && row < topic_count &&
                    click_y >= option_top[row] &&
                    click_y < option_top[row] + row_h)
                    key = help_topics[row].key;
            }
        }
        if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
        for (int i = 0; i < topic_count; i++)
            if (key == help_topics[i].key) {
                draw_help_detail(g, p, &help_topics[i]);
                break;
            }
    }
}

/* MW_PORT: expand-map command branch of WORLD func_0F6E5 and the map
 * primitives in func_1F077/func_1F3FD/far_1FAE6. */
/* ── Command: Expand Map (dungeon map viewer) ── */

static void draw_expanded_map_frame(Game *g, int third) {
    Video *v = &g->video;
    const MwDisplayModeInfo *info = video_display_mode_info(v->display_mode);
    int raster_w = info ? info->raster_w : LOGICAL_W;
    int raster_h = info ? info->raster_h : LOGICAL_H;
    int cs = info ? info->expanded_map_cell_px : 7;
    int split = raster_w == 320;
    int first_row = split ? third * 37 : 0;
    int last_row = split ? first_row + 37 : MAP_H;
    if (last_row > MAP_H) last_row = MAP_H;
    int visible_rows = last_row - first_row;
    int native_map_w = MAP_W * cs;
    int native_ox = (raster_w - native_map_w) / 2;
    int native_oy = 0;
    if (native_ox < 0) native_ox = 0;
    int ox = native_ox * LOGICAL_W / raster_w;
    int oy = native_oy * LOGICAL_H / raster_h;

    video_clear(v, 0);
    for (int y = first_row; y < last_row; y++) {
        int row = y - first_row;
        for (int x = 0; x < MAP_W; x++) {
            if (!g->visited[y][x]) continue;

            int px = ox + x * cs * LOGICAL_W / raster_w;
            int py = oy + row * cs * LOGICAL_H / raster_h;
            int nx = ox + (x + 1) * cs * LOGICAL_W / raster_w;
            int ny = oy + (row + 1) * cs * LOGICAL_H / raster_h;
            int cw = nx - px;
            int ch = ny - py;
            if (cw < 1) cw = 1;
            if (ch < 1) ch = 1;
            video_fill_rect(v, px, py, cw, ch, 0);

            int n = map_get_edge(g, x,     y,     1);
            int e = map_get_edge(g, x + 1, y,     0);
            int s = map_get_edge(g, x,     y + 1, 1);
            int w = map_get_edge(g, x,     y,     0);
            if (n != 3) {
                map_native_hline(v, px, py, cw, ch, cs, 0, 0, cs, 15);
                if (n == 1)
                    draw_map_door_marker(v, px, py, cw, ch, cs, 1, 0, 15);
            }
            if (s != 3) {
                map_native_hline(v, px, py, cw, ch, cs,
                                 0, cs - 1, cs, 15);
                if (s == 1)
                    draw_map_door_marker(v, px, py, cw, ch, cs, 1, 1, 15);
            }
            if (w != 3) {
                map_native_vline(v, px, py, cw, ch, cs, 0, 0, cs, 15);
                if (w == 1)
                    draw_map_door_marker(v, px, py, cw, ch, cs, 0, 0, 15);
            }
            if (e != 3) {
                map_native_vline(v, px, py, cw, ch, cs,
                                 cs - 1, 0, cs, 15);
                if (e == 1)
                    draw_map_door_marker(v, px, py, cw, ch, cs, 0, 1, 15);
            }

            int shop = game_shop_type(g, x, y);
            int ladder = game_ladder_delta(g, x, y);
            int trap = game_trapdoor_floor(g, x, y);
            if (shop) {
                for (int iy = 1; iy < cs - 1; iy++)
                    map_native_hline(v, px, py, cw, ch, cs,
                                     1, iy, cs - 2, (u8)(shop + 2));
            } else if (ladder < 0) {
                for (int p = 1; p < cs - 1; p++)
                    map_native_pixel(v, px, py, cw, ch, cs,
                                     p, cs - 1 - p, 4);
            } else if (ladder > 0) {
                for (int p = 1; p < cs - 1; p++)
                    map_native_pixel(v, px, py, cw, ch, cs,
                                     p, p, MW_COLOR_STATUS_CYAN);
            } else if (trap >= 0) {
                for (int p = 1; p < cs - 1; p++) {
                    map_native_pixel(v, px, py, cw, ch, cs, p, p, 4);
                    map_native_pixel(v, px, py, cw, ch, cs,
                                     cs - 1 - p, p, 4);
                }
            } else if (game_is_known_pitfall(g, x, y)) {
                for (int p = 1; p < cs - 1; p++) {
                    map_native_pixel(v, px, py, cw, ch, cs, p, p, 3);
                    map_native_pixel(v, px, py, cw, ch, cs,
                                     cs - 1 - p, p, 3);
                }
            }

            if (x == g->cur_x && y == g->cur_y &&
                g->map_player_visible) {
                int p0 = cs / 3;
                int p1 = cs - p0;
                for (int iy = p0; iy < p1; iy++)
                    for (int ix = p0; ix < p1; ix++)
                        map_native_pixel(v, px, py, cw, ch, cs,
                                         ix, iy, 15);
                map_native_pixel(v, px, py, cw, ch, cs,
                                 cs / 2, cs / 2, 6);
            }
        }
    }

    const char *caption = "EXPANDED DUNGEON MAP, HIT ANY KEY...";
    if (split) {
        static const char *const third_caption[3] = {
            "DUNGEON MAP, TOP THIRD, HIT ANY KEY...",
            "DUNGEON MAP, MIDDLE THIRD, HIT ANY KEY...",
            "DUNGEON MAP, BOTTOM THIRD, HIT ANY KEY..."
        };
        caption = third_caption[third < 0 ? 0 : (third > 2 ? 2 : third)];
    }
    int caption_y = (raster_h - (v->font_char_h > 0 ?
                    v->font_char_h : 8) - 2) * LOGICAL_H / raster_h;
    video_draw_text(v, 0, caption_y, caption, split ? 11 : 15);
}

static void cmd_expand_map(Game *g) {
    const MwDisplayModeInfo *info =
        video_display_mode_info(g->video.display_mode);
    int pages = info && info->raster_w == 320 ? 3 : 1;
    for (int page = 0; page < pages; page++) {
        u32 next_blink = SDL_GetTicks() + 350;
        g->map_player_visible = 1;
        draw_expanded_map_frame(g, page);
        video_present(&g->video);

        while (!input_poll_quit(&g->input) && !input_kbhit(&g->input)) {
            u32 now = SDL_GetTicks();
            if (SDL_TICKS_PASSED(now, next_blink)) {
                g->map_player_visible = !g->map_player_visible;
                next_blink = now + 350;
                draw_expanded_map_frame(g, page);
                video_present(&g->video);
            }
            SDL_Delay(10);
        }
        if (input_poll_quit(&g->input)) break;
        input_wait_any_key(&g->input);
    }
    g->map_player_visible = 1;
}

/* MW_PORT: WORLD func_0E578/examine_item directional zoom path.  Z opens the
 * original chooser; an arrow (or a viewport click) renders that compass view
 * across the complete 1024x768 surface until another key is pressed.  A
 * second Z changes the three original 3-D size modes instead. */
static void cmd_zoom(Game *g, Character *p) {
    Video *v = &g->video;
    int direction = -1;

    game_draw_exploration_base(g, p);
    video_fill_rect(v, 0, 0, SX(0x2D3), SY(0x1AE), 0);
    video_draw_text_scaled_xy(v, 8, 8,
                              "HIT AN ARROW OR CLICK A VIEW", 7,
                              7, 6, 12, 17);
    video_draw_text_scaled_xy(v, 8, SY(42),
                              "TO ZOOM IN ON THAT VIEW.", 7,
                              7, 6, 12, 17);
    video_draw_text_scaled_xy(v, 8, SY(84),
                              "HIT Z TO CHANGE VIEW SIZE.", 7,
                              7, 6, 12, 17);
    video_draw_text_scaled_xy(v, 8, SY(126), "ESC CANCELS.", 8,
                              7, 6, 12, 17);
    video_present(v);

    while (!input_poll_quit(&g->input)) {
        int key = input_getch(&g->input);
        if (key == 0x1B) return;
        if (key == INPUT_MOUSE_CLICK) {
            direction = game_mouse_view_direction(g);
            if (direction < 0) {
                int x, y;
                if (game_mouse_click_logical(g, &x, &y) &&
                    x < SX(0x2D3) && y < SY(0x1AE))
                    key = 'z';
                else
                    continue;
            }
        } else if (key == 0) {
            switch (input_getch(&g->input)) {
            case 0x48: direction = 0; break;
            case 0x50: direction = 1; break;
            case 0x4B: direction = 2; break;
            case 0x4D: direction = 3; break;
            default: break;
            }
        }

        if (key == 'z' || key == 'Z') {
            g->view_mode = (g->view_mode + 1) % 3;
            return;
        }
        if (direction >= 0) break;
    }
    if (direction < 0) return;

    video_clear(v, 0);
    draw_3d_viewport(g, 0, 0, LOGICAL_W, LOGICAL_H, direction);
    {
        static const int dx[4] = {0, 0, -1, 1};
        static const int dy[4] = {-1, 1, 0, 0};
        int x = g->cur_x + dx[direction];
        int y = g->cur_y + dy[direction];
        if (edge_between_cells(g, g->cur_x, g->cur_y, x, y) == 3) {
            int index = game_find_monster(g, x, y);
            if (index >= 0) {
                int type = g->monster_map[g->monster_layer][index].type;
                if (type >= 0 && type < MONSTER_TYPE_COUNT) {
                    int w = (int)strlen(monster_types[type].name) *
                            (v->font_advance ? v->font_advance : v->font_char_w);
                    video_fill_rect(v, 0, 0, w + 16, v->font_char_h + 12, 0);
                    video_draw_text(v, 8, 6, monster_types[type].name, 15);
                }
            }
        }
    }
    video_present(v);
    input_wait_any_key(&g->input);
}

/* MW_PORT: WORLD func_0C031 effect pages and func_0CDDD/func_0D2C9 effect
 * expiration/reset state.  Page three is an Enhanced-only extension for
 * automatic magic items and native passive relics. */
/* ── Spells in effect display (keys '1', '2', and Enhanced '3') ── */

static void draw_spells_in_effect_page(Game *g, Character *p, int page) {
    Video *v = &g->video;
    char line[128];
    int y = 0;
    int shown = 0;

    /* WORLD func_0C031 clears only the upper-left message pane and omits
       inactive effects.  That is essential here: a page of zero-valued
       rows both hid the four-view display and no longer resembled the DOS
       status pages. */
    town_pane_begin(g, p);
#define EFFECT_LINE(condition, color, ...) do { \
    if (condition) { \
        snprintf(line, sizeof(line), __VA_ARGS__); \
        y = town_pane_text(g, y, line, color); \
        shown++; \
    } \
} while (0)

    if (page == 0) {
        EFFECT_LINE(mw_enchant_wpn_spell(p) > 0, 3, "WEAPONS, PLUS %d",
                    mw_enchant_wpn_spell(p));
        EFFECT_LINE(mw_armor_plus(p) > 0, 3, "ARMOR, PLUS %d",
                    mw_armor_plus(p));
        EFFECT_LINE(p->eff_feather > 0, 7, "FEATHER");
        EFFECT_LINE(p->eff_invisible > 0, 7, "INVISIBILITY");
        EFFECT_LINE(p->eff_fast_move > 0, 7, "FAST - MOVE");
        EFFECT_LINE(p->eff_str_bonus > 0, 6, "STRENGTH (PREP)");
        EFFECT_LINE(p->eff_agi_bonus > 0, 6, "AGILITY (PREP)");
        EFFECT_LINE(p->eff_super_str > 0, 6, "SUPER STRENGTH");
        EFFECT_LINE(p->eff_super_agi > 0, 6, "SUPER AGILITY");
        EFFECT_LINE(p->eff_battle_str > 0, 3, "BATTLE STRENGTH: %d",
                    p->eff_battle_str);
        EFFECT_LINE(p->eff_battle_spd > 0, 3, "BATTLE SPEED: %d",
                    p->eff_battle_spd);
    } else if (page == 1) {
        EFFECT_LINE(p->eff_protect_turns > 0, 5,
                    "PROTECT, LEVEL %d (%d TURNS)",
                    p->eff_protect_lv, p->eff_protect_turns);
        EFFECT_LINE(p->eff_pwr_wpn_turns > 0, 5,
                    "POWER WEAPON %d (%d TURNS)",
                    p->eff_pwr_weapon, p->eff_pwr_wpn_turns);
        EFFECT_LINE(p->eff_slow_mon > 0, 7, "SLOW MONSTER: %d",
                    p->eff_slow_mon);
        EFFECT_LINE(p->eff_hold_monster > 0, 7, "HOLD MONSTER: %d",
                    p->eff_hold_monster);
        EFFECT_LINE(p->eff_stop_monster > 0, 7, "STOP MONSTER: %d",
                    p->eff_stop_monster);
        EFFECT_LINE(p->eff_resist_poison > 0, 6, "RESIST POISON: %d",
                    p->eff_resist_poison);
        EFFECT_LINE(p->eff_resist_disease > 0, 6, "RESIST DISEASE: %d",
                    p->eff_resist_disease);
        EFFECT_LINE(p->eff_resist_drain > 0, 6, "RESIST DRAIN: %d",
                    p->eff_resist_drain);
        EFFECT_LINE(p->eff_anti_cold > 0, 6, "ANTI-COLD: %d",
                    p->eff_anti_cold);
        EFFECT_LINE(p->eff_anti_fire > 0, 6, "ANTI-FIRE: %d",
                    p->eff_anti_fire);
    } else if (mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED) {
        EFFECT_LINE(p->ring_regen > 0, 3, "RING REGEN: +%d HP PER ACTION",
                    p->ring_regen);
        EFFECT_LINE(mw_ring_prot_plus(p) > 0, 3,
                    "RING PROTECTION: PLUS %d", mw_ring_prot_plus(p));
        EFFECT_LINE(p->antimagic_ring > 0, 3, "ANTI-MAGIC: PLUS %d",
                    p->antimagic_ring);
        EFFECT_LINE(mw_body_armor_plus(p) > 0, 5,
                    "BODY ARMOR: PLUS %d", mw_body_armor_plus(p));
        EFFECT_LINE(mw_gauntlet(p) > 0, 5, "GAUNTLET: PLUS %d",
                    mw_gauntlet(p));
        if (mw_relic_owned(p, MW_RELIC_ARCANE_RING)) {
            unsigned phase = p->native.relic_regen_phase;
            unsigned remaining = phase < 4 ? 4 - phase : 1;
            EFFECT_LINE(1, 11, "ARCANE RENEWAL: SP IN %u ACTIONS",
                        remaining);
        }
        EFFECT_LINE(mw_relic_owned(p, MW_RELIC_BLOODSTONE_SIGNET), 11,
                    "BLOODSTONE: 5%% MELEE LIFE-STEAL");
        EFFECT_LINE(mw_relic_owned(p, MW_RELIC_DEEPWARD_AMULET), 11,
                    "DEEPWARD: -15%% DAMAGE; DRAINS X2");
        EFFECT_LINE(mw_relic_owned(p, MW_RELIC_SAGE_PRISM), 11,
                    "SAGE'S PRISM: KILL XP +25%%");
        if (mw_relic_owned(p, MW_RELIC_PHOENIX_SEAL)) {
            if (p->native.relic_phoenix_cooldown)
                EFFECT_LINE(1, 14, "PHOENIX: %u ACTIONS TO READY",
                            p->native.relic_phoenix_cooldown);
            else
                EFFECT_LINE(1, 10, "PHOENIX SEAL: READY");
        }
    }
#undef EFFECT_LINE

    if (!shown)
        y = town_pane_text(g, y, "NO SPELLS IN EFFECT.", 8);
    town_pane_text(g, y, "HIT ANY KEY...", 4);
    video_present(v);
}

static void cmd_spells_in_effect(Game *g, Character *p, int page) {
    draw_spells_in_effect_page(g, p, page);
    input_wait_any_key(&g->input);
}

void game_draw_effects_test(Game *g, Character *p, int page) {
    draw_spells_in_effect_page(g, p, page);
}

static int dialog_outside_unchanged(const Video *v, const u8 *baseline,
                                    int changed_w, int changed_h) {
    for (int y = 0; y < LOGICAL_H; y++)
        for (int x = 0; x < LOGICAL_W; x++)
            if ((x >= changed_w || y >= changed_h) &&
                v->pixels[y * LOGICAL_W + x] !=
                    baseline[y * LOGICAL_W + x])
                return 0;
    return 1;
}

int game_dialog_ui_self_test(Game *g, Character *p) {
    if (!g || !p || !g->video.pixels) return 1;
    const size_t pixel_count = (size_t)LOGICAL_W * LOGICAL_H;
    u8 *baseline = malloc(pixel_count);
    if (!baseline) return 1;
    Input saved_input = g->input;
    int failures = 0;

#define QUEUE_DIALOG_KEY(key_value) do { \
    g->input.head = 0; \
    g->input.tail = 1; \
    g->input.quit_requested = 0; \
    g->input.keys[0] = (key_value); \
} while (0)
#define VERIFY_DIALOG(label, changed_width, changed_height, key_value, ...) do { \
    game_draw_exploration(g, p); \
    memcpy(baseline, g->video.pixels, pixel_count); \
    QUEUE_DIALOG_KEY(key_value); \
    __VA_ARGS__; \
    if (!dialog_outside_unchanged(&g->video, baseline, \
                                  (changed_width), (changed_height))) { \
        fprintf(stderr, "DIALOG UI TEST FAIL: %s escaped its source pane\n", \
                (label)); \
        failures++; \
    } \
} while (0)
#define VERIFY_DIALOG_TWO(label, changed_width, changed_height, key1, key2, ...) do { \
    game_draw_exploration(g, p); \
    memcpy(baseline, g->video.pixels, pixel_count); \
    g->input.head = 0; \
    g->input.tail = 2; \
    g->input.quit_requested = 0; \
    g->input.keys[0] = (key1); \
    g->input.keys[1] = (key2); \
    __VA_ARGS__; \
    if (!dialog_outside_unchanged(&g->video, baseline, \
                                  (changed_width), (changed_height))) { \
        fprintf(stderr, "DIALOG UI TEST FAIL: %s escaped its source pane\n", \
                (label)); \
        failures++; \
    } \
} while (0)
#define VERIFY_SOURCE_SELECTOR(label, statement, ...) do { \
    const int dialog_keys_[] = { __VA_ARGS__ }; \
    game_draw_exploration(g, p); \
    memcpy(baseline, g->video.pixels, pixel_count); \
    g->input.head = 0; \
    g->input.tail = 0; \
    g->input.quit_requested = 0; \
    for (size_t key_i_ = 0; \
         key_i_ < sizeof(dialog_keys_) / sizeof(dialog_keys_[0]); \
         key_i_++) { \
        g->input.keys[g->input.tail] = dialog_keys_[key_i_]; \
        g->input.tail = (g->input.tail + 1) % KEY_QUEUE_SIZE; \
    } \
    statement; \
    if (!dialog_outside_unchanged(&g->video, baseline, \
                                  LOGICAL_W, SY(0x1AE))) { \
        fprintf(stderr, \
                "DIALOG UI TEST FAIL: %s escaped the top selector strip\n", \
                (label)); \
        failures++; \
    } \
} while (0)

    /* Short WORLD shop_magic-style panels. */
    VERIFY_DIALOG("view money", SX(0x2D3), SY(0x1AE), ' ',
                  cmd_view_money(g, p));
    VERIFY_DIALOG("experience", SX(0x2D3), SY(0x1AE), ' ',
                  cmd_exp_needed(g, p));
    VERIFY_DIALOG("pockets menu", SX(0x2D3), SY(0x1AE), 0x1B,
                  cmd_pockets(g, p));
    /* Page Down is the DOS byte pair 0,51h.  51h is also ASCII 'Q', so a
       one-key menu must consume the pair atomically before returning to the
       exploration Save/Quit dispatcher. */
    g->input.head = 0;
    g->input.tail = 3;
    g->input.quit_requested = 0;
    g->input.keys[0] = 0;
    g->input.keys[1] = 0x51;
    g->input.keys[2] = 'v';
    cmd_pockets(g, p);
    if (!input_kbhit(&g->input) || input_getch(&g->input) != 'v') {
        fprintf(stderr,
                "DIALOG INPUT TEST FAIL: Page Down leaked ASCII Q\n");
        failures++;
    }
    VERIFY_DIALOG("spell effects", SX(0x2D3), SY(0x1AE), ' ',
                  cmd_spells_in_effect(g, p, 0));
    if (mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED)
        VERIFY_DIALOG("enhanced effects", SX(0x2D3), SY(0x1AE), ' ',
                      cmd_spells_in_effect(g, p, 2));
    VERIFY_DIALOG("drop item", SX(0x2D3), SY(0x1AE), 0x1B,
                  cmd_drop_item(g, p));
    VERIFY_DIALOG("cast spell", SX(0x2D3), SY(0x1AE), 0x1B,
                  cmd_cast_spell_menu(g, p, NULL));
    VERIFY_DIALOG("use item", SX(0x2D0), SY(0x1AC), 0x1B,
                  cmd_use_item(g, p, NULL));
    VERIFY_DIALOG_TWO("vitamin pill item page", SX(0x2D0), SY(0x1AC),
                      '4', 0x1B, cmd_use_item(g, p, NULL));
    VERIFY_DIALOG_TWO("other magic item page", SX(0x2D0), SY(0x1AC),
                      '5', 0x1B, cmd_use_item(g, p, NULL));
    VERIFY_DIALOG("weapons", SX(0x2D3), SY(0x1AE), 0x1B,
                  cmd_weapons(g, p));
    VERIFY_DIALOG("armor", SX(0x2D3), SY(0x1AE), 0x1B,
                  cmd_armor(g, p));

    /* The shared 30-item selector for casting and consumable sources owns
       the full-width top strip, but must preserve everything below it.
       Enter the selector through every live source rather than testing only
       the preceding category menu. */
    VERIFY_SOURCE_SELECTOR("spellbook selector",
                           (void)cmd_cast_spell_menu(g, p, NULL),
                           '2', 0x1B);
    VERIFY_SOURCE_SELECTOR("scroll selector",
                           (void)cmd_use_item(g, p, NULL),
                           '1', '2', 0x1B);
    VERIFY_SOURCE_SELECTOR("wand selector",
                           (void)cmd_use_item(g, p, NULL),
                           '2', '2', 0x1B);
    VERIFY_SOURCE_SELECTOR("paper selector",
                           (void)cmd_use_item(g, p, NULL),
                           '3', '2', 0x1B);

    /* Long source pages replace only the left column. */
    VERIFY_DIALOG("view stats", SX(0x2D3), LOGICAL_H, ' ',
                  cmd_view_stats(g, p));
    VERIFY_DIALOG("help", SX(0x2D3), LOGICAL_H, 0x1B,
                  cmd_help(g, p));
    /* Keep this last: the magic test harness captures the Enhanced relic
       inventory page for visual regression review. */
    VERIFY_DIALOG_TWO("misc pockets", SX(0x2D3), LOGICAL_H, ' ', ' ',
                      cmd_pockets_misc(g, p));

#undef VERIFY_DIALOG
#undef VERIFY_DIALOG_TWO
#undef VERIFY_SOURCE_SELECTOR
#undef QUEUE_DIALOG_KEY
    g->input = saved_input;
    free(baseline);
    return failures;
}

/* MW_PORT: WORLD select_player (0x0889F) and character_menu (0x092B4). */
/* ── Player selection screen ── */

/* MW_PORT: native counterpart to MW.EXE's initial adapter/driver picker.
 * The twelve WORLD branches are deliberately listed separately: equal
 * resolutions can use different palettes, wall rasterizers and map paths. */
static void game_video_mode_menu(Game *g, int startup) {
    Video *v = &g->video;
    int selected = v->display_mode;
    if (!video_display_mode_info(selected))
        selected = MW_DISPLAY_SVGA_1024X768_256_A;
    const int first_y = 118;
    const int row_h = 41;
    static const char choice_key[MW_DISPLAY_MODE_COUNT + 1] =
        "123456789ABC";

    for (;;) {
        video_clear(v, 0);
        video_draw_text(v, 32, 24, "MORAFF'S WORLD VIDEO DISPLAY", 8);
        video_draw_text_scaled(v, 32, 63,
            "THE 12 ORIGINAL WORLD.EXE DRIVERS (SAME RESOLUTION CAN DIFFER):",
            3, 3, 4);
        video_draw_text_scaled(v, 32, 91,
            "KEY  RASTER     PALETTE  ORIGINAL ADAPTER / RENDERER",
            7, 3, 4);

        for (int i = 0; i < MW_DISPLAY_MODE_COUNT; i++) {
            const MwDisplayModeInfo *info = video_display_mode_info(i);
            int y = first_y + i * row_h;
            char line[128];
            if (i == selected)
                video_fill_rect(v, 20, y - 5, LOGICAL_W - 40, row_h - 2, 1);
            snprintf(line, sizeof(line), "%c) %-10s %3d-COLOR  %s",
                     choice_key[i], info->resolution, info->palette_colors,
                     info->adapter);
            video_draw_text_scaled(v, 42, y, line,
                                   i == selected ? 4 : 15, 3, 4);
        }

        const MwDisplayModeInfo *choice = video_display_mode_info(selected);
        char detail[160];
        static const char *wall_name[] = {
            "HERCULES HATCH", "CGA 4-COLOR", "PLANAR 16-COLOR",
            "CHUNKY 256-COLOR"
        };
        snprintf(detail, sizeof(detail),
                 "DRIVER %d: %dX%d  MAP %dX%d CELLS @ %dPX  WALLS: %s",
                 choice->world_mode, choice->raster_w, choice->raster_h,
                 choice->map_cols, choice->map_rows, choice->map_cell_px,
                 wall_name[choice->wall_style]);
        video_draw_text_scaled(v, 32, 624, detail, 14, 3, 4);
        video_draw_text_scaled(v, 32, 654,
            "UP/DOWN OR 1-9/A-C SELECTS. ENTER OR CLICK APPLIES.", 8, 3, 4);
        video_draw_text_scaled(v, 32, 682,
            startup ? "ESC KEEPS THE SAVED MODE. ALT+V REOPENS THIS MENU LATER."
                    : "ESC OR ALT+V RETURNS WITHOUT CHANGING THE MODE.",
            7, 3, 4);
        video_fill_rect(v, 80, 720, 320, 42, 1);
        video_draw_text_scaled(v, 150, 729, "APPLY DRIVER", 4, 3, 4);
        video_fill_rect(v, 620, 720, 300, 42, 1);
        video_draw_text_scaled(v, 710, 729, "CANCEL", 15, 3, 4);
        video_present(v);

        int key = input_getch(&g->input);
        int apply = 0;
        if (input_poll_quit(&g->input)) return;
        if (key == 0) {
            int scan = input_getch(&g->input);
            if (scan == 0x48) {
                selected = (selected + MW_DISPLAY_MODE_COUNT - 1) %
                           MW_DISPLAY_MODE_COUNT;
            } else if (scan == 0x50) {
                selected = (selected + 1) % MW_DISPLAY_MODE_COUNT;
            } else if (scan == 0x49) {
                selected = 0;
            } else if (scan == 0x51) {
                selected = MW_DISPLAY_MODE_COUNT - 1;
            }
            continue;
        }
        if (key >= '1' && key <= '9') {
            selected = key - '1';
            continue;
        }
        if (key >= 'a' && key <= 'c') key -= 'a' - 'A';
        if (key >= 'A' && key <= 'C') {
            selected = 9 + key - 'A';
            continue;
        }
        if (key == '\r') {
            apply = 1;
        } else if (key == INPUT_MOUSE_CLICK) {
            int x, y;
            if (!game_mouse_click_logical(g, &x, &y)) continue;
            if (y >= first_y - 8 &&
                y < first_y - 8 + MW_DISPLAY_MODE_COUNT * row_h) {
                int row = (y - (first_y - 8)) / row_h;
                if (row >= 0 && row < MW_DISPLAY_MODE_COUNT)
                    selected = row;
                continue;
            }
            if (y >= 714 && x >= 60 && x < 430) apply = 1;
            else if (y >= 714 && x >= 590) return;
            else continue;
        } else if (key == 0x1B || key == INPUT_VIDEO_MODE) {
            return;
        } else {
            continue;
        }

        if (apply) {
            if (video_set_display_mode(v, selected, 1) == 0) {
                const MwDisplayModeInfo *info =
                    video_display_mode_info(selected);
                g->video_mode = info->world_mode;
                g->screen_w = info->raster_w;
                g->screen_h = info->raster_h;
                game_load_display_font(g, selected);
                if (save_display_mode_setting(g, selected) != 0) {
                    video_clear(v, 0);
                    video_draw_text(v, 70, 260,
                        "THE DISPLAY CHANGED, BUT MWPORT.CFG COULD NOT BE SAVED.",
                        12);
                    video_draw_text(v, 160, 340,
                        "HIT ANY KEY TO CONTINUE.", 15);
                    video_present(v);
                    input_wait_any_key(&g->input);
                }
            }
            return;
        }
    }
}

static int remove_save_file(Game *g, const char *name) {
    char path[300];
    game_make_path(g, path, sizeof(path), name);
    errno = 0;
    if (remove(path) == 0 || errno == ENOENT) return 0;
    return -1;
}

/* A native Adventure character owns the original numeric character file and
 * three kinds of port sidecar. Delete the complete slot so reusing its number
 * cannot inherit monsters, Beastiary discoveries, or known pitfalls. */
static int delete_adventure_save(Game *g, int slot) {
    if (!g || slot < 0 || slot >= MAX_PLAYERS) return -1;
    char name[32];
    snprintf(name, sizeof(name), "%d", slot);
    if (remove_save_file(g, name) != 0) return -1;

    int sidecar_error = 0;
    make_bestiary_name(name, sizeof(name), slot);
    sidecar_error |= remove_save_file(g, name) != 0;
    make_monster_map_name(name, sizeof(name), slot);
    sidecar_error |= remove_save_file(g, name) != 0;
    for (int group = 0; group <= MAX_DUNGEON_FLOOR / PIT_GROUP_FLOORS; group++) {
        make_pit_name(name, sizeof(name), slot, group);
        sidecar_error |= remove_save_file(g, name) != 0;
    }

    memset(&g->chars[slot], 0, sizeof(g->chars[slot]));
    g->char_exists[slot] = 0;
    if (g->active_save_slot == slot) {
        g->active_save_slot = -1;
        g->monster_map_loaded = 0;
        g->monster_map_dirty = 0;
        g->pit_state_loaded = 0;
        g->pit_state_dirty = 0;
        g->bestiary_loaded = 0;
        g->bestiary_dirty = 0;
        memset(g->bestiary_kills, 0, sizeof(g->bestiary_kills));
    }
    return sidecar_error ? 1 : 0;
}

static void player_delete_notice(Game *g, const char *title,
                                 const char *detail, u8 color) {
    video_clear(&g->video, 0);
    video_draw_text(&g->video, SX(0), SY(140), title, color);
    video_draw_text(&g->video, SX(0), SY(260), detail, 15);
    video_draw_text(&g->video, SX(0), SY(430), "HIT ANY KEY TO CONTINUE.", 7);
    video_present(&g->video);
    input_wait_any_key(&g->input);
}

static int player_confirm_delete(Game *g, int colosseum, int slot,
                                 const char *name) {
    char line[112];
    video_clear(&g->video, 0);
    snprintf(line, sizeof(line), "DELETE %s SAVE %d?",
             colosseum ? "COLOSSEUM" : "ADVENTURE", slot);
    video_draw_text(&g->video, SX(0), SY(120), line, 12);
    snprintf(line, sizeof(line), "CHARACTER: %s", name && name[0] ? name : "(UNNAMED)");
    video_draw_text(&g->video, SX(0), SY(230), line, 15);
    video_draw_text(&g->video, SX(0), SY(360),
                    "THIS PERMANENTLY DELETES THE CHARACTER AND ITS SAVE DATA.", 12);
    video_draw_text(&g->video, SX(110), SY(540), "Y) DELETE", 12);
    video_draw_text(&g->video, SX(610), SY(540), "N) KEEP SAVE", 8);
    video_present(&g->video);
    for (;;) {
        int key = input_wait_any_key(&g->input);
        if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
        if (key == 'Y') return 1;
        if (key == 'N' || key == 0x1B || input_poll_quit(&g->input)) return 0;
        if (key == INPUT_MOUSE_CLICK) {
            int x, y;
            if (game_mouse_click_logical(g, &x, &y) &&
                y >= SY(490) && y < SY(650))
                return x < LOGICAL_W / 2;
        }
    }
}

static void player_delete_slot(Game *g, int colosseum, int slot) {
    char name[sizeof(((Character *)0)->name) + 1];
    memset(name, 0, sizeof(name));
    if (colosseum) {
        ArenaSave arena;
        if (arena_load_save(g, slot, &arena) != 0) {
            player_delete_notice(g, "THAT COLOSSEUM SLOT IS EMPTY.",
                                 "NO FILES WERE CHANGED.", 14);
            return;
        }
        memcpy(name, arena.base_character.name, sizeof(arena.base_character.name));
        if (!player_confirm_delete(g, 1, slot, name)) return;
        if (arena_delete_save(g, slot) != 0) {
            player_delete_notice(g, "COULD NOT DELETE THE COLOSSEUM SAVE.",
                                 "CHECK THAT THE SAVE FILE IS WRITABLE.", 12);
            return;
        }
        player_delete_notice(g, "COLOSSEUM SAVE DELETED.",
                             "THE SLOT IS NOW AVAILABLE.", 8);
        return;
    }

    if (!g->char_exists[slot]) {
        player_delete_notice(g, "THAT ADVENTURE SLOT IS EMPTY.",
                             "NO FILES WERE CHANGED.", 14);
        return;
    }
    memcpy(name, g->chars[slot].name, sizeof(g->chars[slot].name));
    if (!player_confirm_delete(g, 0, slot, name)) return;
    int result = delete_adventure_save(g, slot);
    if (result < 0) {
        player_delete_notice(g, "COULD NOT DELETE THE ADVENTURE SAVE.",
                             "CHECK THAT THE CHARACTER FILE IS WRITABLE.", 12);
    } else if (result > 0) {
        player_delete_notice(g, "CHARACTER DELETED.",
                             "SOME ORPHANED SIDECAR FILES COULD NOT BE REMOVED.", 14);
    } else {
        player_delete_notice(g, "ADVENTURE SAVE DELETED.",
                             "CHARACTER, MAP, MONSTERS, PITS, AND BESTIARY REMOVED.", 8);
    }
}

static int player_select_screen(Game *g, int *colosseum_page) {
    Video *v = &g->video;
    int page = colosseum_page && *colosseum_page ? 1 : 0;
    int delete_mode = 0;
    enum {
        MODE_DESCRIPTION_Y = 55,
        MODE_SWITCH_Y = 105,
        SLOT_HEADER_Y = 165,
        SLOT_FIRST_Y = 220,
        SLOT_ROW_H = 55
    };
    while (1) {
        video_clear(v, 0);
        video_draw_text(v, SX(0), SY(0),
                        page ? "COLOSSEUM - ROGUELIKE BATTLE MODE"
                             : "MAIN GAME - MORAFF'S WORLD ADVENTURE",
                        page ? 14 : 4);
        video_draw_text(v, SX(0), SY(MODE_DESCRIPTION_Y),
                        page ?
                        "RANDOM FOES AND REWARDS; EVERY RUN BUILDS DIFFERENTLY." :
                        "THE ORIGINAL DUNGEON GAME (CLASSIC OR ENHANCED).",
                        page ? 8 : 3);
        video_draw_text(v, SX(0), SY(MODE_SWITCH_Y),
                        page ?
                        "HIT TAB: RETURN TO THE ORIGINAL DUNGEON ADVENTURE." :
                        "HIT TAB: SWITCH TO THE SEPARATE ROGUELIKE COLOSSEUM.",
                        15);
        video_hline(v, SX(0), SY(150), LOGICAL_W, page ? 14 : 4);
        video_draw_text(v, SX(0), SY(SLOT_HEADER_Y),
                        page ? "NUMBER  NAME          MODE    RUN ROUND BEST"
                             : "NUMBER     NAME    SEX   RACE       CLASS",
                        5);

        char line[112];
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (page) {
                ArenaSave arena;
                if (arena_load_save(g, i, &arena) == 0) {
                    const Character *ch = &arena.base_character;
                    snprintf(line, sizeof(line),
                             "%d) %-13.13s %-6s %3u %5u %4u%s",
                             i, ch->name,
                             arena_difficulty_name(arena.difficulty),
                             arena.run_number, arena.round,
                             arena.best_streak,
                             arena.in_run ? "" : "  (RUN ENDED)");
                    video_draw_text(v, SX(0),
                                    SY(SLOT_FIRST_Y + SLOT_ROW_H * i),
                                    line, 6);
                } else {
                    snprintf(line, sizeof(line),
                             "%d) CREATE A NEW COLOSSEUM COMBATANT", i);
                    video_draw_text(v, SX(0),
                                    SY(SLOT_FIRST_Y + SLOT_ROW_H * i),
                                    line, 14);
                }
            } else if (g->char_exists[i]) {
                Character *ch = &g->chars[i];
                const char *race_str = (ch->race < RACE_COUNT) ? race_names[ch->race] : "???";
                const char *class_str = (ch->class_id < CLASS_COUNT) ? class_names[ch->class_id] : "???";
                /* WORLD right-aligns the name, race and class by copying them
                   over fixed runs of 13, 7 and 10 spaces. */
                memset(line, ' ', 41);
                line[41] = 0;
                line[0] = (char)('0' + i);
                line[1] = ')';
                size_t full_name = 0;
                while (full_name < sizeof(ch->name) && ch->name[full_name]) full_name++;
                size_t n = full_name;
                if (n > 13) n = 13; /* Avoid reproducing the DOS buffer underflow. */
                memcpy(line + 15 - n, ch->name + full_name - n, n);
                if (ch->sex) memcpy(line + 15, " FEMALE", 7);
                else memcpy(line + 15, "  MALE ", 7);
                n = strlen(race_str); if (n > 7) n = 7;
                memcpy(line + 29 - n, race_str, n);
                n = strlen(class_str); if (n > 10) n = 10;
                memcpy(line + 41 - n, class_str, n);
                video_draw_text(v, SX(0),
                                SY(SLOT_FIRST_Y + SLOT_ROW_H * i),
                                line, 6);
            } else {
                snprintf(line, sizeof(line), "%d) SELECT TO CREATE A NEW CHARACTER", i);
                video_draw_text(v, SX(0),
                                SY(SLOT_FIRST_Y + SLOT_ROW_H * i),
                                line, 14);
            }
        }

        if (delete_mode) {
            video_draw_text(v, SX(0), SY(1100),
                            "DELETE MODE: PRESS 0-9 OR CLICK AN OCCUPIED SLOT.", 12);
            video_draw_text(v, SX(0), SY(1150),
                            "ESCAPE CANCELS. A FINAL Y/N CONFIRMATION IS REQUIRED.", 14);
        } else {
            video_draw_text(v, SX(0), SY(1100), "0-9 SELECTS", 5);
            video_draw_text(v, SX(360), SY(1100), "D DELETE SAVE", 12);
            video_draw_text(v, SX(780), SY(1100), "ESCAPE OR Q QUITS", 5);
            video_draw_text(v, SX(0), SY(1150),
                            "MAIN GAME AND COLOSSEUM SAVES ARE COMPLETELY SEPARATE.",
                            8);
            video_draw_text(v, SX(1210), SY(1150), "ALT+V VIDEO", 14);
        }
        video_present(v);

        /* Consume a complete DOS key.  Previously Page Down left its 0x51
           scan byte queued, where the next pass read it as ASCII 'Q' and
           unexpectedly quit the program. */
        int key = input_wait_any_key(&g->input);
        if (input_poll_quit(&g->input)) return -1;
        if (key == '\t') {
            page = !page;
            delete_mode = 0;
            if (colosseum_page) *colosseum_page = page;
            continue;
        }
        if (key == INPUT_MOUSE_CLICK) {
            int x, y;
            if (game_mouse_click_logical(g, &x, &y)) {
                if (!delete_mode && y >= SY(1080) && y < SY(1145) &&
                    x >= SX(330) && x < SX(750)) {
                    delete_mode = 1;
                    continue;
                }
                if (y >= SY(MODE_DESCRIPTION_Y - 10) &&
                    y < SY(SLOT_HEADER_Y - 5)) {
                    page = !page;
                    if (colosseum_page) *colosseum_page = page;
                    continue;
                }
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    int top = SY(SLOT_FIRST_Y - 15 + SLOT_ROW_H * i);
                    int bottom = top + SY(SLOT_ROW_H);
                    if (y >= top && y < bottom) {
                        if (delete_mode) {
                            player_delete_slot(g, page, i);
                            delete_mode = 0;
                            break;
                        }
                        if (colosseum_page) *colosseum_page = page;
                        return i;
                    }
                }
            }
            continue;
        }
        if (key >= '0' && key <= '9') {
            if (delete_mode) {
                player_delete_slot(g, page, key - '0');
                delete_mode = 0;
                continue;
            }
            if (colosseum_page) *colosseum_page = page;
            return key - '0';
        }
        if (key == 'd' || key == 'D') {
            delete_mode = 1;
            continue;
        }
        if (key == INPUT_VIDEO_MODE) {
            game_video_mode_menu(g, 0);
            continue;
        }
        if (delete_mode && (key == 0x1B || key == 'q' || key == 'Q')) {
            delete_mode = 0;
            continue;
        }
        if (key == 0x1B || key == 'q' || key == 'Q') return -1;
    }
}

/* MW_PORT: WORLD func_037B5 character-design entry and far_19115 roller,
 * including ROLL.TXT screens, race profile, stat rerolls, name and class. */
/* ── Original character roller (ROLL.TXT / WORLD.ASM far_19115) ── */

#define CREATION_TEXT_LINES 40
typedef struct {
    char line[CREATION_TEXT_LINES][96];
    int count;
} CreationText;

typedef struct {
    u16 height;
    u16 weight;
    u16 lifespan;
} RaceBodyBase;

static const u8 race_stat_base[RACE_COUNT][6] = {
    /* STR INT WIS CON AGI LUCK; the original roller adds 60 points. */
    {12,12,12,12,12,12}, /* Human */
    { 8,13,12, 9,13,11}, /* Elf */
    {14, 7, 9,15,13, 8}, /* Dwarf */
    { 9, 8, 8,13,16,13}, /* Hobbit */
    { 6,14,12, 9,14,11}, /* Gnome */
    {17, 5, 6,15, 7,10}, /* Ogre */
    { 4,15, 9, 6,15,18}, /* Sprite */
    { 4,18,16,10, 7,11}, /* Imp */
    /* MW_EXTENSION: Enhanced races are specialists, not upgrades. Dragonkin
       trade mental ability, agility, and luck for STR/CON; Celestials trade
       physical resilience, agility, and luck for INT/WIS. */
    {18, 7, 6,17, 6, 8}, /* Dragonkin */
    { 5,17,18, 7,11, 8}, /* Celestial */
};

/* Height, weight and lifespan are the final six bytes of each fourteen-byte
 * race record at DS:0150 in WORLD.EXE. */
static const RaceBodyBase race_body_base[RACE_COUNT] = {
    { 70, 130,  65}, { 54,  80, 190}, { 48, 100, 130}, { 42,  60,  70},
    { 38,  60, 130}, {100, 400,  54}, { 24,  20, 150}, { 78, 100, 230},
    { 84, 260, 320}, { 66, 105, 480}
};

static int creation_rand_scaled(Game *g, int range) {
    return original_rand_scaled(g, range);
}

static void roll_character_stats(Game *g, int race, u16 stats[6]) {
    for (int i = 0; i < 6; i++) stats[i] = race_stat_base[race][i];
    for (int point = 0; point < 60; point++)
        stats[creation_rand_scaled(g, 6)]++;
}

static void roll_character_profile(Game *g, int race, int *sex,
                                   u16 *height, u16 *weight, u32 *age,
                                   u16 stats[6]) {
    const RaceBodyBase *body = &race_body_base[race];
    int years = ((int)body->lifespan * (25 + creation_rand_scaled(g, 10))) / 100;
    *age = (u32)years * MW_AGE_YEAR_UNITS;
    *weight = (u16)((int)body->weight + creation_rand_scaled(g, body->weight / 5)
                    - body->weight / 10);
    *height = (u16)((int)body->height + creation_rand_scaled(g, body->height / 5)
                    - body->height / 10);
    *sex = creation_rand_scaled(g, 2);
    roll_character_stats(g, race, stats);
}

static int load_creation_text(Game *g, CreationText *text) {
    char path[260];
    game_make_path(g, path, sizeof(path), "ROLL.TXT");
    FILE *f = fopen(path, "rt");
    if (!f) {
        game_make_path(g, path, sizeof(path), "roll.txt");
        f = fopen(path, "rt");
    }
    memset(text, 0, sizeof(*text));
    if (!f) return 0;
    while (text->count < CREATION_TEXT_LINES &&
           fgets(text->line[text->count], sizeof(text->line[0]), f)) {
        char *line = text->line[text->count];
        line[strcspn(line, "\r\n")] = 0;
        text->count++;
    }
    fclose(f);
    return text->count >= CREATION_TEXT_LINES;
}

static void creation_draw(Game *g, int x, int y, const char *line, u8 color) {
    video_draw_text(&g->video, SX(x), SY(y), line, color);
}

/* WORLD's character creator distributes glyphs between two logical X
 * coordinates. Its name editor uses eighteen fixed character positions,
 * while ordinary fitted text uses the string length. */
static void creation_draw_distributed(Game *g, int x0, int x1, int y,
                                      const char *line, int positions,
                                      u8 color, int sn, int sd) {
    int length = line ? (int)strlen(line) : 0;
    if (length <= 0 || positions <= 0) return;
    for (int i = 0; i < length; i++) {
        int x = x0 + (x1 - x0) * i / positions;
        video_draw_char_scaled(&g->video, SX(x), SY(y), line[i],
                               color, sn, sd);
    }
}

static void creation_draw_fitted(Game *g, int x0, int x1, int y,
                                 const char *line, u8 color, int sn, int sd) {
    creation_draw_distributed(g, x0, x1, y, line,
                              line ? (int)strlen(line) : 0,
                              color, sn, sd);
}

static int creation_key(Game *g) {
    int key = input_getch(&g->input);
    if (key == 0) {
        (void)input_getch(&g->input);
        return 0;
    }
    if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
    return key;
}

static int show_creation_intro(Game *g, const CreationText *text) {
    static const int y[12] = {0,120,220,320,420,520,620,720,820,920,1020,1150};
    static const u8 color[12] = {3,5,5,5,5,5,5,5,8,8,8,4};
    video_clear(&g->video, 0);
    for (int i = 0; i < 12; i++) creation_draw(g, 0, y[i], text->line[i], color[i]);
    video_present(&g->video);
    input_wait_any_key(&g->input);
    return !input_poll_quit(&g->input);
}

static int select_experience_mode(Game *g) {
    video_clear(&g->video, 0);
    creation_draw(g, 0, 0, "SELECT YOUR DUNGEON EXPERIENCE:", 4);
    creation_draw(g, 0, 120, "1) CLASSIC EXPERIENCE", 14);
    creation_draw(g, 70, 210, "251 TOTAL FLOORS: TOWN LEVEL 0 AND", 15);
    creation_draw(g, 70, 280, "DUNGEON LEVELS 1 THROUGH 250.", 15);
    creation_draw(g, 70, 350, "ORIGINAL MONSTERS, QUEST BOSSES, AND", 15);
    creation_draw(g, 70, 420, "ORIGINAL DIG/ASCEND/DESCEND LIMITS.", 15);

    creation_draw(g, 0, 560, "2) ENHANCED EXPERIENCE", 10);
    creation_draw(g, 70, 650, "1001 TOTAL FLOORS: TOWN LEVEL 0 AND", 15);
    creation_draw(g, 70, 720, "DUNGEON LEVELS 1 THROUGH 1000.", 15);
    creation_draw(g, 70, 790, "ADDS NEW MONSTER TIERS, SIX DEEP QUEST", 15);
    creation_draw(g, 70, 860, "BOSSES, POWERFUL ORBS, AND SCALED", 15);
    creation_draw(g, 70, 930, "DIG/ASCEND/DESCEND DEPTH LIMITS.", 15);

    creation_draw(g, 0, 1060, "THIS CHOICE IS SAVED WITH THE CHARACTER.", 6);
    creation_draw(g, 0, 1150, "PRESS 1 OR 2. ESCAPE CANCELS.", 4);
    video_present(&g->video);

    for (;;) {
        int key = creation_key(g);
        if (input_poll_quit(&g->input) || key == 0x1B) return -1;
        if (key == INPUT_MOUSE_CLICK) {
            int x, y;
            if (!game_mouse_click_logical(g, &x, &y)) continue;
            (void)x;
            if (y >= SY(90) && y < SY(510)) key = '1';
            else if (y >= SY(530) && y < SY(1010)) key = '2';
            else continue;
        }
        if (key == '1' || key == 'C') return MW_EXPERIENCE_CLASSIC;
        if (key == '2' || key == 'E') return MW_EXPERIENCE_ENHANCED;
    }
}

static int select_character_race(Game *g, const CreationText *text,
                                 int experience_mode) {
    static const int y[12] = {0,100,150,220,320,420,520,620,720,820,920,1020};
    static const u8 color[12] = {3,4,4,5,8,8,8,8,8,8,8,8};
    static const char *const enhanced_race_name[2] = {
        "9) DRAGONKIN", "0) CELESTIAL"
    };
    static const int enhanced_race_stats[2][6] = {
        {28, 17, 16, 27, 16, 18},
        {15, 27, 28, 17, 21, 18}
    };
    static const int enhanced_stat_x[6] = {
        225, 433, 620, 809, 1014, 1126
    };
    video_clear(&g->video, 0);
    if (experience_mode == MW_EXPERIENCE_ENHANCED) {
        /* MW_EXTENSION: retain the original average-stat table and continue
           it through rows 9 and 0.  Compacting only the vertical spacing
           keeps the two native races in the same readable flow instead of
           presenting them as detached labels in the header. */
        creation_draw(g, 0, 0, text->line[12], color[0]);
        creation_draw(g, 0, 80,
                      "     PLEASE SELECT A RACE BY HITTING 1-9 OR 0. LISTED BELOW",
                      color[1]);
        creation_draw(g, 0, 135, text->line[14], color[2]);
        creation_draw(g, 0, 205, text->line[15], color[3]);
        for (int i = 0; i < 10; i++) {
            int row_y = 285 + i * 82;
            if (i < 8) {
                creation_draw(g, 0, row_y, text->line[16 + i], 8);
            } else {
                int extra = i - 8;
                char value[8];
                /* The original name column was sized for SPRITE.  Tighten
                   only the glyph spacing of the longer native names so the
                   first statistic still begins at WORLD's original tab. */
                creation_draw_fitted(g, 0, 205, row_y,
                                     enhanced_race_name[extra], 8, 1, 1);
                for (int stat = 0; stat < 6; stat++) {
                    snprintf(value, sizeof(value), "%d",
                             enhanced_race_stats[extra][stat]);
                    creation_draw(g, enhanced_stat_x[stat], row_y,
                                  value, 8);
                }
            }
        }
    } else {
        for (int i = 0; i < 12; i++)
            creation_draw(g, 0, y[i], text->line[12 + i], color[i]);
    }
    video_present(&g->video);
    for (;;) {
        int key = creation_key(g);
        if (input_poll_quit(&g->input) || key == 0x1B) return -1;
        if (key == INPUT_MOUSE_CLICK) {
            int x, click_y;
            if (!game_mouse_click_logical(g, &x, &click_y)) continue;
            (void)x;
            int row;
            if (experience_mode == MW_EXPERIENCE_ENHANCED)
                row = (click_y >= SY(260) && click_y < SY(1105)) ?
                      (click_y - SY(260)) * 10 / (SY(1105) - SY(260)) : -1;
            else
                row = game_mouse_row(g, 0, LOGICAL_W, SY(270), SY(100), 8);
            if (row >= 0) return row;
            continue;
        }
        if (key >= '1' && key <= '8') return key - '1';
        if (experience_mode == MW_EXPERIENCE_ENHANCED && key == '9')
            return RACE_DRAGONKIN;
        if (experience_mode == MW_EXPERIENCE_ENHANCED && key == '0')
            return RACE_CELESTIAL;
    }
}

static void draw_character_card(Game *g, int race, int sex,
                                const u16 stats[6], u16 height, u16 weight,
                                u32 age, int show_choices) {
    static const char *const labels[6] = {
        "STRENGTH:", "INTELLIGENCE:", "WISDOM:",
        "CONSTITUTION:", "AGILITY:", "LUCK:"
    };
    char line[96];
    video_clear(&g->video, 0);
    creation_draw(g, 0, 0, "RACE:", 5);
    creation_draw(g, 330, 0, race_names[race], 5);
    creation_draw(g, sex ? 750 : 900, 0,
                  sex ? "SEX: FEMALE" : "SEX: MALE", 15);
    for (int i = 0; i < 6; i++) {
        creation_draw(g, 0, 100 + i * 60, labels[i], 6);
        snprintf(line, sizeof(line), "%u", stats[i]);
        creation_draw(g, 530, 100 + i * 60, line, 6);
    }
    /* WORLD drew padded label/unit strings before painting the numeric values
       into their gaps.  That depended on the exact 1024X768.FNT advance and
       causes the port's fallback font to overwrite INCHES/POUNDS/YEARS.
       Preserve the original three-column presentation with explicit stops. */
    static const char *const measure_label[3] = {
        "HEIGHT:", "WEIGHT:", "AGE:"
    };
    static const char *const measure_unit[3] = {
        "INCHES", "POUNDS", "YEARS"
    };
    const unsigned measure_value[3] = {
        height, weight, (unsigned)(age / MW_AGE_YEAR_UNITS)
    };
    static const int measure_y[3] = {100, 170, 240};
    for (int i = 0; i < 3; i++) {
        creation_draw(g, 750, measure_y[i], measure_label[i], 8);
        snprintf(line, sizeof(line), "%u", measure_value[i]);
        creation_draw(g, 990, measure_y[i], line, 8);
        creation_draw(g, 1160, measure_y[i], measure_unit[i], 8);
    }
    if (show_choices) {
        creation_draw(g, 190, 700, "Y) KEEP THIS CHARACTER", 4);
        creation_draw(g, 190, 770, "N) ROLL A NEW CHARACTER", 4);
        creation_draw(g, 190, 840, "D) DESIGN YOUR OWN CHARACTER", 4);
        creation_draw(g, 190, 1100, "PLEASE SELECT ONE OF THE ABOVE", 4);
    }
}

void game_draw_character_card_test(Game *g) {
    static const u16 stats[6] = {28, 20, 20, 27, 18, 19};
    if (!g) return;
    draw_character_card(g, RACE_DRAGONKIN, 0, stats,
                        82, 198, 148u * MW_AGE_YEAR_UNITS, 1);
    video_present(&g->video);
}

static int design_character_stats(Game *g, int race, int sex, u16 stats[6],
                                  u16 height, u16 weight, u32 age) {
    u16 rolled[6];
    memcpy(rolled, stats, sizeof(rolled));
    for (int i = 0; i < 6; i++) stats[i] -= 4;
    int left = 24;
    while (left > 0 && !input_poll_quit(&g->input)) {
        char line[96];
        draw_character_card(g, race, sex, stats, height, weight, age, 0);
        creation_draw(g, 150, 550, "ESC-CANCEL THIS CHARACTER", 4);
        creation_draw(g, 0, 700, "YOU MAY ASSIGN 24 ADDITIONAL POINTS", 3);
        creation_draw(g, 200, 770, "TO THE ABOVE CHARACTERISTICS.", 3);
        snprintf(line, sizeof(line), "CHARACTERISTIC POINTS LEFT: %d", left);
        creation_draw(g, 0, 840, line, 6);
        creation_draw(g, 0, 930, "PRESS 'S', 'I', 'W', 'C', 'D', OR 'L' FOR", 4);
        creation_draw(g, 120, 1000, "STRENGTH, INTELLIGENCE, WIZDOM", 4);
        creation_draw(g, 120, 1070, "CONSTITUTION, AGILITY OR LUCK", 4);
        video_present(&g->video);
        int key = creation_key(g);
        int which = -1;
        if (key == 0x1B) {
            memcpy(stats, rolled, sizeof(rolled));
            return 0;
        }
        if (key == INPUT_MOUSE_CLICK) {
            int row = game_mouse_row(g, 0, LOGICAL_W, SY(70), SY(60), 6);
            static const int stat_key[6] = {'S','I','W','C','D','L'};
            if (row >= 0) key = stat_key[row];
        }
        if (key == 'S') which = 0;
        else if (key == 'I') which = 1;
        else if (key == 'W') which = 2;
        else if (key == 'C') which = 3;
        else if (key == 'A' || key == 'D') which = 4;
        else if (key == 'L') which = 5;
        if (which >= 0) { stats[which]++; left--; }
    }
    return left == 0;
}

static int read_character_name(Game *g, int race, int sex, const u16 stats[6],
                               u16 height, u16 weight, u32 age, char name[20]) {
    int used = 0;
    name[0] = 0;
    for (;;) {
        draw_character_card(g, race, sex, stats, height, weight, age, 0);
        video_fill_rect(&g->video, 0, SY(530), LOGICAL_W, LOGICAL_H - SY(530), 0);
        creation_draw(g, 0, 700, "PLEASE TYPE YOUR NAME:", 7);
        if (used)
            creation_draw_distributed(g, 0, 1100, 830, name, 18, 4, 1, 1);
        static const char *const keyboard[3] = {
            "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"
        };
        static const int key_count[3] = {10, 9, 7};
        const int key_y[3] = {560, 615, 670};
        const int key_w = 72;
        for (int row = 0; row < 3; row++) {
            int row_x = (LOGICAL_W - key_count[row] * key_w) / 2;
            for (int col = 0; col < key_count[row]; col++) {
                char label[2] = {keyboard[row][col], 0};
                video_fill_rect(&g->video, row_x + col * key_w, key_y[row],
                                key_w - 4, 44, 0);
                video_hline(&g->video, row_x + col * key_w, key_y[row],
                            key_w - 4, 8);
                video_draw_text_scaled_xy(&g->video,
                    row_x + col * key_w + 22, key_y[row] + 5,
                    label, 15, 2, 3, 2, 3);
            }
        }
        video_draw_text_scaled_xy(&g->video, 110, 728,
            "[SPACE]        [BACKSPACE]        [DONE]        [CANCEL]",
            4, 2, 3, 2, 3);
        video_present(&g->video);
        int key = input_getch(&g->input);
        if (key == INPUT_MOUSE_CLICK) {
            int x, y;
            key = 0;
            if (!game_mouse_click_logical(g, &x, &y)) continue;
            for (int row = 0; row < 3 && !key; row++) {
                int row_x = (LOGICAL_W - key_count[row] * key_w) / 2;
                if (y < key_y[row] || y >= key_y[row] + 44 ||
                    x < row_x || x >= row_x + key_count[row] * key_w)
                    continue;
                int col = (x - row_x) / key_w;
                if (col < key_count[row]) key = keyboard[row][col];
            }
            if (!key && y >= 712) {
                if (x < 280) key = ' ';
                else if (x < 560) key = 8;
                else if (x < 780) key = '\r';
                else key = 0x1B;
            }
            if (!key) continue;
        }
        if (input_poll_quit(&g->input) || key == 0x1B) return 0;
        if (key == '\r' || key == '\n') {
            if (used) return 1;
        } else if (key == 8) {
            if (used) name[--used] = 0;
        } else if (key >= 32 && key <= 126 && used < 18) {
            name[used++] = (char)key;
            name[used] = 0;
        } else if (key == 0) {
            (void)input_getch(&g->input);
        }
    }
}

static int select_character_class(Game *g, const CreationText *text,
                                  const char *name, int experience_mode) {
    static const int classic_x0[16] = {
        0,0,90,0,90,0,90,0,90,0,90,0,90,90,0,90
    };
    static const int classic_y[16] = {
        480,550,590,640,680,730,770,820,860,910,950,1000,1040,1080,1125,1165
    };
    static const u8 classic_color[16] = {
        2,3,3,4,4,5,5,6,6,8,8,7,7,7,2,2
    };
    static const int enhanced_indent[20] = {
        0, 0,90, 0,90, 0,90, 0,90, 0,90,
        0,90,90, 0,90, 0,90, 0,90
    };
    static const u8 enhanced_color[20] = {
        2, 3,3, 4,4, 5,5, 6,6, 8,8,
        7,7,7, 2,2, 10,10, 11,11
    };
    static const int enhanced_class_for_line[20] = {
        -1, 0,0, 1,1, 2,2, 3,3, 4,4,
         5,5,5, 6,6, 7,7, 8,8
    };
    static const char *const enhanced_extra[4] = {
        "8) SPELLBLADE: MARTIAL WIZARD WHO MIXES STRONG WEAPONS WITH OFFENSIVE",
        "SPELLS. HARD TO START; NEEDS HIGH STRENGTH, INTELLIGENCE AND AGILITY.",
        "9) PALADIN: ARMORED PRIEST WHO COMBINES WEAPONS WITH DEFENSIVE MAGIC.",
        "POWERFUL BUT SLOW TO LEARN; NEEDS HIGH WISDOM AND CONSTITUTION."
    };

    video_fill_rect(&g->video, 0,
                    SY(experience_mode == MW_EXPERIENCE_ENHANCED ? 450 : 670),
                    LOGICAL_W,
                    LOGICAL_H - SY(experience_mode == MW_EXPERIENCE_ENHANCED ?
                                   450 : 670), 0);
    creation_draw_fitted(g, 700, 880, 310, "NAME:", 8, 1, 1);
    creation_draw_fitted(g, 910, 910 + (int)strlen(name) * 36, 310,
                         name, 8, 1, 1);
    if (experience_mode == MW_EXPERIENCE_ENHANCED) {
        /* MW_EXTENSION: fit all nine choices into the same continuous flow
           used by ROLL.TXT.  The two new classes are full description pairs,
           not labels floating above Steve Moraff's original seven entries. */
        for (int i = 0; i < 20; i++) {
            const char *line;
            int source_y = i == 0 ? 460 : 520 + (i - 1) * 34;
            if (i == 0)
                line = "PLEASE SELECT A CLASS BY HITTING A NUMBER 1-9:";
            else if (i <= 15)
                line = text->line[24 + i];
            else
                line = enhanced_extra[i - 16];
            creation_draw_fitted(g, enhanced_indent[i],
                                 i == 0 ? 1500 : 1600,
                                 source_y, line, enhanced_color[i],
                                 i == 0 ? 1 : 3,
                                 i == 0 ? 1 : 5);
        }
    } else {
        for (int i = 0; i < 16; i++) {
            /* The heading uses font 1; the explanatory rows use the smaller
               font 0 in the DOS executable. */
            int sn = i == 0 ? 1 : 3;
            int sd = i == 0 ? 1 : 4;
            creation_draw_fitted(g, classic_x0[i],
                                 i == 0 ? 1500 : 1600, classic_y[i],
                                 text->line[24 + i], classic_color[i], sn, sd);
        }
    }
    video_present(&g->video);
    for (;;) {
        int key = creation_key(g);
        if (input_poll_quit(&g->input) || key == 0x1B) return -1;
        if (key == INPUT_MOUSE_CLICK) {
            static const int top[7] = {530,620,710,800,890,980,1100};
            static const int bottom[7] = {620,710,800,890,980,1100,1200};
            int x, click_y;
            if (!game_mouse_click_logical(g, &x, &click_y)) continue;
            if (experience_mode == MW_EXPERIENCE_ENHANCED) {
                for (int i = 1; i < 20; i++) {
                    int source_y = 520 + (i - 1) * 34;
                    int next_y = i == 19 ? 1200 : source_y + 34;
                    if (click_y >= SY(source_y - 8) &&
                        click_y < SY(next_y))
                        return enhanced_class_for_line[i];
                }
            } else {
                for (int i = 0; i < 7; i++)
                    if (click_y >= SY(top[i]) && click_y < SY(bottom[i]))
                        return i;
            }
            continue;
        }
        if (key >= '1' && key <= '7') return key - '1';
        if (experience_mode == MW_EXPERIENCE_ENHANCED && key == '8')
            return CLASS_SPELLBLADE;
        if (experience_mode == MW_EXPERIENCE_ENHANCED && key == '9')
            return CLASS_PALADIN;
    }
}

void game_draw_character_races_test(Game *g) {
    CreationText text;
    if (!g || !load_creation_text(g, &text)) return;
    Input saved_input = g->input;
    g->input.head = 0;
    g->input.tail = 1;
    g->input.quit_requested = 0;
    g->input.keys[0] = 0x1B;
    video_clear(&g->video, 0);
    (void)select_character_race(g, &text, MW_EXPERIENCE_ENHANCED);
    g->input = saved_input;
}

void game_draw_character_classes_test(Game *g) {
    CreationText text;
    if (!g || !load_creation_text(g, &text)) return;
    Input saved_input = g->input;
    g->input.head = 0;
    g->input.tail = 1;
    g->input.quit_requested = 0;
    g->input.keys[0] = 0x1B;
    video_clear(&g->video, 0);
    (void)select_character_class(g, &text, "ENHANCED TEST",
                                 MW_EXPERIENCE_ENHANCED);
    g->input = saved_input;
}

static float starting_spell_points(const Character *p) {
    int intelligence = p->stat_int, wisdom = p->stat_wis;
    switch (p->class_id) {
    case CLASS_FIGHTER: return 0.0f;
    case CLASS_WORSHIPPER: return (float)((wisdom * 2 + intelligence) / 4);
    case CLASS_MONK: return (float)((wisdom + intelligence) / 17 + 1);
    case CLASS_WIZARD: return (float)((intelligence * 2 + wisdom) / 7);
    case CLASS_PRIEST: return (float)((wisdom * 2 + intelligence) / 8);
    case CLASS_SAGE: return (float)((wisdom + intelligence) / 18);
    case CLASS_SPELLBLADE: return (float)((intelligence * 2 + wisdom) / 10);
    case CLASS_PALADIN: return (float)((wisdom * 2 + intelligence) / 10);
    default: return (float)((intelligence * 2 + wisdom) / 12);
    }
}

static void grant_starting_spells(Character *p) {
    if (p->class_id == CLASS_MONK) {
        /* Monks begin knowing the complete original catalog.  Enhanced
         * entries 30-34 remain zero until their deep quest bosses fall. */
        for (int category = 0; category < 4; category++)
            memset(p->spells[category], 1, MW_ORIGINAL_SPELL_COUNT);
        return;
    }
    if (p->class_id == CLASS_FIGHTER) return;
    p->spells[SPELL_CAT_PREPARATION][2] = 1; /* Little Cure */
    if (p->class_id == CLASS_WIZARD || p->class_id == CLASS_SAGE ||
        p->class_id == CLASS_MAGE || p->class_id == CLASS_SPELLBLADE)
        p->spells[SPELL_CAT_WIZARD][1] = 1; /* Magic Zap */
    if (p->class_id == CLASS_WORSHIPPER || p->class_id == CLASS_PRIEST ||
        p->class_id == CLASS_SAGE || p->class_id == CLASS_PALADIN)
        p->spells[SPELL_CAT_PRIEST][2] = 1; /* Strength */
}

static int character_creation_self_test(void) {
    int failures = 0;
    static Game g;
    memset(&g, 0, sizeof(g));
    game_srand(&g, 0x19115u);

    for (int race = 0; race < RACE_COUNT; race++) {
        const RaceBodyBase *body = &race_body_base[race];
        int min_height = body->height - body->height / 10;
        int max_height = body->height + body->height / 5 - 1 - body->height / 10;
        int min_weight = body->weight - body->weight / 10;
        int max_weight = body->weight + body->weight / 5 - 1 - body->weight / 10;
        int min_years = body->lifespan * 25 / 100;
        int max_years = body->lifespan * 34 / 100;
        for (int roll = 0; roll < 64; roll++) {
            int sex;
            u16 height, weight, stats[6];
            u32 age;
            roll_character_profile(&g, race, &sex, &height, &weight, &age, stats);
            int bonus = 0;
            for (int i = 0; i < 6; i++)
                bonus += (int)stats[i] - race_stat_base[race][i];
            int years = (int)(age / MW_AGE_YEAR_UNITS);
            if (bonus != 60 || sex < 0 || sex > 1 ||
                age % MW_AGE_YEAR_UNITS != 0 ||
                height < min_height || height > max_height ||
                weight < min_weight || weight > max_weight ||
                years < min_years || years > max_years) {
                failures++;
                break;
            }
        }
    }

    static const int expected_spells[CLASS_COUNT] = {0, 2, 120, 2, 2, 3, 2, 2, 2};
    for (int class_id = 0; class_id < CLASS_COUNT; class_id++) {
        Character p = {0};
        p.class_id = (u8)class_id;
        grant_starting_spells(&p);
        int known = 0;
        for (int category = 0; category < 4; category++)
            for (int spell = 0; spell < SPELLS_PER_TYPE; spell++)
                known += p.spells[category][spell] != 0;
        if (known != expected_spells[class_id]) failures++;
    }
    {
        Character p = {0};
        mw_character_native_ensure(&p);
        if (mw_experience_mode(&p) != MW_EXPERIENCE_ENHANCED)
            failures++;
        mw_set_experience_mode(&p, MW_EXPERIENCE_CLASSIC);
        if (mw_experience_mode(&p) != MW_EXPERIENCE_CLASSIC)
            failures++;
        mw_set_experience_mode(&p, MW_EXPERIENCE_ENHANCED);
        if (mw_experience_mode(&p) != MW_EXPERIENCE_ENHANCED)
            failures++;
    }
    return failures;
}

static int create_character(Game *g, Character *p,
                            int forced_experience_mode) {
    CreationText text;
    if (!load_creation_text(g, &text)) {
        video_clear(&g->video, 0);
        creation_draw(g, 0, 0,
                      "I CAN'T FIND THE FILE ROLL.TXT. TRY TO FIND A COMPLETE COPY.", 4);
        video_present(&g->video);
        input_wait_any_key(&g->input);
        return 0;
    }
    if (!show_creation_intro(g, &text)) return 0;
    int experience_mode = forced_experience_mode >= 0 ?
                          forced_experience_mode : select_experience_mode(g);
    if (experience_mode < 0) return 0;
    int race = select_character_race(g, &text, experience_mode);
    if (race < 0) return 0;
    int sex;
    u16 height, weight;
    u32 age;
    u16 stats[6];
    roll_character_profile(g, race, &sex, &height, &weight, &age, stats);
    for (;;) {
        draw_character_card(g, race, sex, stats, height, weight, age, 1);
        video_present(&g->video);
        int key = creation_key(g);
        if (key == INPUT_MOUSE_CLICK) {
            int row = game_mouse_row(g, SX(150), SX(1300), SY(665),
                                     SY(70), 3);
            static const int choice_key[3] = {'Y','N','D'};
            if (row >= 0) key = choice_key[row];
        }
        if (key == 'Y') break;
        if (key == 'N')
            roll_character_profile(g, race, &sex, &height, &weight, &age, stats);
        else if (key == 'D') {
            if (design_character_stats(g, race, sex, stats, height, weight, age)) break;
        }
        else if (key == 0x1B || input_poll_quit(&g->input)) return 0;
    }

    char name[20];
    if (!read_character_name(g, race, sex, stats, height, weight, age, name)) return 0;
    int class_id = select_character_class(g, &text, name, experience_mode);
    if (class_id < 0) return 0;

    memset(p, 0, sizeof(*p));
    memcpy(p->name, name, strlen(name) + 1);
    p->race = (u8)race;
    p->sex = (u8)sex;
    p->class_id = (u8)class_id;
    p->stat_str = stats[0]; p->stat_int = stats[1]; p->stat_wis = stats[2];
    p->stat_con = stats[3]; p->stat_agi = stats[4]; p->stat_luck = stats[5];
    p->height_inches = height;
    p->weight_pounds = weight;
    p->hp_max = p->hp_cur = (u16)(p->stat_con + p->stat_luck);
    p->sp_max = p->sp_cur = starting_spell_points(p);
    p->level = 0;
    p->experience = 0.0;
    p->age = age;
    p->weapon_inventory[0] = 1;
    p->armor_inventory[0] = 1;
    p->jewels_pocket = (u32)(creation_rand_scaled(g, p->stat_luck * 2) +
                              p->stat_luck * 2);
    p->x_pos = 0x38;
    p->y_pos = 0x3C;
    p->floor_depth = 0;
    p->_pad_7B4[0] = 8;
    p->_pad_7B4[1] = 11;
    p->_pad_7F8[0] = 0x62; p->_pad_7F8[1] = 0x08;
    p->_pad_7F8[2] = 0x97; p->_pad_7F8[3] = 0x05;
    p->raise_floor = 0;
    p->raise_x = 0x38;
    p->raise_y = 0x3C;
    p->_pad_80A[0] = 0x2C;
    p->_pad_80A[1] = 0x01;
    grant_starting_spells(p);
    mw_set_experience_mode(p, experience_mode);

    draw_character_card(g, race, sex, stats, height, weight, age, 0);
    creation_draw_fitted(g, 700, 880, 310, "NAME:", 8, 1, 1);
    creation_draw_fitted(g, 910, 910 + (int)strlen(name) * 36, 310,
                         name, 8, 1, 1);
    creation_draw_fitted(g, 700, 920, 380, "CLASS:", 8, 1, 1);
    creation_draw(g, 980, 380, class_names[class_id], 8);
    char summary[96];
    snprintf(summary, sizeof(summary), "SPELL POINTS: %.0f    HEALTH POINTS: %u",
             p->sp_max, mw_hp_max(p));
    creation_draw(g, 0, 460, summary, 4);
    snprintf(summary, sizeof(summary), "EXPERIENCE: %s - DUNGEON LEVELS 0-%d",
             experience_mode == MW_EXPERIENCE_CLASSIC ? "CLASSIC" : "ENHANCED",
             experience_mode == MW_EXPERIENCE_CLASSIC ?
             CLASSIC_DUNGEON_FLOOR : MAX_DUNGEON_FLOOR);
    creation_draw(g, 0, 520, summary, 10);
    video_present(&g->video);
    input_wait_any_key(&g->input);
    return 1;
}

/* ── Find a valid starting position ── */

static void find_start_pos(Game *g) {
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            u8 cell = map_get_cell(g, x, y);
            if (((cell & 3) == 3) || (((cell >> 2) & 3) == 3) ||
                (((cell >> 4) & 3) == 3) || (((cell >> 6) & 3) == 3)) {
                g->cur_x = x;
                g->cur_y = y;
                return;
            }
        }
    }
    g->cur_x = MAP_W / 2;
    g->cur_y = MAP_H / 2;
}

/* MW_PORT: WORLD func_09148 encounter setup, combat_encounter (0x018FE),
 * func_0EAE9 proximity handling, and func_21F9C kill/reward hand-off. */
/* ── Monster encounter check ── */

static void record_bestiary_kill(Game *g, int monster_type) {
    if (monster_type < 0 || monster_type >= MONSTER_TYPE_COUNT ||
        !combat_monster_type_spawnable(monster_type)) return;
    if (g->bestiary_kills[monster_type] != UINT32_MAX)
        g->bestiary_kills[monster_type]++;
    g->bestiary_dirty = 1;
    /* A crash after the reward screen must not erase a first discovery. */
    game_save_bestiary(g);
}

static int fight_monster_action(Game *g, Character *player, int index,
                                int action) {
    if (index < 0 || g->monster_layer < 0) return 0;
    int old_floor = g->cur_floor;
    int old_layer = g->monster_layer;
    CombatState cs;
    combat_init_entity(g, &cs, index);
    /* Hold/Stop/Sleep durations must survive the return to WORLD's normal
       command dispatcher between individual battle actions. */
    cs.monster_held = player->eff_hold_monster;
    cs.monster_stopped = player->eff_stop_monster;
    if (!combat_take_turn(g, &cs, player, action)) return 0;

    if (g->cur_floor != old_floor || g->monster_layer != old_layer) {
        g->monster_adjacent = game_find_adjacent_monster(g) >= 0;
        return 1;
    }
    if (cs.monster_hp <= 0) {
        record_bestiary_kill(g, cs.monster_type_idx);
        game_kill_monster(g, index);
        grant_battle_rewards(g, player, &cs);
    } else if (cs.fled) {
        game_kill_monster(g, index);
    } else {
        game_set_monster_hp(g, index, cs.monster_hp);
        if (index >= 0 && index < MONSTERS_PER_FLOOR) {
            int level = cs.monster_level < 1 ? 1 : cs.monster_level;
            if (level > UINT16_MAX) level = UINT16_MAX;
            g->monster_map[g->monster_layer][index].level = (u16)level;
            g->monster_map_dirty = 1;
        }
    }
    g->monster_adjacent = game_find_adjacent_monster(g) >= 0;
    return 1;
}

static void fight_monster(Game *g, Character *player, int index) {
    (void)fight_monster_action(g, player, index, COMBAT_ACTION_FIGHT);
}

/* MW_PORT: exploration dispatcher for WORLD spell_menu/cast_spell and
 * func_10E9A..func_11DA5; implementations are in mw_combat.c. */
/* ── Cast spell from exploration (handles battle vs prep) ── */

static int cmd_cast_spell(Game *g, Character *player) {
    int monster = game_find_adjacent_monster(g);
    if (monster >= 0)
        return fight_monster_action(g, player, monster,
                                    COMBAT_ACTION_CAST) ? 2 : 0;
    return cmd_cast_spell_menu(g, player, NULL) ? 1 : 0;
}

static int cmd_use_item_exploration(Game *g, Character *player) {
    int monster = game_find_adjacent_monster(g);
    if (monster >= 0)
        return fight_monster_action(g, player, monster,
                                    COMBAT_ACTION_ITEM) ? 2 : 0;
    return cmd_use_item(g, player, NULL) ? 1 : 0;
}

/* MW_PORT: dungeon branch of WORLD func_0F6E5, including
 * movement, auto-opening doors, encounter blocking, commands, pits, ladders,
 * shops, effect turns, save/quit, death and raise-contract recovery. */
/* ── Main game loop ── */

static void game_draw_exploration_base(Game *g, Character *player) {
    /* Tests and loaded saves can assign cur_floor directly, so refresh here
     * as well as at transitions.  This mirrors WORLD's palette setup at the
     * beginning of every exploration redraw. */
    game_refresh_world_palette(g);
    video_clear(&g->video, 0);
    draw_4way_view(g);
    {
        int mx, my, mw, mh;
        game_normal_map_rect(g, &mx, &my, &mw, &mh);
        draw_minimap(g, mx, my, mw, mh);
    }
    draw_command_menu(g);
    draw_status_bar(g, player);
    draw_contextual_advice(g, player);
    draw_trapdoor_notice(g, player);
}

/* Draw a line into the original upper-left combat message window.  The DOS
 * text is intentionally wrapped at word boundaries instead of spilling over
 * the north viewport. */
static int draw_combat_message(Video *v, int y, int bottom,
                               const char *text, u8 color) {
    enum { MAX_CHARS = 28 };
    const int row_h = SY(38);
    const char *p = text;
    char line[MAX_CHARS + 1];

    if (!p || !*p) return y;
    while (*p && y + row_h <= bottom) {
        while (*p == ' ') p++;
        int remaining = (int)strlen(p);
        int take = remaining < MAX_CHARS ? remaining : MAX_CHARS;
        if (remaining > MAX_CHARS) {
            int split = take;
            while (split > 0 && p[split] != ' ') split--;
            if (split > 0) take = split;
        }
        memcpy(line, p, (size_t)take);
        line[take] = '\0';
        while (take > 0 && line[take - 1] == ' ') line[--take] = '\0';
        video_draw_text_scaled_xy(v, 0, y, line, color,
                                  7, 6, 12, 17);
        y += row_h;
        p += take;
        while (*p == ' ') p++;
    }
    return y;
}

/* MW_PORT: WORLD func_1D5A7.  The routine was previously mislabeled as the
 * wilderness main loop; it is the rotating tutorial and condition-advice
 * writer used by the otherwise black upper-left pane. */
static void draw_contextual_advice(Game *g, Character *p) {
    static const char *const tutorial[][4] = {
        {"OBJECTIVE: USE ARROW KEYS TO", "EXPLORE THE DUNGEON. USE THE",
         "LADDERS TO DESCEND TO DEEPER,", "MORE DANGEROUS PLACES."},
        {"IF YOU HAVE A MOUSE, JUST", "POINT TO THINGS AND PRESS",
         "BUTTONS TO SEE WHAT HAPPENS.", ""},
        {"ON THE LEFT IS A MAP SHOWING", "THE AREA AROUND YOU. SLANTED",
         "LINES SHOW LADDERS GOING UP", "AND DOWN."},
        {"USE THE CURSOR KEYS TO MOVE.", "UP IS NORTH, DOWN IS SOUTH,",
         "LEFT IS WEST, RIGHT IS EAST.", "HIT F1 FOR MORE INFORMATION."},
        {"MONSTERS ARE ONLY FOUND IN", "THE DUNGEON. YOU ARE IN THE",
         "TOWN NOW, SO YOU MUST FIND A", "LADDER AND GO DOWN IT."},
        {"YOUR MISSION: FIND TREASURES", "AND MONEY, GAIN POWER BY",
         "DEFEATING MONSTERS, ENJOY", "THE FUN AND EXCITEMENT."}
    };
    const char *line[4] = {NULL, NULL, NULL, NULL};
    u8 color = 3;
    if (mw_hp_max(p) && (uint64_t)mw_hp_cur(p) * 3u < mw_hp_max(p)) {
        line[0] = "YOU ARE BADLY DAMAGED. YOU";
        line[1] = "SHOULD CURE YOURSELF WITH";
        line[2] = "THE CURE SPELL OR GO SEARCH";
        line[3] = "THE TOWN FOR A TEMPLE.";
        color = 5;
    } else if (p->poisoned_turns) {
        line[0] = "YOU HAVE BEEN POISONED. FOR";
        line[1] = "A FEW JEWELS YOU CAN GET A";
        line[2] = "CURE POISON AT A TEMPLE.";
        color = 7;
    } else if (p->diseased_turns) {
        line[0] = "YOU DON'T FEEL VERY WELL.";
        line[1] = "YOU SHOULD REALLY TRY TO";
        line[2] = "GET A CURE DISEASE AT A";
        line[3] = "TEMPLE IN THE TOWN.";
        color = 8;
    } else if (p->experience >= experience_for_level((int)p->level + 1)) {
        line[0] = "YOU ARE READY TO GAIN A";
        line[1] = "LEVEL, WHICH WILL MAKE YOU";
        line[2] = "MORE POWERFUL. YOU MUST STAY";
        line[3] = "AT AN INN TO GAIN A LEVEL.";
        color = 6;
    } else if (p->sp_max > 0.0f && p->sp_cur * 4.0f < p->sp_max) {
        line[0] = "YOU ARE RUNNING LOW ON SPELL";
        line[1] = "POINTS. YOU CAN REGAIN YOUR";
        line[2] = "SPELL POINTS BY STAYING AT";
        line[3] = "AN INN IN THE TOWN.";
        color = 6;
    } else if (game_loaded_weight(p) >
               (unsigned long long)p->weight_pounds + (unsigned long long)p->stat_str * 12u) {
        line[0] = "YOU ARE CARRYING A LOT OF";
        line[1] = "WEIGHT. THIS ALLOWS MONSTERS";
        line[2] = "TO TAKE MORE STRIKES AT YOU.";
        line[3] = "YOU SHOULD GO FIND A BANK.";
        color = 5;
    } else if (g->cur_floor == 0) {
        int topic = (int)((g->advice_counter / 8u) % 6u);
        for (int i = 0; i < 4; i++) line[i] = tutorial[topic][i];
    } else {
        return;
    }

    int y = 0;
    const int bottom = SY(0x1AE);
    for (int i = 0; i < 4 && line[i] && *line[i]; i++)
        y = draw_combat_message(&g->video, y, bottom, line[i], color);
}

/* MW_PORT: WORLD func_0E913: a hidden chute interrupts only the upper-left pane. Its
 * opening warning appears alone for 1500 ms, then the explanation waits for
 * a complete keypress.  Keep the destination unseen until that acknowledgement
 * so the visible sequence is warning -> key -> new floor. */
static int game_apply_pitfall_interactive(Game *g, Character *player) {
    int target = pitfall_destination(g);
    if (target == g->cur_floor) return 0;

    Video *v = &g->video;
    const int message_w = SX(0x2D3);
    const int message_bottom = SY(0x1AE);

    game_draw_exploration_base(g, player);
    video_fill_rect(v, 0, 0, message_w, message_bottom, 0);
    video_draw_text_scaled_xy(v, 0, 0,
                              "UH OH... A SINKING FEELING...", 5,
                              7, 6, 12, 17);
    video_present(v);
    mw_audio_play(&g->audio, MW_SFX_FALL);
    game_delay(g, 1500);

    video_draw_text_scaled_xy(v, 0, SY(0x28),
                              "YOU HAVE FALLEN DOWN A CHUTE!", 5,
                              7, 6, 12, 17);
    video_draw_text_scaled_xy(v, 0, SY(0x50),
                              "  HIT ANY KEY TO CONTINUE...", 5,
                              7, 6, 12, 17);
    video_present(v);
    input_wait_any_key(&g->input);

    game_change_floor(g, player, target);
    save_pit_group(g);
    return 1;
}

/* One movement implementation serves both DOS extended-arrow input and the
 * four mouse view hit regions.  In particular, a monster immediately behind
 * a door blocks the step without being revealed or engaged. */
static void show_movement_block(Game *g, Character *player,
                                const char *message) {
    game_draw_exploration_base(g, player);
    video_fill_rect(&g->video, 0, 0, SX(0x2D3), SY(42), 0);
    video_draw_text_scaled_xy(&g->video, 8, 8, message, 12,
                              7, 6, 12, 17);
    video_present(&g->video);
    game_delay(g, 350);
}

static void show_runtime_indicator(Game *g, Character *player,
                                   const char *title, const char *detail,
                                   u8 color) {
    const int pane_w = SX(0x2D3), pane_h = SY(0x78);
    game_draw_exploration_base(g, player);
    video_fill_rect(&g->video, 0, 0, pane_w, pane_h, 0);
    int y = draw_combat_message(&g->video, 0, pane_h, title, color);
    draw_combat_message(&g->video, y, pane_h, detail, 15);
    video_present(&g->video);
    mw_audio_play(&g->audio, MW_SFX_UI);
    game_delay(g, 700);
}

static void show_turbo_indicator(Game *g, Character *player) {
    char detail[96];
    const int pane_w = SX(0x2D3), pane_h = SY(0x78);
    game_draw_exploration_base(g, player);
    video_fill_rect(&g->video, 0, 0, pane_w, pane_h, 0);
    int y = draw_combat_message(&g->video, 0, pane_h,
        g->turbo_enabled ? "TURBO MODE: ON" : "TURBO MODE: OFF",
        g->turbo_enabled ? 4 : 8);
    if (g->turbo_enabled)
        snprintf(detail, sizeof(detail),
                 "GAME SPEED: %d%%   USE + OR - TO ADJUST.",
                 g->turbo_percent);
    else
        snprintf(detail, sizeof(detail),
                 "NORMAL 100%% TIMING RESTORED.");
    draw_combat_message(&g->video, y, pane_h, detail, 15);
    video_present(&g->video);
    mw_audio_play(&g->audio, MW_SFX_UI);
    /* Keep the control readout legible even at 1000 percent. */
    SDL_Delay(700);
}

static int confirm_dungeon_reroll(Game *g, Character *player) {
    const int pane_w = SX(0x2D3), pane_h = SY(0x1AE);
    game_draw_exploration_base(g, player);
    video_fill_rect(&g->video, 0, 0, pane_w, pane_h, 0);
    int y = draw_combat_message(&g->video, 0, pane_h,
                                "REROLL THE ENTIRE DUNGEON?", 12);
    y = draw_combat_message(&g->video, y, pane_h,
                            "THIS CREATES A NEW DUNGEON AND RETURNS YOU TO TOWN.", 15);
    draw_combat_message(&g->video, y, pane_h,
                        "PRESS Y TO CONTINUE OR N TO CANCEL.", 4);
    video_present(&g->video);
    for (;;) {
        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input) || key == 0x1B ||
            key == 'n' || key == 'N')
            return 0;
        if (key == 'y' || key == 'Y') return 1;
        if (key == 0) (void)input_getch(&g->input);
    }
}

static int confirm_town_teleport(Game *g, Character *player) {
    const int pane_w = SX(0x2D3), pane_h = SY(0x1AE);
    game_draw_exploration_base(g, player);
    video_fill_rect(&g->video, 0, 0, pane_w, pane_h, 0);
    int y = draw_combat_message(&g->video, 0, pane_h,
                                "TELEPORT TO FLOOR-ZERO TOWN?", 12);
    y = draw_combat_message(&g->video, y, pane_h,
                            "YOUR CURRENT DUNGEON LOCATION WILL BE LEFT.", 15);
    draw_combat_message(&g->video, y, pane_h,
                        "PRESS Y TO TELEPORT OR N TO CANCEL.", 4);
    video_present(&g->video);
    for (;;) {
        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input) || key == 0x1B ||
            key == 'n' || key == 'N')
            return 0;
        if (key == 'y' || key == 'Y') return 1;
        if (key == 0) (void)input_getch(&g->input);
    }
}

/* MW_EXTENSION: Ctrl+Shift+Alt+F12 is an intentionally extreme, persistent
 * character maximizer.  Every assignment uses the same mode-aware caps as
 * the trainer and every byte inventory stops at 255, so later rewards cannot
 * roll a maxed field through zero. */
void game_max_character(Game *g, Character *player) {
    if (!player) return;

    mw_character_native_ensure(player);
    const int enhanced =
        mw_experience_mode(player) == MW_EXPERIENCE_ENHANCED;
    const int enchant_cap = enhanced ? INT16_MAX : INT8_MAX;
    const u8 pill_cap = enhanced ? UINT8_MAX : INT8_MAX;
    const u16 turn_cap = mw_effect_turn_cap(player);

    /* Remove reversible bonuses before replacing the underlying attributes;
       otherwise their eventual expiration would subtract from the maximum. */
    character_clear_battle_effects(player);
    character_clear_town_effects(player);

    player->level = MW_PLAYER_LEVEL_MAX;
    mw_set_hp_max(player, mw_player_hp_cap(player));
    mw_set_hp_cur(player, mw_hp_max(player));
    player->sp_max = MW_PLAYER_SP_MAX;
    player->sp_cur = player->sp_max;
    player->stat_str = MW_PLAYER_STAT_MAX;
    player->stat_int = MW_PLAYER_STAT_MAX;
    player->stat_wis = MW_PLAYER_STAT_MAX;
    player->stat_con = MW_PLAYER_STAT_MAX;
    player->stat_agi = MW_PLAYER_STAT_MAX;
    player->stat_luck = MW_PLAYER_STAT_MAX;
    player->experience = experience_for_level(MW_PLAYER_LEVEL_MAX);

    player->jewels_bank = UINT32_MAX;
    player->copper_stones = UINT32_MAX;
    player->silver_stones = UINT32_MAX;
    player->ivory_stones = UINT32_MAX;
    player->gold_stones = UINT32_MAX;
    player->platinum_stones = UINT32_MAX;
    player->jewel_stones = UINT32_MAX;

    for (int slot = 0; slot < 8; slot++) {
        if (slot > 0) {
            mw_set_weapon_inventory_count(player, slot, UINT8_MAX);
            mw_set_armor_inventory_count(player, slot, UINT8_MAX);
        }
        mw_set_weapon_enchant(player, slot, enchant_cap);
        mw_set_armor_enchant(player, slot, enchant_cap);
    }
    if (enhanced) {
        for (int deep = 0; deep < MW_DEEP_SPELL_COUNT; deep++)
            mw_unlock_deep_spell_tier(player, deep);
        player->native.late_gear_unlocks = UINT8_MAX;
        for (int slot = 12; slot < WEAPON_STAT_COUNT; slot++) {
            mw_set_weapon_inventory_count(player, slot, UINT8_MAX);
            mw_set_weapon_enchant(player, slot, enchant_cap);
        }
        for (int slot = 8; slot < ARMOR_STAT_COUNT; slot++) {
            mw_set_armor_inventory_count(player, slot, UINT8_MAX);
            mw_set_armor_enchant(player, slot, enchant_cap);
        }
        player->equipped_weapon = WEAPON_STAT_COUNT - 1;
        player->equipped_armor = ARMOR_STAT_COUNT - 1;
    } else {
        player->equipped_weapon = 7;
        player->equipped_armor = 7;
    }

    int spell_count = mw_spell_catalog_count(player);
    for (int category = 0; category < 4; category++)
        for (int spell = 0; spell < spell_count; spell++) {
            player->spells[category][spell] = 1;
            player->scrolls[category][spell] = UINT8_MAX;
            player->wands[category][spell] = UINT8_MAX;
            player->papers[category][spell] = UINT8_MAX;
        }

    player->holy_grenade = UINT8_MAX;
    player->stone_teleport = UINT8_MAX;
    player->stone_see = UINT8_MAX;
    player->floor_slosher = 1;
    player->potion_heal = UINT8_MAX;
    player->ring_regen = UINT8_MAX;
    player->combat_bonus = UINT8_MAX;
    player->green_pill = pill_cap;
    player->orange_pill = pill_cap;
    player->blue_pill = pill_cap;
    player->red_pill = pill_cap;
    player->white_pill = pill_cap;
    player->yellow_pill = pill_cap;
    player->antimagic_ring = 5;
    memset(player->trapdoor_keys, 1, sizeof(player->trapdoor_keys));

    mw_set_enchant_wpn_spell(player, enchant_cap);
    mw_set_armor_plus(player, enchant_cap);
    mw_set_body_armor_plus(player, enchant_cap);
    mw_set_ring_prot_plus(player, enchant_cap);
    mw_set_gauntlet(player, enchant_cap);
    player->eff_feather = 100;
    player->eff_fast_move = 60;
    player->eff_invisible = 100;
    player->eff_str_bonus = 60;
    player->eff_agi_bonus = 60;
    player->eff_super_str = 60;
    player->eff_super_agi = 60;
    player->eff_battle_str = turn_cap;
    player->eff_battle_spd = turn_cap;
    player->eff_slow_mon = turn_cap;
    player->eff_pwr_weapon = enhanced ? 6 : 3;
    player->eff_pwr_wpn_turns = turn_cap;
    player->eff_protect_lv = enhanced ? 10 : 5;
    player->eff_protect_turns = turn_cap;
    player->eff_resist_poison = turn_cap;
    player->eff_resist_disease = turn_cap;
    player->eff_anti_cold = turn_cap;
    player->eff_anti_fire = turn_cap;
    player->eff_resist_drain = turn_cap;
    player->eff_stop_monster = turn_cap;
    player->eff_hold_monster = turn_cap;
    player->poisoned_turns = 0;
    player->diseased_turns = 0;

    u16 quest_flags = enhanced ? UINT16_MAX :
                      (u16)(((1u << 8) - 1u) |
                            MW_UNIVERSAL_ACCESS_FLAG);
    mw_set_quest_flags(player, quest_flags);
    if (enhanced) {
        for (int relic = 0; relic < MW_RELIC_COUNT; relic++)
            mw_set_relic_owned(player, relic, 1);
        player->native.relic_regen_phase = 0;
        player->native.relic_phoenix_cooldown = 0;
    }

    if (g) {
        player->raise_floor = (u16)game_clamp_dungeon_floor(g, g->cur_floor);
        player->raise_x = (u16)g->cur_x;
        player->raise_y = (u16)g->cur_y;
    }
}

static int confirm_max_character(Game *g, Character *player) {
    const int pane_w = SX(0x2D3), pane_h = SY(0x1AE);
    game_draw_exploration_base(g, player);
    video_fill_rect(&g->video, 0, 0, pane_w, pane_h, 0);
    int y = draw_combat_message(&g->video, 0, pane_h,
                                "MAX OUT THIS CHARACTER?", 12);
    y = draw_combat_message(&g->video, y, pane_h,
                            "THIS MAXES STATS, INVENTORY, MAGIC, GEAR, AND ACCESS.", 15);
    y = draw_combat_message(&g->video, y, pane_h,
                            "THE CHANGES ARE STORED WHEN YOU NEXT SAVE.", 15);
    draw_combat_message(&g->video, y, pane_h,
                        "PRESS Y TO MAX OUT OR N TO CANCEL.", 4);
    video_present(&g->video);
    for (;;) {
        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input) || key == 0x1B ||
            key == 'n' || key == 'N')
            return 0;
        if (key == 'y' || key == 'Y') return 1;
        if (key == 0) (void)input_getch(&g->input);
    }
}

void game_debug_max_character(Game *g, Character *player) {
    if (!g || !player) return;
    if (!confirm_max_character(g, player)) {
        show_runtime_indicator(g, player, "MAX CHARACTER CANCELLED.",
                               "NO CHARACTER VALUES WERE CHANGED.", 8);
        return;
    }
    game_max_character(g, player);
    show_runtime_indicator(
        g, player, "CHARACTER MAXIMIZED.",
        mw_experience_mode(player) == MW_EXPERIENCE_ENHANCED ?
        "ALL ENHANCED STATS, ITEMS, SPELLS, AND ACCESS ARE MAXED." :
        "ALL CLASSIC STATS, ITEMS, SPELLS, AND ACCESS ARE MAXED.",
        4);
}

static void debug_teleport_to_town(Game *g, Character *player) {
    if (!confirm_town_teleport(g, player)) {
        show_runtime_indicator(g, player, "TOWN TELEPORT CANCELLED.",
                               "YOUR LOCATION IS UNCHANGED.", 8);
        return;
    }
    game_change_floor(g, player, 0);
    game_relocate(g, player);
    show_runtime_indicator(g, player, "TOWN TELEPORT COMPLETE.",
                           "YOU ARE NOW ON FLOOR ZERO.", 4);
}

static int confirm_quest_boss_warp(Game *g, Character *player,
                                   const char *boss_name) {
    const int pane_w = SX(0x2D3), pane_h = SY(0x1AE);
    char line[96];
    game_draw_exploration_base(g, player);
    video_fill_rect(&g->video, 0, 0, pane_w, pane_h, 0);
    int y = draw_combat_message(&g->video, 0, pane_h,
                                "WARP TO THIS FLOOR'S QUEST BOSS?", 12);
    snprintf(line, sizeof(line), "TARGET: %s.", boss_name);
    y = draw_combat_message(&g->video, y, pane_h, line, 15);
    draw_combat_message(&g->video, y, pane_h,
                        "PRESS Y TO WARP OR N TO CANCEL.", 4);
    video_present(&g->video);
    for (;;) {
        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input) || key == 0x1B ||
            key == 'n' || key == 'N')
            return 0;
        if (key == 'y' || key == 'Y') return 1;
        if (key == 0) (void)input_getch(&g->input);
    }
}

static void debug_warp_to_quest_boss(Game *g, Character *player) {
    int type = quest_boss_type(g, g->cur_floor);
    if (type < 0) {
        show_runtime_indicator(g, player, "NO ACTIVE QUEST BOSS HERE.",
            "THIS FLOOR HAS NO UNDEFEATED QUEST BOSS.", 8);
        return;
    }
    if (g->monster_layer < 0)
        select_monster_floor(g, g->cur_floor);
    if (g->monster_layer < 0) {
        show_runtime_indicator(g, player, "QUEST BOSS NOT AVAILABLE.",
            "THIS FLOOR'S MONSTER LAYER COULD NOT BE LOADED.", 8);
        return;
    }
    int boss = -1;
    for (int i = 0; i < MONSTERS_PER_FLOOR; i++) {
        MonsterRecord *m = &g->monster_map[g->monster_layer][i];
        if (monster_record_alive(g, m) && m->type == type) {
            boss = i;
            break;
        }
    }
    if (boss < 0) {
        show_runtime_indicator(g, player, "QUEST BOSS NOT FOUND.",
            "REENTER THIS FLOOR TO REBUILD ITS MONSTER RECORDS.", 8);
        return;
    }
    if (!confirm_quest_boss_warp(g, player, monster_types[type].name)) {
        show_runtime_indicator(g, player, "QUEST-BOSS WARP CANCELLED.",
                               "YOUR LOCATION IS UNCHANGED.", 8);
        return;
    }

    MonsterRecord *m = &g->monster_map[g->monster_layer][boss];
    static const int ox[4] = {0, 0, -1, 1};
    static const int oy[4] = {-1, 1, 0, 0};
    static const int face[4] = {1, 0, 3, 2};
    int found = -1;
    for (int i = 0; i < 4; i++) {
        int x = (int)m->x + ox[i], y = (int)m->y + oy[i];
        if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H ||
            rock_cell_at(g, x, y, g->cur_floor) ||
            game_find_monster(g, x, y) >= 0 ||
            edge_between_cells(g, x, y, m->x, m->y) != 3)
            continue;
        g->cur_x = x;
        g->cur_y = y;
        g->last_move_dir = face[i];
        found = i;
        break;
    }
    if (found < 0) {
        show_runtime_indicator(g, player, "QUEST-BOSS WARP FAILED.",
            "THE BOSS HAS NO SAFE OPEN APPROACH CELL.", 8);
        return;
    }
    player->x_pos = (u16)g->cur_x;
    player->y_pos = (u16)g->cur_y;
    player->facing_dir = (u16)g->last_move_dir;
    reveal_around_player(g);
    g->monster_adjacent = 1;
    show_runtime_indicator(g, player, "QUEST-BOSS WARP COMPLETE.",
                           "THE BOSS IS NOW DIRECTLY IN FRONT OF YOU.", 4);
}

static void debug_randomize_floor_look(Game *g, Character *player) {
    char line[96];
    /* The two original WALL.PIC records are semantic stone/door surfaces,
       so doors must not be exchanged for stone.  Randomize the complete
       77-floor DAC cycle and the stone surface's sampling phase instead. */
    g->palette_floor_override = game_rand(g) % 77;
    g->wall_texture_offset = (game_rand(g) % 8) * 32;
    g->wall_color_r = (u8)((game_rand(g) % 4) * 0x10);
    g->wall_color_g = (u8)((game_rand(g) % 4) * 0x10);
    g->wall_color_b = (u8)((game_rand(g) % 4) * 0x10);
    game_refresh_world_palette(g);
    snprintf(line, sizeof(line), "PALETTE %d OF 77 - TEXTURE PHASE %d OF 8.",
             g->palette_floor_override + 1,
             g->wall_texture_offset / 32 + 1);
    show_runtime_indicator(g, player, "FLOOR APPEARANCE RANDOMIZED.",
                           line, 4);
}

static void reroll_dungeon(Game *g, Character *player) {
    if (!confirm_dungeon_reroll(g, player)) {
        show_runtime_indicator(g, player, "DUNGEON REROLL CANCELLED.",
                               "THE CURRENT DUNGEON IS UNCHANGED.", 8);
        return;
    }
    int old_seed = g->dungeon_number;
    int new_seed;
    do {
        new_seed = 1 + (int)(game_rand(g) % 31000u);
    } while (new_seed == old_seed);
    game_begin_new_dungeon(g, player, new_seed);
    show_runtime_indicator(g, player, "DUNGEON REROLLED.",
                           "YOU HAVE ARRIVED IN THE NEW TOWN.", 4);
}

static int game_try_step(Game *g, Character *player, int direction) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dy[4] = {-1, 1, 0, 0};
    if (direction < 0 || direction > 3) return 0;

    int nx = g->cur_x + dx[direction];
    int ny = g->cur_y + dy[direction];
    g->last_move_dir = direction;
    if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) {
        if (g->cheat_noclip) {
            /* The finite DUNG.BIN grid cannot represent an infinite void.
               Noclip therefore crosses an outer edge without blocking and
               re-enters at the opposite edge, preserving valid save/map
               coordinates while providing genuinely unbounded movement. */
            nx = (nx % MAP_W + MAP_W) % MAP_W;
            ny = (ny % MAP_H + MAP_H) % MAP_H;
        } else {
            mw_audio_play(&g->audio, MW_SFX_BLOCKED);
            show_movement_block(g, player, "THE EDGE OF THE MAP BLOCKS YOU");
            return 0;
        }
    }
    if (!g->cheat_noclip &&
        !game_can_move(g, g->cur_x, g->cur_y, nx, ny)) {
        mw_audio_play(&g->audio, MW_SFX_BLOCKED);
        show_movement_block(g, player, "THE WALL REFUSES TO MOVE");
        return 0;
    }

    int edge = g->cheat_noclip ? 3 :
               edge_between_cells(g, g->cur_x, g->cur_y, nx, ny);
    int monster = game_find_monster(g, nx, ny);
    if (!g->cheat_noclip && monster >= 0 && edge != 3) {
        /* Both visible and secret doors auto-open only when their destination
         * can be occupied.  WORLD has a distinct message for edge type 2. */
        mw_audio_play(&g->audio, MW_SFX_BLOCKED);
        show_movement_block(g, player,
                            edge == 2 ? "THE SECRET DOOR IS JAMMED"
                                      : "THE DOOR IS JAMMED");
        return 0;
    }
    if (!g->cheat_noclip && monster >= 0) {
        /* WORLD cancels the movement delta when the destination is occupied.
         * Walking into a visible monster never invokes combat; F is the
         * keyboard fight command (and clicking that monster's viewport is
         * the original mouse synonym for F). */
        g->monster_adjacent = 1;
        return 0;
    }

    g->cur_x = nx;
    g->cur_y = ny;
    mw_audio_play(&g->audio, edge == 1 ? MW_SFX_DOOR : MW_SFX_STEP);
    reveal_around_player_animated(g, player);
    game_apply_pitfall_interactive(g, player);
    game_advance_monsters(g, player);
    return mw_hp_cur(player) == 0 ? -1 : 1;
}

static void draw_trapdoor_notice(Game *g, Character *player) {
    int target = game_trapdoor_floor(g, g->cur_x, g->cur_y);
    if (target < 0) return;

    const int message_w = SX(0x2D3);
    const int message_bottom = SY(0x1AE);
    char line[64];
    int y = 0;
    int key_index = target / 10;
    int has_key = key_index > 0 && key_index < 18 &&
                  player->trapdoor_keys[key_index] != 0;

    video_fill_rect(&g->video, 0, 0, message_w, message_bottom, 0);
    y = draw_combat_message(&g->video, y, message_bottom,
                            "YOU HAVE FOUND A TRAP DOOR", 12);
    snprintf(line, sizeof(line), "WITH A KEYHOLE LABELED %d.", target);
    y = draw_combat_message(&g->video, y, message_bottom, line, 12);
    if (has_key) {
        y = draw_combat_message(&g->video, y, message_bottom,
                                "TO USE THE KEY YOU FOUND", 15);
        y = draw_combat_message(&g->video, y, message_bottom,
                                "EARLIER, HIT 'K' TO USE", 15);
        draw_combat_message(&g->video, y, message_bottom, "DOOR...", 15);
    } else {
        y = draw_combat_message(&g->video, y, message_bottom,
                                "UNFORTUNATELY, YOU DO NOT HAVE THE CORRECT KEY.", 15);
        y = draw_combat_message(&g->video, y, message_bottom,
                                "THIS KEY CAN ONLY BE FOUND BY KILLING A LEVEL DRAINER", 15);
        draw_combat_message(&g->video, y, message_bottom,
                            "NEAR THE LEVEL THIS TRAP DOOR LEADS TO.", 15);
    }
}

void game_draw_exploration(Game *g, Character *player) {
    int adjacent = game_find_adjacent_monster(g);
    if (adjacent >= 0 && g->monster_layer >= 0) {
        MonsterRecord *m = &g->monster_map[g->monster_layer][adjacent];
        game_draw_combat_overlay(g, player, adjacent, m->type, m->level,
                                 game_monster_hp(g, adjacent), "", "", "");
        return;
    }
    game_draw_exploration_base(g, player);
}

static void draw_arena_centered(Game *g, int y, const char *text, u8 color,
                                int scale_num, int scale_den) {
    int advance = g->video.font_advance > 0 ? g->video.font_advance : 12;
    int width = (int)strlen(text) * advance * scale_num / scale_den;
    video_draw_text_scaled(&g->video, (LOGICAL_W - width) / 2, y, text,
                           color, scale_num, scale_den);
}

/* MW_EXTENSION: the Colosseum is intentionally a distinct presentation from
   WORLD's four dungeon panes.  It still uses the real monster PIC art and the
   shared combat engine, but stages the current challenger on a sand floor. */
static void game_draw_arena_combat(Game *g, Character *player,
                                   int monster_type, int monster_level,
                                   int monster_hp, const char *msg1,
                                   const char *msg2, const char *msg3) {
    Video *v = &g->video;
    video_load_vga_default_palette(v);
    video_set_palette(v, 1, 35, 25, 65);
    video_set_palette(v, 5, 180, 66, 30);
    video_set_palette(v, 6, 205, 138, 45);
    video_set_palette(v, 7, 185, 175, 155);
    video_set_palette(v, 8, 35, 235, 65);
    video_set_palette(v, 13, 55, 42, 35);
    video_set_palette(v, 14, 255, 205, 45);
    video_clear(v, 0);

    /* Stadium tiers and a deterministic VGA crowd. */
    video_fill_rect(v, 0, 76, LOGICAL_W, 258, 1);
    for (int y = 100; y < 320; y += 22) {
        video_hline(v, 0, y, LOGICAL_W, (y / 22) & 1 ? 5 : 13);
        for (int x = 12 + (y & 15); x < LOGICAL_W; x += 31) {
            int color = 2 + ((x * 5 + y * 3) % 13);
            video_fill_rect(v, x, y - 9, 5, 5, (u8)color);
        }
    }
    video_fill_rect(v, 0, 328, LOGICAL_W, 10, 7);

    /* Elliptical sand and its bright inner boundary. */
    const int cx = LOGICAL_W / 2, cy = 540, rx = 485, ry = 225;
    for (int y = cy - ry; y <= cy + ry; y++) {
        double dy = (double)(y - cy) / (double)ry;
        int half = (int)(rx * sqrt(fmax(0.0, 1.0 - dy * dy)));
        video_hline(v, cx - half, y, half * 2 + 1, 6);
        if (half > 4 && (y == cy - ry || y == cy + ry ||
                         ((y - (cy - ry)) % 28) == 0))
            video_hline(v, cx - half, y, half * 2 + 1, 14);
    }

    char line[160];
    snprintf(line, sizeof(line), "COLOSSEUM %s  ROUND %u%s",
             arena_difficulty_name(g->arena_difficulty), g->arena_round,
             g->arena_champion ? "  -  CHAMPION" : "");
    draw_arena_centered(g, 14, line, g->arena_champion ? 14 : 8, 1, 1);
    snprintf(line, sizeof(line), "STREAK %u   CAREER BEST %u",
             g->arena_streak, g->arena_best);
    draw_arena_centered(g, 50, line, 15, 3, 4);

    if (monster_hp > 0) {
        int pic = get_monster_pic_index_ext(monster_type);
        if (pic < 2) pic = 2;
        draw_pic_billboard(g, pic, cx, 172, 410, 0.12f,
                           120, 82, LOGICAL_W - 240, 570, NULL,
                           get_monster_color_ext(monster_type),
                           get_monster_tint_ext(monster_type));
    }

    const char *monster_name = monster_type >= 0 &&
                               monster_type < MONSTER_TYPE_COUNT ?
                               monster_types[monster_type].name : "UNKNOWN";
    snprintf(line, sizeof(line), "%s  LEVEL %d  HP %d",
             monster_name, monster_level, monster_hp > 0 ? monster_hp : 0);
    draw_arena_centered(g, 100, line, 14, 3, 4);

    video_fill_rect(v, 12, 590, 420, 122, 0);
    snprintf(line, sizeof(line), "%s  LEVEL %u", player->name,
             player->level);
    video_draw_text_scaled(v, 24, 600, line, 8, 3, 4);
    snprintf(line, sizeof(line), "HP %u/%u   SP %.0f/%.0f",
             mw_hp_cur(player), mw_hp_max(player),
             player->sp_cur, player->sp_max);
    video_draw_text_scaled(v, 24, 638, line, 15, 3, 4);
    snprintf(line, sizeof(line), "%s / %s",
             player->equipped_weapon < WEAPON_STAT_COUNT ?
                 weapon_stats[player->equipped_weapon].name : "UNKNOWN",
             combat_armor_name(player->equipped_armor));
    video_draw_text_scaled(v, 24, 676, line, 7, 3, 4);

    video_fill_rect(v, 444, 590, 568, 122, 0);
    int message_y = 598;
    if (msg1 && msg1[0]) {
        video_draw_text_scaled(v, 458, message_y, msg1, 15, 3, 4);
        message_y += 36;
    }
    if (msg2 && msg2[0]) {
        video_draw_text_scaled(v, 458, message_y, msg2, 15, 3, 4);
        message_y += 36;
    }
    if (msg3 && msg3[0])
        video_draw_text_scaled(v, 458, message_y, msg3, 15, 3, 4);

    video_fill_rect(v, 0, 720, LOGICAL_W, 48, 0);
    draw_arena_centered(g, 729,
        "F FIGHT  C CAST  I ITEM  W WEAPON  A ARMOR  V STATS  H HELP",
        8, 3, 4);
}

/* Original combat never changes to a separate full-screen scene.  The normal
 * four-view exploration frame stays in place, the engaged monster grows in
 * its compass pane, and messages occupy the otherwise-black upper-left pane. */
void game_draw_combat_overlay(Game *g, Character *player,
                              int entity_index, int monster_type,
                              int monster_level, int monster_hp,
                              const char *msg1, const char *msg2,
                              const char *msg3) {
    if (g->arena_active) {
        game_draw_arena_combat(g, player, monster_type, monster_level,
                               monster_hp, msg1, msg2, msg3);
        return;
    }
    static const char *dir_name[4] = {"NORTH", "SOUTH", "WEST", "EAST"};
    ViewLayout view_layout;
    const ViewLayout *layout = &view_layout;
    game_view_layout(g, g->view_mode, &view_layout);
    const ViewRect *pane[4] = {
        &layout->north, &layout->south, &layout->west, &layout->east
    };
    int dir = g->last_move_dir;
    MonsterRecord saved = {0};
    MonsterRecord *engaged = NULL;

    if (dir < 0 || dir > 3) dir = 0;
    if (g->monster_map_loaded && g->monster_layer >= 0 &&
        entity_index >= 0 &&
        entity_index < MONSTERS_PER_FLOOR) {
        engaged = &g->monster_map[g->monster_layer][entity_index];
        int dx = (int)engaged->x - g->cur_x;
        int dy = (int)engaged->y - g->cur_y;
        if (dx == 0 && dy == -1) dir = 0;
        else if (dx == 0 && dy == 1) dir = 1;
        else if (dx == -1 && dy == 0) dir = 2;
        else if (dx == 1 && dy == 0) dir = 3;

        /* Keep the normal-distance actor from showing through the enlarged
         * sprite.  This is a render-only substitution; MON.MAP is restored
         * immediately and is not marked dirty. */
        saved = *engaged;
        engaged->x = 100;
        engaged->y = 100;
    }

    game_draw_exploration_base(g, player);
    if (engaged) *engaged = saved;

    const ViewRect *vr = pane[dir];
    if (monster_hp > 0) {
        int pic = get_monster_pic_index_ext(monster_type);
        if (pic < 2) pic = 2;
        int sprite_h = vr->h * 17 / 20;
        int top = vr->y + vr->h - sprite_h - 2;
        draw_pic_billboard(g, pic, vr->x + vr->w / 2, top, sprite_h,
                           0.20f, vr->x, vr->y, vr->w, vr->h, NULL,
                           get_monster_color_ext(monster_type),
                           get_monster_tint_ext(monster_type));
    }

    /* LEV/HP strip belongs to the active viewport, not to a separate UI. */
    const int strip_h = SY(42);
    char line[96];
    video_fill_rect(&g->video, vr->x, vr->y, vr->w, strip_h, 14);
    snprintf(line, sizeof(line), "LEV:%d  HP:%d", monster_level,
             monster_hp > 0 ? monster_hp : 0);
    video_draw_text_scaled(&g->video, vr->x + 2, vr->y + 1,
                           line, 15, 3, 4);

    /* The original upper-left message pane is exactly the rectangle before
     * the north/west view origins in full-size mode. */
    const int message_w = SX(0x2D3);
    const int message_bottom = SY(0x1AE);
    video_fill_rect(&g->video, 0, 0, message_w, message_bottom, 0);
    int y = 0;
    y = draw_combat_message(&g->video, y, message_bottom, msg1, 15);
    y = draw_combat_message(&g->video, y, message_bottom, msg2, 15);
    y = draw_combat_message(&g->video, y, message_bottom, msg3, 15);
    y = draw_combat_message(&g->video, y, message_bottom,
                            "YOU ARE FIGHTING THE MONSTER", 15);
    snprintf(line, sizeof(line), "IN THE %s VIEW.", dir_name[dir]);
    y = draw_combat_message(&g->video, y, message_bottom, line, 15);
    if (monster_type >= 0 && monster_type < MONSTER_TYPE_COUNT)
        snprintf(line, sizeof(line), "MONSTER TYPE: %s",
                 monster_types[monster_type].name);
    else
        snprintf(line, sizeof(line), "MONSTER TYPE: UNKNOWN");
    draw_combat_message(&g->video, y, message_bottom, line, 15);
}

/* MW_PORT: WORLD func_1FC56 (0x1FC56), the original title introduction.
 * WORLD grows a fresh randomly selected monster on every frame without
 * erasing the previous actors, producing the remembered crowd of monsters
 * "popping" into the lower screen.  Its projection starts at 15 source units,
 * advances by three through 70 and then by eight through 0x127.  The native
 * enhanced roster is deliberately sampled at regular showcase intervals;
 * all other selections retain WORLD's original 9/57/100 weighted families. */
static void title_draw_centered(Game *g, int y, const char *text, u8 color,
                                int scale_num, int scale_den) {
    int advance = g->video.font_advance > 0 ? g->video.font_advance : 12;
    int width = (int)strlen(text) * advance * scale_num / scale_den;
    int x = (LOGICAL_W - width) / 2;
    video_draw_text_scaled(&g->video, x, y, text, color,
                           scale_num, scale_den);
}

enum {
    TITLE_SOURCE_W = 1600,
    TITLE_SOURCE_H = 1200,
    TITLE_BLUE_SOURCE_Y = 230,
    TITLE_GRAY_SOURCE_Y = 450
};

static int title_scale_source_y(const Game *g, int source_y) {
    /* WORLD scales against the maximum source and destination coordinates,
     * not their pixel counts.  Preserve each driver's integer rounding before
     * expanding its native scanline onto the SDL logical surface. */
    const MwDisplayModeInfo *mode =
        video_display_mode_info(g->video.display_mode);
    if (!mode)
        return source_y * (LOGICAL_H - 1) / (TITLE_SOURCE_H - 1);
    int native = source_y * (mode->raster_h - 1) /
                 (TITLE_SOURCE_H - 1);
    return native * LOGICAL_H / mode->raster_h;
}

static void title_load_original_palette(Video *v) {
    video_load_vga_default_palette(v);

    /* Captured 1024x768x256 DAC values from WORLD func_1FC56.  The original
     * driver expands each six-bit channel by shifting it left two bits.  Keep
     * these title colors exact instead of using the native renderer's rounded
     * six-to-eight-bit conversion. */
    video_set_palette(v, 1,   0,   0, 152); /* royal-blue middle field */
    video_set_palette(v, 3,  80, 200, 252); /* copyright cyan */
    video_set_palette(v, 5, 212,  80,  40); /* title/prompt orange */
    video_set_palette(v, 13, 52,  52,  52); /* lower charcoal field */
}

static void title_draw_base(Game *g) {
    Video *v = &g->video;
    const MwDisplayModeInfo *mode = video_display_mode_info(v->display_mode);
    int blue_y = title_scale_source_y(g, TITLE_BLUE_SOURCE_Y);
    int gray_y = title_scale_source_y(g, TITLE_GRAY_SOURCE_Y);

    title_load_original_palette(v);
    video_clear(v, 0);

    /* WORLD func_1FC56 paints these after the heading.  Its filled rectangle
     * stops before y2, so the original screen has one black scanline beneath
     * the gray field at y=767.  The title text lies above both rectangles and
     * is therefore equivalent (and clearer here) when drawn afterward. */
    if (mode && mode->world_mode < 2) {
        /* WORLD func_1FC56 does not use the two solid rectangles in the
         * Hercules/CGA branches.  It draws every other native scanline in
         * colour 1 from source y=230 to the bottom, leaving black gaps. */
        int first_native = TITLE_BLUE_SOURCE_Y * (mode->raster_h - 1) /
                           (TITLE_SOURCE_H - 1);
        for (int ny = first_native; ny < mode->raster_h; ny += 2) {
            int y0 = ny * LOGICAL_H / mode->raster_h;
            int y1 = (ny + 1) * LOGICAL_H / mode->raster_h;
            if (y1 <= y0) y1 = y0 + 1;
            video_fill_rect(v, 0, y0, LOGICAL_W, y1 - y0, 1);
        }
    } else {
        video_fill_rect(v, 0, blue_y, LOGICAL_W, gray_y - blue_y, 1);
        video_fill_rect(v, 0, gray_y, LOGICAL_W,
                        (LOGICAL_H - 1) - gray_y, 13);
    }

    title_draw_centered(g, 0, "MORAFF'S WORLD", 5, 2, 1);
    title_draw_centered(g, 78, "VERSION 6.1, COPYRIGHT 1993,", 3, 1, 1);
    title_draw_centered(g, 116, "ALL RIGHTS RESERVED", 3, 1, 1);
    title_draw_centered(g, 732, "HIT ANY KEY TO SKIP INTRODUCTION",
                        5, 3, 4);
}

static int title_pick_original_monster(Game *g) {
    for (int attempt = 0; attempt < 256; attempt++) {
        int type;
        if (game_rand(g) * 2 / 0x8000 != 0)
            type = game_rand(g) * 9 / 0x8000;
        else if (game_rand(g) * 3 / 0x8000 != 0)
            type = game_rand(g) * 57 / 0x8000;
        else
            type = game_rand(g) * 100 / 0x8000;
        if (combat_monster_type_spawnable(type)) return type;
    }
    return 0; /* Ogre is the original table's safe first actor. */
}

static int title_pick_monster(Game *g, int popup_index) {
    static const int enhanced_showcase[] = {
        112, /* Violet Abyss King */
        114, /* Azure Ogre */
        138, /* Runic Stone Lord */
        150, /* Crimson Lich */
        166, /* Abyssal Dragon */
        174, /* Cobalt Rift Tyrant */
        176, /* Viridian Eternity Dragon */
        177  /* Radiant Moraff Ascendant */
    };

    if (popup_index % 6 == 5) {
        int showcase = popup_index / 6;
        int count = (int)(sizeof(enhanced_showcase) /
                          sizeof(enhanced_showcase[0]));
        int type = enhanced_showcase[showcase % count];
        if (combat_monster_type_spawnable(type)) return type;
    }
    return title_pick_original_monster(g);
}

static void title_draw_monster_popup(Game *g, int type, int source_size) {
    int pic = get_monster_pic_index_ext(type);
    if (pic < 2) return;

    /* Exact WORLD 1600x1200 title geometry:
     *   x1 = random(1580 - 2*size) + 10
     *   x2 = x1 + 2*size
     *   y1 = 410 - size/2
     *   y2 = 410 + 5*size/2
     * Scale those source coordinates to the native 1024x768 surface. */
    int source_width = source_size * 2;
    int source_span = 1580 - source_width;
    if (source_span < 1) source_span = 1;
    int source_left = 10 + game_rand(g) * source_span / 0x8000;
    int source_top = 410 - source_size / 2;
    int source_bottom = 410 + source_size * 5 / 2;
    int left = display_design_x(g, source_left);
    int right = display_design_x(g, source_left + source_width);
    int top = display_design_y(g, source_top);
    int bottom = display_design_y(g, source_bottom);
    int draw_w = right - left;
    int draw_h = bottom - top;
    int cx = (left + right) / 2;
    if (draw_w < 1) draw_w = 1;
    if (draw_h < 1) draw_h = 1;

    draw_pic_billboard_sized(g, pic, cx, top, draw_w, draw_h, 0.0f,
                             0, 0, LOGICAL_W, LOGICAL_H, NULL,
                             get_monster_color_ext(type),
                             get_monster_tint_ext(type));
}

static void title_draw_credit_card(Game *g) {
    Video *v = &g->video;

    /* The original finishes the monster sequence by laying its credit block
     * over the lower field.  The black inset and VGA-colored rails preserve
     * that readable panel at modern pixel-perfect output. */
    video_fill_rect(v, 78, 482, 868, 238, 0);
    video_fill_rect(v, 78, 482, 868, 2, 13);
    video_fill_rect(v, 78, 718, 868, 2, 2);
    video_fill_rect(v, 78, 482, 2, 238, 1);
    video_fill_rect(v, 944, 482, 2, 238, 1);

    title_draw_centered(g, 490, "WRITTEN AND PRODUCED BY STEVE MORAFF",
                        6, 1, 1);
    title_draw_centered(g, 528, "ARTWORK BY RODNEY PAGE", 3, 1, 1);
    title_draw_centered(g, 566, "ADDITIONAL ARTWORK AND TESTING BY",
                        8, 1, 1);
    title_draw_centered(g, 604, "MARTIN AND LAURIE NOEL", 8, 1, 1);
    title_draw_centered(g, 642, "FINANCED BY OUR REGISTERED USERS",
                        5, 1, 1);
    title_draw_centered(g, 680, "WINDOWS PRESERVATION PORT BY KANDOWONTU",
                        15, 3, 4);
}

static int title_key_requests_exit(int key) {
    return key == 0x1B || key == 'q' || key == 'Q';
}

int game_title_input_self_test(void) {
    static const int exit_keys[] = { 0x1B, 'q', 'Q' };
    static const int continue_keys[] = {
        ' ', '\r', 'a', INPUT_MOUSE_CLICK, 0
    };
    int failures = 0;

    for (size_t i = 0; i < sizeof(exit_keys) / sizeof(exit_keys[0]); i++)
        if (!title_key_requests_exit(exit_keys[i])) failures++;
    for (size_t i = 0;
         i < sizeof(continue_keys) / sizeof(continue_keys[0]); i++)
        if (title_key_requests_exit(continue_keys[i])) failures++;
    return failures;
}

int game_title_background_self_test(Game *g) {
    if (!g) return 1;

    int failures = 0;
    const MwDisplayModeInfo *mode =
        video_display_mode_info(g->video.display_mode);
    int blue_y = title_scale_source_y(g, TITLE_BLUE_SOURCE_Y);
    int gray_y = title_scale_source_y(g, TITLE_GRAY_SOURCE_Y);
    title_draw_base(g);

    /* Sample the unobstructed left edge at every transition. */
    if (video_get_pixel(&g->video, 0, blue_y - 1) != 0) failures++;
    if (video_get_pixel(&g->video, 0, blue_y) != 1) failures++;
    if (mode && mode->world_mode < 2) {
        int native = TITLE_BLUE_SOURCE_Y * (mode->raster_h - 1) /
                     (TITLE_SOURCE_H - 1);
        int gap_y = (native + 1) * LOGICAL_H / mode->raster_h;
        if (video_get_pixel(&g->video, 0, gap_y) != 0) failures++;
    } else {
        if (video_get_pixel(&g->video, 0, gray_y - 1) != 1) failures++;
        if (video_get_pixel(&g->video, 0, gray_y) != 13) failures++;
        if (video_get_pixel(&g->video, 0, LOGICAL_H - 2) != 13) failures++;
        if (video_get_pixel(&g->video, 0, LOGICAL_H - 1) != 0) failures++;
    }

    const PaletteEntry *pal = g->video.palette;
    if (pal[1].r != 0 || pal[1].g != 0 || pal[1].b != 152) failures++;
    if (pal[3].r != 80 || pal[3].g != 200 || pal[3].b != 252) failures++;
    if (pal[5].r != 212 || pal[5].g != 80 || pal[5].b != 40) failures++;
    if (pal[13].r != 52 || pal[13].g != 52 || pal[13].b != 52) failures++;
    return failures;
}

/* Returns -1 for an explicit application-exit request, +1 to leave the
 * introduction for character selection, and zero when the delay expires.
 * input_wait_any_key drains the scan byte of DOS-style extended keys so an
 * arrow/Page key cannot leak into the selection screen. */
static int title_wait_interruptible(Game *g, u32 delay_ms) {
    u32 start = SDL_GetTicks();
    while ((u32)(SDL_GetTicks() - start) < delay_ms) {
        if (input_poll_quit(&g->input)) return -1;
        if (input_kbhit(&g->input)) {
            int key = input_wait_any_key(&g->input);
            return title_key_requests_exit(key) ? -1 : 1;
        }
        u32 elapsed = (u32)(SDL_GetTicks() - start);
        u32 remaining = delay_ms > elapsed ? delay_ms - elapsed : 0;
        SDL_Delay(remaining > 10 ? 10 : remaining);
    }
    return 0;
}

static int game_run_title_intro(Game *g) {
    int popup_index = 0;

    for (;;) {
        title_draw_base(g);
        video_present(&g->video);

        int source_size = 15;
        while (source_size < 0x127) {
            source_size += source_size < 0x46 ? 3 : 8;
            int delay_ms = 0x1A4 - source_size;
            if (delay_ms > 0) {
                int action = title_wait_interruptible(g, (u32)delay_ms);
                if (action != 0) return action > 0;
            }

            int type = title_pick_monster(g, popup_index++);
            title_draw_monster_popup(g, type, source_size);
            video_present(&g->video);
        }

        /* WORLD waits twelve 250ms ticks on the finished lineup. */
        int action = title_wait_interruptible(g, 12u * 250u);
        if (action != 0) return action > 0;

        title_draw_credit_card(g);
        video_present(&g->video);

        /* The original credit card remains for thirty-two 250ms ticks before
         * the introduction begins again. */
        action = title_wait_interruptible(g, 32u * 250u);
        if (action != 0) return action > 0;
    }
}

void game_draw_title_preview(Game *g, int show_credits) {
    u32 saved_rand = g->rand_state;
    int source_size = 15;
    int popup_index = 0;

    game_srand(g, 0x1FC56u);
    title_draw_base(g);
    while (source_size < 0x127) {
        source_size += source_size < 0x46 ? 3 : 8;
        int type = title_pick_monster(g, popup_index++);
        title_draw_monster_popup(g, type, source_size);
    }
    if (show_credits) title_draw_credit_card(g);
    g->rand_state = saved_rand;
}

void game_draw_title_background_preview(Game *g) {
    title_draw_base(g);
}

static void game_leave_character_session(Game *g) {
    /* Every caller has already saved the character and world.  Detaching the
     * slot prevents game_shutdown from writing that stale session again if
     * the player subsequently exits from the title screen. */
    g->active_save_slot = -1;
    g->monster_adjacent = 0;
}

void game_run(Game *g) {
    Video *v = &g->video;

    /* MW.EXE asks for a display driver before WORLD.EXE begins.  Keep that
     * startup flow, defaulting the highlight to the last saved choice. */
    game_video_mode_menu(g, 1);
    if (input_poll_quit(&g->input)) return;

title_screen:
    ;
    if (!game_run_title_intro(g)) return;

    /* Player selection.  Cancelling a partially designed character returns
     * to the slot list without creating or overwriting a save. */
    int slot;
    Character *player;
    int colosseum_page = 0;
    for (;;) {
        slot = player_select_screen(g, &colosseum_page);
        if (slot < 0) return;
        if (colosseum_page) {
            ArenaSave arena;
            if (arena_load_save(g, slot, &arena) != 0) {
                Character created;
                if (!create_character(g, &created,
                                      MW_EXPERIENCE_ENHANCED)) {
                    if (input_poll_quit(&g->input)) return;
                    continue;
                }
                int difficulty = arena_select_difficulty(g);
                if (difficulty < 0) {
                    if (input_poll_quit(&g->input)) return;
                    continue;
                }
                arena_initialize_save(&arena, &created);
                arena.difficulty = (u8)difficulty;
                if (arena_save_save(g, slot, &arena) != 0) {
                    video_clear(v, 0);
                    video_draw_text(v, SX(0), SY(0),
                        "COULD NOT CREATE THE COLOSSEUM SAVE.", 12);
                    video_draw_text(v, SX(0), SY(70),
                        "CHECK THAT THIS DIRECTORY IS WRITABLE.", 15);
                    video_present(v);
                    input_wait_any_key(&g->input);
                    continue;
                }
            }
            arena_run(g, slot, &arena);
            if (input_poll_quit(&g->input)) return;
            continue;
        }
        player = &g->chars[slot];
        if (g->char_exists[slot]) break;
        if (create_character(g, player, -1)) {
            g->char_exists[slot] = 1;
            game_save_character(g, slot);
            /* Reusing an empty slot starts a genuinely new monster record,
               even if an orphaned sidecar from an older character remains. */
            memset(g->bestiary_kills, 0, sizeof(g->bestiary_kills));
            g->active_save_slot = slot;
            g->bestiary_loaded = 1;
            g->bestiary_dirty = 1;
            game_save_bestiary(g);
            break;
        }
        if (input_poll_quit(&g->input)) return;
    }
    g->player_slot[0] = slot;
    g->cur_player = slot;
    g->dungeon_max_floor =
        mw_experience_mode(player) == MW_EXPERIENCE_CLASSIC ?
        CLASSIC_DUNGEON_FLOOR : MAX_DUNGEON_FLOOR;

    g->cur_x = player->x_pos;
    g->cur_y = player->y_pos;
    g->cur_floor = game_clamp_dungeon_floor(g, player->floor_depth);
    player->floor_depth = (u16)g->cur_floor;
    g->dungeon_number = player->dungeon_number;
    g->last_move_dir = player->facing_dir <= 3 ? player->facing_dir : 0;
    g->view_mode = 0;
    g->monster_adjacent = 0;
    memset(g->visited, 0, sizeof(g->visited));

    if (g->cur_x == 0 && g->cur_y == 0) {
        find_start_pos(g);
    }

    game_load_world_state(g, slot);

    reveal_around_player(g);
    game_apply_pitfall_interactive(g, player);

    g->map_player_visible = 1;
    u32 next_map_blink = SDL_GetTicks() + 350;
    unsigned hover_serial = 0;
    input_mouse_position(&g->input, NULL, NULL, &hover_serial);
    while (!input_poll_quit(&g->input)) {
        int preserve_combat_feedback = g->combat_feedback_visible;
        if (!preserve_combat_feedback) {
            game_draw_exploration(g, player);
            video_present(v);
        }

        /* WORLD keeps polling while the exploration screen is idle.  Doing
         * the same here lets its square map cursor blink without requiring
         * movement or another keypress. */
        while (!input_poll_quit(&g->input) && !input_kbhit(&g->input)) {
            u32 now = SDL_GetTicks();
            unsigned current_hover = hover_serial;
            input_mouse_position(&g->input, NULL, NULL, &current_hover);
            if (!preserve_combat_feedback &&
                current_hover != hover_serial) {
                hover_serial = current_hover;
                game_draw_exploration(g, player);
                draw_mouse_hover(g);
                video_present(v);
            }
            if (!preserve_combat_feedback &&
                SDL_TICKS_PASSED(now, next_map_blink)) {
                g->map_player_visible = !g->map_player_visible;
                next_map_blink = now + 350;
                {
                    int mx, my, mw, mh;
                    game_normal_map_rect(g, &mx, &my, &mw, &mh);
                    draw_minimap(g, mx, my, mw, mh);
                }
                video_present(v);
            }
            SDL_Delay(10);
        }
        if (input_poll_quit(&g->input)) break;
        int key = input_getch(&g->input);
        /* The command being read is the original game's implicit dismissal
           of the retained combat text; it must still execute normally. */
        g->combat_feedback_visible = 0;

        if (key == INPUT_MOUSE_CLICK) {
            int mouse_x, mouse_y;
            input_last_mouse_click(&g->input, &mouse_x, &mouse_y);
            key = command_menu_click_key(g, mouse_x, mouse_y);
            if (!key) {
                int direction = game_mouse_view_direction(g);
                if (direction >= 0) {
                    int monster =
                        game_find_engaged_monster_in_direction(g, direction);
                    if (monster >= 0) {
                        mw_audio_play(&g->audio, MW_SFX_ATTACK);
                        fight_monster(g, player, monster);
                        if (mw_hp_cur(player) == 0) goto game_over;
                    } else if (game_try_step(g, player, direction) < 0) {
                        goto game_over;
                    }
                    /* The click has already executed the turn. */
                    key = -1;
                } else {
                    int logical_x, logical_y;
                    if (game_mouse_click_logical(g, &logical_x, &logical_y) &&
                        logical_x >= SX(0x2D3) && logical_x < SX(0x48A) &&
                        logical_y > SY(0x487)) {
                        int delta = ladder_delta(g, g->cur_x, g->cur_y);
                        int shop = game_shop_type(g, g->cur_x, g->cur_y);
                        int trap = game_trapdoor_floor(g, g->cur_x, g->cur_y);
                        key = (shop || delta < 0) ? 'u' :
                              (delta > 0 ? 'd' : (trap >= 0 ? 'k' : 'd'));
                    } else {
                        continue;
                    }
                }
            }
        }

        if (game_handle_turbo_key(g, key)) {
            show_turbo_indicator(g, player);
        } else if (key == INPUT_VIDEO_MODE) {
            game_video_mode_menu(g, 0);
        } else if (key == INPUT_MAX_CHARACTER) {
            game_debug_max_character(g, player);
        } else if (key == INPUT_BATTLE_SIMULATOR) {
            battle_simulator_run(g, player);
        } else if (key == INPUT_RANDOMIZE_FLOOR) {
            debug_randomize_floor_look(g, player);
        } else if (key == INPUT_QUEST_BOSS_WARP) {
            debug_warp_to_quest_boss(g, player);
        } else if (key == INPUT_MODEL_VIEWER) {
            model_viewer_run(g);
        } else if (key == INPUT_DUNGEON_REROLL) {
            reroll_dungeon(g, player);
        } else if (key == INPUT_OPEN_FLOOR_TOGGLE) {
            g->cheat_open_floor = !g->cheat_open_floor;
            show_runtime_indicator(g, player,
                g->cheat_open_floor ? "OPEN FLOOR MODE: ON"
                                    : "OPEN FLOOR MODE: OFF",
                g->cheat_open_floor ? "U AND D NOW WORK WITHOUT LADDERS."
                                    : "U AND D REQUIRE LADDERS AGAIN.",
                g->cheat_open_floor ? 4 : 8);
        } else if (key == INPUT_TOWN_TELEPORT) {
            debug_teleport_to_town(g, player);
        } else if (key == INPUT_GOD_TOGGLE) {
            g->cheat_god_mode = !g->cheat_god_mode;
            show_runtime_indicator(g, player,
                g->cheat_god_mode ? "GOD MODE: ON" : "GOD MODE: OFF",
                g->cheat_god_mode ? "DAMAGE AND SPELL-POINT COSTS ARE DISABLED."
                                  : "NORMAL DAMAGE AND SPELL COSTS RESTORED.",
                g->cheat_god_mode ? 4 : 8);
        } else if (key == INPUT_NOCLIP_TOGGLE) {
            g->cheat_noclip = !g->cheat_noclip;
            show_runtime_indicator(g, player,
                g->cheat_noclip ? "NOCLIP: ON" : "NOCLIP: OFF",
                g->cheat_noclip ? "ALL COLLISION IS OFF; MAP EDGES WRAP."
                                : "NORMAL DUNGEON COLLISION RESTORED.",
                g->cheat_noclip ? 4 : 8);
        } else if (key == INPUT_WILDERNESS_TEST) {
            wilderness_test_run(g, player);
        } else if (key == INPUT_TRAINER) {
            trainer_run(g, player);
        } else if (key == 0x1B) {
            /* WORLD's dungeon dispatcher sends Escape to its transient-pane
             * clear path.  It does not quit and does not save; Q is the only
             * printed save-and-quit command. */
            continue;
        } else if (key == '*') {
            /* Undocumented WORLD controls: each key advances one VGA DAC
             * channel by 0x10.  Six-bit hardware makes four presses wrap. */
            g->wall_color_r = (u8)(g->wall_color_r + 0x10);
            game_refresh_world_palette(g);
        } else if (key == '(') {
            g->wall_color_g = (u8)(g->wall_color_g + 0x10);
            game_refresh_world_palette(g);
        } else if (key == ')') {
            g->wall_color_b = (u8)(g->wall_color_b + 0x10);
            game_refresh_world_palette(g);
        } else if (key == 'v' || key == 'V') {
            cmd_view_stats(g, player);
        } else if (key == 'm' || key == 'M') {
            cmd_view_money(g, player);
        } else if (key == 'p' || key == 'P') {
            cmd_pockets(g, player);
        } else if (key == 'e' || key == 'E') {
            cmd_exp_needed(g, player);
        } else if (key == 'h' || key == 'H') {
            cmd_help(g, player);
        } else if (key == 'j' || key == 'J') {
            cmd_bestiary(g);
        } else if (key == 'g' || key == 'G') {
            cmd_game_stats(g, player);
        } else if (key == 'x' || key == 'X') {
            cmd_expand_map(g);
        } else if (key == 'f' || key == 'F') {
            int monster = game_find_adjacent_monster(g);
            if (monster >= 0) {
                fight_monster(g, player, monster);
                if (mw_hp_cur(player) == 0) break;
            } else {
                town_message(g, player, "YOU ARE NOT CURRENTLY",
                             "ENGAGING ANY MONSTER.", "", 6);
            }
        } else if (key == 'c' || key == 'C') {
            int cast_result = cmd_cast_spell(g, player);
            if (cast_result != 0) {
                mw_audio_play(&g->audio, MW_SFX_MAGIC);
                if (cast_result == 1) game_advance_monsters(g, player);
            }
        } else if (key == 'i' || key == 'I') {
            int item_result = cmd_use_item_exploration(g, player);
            if (item_result != 0) {
                mw_audio_play(&g->audio, MW_SFX_MAGIC);
                if (item_result == 1) game_advance_monsters(g, player);
            }
        } else if (key == 'l' || key == 'L') {
            cmd_drop_item(g, player);
        } else if (key == 'w' || key == 'W') {
            cmd_weapons(g, player);
        } else if (key == 'a' || key == 'A') {
            cmd_armor(g, player);
        } else if (key == 'b' || key == 'B') {
            g->brick_speed = (g->brick_speed + 1) & 3;
            char line[64];
            static const char *const speed_name[4] = {
                "SLOW", "NORMAL", "FAST", "INSTANT"
            };
            snprintf(line, sizeof(line), "BRICK SPEED: %s (%d OF 4)",
                     speed_name[g->brick_speed], g->brick_speed + 1);
            video_fill_rect(v, 0, 0, SX(0x2D3), SY(42), 0);
            video_draw_text(v, 8, 8, line, 4);
            video_present(v);
            mw_audio_play(&g->audio, MW_SFX_UI);
            game_delay(g, 450);
        } else if (key == 'o' || key == 'O') {
            g->sound_enabled = !g->sound_enabled;
            mw_audio_set_enabled(&g->audio, g->sound_enabled);
            if (g->sound_enabled) mw_audio_play(&g->audio, MW_SFX_UI);
        } else if (key == 'z' || key == 'Z') {
            cmd_zoom(g, player);
        } else if (key == '1') {
            cmd_spells_in_effect(g, player, 0);
        } else if (key == '2') {
            cmd_spells_in_effect(g, player, 1);
        } else if (key == '3' &&
                   mw_experience_mode(player) == MW_EXPERIENCE_ENHANCED) {
            cmd_spells_in_effect(g, player, 2);
        } else if (key == 't' || key == 'T') {
            int monster = game_find_adjacent_monster(g);
            if (monster >= 0)
                (void)fight_monster_action(g, player, monster,
                                           COMBAT_ACTION_WAIT);
            else
                game_advance_monsters(g, player);
        } else if (key == 'u' || key == 'U') {
            int delta = ladder_delta(g, g->cur_x, g->cur_y);
            int shop = game_shop_type(g, g->cur_x, g->cur_y);
            if (shop) enter_town_location(g, player, shop);
            else if (delta < 0) game_change_floor(g, player, g->cur_floor + delta);
            else if (g->cheat_open_floor) {
                if (g->cur_floor > 0)
                    game_change_floor(g, player, g->cur_floor - 1);
                else
                    show_runtime_indicator(g, player,
                        "OPEN FLOOR MODE: TOP REACHED.",
                        "THERE IS NO FLOOR ABOVE TOWN.", 8);
            }
        } else if (key == 'd' || key == 'D') {
            int delta = ladder_delta(g, g->cur_x, g->cur_y);
            if (delta > 0) {
                game_change_floor(g, player, g->cur_floor + delta);
            } else if (g->cheat_open_floor) {
                if (g->cur_floor < game_dungeon_max_floor(g))
                    game_change_floor(g, player, g->cur_floor + 1);
                else
                    show_runtime_indicator(g, player,
                        "OPEN FLOOR MODE: BOTTOM REACHED.",
                        "THERE IS NO DEEPER DUNGEON FLOOR.", 8);
            } else if (cmd_dig_hole(g, player) && mw_hp_cur(player) == 0) {
                goto game_over;
            }
        } else if (key == 'k' || key == 'K') {
            int target = game_trapdoor_floor(g, g->cur_x, g->cur_y);
            int key_index = target >= 0 ? target / 10 : -1;
            if (target >= 0 && key_index > 0 && key_index < 18 &&
                player->trapdoor_keys[key_index]) {
                game_change_floor(g, player, target);
                game_relocate(g, player);
                town_message(g, player, "THE TRAPDOOR DROPS YOU!",
                             "YOU LAND ON A DEEPER FLOOR.", "", 12);
            } else if (target < 0) {
                town_message(g, player, "I DON'T SEE ANY TRAP DOOR HERE.",
                             "KEEP SEARCHING.", "", 12);
            } else {
                draw_trapdoor_notice(g, player);
                video_present(v);
                input_wait_any_key(&g->input);
            }
        } else if (key == 's' || key == 'S') {
            player->x_pos = (u16)g->cur_x;
            player->y_pos = (u16)g->cur_y;
            player->floor_depth = (u16)g->cur_floor;
            player->facing_dir = (u16)(g->last_move_dir & 3);
            player->dungeon_number = (u16)g->dungeon_number;
            game_save_character(g, slot);
            game_save_world_state(g);
            video_fill_rect(v, 0, 0, SX(0x2D3), SY(42), 0);
            video_draw_text(v, 8, 8, "GAME SAVED - CONTINUING.", 15);
            video_present(v);
            game_delay(g, 600);
        } else if (key == 'q' || key == 'Q') {
            player->x_pos = (u16)g->cur_x;
            player->y_pos = (u16)g->cur_y;
            player->floor_depth = (u16)g->cur_floor;
            player->facing_dir = (u16)(g->last_move_dir & 3);
            player->dungeon_number = (u16)g->dungeon_number;
            game_save_character(g, slot);
            game_save_world_state(g);
            video_clear(v, 0);
            video_draw_text(v, 180, 200, "GAME SAVED.", 15);
            video_present(v);
            game_delay(g, 1000);
            game_leave_character_session(g);
            goto title_screen;
        } else if (key == 0) {
            int scan = input_getch(&g->input);
            int direction = -1;
            switch (scan) {
            case 0x3B: /* F1 is the printed Help synonym. */
                cmd_help(g, player);
                break;
            case 0x48: /* Up arrow = North (Y-1) */
                direction = 0;
                break;
            case 0x50: /* Down arrow = South (Y+1) */
                direction = 1;
                break;
            case 0x4B: /* Left arrow = West (X-1) */
                direction = 2;
                break;
            case 0x4D: /* Right arrow = East (X+1) */
                direction = 3;
                break;
            }
            if (direction >= 0 && game_try_step(g, player, direction) < 0)
                goto game_over;
        }

        /* Waiting, magic and item use can all advance monsters.  Route any
         * resulting death through the same contract/permanent-death handler
         * immediately instead of drawing another zero-HP exploration frame. */
        if (mw_hp_cur(player) == 0) goto game_over;

        if (g->cur_x < 0) g->cur_x = 0;
        if (g->cur_y < 0) g->cur_y = 0;
        if (g->cur_x >= MAP_W) g->cur_x = MAP_W - 1;
        if (g->cur_y >= MAP_H) g->cur_y = MAP_H - 1;

        player->x_pos = g->cur_x;
        player->y_pos = g->cur_y;
        player->floor_depth = (u16)g->cur_floor;
        player->facing_dir = (u16)(g->last_move_dir & 3);
        player->dungeon_number = (u16)g->dungeon_number;
    }

game_over:
    ;
    int permanent_death = 0;
    if (mw_hp_cur(player) == 0) {
        video_clear(v, 0);
        video_draw_text(v, 160, 180, "YOU HAVE DIED!", 12);
        if (player->raise_x != 0xFFFFu) {
            video_draw_text(v, 80, 260, "YOUR RAISE CONTRACT SAVES YOU!", 10);
            int return_floor, return_x, return_y;
            int death_raised = character_apply_raise_contract(
                player, &return_floor, &return_x, &return_y);
            if (death_raised) {
                game_change_floor(g, player, return_floor);
                g->cur_x = return_x;
                g->cur_y = return_y;
                player->x_pos = (u16)return_x;
                player->y_pos = (u16)return_y;
                player->floor_depth = (u16)return_floor;
                reveal_around_player(g);
            }
        } else {
            video_draw_text(v, 100, 220, "YOUR ADVENTURE IS OVER...", 7);
            permanent_death = 1;
        }
        video_draw_text(v, 160, 300, "PRESS ANY KEY...", 15);
        video_present(v);
        input_wait_any_key(&g->input);
    }

    player->facing_dir = (u16)(g->last_move_dir & 3);
    player->dungeon_number = (u16)g->dungeon_number;
    game_save_character(g, slot);
    game_save_world_state(g);

    /* Closing the SDL window is the only in-session process-exit path.
     * Gameplay Q and every completed death flow return to the animated title.
     * A successfully raised character remains selectable with its recovered
     * position/HP, while a permanent-death tombstone frees the save slot. */
    if (input_poll_quit(&g->input)) return;
    if (permanent_death) g->char_exists[slot] = 0;
    game_leave_character_session(g);
    goto title_screen;
}
