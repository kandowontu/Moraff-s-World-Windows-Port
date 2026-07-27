#include "mw_game.h"
#include "mw_combat.h"
#include "mw_trainer.h"
#include "mw_wilderness.h"
#include "mw_model_viewer.h"
#include "mw_integrity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Native launcher/test harness. It replaces original startup plumbing and
 * the anti-copy routine func_08BDF; gameplay coverage starts in game_run's
 * implementation of WORLD func_0F6E5. See PORT_STATUS.md. */

#ifdef _WIN32
#include <direct.h>
#define change_dir _chdir
#else
#include <unistd.h>
#define change_dir chdir
#endif

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static int has_game_data(const char *dir) {
    static const char *required[] = {
        "DUNG.BIN", "WORLDMAP.BIN", "H.BIN", "WORLD.PIC", "WALL.PIC",
        "360X480.FNT", "320X200.FNT", "ROLL.TXT"
    };
    char test[300];
    for (int i = 0; i < (int)(sizeof(required) / sizeof(required[0])); i++) {
        snprintf(test, sizeof(test), "%s/%s", dir, required[i]);
        if (!file_exists(test)) return 0;
    }
    return 1;
}

static void use_local_game_directory(char *out, int out_sz) {
    /* The native port is self-contained: resources and saves live beside the
     * executable.  Change there once, then keep every game file reference
     * local and relative (./DUNG.BIN, ./WORLD.PIC, save slots, and so on). */
    char *base = SDL_GetBasePath();
    if (base) {
        if (change_dir(base) != 0)
            fprintf(stderr, "WARNING: Could not select the executable directory.\n");
        SDL_free(base);
    }
    snprintf(out, out_sz, ".");
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
    int spawnable_count = 0;
    for (int i = 0; i < MONSTER_TYPE_COUNT; i++) {
        int pic = get_monster_pic_index_ext(i);
        int spawnable = combat_monster_type_spawnable(i);
        if (spawnable) {
            spawnable_count++;
            if (pic < 2 || pic >= game.world_pic_count) failures++;
            if (pic >= 0 && pic < 256) seen_pic[pic] = 1;
            game.bestiary_kills[i] = (u32)(i + 1);
        } else if (pic >= 0) {
            failures++;
        }
        if (get_monster_color_ext(i) < 0) failures++;
    }
    if (spawnable_count != BESTIARY_CATALOG_COUNT) failures++;
    if (combat_monster_type_spawnable(6) ||
        combat_monster_type_valid(6, 50)) failures++; /* dormant Hobbit row */
    for (int pic = 2; pic < game.world_pic_count; pic++)
        if (!seen_pic[pic]) failures++;
    failures += game_ui_self_test(&game);
    if (selected < 0) selected = 0;
    if (selected >= MONSTER_TYPE_COUNT) selected = MONSTER_TYPE_COUNT - 1;
    /* The trainer unlock is distinct from fabricated kill counts. */
    memset(game.bestiary_kills, 0, sizeof(game.bestiary_kills));
    game.bestiary_unlock_all = 1;
    game_draw_bestiary_test(&game, selected);
    char bmp_path[300];
    snprintf(bmp_path, sizeof(bmp_path), "%s/test_bestiary_%d.bmp",
             data_dir, selected);
    int rc = save_framebuffer_bmp(&game.video, bmp_path);
    printf("Beastiary spawn/picture/color coverage: %s (%d failures)\n",
           failures ? "FAIL" : "PASS", failures);
    if (rc == 0) printf("Saved: %s\n", bmp_path);
    game_shutdown(&game);
    return (failures || rc < 0) ? 1 : 0;
}

static int run_test_trainer(const char *data_dir) {
    Game game;
    if (game_init(&game, data_dir) < 0) return 1;
    Character dummy;
    memset(&dummy, 0, sizeof(dummy));
    strncpy(dummy.name, "TRAINER TEST", sizeof(dummy.name) - 1);
    dummy.race = RACE_ELF;
    dummy.sex = 1;
    dummy.class_id = CLASS_WIZARD;
    dummy.level = 42;
    dummy.hp_cur = 1234;
    dummy.hp_max = 2345;
    dummy.sp_cur = 3456.0f;
    dummy.sp_max = 4567.0f;
    dummy.age = 42u * MW_AGE_YEAR_UNITS + 123u * MW_AGE_DAY_UNITS;
    dummy.x_pos = 31;
    dummy.y_pos = 44;
    dummy.floor_depth = 7;
    dummy.stat_str = 101;
    dummy.stat_int = 202;
    dummy.stat_wis = 303;
    dummy.stat_con = 404;
    dummy.stat_agi = 505;
    dummy.stat_luck = 606;
    game.cur_x = dummy.x_pos;
    game.cur_y = dummy.y_pos;
    game.cur_floor = dummy.floor_depth;

    /* Let trainer_run paint its initial page, then close it without requiring
       interactive input so CI can verify the actual rendering path. */
    game.input.keys[game.input.tail] = 0x1B;
    game.input.tail = (game.input.tail + 1) % KEY_QUEUE_SIZE;
    trainer_run(&game, &dummy);
    char bmp_path[300];
    snprintf(bmp_path, sizeof(bmp_path),
             "%s/test_trainer_stats.bmp", data_dir);
    int failures = 0;
    if (save_framebuffer_bmp(&game.video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);
    failures += trainer_self_test();

    mw_set_experience_mode(&dummy, MW_EXPERIENCE_ENHANCED);
    for (int relic = 0; relic < MW_RELIC_COUNT; relic++)
        mw_set_relic_owned(&dummy, relic, 1);
    dummy.native.relic_phoenix_cooldown = 187;
    trainer_draw_stats_test(&game, &dummy, 49);
    snprintf(bmp_path, sizeof(bmp_path),
             "%s/test_trainer_relics.bmp", data_dir);
    if (save_framebuffer_bmp(&game.video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < MW_ENHANCED_SPELL_COUNT; col++) {
            dummy.spells[row][col] = (u8)(((row + col) % 4) == 0);
            dummy.scrolls[row][col] = (u8)(((row * 2 + col) % 5) == 0);
            dummy.wands[row][col] = (u8)((row + col) % 10);
            dummy.papers[row][col] = (u8)(((row + col * 2) % 7) == 0);
        }
    }
    trainer_draw_grid_test(&game, &dummy, 2, SPELL_CAT_PREPARATION, 34);
    snprintf(bmp_path, sizeof(bmp_path), "%s/test_trainer.bmp", data_dir);
    if (save_framebuffer_bmp(&game.video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);

    for (int tier = 0; tier < 8; tier++) {
        mw_set_weapon_inventory_count(&dummy, 12 + tier, tier + 1);
        mw_set_weapon_enchant(&dummy, 12 + tier, 200 + tier * 125);
        mw_set_armor_inventory_count(&dummy, 8 + tier, tier + 1);
        mw_set_armor_enchant(&dummy, 8 + tier, 150 + tier * 100);
    }
    trainer_draw_equipment_test(&game, &dummy, 31);
    snprintf(bmp_path, sizeof(bmp_path),
             "%s/test_trainer_equipment.bmp", data_dir);
    if (save_framebuffer_bmp(&game.video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);

    /* Classic mode must not render or select any deep-magic or native gear
       slot even when a stale Enhanced cursor is supplied by the test. */
    mw_set_experience_mode(&dummy, MW_EXPERIENCE_CLASSIC);
    trainer_draw_stats_test(&game, &dummy, 49);
    snprintf(bmp_path, sizeof(bmp_path),
             "%s/test_trainer_classic_stats.bmp", data_dir);
    if (save_framebuffer_bmp(&game.video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);
    trainer_draw_grid_test(&game, &dummy, 2, SPELL_CAT_PREPARATION,
                           MW_ENHANCED_SPELL_COUNT - 1);
    snprintf(bmp_path, sizeof(bmp_path),
             "%s/test_trainer_classic_magic.bmp", data_dir);
    if (save_framebuffer_bmp(&game.video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);
    trainer_draw_equipment_test(&game, &dummy, 31);
    snprintf(bmp_path, sizeof(bmp_path),
             "%s/test_trainer_classic_equipment.bmp", data_dir);
    if (save_framebuffer_bmp(&game.video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);
    mw_set_experience_mode(&dummy, MW_EXPERIENCE_ENHANCED);
    trainer_draw_equipment_test(&game, &dummy, 31);
    snprintf(bmp_path, sizeof(bmp_path),
             "%s/test_trainer_enhanced_final.bmp", data_dir);
    if (save_framebuffer_bmp(&game.video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);

    game.active_save_slot = 3;
    game.dungeon_number = 24680;
    game.bestiary_kills[0] = 12;
    game.bestiary_kills[1] = 34;
    game.cheat_god_mode = 1;
    game_draw_game_stats_test(&game, &dummy);
    snprintf(bmp_path, sizeof(bmp_path), "%s/test_game_stats.bmp", data_dir);
    if (save_framebuffer_bmp(&game.video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);
    game.active_save_slot = -1;
    game_shutdown(&game);
    return failures ? 1 : 0;
}

static int run_test_wilderness(const char *data_dir) {
    int failures = wilderness_self_test();
    Game game;
    if (game_init(&game, data_dir) < 0) return failures + 1;
    Character p;
    memset(&p, 0, sizeof(p));
    snprintf(p.name, sizeof(p.name), "WILDERNESS TEST");
    p.level = 12;
    p.hp_cur = p.hp_max = 100;
    p.sp_cur = p.sp_max = 50.0f;
    p.stat_str = 20;
    p.jewels_pocket = 25000;
    wilderness_draw_test(&game, &p);
    char bmp_path[300];
    snprintf(bmp_path, sizeof(bmp_path), "%s/test_wilderness.bmp", data_dir);
    if (save_framebuffer_bmp(&game.video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);
    game_shutdown(&game);
    return failures;
}

static int run_test_model_viewer(const char *data_dir) {
    Game game;
    int failures;
    char bmp_path[300];
    if (game_init(&game, data_dir) < 0) return 1;

    failures = model_viewer_self_test(&game);
    model_viewer_draw_test(&game, MODEL_VIEWER_WORLD,
                           game.world_pic_count > 2 ? 2 : 0,
                           1.15f, 17.5f);
    snprintf(bmp_path, sizeof(bmp_path), "%s/test_model_viewer.bmp", data_dir);
    if (save_framebuffer_bmp(&game.video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);
    printf("Graphics/model viewer coverage: %s (%d failures)\n",
           failures ? "FAIL" : "PASS", failures);
    game_shutdown(&game);
    return failures ? 1 : 0;
}

static int run_test_magic(const char *data_dir) {
    int failures = combat_self_test();
    Game *game = calloc(1, sizeof(*game));
    if (!game) return 1;
    if (game_init(game, data_dir) < 0) {
        free(game);
        return 1;
    }

    Character dummy;
    memset(&dummy, 0, sizeof(dummy));
    strncpy(dummy.name, "SPELL TEST", sizeof(dummy.name) - 1);
    dummy.class_id = CLASS_WIZARD;
    dummy.level = 40;
    dummy.hp_cur = dummy.hp_max = 500;
    dummy.sp_cur = dummy.sp_max = 999.0f;
    dummy.floor_depth = 1;
    dummy.x_pos = 19;
    dummy.y_pos = 20;
    mw_set_experience_mode(&dummy, MW_EXPERIENCE_ENHANCED);
    for (int category = 0; category < 4; category++)
        for (int index = 0; index < MW_ENHANCED_SPELL_COUNT; index++)
            dummy.spells[category][index] = 1;
    game->cur_floor = dummy.floor_depth;
    game->cur_x = dummy.x_pos;
    game->cur_y = dummy.y_pos;
    game_update_visibility(game);

    /* Choose preparation spells, then leave the all-level selector visible
       for the screenshot without casting or changing the test character. */
    game->input.keys[game->input.tail] = '2';
    game->input.tail = (game->input.tail + 1) % KEY_QUEUE_SIZE;
    game->input.keys[game->input.tail] = 0x1B;
    game->input.tail = (game->input.tail + 1) % KEY_QUEUE_SIZE;
    cmd_cast_spell_menu(game, &dummy, NULL);

    char bmp_path[300];
    snprintf(bmp_path, sizeof(bmp_path), "%s/test_spell_selector.bmp", data_dir);
    if (save_framebuffer_bmp(&game->video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);

    /* Enhanced adds a second page for levels 11-14 without changing the
       original selector page or its hotkeys. */
    game->input.keys[game->input.tail] = '2';
    game->input.tail = (game->input.tail + 1) % KEY_QUEUE_SIZE;
    game->input.keys[game->input.tail] = 0;
    game->input.tail = (game->input.tail + 1) % KEY_QUEUE_SIZE;
    game->input.keys[game->input.tail] = 0x51;
    game->input.tail = (game->input.tail + 1) % KEY_QUEUE_SIZE;
    game->input.keys[game->input.tail] = 0x1B;
    game->input.tail = (game->input.tail + 1) % KEY_QUEUE_SIZE;
    cmd_cast_spell_menu(game, &dummy, NULL);
    snprintf(bmp_path, sizeof(bmp_path),
             "%s/test_deep_spell_selector.bmp", data_dir);
    if (save_framebuffer_bmp(&game->video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);

    /* Cast the longest preparation-result notice and retain its final frame.
       This guards the original upper-left pane against horizontal runoff. */
    game->input.keys[game->input.tail] = '2';
    game->input.tail = (game->input.tail + 1) % KEY_QUEUE_SIZE;
    game->input.keys[game->input.tail] = 0;
    game->input.tail = (game->input.tail + 1) % KEY_QUEUE_SIZE;
    game->input.keys[game->input.tail] = 0x51;
    game->input.tail = (game->input.tail + 1) % KEY_QUEUE_SIZE;
    game->input.keys[game->input.tail] = 'H';
    game->input.tail = (game->input.tail + 1) % KEY_QUEUE_SIZE;
    game->input.keys[game->input.tail] = ' ';
    game->input.tail = (game->input.tail + 1) % KEY_QUEUE_SIZE;
    cmd_cast_spell_menu(game, &dummy, NULL);
    snprintf(bmp_path, sizeof(bmp_path),
             "%s/test_spell_notice_wrap.bmp", data_dir);
    if (save_framebuffer_bmp(&game->video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);

    /* Capture the original shop_magic-sized USE ITEM dispatcher and both
       non-spell child pages for visual regression review. */
    for (int page = 0; page < 3; page++) {
        static const char *const names[3] = {
            "test_use_item_menu.bmp",
            "test_use_item_pills.bmp",
            "test_use_item_other.bmp"
        };
        game_draw_use_item_test(game, &dummy, NULL, page);
        snprintf(bmp_path, sizeof(bmp_path), "%s/%s", data_dir, names[page]);
        if (save_framebuffer_bmp(&game->video, bmp_path) < 0) failures++;
        else printf("Saved: %s\n", bmp_path);
    }

    /* Exercise both equipment pages with a mix of collected and hidden
       entries.  The selector must consume only Escape/PageDown—not a normal
       command waiting behind it. */
    mw_set_experience_mode(&dummy, MW_EXPERIENCE_ENHANCED);
    dummy.weapon_inventory[0] = 1;
    dummy.weapon_inventory[1] = 1;
    dummy.weapon_inventory[4] = 1;
    mw_set_weapon_enchant(&dummy, 4, 3);
    game_draw_exploration(game, &dummy);
    game->input.keys[game->input.tail] = 0x1B;
    game->input.tail = (game->input.tail + 1) % KEY_QUEUE_SIZE;
    cmd_weapons(game, &dummy);
    snprintf(bmp_path, sizeof(bmp_path),
             "%s/test_weapon_selector.bmp", data_dir);
    if (save_framebuffer_bmp(&game->video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);

    mw_set_weapon_inventory_count(&dummy, 12, 1);
    mw_set_weapon_inventory_count(&dummy, 14, 1);
    game_draw_exploration(game, &dummy);
    game->input.keys[game->input.tail] = 0;
    game->input.tail = (game->input.tail + 1) % KEY_QUEUE_SIZE;
    game->input.keys[game->input.tail] = 0x51;
    game->input.tail = (game->input.tail + 1) % KEY_QUEUE_SIZE;
    game->input.keys[game->input.tail] = 0x1B;
    game->input.tail = (game->input.tail + 1) % KEY_QUEUE_SIZE;
    cmd_weapons(game, &dummy);
    snprintf(bmp_path, sizeof(bmp_path),
             "%s/test_deep_weapon_selector.bmp", data_dir);
    if (save_framebuffer_bmp(&game->video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);

    CombatState one_action = {0};
    one_action.active = 1;
    one_action.entity_index = -1;
    one_action.monster_type_idx = 0;
    one_action.monster_level = 1;
    one_action.monster_hp = one_action.monster_max_hp = 1000000;
    game->cheat_god_mode = 1;
    game->input.keys[game->input.tail] = 'v';
    game->input.tail = (game->input.tail + 1) % KEY_QUEUE_SIZE;
    combat_run(game, &one_action, &dummy);
    if (!input_kbhit(&game->input) ||
        input_getch(&game->input) != 'v') {
        fprintf(stderr,
                "COMBAT TEST FAIL: one-action combat consumed a main-loop key\n");
        failures++;
    }
    for (int relic = 0; relic < MW_RELIC_COUNT; relic++)
        mw_set_relic_owned(&dummy, relic, 1);
    dummy.ring_regen = 4;
    mw_set_ring_prot_plus(&dummy, 125);
    dummy.antimagic_ring = 5;
    mw_set_body_armor_plus(&dummy, 300);
    mw_set_gauntlet(&dummy, 275);
    dummy.native.relic_regen_phase = 2;
    dummy.native.relic_phoenix_cooldown = 187;
    game_draw_exploration(game, &dummy);
    game_draw_effects_test(game, &dummy, 2);
    snprintf(bmp_path, sizeof(bmp_path),
             "%s/test_effects_3.bmp", data_dir);
    if (save_framebuffer_bmp(&game->video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);
    failures += game_dialog_ui_self_test(game, &dummy);
    snprintf(bmp_path, sizeof(bmp_path),
             "%s/test_relic_pockets.bmp", data_dir);
    if (save_framebuffer_bmp(&game->video, bmp_path) < 0) failures++;
    else printf("Saved: %s\n", bmp_path);
    game->active_save_slot = -1;
    game_shutdown(game);
    free(game);
    return failures ? 1 : 0;
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
    int dormant_index = -1;
    for (int i = 0; i < MONSTERS_PER_FLOOR; i++)
        if (i != index && game_monster_hp(&game, i) > 0) {
            dormant_index = i;
            break;
        }
    if (dormant_index < 0) { game_shutdown(&game); return 2; }
    int hp = game_monster_hp(&game, index);
    game_set_monster_hp(&game, index, hp - 1);
    game.monster_map[game.monster_layer][dormant_index].type = 6;
    game.bestiary_kills[0] = 11;
    game.bestiary_kills[6] = 77;
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
    int dormant_type = game.monster_map[game.monster_layer][dormant_index].type;
    int dormant_purged = dormant_type != 6 &&
                         combat_monster_type_valid(dormant_type, floor);
    int bestiary_compact = game.bestiary_kills[0] == 11 &&
                           game.bestiary_kills[6] == 0;
    printf("Dormant MON.MAP record migration: %s\n",
           dormant_purged ? "PASS" : "FAIL");
    printf("Compact Beastiary save migration: %s\n",
           bestiary_compact ? "PASS" : "FAIL");
    if (!dormant_purged || !bestiary_compact) {
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
    int pit_target = floor;
    for (int y = 1; ok && y < MAP_H - 1 && pit_x < 0; y++) {
        for (int x = 1; x < MAP_W - 1; x++) {
            game.cur_floor = floor;
            game.cur_x = x;
            game.cur_y = y;
            dummy.floor_depth = (u16)floor;
            if (game_apply_pitfall(&game, &dummy)) {
                pit_x = x; pit_y = y;
                pit_target = game.cur_floor;
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
        int marked = game_is_known_pitfall(&game, pit_x, pit_y);
        int repeated = game_apply_pitfall(&game, &dummy);
        int same_target = game.cur_floor == pit_target;
        printf("Discovered pitfall map tracking: %s\n", marked ? "PASS" : "FAIL");
        printf("Discovered pitfall retrigger: %s\n",
               repeated && same_target ? "PASS" : "FAIL");
        if (!marked || !repeated || !same_target) ok = 0;
        game_shutdown(&game);
    } else if (ok) {
        printf("Discovered pitfall tracking: SKIP (no chute on test floor)\n");
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
    int test_trainer_mode = 0;
    int test_wilderness_mode = 0;
    int test_model_viewer_mode = 0;
    int test_x = 19, test_y = 20, test_floor = 787;
    int test_monster_type = 57; /* default: ball */

    /* Parse args */
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
        } else if (strcmp(argv[i], "--test-trainer") == 0) {
            test_trainer_mode = 1;
        } else if (strcmp(argv[i], "--test-wilderness") == 0) {
            test_wilderness_mode = 1;
        } else if (strcmp(argv[i], "--test-model-viewer") == 0) {
            test_model_viewer_mode = 1;
        }
    }

    use_local_game_directory(data_dir, sizeof(data_dir));

    printf("Moraff's World - Native Port\n");
    printf("Data directory: %s\n", data_dir);

    if (!has_game_data(data_dir)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
            "Moraff's World files missing",
            "Required original Moraff's World data files are missing beside the program.",
            NULL);
        SDL_Quit();
        return 1;
    }

    char integrity_error[512];
    if (!integrity_verify_original_executables(data_dir, integrity_error,
                                               sizeof(integrity_error))) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
            "Original game verification failed", integrity_error, NULL);
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

    if (test_trainer_mode) {
        int rc = run_test_trainer(data_dir);
        SDL_Quit();
        return rc;
    }

    if (test_wilderness_mode) {
        int rc = run_test_wilderness(data_dir);
        SDL_Quit();
        return rc;
    }

    if (test_model_viewer_mode) {
        int rc = run_test_model_viewer(data_dir);
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
        int rc = run_test_magic(data_dir);
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
