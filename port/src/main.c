#include "mw_game.h"
#include "mw_combat.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static int has_game_data(const char *dir) {
    char test[300];
    snprintf(test, sizeof(test), "%s/DUNG.BIN", dir);
    if (file_exists(test)) return 1;
    snprintf(test, sizeof(test), "%s/dung.bin", dir);
    return file_exists(test);
}

static void find_data_dir(char *out, int out_sz, const char *argv0) {
    /* 1) Check CWD */
    if (getcwd(out, out_sz) && has_game_data(out)) return;

    /* 2) Check exe directory and ancestors (handles port/build/ layout) */
    char exe_dir[260];
    const char *base = SDL_GetBasePath();
    if (base) {
        strncpy(exe_dir, base, sizeof(exe_dir) - 1);
        exe_dir[sizeof(exe_dir) - 1] = '\0';
        SDL_free((void *)base);

        /* Strip trailing separator */
        int len = (int)strlen(exe_dir);
        if (len > 0 && (exe_dir[len-1] == '/' || exe_dir[len-1] == '\\'))
            exe_dir[--len] = '\0';

        /* Check exe dir itself */
        if (has_game_data(exe_dir)) {
            strncpy(out, exe_dir, out_sz);
            return;
        }

        /* Walk up directory tree (up to 3 levels for port/build/) */
        for (int i = 0; i < 3; i++) {
            char *sep = strrchr(exe_dir, '\\');
            if (!sep) sep = strrchr(exe_dir, '/');
            if (!sep) break;
            *sep = '\0';
            if (has_game_data(exe_dir)) {
                strncpy(out, exe_dir, out_sz);
                return;
            }
        }
    }

    /* 3) Fallback to CWD */
    if (!getcwd(out, out_sz)) strcpy(out, ".");
}

static int save_framebuffer_bmp(Video *v, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    int w = LOGICAL_W, h = LOGICAL_H;
    int row_bytes = w * 3;
    int pad = (4 - (row_bytes % 4)) % 4;
    int stride = row_bytes + pad;
    int img_size = stride * h;
    int file_size = 54 + img_size;

    /* BMP header */
    u8 hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = (u8)(file_size); hdr[3] = (u8)(file_size >> 8);
    hdr[4] = (u8)(file_size >> 16); hdr[5] = (u8)(file_size >> 24);
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = (u8)(w); hdr[19] = (u8)(w >> 8);
    hdr[22] = (u8)(h); hdr[23] = (u8)(h >> 8);
    hdr[26] = 1;
    hdr[28] = 24;
    hdr[34] = (u8)(img_size); hdr[35] = (u8)(img_size >> 8);
    hdr[36] = (u8)(img_size >> 16); hdr[37] = (u8)(img_size >> 24);

    fwrite(hdr, 1, 54, f);

    u8 zero[4] = {0};
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            u8 idx = v->pixels[y * w + x];
            PaletteEntry *c = &v->palette[idx];
            u8 bgr[3] = {c->b, c->g, c->r};
            fwrite(bgr, 1, 3, f);
        }
        if (pad > 0) fwrite(zero, 1, pad, f);
    }

    fclose(f);
    return 0;
}

static int run_test_sprite(const char *data_dir, int monster_type, int asset_test) {
    Game game;
    if (game_init(&game, data_dir) < 0) {
        fprintf(stderr, "Failed to initialize game\n");
        return 1;
    }

    CombatState cs = {0};
    cs.active = 1;
    cs.monster_type_idx = monster_type;
    cs.monster_level = 5;
    cs.monster_hp = 100;
    cs.monster_max_hp = 100;

    Character dummy = {0};
    dummy.level = 5;
    dummy.hp_cur = 200;
    dummy.hp_max = 200;
    strncpy(dummy.name, "TEST", sizeof(dummy.name) - 1);

    Video *v = &game.video;
    video_clear(v, 0);

    const MonsterType *mt = &monster_types[cs.monster_type_idx];

    if (asset_test == 1) {
        int wall_idx = monster_type;
        if (wall_idx < 0 || wall_idx >= game.wall_pic_count)
            wall_idx = 0;
        if (game.wall_pic_data[wall_idx]) {
            video_blit_pic_sprite(v, LOGICAL_W / 2, 0, LOGICAL_H,
                                  game.wall_pic_data[wall_idx],
                                  game.wall_pic_sizes[wall_idx], 0xFF);
        }

        char bmp_path[300];
        snprintf(bmp_path, sizeof(bmp_path), "%s/test_wall_%d.bmp", data_dir, wall_idx);
        save_framebuffer_bmp(v, bmp_path);
        printf("Saved: %s\n", bmp_path);
        video_present(v);
        SDL_Delay(100);
        game_shutdown(&game);
        return 0;
    }

    if (asset_test == 2) {
        int pic_idx = monster_type;
        if (pic_idx < 0 || pic_idx >= game.world_pic_count) pic_idx = 0;
        video_blit_pic_sprite(v, LOGICAL_W / 2, 20, LOGICAL_H - 40,
                              game.world_pic_data[pic_idx],
                              game.world_pic_sizes[pic_idx], 0xFF);
        char bmp_path[300];
        snprintf(bmp_path, sizeof(bmp_path), "%s/test_world_%d.bmp", data_dir, pic_idx);
        save_framebuffer_bmp(v, bmp_path);
        printf("Saved: %s\n", bmp_path);
        video_present(v);
        SDL_Delay(100);
        game_shutdown(&game);
        return 0;
    }

    /* Render monster sprite exactly as combat does */
    extern int get_monster_pic_index_ext(int type_idx);
    int pic_idx = get_monster_pic_index_ext(cs.monster_type_idx);
    if (pic_idx >= 0 && pic_idx < game.world_pic_count && game.world_pic_data[pic_idx]) {
        int sprite_h = 400;
        int sprite_cx = LOGICAL_W - 160;
        int sprite_cy = 40;
        video_blit_pic_sprite(v, sprite_cx, sprite_cy, sprite_h,
                              game.world_pic_data[pic_idx],
                              game.world_pic_sizes[pic_idx], 0xFF);
        printf("Rendered monster '%s' (type %d, pic_idx %d)\n",
               mt->name, monster_type, pic_idx);
    } else {
        printf("Monster '%s' (type %d) has no sprite (pic_idx=%d)\n",
               mt->name, monster_type, pic_idx);
    }

    /* Draw text info */
    char line[128];
    snprintf(line, sizeof(line), "A %s APPEARS!", mt->name);
    video_draw_text(v, 8, 4, line, 12);
    snprintf(line, sizeof(line), "Type:%d  Pic:%d  HP:%d", monster_type, pic_idx, cs.monster_hp);
    video_draw_text(v, 8, 20, line, 14);

    /* Save to BMP */
    char bmp_path[300];
    snprintf(bmp_path, sizeof(bmp_path), "%s/test_sprite_%d.bmp", data_dir, monster_type);
    if (save_framebuffer_bmp(v, bmp_path) == 0) {
        printf("Saved: %s\n", bmp_path);
    } else {
        fprintf(stderr, "Failed to save BMP\n");
    }

    /* Also present on screen briefly */
    video_present(v);
    SDL_Delay(100);

    game_shutdown(&game);
    return 0;
}

static int run_test_bestiary(const char *data_dir, int selected) {
    Game game;
    if (game_init(&game, data_dir) < 0) return 1;
    int failures = 0;
    int seen_pic[256] = {0};
    for (int i = 0; i < MONSTER_TYPE_COUNT; i++) {
        int pic = get_monster_pic_index_ext(i);
        if (pic >= game.world_pic_count || (pic >= 0 && pic < 2)) failures++;
        if (pic >= 0 && pic < 256) seen_pic[pic] = 1;
        if (get_monster_color_ext(i) < 0) failures++;
        game.bestiary_kills[i] = (u32)(i + 1);
    }
    for (int pic = 2; pic < game.world_pic_count; pic++)
        if (!seen_pic[pic]) failures++;
    failures += game_ui_self_test(&game);
    if (selected < 0) selected = 0;
    if (selected >= MONSTER_TYPE_COUNT) selected = MONSTER_TYPE_COUNT - 1;
    game_draw_bestiary_test(&game, selected);
    char bmp_path[300];
    snprintf(bmp_path, sizeof(bmp_path), "%s/test_bestiary_%d.bmp",
             data_dir, selected);
    int rc = save_framebuffer_bmp(&game.video, bmp_path);
    printf("Beastiary picture/color coverage: %s (%d failures)\n",
           failures ? "FAIL" : "PASS", failures);
    if (rc == 0) printf("Saved: %s\n", bmp_path);
    game_shutdown(&game);
    return (failures || rc < 0) ? 1 : 0;
}

static int run_test_frame(const char *data_dir, int test_x, int test_y,
                          int test_floor, int test_slot, int combat_overlay) {
    Game game;
    if (game_init(&game, data_dir) < 0) {
        fprintf(stderr, "Failed to initialize game\n");
        return 1;
    }

    Character dummy = {0};
    strncpy(dummy.name, "TEST", sizeof(dummy.name) - 1);
    dummy.hp_cur = 4904;
    dummy.hp_max = 4896;
    dummy.sp_cur = 4266.0f;
    dummy.sp_max = 4266.0f;
    dummy.stat_str = 978;
    dummy.stat_con = 890;
    dummy.stat_int = 904;
    dummy.stat_agi = 903;
    dummy.stat_wis = 901;
    dummy.stat_luck = 906;
    dummy.level = 181;

    int auto_actor = test_x == -2;
    int auto_shop = test_x == -3;
    int auto_ladder = test_x == -4 ? 1 : (test_x == -5 ? -1 : 0);
    int auto_trap = test_x == -6;
    int actor_index = -1;
    if (test_x < 0) test_x = 0;
    if (test_x >= MAP_W) test_x = MAP_W - 1;
    if (test_y < 0) test_y = 0;
    if (test_y >= MAP_H) test_y = MAP_H - 1;
    game.cur_x = test_x;
    game.cur_y = test_y;
    game.cur_floor = test_floor;
    if (test_slot >= 0 && test_slot < MAX_PLAYERS &&
        game.char_exists[test_slot])
        game.dungeon_number = game.chars[test_slot].dungeon_number;
    game.view_mode = 0;
    if (auto_shop) {
        for (int y = 1; y < MAP_H - 1; y++) {
            for (int x = 1; x < MAP_W - 1; x++) {
                if (game_shop_type(&game, x, y) && !map_is_wall(&game, x, y)) {
                    game.cur_x = x;
                    game.cur_y = y;
                    y = MAP_H;
                    break;
                }
            }
        }
    }
    if (auto_ladder) {
        for (int y = 1; y < MAP_H - 1; y++) {
            for (int x = 1; x < MAP_W - 1; x++) {
                int delta = game_ladder_delta(&game, x, y);
                if ((auto_ladder > 0 && delta > 0) ||
                    (auto_ladder < 0 && delta < 0)) {
                    game.cur_x = x;
                    game.cur_y = y;
                    y = MAP_H;
                    break;
                }
            }
        }
    }
    if (auto_trap) {
        for (int y = 1; y < MAP_H - 1; y++) {
            for (int x = 1; x < MAP_W - 1; x++) {
                if (game_trapdoor_floor(&game, x, y) >= 0 &&
                    !map_is_wall(&game, x, y)) {
                    game.cur_x = x;
                    game.cur_y = y;
                    y = MAP_H;
                    break;
                }
            }
        }
    }
    if (test_slot >= 0) {
        game_load_world_state(&game, test_slot);
        int alive = 0;
        for (int i = 0; i < MONSTERS_PER_FLOOR; i++)
            if (game_monster_hp(&game, i) > 0 &&
                game.monster_map[game.monster_layer][i].x < MAP_W &&
                game.monster_map[game.monster_layer][i].y < MAP_H) alive++;
        printf("Loaded save slot %d world state: dungeon=%d layer=%d monsters=%d\n",
               test_slot, game.dungeon_number, game.monster_layer, alive);
        if (auto_actor) {
            static const int dx[4] = {0, 0, -1, 1};
            static const int dy[4] = {-1, 1, 0, 0};
            for (int i = 0; i < MONSTERS_PER_FLOOR; i++) {
                MonsterRecord *m = &game.monster_map[game.monster_layer][i];
                if (game_monster_hp(&game, i) <= 0 || m->x >= MAP_W || m->y >= MAP_H)
                    continue;
                for (int d = 0; d < 4; d++) {
                    int px = (int)m->x + dx[d], py = (int)m->y + dy[d];
                    if (game_can_move(&game, px, py, m->x, m->y)) {
                        game.cur_x = px; game.cur_y = py;
                        actor_index = i;
                        printf("Actor test: monster=%d type=%d at %d,%d; player=%d,%d\n",
                               i, m->type, m->x, m->y, px, py);
                        i = MONSTERS_PER_FLOOR;
                        break;
                    }
                }
            }
        }
    }

    printf("Test cell L:%d X:%d Y:%d edges N/E/S/W=%d/%d/%d/%d\n",
           game.cur_floor, game.cur_x, game.cur_y,
           map_get_edge(&game, game.cur_x,     game.cur_y,     1),
           map_get_edge(&game, game.cur_x + 1, game.cur_y,     0),
           map_get_edge(&game, game.cur_x,     game.cur_y + 1, 1),
           map_get_edge(&game, game.cur_x,     game.cur_y,     0));
    printf("Movement N/E/S/W=%d/%d/%d/%d\n",
           game_can_move(&game, game.cur_x, game.cur_y,
                         game.cur_x, game.cur_y - 1),
           game_can_move(&game, game.cur_x, game.cur_y,
                         game.cur_x + 1, game.cur_y),
           game_can_move(&game, game.cur_x, game.cur_y,
                         game.cur_x, game.cur_y + 1),
           game_can_move(&game, game.cur_x, game.cur_y,
                         game.cur_x - 1, game.cur_y));
    printf("Features ladder=%d shop=%d trapdoor=%d\n",
           game_ladder_delta(&game, game.cur_x, game.cur_y),
           game_shop_type(&game, game.cur_x, game.cur_y),
           game_trapdoor_floor(&game, game.cur_x, game.cur_y));

    game_update_visibility(&game);

    if (combat_overlay && actor_index >= 0) {
        MonsterRecord *m = &game.monster_map[game.monster_layer][actor_index];
        game_draw_combat_overlay(&game, &dummy, actor_index, m->type,
                                 m->level, game_monster_hp(&game, actor_index),
                                 "THE MONSTER DOES 8 POINTS", "", "");
    } else {
        game_draw_exploration(&game, &dummy);
    }

    char bmp_path[300];
    snprintf(bmp_path, sizeof(bmp_path), "%s/%s", data_dir,
             combat_overlay ? "test_combat_overlay.bmp" : "test_exploration.bmp");
    int rc = save_framebuffer_bmp(&game.video, bmp_path);
    if (rc == 0) printf("Saved: %s\n", bmp_path);
    else fprintf(stderr, "Failed to save BMP\n");

    video_present(&game.video);
    SDL_Delay(100);
    /* Test rendering is read-only even when a real MON.MAP was loaded. */
    game.active_save_slot = -1;
    game.pit_state_loaded = 0;
    game_shutdown(&game);
    return rc < 0;
}

static int run_test_state_roundtrip(const char *data_dir, int slot, int floor) {
    Game game;
    if (game_init(&game, data_dir) < 0) return 1;
    game.cur_floor = floor;
    game.cur_x = 1;
    game.cur_y = 1;
    game_load_world_state(&game, slot);
    int index = -1;
    for (int i = 0; i < MONSTERS_PER_FLOOR; i++)
        if (game_monster_hp(&game, i) > 1 &&
            game.monster_map[game.monster_layer][i].x < MAP_W &&
            game.monster_map[game.monster_layer][i].y < MAP_H) { index = i; break; }
    if (index < 0) { game_shutdown(&game); return 2; }
    int hp = game_monster_hp(&game, index);
    game_set_monster_hp(&game, index, hp - 1);
    game_save_world_state(&game);
    game_shutdown(&game);

    if (game_init(&game, data_dir) < 0) return 3;
    game.cur_floor = floor;
    game_load_world_state(&game, slot);
    if (game_monster_hp(&game, index) != hp - 1) {
        fprintf(stderr, "MON.MAP HP roundtrip failed\n");
        game_shutdown(&game);
        return 4;
    }
    game_kill_monster(&game, index);
    game_shutdown(&game);

    if (game_init(&game, data_dir) < 0) return 5;
    game.cur_floor = floor;
    game_load_world_state(&game, slot);
    MonsterRecord *m = &game.monster_map[game.monster_layer][index];
    int ok = game_monster_hp(&game, index) == 0 && m->x == 100 && m->y == 100;
    printf("Persistent monster damage/death roundtrip: %s\n", ok ? "PASS" : "FAIL");
    Character dummy = {0};
    int pit_x = -1, pit_y = -1;
    for (int y = 1; ok && y < MAP_H - 1 && pit_x < 0; y++) {
        for (int x = 1; x < MAP_W - 1; x++) {
            game.cur_floor = floor;
            game.cur_x = x;
            game.cur_y = y;
            dummy.floor_depth = (u16)floor;
            if (game_apply_pitfall(&game, &dummy)) {
                pit_x = x; pit_y = y;
                break;
            }
        }
    }
    game_shutdown(&game);

    if (ok && pit_x >= 0) {
        if (game_init(&game, data_dir) < 0) return 7;
        game.cur_floor = floor;
        game.cur_x = pit_x;
        game.cur_y = pit_y;
        dummy.floor_depth = (u16)floor;
        game_load_world_state(&game, slot);
        int repeated = game_apply_pitfall(&game, &dummy);
        printf("Previously-used pitfall tracking: %s\n", repeated ? "FAIL" : "PASS");
        if (repeated) ok = 0;
        game_shutdown(&game);
    } else if (ok) {
        printf("Previously-used pitfall tracking: SKIP (all candidates already used)\n");
    }
    return ok ? 0 : 6;
}

int main(int argc, char *argv[]) {
    /* Need SDL init early for SDL_GetBasePath */
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

    char data_dir[260];
    int test_sprite_mode = 0;
    int test_wall_mode = 0;
    int test_frame_mode = 0;
    int test_combat_mode = 0;
    int test_slot = -1;
    int test_state_mode = 0;
    int test_magic_mode = 0;
    int test_economy_mode = 0;
    int test_bestiary_mode = 0;
    int test_x = 19, test_y = 20, test_floor = 787;
    int test_monster_type = 57; /* default: ball */

    /* Parse args */
    int data_arg = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test-sprite") == 0) {
            test_sprite_mode = 1;
            if (i + 1 < argc) {
                test_monster_type = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "--test-wall") == 0) {
            test_sprite_mode = 1;
            test_wall_mode = 1;
            if (i + 1 < argc) {
                test_monster_type = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "--test-world") == 0) {
            test_sprite_mode = 1;
            test_wall_mode = 2;
            if (i + 1 < argc) test_monster_type = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--test-frame") == 0) {
            test_frame_mode = 1;
            if (i + 2 < argc) {
                test_x = atoi(argv[++i]);
                test_y = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "--test-actors") == 0) {
            test_frame_mode = 1;
            test_x = -2;
            test_y = 0;
        } else if (strcmp(argv[i], "--test-combat") == 0) {
            test_frame_mode = 1;
            test_combat_mode = 1;
            test_x = -2;
            test_y = 0;
        } else if (strcmp(argv[i], "--test-floor") == 0) {
            if (i + 1 < argc) test_floor = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--test-slot") == 0) {
            if (i + 1 < argc) test_slot = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--test-state-roundtrip") == 0) {
            test_state_mode = 1;
        } else if (strcmp(argv[i], "--test-magic") == 0) {
            test_magic_mode = 1;
        } else if (strcmp(argv[i], "--test-economy") == 0) {
            test_economy_mode = 1;
        } else if (strcmp(argv[i], "--test-bestiary") == 0) {
            test_bestiary_mode = 1;
            if (i + 1 < argc) test_monster_type = atoi(argv[++i]);
        } else if (!data_arg) {
            strncpy(data_dir, argv[i], sizeof(data_dir) - 1);
            data_dir[sizeof(data_dir) - 1] = '\0';
            data_arg = 1;
        }
    }

    if (!data_arg) {
        find_data_dir(data_dir, sizeof(data_dir), argv[0]);
    }

    printf("Moraff's World - Native Port\n");
    printf("Data directory: %s\n", data_dir);

    if (!has_game_data(data_dir)) {
        fprintf(stderr, "ERROR: Game data not found in '%s'\n", data_dir);
        fprintf(stderr, "Place the executable in the game directory or pass the path as an argument.\n");
        SDL_Quit();
        return 1;
    }

    if (test_sprite_mode) {
        int rc = run_test_sprite(data_dir, test_monster_type, test_wall_mode);
        SDL_Quit();
        return rc;
    }

    if (test_bestiary_mode) {
        int rc = run_test_bestiary(data_dir, test_monster_type);
        SDL_Quit();
        return rc;
    }

    if (test_frame_mode) {
        int rc = run_test_frame(data_dir, test_x, test_y, test_floor,
                                test_slot, test_combat_mode);
        SDL_Quit();
        return rc;
    }

    if (test_state_mode) {
        int rc = run_test_state_roundtrip(data_dir,
                                          test_slot >= 0 ? test_slot : 4,
                                          test_floor);
        SDL_Quit();
        return rc;
    }

    if (test_magic_mode) {
        int rc = combat_self_test();
        SDL_Quit();
        return rc;
    }

    if (test_economy_mode) {
        int rc = game_economy_self_test();
        SDL_Quit();
        return rc;
    }

    Game game;
    if (game_init(&game, data_dir) < 0) {
        fprintf(stderr, "Failed to initialize game\n");
        SDL_Quit();
        return 1;
    }

    game_run(&game);
    game_shutdown(&game);

    return 0;
}
