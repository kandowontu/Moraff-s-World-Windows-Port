#include "mw_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

/* ── Dungeon map access ── */

u8 map_get_cell(Game *g, int x, int y) {
    if (!g->dungeon_data) return 0xFF;
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return 0xFF;
    int off = y * MAP_STRIDE + x;
    if (off >= g->dungeon_data_size) return 0xFF;
    return g->dungeon_data[off];
}

int map_is_wall(Game *g, int x, int y) {
    u8 cell = map_get_cell(g, x, y);
    return (cell != 0x00);
}

int map_has_wall_n(u8 cell) { return (cell & WALL_N_MASK) != 0; }
int map_has_wall_e(u8 cell) { return (cell & WALL_E_MASK) != 0; }
int map_has_wall_s(u8 cell) { return (cell & WALL_S_MASK) != 0; }
int map_has_wall_w(u8 cell) { return (cell & WALL_W_MASK) != 0; }

/* Check if player can move to (nx, ny) from (ox, oy) */
static int can_move(Game *g, int ox, int oy, int nx, int ny) {
    if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) return 0;
    u8 dest = map_get_cell(g, nx, ny);
    if (dest == 0xFF) return 0;
    return 1;
}

/* ── Initialization ── */

int game_init(Game *g, const char *data_dir) {
    memset(g, 0, sizeof(*g));
    strncpy(g->game_dir, data_dir, sizeof(g->game_dir) - 1);

    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
            fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return -1;
        }
    }

    if (video_init(&g->video, "Moraff's World", 2) < 0) {
        return -1;
    }

    input_init(&g->input);

    char path[260];
    game_make_path(g, path, sizeof(path), "640X480.FNT");
    if (video_load_font(&g->video, path) < 0) {
        game_make_path(g, path, sizeof(path), "320X200.FNT");
        if (video_load_font(&g->video, path) < 0) {
            game_make_path(g, path, sizeof(path), "360X480.FNT");
            video_load_font(&g->video, path);
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

    /* Bypass MW.EXE launcher — set video mode 6 (640x480) directly */
    g->video_mode = 6;
    g->screen_w = 640;
    g->screen_h = 480;

    return 0;
}

void game_shutdown(Game *g) {
    for (int i = 0; i < 256; i++) free(g->world_pic_data[i]);
    for (int i = 0; i < 64; i++) free(g->wall_pic_data[i]);
    free(g->dungeon_data);
    free(g->worldmap_data);
    free(g->monster_data);

    video_shutdown(&g->video);
    SDL_Quit();
}

/* ── Drawing: Minimap ── */

void draw_minimap(Game *g, int mx, int my, int mw, int mh) {
    Video *v = &g->video;
    int cell_w = mw / 21;
    int cell_h = mh / 21;
    if (cell_w < 2) cell_w = 2;
    if (cell_h < 2) cell_h = 2;

    int view_r = 10;
    int cx = g->cur_x;
    int cy = g->cur_y;

    video_fill_rect(v, mx, my, mw, mh, 0);

    for (int dy = -view_r; dy <= view_r; dy++) {
        for (int dx = -view_r; dx <= view_r; dx++) {
            int wx = cx + dx;
            int wy = cy + dy;
            int sx = mx + (dx + view_r) * cell_w;
            int sy = my + (dy + view_r) * cell_h;

            if (wx < 0 || wx >= MAP_W || wy < 0 || wy >= MAP_H) {
                video_fill_rect(v, sx, sy, cell_w, cell_h, 8);
                continue;
            }

            u8 cell = map_get_cell(g, wx, wy);

            if (cell == 0xFF) {
                video_fill_rect(v, sx, sy, cell_w, cell_h, 8);
            } else if (cell == 0x00) {
                video_fill_rect(v, sx, sy, cell_w, cell_h, 1);
            } else {
                video_fill_rect(v, sx, sy, cell_w, cell_h, 3);
            }

            if (dx == 0 && dy == 0) {
                u8 pc = 14;
                int pcx = sx + cell_w / 2;
                int pcy = sy + cell_h / 2;
                video_put_pixel(v, pcx, pcy, pc);
                video_put_pixel(v, pcx - 1, pcy, pc);
                video_put_pixel(v, pcx + 1, pcy, pc);
                video_put_pixel(v, pcx, pcy - 1, pc);
                video_put_pixel(v, pcx, pcy + 1, pc);

                int ddx = 0, ddy = 0;
                switch (g->facing_dir) {
                    case 0: ddy = -2; break;
                    case 1: ddx = 2; break;
                    case 2: ddy = 2; break;
                    case 3: ddx = -2; break;
                }
                video_put_pixel(v, pcx + ddx, pcy + ddy, 15);
            }
        }
    }

    video_hline(v, mx, my, mw, 7);
    video_hline(v, mx, my + mh - 1, mw, 7);
    video_vline(v, mx, my, mh, 7);
    video_vline(v, mx + mw - 1, my, mh, 7);
}

/* ── Drawing: 3D first-person view ── */

static int check_wall_ahead(Game *g, int px, int py, int dir, int dist) {
    int dx = 0, dy = 0;
    switch (dir) {
        case 0: dy = -1; break;
        case 1: dx = 1; break;
        case 2: dy = 1; break;
        case 3: dx = -1; break;
    }
    int tx = px + dx * dist;
    int ty = py + dy * dist;
    return map_is_wall(g, tx, ty);
}

static int check_wall_side(Game *g, int px, int py, int fwd_dir, int dist, int side) {
    int fdx = 0, fdy = 0;
    switch (fwd_dir) {
        case 0: fdy = -1; break;
        case 1: fdx = 1; break;
        case 2: fdy = 1; break;
        case 3: fdx = -1; break;
    }
    int sdx = 0, sdy = 0;
    int right_dir = (fwd_dir + 1) % 4;
    switch (right_dir) {
        case 0: sdy = -1; break;
        case 1: sdx = 1; break;
        case 2: sdy = 1; break;
        case 3: sdx = -1; break;
    }
    int tx = px + fdx * dist + sdx * side;
    int ty = py + fdy * dist + sdy * side;
    return map_is_wall(g, tx, ty);
}

void draw_3d_view(Game *g, int vx, int vy, int vw, int vh) {
    Video *v = &g->video;

    video_fill_rect(v, vx, vy, vw, vh / 2, 2);
    video_fill_rect(v, vx, vy + vh / 2, vw, vh / 2, 8);

    int max_depth = 8;
    int cx = vx + vw / 2;
    int cy = vy + vh / 2;

    for (int d = max_depth; d >= 1; d--) {
        float scale = 1.0f / (float)d;
        int hw = (int)(vw * 0.5f * scale);
        int hh = (int)(vh * 0.5f * scale);

        int left = cx - hw;
        int right = cx + hw;
        int top = cy - hh;
        int bottom = cy + hh;

        if (check_wall_ahead(g, g->cur_x, g->cur_y, g->facing_dir, d)) {
            u8 shade = (u8)(3 + (max_depth - d));
            if (shade > 15) shade = 15;
            video_fill_rect(v, left, top, right - left, bottom - top, shade);

            video_hline(v, left, top, right - left, 7);
            video_hline(v, left, bottom, right - left, 7);
            video_vline(v, left, top, bottom - top, 7);
            video_vline(v, right, top, bottom - top, 7);

            int brick_h = (bottom - top) / 4;
            if (brick_h > 2) {
                for (int r = 1; r < 4; r++) {
                    int by = top + r * brick_h;
                    video_hline(v, left, by, right - left, 7);
                }
                int brick_w = (right - left) / 6;
                if (brick_w > 3) {
                    for (int r = 0; r < 4; r++) {
                        int by = top + r * brick_h;
                        int offset = (r % 2) ? brick_w / 2 : 0;
                        for (int c = 1; c < 6; c++) {
                            int bx = left + offset + c * brick_w;
                            if (bx > left && bx < right) {
                                video_vline(v, bx, by, brick_h, 7);
                            }
                        }
                    }
                }
            }
            break;
        }

        float prev_scale = (d < max_depth) ? 1.0f / (float)(d + 1) : 0.0f;
        int prev_hw = (int)(vw * 0.5f * prev_scale);
        int prev_hh = (int)(vh * 0.5f * prev_scale);
        int prev_top = cy - prev_hh;
        int prev_bottom = cy + prev_hh;
        int prev_left = cx - prev_hw;
        int prev_right = cx + prev_hw;

        if (check_wall_side(g, g->cur_x, g->cur_y, g->facing_dir, d, -1)) {
            u8 shade = (u8)(4 + (max_depth - d));
            if (shade > 15) shade = 15;
            int wall_w = left - prev_left;
            if (wall_w > 0) {
                video_fill_rect(v, prev_left, top, wall_w, bottom - top, shade - 1);
                video_vline(v, left, top, bottom - top, 7);
                video_hline(v, prev_left, top, wall_w, 7);
                video_hline(v, prev_left, bottom - 1, wall_w, 7);

                for (int ty = prev_top; ty < prev_bottom; ty++) {
                    if (ty >= top && ty < bottom) continue;
                    video_put_pixel(v, prev_left, ty, shade - 1);
                }
            }
        }

        if (check_wall_side(g, g->cur_x, g->cur_y, g->facing_dir, d, 1)) {
            u8 shade = (u8)(4 + (max_depth - d));
            if (shade > 15) shade = 15;
            int wall_w = prev_right - right;
            if (wall_w > 0) {
                video_fill_rect(v, right, top, wall_w, bottom - top, shade - 1);
                video_vline(v, right, top, bottom - top, 7);
                video_hline(v, right, top, wall_w, 7);
                video_hline(v, right, bottom - 1, wall_w, 7);
            }
        }
    }

    video_hline(v, vx, vy, vw, 15);
    video_hline(v, vx, vy + vh - 1, vw, 15);
    video_vline(v, vx, vy, vh, 15);
    video_vline(v, vx + vw - 1, vy, vh, 15);
}

/* ── Drawing: Command menu (matches original game layout from func_27112) ── */

static void draw_command_menu(Game *g) {
    Video *v = &g->video;
    int fh = v->font_char_h;

    int menu_x = LOGICAL_W - 20 * v->font_advance;
    if (menu_x < 300) menu_x = 300;
    int spacing = fh + 2;
    int y = 2;

    video_draw_text(v, menu_x, y, " RICKS   VIEW  ONEY", 8);
    video_draw_text(v, menu_x, y, "B             M    ", 4);
    y += spacing;
    video_draw_text(v, menu_x, y, " EAPONS  VIEW STATS", 8);
    video_draw_text(v, menu_x, y, "W        V         ", 4);
    y += spacing;
    video_draw_text(v, menu_x, y, " OOM     CAST SPELL", 8);
    video_draw_text(v, menu_x, y, "Z        C         ", 4);
    y += spacing;
    video_draw_text(v, menu_x, y, "USE ITEM EXPAND MAP", 8);
    video_draw_text(v, menu_x, y, "    I    X         ", 4);
    y += spacing;
    video_draw_text(v, menu_x, y, " RMOR    LOSE ITEM ", 8);
    video_draw_text(v, menu_x, y, "A        L         ", 4);
    y += spacing;
    video_draw_text(v, menu_x, y, " IGHT    POCKETS   ", 8);
    video_draw_text(v, menu_x, y, "F        P         ", 4);
    y += spacing;
    video_draw_text(v, menu_x, y, "WAI      EXP NEEDED", 8);
    video_draw_text(v, menu_x, y, "   T     E         ", 4);
    y += spacing;
    video_draw_text(v, menu_x, y, "TURN SOUND ON      ", 8);
    video_draw_text(v, menu_x, y, "           O       ", 4);
    y += spacing;
    video_draw_text(v, menu_x, y, "SPELLS IN EFFECT 1 ", 8);
    y += spacing;
    video_draw_text(v, menu_x, y, "SPELLS IN EFFECT 2 ", 8);
    y += spacing;
    video_draw_text(v, menu_x, y, " UIT-SAVE HELP (F1)", 8);
    video_draw_text(v, menu_x, y, "Q         H        ", 4);
}

/* ── Drawing: Status bar (matches original format) ── */

static void draw_status_bar(Game *g, Character *player) {
    Video *v = &g->video;
    int fh = v->font_char_h;
    int bar_y = LOGICAL_H - fh * 4 - 4;

    video_fill_rect(v, 0, bar_y, LOGICAL_W, LOGICAL_H - bar_y, 0);
    video_hline(v, 0, bar_y, LOGICAL_W, 7);

    char line[128];
    int y = bar_y + 2;
    int col2 = 400;

    const char *dir_str[] = {"NORTH", "EAST", "SOUTH", "WEST"};
    snprintf(line, sizeof(line), "L:%d  X:%d  Y:%d  %s",
             g->cur_floor, g->cur_x, g->cur_y, dir_str[g->facing_dir]);
    video_draw_text(v, 4, y, line, 6);
    y += fh;

    snprintf(line, sizeof(line), "SPELL POINTS: %.0f OF %.0f",
             player->sp_cur, player->sp_max);
    video_draw_text(v, 4, y, line, 6);
    snprintf(line, sizeof(line), "STR: %d CON: %d",
             player->stat_str, player->stat_con);
    video_draw_text(v, col2, y, line, 2);
    y += fh;

    snprintf(line, sizeof(line), "HEALTH POINTS: %d OF %d",
             player->hp_cur, player->hp_max);
    video_draw_text(v, 4, y, line, 6);
    snprintf(line, sizeof(line), "INT: %d DEX: %d",
             player->stat_int, player->stat_agi);
    video_draw_text(v, col2, y, line, 2);
    y += fh;

    snprintf(line, sizeof(line), "                              ");
    video_draw_text(v, 4, y, line, 6);
    snprintf(line, sizeof(line), "WIZ: %d LUCK:%d",
             player->stat_wis, player->stat_luck);
    video_draw_text(v, col2, y, line, 2);
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

    const char *wpn = (p->weapon_plus < WEAPON_COUNT) ?
        weapon_names[p->weapon_plus] : "UNKNOWN";
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

    video_clear(v, 0);
    video_draw_text(v, 8, 4, "EXPANDED DUNGEON MAP, HIT ANY KEY...", 15);
    for (int y = 0; y < MAP_H && y * 5 + 20 < LOGICAL_H; y++) {
        for (int x = 0; x < MAP_W && x * 5 + 8 < LOGICAL_W; x++) {
            u8 cell = map_get_cell(g, x, y);
            u8 color;
            if (cell == 0xFF) color = 8;
            else if (cell == 0x00) color = 1;
            else color = 3;
            video_fill_rect(v, 8 + x * 5, 20 + y * 5, 4, 4, color);
            if (x == g->cur_x && y == g->cur_y) {
                video_fill_rect(v, 8 + x * 5, 20 + y * 5, 4, 4, 14);
            }
        }
    }
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
            if (map_get_cell(g, x, y) == 0x00) {
                g->cur_x = x;
                g->cur_y = y;
                return;
            }
        }
    }
    g->cur_x = MAP_W / 2;
    g->cur_y = MAP_H / 2;
}

/* ── Main game loop ── */

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
    g->facing_dir = 0;

    if (g->cur_x == 0 && g->cur_y == 0) {
        find_start_pos(g);
    }

    /* Screen layout matching original game (ideal 1600x1200 scaled to 640x480):
     * North 3D view: top-left quadrant     (0,0)-(290,240)
     * East 3D view:  bottom-left quadrant  (112,172)-(288,412)
     * South 3D view: top-right quadrant    (291,241)-(463,465)
     * West 3D view:  bottom-right quadrant (465,172)-(639,412)
     * Command menu:  top-right text overlay at x=465
     * Status bar:    bottom strip           */
    int fh = v->font_char_h;
    int status_h = fh * 4 + 4;
    int status_y = LOGICAL_H - status_h;

    int nv_x = 0, nv_y = 0, nv_w = 290, nv_h = 240;
    int ev_x = 112, ev_y = 172, ev_w = 176, ev_h = 240;
    int sv_x = 291, sv_y = 241, sv_w = 172, sv_h = 224;
    int wv_x = 465, wv_y = 172, wv_w = 174, wv_h = 240;

    while (!input_poll_quit(&g->input)) {
        video_clear(v, 0);

        draw_3d_view(g, nv_x, nv_y, nv_w, nv_h);
        draw_minimap(g, ev_x, ev_y, ev_w, ev_h);
        draw_command_menu(g);
        draw_status_bar(g, player);

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
        } else if (key == 't' || key == 'T') {
            /* Wait - skip a turn (original: just passes without moving) */
        } else if (key == 'q' || key == 'Q') {
            game_save_character(g, slot);
            video_clear(v, 0);
            video_draw_text(v, 180, 200, "GAME SAVED.", 15);
            video_present(v);
            SDL_Delay(1000);
            break;
        } else if (key == 0) {
            int scan = input_getch(&g->input);
            int nx = g->cur_x;
            int ny = g->cur_y;
            switch (scan) {
            case 0x48: /* Up arrow - move forward */
                switch (g->facing_dir) {
                case 0: ny--; break;
                case 1: nx++; break;
                case 2: ny++; break;
                case 3: nx--; break;
                }
                if (can_move(g, g->cur_x, g->cur_y, nx, ny)) {
                    g->cur_x = nx;
                    g->cur_y = ny;
                }
                break;
            case 0x50: /* Down arrow - move backward */
                switch (g->facing_dir) {
                case 0: ny++; break;
                case 1: nx--; break;
                case 2: ny--; break;
                case 3: nx++; break;
                }
                if (can_move(g, g->cur_x, g->cur_y, nx, ny)) {
                    g->cur_x = nx;
                    g->cur_y = ny;
                }
                break;
            case 0x4B: /* Left arrow - turn left */
                g->facing_dir = (g->facing_dir + 3) % 4;
                break;
            case 0x4D: /* Right arrow - turn right */
                g->facing_dir = (g->facing_dir + 1) % 4;
                break;
            }
        }

        if (g->cur_x < 0) g->cur_x = 0;
        if (g->cur_y < 0) g->cur_y = 0;
        if (g->cur_x >= MAP_W) g->cur_x = MAP_W - 1;
        if (g->cur_y >= MAP_H) g->cur_y = MAP_H - 1;

        player->x_pos = g->cur_x;
        player->y_pos = g->cur_y;
    }

    game_save_character(g, slot);
}
