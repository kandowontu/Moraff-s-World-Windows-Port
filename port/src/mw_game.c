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
 * floor and player number; far_1EFA4 then extracts one two-bit edge value.
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

static s16 dungeon_hash(int block_x, int block_y, int floor, int player,
                        int modulus) {
    if (block_x < 0 || block_y < 0) return 0;

    s16 x = dos16(block_x + 9);
    s16 y = dos16(block_y + 7);
    s16 f = dos16(floor + 13);
    s16 p = dos16(player + 15);
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

    int which = dungeon_hash(x >> 4, y >> 4, floor, g->cur_player, 18);
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
        if (dungeon_hash(x, y, shaft, g->cur_player, 31) != 1) continue;

        int landing = shaft + 1;
        while (landing <= floor && rock_cell_at(g, x, y, landing))
            landing++;
        if (landing == floor) return shaft - floor;
    }

    if (dungeon_hash(x, y, floor, g->cur_player, 31) == 1) {
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

    int made = 0;
    for (int tries = 0; made < MONSTERS_PER_FLOOR && tries < 100000; tries++) {
        int x = game_rand(g) % MAP_W;
        int y = game_rand(g) % MAP_H;
        if (rock_cell_at(g, x, y, floor)) continue;

        int occupied = 0;
        for (int j = 0; j < made; j++)
            if (map[j].x == x && map[j].y == y) { occupied = 1; break; }
        if (occupied || (x == g->cur_x && y == g->cur_y)) continue;

        int type = combat_pick_monster_type(g, floor);
        int level = (floor > 0 ? floor : 1) + (game_rand(g) % 5) - 2;
        if (level < 1) level = 1;
        if (level > 255) level = 255;
        int cap = combat_calc_monster_hp(&monster_types[type], level);
        int hp = cap > 1 ? 1 + game_rand(g) % cap : 1;
        map[made].x = (u8)x;
        map[made].y = (u8)y;
        monster_record_set_hp(&map[made], hp);
        map[made].type = (u8)type;
        map[made].level = (u8)level;
        made++;
    }
    g->monster_floor[layer] = (u8)floor;
    g->monster_map_dirty = 1;
}

static int select_monster_floor(Game *g, int floor) {
    if (!g->monster_map_loaded) return -1;
    for (int i = 0; i < MONSTER_MAP_LAYERS; i++) {
        if (g->monster_floor[i] == (u8)floor) {
            g->monster_layer = i;
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
    return (a < 0 || b < 0) ? -1 : 0;
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
    if (!g->monster_map_loaded || index < 0 || index >= MONSTERS_PER_FLOOR) return 0;
    return monster_record_hp(&g->monster_map[g->monster_layer][index]);
}

void game_set_monster_hp(Game *g, int index, int hp) {
    if (!g->monster_map_loaded || index < 0 || index >= MONSTERS_PER_FLOOR) return;
    monster_record_set_hp(&g->monster_map[g->monster_layer][index], hp);
    g->monster_map_dirty = 1;
}

void game_kill_monster(Game *g, int index) {
    if (!g->monster_map_loaded || index < 0 || index >= MONSTERS_PER_FLOOR) return;
    clear_monster_record(&g->monster_map[g->monster_layer][index]);
    g->monster_map_dirty = 1;
    save_monster_map(g);
}

void game_advance_monsters(Game *g, Character *player) {
    if (!g->monster_map_loaded) return;
    if (player->eff_feather) player->eff_feather--;
    if (player->eff_fast_move) player->eff_fast_move--;
    if (player->eff_invisible) player->eff_invisible--;
    if (player->eff_str_bonus) player->eff_str_bonus--;
    if (player->eff_agi_bonus) player->eff_agi_bonus--;
    if (player->eff_super_str) player->eff_super_str--;
    if (player->eff_super_agi) player->eff_super_agi--;
    if (player->eff_fast_move && (game_rand(g) & 1)) {
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
    int target = dungeon_hash(x, y, g->cur_floor, g->cur_player, 0x960) * 10;
    if (target < 10 || target >= 180 || target / 10 == g->cur_floor / 10)
        return -1;
    return target;
}

int game_shop_type(Game *g, int x, int y) {
    if (g->cur_floor != 0 || x <= 0 || x >= MAP_W - 1 ||
        y <= 0 || y >= MAP_H - 1) return 0;
    int type = dungeon_hash(x, y, 0, g->cur_player, 110);
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
    if (dungeon_hash(x, y, floor, g->cur_player, modulus) >= threshold)
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
            int cx = x + cell_px / 2;
            int cy = y + cell_px / 2;
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
                video_fill_rect(v, x + 2, y + 2, cell_px - 4, cell_px - 4,
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
                /* Down ladder: the original map help describes this as a
                 * square. */
                video_hline(v, x + 2, y + 2, 6, 4);
                video_hline(v, x + 2, y + 7, 6, 4);
                video_vline(v, x + 2, y + 2, 6, 4);
                video_vline(v, x + 7, y + 2, 6, 4);
            } else if (ladder < 0) {
                /* Up ladder: two rails and three red rungs. */
                video_vline(v, x + 2, y + 1, 8, 46);
                video_vline(v, x + 7, y + 1, 8, 46);
                video_hline(v, x + 2, y + 2, 6, 10);
                video_hline(v, x + 2, y + 5, 6, 10);
                video_hline(v, x + 2, y + 8, 6, 10);
            }

            int trap = game_trapdoor_floor(g, wx, wy);
            if (!ladder && trap >= 0) {
                video_fill_rect(v, x + 2, y + 2, 6, 6, 0);
                video_hline(v, x + 2, y + 2, 6, 4);
                video_hline(v, x + 2, y + 7, 6, 4);
                video_vline(v, x + 2, y + 2, 6, 4);
                video_vline(v, x + 7, y + 2, 6, 4);
            } else if (!ladder && g->cur_floor > 0 &&
                       pit_bit_is_set(g, wx, wy) &&
                       pitfall_target(g, wx, wy) != g->cur_floor) {
                /* A previously triggered pit remains on the map. */
                video_fill_rect(v, x + 3, y + 3, 4, 4, 3);
            }

            if (wx == g->cur_x && wy == g->cur_y) {
                for (int p = 1; p < cell_px - 1; p++) {
                    video_put_pixel(v, x + p, y + p, 4);
                    video_put_pixel(v, x + cell_px - 1 - p, y + p, 4);
                }
                video_put_pixel(v, cx, cy, 6);
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

static void draw_ladder(Video *v, ProjRect p, int delta,
                        int vx, int vy, int vw, int vh) {
    int pw = p.right - p.left + 1;
    int ph = p.bottom - p.top + 1;
    if (pw < 8 || ph < 8) return;
    int cx = (p.left + p.right) / 2;
    int rail = pw / 7;
    if (rail < 3) rail = 3;

    if (delta < 0) {
        int top = p.top + ph / 5;
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
        /* Perspective rails descending into a dark floor opening. */
        int far_y = p.top + ph * 55 / 100;
        int near_y = p.bottom - 1;
        int far_r = rail / 3 + 1;
        for (int y = far_y; y <= near_y; y++) {
            int half = far_r + (rail - far_r) * (y - far_y) /
                                  ((near_y - far_y) ? (near_y - far_y) : 1);
            video_hline(v, cx - half, y, half * 2 + 1, 0);
        }
        dungeon_line(v, cx - far_r, far_y, cx - rail, near_y, 46,
                     vx, vy, vw, vh);
        dungeon_line(v, cx + far_r, far_y, cx + rail, near_y, 46,
                     vx, vy, vw, vh);
        for (int i = 1; i < 5; i++) {
            int y = far_y + (near_y - far_y) * i / 5;
            int half = far_r + (rail - far_r) * (y - far_y) /
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
                               const float *wall_depth) {
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
            if (color != 0 && color != 16) {
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
    int kind;       /* 0 monster, 1 up ladder, 2 down, 3 trapdoor, 4 shop */
} ViewActor;

static void draw_view_actors(Game *g, int vx, int vy, int vw, int vh, int dir,
                             const float *wall_depth) {
    int fdx, fdy, rdx, rdy;
    dir_to_delta(dir, &fdx, &fdy);
    dir_to_right(dir, &rdx, &rdy);
    ViewActor actors[MONSTERS_PER_FLOOR + 64];
    int count = 0;

    if (g->monster_map_loaded) {
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
            actors[count++] = (ViewActor){forward, side, pic, 0};
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
            if (ladder && count < (int)(sizeof(actors)/sizeof(actors[0])))
                actors[count++] = (ViewActor){forward, side, ladder < 0 ? 0 : 1,
                                               ladder < 0 ? 1 : 2};
            else if (trap >= 0 && count < (int)(sizeof(actors)/sizeof(actors[0])))
                actors[count++] = (ViewActor){forward, side, 1, 3};
            else if (shop && count < (int)(sizeof(actors)/sizeof(actors[0])))
                actors[count++] = (ViewActor){forward, side, shop, 4};
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
        else if (a->kind == 4) height = wall_h / 3;
        else height = wall_h * 4 / 5;
        if (height < 6) height = 6;
        int top = bottom - height;
        if (a->kind == 2 || a->kind == 3) top = horizon + wall_h / 8;
        if (a->kind == 3) {
            int sw = height * 2;
            int hatch_h = height / 2;
            for (int sy = top; sy < top + hatch_h; sy++) {
                if (sy < vy || sy >= vy + vh) continue;
                for (int sx = cx - sw / 2; sx <= cx + sw / 2; sx++) {
                    if (sx < vx || sx >= vx + vw) continue;
                    if (a->depth >= wall_depth[sx - vx] + 0.02f) continue;
                    int border = sy == top || sy == top + hatch_h - 1 ||
                                 sx == cx - sw / 2 || sx == cx + sw / 2;
                    g->video.pixels[sy * LOGICAL_W + sx] = border ? 4 : 0;
                }
            }
            g->video.dirty = 1;
        } else if (a->kind == 4) {
            int sw = height * 2;
            for (int sy = top; sy < top + height; sy++) {
                if (sy < vy || sy >= vy + vh) continue;
                for (int sx = cx - sw / 2; sx <= cx + sw / 2; sx++) {
                    if (sx < vx || sx >= vx + vw) continue;
                    if (a->depth < wall_depth[sx - vx] + 0.02f)
                        g->video.pixels[sy * LOGICAL_W + sx] = (u8)(a->pic + 2);
                }
            }
            g->video.dirty = 1;
        } else {
            draw_pic_billboard(g, a->pic, cx, top, height, a->depth,
                               vx, vy, vw, vh, wall_depth);
        }
    }

    /* A feature under the party is shown at the foot of every view, matching
     * the DOS floor-plane placement rather than becoming a wall decal. */
    int here_ladder = ladder_delta(g, g->cur_x, g->cur_y);
    int here_trap = game_trapdoor_floor(g, g->cur_x, g->cur_y);
    if (here_ladder > 0 || here_trap >= 0) {
        if (here_ladder > 0) {
            draw_pic_billboard(g, 1, vx + vw / 2, horizon + vh / 8, vh / 3,
                               0.25f, vx, vy, vw, vh, NULL);
        } else {
            int hx = vx + vw / 2, hy = horizon + vh / 5;
            video_fill_rect(&g->video, hx - vw / 6, hy, vw / 3, 3, 4);
            video_fill_rect(&g->video, hx - vw / 6, hy + vh / 10, vw / 3, 3, 4);
            video_vline(&g->video, hx - vw / 6, hy, vh / 10, 4);
            video_vline(&g->video, hx + vw / 6, hy, vh / 10, 4);
        }
    }
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
    const int spacing = SY(38);
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
    video_draw_text_scaled_xy(v, menu_x, y, "TURN SOUND  N      ", 8, xsn, xsd, ysn, ysd);
    video_draw_text_scaled_xy(v, menu_x, y, "           O       ", 4, xsn, xsd, ysn, ysd);
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
    const char *action = "HIT 'D' TO DIG A HOLE";
    int ladder = ladder_delta(g, g->cur_x, g->cur_y);
    int trap = game_trapdoor_floor(g, g->cur_x, g->cur_y);
    int shop = game_shop_type(g, g->cur_x, g->cur_y);
    if (ladder < 0) action = "HIT 'U' TO GO UP";
    else if (ladder > 0) action = "HIT 'D' TO GO DOWN";
    else if (trap >= 0) action = "HIT 'K' TO USE TRAPDOOR";
    else if (shop) action = "HIT 'D' TO ENTER SHOP";
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
    "FIELD PLATE", "TITANIUM"
};
#define ARMOR_COUNT 7

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

    const char *arm = (p->body_armor_lv < ARMOR_COUNT) ?
        armor_names[p->body_armor_lv] : "UNKNOWN";
    snprintf(line, sizeof(line), "CURRENT ARMOR: %s", arm);
    video_draw_text(v, 8, y, line, 4);
    y += fh;

    snprintf(line, sizeof(line), "LEVEL: %d", p->level);
    video_draw_text(v, 8, y, line, 5);
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
    input_getch(&g->input);
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
    input_getch(&g->input);
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
        input_getch(&g->input);
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
    snprintf(line, sizeof(line), "15) BODY ARMOR, LEVEL %d", p->body_armor_lv);
    video_draw_text(v, 8, y, line, 7); y += fh;
    snprintf(line, sizeof(line), "16) GAUNTLET, PLUS %d", p->gauntlet);
    video_draw_text(v, 8, y, line, 7);

    video_draw_text(v, 8, LOGICAL_H - fh - 4, "HIT ANY KEY...", 15);
    video_present(v);
    input_getch(&g->input);
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

    snprintf(line, sizeof(line), "HEALTH POINTS: %d OF %d",
             p->hp_cur, p->hp_max);
    video_draw_text(v, 8, y, line, 7);
    y += fh;

    snprintf(line, sizeof(line), "SPELL POINTS: %.0f OF %.0f",
             p->sp_cur, p->sp_max);
    video_draw_text(v, 8, y, line, 7);

    video_draw_text(v, 8, LOGICAL_H - fh - 4, "HIT ANY KEY...", 15);
    video_present(v);
    input_getch(&g->input);
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
    video_draw_text(v, 8, y, "B-BRICKS (BUY FROM A SHOP)", 8); y += fh;
    video_draw_text(v, 8, y, "C-CAST SPELL OR GET HELP ON SPELLS", 8); y += fh;
    video_draw_text(v, 8, y, "D-DOOR (DIG A HOLE IN THE FLOOR)", 8); y += fh;
    video_draw_text(v, 8, y, "E-EXPERIENCE NEEDED TO GAIN LEVEL", 8); y += fh;
    video_draw_text(v, 8, y, "F-FIGHT A MONSTER", 8); y += fh;
    video_draw_text(v, 8, y, "H-THIS HELP SCREEN", 8); y += fh;
    video_draw_text(v, 8, y, "I-USE AN ITEM", 8); y += fh;
    video_draw_text(v, 8, y, "L-LOSE (DROP) AN ITEM", 8); y += fh;
    video_draw_text(v, 8, y, "M-VIEW MONEY (FINANCIAL STATUS)", 8); y += fh;
    video_draw_text(v, 8, y, "O-TOGGLE SOUND ON AND OFF", 8); y += fh;
    video_draw_text(v, 8, y, "P-VIEW CONTENTS OF POCKETS", 8); y += fh;
    video_draw_text(v, 8, y, "Q-QUIT AND SAVE GAME", 8); y += fh;
    video_draw_text(v, 8, y, "T-WAIT (SKIP A TURN)", 8); y += fh;
    video_draw_text(v, 8, y, "V-VIEW STATS", 8); y += fh;
    video_draw_text(v, 8, y, "W-WEAPONS (SELECT TYPE)", 8); y += fh;
    video_draw_text(v, 8, y, "X-EXPAND MAP VIEW", 8); y += fh;
    video_draw_text(v, 8, y, "Z-ZOOM (TOGGLE MAP SIZE)", 8); y += fh;
    y += 4;
    video_draw_text(v, 8, y, "ARROWS-MOVE AND TURN", 8); y += fh;
    video_draw_text(v, 8, y, "1,2-SWITCH ACTIVE PLAYER", 8); y += fh;

    video_draw_text(v, 8, LOGICAL_H - fh - 4, "HIT ANY KEY...", 15);
    video_present(v);
    input_getch(&g->input);
}

/* ── Command: Expand Map (dungeon map viewer) ── */

static void cmd_expand_map(Game *g) {
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

            if (x == g->cur_x && y == g->cur_y) {
                video_fill_rect(v, px + 1, py + 1, cs - 3, cs - 3, 15);
            }
        }
    }
    video_present(v);
    input_getch(&g->input);
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
    } else {
        snprintf(line, sizeof(line), "DISEASE:         %s", p->diseased_turns > 0 ? "DISEASED" : "HEALTHY");
        video_draw_text(v, 8, y, line, p->diseased_turns > 0 ? 6 : 15); y += fh;
        snprintf(line, sizeof(line), "POISON:          %s", p->poisoned_turns > 0 ? "POISONED" : "CLEAN");
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
    }

    y += fh;
    video_draw_text(v, 8, y, "PRESS ANY KEY...", 7);
    video_present(v);
    input_getch(&g->input);
}

/* ── Player selection screen ── */

static int player_select_screen(Game *g) {
    Video *v = &g->video;

    video_clear(v, 0);
    video_draw_text(v, 16, 16, "PLEASE SELECT A PLAYER:", 15);
    video_draw_text(v, 16, 40, "NUM  NAME                 SEX     RACE        CLASS", 14);

    char line[80];
    for (int i = 0; i < MAX_PLAYERS; i++) {
        int y = 64 + i * (v->font_char_h + 4);
        if (g->char_exists[i]) {
            Character *ch = &g->chars[i];
            const char *sex_str = (ch->sex == 0) ? "MALE  " : "FEMALE";
            const char *race_str = (ch->race < RACE_COUNT) ? race_names[ch->race] : "???";
            const char *class_str = (ch->class_id < CLASS_COUNT) ? class_names[ch->class_id] : "???";
            snprintf(line, sizeof(line), " %d)  %-20.20s %s  %-11s %s",
                     i, ch->name, sex_str, race_str, class_str);
        } else {
            snprintf(line, sizeof(line), " %d)  SELECT TO CREATE A NEW CHARACTER", i);
        }
        video_draw_text(v, 16, y, line, g->char_exists[i] ? 7 : 8);
    }

    video_draw_text(v, 16, 64 + MAX_PLAYERS * (v->font_char_h + 4) + 16,
                    "SELECT PLAYER (0-9):", 15);
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

static void fight_monster(Game *g, Character *player, int index) {
    if (index < 0) return;
    CombatState cs;
    combat_init_entity(g, &cs, index);
    combat_run(g, &cs, player);
    if (cs.monster_hp <= 0 || cs.fled)
        game_kill_monster(g, index);
    else
        game_set_monster_hp(g, index, cs.monster_hp);
    g->monster_adjacent = game_find_adjacent_monster(g) >= 0;
}

/* ── Cast spell from exploration (handles battle vs prep) ── */

static void cmd_cast_spell(Game *g, Character *player) {
    Video *v = &g->video;
    int fh = v->font_char_h + 2;

    video_clear(v, 0);
    video_draw_text(v, 8, 4, "CAST WHICH TYPE OF SPELL?", 14);
    video_draw_text(v, 8, 4 + fh * 2, "1) PREPARATION SPELL", 7);
    video_draw_text(v, 8, 4 + fh * 3, "2) WIZARD BATTLE SPELL (COMBAT ONLY)", 8);
    video_draw_text(v, 8, 4 + fh * 4, "3) PRIEST BATTLE SPELL (COMBAT ONLY)", 8);
    video_draw_text(v, 8, 4 + fh * 6, "ESC TO CANCEL", 8);
    video_present(v);

    int key = input_getch(&g->input);
    if (key == '1') {
        cmd_cast_prep_spell(g, player);
    } else if (key == '2' || key == '3') {
        video_clear(v, 0);
        video_draw_text(v, 8, 4, "BATTLE SPELLS CAN ONLY BE CAST IN COMBAT!", 12);
        video_draw_text(v, 8, 4 + fh, "ENGAGE A MONSTER FIRST (PRESS F TO FIGHT).", 7);
        video_draw_text(v, 8, LOGICAL_H - fh - 4, "HIT ANY KEY...", 15);
        video_present(v);
        input_getch(&g->input);
    }
}

/* ── Main game loop ── */

void game_draw_exploration(Game *g, Character *player) {
    video_clear(&g->video, 0);
    draw_4way_view(g);
    draw_minimap(g, 0, SY(0x1AE), SX(0x11B), 38 * 10);
    draw_command_menu(g);
    draw_status_bar(g, player);
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
    if (g->monster_map_loaded && entity_index >= 0 &&
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

    game_draw_exploration(g, player);
    if (engaged) *engaged = saved;

    const ViewRect *vr = pane[dir];
    if (monster_hp > 0) {
        int pic = get_monster_pic_index_ext(monster_type);
        if (pic < 2) pic = 2;
        int sprite_h = vr->h * 17 / 20;
        int top = vr->y + vr->h - sprite_h - 2;
        draw_pic_billboard(g, pic, vr->x + vr->w / 2, top, sprite_h,
                           0.20f, vr->x, vr->y, vr->w, vr->h, NULL);
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
    input_getch(&g->input);
    if (input_poll_quit(&g->input)) return;

    /* Player selection */
    int slot = player_select_screen(g);
    if (slot < 0) return;

    g->player_slot[0] = slot;
    Character *player = &g->chars[slot];

    if (!g->char_exists[slot]) {
        video_clear(v, 0);
        video_draw_text(v, 16, 16, "CHARACTER CREATION", 15);
        video_draw_text(v, 16, 48, "Not yet implemented - creating default character", 14);
        video_present(v);

        memset(player, 0, sizeof(*player));
        strncpy(player->name, "ADVENTURER", 20);
        player->race = RACE_HUMAN;
        player->sex = 0;
        player->class_id = CLASS_WIZARD;
        player->hp_cur = 20;
        player->hp_max = 20;
        player->sp_cur = 10.0f;
        player->sp_max = 10.0f;
        player->level = 1;
        player->stat_str = 12;
        player->stat_int = 12;
        player->stat_wis = 12;
        player->stat_con = 12;
        player->stat_agi = 12;
        player->stat_luck = 12;
        player->x_pos = 0;
        player->y_pos = 0;
        player->floor_depth = 0;
        g->char_exists[slot] = 1;

        SDL_Delay(1500);
    }

    g->cur_x = player->x_pos;
    g->cur_y = player->y_pos;
    g->cur_floor = player->floor_depth;
    g->last_move_dir = 0;
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

    while (!input_poll_quit(&g->input)) {
        game_draw_exploration(g, player);

        video_present(v);

        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input)) break;

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
            cmd_cast_spell(g, player);
            game_advance_monsters(g, player);
        } else if (key == 'w' || key == 'W') {
            cmd_weapons(g, player);
        } else if (key == 'a' || key == 'A') {
            cmd_armor(g, player);
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
            if (delta < 0) game_change_floor(g, player, g->cur_floor + delta);
        } else if (key == 'd' || key == 'D') {
            int delta = ladder_delta(g, g->cur_x, g->cur_y);
            int shop = game_shop_type(g, g->cur_x, g->cur_y);
            if (delta > 0) {
                game_change_floor(g, player, g->cur_floor + delta);
            } else if (shop) {
                static const char *shop_names[6] = {
                    "", "GENERAL STORE", "TEMPLE", "BANK", "INN", "HOTEL"
                };
                video_clear(v, 0);
                video_draw_text(v, 220, LOGICAL_H / 2 - 20, shop_names[shop],
                                (u8)(shop + 2));
                video_draw_text(v, 260, LOGICAL_H / 2 + 20,
                                "WELCOME, ADVENTURER!", 15);
                video_present(v);
                input_getch(&g->input);
            }
        } else if (key == 'k' || key == 'K') {
            int target = game_trapdoor_floor(g, g->cur_x, g->cur_y);
            if (target >= 0) {
                game_change_floor(g, player, target);
                game_relocate(g, player);
                video_clear(v, 0);
                video_draw_text(v, 225, LOGICAL_H / 2,
                                "THE TRAPDOOR DROPS YOU!", 12);
                video_present(v);
                SDL_Delay(800);
            }
        } else if (key == 'q' || key == 'Q') {
            player->x_pos = (u16)g->cur_x;
            player->y_pos = (u16)g->cur_y;
            player->floor_depth = (u16)g->cur_floor;
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
                if (game_find_monster(g, nx, ny) >= 0) {
                    g->monster_adjacent = 1;
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
    }

game_over:
    if (player->hp_cur <= 0) {
        video_clear(v, 0);
        video_draw_text(v, 160, 180, "YOU HAVE DIED!", 12);
        video_draw_text(v, 100, 220, "YOUR ADVENTURE IS OVER...", 7);
        if (player->raise_contract) {
            video_draw_text(v, 80, 260, "YOUR RAISE CONTRACT SAVES YOU!", 10);
            player->hp_cur = player->hp_max / 2;
            if (player->hp_cur < 1) player->hp_cur = 1;
            player->raise_contract = 0;
        }
        video_draw_text(v, 160, 300, "PRESS ANY KEY...", 15);
        video_present(v);
        input_getch(&g->input);
    }

    game_save_character(g, slot);
    game_save_world_state(g);
}
