#include "mw_arena.h"
#include "mw_combat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

/* MW_EXTENSION: a deliberately separate roguelike ruleset.  Colosseum
   records use COLOSSEUM#.SAV and never pass through WORLD's character,
   dungeon, monster, pitfall, or bestiary serializers. */

enum ArenaBoon {
    ARENA_BOON_HEAL = 0,
    ARENA_BOON_RESTORE,
    ARENA_BOON_VIGOR,
    ARENA_BOON_FOCUS,
    ARENA_BOON_MIGHT,
    ARENA_BOON_MIND,
    ARENA_BOON_SWIFTNESS,
    ARENA_BOON_WARD,
    ARENA_BOON_FURY,
    ARENA_BOON_REGEN,
    ARENA_BOON_RELIC,
    ARENA_BOON_COUNT
};

static const char *const rarity_names[ARENA_RARITY_COUNT] = {
    "COMMON", "UNCOMMON", "RARE", "EPIC", "LEGENDARY",
    "ULTRA RARE", "SUPER ULTRA RARE"
};

static const u8 rarity_colors[ARENA_RARITY_COUNT] = {
    7, 3, 8, 13, 4, 11, 14
};

static const int weapon_ladder[16] = {
    0, 4, 1, 5, 2, 6, 3, 7, 12, 13, 14, 15, 16, 17, 18, 19
};

static const int armor_ladder[16] = {
    0, 1, 2, 3, 4, 5, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15
};

static void arena_filename(char *out, size_t size, int slot) {
    snprintf(out, size, "COLOSSEUM%d.SAV", slot);
}

static u32 arena_random(Game *g, u32 limit) {
    if (!limit) return 0;
    u32 value = ((u32)game_rand(g) << 15) ^ (u32)game_rand(g);
    return value % limit;
}

static int arena_clamp_int64(long long value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return (int)value;
}

static void arena_add_stat(u16 *stat, int amount) {
    unsigned value = stat ? *stat : 0;
    if (!stat || amount <= 0) return;
    value += (unsigned)amount;
    if (value > MW_PLAYER_STAT_MAX) value = MW_PLAYER_STAT_MAX;
    *stat = (u16)value;
}

static void arena_add_float(float *value, double amount) {
    if (!value || amount <= 0.0) return;
    double current = isfinite(*value) && *value > 0.0f ? *value : 0.0;
    current += amount;
    if (current > MW_PLAYER_SP_MAX) current = MW_PLAYER_SP_MAX;
    *value = (float)current;
}

static void arena_add_perk(u16 *perk, int amount) {
    unsigned value;
    if (!perk || amount <= 0) return;
    value = (unsigned)*perk + (unsigned)amount;
    *perk = (u16)(value > UINT16_MAX ? UINT16_MAX : value);
}

static void arena_increment(u32 *value) {
    if (value && *value != UINT32_MAX) (*value)++;
}

static void arena_prepare_template(Character *p) {
    if (!p) return;
    mw_character_native_ensure(p);
    mw_set_experience_mode(p, MW_EXPERIENCE_ENHANCED);
    mw_set_quest_flags(p, mw_quest_flags(p) | MW_UNIVERSAL_ACCESS_FLAG);
    p->level = 1;
    p->experience = 0.0;
    p->equipped_weapon = 0;
    p->equipped_armor = 0;
    mw_set_weapon_inventory_count(p, 0, 1);
    mw_set_armor_inventory_count(p, 0, 1);
    p->raise_floor = 0;
    p->raise_x = 0xFFFFu;
    p->raise_y = 0;
    p->poisoned_turns = 0;
    p->diseased_turns = 0;
    character_clear_town_effects(p);
    character_clear_battle_effects(p);

    uint64_t hp = 50u + (uint64_t)p->stat_con * 3u + p->stat_luck;
    if (hp < mw_hp_max(p)) hp = mw_hp_max(p);
    mw_set_hp_max(p, hp);
    mw_set_hp_cur(p, hp);
    float sp = 12.0f + (float)(p->stat_int + p->stat_wis) / 2.0f;
    if (sp < p->sp_max) sp = p->sp_max;
    p->sp_max = p->sp_cur = sp;
}

static void arena_begin_new_run(ArenaSave *save) {
    save->character = save->base_character;
    arena_prepare_template(&save->character);
    arena_increment(&save->run_number);
    save->round = 1;
    save->current_streak = 0;
    memset(save->perk_levels, 0, sizeof(save->perk_levels));
    memset(save->rewards, 0, sizeof(save->rewards));
    save->enemy_type = 0;
    save->enemy_level = 0;
    save->enemy_hp = 0;
    save->enemy_max_hp = 0;
    save->enemy_asleep = 0;
    save->enemy_held = 0;
    save->enemy_stopped = 0;
    save->enemy_active = 0;
    save->enemy_champion = 0;
    save->pending_rewards = 0;
    save->in_run = 1;
}

void arena_initialize_save(ArenaSave *save, const Character *created) {
    if (!save || !created) return;
    memset(save, 0, sizeof(*save));
    save->magic = ARENA_SAVE_MAGIC;
    save->version = ARENA_SAVE_VERSION;
    save->record_size = (u32)sizeof(*save);
    save->base_character = *created;
    arena_prepare_template(&save->base_character);
    arena_begin_new_run(save);
}

int arena_load_save(Game *g, int slot, ArenaSave *save) {
    if (!g || !save || slot < 0 || slot >= MAX_PLAYERS) return -1;
    char name[32], path[300];
    arena_filename(name, sizeof(name), slot);
    game_make_path(g, path, sizeof(path), name);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    ArenaSave loaded;
    size_t read = fread(&loaded, 1, sizeof(loaded), f);
    fclose(f);
    if (read != sizeof(loaded) || loaded.magic != ARENA_SAVE_MAGIC ||
        loaded.version != ARENA_SAVE_VERSION ||
        loaded.record_size != sizeof(loaded))
        return -1;
    mw_character_native_ensure(&loaded.base_character);
    mw_character_native_ensure(&loaded.character);
    if (!loaded.base_character.name[0]) return -1;
    *save = loaded;
    return 0;
}

int arena_save_save(Game *g, int slot, ArenaSave *save) {
    if (!g || !save || slot < 0 || slot >= MAX_PLAYERS) return -1;
    mw_character_native_ensure(&save->base_character);
    mw_character_native_ensure(&save->character);
    save->magic = ARENA_SAVE_MAGIC;
    save->version = ARENA_SAVE_VERSION;
    save->record_size = (u32)sizeof(*save);
    char name[32], path[300];
    arena_filename(name, sizeof(name), slot);
    game_make_path(g, path, sizeof(path), name);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t written = fwrite(save, 1, sizeof(*save), f);
    fclose(f);
    return written == sizeof(*save) ? 0 : -1;
}

static int arena_roll_rarity(Game *g, const ArenaSave *save) {
    int luck = save->character.stat_luck;
    int bonus = luck / 32 + (int)save->current_streak * 3;
    if (bonus > 2200) bonus = 2200;
    int roll = (int)arena_random(g, 10000);
    roll = roll > bonus ? roll - bonus : 0;
    if (roll < 5) return ARENA_SUPER_ULTRA_RARE;
    if (roll < 25) return ARENA_ULTRA_RARE;
    if (roll < 100) return ARENA_LEGENDARY;
    if (roll < 400) return ARENA_EPIC;
    if (roll < 1200) return ARENA_RARE;
    if (roll < 3000) return ARENA_UNCOMMON;
    return ARENA_COMMON;
}

static int arena_ladder_index(Game *g, int enemy_level, int rarity) {
    static const int ahead[ARENA_RARITY_COUNT] = {0, 1, 2, 3, 5, 8, 15};
    int base = enemy_level * 15 / MAX_DUNGEON_FLOOR;
    int jump = ahead[rarity] ?
               (int)arena_random(g, (u32)ahead[rarity] + 1u) : 0;
    int tier = base + jump;
    if (tier > 15) tier = 15;
    return tier;
}

static int arena_reward_enchant(int enemy_level, int rarity) {
    long long value = enemy_level / 10 +
        (long long)(rarity + 1) * (enemy_level / 12 + 8);
    if (rarity == ARENA_SUPER_ULTRA_RARE)
        value += enemy_level + 500;
    return arena_clamp_int64(value, 0, INT16_MAX);
}

static ArenaReward arena_make_reward(Game *g, ArenaSave *save, int kind) {
    static const int spell_ahead[ARENA_RARITY_COUNT] = {0,1,2,3,5,8,14};
    ArenaReward reward;
    memset(&reward, 0, sizeof(reward));
    reward.kind = (u8)kind;
    reward.rarity = (u8)arena_roll_rarity(g, save);
    int enemy_level = save->enemy_level ? save->enemy_level : 1;

    if (kind == ARENA_REWARD_WEAPON) {
        int tier = arena_ladder_index(g, enemy_level, reward.rarity);
        reward.item = (u16)weapon_ladder[tier];
        reward.value = (u32)arena_reward_enchant(enemy_level, reward.rarity);
    } else if (kind == ARENA_REWARD_ARMOR) {
        int tier = arena_ladder_index(g, enemy_level, reward.rarity);
        reward.item = (u16)armor_ladder[tier];
        reward.value = (u32)arena_reward_enchant(enemy_level, reward.rarity);
    } else if (kind == ARENA_REWARD_SPELL) {
        int base_level = enemy_level * 14 / MAX_DUNGEON_FLOOR;
        int ahead = spell_ahead[reward.rarity];
        int level = base_level + (ahead ?
            (int)arena_random(g, (u32)ahead + 1u) : 0);
        if (level > 14) level = 14;
        reward.category = (u8)(SPELL_CAT_WIZARD + arena_random(g, 2));
        reward.item = (u16)(level * 3 + arena_random(g, 3));
        int source_roll = (int)arena_random(g, 100);
        reward.source = source_roll < 65 + reward.rarity * 4 ? 0 :
                        source_roll < 78 ? 1 :
                        source_roll < 92 ? 2 : 3;
        reward.amount = reward.source == 2 ?
            (u16)(3 + reward.rarity * 3 + arena_random(g, 5)) : 1;
    } else {
        int boon = (int)arena_random(g, ARENA_BOON_COUNT);
        if (reward.rarity < ARENA_ULTRA_RARE && boon == ARENA_BOON_RELIC)
            boon = ARENA_BOON_HEAL + (int)arena_random(g, ARENA_BOON_REGEN + 1);
        reward.item = (u16)boon;
        reward.amount = (u16)(reward.rarity + 1);
        reward.value = 5u + (u32)(reward.rarity + 1) * 5u;
    }
    return reward;
}

static void arena_generate_rewards(Game *g, ArenaSave *save) {
    for (int kind = 0; kind < ARENA_REWARD_COUNT; kind++)
        save->rewards[kind] = arena_make_reward(g, save, kind);
    save->pending_rewards = 1;
}

static const char *arena_spell_source_name(int source) {
    switch (source) {
    case 0: return "LEARN";
    case 1: return "SCROLL";
    case 2: return "WAND";
    default: return "PAPER";
    }
}

static const char *arena_boon_name(int boon) {
    static const char *const names[ARENA_BOON_COUNT] = {
        "HEALING DRAUGHT", "ARCANE REFILL", "VIGOR",
        "DEEPER RESERVES", "MIGHT", "MIND", "SWIFTNESS",
        "WARDING", "BATTLE FURY", "REGENERATION", "LOST RELIC"
    };
    return boon >= 0 && boon < ARENA_BOON_COUNT ? names[boon] : "MYSTERY";
}

static void arena_reward_text(const ArenaReward *reward, char *line,
                              size_t line_size, char *detail,
                              size_t detail_size) {
    int rarity = reward->rarity < ARENA_RARITY_COUNT ? reward->rarity : 0;
    if (reward->kind == ARENA_REWARD_WEAPON) {
        int slot = reward->item < WEAPON_STAT_COUNT ? reward->item : 0;
        snprintf(line, line_size, "%s WEAPON: %s", rarity_names[rarity],
                 weapon_stats[slot].name);
        snprintf(detail, detail_size, "PERMANENT ENCHANT +%u; AUTO-EQUIPS.",
                 (unsigned)reward->value);
    } else if (reward->kind == ARENA_REWARD_ARMOR) {
        int slot = reward->item < ARMOR_STAT_COUNT ? reward->item : 0;
        snprintf(line, line_size, "%s ARMOR: %s", rarity_names[rarity],
                 combat_armor_name(slot));
        snprintf(detail, detail_size, "PERMANENT ENCHANT +%u; AUTO-EQUIPS.",
                 (unsigned)reward->value);
    } else if (reward->kind == ARENA_REWARD_SPELL) {
        int category = reward->category <= SPELL_CAT_PRIEST ?
                       reward->category : SPELL_CAT_WIZARD;
        int spell = reward->item < MW_ENHANCED_SPELL_COUNT ? reward->item : 0;
        snprintf(line, line_size, "%s %s: %s", rarity_names[rarity],
                 arena_spell_source_name(reward->source),
                 combat_spell_name(category, spell));
        if (reward->source == 2)
            snprintf(detail, detail_size, "A WAND WITH %u CHARGES.",
                     reward->amount);
        else
            snprintf(detail, detail_size, "LEVEL %d %s BATTLE MAGIC.",
                     spell / 3 + 1,
                     category == SPELL_CAT_WIZARD ? "WIZARD" : "PRIEST");
    } else {
        snprintf(line, line_size, "%s BOON: %s", rarity_names[rarity],
                 arena_boon_name(reward->item));
        snprintf(detail, detail_size, "POWER TIER %u - APPLIED IMMEDIATELY.",
                 reward->amount);
    }
}

static void arena_apply_boon(ArenaSave *save, const ArenaReward *reward) {
    Character *p = &save->character;
    int power = reward->amount ? reward->amount : 1;
    switch (reward->item) {
    case ARENA_BOON_HEAL: {
        uint64_t heal = (uint64_t)mw_hp_max(p) * (25u + power * 10u) / 100u;
        uint64_t hp = (uint64_t)mw_hp_cur(p) + heal;
        mw_set_hp_cur(p, hp > mw_hp_max(p) ? mw_hp_max(p) : hp);
        if (power >= 4) p->poisoned_turns = p->diseased_turns = 0;
        break;
    }
    case ARENA_BOON_RESTORE:
        p->sp_cur += p->sp_max * (0.25f + power * 0.10f);
        if (p->sp_cur > p->sp_max) p->sp_cur = p->sp_max;
        break;
    case ARENA_BOON_VIGOR: {
        uint64_t gain = (uint64_t)mw_hp_max(p) * (5u + power * 3u) / 100u + 5u;
        mw_set_hp_max(p, (uint64_t)mw_hp_max(p) + gain);
        mw_set_hp_cur(p, (uint64_t)mw_hp_cur(p) + gain);
        arena_add_perk(&save->perk_levels[0], power);
        break;
    }
    case ARENA_BOON_FOCUS:
        arena_add_float(&p->sp_max, 3.0 + power * 3.0);
        p->sp_cur = p->sp_max;
        arena_add_perk(&save->perk_levels[1], power);
        break;
    case ARENA_BOON_MIGHT:
        arena_add_stat(&p->stat_str, power * 3);
        arena_add_stat(&p->stat_con, power * 2);
        arena_add_perk(&save->perk_levels[2], power);
        break;
    case ARENA_BOON_MIND:
        arena_add_stat(&p->stat_int, power * 3);
        arena_add_stat(&p->stat_wis, power * 3);
        arena_add_perk(&save->perk_levels[3], power);
        break;
    case ARENA_BOON_SWIFTNESS:
        arena_add_stat(&p->stat_agi, power * 3);
        arena_add_stat(&p->stat_luck, power * 2);
        arena_add_perk(&save->perk_levels[4], power);
        break;
    case ARENA_BOON_WARD:
        mw_set_body_armor_plus(p, mw_body_armor_plus(p) + power * 4);
        mw_set_ring_prot_plus(p, mw_ring_prot_plus(p) + power * 3);
        arena_add_perk(&save->perk_levels[5], power);
        break;
    case ARENA_BOON_FURY:
        p->combat_bonus = p->combat_bonus > 255 - power ?
                          255 : (u8)(p->combat_bonus + power);
        mw_set_gauntlet(p, mw_gauntlet(p) + power * 3);
        arena_add_perk(&save->perk_levels[6], power);
        break;
    case ARENA_BOON_REGEN:
        p->ring_regen = p->ring_regen > 255 - power ?
                        255 : (u8)(p->ring_regen + power);
        arena_add_perk(&save->perk_levels[7], power);
        break;
    case ARENA_BOON_RELIC:
        mw_set_relic_owned(p, power % MW_RELIC_COUNT, 1);
        break;
    }
}

static void arena_apply_reward(ArenaSave *save, const ArenaReward *reward) {
    Character *p = &save->character;
    if (reward->kind == ARENA_REWARD_WEAPON) {
        int slot = reward->item < WEAPON_STAT_COUNT ? reward->item : 0;
        mw_set_weapon_inventory_count(p, slot, 1);
        if ((int)reward->value > mw_weapon_enchant(p, slot))
            mw_set_weapon_enchant(p, slot, (int)reward->value);
        p->equipped_weapon = (u8)slot;
    } else if (reward->kind == ARENA_REWARD_ARMOR) {
        int slot = reward->item < ARMOR_STAT_COUNT ? reward->item : 0;
        mw_set_armor_inventory_count(p, slot, 1);
        if ((int)reward->value > mw_armor_enchant(p, slot))
            mw_set_armor_enchant(p, slot, (int)reward->value);
        p->equipped_armor = (u8)slot;
    } else if (reward->kind == ARENA_REWARD_SPELL) {
        int category = reward->category <= SPELL_CAT_PRIEST ?
                       reward->category : SPELL_CAT_WIZARD;
        int spell = reward->item < MW_ENHANCED_SPELL_COUNT ? reward->item : 0;
        if (reward->source == 0) {
            if (p->spells[category][spell]) {
                int level = spell / 3 + 1;
                arena_add_float(&p->sp_max,
                                (double)level * (reward->rarity + 1));
                p->sp_cur = p->sp_max;
            } else {
                p->spells[category][spell] = 1;
                int level = spell / 3 + 1;
                if (p->sp_max < level) p->sp_max = (float)level;
                if (p->sp_cur < level) p->sp_cur = (float)level;
            }
        } else if (reward->source == 1) {
            if (p->scrolls[category][spell] != UINT8_MAX)
                p->scrolls[category][spell]++;
        } else if (reward->source == 2) {
            unsigned charges = p->wands[category][spell] + reward->amount;
            p->wands[category][spell] =
                (u8)(charges > UINT8_MAX ? UINT8_MAX : charges);
        } else if (p->papers[category][spell] != UINT8_MAX) {
            p->papers[category][spell]++;
        }
    } else {
        arena_apply_boon(save, reward);
    }
}

static void arena_advance_character(Game *g, ArenaSave *save) {
    Character *p = &save->character;
    if (p->level < MW_PLAYER_LEVEL_MAX) p->level++;
    uint64_t hp_gain = 5u + p->stat_con / 3u + arena_random(g, 5);
    mw_set_hp_max(p, (uint64_t)mw_hp_max(p) + hp_gain);
    arena_add_float(&p->sp_max,
                    2.0 + (double)(p->stat_int + p->stat_wis) / 30.0);
    uint64_t hp = (uint64_t)mw_hp_cur(p) + mw_hp_max(p) / 8u + 1u;
    mw_set_hp_cur(p, hp > mw_hp_max(p) ? mw_hp_max(p) : hp);
    p->sp_cur += p->sp_max / 8.0f + 1.0f;
    if (p->sp_cur > p->sp_max) p->sp_cur = p->sp_max;
}

static int arena_enemy_type(Game *g, int level, int champion) {
    if (champion) {
        int candidates[MONSTER_TYPE_COUNT];
        int count = 0;
        for (int type = 0; type < MONSTER_TYPE_COUNT; type++)
            if (combat_monster_type_spawnable(type) &&
                monster_types[type].boss &&
                combat_monster_type_valid(type, level))
                candidates[count++] = type;
        if (count) return candidates[arena_random(g, (u32)count)];
    }
    for (int attempt = 0; attempt < 64; attempt++) {
        int type = combat_pick_monster_type(g, level);
        if (combat_monster_type_spawnable(type) &&
            (!monster_types[type].boss || champion))
            return type;
    }
    return combat_pick_monster_type(g, level);
}

static void arena_generate_enemy(Game *g, ArenaSave *save) {
    int threat = 1 + (int)save->current_streak * 3;
    if (threat < save->character.level) threat = save->character.level;
    if (threat > MAX_DUNGEON_FLOOR) threat = MAX_DUNGEON_FLOOR;
    int low = threat * 85 / 100;
    int high = threat * 115 / 100 + 2;
    if (low < 1) low = 1;
    if (high > MAX_DUNGEON_FLOOR) high = MAX_DUNGEON_FLOOR;
    if (high < low) high = low;
    int level = low + (int)arena_random(g, (u32)(high - low + 1));
    int champion = save->round > 0 && save->round % 10 == 0;
    int type = arena_enemy_type(g, level, champion);
    const MonsterType *mt = &monster_types[type];
    uint64_t range = (uint64_t)(mt->hpF > 0 ? mt->hpF : 1) * (unsigned)level;
    if (range > INT_MAX) range = INT_MAX;
    uint64_t hp = 1u + arena_random(g, (u32)range);
    if (mt->boss) hp += (uint64_t)level * 20u;
    if (champion) hp = hp * 3u / 2u + (uint64_t)level * 5u;
    if (hp > INT_MAX) hp = INT_MAX;
    save->enemy_type = (u16)type;
    save->enemy_level = (u16)level;
    save->enemy_hp = save->enemy_max_hp = (u32)hp;
    save->enemy_asleep = save->enemy_held = save->enemy_stopped = 0;
    save->enemy_active = 1;
    save->enemy_champion = (u8)champion;
}

static void arena_sync_game(Game *g, const ArenaSave *save) {
    g->arena_active = 1;
    g->arena_round = save->round;
    g->arena_streak = save->current_streak;
    g->arena_best = save->best_streak;
    g->arena_champion = save->enemy_champion;
    g->cur_floor = save->enemy_level ? save->enemy_level : 1;
    g->dungeon_max_floor = MAX_DUNGEON_FLOOR;
    g->last_move_dir = 0;
    g->active_save_slot = -1;
}

static void arena_state_from_combat(ArenaSave *save, const CombatState *cs) {
    save->enemy_type = (u16)cs->monster_type_idx;
    save->enemy_level = (u16)(cs->monster_level < 1 ? 1 : cs->monster_level);
    save->enemy_hp = cs->monster_hp > 0 ? (u32)cs->monster_hp : 0;
    save->enemy_max_hp = cs->monster_max_hp > 0 ?
                         (u32)cs->monster_max_hp : save->enemy_max_hp;
    save->enemy_asleep = (u16)(cs->monster_asleep > 0 ? cs->monster_asleep : 0);
    save->enemy_held = (u16)(cs->monster_held > 0 ? cs->monster_held : 0);
    save->enemy_stopped = (u16)(cs->monster_stopped > 0 ? cs->monster_stopped : 0);
    save->enemy_active = cs->monster_hp > 0 && !cs->fled;
}

static CombatState arena_combat_from_state(const ArenaSave *save) {
    CombatState cs;
    memset(&cs, 0, sizeof(cs));
    cs.active = 1;
    cs.entity_index = -1;
    cs.monster_type_idx = save->enemy_type < MONSTER_TYPE_COUNT ?
                          save->enemy_type : 0;
    cs.monster_level = save->enemy_level ? save->enemy_level : 1;
    cs.monster_hp = save->enemy_hp > INT_MAX ? INT_MAX : (int)save->enemy_hp;
    cs.monster_max_hp = save->enemy_max_hp > INT_MAX ?
                        INT_MAX : (int)save->enemy_max_hp;
    cs.monster_asleep = save->enemy_asleep;
    cs.monster_held = save->enemy_held;
    cs.monster_stopped = save->enemy_stopped;
    return cs;
}

static void arena_palette(Game *g) {
    video_load_vga_default_palette(&g->video);
    video_set_palette(&g->video, 1, 35, 25, 65);
    video_set_palette(&g->video, 5, 180, 66, 30);
    video_set_palette(&g->video, 6, 205, 138, 45);
    video_set_palette(&g->video, 7, 185, 175, 155);
    video_set_palette(&g->video, 8, 35, 235, 65);
    video_set_palette(&g->video, 13, 185, 55, 220);
    video_set_palette(&g->video, 14, 255, 205, 45);
}

static int arena_confirm(Game *g, const char *title, const char *detail) {
    arena_palette(g);
    video_clear(&g->video, 0);
    video_fill_rect(&g->video, 80, 180, LOGICAL_W - 160, 300, 1);
    video_draw_text_scaled(&g->video, 120, 220, title, 14, 3, 4);
    video_draw_text_scaled(&g->video, 120, 290, detail, 15, 3, 4);
    video_draw_text_scaled(&g->video, 120, 390, "Y) YES       N) NO", 8, 3, 4);
    video_present(&g->video);
    for (;;) {
        int key = input_wait_any_key(&g->input);
        if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
        if (key == 'Y') return 1;
        if (key == 'N' || key == 0x1B || input_poll_quit(&g->input)) return 0;
    }
}

static void arena_draw_help(Game *g) {
    arena_palette(g);
    video_clear(&g->video, 0);
    video_draw_text(&g->video, 28, 20, "COLOSSEUM RULES", 8);
    const char *const lines[] = {
        "F  FIGHT             C  CAST BATTLE MAGIC",
        "I  USE ITEM          W/A  SELECT WEAPON/ARMOR",
        "V  COMBATANT SHEET   S  SAVE RUN",
        "Q/ESC  SAVE AND RETURN TO CHARACTER SELECTION",
        "X  RETIRE THIS RUN (CAREER RECORDS REMAIN)",
        "",
        "EVERY VICTORY OFFERS FOUR RANDOM REWARDS:",
        "WEAPON, ARMOR, MAGIC, OR A PERK/RECOVERY BOON.",
        "RARITIES RUN FROM COMMON TO SUPER ULTRA RARE.",
        "HIGH RARITY CAN JUMP FAR AHEAD OF THE NORMAL GEAR",
        "OR SPELL LADDER. EVERY TENTH ROUND IS A CHAMPION.",
        "",
        "DEATH ENDS THE CURRENT RUN. YOUR BEST STREAK AND",
        "TOTAL VICTORIES REMAIN IN THIS COLOSSEUM SLOT.",
        "ADVENTURE SAVES ARE NEVER OPENED OR MODIFIED."
    };
    int y = 78;
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++, y += 38)
        video_draw_text_scaled(&g->video, 28, y, lines[i], i < 5 ? 14 : 15, 3, 4);
    video_draw_text(&g->video, 28, 730, "HIT ANY KEY...", 8);
    video_present(&g->video);
    input_wait_any_key(&g->input);
}

static void arena_draw_stats(Game *g, const ArenaSave *save) {
    const Character *p = &save->character;
    arena_palette(g);
    video_clear(&g->video, 0);
    char line[128];
    video_draw_text(&g->video, 28, 18, "COLOSSEUM COMBATANT", 8);
    snprintf(line, sizeof(line), "%s  RUN %u  ROUND %u  BEST %u",
             p->name, save->run_number, save->round, save->best_streak);
    video_draw_text_scaled(&g->video, 28, 62, line, 14, 3, 4);
    snprintf(line, sizeof(line), "LEVEL %u   HP %u/%u   SP %.0f/%.0f", p->level,
             mw_hp_cur(p), mw_hp_max(p), p->sp_cur, p->sp_max);
    video_draw_text_scaled(&g->video, 28, 110, line, 15, 3, 4);
    snprintf(line, sizeof(line), "STR %u  INT %u  WIS %u  CON %u  AGI %u  LUCK %u",
             p->stat_str, p->stat_int, p->stat_wis, p->stat_con,
             p->stat_agi, p->stat_luck);
    video_draw_text_scaled(&g->video, 28, 150, line, 15, 3, 4);
    snprintf(line, sizeof(line), "WEAPON: %s +%d",
             p->equipped_weapon < WEAPON_STAT_COUNT ?
             weapon_stats[p->equipped_weapon].name : "UNKNOWN",
             mw_weapon_enchant(p, p->equipped_weapon));
    video_draw_text_scaled(&g->video, 28, 210, line, 4, 3, 4);
    snprintf(line, sizeof(line), "ARMOR: %s +%d",
             combat_armor_name(p->equipped_armor),
             mw_armor_enchant(p, p->equipped_armor));
    video_draw_text_scaled(&g->video, 28, 250, line, 4, 3, 4);
    static const char *const perk_name[ARENA_PERK_COUNT] = {
        "VIGOR", "FOCUS", "MIGHT", "MIND", "SWIFTNESS", "WARD",
        "FURY", "REGENERATION"
    };
    video_draw_text(&g->video, 28, 315, "PERKS EARNED THIS RUN", 8);
    int y = 360;
    for (int i = 0; i < ARENA_PERK_COUNT; i += 2, y += 42) {
        snprintf(line, sizeof(line), "%-16s %u       %-16s %u",
                 perk_name[i], save->perk_levels[i], perk_name[i + 1],
                 save->perk_levels[i + 1]);
        video_draw_text_scaled(&g->video, 28, y, line, 15, 3, 4);
    }
    snprintf(line, sizeof(line), "CAREER: %u VICTORIES, %u DEATHS",
             save->total_victories, save->total_deaths);
    video_draw_text_scaled(&g->video, 28, 570, line, 11, 3, 4);
    video_draw_text(&g->video, 28, 730, "HIT ANY KEY...", 8);
    video_present(&g->video);
    input_wait_any_key(&g->input);
}

static int arena_choose_reward(Game *g, ArenaSave *save) {
    for (;;) {
        arena_palette(g);
        video_clear(&g->video, 0);
        char line[160], detail[160], header[128];
        snprintf(header, sizeof(header),
                 "ROUND %u WON - CHOOSE ONE REWARD", save->round);
        video_draw_text(&g->video, 24, 18, header, 8);
        video_draw_text_scaled(&g->video, 24, 58,
            "THE OTHER THREE VANISH WHEN YOU CHOOSE.", 15, 3, 4);
        for (int i = 0; i < ARENA_REWARD_COUNT; i++) {
            const ArenaReward *reward = &save->rewards[i];
            arena_reward_text(reward, line, sizeof(line), detail, sizeof(detail));
            int y = 120 + i * 135;
            video_fill_rect(&g->video, 20, y, LOGICAL_W - 40, 112, 1);
            snprintf(header, sizeof(header), "%d) %s", i + 1, line);
            video_draw_text_scaled(&g->video, 40, y + 14, header,
                rarity_colors[reward->rarity < ARENA_RARITY_COUNT ?
                              reward->rarity : 0], 3, 4);
            video_draw_text_scaled(&g->video, 64, y + 62, detail, 15, 3, 4);
        }
        video_draw_text(&g->video, 24, 720,
                        "1-4 CHOOSE   Q/ESC SAVE REWARD FOR LATER", 14);
        video_present(&g->video);
        int key = input_wait_any_key(&g->input);
        if (key == INPUT_MOUSE_CLICK) {
            int x, y;
            key = 0;
            if (game_mouse_click_logical(g, &x, &y) && x >= 20 &&
                x < LOGICAL_W - 20 && y >= 120 && y < 660) {
                int row = (y - 120) / 135;
                if (row >= 0 && row < ARENA_REWARD_COUNT) key = '1' + row;
            }
        }
        if (key >= '1' && key <= '4') return key - '1';
        if (key == 'q' || key == 'Q' || key == 0x1B ||
            input_poll_quit(&g->input)) return -1;
    }
}

static void arena_show_death(Game *g, ArenaSave *save) {
    arena_palette(g);
    video_clear(&g->video, 0);
    char line[128];
    video_draw_text_scaled(&g->video, 210, 150,
                           "THE CROWD FALLS SILENT.", 12, 1, 1);
    video_draw_text_scaled(&g->video, 250, 235,
                           "YOUR RUN IS OVER.", 14, 1, 1);
    snprintf(line, sizeof(line), "FINAL STREAK: %u    CAREER BEST: %u",
             save->current_streak, save->best_streak);
    video_draw_text_scaled(&g->video, 190, 340, line, 15, 3, 4);
    video_draw_text_scaled(&g->video, 280, 470,
                           "PRESS ANY KEY...", 8, 3, 4);
    video_present(&g->video);
    input_wait_any_key(&g->input);
}

static void arena_victory(Game *g, int slot, ArenaSave *save) {
    save->enemy_active = 0;
    arena_increment(&save->current_streak);
    arena_increment(&save->total_victories);
    if (save->current_streak > save->best_streak)
        save->best_streak = save->current_streak;
    arena_advance_character(g, save);
    arena_generate_rewards(g, save);
    arena_save_save(g, slot, save);
}

void arena_run(Game *g, int slot, ArenaSave *save) {
    if (!g || !save || slot < 0 || slot >= MAX_PLAYERS) return;
    g->active_save_slot = -1;
    if (!save->in_run) {
        if (!arena_confirm(g, "BEGIN A NEW COLOSSEUM RUN?",
                           "DEATH RESET THE PREVIOUS RUN'S BUILD."))
            return;
        arena_begin_new_run(save);
        arena_save_save(g, slot, save);
    }

    while (!input_poll_quit(&g->input) && save->in_run) {
        if (save->pending_rewards) {
            int choice = arena_choose_reward(g, save);
            if (choice < 0) break;
            arena_apply_reward(save, &save->rewards[choice]);
            save->pending_rewards = 0;
            arena_increment(&save->round);
            save->character.eff_hold_monster = 0;
            save->character.eff_stop_monster = 0;
            save->character.eff_slow_mon = 0;
            arena_save_save(g, slot, save);
            continue;
        }
        if (!save->enemy_active) {
            arena_generate_enemy(g, save);
            arena_save_save(g, slot, save);
        }
        arena_sync_game(g, save);
        CombatState cs = arena_combat_from_state(save);
        game_draw_combat_overlay(g, &save->character, -1,
            cs.monster_type_idx, cs.monster_level, cs.monster_hp,
            save->enemy_champion ? "A CHAMPION ENTERS THE SAND!" :
                                   "A NEW CHALLENGER ENTERS!",
            "FIGHT, CAST, OR PREPARE YOUR GEAR.", "");
        video_present(&g->video);

        int key = input_getch(&g->input);
        if (key == 0) {
            (void)input_getch(&g->input);
            continue;
        }
        if (key == 'q' || key == 'Q' || key == 0x1B) {
            if (arena_confirm(g, "SAVE AND LEAVE THE COLOSSEUM?",
                              "YOUR CURRENT ENEMY AND BUILD WILL WAIT.")) {
                arena_state_from_combat(save, &cs);
                arena_save_save(g, slot, save);
                break;
            }
            continue;
        }
        if (key == 'x' || key == 'X') {
            if (arena_confirm(g, "RETIRE THIS RUN?",
                              "THE BUILD ENDS; CAREER RECORDS REMAIN.")) {
                save->in_run = 0;
                save->enemy_active = 0;
                save->pending_rewards = 0;
                arena_save_save(g, slot, save);
                break;
            }
            continue;
        }
        if (key == 'h' || key == 'H') {
            arena_draw_help(g);
            continue;
        }
        if (key == 'v' || key == 'V') {
            arena_draw_stats(g, save);
            continue;
        }
        if (key == 'w' || key == 'W') {
            cmd_weapons(g, &save->character);
            arena_save_save(g, slot, save);
            continue;
        }
        if (key == 'a' || key == 'A') {
            cmd_armor(g, &save->character);
            arena_save_save(g, slot, save);
            continue;
        }
        if (key == 's' || key == 'S') {
            arena_state_from_combat(save, &cs);
            arena_save_save(g, slot, save);
            continue;
        }

        int action = -1;
        if (key == 'f' || key == 'F') action = COMBAT_ACTION_FIGHT;
        else if (key == 'c' || key == 'C') action = COMBAT_ACTION_CAST;
        else if (key == 'i' || key == 'I') action = COMBAT_ACTION_ITEM;
        else if (key == 't' || key == 'T') action = COMBAT_ACTION_WAIT;
        if (action < 0) continue;

        if (!combat_take_turn(g, &cs, &save->character, action)) continue;
        arena_state_from_combat(save, &cs);
        if (!mw_hp_cur(&save->character)) {
            arena_increment(&save->total_deaths);
            save->in_run = 0;
            save->enemy_active = 0;
            save->pending_rewards = 0;
            if (save->current_streak > save->best_streak)
                save->best_streak = save->current_streak;
            arena_save_save(g, slot, save);
            arena_show_death(g, save);
            break;
        }
        if (cs.player_fled) {
            save->in_run = 0;
            save->enemy_active = 0;
            save->pending_rewards = 0;
            arena_save_save(g, slot, save);
            break;
        }
        if (cs.monster_hp <= 0 || cs.fled) {
            arena_victory(g, slot, save);
            continue;
        }
        arena_save_save(g, slot, save);
    }
    g->arena_active = 0;
    g->arena_champion = 0;
    g->active_save_slot = -1;
    arena_palette(g);
}

int arena_self_test(void) {
    int failures = 0;
    Character created;
    memset(&created, 0, sizeof(created));
    strcpy(created.name, "ARENA TEST");
    created.stat_str = created.stat_int = created.stat_wis = 20;
    created.stat_con = created.stat_agi = created.stat_luck = 20;
    created.hp_cur = created.hp_max = 40;
    created.sp_cur = created.sp_max = 5.0f;
    ArenaSave save;
    arena_initialize_save(&save, &created);
    if (save.magic != ARENA_SAVE_MAGIC || save.record_size != sizeof(save) ||
        !save.in_run || save.round != 1 || save.run_number != 1 ||
        mw_experience_mode(&save.character) != MW_EXPERIENCE_ENHANCED ||
        !mw_universal_access(&save.character) || mw_hp_cur(&save.character) < 40)
        failures++;

    /* Game owns a full 1024x768 software framebuffer.  Keeping a second Game
       on this nested self-test's stack overflowed Windows' default stack when
       game_ui_self_test already had its caller's Game alive. */
    Game *g = (Game *)calloc(1, sizeof(*g));
    if (!g) return failures + 1;
    game_srand(g, 0xC011055u);
    save.enemy_level = 500;
    arena_generate_rewards(g, &save);
    for (int i = 0; i < ARENA_REWARD_COUNT; i++)
        if (save.rewards[i].kind != i ||
            save.rewards[i].rarity >= ARENA_RARITY_COUNT)
            failures++;
    ArenaReward weapon = {ARENA_REWARD_WEAPON, ARENA_EPIC, 0, 0, 19, 0, 777};
    arena_apply_reward(&save, &weapon);
    if (save.character.equipped_weapon != 19 ||
        !mw_weapon_inventory_count(&save.character, 19) ||
        mw_weapon_enchant(&save.character, 19) != 777)
        failures++;
    ArenaReward armor = {ARENA_REWARD_ARMOR, ARENA_RARE, 0, 0, 15, 0, 555};
    arena_apply_reward(&save, &armor);
    if (save.character.equipped_armor != 15 ||
        !mw_armor_inventory_count(&save.character, 15) ||
        mw_armor_enchant(&save.character, 15) != 555)
        failures++;
    ArenaReward spell = {ARENA_REWARD_SPELL, ARENA_LEGENDARY,
                         SPELL_CAT_WIZARD, 0, 43, 1, 0};
    arena_apply_reward(&save, &spell);
    if (!save.character.spells[SPELL_CAT_WIZARD][43]) failures++;
    u32 before_hp = mw_hp_max(&save.character);
    ArenaReward vigor = {ARENA_REWARD_BOON, ARENA_RARE, 0, 0,
                         ARENA_BOON_VIGOR, 3, 0};
    arena_apply_reward(&save, &vigor);
    if (mw_hp_max(&save.character) <= before_hp || !save.perk_levels[0])
        failures++;
    save.perk_levels[0] = UINT16_MAX - 1;
    arena_apply_reward(&save, &vigor);
    if (save.perk_levels[0] != UINT16_MAX) failures++;
    {
        char first[32], last[32];
        arena_filename(first, sizeof(first), 0);
        arena_filename(last, sizeof(last), MAX_PLAYERS - 1);
        if (strcmp(first, "COLOSSEUM0.SAV") ||
            strcmp(last, "COLOSSEUM9.SAV"))
            failures++;
    }
    for (int level = 1; level <= MAX_DUNGEON_FLOOR; level += 37) {
        int tier = arena_ladder_index(g, level, ARENA_SUPER_ULTRA_RARE);
        if (tier < 0 || tier > 15 ||
            weapon_ladder[tier] == 8 || weapon_ladder[tier] == 9 ||
            weapon_ladder[tier] == 10 || weapon_ladder[tier] == 11)
            failures++;
    }
    free(g);
    return failures;
}
