#include "mw_game.h"
#include "mw_combat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void reveal_around_player(Game *g);
static int select_monster_floor(Game *g, int floor);
static int load_pit_group(Game *g, int group);
static int pit_bit_is_set(Game *g, int x, int y);
static int pitfall_target(Game *g, int x, int y);
static double experience_for_level(int level);
static void draw_trapdoor_notice(Game *g, Character *player);
static const u8 race_stat_base[RACE_COUNT][6];
static void roll_character_stats(Game *g, int race, u16 stats[6]);
static float starting_spell_points(const Character *p);
static int character_creation_self_test(void);

/* ── File path helper ── */

void game_make_path(Game *g, char *out, int out_sz, const char *filename) {
    snprintf(out, out_sz, "%s/%s", g->game_dir, filename);
}

/* ── RNG — exact match of original LCG ── */

void game_srand(Game *g, u32 seed) {
    g->rand_state = seed;
}

int game_rand(Game *g) {
    g->rand_state = g->rand_state * 0x15A4E35u + 1;
    return (int)((g->rand_state >> 16) & 0x7FFF);
}

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
        g->char_exists[slot] = 1;
        return 0;
    }

    g->char_exists[slot] = 0;
    return -1;
}

int game_save_character(Game *g, int slot) {
    if (slot < 0 || slot >= MAX_PLAYERS) return -1;

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

int game_load_bestiary(Game *g, int slot) {
    static const u8 magic[8] = {'M','W','B','E','S','T','0','1'};
    enum { HEADER_SIZE = 12, RECORD_SIZE = 4 };
    u8 data[HEADER_SIZE + BESTIARY_MONSTER_COUNT * RECORD_SIZE];
    char name[24], path[300];

    memset(g->bestiary_kills, 0, sizeof(g->bestiary_kills));
    g->bestiary_loaded = 1;
    g->bestiary_dirty = 0;
    if (slot < 0 || slot >= MAX_PLAYERS) return -1;

    make_bestiary_name(name, sizeof(name), slot);
    game_make_path(g, path, sizeof(path), name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0; /* Existing DOS/port saves begin with an empty record. */
    size_t got = fread(data, 1, sizeof(data), f);
    fclose(f);
    if (got != sizeof(data) || memcmp(data, magic, sizeof(magic)) != 0 ||
        read_le32(data + 8) != BESTIARY_MONSTER_COUNT)
        return -1;

    for (int i = 0; i < BESTIARY_MONSTER_COUNT; i++)
        g->bestiary_kills[i] = read_le32(data + HEADER_SIZE + i * RECORD_SIZE);
    return 0;
}

int game_save_bestiary(Game *g) {
    static const u8 magic[8] = {'M','W','B','E','S','T','0','1'};
    enum { HEADER_SIZE = 12, RECORD_SIZE = 4 };
    u8 data[HEADER_SIZE + BESTIARY_MONSTER_COUNT * RECORD_SIZE];
    char name[24], path[300];

    if (!g->bestiary_loaded || g->active_save_slot < 0 ||
        g->active_save_slot >= MAX_PLAYERS) return 0;
    memcpy(data, magic, sizeof(magic));
    write_le32(data + 8, BESTIARY_MONSTER_COUNT);
    for (int i = 0; i < BESTIARY_MONSTER_COUNT; i++)
        write_le32(data + HEADER_SIZE + i * RECORD_SIZE,
                   g->bestiary_kills[i]);

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
    if (g->worldmap_data) printf("Loaded WORLDMAP.BIN: %d bytes\n", wsz);

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
             landing < floor + 3 && landing < 202; landing++) {
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

/* Check if player can move to (nx, ny) from (ox, oy).
   Checks wall bits on the exit side of the current cell.  Do not use 0xFF as
   an invalid-cell sentinel here: four open (3) edges are also exactly 0xFF. */
int game_can_move(Game *g, int ox, int oy, int nx, int ny) {
    if (ox < 0 || ox >= MAP_W || oy < 0 || oy >= MAP_H) return 0;
    if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) return 0;
    u8 src = map_get_cell(g, ox, oy);
    int wall_val = 0;
    if (ny < oy) wall_val = src & WALL_N_MASK;
    else if (ny > oy) wall_val = (src & WALL_S_MASK) >> 4;
    else if (nx < ox) wall_val = (src & WALL_W_MASK) >> 6;
    else if (nx > ox) wall_val = (src & WALL_E_MASK) >> 2;

    return wall_val == 3;
}

/* ── Persistent MON.MAP and .DUN world state ── */

static int monster_record_hp(const MonsterRecord *m) {
    return (int)m->hp_lo | ((int)m->hp_hi << 8);
}

static void monster_record_set_hp(MonsterRecord *m, int hp) {
    if (hp < 0) hp = 0;
    if (hp > 0xFFFF) hp = 0xFFFF;
    m->hp_lo = (u8)hp;
    m->hp_hi = (u8)(hp >> 8);
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
    m->hp_lo = m->hp_hi = m->type = m->level = 0;
}

static void make_monster_map_name(char *out, int out_sz, int slot) {
    snprintf(out, out_sz, "%dMON.MAP", slot);
}

static int quest_boss_type(Game *g, int floor) {
    static const int quest_floor[8] = {4, 8, 12, 16, 125, 150, 175, 200};
    u8 flags = 0;
    if (g->cur_player >= 0 && g->cur_player < MAX_PLAYERS)
        flags = g->chars[g->cur_player].quest_flags;
    for (int i = 0; i < 8; i++)
        if (floor == quest_floor[i] && !(flags & (1u << i)))
            return 104 + i;
    return -1;
}

static void roll_monster_identity(Game *g, MonsterRecord *m,
                                  int floor, int forced_type) {
    int type = forced_type >= 0 ? forced_type :
               combat_pick_monster_type(g, floor);
    int level = floor + (game_rand(g) % 5) - 2;
    if (level < 1) level = 1;
    if (level > 255) level = 255;
    int cap = combat_calc_monster_hp(&monster_types[type], level);
    int hp = cap > 1 ? 1 + game_rand(g) % cap : 1;
    monster_record_set_hp(m, hp);
    m->type = (u8)type;
    m->level = (u8)level;
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
        if (m->type >= 104 || !combat_monster_type_valid(m->type, floor)) {
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
    fwrite(g->monster_floor, 1, MONSTER_MAP_LAYERS, f);
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
    g->monster_floor[layer] = (u8)floor;
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
        if (g->monster_floor[i] == (u8)floor) {
            g->monster_layer = i;
            sanitize_monster_floor(g, i, floor);
            return i;
        }
    }

    /* MON.MAP is a three-floor cache.  Rotate the oldest layer out exactly
     * as the DOS game does when a newly visited floor is entered. */
    memmove(&g->monster_floor[0], &g->monster_floor[1],
            MONSTER_MAP_LAYERS - 1);
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
        size_t a = fread(g->monster_floor, 1, MONSTER_MAP_LAYERS, f);
        size_t b = fread(g->monster_map, sizeof(MonsterRecord),
                         MONSTER_MAP_LAYERS * MONSTERS_PER_FLOOR, f);
        fclose(f);
        if (a != MONSTER_MAP_LAYERS ||
            b != MONSTER_MAP_LAYERS * MONSTERS_PER_FLOOR) {
            memset(g->monster_floor, 0xFF, sizeof(g->monster_floor));
        }
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

int game_find_adjacent_monster(Game *g) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dy[4] = {-1, 1, 0, 0};
    for (int d = 0; d < 4; d++) {
        int x = g->cur_x + dx[d], y = g->cur_y + dy[d];
        if (!game_can_move(g, g->cur_x, g->cur_y, x, y)) continue;
        int index = game_find_monster(g, x, y);
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
}

void game_advance_monsters(Game *g, Character *player) {
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

    for (int i = 0; i < MONSTERS_PER_FLOOR; i++) {
        MonsterRecord *m = &map[i];
        if (!monster_record_alive(g, m)) continue;
        int distance = abs((int)m->x - g->cur_x) + abs((int)m->y - g->cur_y);
        if (distance <= 1) continue;
        int notices = distance <= 12 && (!player->eff_invisible || game_rand(g) % 3 == 0);
        if (!notices && game_rand(g) % 8) continue;

        int order[4] = {0, 1, 2, 3};
        if (notices) {
            int hx = g->cur_x - (int)m->x;
            int hy = g->cur_y - (int)m->y;
            order[0] = abs(hx) >= abs(hy) ? (hx < 0 ? 2 : 3) : (hy < 0 ? 0 : 1);
            order[1] = abs(hx) >= abs(hy) ? (hy < 0 ? 0 : 1) : (hx < 0 ? 2 : 3);
            order[2] = order[0] ^ 1;
            order[3] = order[1] ^ 1;
        } else {
            int r = game_rand(g) & 3;
            for (int k = 0; k < 4; k++) order[k] = (r + k) & 3;
        }

        for (int k = 0; k < 4; k++) {
            int d = order[k], nx = (int)m->x + dx[d], ny = (int)m->y + dy[d];
            if (nx == g->cur_x && ny == g->cur_y) continue;
            if (!game_can_move(g, m->x, m->y, nx, ny)) continue;
            int occupied = 0;
            for (int j = 0; j < MONSTERS_PER_FLOOR; j++) {
                if (j != i && monster_record_alive(g, &map[j]) &&
                    map[j].x == nx && map[j].y == ny) { occupied = 1; break; }
            }
            if (occupied) continue;
            m->x = (u8)nx; m->y = (u8)ny;
            g->monster_map_dirty = 1;
            break;
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
    int modulus = 230 - floor / 3;
    if (modulus < 20) modulus = 20;
    int threshold = floor > 9 ? 5 : 3;
    if (dungeon_hash(x, y, floor, g->dungeon_number, modulus) >= threshold)
        return floor;
    for (int target = floor + 1; target <= floor + threshold && target <= 180; target++)
        if (!rock_cell_at(g, x, y, target)) return target;
    return floor;
}

int game_change_floor(Game *g, Character *player, int new_floor) {
    if (new_floor < 0) new_floor = 0;
    if (new_floor > 201) new_floor = 201;
    if (new_floor == g->cur_floor) return 0;
    save_monster_map(g);
    g->cur_floor = new_floor;
    player->floor_depth = (u16)new_floor;
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
    return 1;
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

int game_apply_pitfall(Game *g, Character *player) {
    if (g->cur_floor <= 0 || ladder_delta(g, g->cur_x, g->cur_y) != 0 ||
        game_trapdoor_floor(g, g->cur_x, g->cur_y) >= 0 ||
        pit_bit_is_set(g, g->cur_x, g->cur_y)) return 0;
    set_pit_bit(g, g->cur_x, g->cur_y);
    if (player->eff_feather) return 0;
    int target = pitfall_target(g, g->cur_x, g->cur_y);
    if (target == g->cur_floor) return 0;
    game_change_floor(g, player, target);
    save_pit_group(g);
    return 1;
}

/* ── Initialization ── */

int game_init(Game *g, const char *data_dir) {
    memset(g, 0, sizeof(*g));
    g->active_save_slot = -1;
    g->monster_layer = -1;
    g->pit_group = -1;
    g->brick_speed = 3;
    g->sound_enabled = 1;
    g->map_player_visible = 1;
    strncpy(g->game_dir, data_dir, sizeof(g->game_dir) - 1);

    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
            fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return -1;
        }
    }

    if (video_init(&g->video, "Moraff's World", 1) < 0) {
        return -1;
    }

    input_init(&g->input);

    char path[260];
    game_make_path(g, path, sizeof(path), "1024X768.FNT");
    if (video_load_font(&g->video, path) < 0) {
        game_make_path(g, path, sizeof(path), "1024x768.FNT");
        if (video_load_font(&g->video, path) < 0) {
            game_make_path(g, path, sizeof(path), "640X480.FNT");
            if (video_load_font(&g->video, path) < 0) {
                game_make_path(g, path, sizeof(path), "360X480.FNT");
                if (video_load_font(&g->video, path) < 0) {
                    game_make_path(g, path, sizeof(path), "320X200.FNT");
                    video_load_font(&g->video, path);
                }
            }
        }
    }

    game_srand(g, (u32)SDL_GetTicks());

    for (int i = 0; i < MAX_PLAYERS; i++) {
        game_load_character(g, i);
    }

    game_load_pics(g);
    game_load_dungeon(g);
    game_load_monsters(g);

    g->num_players = 1;
    g->cur_player = 0;

    /* WORLD.ASM modes 8-10 are the chipset-specific 1024x768 variants. */
    g->video_mode = 8;
    g->screen_w = LOGICAL_W;
    g->screen_h = LOGICAL_H;

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

    video_shutdown(&g->video);
    SDL_Quit();
}

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

/* ── Check if any adjacent cell has a monster ── */

static int has_adjacent_monster(Game *g) {
    g->monster_adjacent = game_find_adjacent_monster(g) >= 0;
    return g->monster_adjacent;
}

/* ── Drawing: 1024-mode map window (WORLD.ASM sub_086F1/far_1FAE6) ── */

void draw_minimap(Game *g, int mx, int my, int mw, int mh) {
    Video *v = &g->video;
    const int cell_px = 10; /* mode 8-10 value at DS:4488 */
    int cols = mw / cell_px; /* 18 columns */
    int rows = mh / cell_px; /* 38 rows */
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

            video_fill_rect(v, mx + gx * cell_px, my + gy * cell_px,
                            cell_px, cell_px, 0);
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

            int x = mx + gx * cell_px;
            int y = my + gy * cell_px;
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
                video_fill_rect(v, x + 1, y + 1, cell_px - 2, cell_px - 2,
                                (u8)(shop + 2));

            /* far_1F3FD calls func_1F077 once for each actual edge.  Open
             * value 3 is left undrawn; doors get a broken white line with a
             * yellow center, while both stone wall values are solid white. */
            if (edge[0] != 3) {
                video_hline(v, x, y, cell_px, 46);
                if (edge[0] == 1) video_hline(v, x + 3, y, 4, 4);
            }
            if (edge[2] != 3) {
                video_hline(v, x, y + cell_px - 1, cell_px, 46);
                if (edge[2] == 1)
                    video_hline(v, x + 3, y + cell_px - 1, 4, 4);
            }
            if (edge[3] != 3) {
                video_vline(v, x, y, cell_px, 46);
                if (edge[3] == 1) video_vline(v, x, y + 3, 4, 4);
            }
            if (edge[1] != 3) {
                video_vline(v, x + cell_px - 1, y, cell_px, 46);
                if (edge[1] == 1)
                    video_vline(v, x + cell_px - 1, y + 3, 4, 4);
            }

            /* Red corner pixels reproduce the brick joints against the
             * dark-red unexplored field without inventing neighbor walls. */
            video_put_pixel(v, x, y, 10);
            video_put_pixel(v, x + cell_px - 1, y + cell_px - 1, 10);

            int ladder = ladder_delta(g, wx, wy);
            if (ladder > 0) {
                /* The DOS map uses opposite diagonal strokes for the two
                 * ladder directions.  Down is a backslash. */
                for (int p = 2; p <= 7; p++) {
                    video_put_pixel(v, x + p, y + p, 47);
                    video_put_pixel(v, x + p, y + p + 1, 47);
                }
            } else if (ladder < 0) {
                /* Up is a forward slash. */
                for (int p = 2; p <= 7; p++) {
                    video_put_pixel(v, x + p, y + 9 - p, 4);
                    video_put_pixel(v, x + p, y + 8 - p, 4);
                }
            }

            if (shop) {
                /* The town help calls these "colored squares" and explicitly
                 * identifies them as ladders going up to a location.  Put a
                 * high-contrast ladder inside the authentic type+2 color. */
                u8 shop_color = (u8)(shop + 2);
                video_fill_rect(v, x + 1, y + 1, 8, 8, shop_color);
                video_hline(v, x + 1, y + 1, 8, 46);
                video_hline(v, x + 1, y + 8, 8, 46);
                video_vline(v, x + 1, y + 1, 8, 46);
                video_vline(v, x + 8, y + 1, 8, 46);
                video_vline(v, x + 3, y + 2, 6, 0);
                video_vline(v, x + 6, y + 2, 6, 0);
                video_hline(v, x + 3, y + 3, 4, 4);
                video_hline(v, x + 3, y + 5, 4, 4);
                video_hline(v, x + 3, y + 7, 4, 4);
                video_vline(v, x + 5, y + 2, 5, 4);
                video_put_pixel(v, x + 4, y + 2, 4);
                video_put_pixel(v, x + 6, y + 2, 4);
            }

            int trap = game_trapdoor_floor(g, wx, wy);
            if (!ladder && trap >= 0) {
                /* WORLD describes a trap door with an X, not a generic
                   unexplained square. */
                for (int p = 2; p <= 7; p++) {
                    video_put_pixel(v, x + p, y + p, 4);
                    video_put_pixel(v, x + 9 - p, y + p, 4);
                }
            } else if (!ladder && g->cur_floor > 0 &&
                       pit_bit_is_set(g, wx, wy) &&
                       pitfall_target(g, wx, wy) != g->cur_floor) {
                /* A previously triggered pit remains on the map. */
                video_fill_rect(v, x + 3, y + 3, 4, 4, 3);
            }

            if (wx == g->cur_x && wy == g->cur_y) {
                /* Like WORLD's map cursor, the party is a blinking square.
                 * Its off phase exposes the feature beneath it. */
                if (g->map_player_visible) {
                    video_fill_rect(v, x + 3, y + 3, 4, 4, 15);
                    video_fill_rect(v, x + 4, y + 4, 2, 2, 6);
                }
            }
        }
    }
}

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
    if (x2 < 0 || x2 >= MAP_W || y2 < 0 || y2 >= MAP_H) return 0;
    if (y2 < y1) return map_get_edge(g, x1,     y1,     1);
    if (y2 > y1) return map_get_edge(g, x1,     y1 + 1, 1);
    if (x2 < x1) return map_get_edge(g, x1,     y1,     0);
    if (x2 > x1) return map_get_edge(g, x1 + 1, y1,     0);
    return 3;
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

static u8 remap_wall_texel(u8 src, int door) {
    if (src == 14) return 32;       /* E0E0E0 face */
    if (src == 12) return 33;       /* C0C0C0 joints/highlight */
    if (src == 13) return 34;       /* blue cracks */
    if (door) {
        /* Mode 8 changes these low DAC entries while drawing WALL.PIC. */
        if (src == 1) return 0;                 /* black door boards */
        if (src == 6 || src == 11 || src == 13) return 2; /* bright blue arch */
        return src;                             /* white ironwork */
    }
    if (src == 0 || src == 16) return 32;
    return 34;
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
                remap_wall_texel(sample_wall_texel(tex, tx, ty), door);
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

static void draw_dungeon_gradient(Video *v, int vx, int vy, int vw, int vh) {
    static const u8 ceiling[] = {35, 36, 35, 40, 36, 35, 39, 40};
    static const u8 floor[]   = {41, 0, 42, 0, 43, 0, 44, 0, 45, 0};
    int horizon = vy + vh / 2;
    for (int x = vx; x < vx + vw; x++) {
        u8 cc = ceiling[(x - vx) & 7];
        u8 fc = floor[(x - vx) % 10];
        video_vline(v, x, vy, horizon - vy, cc);
        video_vline(v, x, horizon, vy + vh - horizon, fc);
    }
    video_hline(v, vx, horizon - 2, vw, 0);
    video_hline(v, vx, horizon - 1, vw, 34);
    video_hline(v, vx, horizon, vw, 44);
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
                remap_wall_texel(sample_wall_texel(tex, tx, ty), door);
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

static void draw_ladder(Video *v, ProjRect p, int delta,
                        int vx, int vy, int vw, int vh) {
    int pw = p.right - p.left + 1;
    int ph = p.bottom - p.top + 1;
    if (pw < 8 || ph < 8) return;
    int cx = (p.left + p.right) / 2;
    int rail = pw / 7;
    if (rail < 3) rail = 3;

    if (delta < 0) {
        int top = p.top + ph / 10;
        int bottom = p.bottom - ph / 8;
        dungeon_line(v, cx - rail, top, cx - rail, bottom, 46,
                     vx, vy, vw, vh);
        dungeon_line(v, cx + rail, top, cx + rail, bottom, 46,
                     vx, vy, vw, vh);
        for (int i = 1; i < 6; i++) {
            int y = top + (bottom - top) * i / 6;
            dungeon_line(v, cx - rail, y, cx + rail, y, 10,
                         vx, vy, vw, vh);
        }
    } else {
        /* Perspective rails descending into the floor opening behind them. */
        int far_y = p.top + ph / 10;
        int near_y = p.bottom - ph / 12;
        int near_r = pw * 2 / 5;
        int far_r = near_r / 5 + 1;
        dungeon_line(v, cx - far_r, far_y, cx - near_r, near_y, 46,
                     vx, vy, vw, vh);
        dungeon_line(v, cx + far_r, far_y, cx + near_r, near_y, 46,
                     vx, vy, vw, vh);
        for (int i = 1; i < 5; i++) {
            int y = far_y + (near_y - far_y) * i / 5;
            int half = far_r + (near_r - far_r) * (y - far_y) /
                                  ((near_y - far_y) ? (near_y - far_y) : 1);
            dungeon_line(v, cx - half, y, cx + half, y, 7,
                         vx, vy, vw, vh);
        }
    }
}

/* Draw a WORLD.PIC actor in perspective while respecting the wall depth for
 * every viewport column.  WORLD.PIC index 0 is the original ladder artwork;
 * index 1 is the trapdoor/pit artwork; monster indices are mapped in combat. */
static void draw_pic_billboard(Game *g, int pic_index, int cx, int top,
                               int draw_h, float depth,
                               int vx, int vy, int vw, int vh,
                               const float *wall_depth, int replace_color) {
    if (pic_index < 0 || pic_index >= g->world_pic_count || draw_h < 2) return;
    const u8 *pic = g->world_pic_data[pic_index];
    int pic_size = g->world_pic_sizes[pic_index];
    if (!pic || pic_size < 0x192) return;
    const int rows_count = 200, table_size = 0x190;
    const u8 *data = pic + table_size;
    int data_len = pic_size - table_size;
    int draw_w = draw_h * 3 / 4;
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
            if (color == 17 && replace_color >= 0)
                color = replace_color;
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

typedef struct ViewActor {
    float depth, lateral;
    int pic;
    int kind;       /* 0 monster, 1 up ladder/shop, 2 down, 3 trapdoor */
    int color;      /* original replacement color for shared monster art */
} ViewActor;

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
            float dx = (float)map[i].x - (float)g->cur_x;
            float dy = (float)map[i].y - (float)g->cur_y;
            float forward = dx * fdx + dy * fdy;
            float side = dx * rdx + dy * rdy;
            if (forward < 0.35f || forward > 12.0f || fabsf(side) > forward * 1.05f)
                continue;
            int pic = get_monster_pic_index_ext(map[i].type);
            if (pic < 2) pic = 2; /* keep rare text-only DOS types visible */
            actors[count++] = (ViewActor){forward, side, pic, 0,
                                           get_monster_color_ext(map[i].type)};
        }
    }

    /* Coordinate features use their original WORLD.PIC images.  Scan the
     * visible fan rather than just the centre line, so ladders at a junction
     * appear in the correct side of the viewport. */
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
            int trap = game_trapdoor_floor(g, x, y);
            int shop = game_shop_type(g, x, y);
            if (shop && count < (int)(sizeof(actors)/sizeof(actors[0])))
                /* Town locations look exactly like ordinary ladders up in
                 * the viewports; their type color belongs only on the map. */
                actors[count++] = (ViewActor){forward, side, 0, 1, -1};
            else if (ladder && count < (int)(sizeof(actors)/sizeof(actors[0])))
                actors[count++] = (ViewActor){forward, side, 0,
                                               ladder < 0 ? 1 : 2, -1};
            else if (trap >= 0 && count < (int)(sizeof(actors)/sizeof(actors[0])))
                actors[count++] = (ViewActor){forward, side, 1, 3, -1};
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
        else if (a->kind == 3) height = wall_h / 2;
        else height = wall_h * 4 / 5;
        if (height < 6) height = 6;
        int top = bottom - height;
        if (a->kind == 1)
            top = horizon - wall_h * 3 / 5;
        else if (a->kind == 2)
            top = horizon - wall_h / 5;
        else if (a->kind == 3)
            top = horizon + wall_h / 8;
        int depth_col = cx - vx;
        if (depth_col < 0) depth_col = 0;
        if (depth_col >= vw) depth_col = vw - 1;
        int feature_center_visible =
            a->depth < wall_depth[depth_col] + 0.02f;
        if (a->kind == 3) {
            int sw = height * 2;
            int hatch_h = height / 2;
            for (int sy = top; sy < top + hatch_h; sy++) {
                if (sy < vy || sy >= vy + vh) continue;
                for (int sx = cx - sw / 2; sx <= cx + sw / 2; sx++) {
                    if (sx < vx || sx >= vx + vw) continue;
                    if (a->depth >= wall_depth[sx - vx] + 0.02f) continue;
                    int px = sx - (cx - sw / 2);
                    int py = sy - top;
                    int d1 = abs(px * hatch_h - py * sw);
                    int d2 = abs((sw - px) * hatch_h - py * sw);
                    if (d1 <= sw || d2 <= sw)
                        g->video.pixels[sy * LOGICAL_W + sx] = 4;
                }
            }
            g->video.dirty = 1;
        } else if (a->kind == 0) {
            draw_pic_billboard(g, a->pic, cx, top, height, a->depth,
                               vx, vy, vw, vh, wall_depth, a->color);
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
                               vx, vy, vw, vh, wall_depth, -1);
            draw_ladder(&g->video, ladder_box, delta, vx, vy, vw, vh);
            if (a->kind == 1) {
                int arrow_y = top + height / 10;
                dungeon_line(&g->video, cx, arrow_y, cx - height / 8,
                             arrow_y + height / 7, 4, vx, vy, vw, vh);
                dungeon_line(&g->video, cx, arrow_y, cx + height / 8,
                             arrow_y + height / 7, 4, vx, vy, vw, vh);
                dungeon_line(&g->video, cx, arrow_y, cx,
                             arrow_y + height / 3, 4, vx, vy, vw, vh);
            } else {
                int arrow_y = top + height * 2 / 3;
                dungeon_line(&g->video, cx, arrow_y, cx,
                             arrow_y + height / 4, 47, vx, vy, vw, vh);
                dungeon_line(&g->video, cx, arrow_y + height / 4,
                             cx - height / 8, arrow_y + height / 8,
                             47, vx, vy, vw, vh);
                dungeon_line(&g->video, cx, arrow_y + height / 4,
                             cx + height / 8, arrow_y + height / 8,
                             47, vx, vy, vw, vh);
            }
        }
    }

    /* Coordinate features at the party's exact position are deliberately
     * absent from every viewport.  WORLD only draws them while approaching;
     * once the party steps onto one, the map and command/status text carry
     * that information instead. */
}

static void draw_3d_viewport(Game *g, int vx, int vy, int vw, int vh, int dir) {
    Video *v = &g->video;

    draw_dungeon_gradient(v, vx, vy, vw, vh);

    /* The ray pass draws front faces, side faces, corners and door planes as
     * one connected scene. */
    float wall_depth[LOGICAL_W];
    draw_ray_walls(g, vx, vy, vw, vh, dir, wall_depth);

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
#define VR(x1,y1,x2,y2) {SX(x1), SY(y1), SX(x2)-SX(x1)+1, SY(y2)-SY(y1)+1}

static const ViewLayout view_layouts[3] = {
    /* Mode 0: Full size */
    {
        VR(0x2D3, 0x000, 0x484, 0x258), /* North */
        VR(0x11B, 0x1AE, 0x2CD, 0x406), /* West */
        VR(0x2D3, 0x25D, 0x484, 0x487), /* South */
        VR(0x48A, 0x1AE, 0x63E, 0x406), /* East */
    },
    /* Mode 1: Medium */
    {
        VR(0x2D3, 0x12C, 0x484, 0x258), /* North */
        VR(0x11B, 0x1AE, 0x2CD, 0x2DA), /* West */
        VR(0x2D3, 0x25D, 0x484, 0x389), /* South */
        VR(0x48A, 0x1AE, 0x63E, 0x2DA), /* East */
    },
    /* Mode 2: Small */
    {
        VR(0x340, 0x12C, 0x417, 0x258), /* North */
        VR(0x260, 0x1AE, 0x33A, 0x2DA), /* West */
        VR(0x340, 0x25D, 0x417, 0x384), /* South */
        VR(0x41D, 0x1AE, 0x4F7, 0x2DA), /* East */
    },
};

static void draw_4way_view(Game *g) {
    const ViewLayout *vl = &view_layouts[g->view_mode % 3];

    draw_3d_viewport(g, vl->north.x, vl->north.y, vl->north.w, vl->north.h, 0);
    draw_3d_viewport(g, vl->west.x,  vl->west.y,  vl->west.w,  vl->west.h,  2);
    draw_3d_viewport(g, vl->south.x, vl->south.y, vl->south.w, vl->south.h, 1);
    draw_3d_viewport(g, vl->east.x,  vl->east.y,  vl->east.w,  vl->east.h,  3);

}

/* ── Drawing: Command menu (matches func_27112, top-right area) ── */

static void draw_command_menu(Game *g) {
    Video *v = &g->video;
    const int menu_x = SX(0x48C);
    /* One extra command row fits above the east viewport with the original
       glyph proportions by tightening only the inter-row pitch slightly. */
    const int spacing = SY(35);
    const int xsn = 7, xsd = 6;
    const int ysn = 12, ysd = 17;
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
    video_draw_text_scaled_xy(v, menu_x, y, "BEASTIARY ( )       ", 8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "           J        ", 4, xsn, xsd, ysn, ysd);
    y += spacing;
    video_draw_text_scaled_xy(v, menu_x, y, "SPELLS IN EFFECT  ", 8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "                 1", 4, xsn, xsd, ysn, ysd);
    y += spacing;
    video_draw_text_scaled_xy(v, menu_x, y, "SPELLS IN EFFECT  ", 8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "                 2", 4, xsn, xsd, ysn, ysd);
    y += spacing;
    video_draw_text_scaled_xy(v, menu_x, y, " UIT-SAVE  ELP (  )", 8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "Q         H     F1 ", 4, xsn, xsd, ysn, ysd);
}

/* Convert a click on the command legend into the exact same command byte as
 * its keyboard hotkey.  SDL performs the inverse logical-size transform so
 * this remains accurate for resized and letterboxed windows. */
static int command_menu_click_key(Game *g, int window_x, int window_y) {
    static const int command[12][2] = {
        {'b','m'}, {'w','v'}, {'z','c'}, {'i','x'},
        {'a','l'}, {'f','p'}, {'t','e'}, {'o','o'},
        {'j','j'}, {'1','1'}, {'2','2'}, {'q','h'}
    };
    float logical_x, logical_y;
    SDL_RenderWindowToLogical(g->video.renderer, window_x, window_y,
                              &logical_x, &logical_y);
    const int menu_x = SX(0x48C);
    const int spacing = SY(35);
    int adv = g->video.font_advance ? g->video.font_advance
                                    : g->video.font_char_w;
    int scaled_advance = adv * 7 / 6;
    int scaled_height = g->video.font_char_h * 12 / 17;
    int menu_right = menu_x + scaled_advance * 20;
    if (logical_x < menu_x || logical_x >= menu_right ||
        logical_y < 0 || logical_y >= spacing * 11 + scaled_height)
        return 0;
    int row = (int)logical_y / spacing;
    if (row > 11) row = 11;
    int right_column = logical_x >= menu_x + scaled_advance * 9;
    return command[row][right_column];
}

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

    snprintf(line, sizeof(line), "L:%d  X:%d  Y:%d",
             g->cur_floor, g->cur_x, g->cur_y);
    video_draw_text_scaled_xy(v, 0, y, line, 6, xsn, xsd, ysn, ysd);
    snprintf(line, sizeof(line), "STR: %d  CON: %d",
             player->stat_str, player->stat_con);
    video_draw_text_scaled_xy(v, right_x + 7, y, line, 47, xsn, xsd, ysn, ysd);
    y += line_step;

    snprintf(line, sizeof(line), "SPELL POINTS: %.0f OF %.0f",
             player->sp_cur, player->sp_max);
    video_draw_text_scaled_xy(v, 0, y, line, 6, xsn, xsd, ysn, ysd);
    snprintf(line, sizeof(line), "INT: %d  DEX: %d",
             player->stat_int, player->stat_agi);
    video_draw_text_scaled_xy(v, right_x + 7, y, line, 47, xsn, xsd, ysn, ysd);
    y += line_step;

    snprintf(line, sizeof(line), "HEALTH POINTS: %d OF %d",
             player->hp_cur, player->hp_max);
    video_draw_text_scaled_xy(v, 0, y, line, 6, xsn, xsd, ysn, ysd);
    char action_line[64];
    const char *action = "HIT 'D' TO DIG A HOLE";
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
                              action, 48, 13, 12, ysn, ysd);
    snprintf(line, sizeof(line), "WIZ: %d  LUCK: %d",
             player->stat_wis, player->stat_luck);
    video_draw_text_scaled_xy(v, right_x + 7, y, line, 47, xsn, xsd, ysn, ysd);
}

/* ── Weapon/armor name tables (from DS:0x1C0 and DS:0x214) ── */

static const char *weapon_names[] = {
    "FIST", "STICK", "CLUB", "MACE", "KNIFE",
    "SHORTSWORD", "LONG SWORD", "GREAT SWORD",
    "POWER WEAPON 1", "POWER WEAPON 2", "POWER WEAPON 3", "POWER WEAPON 4"
};
#define WEAPON_COUNT 12

static const char *armor_names[] = {
    "SKIN", "LEATHER", "CHAIN", "SCALE", "PLATE",
    "FIELD PLATE", "TITANIUM", "OGRE"
};
#define ARMOR_COUNT 8

/* ── Command: View Stats (func_0DF4A) ── */

static void cmd_view_stats(Game *g, Character *p) {
    Video *v = &g->video;
    int fh = v->font_char_h + 2;
    char line[128];

    video_clear(v, 0);
    int y = 4;

    snprintf(line, sizeof(line), "VIEW STATS FOR %s", p->name);
    video_draw_text(v, 8, y, line, 3);
    y += fh + 2;

    const char *race_str = (p->race < RACE_COUNT) ? race_names[p->race] : "???";
    snprintf(line, sizeof(line), "RACE: %s", race_str);
    video_draw_text(v, 8, y, line, 4);
    y += fh;

    snprintf(line, sizeof(line), "SEX: %s", p->sex == 0 ? "MALE" : "FEMALE");
    video_draw_text(v, 8, y, line, 4);
    y += fh;

    const char *cls = (p->class_id < CLASS_COUNT) ? class_names[p->class_id] : "???";
    snprintf(line, sizeof(line), "CLASS: %s", cls);
    video_draw_text(v, 8, y, line, 4);
    y += fh;

    snprintf(line, sizeof(line), "MONEY IN POCKET: %u", p->jewels_pocket);
    video_draw_text(v, 8, y, line, 8);
    y += fh;

    snprintf(line, sizeof(line), "MONEY IN BANK: %u", p->jewels_bank);
    video_draw_text(v, 8, y, line, 8);
    y += fh;

    snprintf(line, sizeof(line), "TOTAL MONEY: %u",
             p->jewels_pocket + p->jewels_bank);
    video_draw_text(v, 8, y, line, 8);
    y += fh + 2;

    snprintf(line, sizeof(line), "STRENGTH: %d", p->stat_str);
    video_draw_text(v, 8, y, line, 6);
    y += fh;

    snprintf(line, sizeof(line), "INTELLIGENCE: %d", p->stat_int);
    video_draw_text(v, 8, y, line, 6);
    y += fh;

    snprintf(line, sizeof(line), "WISDOM: %d", p->stat_wis);
    video_draw_text(v, 8, y, line, 6);
    y += fh;

    snprintf(line, sizeof(line), "CONSTITUTION: %d", p->stat_con);
    video_draw_text(v, 8, y, line, 6);
    y += fh;

    snprintf(line, sizeof(line), "AGILITY: %d", p->stat_agi);
    video_draw_text(v, 8, y, line, 6);
    y += fh;

    snprintf(line, sizeof(line), "LUCK: %d", p->stat_luck);
    video_draw_text(v, 8, y, line, 6);
    y += fh + 2;

    const char *wpn = (p->equipped_weapon < WEAPON_COUNT) ?
        weapon_names[p->equipped_weapon] : "UNKNOWN";
    snprintf(line, sizeof(line), "WEAPON IN HAND: %s", wpn);
    video_draw_text(v, 8, y, line, 4);
    y += fh;

    const char *arm = (p->equipped_armor < ARMOR_COUNT) ?
        armor_names[p->equipped_armor] : "UNKNOWN";
    snprintf(line, sizeof(line), "CURRENT ARMOR: %s", arm);
    video_draw_text(v, 8, y, line, 4);
    y += fh;

    snprintf(line, sizeof(line), "LEVEL: %d", p->level);
    video_draw_text(v, 8, y, line, 5);
    y += fh;

    snprintf(line, sizeof(line), "EXPERIENCE: %.0f", p->experience);
    video_draw_text(v, 8, y, line, 5);
    y += fh;

    video_draw_text(v, 8, y,
                    p->raise_x == 0xFFFFu ?
                    "NO RAISE DEAD CONTRACT" : "RAISE DEAD CONTRACT IN EFFECT",
                    p->raise_x == 0xFFFFu ? 8 : 3);
    y += fh;

    if (p->diseased_turns > 0) {
        snprintf(line, sizeof(line), "YOU ARE DISEASED-MOVES LEFT: %d",
                 p->diseased_turns);
        video_draw_text(v, 8, y, line, 8);
        y += fh;
    }
    if (p->poisoned_turns > 0) {
        snprintf(line, sizeof(line), "YOU ARE POISONED-MOVES LEFT: %d",
                 p->poisoned_turns);
        video_draw_text(v, 8, y, line, 6);
        y += fh;
    }

    video_draw_text(v, 8, LOGICAL_H - fh - 4, "HIT ANY KEY...", 15);
    video_present(v);
    input_wait_any_key(&g->input);
}

/* ── Command: View Money (shop_finances) ── */

static void cmd_view_money(Game *g, Character *p) {
    Video *v = &g->video;
    int fh = v->font_char_h + 2;
    char line[128];

    video_clear(v, 0);
    int y = 4;

    video_draw_text(v, 8, y, "YOUR FINANCIAL STATEMENT:", 4);
    y += fh + 4;

    snprintf(line, sizeof(line), "COPPER STONES:   %10u", p->copper_stones);
    video_draw_text(v, 8, y, line, 7); y += fh;

    snprintf(line, sizeof(line), "SILVER STONES:   %10u", p->silver_stones);
    video_draw_text(v, 8, y, line, 7); y += fh;

    snprintf(line, sizeof(line), "IVORY STONES:    %10u", p->ivory_stones);
    video_draw_text(v, 8, y, line, 7); y += fh;

    snprintf(line, sizeof(line), "GOLD STONES:     %10u", p->gold_stones);
    video_draw_text(v, 8, y, line, 7); y += fh;

    snprintf(line, sizeof(line), "PLATINUM STONES: %10u", p->platinum_stones);
    video_draw_text(v, 8, y, line, 7); y += fh;

    snprintf(line, sizeof(line), "JEWEL STONES:    %10u", p->jewel_stones);
    video_draw_text(v, 8, y, line, 7); y += fh + 2;

    snprintf(line, sizeof(line), "JEWELS IN POCKET:%10u", p->jewels_pocket);
    video_draw_text(v, 8, y, line, 7); y += fh;

    snprintf(line, sizeof(line), "JEWELS IN BANK:  %10u", p->jewels_bank);
    video_draw_text(v, 8, y, line, 7);

    video_draw_text(v, 8, LOGICAL_H - fh - 4, "HIT ANY KEY...", 15);
    video_present(v);
    input_wait_any_key(&g->input);
}

/* ── Spell name tables (extracted from WORLD.EXE pointer tables) ── */
/* 4 spell categories, 30 spells each, 3 per level (level = index/3 + 1) */

static const char *perm_names[30] = {
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
};
static const char *prep_names[30] = {
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
};
static const char *wiz_names[30] = {
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
};
static const char *priest_names[30] = {
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
};

static const char **spell_type_names[4] = { perm_names, prep_names, wiz_names, priest_names };
static const char *type_headers[4] = {
    "PERMANENT SPELLS", "PREPARATION SPELLS",
    "WIZARD BATTLE SPELLS", "PRIEST BATTLE SPELLS"
};

/* Color per level: cycles through 6,8,3,4,5,7,6,8,3,4 */
static const u8 level_colors[10] = { 6, 8, 3, 4, 5, 7, 6, 8, 3, 4 };

/* ── Command: Pockets - Spell list display (2 pages) ── */
/* Shows spells/scrolls/wands/papers with names, 2 types per page */
/* is_wand: if true, show charge count next to name instead of just color */

static void cmd_pockets_spells(Game *g, Character *p, u8 data[4][45], int is_wand, const char *title) {
    Video *v = &g->video;
    int fh = v->font_char_h * 3 / 4;
    int fa = v->font_advance * 3 / 4;
    char line[80];

    for (int page = 0; page < 2; page++) {
        int t0 = page * 2;
        int t1 = t0 + 1;
        const char **names0 = spell_type_names[t0];
        const char **names1 = spell_type_names[t1];

        video_clear(v, 0);

        int header_h = fh + 2;
        int avail = LOGICAL_H - header_h * 2 - fh - 4;
        int row_h = avail / 30;
        if (row_h < fh) row_h = fh;

        int y = 0;
        int col_level = 2;
        int col_left = 6 * fa;
        int col_right = 34 * fa;

        video_draw_text_scaled(v, col_level, y, "LEVEL", 4, 3, 4);
        video_draw_text_scaled(v, col_left, y, type_headers[t0], 4, 3, 4);
        video_draw_text_scaled(v, col_right, y, type_headers[t1], 4, 3, 4);
        y += header_h;

        for (int i = 0; i < 30; i++) {
            int lv = i / 3 + 1;
            u8 color = level_colors[lv - 1];

            snprintf(line, sizeof(line), "%2d", lv);
            video_draw_text_scaled(v, col_level + fa, y, line, 10, 3, 4);

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

        snprintf(line, sizeof(line), "%s PAGE %d/2 - HIT ANY KEY...", title, page + 1);
        video_draw_text_scaled(v, col_level, LOGICAL_H - fh - 2, line, 15, 3, 4);
        video_present(v);
        input_wait_any_key(&g->input);
    }
}

/* ── Command: Pockets - Misc Magic Items subpage (option 5) ── */

static void cmd_pockets_misc(Game *g, Character *p) {
    Video *v = &g->video;
    int fh = v->font_char_h + 2;
    char line[128];

    video_clear(v, 0);
    int y = 4;

    video_draw_text(v, 8, y, "MISC. MAGIC ITEMS:", 14);
    y += fh + 2;

    video_draw_text(v, 8, y, "HIT 'I' AND '5' TO USE THESE:", 14);
    y += fh;
    snprintf(line, sizeof(line), "1) HOLY HAND GRENADES: %d", p->holy_grenade);
    video_draw_text(v, 8, y, line, 7); y += fh;
    snprintf(line, sizeof(line), "2) STONES OF TELEPORTATION: %d", p->stone_teleport);
    video_draw_text(v, 8, y, line, 7); y += fh;
    snprintf(line, sizeof(line), "3) STONES OF SEEING: %d", p->stone_see);
    video_draw_text(v, 8, y, line, 7); y += fh;
    snprintf(line, sizeof(line), "4) FLOOR SLOSHERS: %d", p->floor_slosher);
    video_draw_text(v, 8, y, line, 7); y += fh;
    snprintf(line, sizeof(line), "5) POTION OF HEALING: %d", p->potion_heal);
    video_draw_text(v, 8, y, line, 7); y += fh + 2;

    video_draw_text(v, 8, y, "HIT 'I' AND '4' TO USE THESE:", 14);
    y += fh;
    snprintf(line, sizeof(line), "6) GREEN PILLS: %d", p->green_pill);
    video_draw_text(v, 8, y, line, 7); y += fh;
    snprintf(line, sizeof(line), "7) ORANGE PILLS: %d", p->orange_pill);
    video_draw_text(v, 8, y, line, 7); y += fh;
    snprintf(line, sizeof(line), "8) YELLOW PILLS: %d", p->yellow_pill);
    video_draw_text(v, 8, y, line, 7); y += fh;
    snprintf(line, sizeof(line), "9) RED PILLS: %d", p->red_pill);
    video_draw_text(v, 8, y, line, 7); y += fh;
    snprintf(line, sizeof(line), "10) BLUE PILLS: %d", p->blue_pill);
    video_draw_text(v, 8, y, line, 7); y += fh;
    snprintf(line, sizeof(line), "11) WHITE PILLS: %d", p->white_pill);
    video_draw_text(v, 8, y, line, 7); y += fh + 2;

    video_draw_text(v, 8, y, "THESE ARE AUTOMATICALLY IN USE:", 14);
    y += fh;
    snprintf(line, sizeof(line), "12) RINGS OF REGENERATION: %d", p->ring_regen);
    video_draw_text(v, 8, y, line, 7); y += fh;
    snprintf(line, sizeof(line), "13) RING OF PROTECTION, PLUS %d", p->ring_prot_plus);
    video_draw_text(v, 8, y, line, 7); y += fh;
    snprintf(line, sizeof(line), "14) ANTI MAGIC RING, PLUS %d", p->antimagic_ring);
    video_draw_text(v, 8, y, line, 7); y += fh;
    snprintf(line, sizeof(line), "15) BODY ARMOR, LEVEL %d", p->body_armor_plus);
    video_draw_text(v, 8, y, line, 7); y += fh;
    snprintf(line, sizeof(line), "16) GAUNTLET, PLUS %d", p->gauntlet);
    video_draw_text(v, 8, y, line, 7);

    video_draw_text(v, 8, LOGICAL_H - fh - 4, "HIT ANY KEY...", 15);
    video_present(v);
    input_wait_any_key(&g->input);
}

/* ── Command: Pockets main menu (func_0DDAA) ── */

static void cmd_pockets(Game *g, Character *p) {
    Video *v = &g->video;
    int fh = v->font_char_h + 2;

    video_clear(v, 0);
    int y = 4;

    video_draw_text(v, 8, y, "WHICH DO YOU WISH TO SEE?", 14);
    y += fh + 4;
    video_draw_text(v, 8, y, "1) SPELLBOOKS", 7); y += fh;
    video_draw_text(v, 8, y, "2) SCROLLS", 7); y += fh;
    video_draw_text(v, 8, y, "3) WANDS", 7); y += fh;
    video_draw_text(v, 8, y, "4) PAPERS", 7); y += fh;
    video_draw_text(v, 8, y, "5) MISC. MAGIC ITEMS", 7); y += fh + 4;
    video_draw_text(v, 8, y, "ANY OTHER KEY TO RETURNS...", 7);
    video_present(v);

    int key = input_getch(&g->input);
    switch (key) {
        case '1': cmd_pockets_spells(g, p, p->spells, 0, "SPELLBOOKS"); break;
        case '2': cmd_pockets_spells(g, p, p->scrolls, 0, "SCROLLS"); break;
        case '3': cmd_pockets_spells(g, p, p->wands, 1, "WANDS"); break;
        case '4': cmd_pockets_spells(g, p, p->papers, 0, "PAPERS"); break;
        case '5': cmd_pockets_misc(g, p); break;
    }
}

/* ── Command: Experience Needed ── */

static void cmd_exp_needed(Game *g, Character *p) {
    Video *v = &g->video;
    int fh = v->font_char_h + 2;
    char line[128];

    video_clear(v, 0);
    int y = 4;

    snprintf(line, sizeof(line), "EXPERIENCE NEEDED FOR LEVEL: %d",
             p->level + 1);
    video_draw_text(v, 8, y, line, 3);
    y += fh + 4;

    snprintf(line, sizeof(line), "CURRENT LEVEL: %d", p->level);
    video_draw_text(v, 8, y, line, 7);
    y += fh;

    snprintf(line, sizeof(line), "CURRENT EXPERIENCE: %.0f", p->experience);
    video_draw_text(v, 8, y, line, 7);
    y += fh;

    double target = experience_for_level((int)p->level + 1);
    snprintf(line, sizeof(line), "EXPERIENCE REQUIRED: %.0f", target);
    video_draw_text(v, 8, y, line, 7);
    y += fh;

    double needed = target - p->experience;
    if (needed < 0.0) needed = 0.0;
    snprintf(line, sizeof(line), "EXPERIENCE STILL NEEDED: %.0f", needed);
    video_draw_text(v, 8, y, line, needed <= 0.0 ? 10 : 7);
    y += fh;

    snprintf(line, sizeof(line), "HEALTH POINTS: %d OF %d",
             p->hp_cur, p->hp_max);
    video_draw_text(v, 8, y, line, 7);
    y += fh;

    snprintf(line, sizeof(line), "SPELL POINTS: %.0f OF %.0f",
             p->sp_cur, p->sp_max);
    video_draw_text(v, 8, y, line, 7);

    video_draw_text(v, 8, LOGICAL_H - fh - 4, "HIT ANY KEY...", 15);
    video_present(v);
    input_wait_any_key(&g->input);
}

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

static int town_pane_text(Game *g, int y, const char *text, u8 color) {
    enum { MAX_CHARS = 32 };
    const int row_h = SY(38);
    const int bottom = SY(0x1AE);
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
    int y = 0;
    town_pane_begin(g, p);
    y = town_pane_text(g, y, title, color);
    if (subtitle && *subtitle) y = town_pane_text(g, y, subtitle, 7);
    if (money != UINT32_MAX) {
        snprintf(line, sizeof(line), "MONEY ON HAND: %u JP", money);
        y = town_pane_text(g, y, line, 8);
    }
    for (int i = 0; i < item_count; i++) {
        snprintf(line, sizeof(line), "%d) %s", i + 1, items[i]);
        y = town_pane_text(g, y, line,
                           (u8)(i == item_count - 1 ? 8 : 7));
    }
    town_pane_text(g, y, "SELECT OPTION (ESC LEAVES)", 15);
    video_present(v);
    for (;;) {
        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input) || key == 0x1B) return -1;
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
        video_present(v);

        int key = input_getch(&g->input);
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
        town_message(g, p, "SORRY, CAN'T BUY ON CREDIT HERE.",
                     "YOU DO NOT HAVE ENOUGH JEWEL PIECES.", "", 12);
        return 0;
    }
    p->jewels_pocket -= price;
    return 1;
}

/* Original L command (func_0C366): discard armor, weapons, or one complete
 * denomination of carried money.  Negative enchant bytes are cursed items;
 * when equipped they cannot be removed or dropped. */
static void cmd_drop_item(Game *g, Character *p) {
    static const char *const categories[] = {
        "ARMOR", "WEAPON", "MONEY", "RETURN TO GAME"
    };
    int category = town_menu(g, p, "WHICH TYPE OF ITEM WOULD YOU LIKE TO DROP?",
                             "", categories, 4, 14, UINT32_MAX);
    if (category < 0 || category == 3) return;

    if (category == 0 || category == 1) {
        char labels[8][64];
        const char *items[8];
        for (int i = 0; i < 8; i++) {
            int count = category == 0 ? p->armor_inventory[i]
                                      : p->weapon_inventory[i];
            int enchant = category == 0 ? (s8)p->armor_enchant[i]
                                        : (s8)p->eq_wep_enchant[i];
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
        u8 *enchant = category == 0 ? p->armor_enchant
                                    : p->eq_wep_enchant;
        int equipped = category == 0 ? p->equipped_armor
                                     : p->equipped_weapon;
        if (!inventory[selected]) return;
        if (equipped == selected && (s8)enchant[selected] < 0) {
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

static int dig_hole_target(Game *g) {
    int last = g->cur_floor + 40;
    if (last > 180) last = 180;
    for (int floor = g->cur_floor + 1; floor <= last; floor++)
        if (!rock_cell_at(g, g->cur_x, g->cur_y, floor)) return floor;
    return -1;
}

/* Original D command (func_0EDAD).  Digging is deliberately slow in game
 * turns, records the shaft in the same persistent .DUN bit grid as used
 * pitfalls, and retains the original solid-rock cutoff below level 120. */
static int cmd_dig_hole(Game *g, Character *p) {
    if (g->cur_floor <= 0 || g->cur_floor > 120 ||
        game_trapdoor_floor(g, g->cur_x, g->cur_y) >= 0) {
        town_message(g, p, "THE FLOOR SEEMS TO BE MADE OF SOLID ROCK.",
                     "IT IS NOT POSSIBLE TO DIG HERE.",
                     "A LITTLE MOUSE SUGGESTS A LADDER OR SPELL.", 4);
        return 0;
    }

    static const char *const choices[] = {
        "DIG A HOLE IN THE FLOOR", "FORGET THE HOLE IDEA"
    };
    if (town_menu(g, p, "DO YOU WISH TO DIG A HOLE IN THE FLOOR?",
                  "IT MAY TAKE SOME TIME.", choices, 2, 14,
                  UINT32_MAX) != 0)
        return 0;

    int target = dig_hole_target(g);
    if (target < 0) {
        town_message(g, p, "DIGGING... DIGGING...",
                     "THE ROCK BELOW WILL NOT GIVE WAY.", "", 4);
        return 0;
    }

    int old_x = g->cur_x, old_y = g->cur_y;
    set_pit_bit(g, old_x, old_y);
    for (int turn = 0; turn < 4 && p->hp_cur > 0; turn++)
        game_advance_monsters(g, p);
    if (p->hp_cur == 0) return 1;

    char line[96];
    snprintf(line, sizeof(line), "THE HOLE OPENS ON LEVEL %d.", target);
    game_change_floor(g, p, target);
    /* Normally the shaft lands at the same coordinate.  game_change_floor()
     * may relocate if a cached monster occupies that landing; preserve that
     * safety relocation instead of putting the player inside an actor. */
    p->x_pos = (u16)g->cur_x;
    p->y_pos = (u16)g->cur_y;
    town_message(g, p, "DIGGING... DIGGING...", line,
                 "YOUR HANDS ARE RAW AND BLISTERED.", 4);
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
    unsigned long long price = (unsigned long long)p->level * 500u +
                               (u32)(game_rand(g) % 20);
    if (price < 100000u) price = 100000u;
    if (price > 500000u) price = 500000u;
    return (u32)price;
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
            p->hp_cur = (u16)((p->hp_cur + heal > p->hp_max) ?
                              p->hp_max : p->hp_cur + heal);
            snprintf(result, sizeof(result), "THE TEMPLE RESTORES %d HEALTH POINTS.", heal);
        } else if (choice == 1) {
            int heal = 10;
            for (int i = 0; i < 5; i++) heal += game_rand(g) % 15;
            p->hp_cur = (u16)((p->hp_cur + heal > p->hp_max) ?
                              p->hp_max : p->hp_cur + heal);
            snprintf(result, sizeof(result), "THE TEMPLE RESTORES %d HEALTH POINTS.", heal);
        } else if (choice == 2) {
            p->hp_cur = p->hp_max;
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

static int inn_apply_levels(Game *g, Character *p) {
    int gained = 0;
    if (!isfinite(p->experience) || p->experience < 0.0) p->experience = 0.0;
    while (p->level < 1000 &&
           p->experience >= experience_for_level((int)p->level + 1)) {
        int hp_gain = 1 + p->stat_con / 5 +
                      game_rand(g) % (p->stat_con / 4 + 1);
        unsigned hp = (unsigned)p->hp_max + (unsigned)hp_gain;
        p->hp_max = (u16)(hp > 0xFFFFu ? 0xFFFFu : hp);
        if (p->class_id != CLASS_FIGHTER) {
            float sp_gain = (float)(1 + (p->stat_int + p->stat_wis) / 10);
            p->sp_max += sp_gain;
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
    p->age += 8u * 3600u;
    character_clear_town_effects(p);
    int gained = inn_apply_levels(g, p);
    p->hp_cur = p->hp_max;
    p->sp_cur = p->sp_max;
    if (gained) {
        char line[96];
        snprintf(line, sizeof(line), "YOU GAINED %d LEVEL%s AND ARE NOW LEVEL %u.",
                 gained, gained == 1 ? "" : "S", p->level);
        town_message(g, p, "CONGRATULATIONS! YOU HAVE BECOME MORE POWERFUL.",
                     line, "YOUR HEALTH AND SPELL POINTS ARE RESTORED.", 6);
    } else {
        town_message(g, p, "YOU REST FOR THE NIGHT.",
                     "YOUR HEALTH AND SPELL POINTS ARE RESTORED.",
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
    if (choice == 0)
        town_message(g, p, "WILDERNESS EXPLORATION",
                     "THE WILDERNESS EXIT IS READY.",
                     "EXPLORATION ITSELF IS NOT IMPLEMENTED YET.", 7);
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

static void grant_quest_reward(Character *p, const CombatState *cs,
                               char *out, size_t out_size) {
    static const int floor_needed[8] = {4, 8, 12, 16, 125, 150, 175, 200};
    int step = cs->monster_type_idx - 104;
    if (step < 0 || step >= 8 || cs->monster_level < 1 ||
        (int)p->floor_depth != floor_needed[step] ||
        (p->quest_flags & (1u << step))) return;
    p->quest_flags |= (u8)(1u << step);
    switch (step) {
    case 0: p->body_armor_plus = 9; snprintf(out, out_size, "QUEST: PLUS 9 BODY ARMOR!"); break;
    case 1: p->gauntlet = 12; snprintf(out, out_size, "QUEST: PLUS 12 GAUNTLET!"); break;
    case 2: p->ring_prot_plus = 15; snprintf(out, out_size, "QUEST: PLUS 15 RING OF PROTECTION!"); break;
    case 3:
        p->eq_wep_enchant[p->equipped_weapon < 12 ? p->equipped_weapon : 0] = 25;
        snprintf(out, out_size, "QUEST: YOUR EQUIPPED WEAPON IS NOW PLUS 25!");
        break;
    case 4: p->body_armor_plus = 25; snprintf(out, out_size, "QUEST: PLUS 25 BODY ARMOR!"); break;
    case 5: p->gauntlet = 50; snprintf(out, out_size, "QUEST: PLUS 50 GAUNTLET!"); break;
    case 6: p->ring_prot_plus = 50; snprintf(out, out_size, "QUEST: PLUS 50 RING OF PROTECTION!"); break;
    case 7:
        p->eq_wep_enchant[p->equipped_weapon < 12 ? p->equipped_weapon : 0] = 100;
        snprintf(out, out_size, "QUEST: YOUR EQUIPPED WEAPON IS NOW PLUS 100!");
        break;
    }
}

static int award_random_loot(Game *g, Character *p, int depth,
                             char *loot, size_t loot_size) {
    int kind = game_rand(g) % 7;
    if (kind < 3) {
        int max_spell_level = 1 + depth / 20;
        if (max_spell_level > 10) max_spell_level = 10;
        int cat = game_rand(g) % 4;
        int spell = game_rand(g) % (max_spell_level * 3);
        if (spell > 29) spell = 29;
        if (kind == 0) {
            inc_u8_sat(&p->scrolls[cat][spell], 1);
            snprintf(loot, loot_size, "YOU FIND A SCROLL OF %s!",
                     spell_type_names[cat][spell]);
        } else if (kind == 1) {
            int charges = 2 + game_rand(g) % 5;
            inc_u8_sat(&p->wands[cat][spell], charges);
            snprintf(loot, loot_size, "YOU FIND A %d-CHARGE WAND OF %s!",
                     charges, spell_type_names[cat][spell]);
        } else {
            inc_u8_sat(&p->papers[cat][spell], 1);
            snprintf(loot, loot_size, "YOU FIND A PAPER OF %s!",
                     spell_type_names[cat][spell]);
        }
    } else if (kind == 3) {
        switch (game_rand(g) % 12) {
        case 0: inc_u8_sat(&p->holy_grenade, 1); snprintf(loot,loot_size,"YOU FIND A HOLY HAND GRENADE!"); break;
        case 1: inc_u8_sat(&p->stone_teleport, 1); snprintf(loot,loot_size,"YOU FIND A STONE OF TELEPORTATION!"); break;
        case 2: inc_u8_sat(&p->stone_see, 1); snprintf(loot,loot_size,"YOU FIND A STONE OF SEEING!"); break;
        case 3: if (!p->floor_slosher) p->floor_slosher=1; snprintf(loot,loot_size,"YOU FIND A FLOOR SLOSHER!"); break;
        case 4: inc_u8_sat(&p->potion_heal, 1); snprintf(loot,loot_size,"YOU FIND A POTION OF HEALING!"); break;
        case 5: inc_u8_sat(&p->ring_regen, 1); snprintf(loot,loot_size,"YOU FIND A RING OF REGENERATION!"); break;
        case 6: p->stat_str++; snprintf(loot,loot_size,"YOU FIND A BOOK OF STRENGTH!"); break;
        case 7: p->stat_int++; snprintf(loot,loot_size,"YOU FIND A BOOK OF INTELLIGENCE!"); break;
        case 8: p->stat_wis++; snprintf(loot,loot_size,"YOU FIND A BOOK OF WISDOM!"); break;
        case 9: p->stat_con++; snprintf(loot,loot_size,"YOU FIND A BOOK OF CONSTITUTION!"); break;
        case 10:p->stat_agi++; snprintf(loot,loot_size,"YOU FIND A BOOK OF AGILITY!"); break;
        default:p->stat_luck++;snprintf(loot,loot_size,"YOU FIND A BOOK OF LUCK!"); break;
        }
    } else if (kind == 4) {
        u8 *pills[6] = {&p->green_pill,&p->orange_pill,&p->yellow_pill,
                        &p->red_pill,&p->blue_pill,&p->white_pill};
        static const char *pill_names[6] = {"GREEN","ORANGE","YELLOW","RED","BLUE","WHITE"};
        int pill = game_rand(g) % 6;
        inc_u8_sat(pills[pill], 1);
        snprintf(loot, loot_size, "YOU FIND A %s PILL!", pill_names[pill]);
    } else if (kind == 5) {
        int max_weapon = 1 + depth / 25;
        if (max_weapon > 7) max_weapon = 7;
        int weapon = 1 + game_rand(g) % max_weapon;
        inc_u8_sat(&p->weapon_inventory[weapon], 1);
        snprintf(loot, loot_size, "YOU FIND AND TAKE A %s!", weapon_names[weapon]);
    } else {
        int max_armor = 1 + depth / 35;
        if (max_armor > 6) max_armor = 6;
        int armor = 1 + game_rand(g) % max_armor;
        inc_u8_sat(&p->armor_inventory[armor], 1);
        snprintf(loot, loot_size, "YOU FIND AND TAKE %s ARMOR!", armor_names[armor]);
    }
    return kind;
}

static void grant_battle_rewards(Game *g, Character *p, const CombatState *cs) {
    const MonsterType *mt = &monster_types[cs->monster_type_idx];
    int xp = cs->monster_level * mt->hpF + mt->atk + mt->def;
    if (xp < 1) xp = 1;
    if (!isfinite(p->experience) || p->experience < 0.0) p->experience = 0.0;
    p->experience += (double)xp;

    int depth = g->cur_floor > cs->monster_level ? g->cur_floor : cs->monster_level;
    int amount = 1 + game_rand(g) % (depth / 2 + mt->hpF / 2 + 2);
    const char *stone_name;
    if (depth < 10) { add_u32_sat(&p->copper_stones, amount); stone_name = "COPPER STONES"; }
    else if (depth < 30) { add_u32_sat(&p->silver_stones, amount); stone_name = "SILVER STONES"; }
    else if (depth < 65) { add_u32_sat(&p->ivory_stones, amount); stone_name = "IVORY STONES"; }
    else if (depth < 110) { add_u32_sat(&p->gold_stones, amount); stone_name = "GOLD STONES"; }
    else if (depth < 170) { add_u32_sat(&p->platinum_stones, amount); stone_name = "PLATINUM STONES"; }
    else { add_u32_sat(&p->jewel_stones, amount); stone_name = "JEWEL STONES"; }

    char loot[128] = "NO EXTRA TREASURE THIS TIME.";
    int key_index = g->cur_floor / 10;
    if (combat_monster_drain_amount(cs->monster_type_idx) > 0 &&
        g->cur_floor >= 10 && g->cur_floor < 180 &&
        key_index > 0 && key_index < 18 && !p->trapdoor_keys[key_index]) {
        p->trapdoor_keys[key_index] = 1;
        snprintf(loot, sizeof(loot),
                 "YOU FOUND THE KEY LABELED %d!", key_index * 10);
    } else {
        int chance = 12 + (depth > 100 ? 50 : depth / 2);
        if (chance > 70) chance = 70;
        if (game_rand(g) % 100 < chance)
            award_random_loot(g, p, depth, loot, sizeof(loot));
    }

    char quest[128] = "";
    p->floor_depth = (u16)g->cur_floor;
    grant_quest_reward(p, cs, quest, sizeof(quest));

    char line[128];
    int y = 0;
    town_pane_begin(g, p);
    y = town_pane_text(g, y, "YOU KILLED IT!", 8);
    snprintf(line, sizeof(line), "YOU GAIN %d EXPERIENCE POINTS.", xp);
    y = town_pane_text(g, y, line, 7);
    snprintf(line, sizeof(line), "YOU TAKE %d %s.", amount, stone_name);
    y = town_pane_text(g, y, line, 7);
    y = town_pane_text(g, y, loot, 14);
    if (*quest) y = town_pane_text(g, y, quest, 10);
    town_pane_text(g, y, "HIT ANY KEY...", 15);
    video_present(&g->video);
    input_wait_any_key(&g->input);
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
    for (int i = 0; i < 4096 && seen != 0x7F; i++)
        seen |= 1 << award_random_loot(&g, &p, 200, loot, sizeof(loot));
    if (seen != 0x7F) failures++;

    Character leveler = {0};
    leveler.level = 1;
    leveler.experience = 32.0;
    leveler.hp_max = 10;
    leveler.stat_con = leveler.stat_int = leveler.stat_wis = 10;
    if (inn_apply_levels(&g, &leveler) != 1 || leveler.level != 2 ||
        experience_for_level(3) != 243.0) failures++;

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
    failures += character_creation_self_test();

    printf("Economy/reward/creation self-test: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

/* ── Beastiary ───────────────────────────────────────────────────────
 *
 * J was unused by the DOS command set (and reads naturally as monster
 * journal), so it opens a persistent record of all 112 definitions. */

static void bestiary_floor_text(int type, char *out, size_t out_size) {
    static const int quest_floor[8] = {4, 8, 12, 16, 125, 150, 175, 200};
    const MonsterType *mt = &monster_types[type];
    if (type >= 104) {
        snprintf(out, out_size, "FLOOR: %d (QUEST)", quest_floor[type - 104]);
        return;
    }
    int min_floor = mt->minL < 1 ? 1 : mt->minL;
    int max_floor = mt->maxL > 120 ? 200 : mt->maxL;
    if (min_floor > max_floor)
        snprintf(out, out_size, "FLOORS: NOT IN RANDOM SPAWNS");
    else if (min_floor == max_floor)
        snprintf(out, out_size, "FLOOR: %d", min_floor);
    else
        snprintf(out, out_size, "FLOORS: %d-%d", min_floor, max_floor);
}

static void draw_bestiary_fullscreen(Game *g, int type) {
    Video *v = &g->video;
    const MonsterType *mt = &monster_types[type];
    int adv = v->font_advance ? v->font_advance : v->font_char_w;
    int title_x = (LOGICAL_W - (int)strlen(mt->name) * adv) / 2;
    int pic = get_monster_pic_index_ext(type);
    int substitute = pic < 2;
    if (substitute) pic = 2;

    video_clear(v, 0);
    video_draw_text(v, title_x > 4 ? title_x : 4, 5, mt->name, 14);
    draw_pic_billboard(g, pic, LOGICAL_W / 2,
                       42, LOGICAL_H - 104, 0.0f,
                       0, 0, LOGICAL_W, LOGICAL_H, NULL,
                       get_monster_color_ext(type));
    if (substitute)
        video_draw_text_scaled(v, 12, LOGICAL_H - 62,
                               "SUBSTITUTE ART - SOURCE SLOT IS ABSENT IN 1024X768 WORLD.PIC",
                               8, 3, 4);
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
    char line[160];

    for (int i = 0; i < MONSTER_TYPE_COUNT; i++)
        if (g->bestiary_kills[i]) discovered++;

    video_clear(v, 0);
    video_draw_text(v, 10, 4, "BEASTIARY", 4);
    snprintf(line, sizeof(line), "DISCOVERED: %d OF %d    PAGE %d OF %d",
             discovered, MONSTER_TYPE_COUNT, page + 1,
             (MONSTER_TYPE_COUNT + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);
    video_draw_text_scaled(v, 205, 9, line, 8, 3, 4);
    video_hline(v, 4, 41, LOGICAL_W - 8, 8);
    video_vline(v, LIST_RIGHT, 42, LOGICAL_H - 84, 8);

    for (int row = 0; row < ROWS_PER_PAGE; row++) {
        int type = first + row;
        if (type >= MONSTER_TYPE_COUNT) break;
        int y = 53 + row * 35;
        int unlocked = g->bestiary_kills[type] != 0;
        if (type == selected)
            video_fill_rect(v, 7, y - 3, LIST_RIGHT - 15, 31, 1);
        if (unlocked)
            snprintf(line, sizeof(line), "%03d  %s", type + 1,
                     monster_types[type].name);
        else
            snprintf(line, sizeof(line), "%03d  ????", type + 1);
        video_draw_text_scaled(v, 13, y, line,
                               type == selected ? 15 : (unlocked ? 7 : 8),
                               3, 4);
    }

    int unlocked = g->bestiary_kills[selected] != 0;
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
        const MonsterType *mt = &monster_types[selected];
        int pic = get_monster_pic_index_ext(selected);
        int substitute = pic < 2;
        if (substitute) pic = 2;
        video_draw_text(v, 430, 48, mt->name, 14);
        draw_pic_billboard(g, pic, 720, 80, 300, 0.0f,
                           LIST_RIGHT + 1, 42, LOGICAL_W - LIST_RIGHT - 1,
                           350, NULL, get_monster_color_ext(selected));
        if (substitute)
            video_draw_text_scaled(v, 430, 353,
                                   "SUBSTITUTE ART (SOURCE SLOT NOT IN THIS PIC SET)",
                                   8, 3, 4);

        int y = 390;
        bestiary_floor_text(selected, line, sizeof(line));
        video_draw_text_scaled(v, 430, y, line, 10, 3, 4); y += 32;
        snprintf(line, sizeof(line), "KILLED: %u", g->bestiary_kills[selected]);
        video_draw_text_scaled(v, 430, y, line, 15, 3, 4); y += 32;
        snprintf(line, sizeof(line), "DEFENSE: %d       ATTACK: %d",
                 mt->def, mt->atk);
        video_draw_text_scaled(v, 430, y, line, 7, 3, 4); y += 32;
        snprintf(line, sizeof(line), "DAMAGE: %d        AGILITY: %d",
                 mt->dmg, mt->agi);
        video_draw_text_scaled(v, 430, y, line, 7, 3, 4); y += 32;
        snprintf(line, sizeof(line), "DEFENSE MOD: %d   HP FACTOR: %d",
                 mt->defMod, mt->hpF);
        video_draw_text_scaled(v, 430, y, line, 7, 3, 4); y += 32;
        snprintf(line, sizeof(line), "IMMUNITY: %u      SAVES: %u / %u",
                 mt->imm, mt->saveA, mt->saveB);
        video_draw_text_scaled(v, 430, y, line, 7, 3, 4); y += 32;
        snprintf(line, sizeof(line), "BOSS: %s", mt->boss ? "YES" : "NO");
        video_draw_text_scaled(v, 430, y, line, mt->boss ? 12 : 7, 3, 4);
        video_draw_text_scaled(v, 430, 636,
                               "HP: 1 TO (HP FACTOR X MONSTER LEVEL)", 8,
                               3, 4);
        if (mt->boss)
            video_draw_text_scaled(v, 430, 663,
                                   "BOSS BONUS: +20 HP X MONSTER LEVEL", 8,
                                   3, 4);
    }

    video_hline(v, 4, LOGICAL_H - 42, LOGICAL_W - 8, 8);
    video_draw_text_scaled(v, 10, LOGICAL_H - 34,
                           "UP/DOWN SELECT  LEFT/RIGHT OR PGUP/PGDN PAGE  F FULLSCREEN  ESC RETURN",
                           15, 3, 4);
}

void game_draw_bestiary_test(Game *g, int selected) {
    if (selected < 0) selected = 0;
    if (selected >= MONSTER_TYPE_COUNT) selected = MONSTER_TYPE_COUNT - 1;
    draw_bestiary_page(g, selected);
}

static int bestiary_move_selection(int selected, int scan) {
    switch (scan) {
    case 0x48:
        return selected > 0 ? selected - 1 : MONSTER_TYPE_COUNT - 1;
    case 0x50:
        return selected + 1 < MONSTER_TYPE_COUNT ? selected + 1 : 0;
    case 0x49:
    case 0x4B:
        return selected >= 18 ? selected - 18 : MONSTER_TYPE_COUNT - 1;
    case 0x51:
    case 0x4D:
        return selected + 18 < MONSTER_TYPE_COUNT ? selected + 18 : 0;
    case 0x47:
        return 0;
    case 0x4F:
        return MONSTER_TYPE_COUNT - 1;
    default:
        return selected;
    }
}

int game_ui_self_test(Game *g) {
    static const int expected[12][2] = {
        {'b','m'}, {'w','v'}, {'z','c'}, {'i','x'},
        {'a','l'}, {'f','p'}, {'t','e'}, {'o','o'},
        {'j','j'}, {'1','1'}, {'2','2'}, {'q','h'}
    };
    int failures = 0;
    if (bestiary_move_selection(0, 0x48) != MONSTER_TYPE_COUNT - 1) failures++;
    if (bestiary_move_selection(MONSTER_TYPE_COUNT - 1, 0x50) != 0) failures++;
    if (bestiary_move_selection(0, 0x49) != MONSTER_TYPE_COUNT - 1) failures++;
    if (bestiary_move_selection(MONSTER_TYPE_COUNT - 1, 0x51) != 0) failures++;
    if (bestiary_move_selection(18, 0x49) != 0) failures++;
    if (bestiary_move_selection(0, 0x51) != 18) failures++;
    if (input_sdl_to_dos(SDLK_PAGEUP, KMOD_NONE) != -0x49) failures++;
    if (input_sdl_to_dos(SDLK_PAGEDOWN, KMOD_NONE) != -0x51) failures++;
    if (input_sdl_to_dos(SDLK_KP_9, KMOD_NONE) != -0x49) failures++;
    if (input_sdl_to_dos(SDLK_KP_3, KMOD_NONE) != -0x51) failures++;

    const int menu_x = SX(0x48C);
    const int spacing = SY(35);
    int adv = g->video.font_advance ? g->video.font_advance
                                    : g->video.font_char_w;
    int scaled_advance = adv * 7 / 6;
    for (int row = 0; row < 12; row++) {
        for (int col = 0; col < 2; col++) {
            float logical_x = (float)(menu_x + scaled_advance *
                                      (col ? 12 : 2));
            float logical_y = (float)(row * spacing + 4);
            int window_x, window_y;
            SDL_RenderLogicalToWindow(g->video.renderer, logical_x, logical_y,
                                      &window_x, &window_y);
            if (command_menu_click_key(g, window_x, window_y) !=
                expected[row][col])
                failures++;
        }
    }
    return failures;
}

static void cmd_bestiary(Game *g) {
    int selected = 0;
    for (;;) {
        draw_bestiary_page(g, selected);
        video_present(&g->video);
        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input) || key == 0x1B) return;
        if ((key == 'f' || key == 'F') && g->bestiary_kills[selected]) {
            draw_bestiary_fullscreen(g, selected);
            continue;
        }
        if (key != 0) continue;
        int scan = input_getch(&g->input);
        selected = bestiary_move_selection(selected, scan);
    }
}

/* ── Command: Help screen (matches original func_26DE1 help menu) ── */

static void cmd_help(Game *g) {
    Video *v = &g->video;
    int fh = v->font_char_h + 2;

    video_clear(v, 0);
    int y = 4;

    video_draw_text(v, 8, y, "HELP MENU-HIT ESC TO RETURN TO GAME", 4);
    y += fh;
    video_draw_text(v, 8, y, "HIT LETTER OR NUMBER FOR MORE HELP", 5);
    y += fh + 4;

    video_draw_text(v, 8, y, "A-ARMOR (SELECT TYPE)", 8); y += fh;
    video_draw_text(v, 8, y, "B-BRICK SPEED CHANGE (4 SETTINGS)", 8); y += fh;
    video_draw_text(v, 8, y, "C-CAST SPELL OR GET HELP ON SPELLS", 8); y += fh;
    video_draw_text(v, 8, y, "D-GO DOWN LADDER OR DIG HOLE", 8); y += fh;
    video_draw_text(v, 8, y, "E-EXPERIENCE NEEDED TO GAIN LEVEL", 8); y += fh;
    video_draw_text(v, 8, y, "F-FIGHT A MONSTER", 8); y += fh;
    video_draw_text(v, 8, y, "H-THIS HELP SCREEN", 8); y += fh;
    video_draw_text(v, 8, y, "I-USE AN ITEM", 8); y += fh;
    video_draw_text(v, 8, y, "L-LOSE (DROP) AN ITEM", 8); y += fh;
    video_draw_text(v, 8, y, "M-VIEW MONEY (FINANCIAL STATUS)", 8); y += fh;
    video_draw_text(v, 8, y, "O-TOGGLE SOUND ON AND OFF", 8); y += fh;
    video_draw_text(v, 8, y, "P-VIEW CONTENTS OF POCKETS", 8); y += fh;
    video_draw_text(v, 8, y, "Q-QUIT AND SAVE GAME", 8); y += fh;
    video_draw_text(v, 8, y, "J-BEASTIARY (MONSTER JOURNAL)", 8); y += fh;
    video_draw_text(v, 8, y, "S-SAVE AND CONTINUE PLAYING", 8); y += fh;
    video_draw_text(v, 8, y, "T-WAIT (SKIP A TURN)", 8); y += fh;
    video_draw_text(v, 8, y, "V-VIEW STATS", 8); y += fh;
    video_draw_text(v, 8, y, "W-WEAPONS (SELECT TYPE)", 8); y += fh;
    video_draw_text(v, 8, y, "X-EXPAND MAP VIEW", 8); y += fh;
    video_draw_text(v, 8, y, "Z-ZOOM (TOGGLE MAP SIZE)", 8); y += fh;
    y += 4;
    video_draw_text(v, 8, y, "ARROWS-MOVE AND TURN", 8); y += fh;
    video_draw_text(v, 8, y, "1,2-SHOW SPELL EFFECT PAGES", 8); y += fh;

    video_draw_text(v, 8, LOGICAL_H - fh - 4, "HIT ANY KEY...", 15);
    video_present(v);
    input_wait_any_key(&g->input);
}

/* ── Command: Expand Map (dungeon map viewer) ── */

static void draw_expanded_map_frame(Game *g) {
    Video *v = &g->video;
    int cs = 5;
    int ox = 8, oy = 20;

    video_clear(v, 0);
    video_draw_text(v, 8, 4, "DUNGEON MAP - HIT ANY KEY", 15);

    for (int y = 0; y < MAP_H && y * cs + oy < LOGICAL_H - cs; y++) {
        for (int x = 0; x < MAP_W && x * cs + ox < LOGICAL_W - cs; x++) {
            if (!g->visited[y][x]) continue;

            int px = ox + x * cs;
            int py = oy + y * cs;
            video_fill_rect(v, px, py, cs, cs, 0);

            int n = map_get_edge(g, x,     y,     1);
            int e = map_get_edge(g, x + 1, y,     0);
            int s = map_get_edge(g, x,     y + 1, 1);
            int w = map_get_edge(g, x,     y,     0);
            if (n != 3) video_hline(v, px, py, cs, n == 1 ? 4 : 15);
            if (s != 3) video_hline(v, px, py + cs - 1, cs, s == 1 ? 4 : 15);
            if (w != 3) video_vline(v, px, py, cs, w == 1 ? 4 : 15);
            if (e != 3) video_vline(v, px + cs - 1, py, cs, e == 1 ? 4 : 15);

            int shop = game_shop_type(g, x, y);
            int ladder = game_ladder_delta(g, x, y);
            int trap = game_trapdoor_floor(g, x, y);
            if (shop) {
                video_fill_rect(v, px + 1, py + 1, cs - 2, cs - 2,
                                (u8)(shop + 2));
            } else if (ladder < 0) {
                for (int p = 1; p < cs - 1; p++)
                    video_put_pixel(v, px + p, py + cs - 1 - p, 4);
            } else if (ladder > 0) {
                for (int p = 1; p < cs - 1; p++)
                    video_put_pixel(v, px + p, py + p, 47);
            } else if (trap >= 0) {
                for (int p = 1; p < cs - 1; p++) {
                    video_put_pixel(v, px + p, py + p, 4);
                    video_put_pixel(v, px + cs - 1 - p, py + p, 4);
                }
            } else if (g->cur_floor > 0 && pit_bit_is_set(g, x, y) &&
                       pitfall_target(g, x, y) != g->cur_floor) {
                video_fill_rect(v, px + 1, py + 1, cs - 2, cs - 2, 3);
            }

            if (x == g->cur_x && y == g->cur_y &&
                g->map_player_visible) {
                video_fill_rect(v, px + 1, py + 1, 3, 3, 15);
                video_put_pixel(v, px + 2, py + 2, 6);
            }
        }
    }

    int lx = 430;
    int ly = 30;
    int step = v->font_char_h + 5;
    video_draw_text(v, lx, ly, "MAP KEY", 15); ly += step * 2;
    video_draw_text(v, lx, ly, "STORE LADDER", 3); ly += step;
    video_draw_text(v, lx, ly, "TEMPLE LADDER", 4); ly += step;
    video_draw_text(v, lx, ly, "BANK LADDER", 5); ly += step;
    video_draw_text(v, lx, ly, "INN LADDER", 6); ly += step;
    video_draw_text(v, lx, ly, "WILDERNESS EXIT", 7); ly += step * 2;
    video_draw_text(v, lx, ly, "YELLOW /: UP", 4); ly += step;
    video_draw_text(v, lx, ly, "CYAN \\: DOWN", 47); ly += step;
    video_draw_text(v, lx, ly, "ORANGE X: TRAPDOOR", 4); ly += step;
    video_draw_text(v, lx, ly, "MAGENTA BOX: USED PIT", 3);
}

static void cmd_expand_map(Game *g) {
    u32 next_blink = SDL_GetTicks() + 350;
    g->map_player_visible = 1;
    draw_expanded_map_frame(g);
    video_present(&g->video);

    while (!input_poll_quit(&g->input) && !input_kbhit(&g->input)) {
        u32 now = SDL_GetTicks();
        if (SDL_TICKS_PASSED(now, next_blink)) {
            g->map_player_visible = !g->map_player_visible;
            next_blink = now + 350;
            draw_expanded_map_frame(g);
            video_present(&g->video);
        }
        SDL_Delay(10);
    }
    if (!input_poll_quit(&g->input))
        input_wait_any_key(&g->input);
    g->map_player_visible = 1;
}

/* ── Spells in effect display (keys '1' and '2', matches func_0C031) ── */

static void cmd_spells_in_effect(Game *g, Character *p, int page) {
    Video *v = &g->video;
    int fh = v->font_char_h + 2;
    char line[128];

    video_clear(v, 0);
    snprintf(line, sizeof(line), "SPELLS IN EFFECT - PAGE %d", page + 1);
    video_draw_text(v, 8, 4, line, 14);

    int y = 4 + fh * 2;

    if (page == 0) {
        snprintf(line, sizeof(line), "STRENGTH BONUS:  %d", p->eff_str_bonus);
        video_draw_text(v, 8, y, line, p->eff_str_bonus > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "SUPER STRENGTH:  %d", p->eff_super_str);
        video_draw_text(v, 8, y, line, p->eff_super_str > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "AGILITY BONUS:   %d", p->eff_agi_bonus);
        video_draw_text(v, 8, y, line, p->eff_agi_bonus > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "SUPER AGILITY:   %d", p->eff_super_agi);
        video_draw_text(v, 8, y, line, p->eff_super_agi > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "INVISIBLE:       %d", p->eff_invisible);
        video_draw_text(v, 8, y, line, p->eff_invisible > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "FEATHER FALL:    %d", p->eff_feather);
        video_draw_text(v, 8, y, line, p->eff_feather > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "FAST MOVE:       %d", p->eff_fast_move);
        video_draw_text(v, 8, y, line, p->eff_fast_move > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "PROTECTION:      %d (LV %d)", p->eff_protect_turns, p->eff_protect_lv);
        video_draw_text(v, 8, y, line, p->eff_protect_turns > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "BATTLE STRENGTH: %d", p->eff_battle_str);
        video_draw_text(v, 8, y, line, p->eff_battle_str > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "BATTLE SPEED:    %d", p->eff_battle_spd);
        video_draw_text(v, 8, y, line, p->eff_battle_spd > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "SLOW MONSTER:    %d", p->eff_slow_mon);
        video_draw_text(v, 8, y, line, p->eff_slow_mon > 0 ? 15 : 8); y += fh;
    } else {
        snprintf(line, sizeof(line), "DISEASE:         %d", p->diseased_turns);
        video_draw_text(v, 8, y, line, p->diseased_turns > 0 ? 6 : 15); y += fh;
        snprintf(line, sizeof(line), "POISON:          %d", p->poisoned_turns);
        video_draw_text(v, 8, y, line, p->poisoned_turns > 0 ? 6 : 15); y += fh;
        snprintf(line, sizeof(line), "RESIST POISON:   %d", p->eff_resist_poison);
        video_draw_text(v, 8, y, line, p->eff_resist_poison > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "RESIST DISEASE:  %d", p->eff_resist_disease);
        video_draw_text(v, 8, y, line, p->eff_resist_disease > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "ANTI COLD:       %d", p->eff_anti_cold);
        video_draw_text(v, 8, y, line, p->eff_anti_cold > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "ANTI FIRE:       %d", p->eff_anti_fire);
        video_draw_text(v, 8, y, line, p->eff_anti_fire > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "RESIST DRAIN:    %d", p->eff_resist_drain);
        video_draw_text(v, 8, y, line, p->eff_resist_drain > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "POWER WEAPON:    %d (%d TURNS)", p->eff_pwr_weapon, p->eff_pwr_wpn_turns);
        video_draw_text(v, 8, y, line, p->eff_pwr_weapon > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "HOLD MONSTER:    %d", p->eff_hold_monster);
        video_draw_text(v, 8, y, line, p->eff_hold_monster > 0 ? 15 : 8); y += fh;
        snprintf(line, sizeof(line), "STOP MONSTER:    %d", p->eff_stop_monster);
        video_draw_text(v, 8, y, line, p->eff_stop_monster > 0 ? 15 : 8); y += fh;
    }

    y += fh;
    video_draw_text(v, 8, y, "PRESS ANY KEY...", 7);
    video_present(v);
    input_wait_any_key(&g->input);
}

/* ── Player selection screen ── */

static int player_select_screen(Game *g) {
    Video *v = &g->video;

    video_clear(v, 0);
    video_draw_text(v, SX(0), SY(0), "PLEASE SELECT A PLAYER:", 4);
    video_draw_text(v, SX(0), SY(100),
                    "NUMBER     NAME    SEX   RACE       CLASS", 5);

    char line[96];
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g->char_exists[i]) {
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
            video_draw_text(v, SX(0), SY(155 + 55 * i), line, 6);
        } else {
            snprintf(line, sizeof(line), "%d) SELECT TO CREATE A NEW CHARACTER", i);
            video_draw_text(v, SX(0), SY(165 + 55 * i), line, 14);
        }
    }

    video_draw_text(v, SX(0), SY(1100), "HIT ESCAPE TO QUIT", 5);
    video_present(v);

    while (1) {
        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input)) return -1;
        if (key >= '0' && key <= '9') {
            return key - '0';
        }
        if (key == 0x1B) return -1;
    }
}

/* ── Original character roller (ROLL.TXT / WORLD.ASM far_19115) ── */

#define CREATION_TEXT_LINES 40
#define MW_AGE_YEAR_UNITS   0x80520u /* 525,600 minutes per year */

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
};

/* Height, weight and lifespan are the final six bytes of each fourteen-byte
 * race record at DS:0150 in WORLD.EXE. */
static const RaceBodyBase race_body_base[RACE_COUNT] = {
    { 70, 130,  65}, { 54,  80, 190}, { 48, 100, 130}, { 42,  60,  70},
    { 38,  60, 130}, {100, 400,  54}, { 24,  20, 150}, { 78, 100, 230}
};

static int creation_rand_scaled(Game *g, int range) {
    if (range <= 0) return 0;
    return (int)(((u32)game_rand(g) * (u32)range) / 0x8000u);
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

/* WORLD's far_277E7 distributes glyphs between two logical X coordinates.
 * Its name editor uses the same operation with eighteen fixed character
 * positions, while ordinary fitted text uses the string length. */
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

static int select_character_race(Game *g, const CreationText *text) {
    static const int y[12] = {0,100,150,220,320,420,520,620,720,820,920,1020};
    static const u8 color[12] = {3,4,4,5,8,8,8,8,8,8,8,8};
    video_clear(&g->video, 0);
    for (int i = 0; i < 12; i++) creation_draw(g, 0, y[i], text->line[12 + i], color[i]);
    video_present(&g->video);
    for (;;) {
        int key = creation_key(g);
        if (input_poll_quit(&g->input) || key == 0x1B) return -1;
        if (key >= '1' && key <= '8') return key - '1';
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
    creation_draw(g, 750, 100, "HEIGHT:      INCHES", 8);
    snprintf(line, sizeof(line), "%u", height);
    creation_draw(g, 1080, 100, line, 8);
    creation_draw(g, 750, 170, "WEIGHT:      POUNDS", 8);
    snprintf(line, sizeof(line), "%u", weight);
    creation_draw(g, 1080, 170, line, 8);
    creation_draw(g, 750, 240, "AGE:         YEARS", 8);
    snprintf(line, sizeof(line), "%u", (unsigned)(age / MW_AGE_YEAR_UNITS));
    creation_draw(g, 1080, 240, line, 8);
    if (show_choices) {
        creation_draw(g, 190, 700, "Y) KEEP THIS CHARACTER", 4);
        creation_draw(g, 190, 770, "N) ROLL A NEW CHARACTER", 4);
        creation_draw(g, 190, 840, "D) DESIGN YOUR OWN CHARACTER", 4);
        creation_draw(g, 190, 1100, "PLEASE SELECT ONE OF THE ABOVE", 4);
    }
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
            creation_draw_distributed(g, 0, 1100, 1000, name, 18, 4, 1, 1);
        video_present(&g->video);
        int key = input_getch(&g->input);
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

static int select_character_class(Game *g, const CreationText *text, const char *name) {
    static const int x0[16] = {
        0,0,90,0,90,0,90,0,90,0,90,0,90,90,0,90
    };
    static const int y[16] = {
        480,550,590,640,680,730,770,820,860,910,950,1000,1040,1080,1125,1165
    };
    static const u8 color[16] = {2,3,3,4,4,5,5,6,6,8,8,7,7,7,2,2};
    video_fill_rect(&g->video, 0, SY(670), LOGICAL_W, LOGICAL_H - SY(670), 0);
    creation_draw_fitted(g, 700, 880, 310, "NAME:", 8, 1, 1);
    creation_draw_fitted(g, 910, 910 + (int)strlen(name) * 36, 310,
                         name, 8, 1, 1);
    for (int i = 0; i < 16; i++) {
        /* The heading uses font 1; the explanatory rows use the smaller
           font 0 in the DOS executable. */
        int sn = i == 0 ? 1 : 3;
        int sd = i == 0 ? 1 : 4;
        creation_draw_fitted(g, x0[i], i == 0 ? 1500 : 1600, y[i],
                             text->line[24 + i], color[i], sn, sd);
    }
    video_present(&g->video);
    for (;;) {
        int key = creation_key(g);
        if (input_poll_quit(&g->input) || key == 0x1B) return -1;
        if (key >= '1' && key <= '7') return key - '1';
    }
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
    default: return (float)((intelligence * 2 + wisdom) / 12);
    }
}

static void grant_starting_spells(Character *p) {
    if (p->class_id == CLASS_MONK) {
        memset(p->spells, 1, sizeof(p->spells));
        return;
    }
    if (p->class_id == CLASS_FIGHTER) return;
    p->spells[SPELL_CAT_PREPARATION][2] = 1; /* Little Cure */
    if (p->class_id == CLASS_WIZARD || p->class_id == CLASS_SAGE ||
        p->class_id == CLASS_MAGE)
        p->spells[SPELL_CAT_WIZARD][1] = 1; /* Magic Zap */
    if (p->class_id == CLASS_WORSHIPPER || p->class_id == CLASS_PRIEST ||
        p->class_id == CLASS_SAGE)
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

    static const int expected_spells[CLASS_COUNT] = {0, 2, 180, 2, 2, 3, 2};
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
    return failures;
}

static int create_character(Game *g, Character *p) {
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
    int race = select_character_race(g, &text);
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
    int class_id = select_character_class(g, &text, name);
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

    draw_character_card(g, race, sex, stats, height, weight, age, 0);
    creation_draw_fitted(g, 700, 880, 310, "NAME:", 8, 1, 1);
    creation_draw_fitted(g, 910, 910 + (int)strlen(name) * 36, 310,
                         name, 8, 1, 1);
    creation_draw_fitted(g, 700, 920, 380, "CLASS:", 8, 1, 1);
    creation_draw(g, 980, 380, class_names[class_id], 8);
    char summary[96];
    snprintf(summary, sizeof(summary), "SPELL POINTS: %.0f    HEALTH POINTS: %u",
             p->sp_max, p->hp_max);
    creation_draw(g, 0, 460, summary, 4);
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

/* ── Monster encounter check ── */

static void record_bestiary_kill(Game *g, int monster_type) {
    if (monster_type < 0 || monster_type >= MONSTER_TYPE_COUNT) return;
    if (g->bestiary_kills[monster_type] != UINT32_MAX)
        g->bestiary_kills[monster_type]++;
    g->bestiary_dirty = 1;
    /* A crash after the reward screen must not erase a first discovery. */
    game_save_bestiary(g);
}

static void fight_monster(Game *g, Character *player, int index) {
    if (index < 0) return;
    CombatState cs;
    combat_init_entity(g, &cs, index);
    combat_run(g, &cs, player);
    if (cs.monster_hp <= 0) {
        record_bestiary_kill(g, cs.monster_type_idx);
        game_kill_monster(g, index);
        grant_battle_rewards(g, player, &cs);
    } else if (cs.fled) {
        game_kill_monster(g, index);
    } else {
        game_set_monster_hp(g, index, cs.monster_hp);
    }
    g->monster_adjacent = game_find_adjacent_monster(g) >= 0;
}

/* ── Cast spell from exploration (handles battle vs prep) ── */

static int cmd_cast_spell(Game *g, Character *player) {
    return cmd_cast_spell_menu(g, player, NULL);
}

/* ── Main game loop ── */

static void game_draw_exploration_base(Game *g, Character *player) {
    video_clear(&g->video, 0);
    draw_4way_view(g);
    draw_minimap(g, 0, SY(0x1AE), SX(0x11B), 38 * 10);
    draw_command_menu(g);
    draw_status_bar(g, player);
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

/* Original combat never changes to a separate full-screen scene.  The normal
 * four-view exploration frame stays in place, the engaged monster grows in
 * its compass pane, and messages occupy the otherwise-black upper-left pane. */
void game_draw_combat_overlay(Game *g, Character *player,
                              int entity_index, int monster_type,
                              int monster_level, int monster_hp,
                              const char *msg1, const char *msg2,
                              const char *msg3) {
    static const char *dir_name[4] = {"NORTH", "SOUTH", "WEST", "EAST"};
    const ViewLayout *layout = &view_layouts[g->view_mode % 3];
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
                           get_monster_color_ext(monster_type));
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

void game_run(Game *g) {
    Video *v = &g->video;

    /* Title screen */
    video_clear(v, 0);
    video_draw_text(v, 180, 100, "MORAFF'S WORLD", 14);
    video_draw_text(v, 130, 140, "Native Port - Work In Progress", 7);
    video_draw_text(v, 160, 200, "Press any key to continue...", 15);
    video_present(v);
    input_wait_any_key(&g->input);
    if (input_poll_quit(&g->input)) return;

    /* Player selection.  Cancelling a partially designed character returns
     * to the slot list without creating or overwriting a save. */
    int slot;
    Character *player;
    for (;;) {
        slot = player_select_screen(g);
        if (slot < 0) return;
        player = &g->chars[slot];
        if (g->char_exists[slot]) break;
        if (create_character(g, player)) {
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

    g->cur_x = player->x_pos;
    g->cur_y = player->y_pos;
    g->cur_floor = player->floor_depth;
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
    if (game_apply_pitfall(g, player)) {
        video_clear(v, 0);
        video_draw_text(v, 250, LOGICAL_H / 2, "YOU FALL THROUGH A PIT!", 12);
        video_present(v);
        SDL_Delay(900);
    }

    g->map_player_visible = 1;
    u32 next_map_blink = SDL_GetTicks() + 350;
    while (!input_poll_quit(&g->input)) {
        game_draw_exploration(g, player);

        video_present(v);

        /* WORLD keeps polling while the exploration screen is idle.  Doing
         * the same here lets its square map cursor blink without requiring
         * movement or another keypress. */
        while (!input_poll_quit(&g->input) && !input_kbhit(&g->input)) {
            u32 now = SDL_GetTicks();
            if (SDL_TICKS_PASSED(now, next_map_blink)) {
                g->map_player_visible = !g->map_player_visible;
                next_map_blink = now + 350;
                draw_minimap(g, 0, SY(0x1AE), SX(0x11B), 38 * 10);
                video_present(v);
            }
            SDL_Delay(10);
        }
        if (input_poll_quit(&g->input)) break;
        int key = input_getch(&g->input);

        if (key == INPUT_MOUSE_CLICK) {
            int mouse_x, mouse_y;
            input_last_mouse_click(&g->input, &mouse_x, &mouse_y);
            key = command_menu_click_key(g, mouse_x, mouse_y);
            if (!key) continue;
        }

        if (key == 0x1B) {
            break;
        } else if (key == 'v' || key == 'V') {
            cmd_view_stats(g, player);
        } else if (key == 'm' || key == 'M') {
            cmd_view_money(g, player);
        } else if (key == 'p' || key == 'P') {
            cmd_pockets(g, player);
        } else if (key == 'e' || key == 'E') {
            cmd_exp_needed(g, player);
        } else if (key == 'h' || key == 'H') {
            cmd_help(g);
        } else if (key == 'j' || key == 'J') {
            cmd_bestiary(g);
        } else if (key == 'x' || key == 'X') {
            cmd_expand_map(g);
        } else if (key == 'f' || key == 'F') {
            int monster = game_find_adjacent_monster(g);
            if (monster >= 0) {
                fight_monster(g, player, monster);
                if (player->hp_cur <= 0) break;
            } else {
                video_fill_rect(v, 0, LOGICAL_H / 2 - 10, LOGICAL_W, 20, 0);
                video_draw_text(v, 200, LOGICAL_H / 2 - 7, "NO MONSTER HERE!", 6);
                video_present(v);
                SDL_Delay(800);
            }
        } else if (key == 'c' || key == 'C') {
            if (cmd_cast_spell(g, player) != 0)
                game_advance_monsters(g, player);
        } else if (key == 'i' || key == 'I') {
            if (cmd_use_item(g, player, NULL) != 0)
                game_advance_monsters(g, player);
        } else if (key == 'l' || key == 'L') {
            cmd_drop_item(g, player);
        } else if (key == 'w' || key == 'W') {
            cmd_weapons(g, player);
        } else if (key == 'a' || key == 'A') {
            cmd_armor(g, player);
        } else if (key == 'b' || key == 'B') {
            g->brick_speed = (g->brick_speed + 1) & 3;
            char line[64];
            snprintf(line, sizeof(line), "BRICK SPEED SETTING %d OF 4",
                     g->brick_speed + 1);
            video_fill_rect(v, 0, 0, SX(0x2D3), SY(42), 0);
            video_draw_text(v, 8, 8, line, 4);
            video_present(v);
            SDL_Delay(450);
        } else if (key == 'o' || key == 'O') {
            g->sound_enabled = !g->sound_enabled;
        } else if (key == 'z' || key == 'Z') {
            g->view_mode = (g->view_mode + 1) % 3;
        } else if (key == '1') {
            cmd_spells_in_effect(g, player, 0);
        } else if (key == '2') {
            cmd_spells_in_effect(g, player, 1);
        } else if (key == 't' || key == 'T') {
            game_advance_monsters(g, player);
        } else if (key == 'u' || key == 'U') {
            int delta = ladder_delta(g, g->cur_x, g->cur_y);
            int shop = game_shop_type(g, g->cur_x, g->cur_y);
            if (shop) enter_town_location(g, player, shop);
            else if (delta < 0) game_change_floor(g, player, g->cur_floor + delta);
        } else if (key == 'd' || key == 'D') {
            int delta = ladder_delta(g, g->cur_x, g->cur_y);
            if (delta > 0) {
                game_change_floor(g, player, g->cur_floor + delta);
            } else if (cmd_dig_hole(g, player) && player->hp_cur <= 0) {
                goto game_over;
            }
        } else if (key == 'k' || key == 'K') {
            int target = game_trapdoor_floor(g, g->cur_x, g->cur_y);
            int key_index = target >= 0 ? target / 10 : -1;
            if (target >= 0 && key_index > 0 && key_index < 18 &&
                player->trapdoor_keys[key_index]) {
                game_change_floor(g, player, target);
                game_relocate(g, player);
                video_clear(v, 0);
                video_draw_text(v, 225, LOGICAL_H / 2,
                                "THE TRAPDOOR DROPS YOU!", 12);
                video_present(v);
                SDL_Delay(800);
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
            SDL_Delay(600);
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
            SDL_Delay(1000);
            break;
        } else if (key == 0) {
            int scan = input_getch(&g->input);
            int nx = g->cur_x;
            int ny = g->cur_y;
            int moved = 0;
            switch (scan) {
            case 0x48: /* Up arrow = North (Y-1) */
                ny--;
                g->last_move_dir = 0;
                moved = 1;
                break;
            case 0x50: /* Down arrow = South (Y+1) */
                ny++;
                g->last_move_dir = 1;
                moved = 1;
                break;
            case 0x4B: /* Left arrow = West (X-1) */
                nx--;
                g->last_move_dir = 2;
                moved = 1;
                break;
            case 0x4D: /* Right arrow = East (X+1) */
                nx++;
                g->last_move_dir = 3;
                moved = 1;
                break;
            }
            if (moved && game_can_move(g, g->cur_x, g->cur_y, nx, ny)) {
                int monster = game_find_monster(g, nx, ny);
                if (monster >= 0) {
                    /* Walking toward an adjacent monster is the same direct
                       engagement as F, including the rear/south pane. */
                    fight_monster(g, player, monster);
                    if (player->hp_cur <= 0) goto game_over;
                } else {
                    g->cur_x = nx;
                    g->cur_y = ny;
                    reveal_around_player(g);
                    if (game_apply_pitfall(g, player)) {
                        video_clear(v, 0);
                        video_draw_text(v, 250, LOGICAL_H / 2,
                                        "YOU FALL THROUGH A PIT!", 12);
                        video_present(v);
                        SDL_Delay(900);
                    }
                    game_advance_monsters(g, player);
                    if (player->hp_cur <= 0) goto game_over;
                }
            }
        }

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
    if (player->hp_cur <= 0) {
        video_clear(v, 0);
        video_draw_text(v, 160, 180, "YOU HAVE DIED!", 12);
        if (player->raise_x != 0xFFFFu) {
            video_draw_text(v, 80, 260, "YOUR RAISE CONTRACT SAVES YOU!", 10);
            int return_floor = player->raise_floor < MAX_DUNGEON_FLOORS ?
                               player->raise_floor : 0;
            int return_x = player->raise_x < MAP_W ? player->raise_x : MAP_W / 2;
            int return_y = player->raise_y < MAP_H ? player->raise_y : MAP_H / 2;
            player->raise_x = 0xFFFFu;
            if (player->stat_con > 1) player->stat_con--;
            character_clear_battle_effects(player);
            game_change_floor(g, player, return_floor);
            g->cur_x = return_x;
            g->cur_y = return_y;
            player->x_pos = (u16)return_x;
            player->y_pos = (u16)return_y;
            player->floor_depth = (u16)return_floor;
            player->hp_cur = player->hp_max;
            player->sp_cur = player->sp_max;
        } else {
            video_draw_text(v, 100, 220, "YOUR ADVENTURE IS OVER...", 7);
        }
        video_draw_text(v, 160, 300, "PRESS ANY KEY...", 15);
        video_present(v);
        input_wait_any_key(&g->input);
    }

    player->facing_dir = (u16)(g->last_move_dir & 3);
    player->dungeon_number = (u16)g->dungeon_number;
    game_save_character(g, slot);
    game_save_world_state(g);
}
