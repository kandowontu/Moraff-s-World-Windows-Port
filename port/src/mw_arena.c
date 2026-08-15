#include "mw_arena.h"
#include "mw_combat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <errno.h>

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

static void arena_roll_rewards(Game *g, ArenaSave *save);

/* Equipment damage/defense and enchantment accuracy multiply one another in
   WORLD's combat formula.  These explicit gates keep a lucky draft exciting
   without turning a round-20 combatant into a floor-1,000 character. */
static const u16 arena_tier_round[16] = {
    1, 2, 4, 6, 8, 10, 12, 15, 20, 27, 35, 45, 58, 72, 88, 108
};

static int arena_base_ladder_index(unsigned round) {
    int tier = 0;
    if (!round) round = 1;
    for (int i = 1; i < 16 && round >= arena_tier_round[i]; i++)
        tier = i;
    return tier;
}

static int arena_enchant_cap(unsigned round) {
    int cap = 5 + (int)round / 3;
    return cap > 120 ? 120 : cap;
}

static int arena_max_ladder_index(unsigned round, int armor) {
    int base = arena_base_ladder_index(round);
    int tier = base + 4;
    if (tier > 15) tier = 15;
    int base_power = armor ? combat_armor_defense(armor_ladder[base]) :
                             weapon_stats[weapon_ladder[base]].maxDmg;
    int power_cap = armor ? base_power * 5 + 10 : base_power * 4 + 8;
    while (tier > base) {
        int power = armor ? combat_armor_defense(armor_ladder[tier]) :
                            weapon_stats[weapon_ladder[tier]].maxDmg;
        if (power <= power_cap) break;
        tier--;
    }
    return tier;
}

static int arena_base_spell_level(unsigned round) {
    static const u16 level_round[15] = {
        1, 3, 6, 9, 12, 15, 19, 23, 28, 34, 45, 58, 72, 88, 108
    };
    int level = 1;
    if (!round) round = 1;
    for (int i = 1; i < 15 && round >= level_round[i]; i++)
        level = i + 1;
    return level;
}

/* Player levels earn periodic bonus steps, so a threat curve equal to round
   inevitably becomes easier the longer a run survives.  Preserve rounds
   1-10 exactly, then accelerate in two gradual bands.  The ceiling follows
   the expanded player-level limit rather than dungeon depth: monster species
   still use the closest valid dungeon floor, while their combat level keeps
   scaling for long Colosseum runs. */
static int arena_threat_level(unsigned round) {
    uint64_t threat = round ? round : 1u;
    if (round > 10u) threat += (round - 10u) / 2u;
    if (round > 80u) threat += (round - 80u) / 5u;
    if (threat > MW_PLAYER_LEVEL_MAX) threat = MW_PLAYER_LEVEL_MAX;
    return (int)threat;
}

static unsigned arena_endurance_percent(unsigned round) {
    uint64_t endurance;
    if (round <= 4u)
        endurance = 70u + (uint64_t)round * 7u;
    else if (round < 10u)
        endurance = 98u + ((uint64_t)round - 4u) * 8u;
    else {
        endurance = 120u + (round > 190u ? 380u : (uint64_t)round * 2u);
        /* Gear and enchantment growth become multiplicative in the late
           draft. Add a slower endurance band after round 25 so later foes
           survive enough exchanges to use their own attacks and spells. */
        if (round > 25u) endurance += (uint64_t)round - 25u;
        if (endurance > 750u) endurance = 750u;
    }
    return (unsigned)endurance;
}

enum {
    ARENA_SKIP_COST_PERCENT = 20,
    ARENA_CHAMPION_SKIP_COST_PERCENT = 35
};

const char *arena_difficulty_name(int difficulty) {
    static const char *const names[ARENA_DIFFICULTY_COUNT] = {
        "EASY", "NORMAL", "HARD"
    };
    return difficulty >= 0 && difficulty < ARENA_DIFFICULTY_COUNT ?
           names[difficulty] : "NORMAL";
}

static int arena_difficulty(const ArenaSave *save) {
    return save && save->difficulty < ARENA_DIFFICULTY_COUNT ?
           save->difficulty : ARENA_DIFFICULTY_NORMAL;
}

static void arena_filename(char *out, size_t size, int slot) {
    snprintf(out, size, "COLOSSEUM%d.SAV", slot);
}

static u32 arena_random(Game *g, u32 limit) {
    if (!limit) return 0;
    u32 value = ((u32)game_rand(g) << 15) ^ (u32)game_rand(g);
    return value % limit;
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

static void arena_restore_percent(Character *p, unsigned hp_percent,
                                  unsigned sp_percent) {
    uint64_t hp = (uint64_t)mw_hp_cur(p) +
                  (uint64_t)mw_hp_max(p) * hp_percent / 100u;
    mw_set_hp_cur(p, hp > mw_hp_max(p) ? mw_hp_max(p) : hp);
    p->sp_cur += p->sp_max * (float)sp_percent / 100.0f;
    if (p->sp_cur > p->sp_max) p->sp_cur = p->sp_max;
}

static void arena_sanitize_magic(Character *p) {
    if (!p) return;
    for (int category = 0; category < SPELL_TYPES; category++) {
        for (int spell = 0; spell < MW_ENHANCED_SPELL_COUNT; spell++) {
            if (combat_spell_arena_eligible(category, spell)) continue;
            p->spells[category][spell] = 0;
            p->scrolls[category][spell] = 0;
            p->wands[category][spell] = 0;
            p->papers[category][spell] = 0;
        }
    }
}

static void arena_prepare_template(Character *p) {
    if (!p) return;
    mw_character_native_ensure(p);
    mw_set_experience_mode(p, MW_EXPERIENCE_ENHANCED);
    mw_set_quest_flags(p, mw_quest_flags(p) | MW_UNIVERSAL_ACCESS_FLAG);
    p->level = 1;
    p->experience = 0.0;
    /* A fist-and-skin opening made the first draft feel mandatory: weak
       combatants could lose a run before their build had begun.  Every new
       Colosseum run now starts with the two universal tier-one items. */
    p->equipped_weapon = 1; /* Stick */
    p->equipped_armor = 1;  /* Leather */
    mw_set_weapon_inventory_count(p, 0, 1);
    mw_set_armor_inventory_count(p, 0, 1);
    mw_set_weapon_inventory_count(p, 1, 1);
    mw_set_armor_inventory_count(p, 1, 1);
    p->raise_floor = 0;
    p->raise_x = 0xFFFFu;
    p->raise_y = 0;
    p->poisoned_turns = 0;
    p->diseased_turns = 0;
    character_clear_town_effects(p);
    character_clear_battle_effects(p);
    arena_sanitize_magic(p);

    uint64_t hp = 50u + (uint64_t)p->stat_con * 3u + p->stat_luck;
    if (hp < mw_hp_max(p)) hp = mw_hp_max(p);
    mw_set_hp_max(p, hp);
    mw_set_hp_cur(p, hp);
    float sp = 12.0f + (float)(p->stat_int + p->stat_wis) / 2.0f;
    if (sp < p->sp_max) sp = p->sp_max;
    p->sp_max = p->sp_cur = sp;
}

/* Version 4 eases only the opening four rounds.  Rescue an in-progress
   version-3 run from the old opening curve without granting anything beyond
   the new starter equipment or carrying that help into the later game. */
static void arena_migrate_opening_v4(ArenaSave *save) {
    if (!save) return;
    arena_prepare_template(&save->base_character);
    if (!save->in_run || save->round > 4u) return;

    Character *p = &save->character;
    mw_set_weapon_inventory_count(p, 1, 1);
    mw_set_armor_inventory_count(p, 1, 1);
    if (p->equipped_weapon == 0) p->equipped_weapon = 1;
    if (p->equipped_armor == 0) p->equipped_armor = 1;
    uint64_t rescue = (uint64_t)mw_hp_max(p) * 3u / 4u;
    if (mw_hp_cur(p) < rescue) mw_set_hp_cur(p, rescue);
    p->poisoned_turns = 0;
    p->diseased_turns = 0;
    if (save->enemy_active) {
        save->enemy_active = 0;
        save->enemy_hp = save->enemy_max_hp = 0;
        save->enemy_asleep = save->enemy_held = save->enemy_stopped = 0;
    }
}

/* Version 5 replaces the remaining attrition-heavy onboarding curve.  Old
   runs already tagged version 4 otherwise retain their nearly-empty HP and
   the opponent rolled under that curve, so refresh only the first six rounds
   once when they are loaded. */
static void arena_migrate_opening_v5(ArenaSave *save) {
    if (!save || !save->in_run || save->round > 6u) return;
    Character *p = &save->character;
    mw_set_weapon_inventory_count(p, 1, 1);
    mw_set_armor_inventory_count(p, 1, 1);
    if (p->equipped_weapon == 0) p->equipped_weapon = 1;
    if (p->equipped_armor == 0) p->equipped_armor = 1;
    mw_set_hp_cur(p, mw_hp_max(p));
    p->sp_cur = p->sp_max;
    p->poisoned_turns = 0;
    p->diseased_turns = 0;
    save->enemy_active = 0;
    save->enemy_hp = save->enemy_max_hp = 0;
    save->enemy_asleep = save->enemy_held = save->enemy_stopped = 0;
}

/* Version 6 gives the combatant one genuine bonus level every four wins.
   Opponents now follow the round ladder rather than feeding those bonus
   levels back into their own strength, so restore that earned advantage for
   runs created under the previous curve. */
static void arena_migrate_progression_v6(ArenaSave *save) {
    if (!save || !save->in_run) return;
    unsigned wins = save->current_streak;
    unsigned minimum = 1u + wins + wins / 4u + wins / 10u;
    if (minimum > MW_PLAYER_LEVEL_MAX) minimum = MW_PLAYER_LEVEL_MAX;
    if (save->character.level < minimum)
        save->character.level = (u16)minimum;
}

/* Version 7 slightly accelerates the combatant curve after further live
   playtesting.  This is deliberately only one extra level every third win;
   enemies remain tied to arena round, so the bonus is not canceled out. */
static void arena_migrate_progression_v7(ArenaSave *save) {
    if (!save || !save->in_run) return;
    unsigned wins = save->current_streak;
    unsigned minimum = 1u + wins + wins / 3u + wins / 10u;
    if (minimum > MW_PLAYER_LEVEL_MAX) minimum = MW_PLAYER_LEVEL_MAX;
    if (save->character.level < minimum)
        save->character.level = (u16)minimum;
}

/* Version 1's compressed draft curve allowed late-game weapon dice and
   three-digit enchantments to compound into five-digit damage before round
   20.  Keep the run and career record, but move an existing build back into
   the version-2 power budget. */
static void arena_migrate_balance_v2(ArenaSave *save) {
    Character *p = &save->character;
    const Character *base = &save->base_character;
    unsigned wins = save->current_streak;
    unsigned round = save->round ? save->round : 1;
    unsigned level_cap = 1u + wins + wins / 10u + wins / 5u;
    unsigned stat_cap_bonus = 3u + wins / 2u;
    int enchant_cap = arena_enchant_cap(round);
    int max_weapon_tier = arena_max_ladder_index(round, 0);
    int max_armor_tier = arena_max_ladder_index(round, 1);

    if (p->level > level_cap) p->level = (u16)level_cap;
    u16 *stats[6] = {&p->stat_str, &p->stat_int, &p->stat_wis,
                     &p->stat_con, &p->stat_agi, &p->stat_luck};
    const u16 base_stats[6] = {base->stat_str, base->stat_int, base->stat_wis,
                               base->stat_con, base->stat_agi, base->stat_luck};
    for (int i = 0; i < 6; i++) {
        unsigned cap = (unsigned)base_stats[i] + stat_cap_bonus;
        if (*stats[i] > cap) *stats[i] = (u16)cap;
    }

    uint64_t old_hp_max = mw_hp_max(p);
    uint64_t hp_budget = (uint64_t)mw_hp_max(base) +
        (uint64_t)wins * (8u + base->stat_con / 5u) +
        (uint64_t)wins * wins / 6u;
    hp_budget += hp_budget / 4u; /* leave room for legitimate Vigor drafts */
    if (old_hp_max > hp_budget) {
        uint64_t hp = old_hp_max ?
            (uint64_t)mw_hp_cur(p) * hp_budget / old_hp_max : hp_budget;
        mw_set_hp_max(p, hp_budget);
        mw_set_hp_cur(p, hp ? hp : 1);
    }
    float sp_budget = base->sp_max +
        (float)wins * (3.0f + (float)(base->stat_int + base->stat_wis) / 24.0f);
    sp_budget = sp_budget * 1.25f + 10.0f;
    if (p->sp_max > sp_budget) {
        float ratio = p->sp_max > 0.0f ? p->sp_cur / p->sp_max : 1.0f;
        p->sp_max = sp_budget;
        p->sp_cur = sp_budget * ratio;
    }

    for (int tier = 0; tier < 16; tier++) {
        int weapon = weapon_ladder[tier];
        int armor = armor_ladder[tier];
        if (mw_weapon_enchant(p, weapon) > enchant_cap)
            mw_set_weapon_enchant(p, weapon, enchant_cap);
        if (mw_armor_enchant(p, armor) > enchant_cap)
            mw_set_armor_enchant(p, armor, enchant_cap);
        if (tier > max_weapon_tier) {
            mw_set_weapon_inventory_count(p, weapon, 0);
            mw_set_weapon_enchant(p, weapon, 0);
        }
        if (tier > max_armor_tier) {
            mw_set_armor_inventory_count(p, armor, 0);
            mw_set_armor_enchant(p, armor, 0);
        }
    }
    if (!mw_weapon_inventory_count(p, p->equipped_weapon)) {
        p->equipped_weapon = (u8)weapon_ladder[max_weapon_tier];
        mw_set_weapon_inventory_count(p, p->equipped_weapon, 1);
        mw_set_weapon_enchant(p, p->equipped_weapon, enchant_cap);
    }
    if (!mw_armor_inventory_count(p, p->equipped_armor)) {
        p->equipped_armor = (u8)armor_ladder[max_armor_tier];
        mw_set_armor_inventory_count(p, p->equipped_armor, 1);
        mw_set_armor_enchant(p, p->equipped_armor, enchant_cap);
    }

    int defense_cap = 3 + (int)round / 5;
    if (mw_body_armor_plus(p) > defense_cap)
        mw_set_body_armor_plus(p, defense_cap);
    if (mw_ring_prot_plus(p) > defense_cap)
        mw_set_ring_prot_plus(p, defense_cap);
    if (mw_gauntlet(p) > defense_cap) mw_set_gauntlet(p, defense_cap);
    if (p->combat_bonus > 1u + round / 10u)
        p->combat_bonus = (u8)(1u + round / 10u);
    if (p->ring_regen > 1u + round / 20u)
        p->ring_regen = (u8)(1u + round / 20u);
    for (int i = 0; i < ARENA_PERK_COUNT; i++)
        if (save->perk_levels[i] > 12) save->perk_levels[i] = 12;

    /* Preserve an unclaimed draft, but normalize cards rolled under the old
       tables so loading immediately before a pick cannot bypass migration. */
    static const int spell_ahead[ARENA_RARITY_COUNT] = {0, 0, 1, 1, 1, 2, 3};
    for (int i = 0; i < ARENA_REWARD_COUNT; i++) {
        ArenaReward *reward = &save->rewards[i];
        int rarity = reward->rarity < ARENA_RARITY_COUNT ? reward->rarity : 0;
        if (reward->kind == ARENA_REWARD_WEAPON) {
            int tier = 0;
            for (int candidate = 0; candidate < 16; candidate++)
                if (weapon_ladder[candidate] == reward->item) tier = candidate;
            if (tier > max_weapon_tier) tier = max_weapon_tier;
            reward->item = (u16)weapon_ladder[tier];
            if (reward->value > (u32)enchant_cap) reward->value = (u32)enchant_cap;
        } else if (reward->kind == ARENA_REWARD_ARMOR) {
            int tier = 0;
            for (int candidate = 0; candidate < 16; candidate++)
                if (armor_ladder[candidate] == reward->item) tier = candidate;
            if (tier > max_armor_tier) tier = max_armor_tier;
            reward->item = (u16)armor_ladder[tier];
            if (reward->value > (u32)enchant_cap) reward->value = (u32)enchant_cap;
        } else if (reward->kind == ARENA_REWARD_SPELL) {
            int category = reward->category == SPELL_CAT_PRIEST ?
                           SPELL_CAT_PRIEST : SPELL_CAT_WIZARD;
            int level = arena_base_spell_level(round) + spell_ahead[rarity];
            if (level > 15) level = 15;
            if (reward->item / 3 + 1 > level ||
                !combat_spell_arena_eligible(category, reward->item)) {
                int replacement = 0;
                for (int spell = level * 3 - 1; spell >= 0; spell--)
                    if (combat_spell_arena_eligible(category, spell)) {
                        replacement = spell;
                        break;
                    }
                reward->category = (u8)category;
                reward->item = (u16)replacement;
            }
        }
    }

    character_clear_battle_effects(p);
    save->enemy_active = 0;
    save->enemy_hp = save->enemy_max_hp = 0;
    save->enemy_asleep = save->enemy_held = save->enemy_stopped = 0;
    save->version = ARENA_SAVE_VERSION;
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
    save->difficulty = ARENA_DIFFICULTY_NORMAL;
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
    memset(&loaded, 0, sizeof(loaded));
    size_t read = fread(&loaded, 1, sizeof(loaded), f);
    fclose(f);
    /* Versions 1-6 ended after four reward cards (4812 bytes).  Version 7+
       appends three more cards; because every earlier field retains its
       offset, a zero-filled partial read is a lossless conversion. */
    int legacy_size = loaded.version <= 6u && read == 4812u &&
                      loaded.record_size == 4812u;
    int current_size = read == sizeof(loaded) &&
                       loaded.record_size == sizeof(loaded);
    if ((!legacy_size && !current_size) || loaded.magic != ARENA_SAVE_MAGIC ||
        (loaded.version < 1u || loaded.version > ARENA_SAVE_VERSION) ||
        loaded.record_size != read)
        return -1;
    mw_character_native_ensure(&loaded.base_character);
    mw_character_native_ensure(&loaded.character);
    if (!loaded.base_character.name[0]) return -1;
    /* Sanitize battle-ineligible magic on every load. Version 1 records also
       receive the one-time balance migration above. */
    arena_sanitize_magic(&loaded.base_character);
    arena_sanitize_magic(&loaded.character);
    u32 source_version = loaded.version;
    if (source_version == 1u) arena_migrate_balance_v2(&loaded);
    if (source_version < 3u)
        loaded.difficulty = ARENA_DIFFICULTY_NORMAL;
    if (source_version < 4u) arena_migrate_opening_v4(&loaded);
    if (source_version < 5u) arena_migrate_opening_v5(&loaded);
    if (source_version < 6u) {
        arena_migrate_progression_v6(&loaded);
        /* Cards saved under the old tables may already be owned.  Replace
           the whole draft once so a resumed run receives the same no-repeat
           guarantee as a newly generated reward screen. */
        if (loaded.pending_rewards) arena_roll_rewards(g, &loaded);
    }
    if (source_version < 7u) {
        arena_migrate_progression_v7(&loaded);
        if (loaded.pending_rewards) arena_roll_rewards(g, &loaded);
    }
    if (source_version < 8u) {
        /* Do not strand a resumed run inside an opponent generated by the
           old HP/accuracy tables (or an active Puffball).  Preserve the round
           and character, then roll a corrected challenger on entry. */
        loaded.enemy_active = 0;
        loaded.enemy_hp = loaded.enemy_max_hp = 0;
        loaded.enemy_asleep = loaded.enemy_held = loaded.enemy_stopped = 0;
        loaded.enemy_champion = 0;
        if (loaded.pending_rewards) arena_roll_rewards(g, &loaded);
    }
    if (source_version < 9u) {
        /* Version 9 accelerates opponents only after the first champion.
           Discard an encounter rolled on the old flattened curve; the run,
           build, round, streak, and pending draft remain untouched. */
        loaded.enemy_active = 0;
        loaded.enemy_hp = loaded.enemy_max_hp = 0;
        loaded.enemy_asleep = loaded.enemy_held = loaded.enemy_stopped = 0;
        loaded.enemy_champion = 0;
    }
    loaded.version = ARENA_SAVE_VERSION;
    *save = loaded;
    return 0;
}

int arena_save_save(Game *g, int slot, ArenaSave *save) {
    if (!g || !save || slot < 0 || slot >= MAX_PLAYERS) return -1;
    mw_character_native_ensure(&save->base_character);
    mw_character_native_ensure(&save->character);
    if (save->difficulty >= ARENA_DIFFICULTY_COUNT)
        save->difficulty = ARENA_DIFFICULTY_NORMAL;
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

int arena_delete_save(Game *g, int slot) {
    if (!g || slot < 0 || slot >= MAX_PLAYERS) return -1;
    char name[32], path[300];
    arena_filename(name, sizeof(name), slot);
    game_make_path(g, path, sizeof(path), name);
    if (remove(path) == 0 || errno == ENOENT) return 0;
    return -1;
}

static int arena_roll_rarity(Game *g, const ArenaSave *save) {
    int luck = save->character.stat_luck;
    int bonus = luck / 48 + (int)save->current_streak * 5;
    if (arena_difficulty(save) == ARENA_DIFFICULTY_HARD) bonus += 150;
    if (bonus > 1000) bonus = 1000;
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

static int arena_ladder_index(Game *g, const ArenaSave *save, int rarity,
                              int armor) {
    static const int ahead_min[ARENA_RARITY_COUNT] = {0, 0, 0, 1, 1, 2, 3};
    static const int ahead_max[ARENA_RARITY_COUNT] = {0, 1, 2, 3, 4, 4, 5};
    unsigned round = save->round ? save->round : 1;
    int base = arena_base_ladder_index(round);
    int minimum = ahead_min[rarity];
    int maximum_jump = ahead_max[rarity];
    int jump = minimum + (int)arena_random(
        g, (u32)(maximum_jump - minimum + 1));
    int tier = base + jump;
    int maximum = arena_max_ladder_index(round, armor);
    /* Yellow/Super Ultra Rare gear is the jackpot tier.  It may cross the
       ordinary power-ratio gate by several ladder steps instead of rolling
       the same equipment currently available from common drafts. */
    if (rarity == ARENA_SUPER_ULTRA_RARE) {
        int jackpot_maximum = base + 5;
        if (jackpot_maximum > 15) jackpot_maximum = 15;
        if (maximum < jackpot_maximum) maximum = jackpot_maximum;
    }
    if (tier > maximum) tier = maximum;
    return tier;
}

static int arena_reward_enchant(const ArenaSave *save, int rarity) {
    unsigned round = save->round ? save->round : 1;
    static const int rarity_bonus[ARENA_RARITY_COUNT] = {0, 1, 3, 5, 7, 10, 15};
    int value = 1 + (int)round / 8 + rarity_bonus[rarity];
    int cap = arena_enchant_cap(round);
    return value > cap ? cap : value;
}

static int arena_has_magic(const Character *p, int category, int spell,
                           int source) {
    if (!p || category < 0 || category >= SPELL_TYPES || spell < 0 ||
        spell >= MW_ENHANCED_SPELL_COUNT)
        return 1;
    if (source == 0)
        return (p->class_id == CLASS_MONK &&
                spell < MW_ORIGINAL_SPELL_COUNT) ||
               p->spells[category][spell] != 0;
    if (source == 1) return p->scrolls[category][spell] != 0;
    if (source == 2) return p->wands[category][spell] != 0;
    return p->papers[category][spell] != 0;
}

static int arena_reward_is_magic(const ArenaReward *reward) {
    return reward && reward->kind >= ARENA_REWARD_SPELL &&
           reward->kind <= ARENA_REWARD_PAPER;
}

static int arena_spell_name_in_draft(const ArenaReward *prior,
                                     int prior_count, int category,
                                     int spell) {
    const char *name = combat_spell_name(category, spell);
    for (int i = 0; i < prior_count; i++) {
        if (!arena_reward_is_magic(&prior[i])) continue;
        if (!strcmp(name, combat_spell_name(prior[i].category,
                                            prior[i].item)))
            return 1;
    }
    return 0;
}

/* Magic rarity describes the actual spell, never the random roll that led to
   it.  Thus FAST CURE (level 2) is always Uncommon and SLEEP (level 1) is
   always Common instead of acquiring contradictory labels between drafts. */
static int arena_spell_rarity(int spell) {
    static const int first_level[ARENA_RARITY_COUNT] = {
        1, 2, 4, 6, 8, 11, 14
    };
    int level = spell / 3 + 1;
    int rarity = ARENA_COMMON;
    for (int i = 1; i < ARENA_RARITY_COUNT; i++)
        if (level >= first_level[i]) rarity = i;
    return rarity;
}

static int arena_pick_reward_spell(Game *g, const Character *p, int category,
                                   int target_level, int source,
                                   const ArenaReward *prior,
                                   int prior_count) {
    int candidates[MW_ENHANCED_SPELL_COUNT];
    int count = 0;
    int minimum = target_level > 2 ? target_level - 2 : 1;
    for (int spell = 0; spell < MW_ENHANCED_SPELL_COUNT; spell++) {
        int level = spell / 3 + 1;
        if (level >= minimum && level <= target_level &&
            combat_spell_source_allowed(p, category, source) &&
            combat_spell_arena_eligible(category, spell) &&
            !arena_has_magic(p, category, spell, source) &&
            !arena_spell_name_in_draft(prior, prior_count, category, spell))
            candidates[count++] = spell;
    }
    if (!count) {
        for (int spell = 0; spell < MW_ENHANCED_SPELL_COUNT; spell++)
            if (spell / 3 + 1 <= target_level &&
                combat_spell_source_allowed(p, category, source) &&
                combat_spell_arena_eligible(category, spell) &&
                !arena_has_magic(p, category, spell, source) &&
                !arena_spell_name_in_draft(prior, prior_count,
                                           category, spell))
                candidates[count++] = spell;
    }
    /* Four source cards need four visually meaningful choices even in round
       one.  If the current power band is exhausted, take the nearest higher
       eligible level; its canonical rarity makes that upgrade explicit. */
    for (int level = target_level + 1; !count && level <= 15; level++) {
        for (int spell = 0; spell < MW_ENHANCED_SPELL_COUNT; spell++)
            if (spell / 3 + 1 == level &&
                combat_spell_source_allowed(p, category, source) &&
                combat_spell_arena_eligible(category, spell) &&
                !arena_has_magic(p, category, spell, source) &&
                !arena_spell_name_in_draft(prior, prior_count,
                                           category, spell))
                candidates[count++] = spell;
    }
    if (!count) return -1;
    return candidates[arena_random(g, (u32)count)];
}

static int arena_pick_unowned_gear(Game *g, const ArenaSave *save,
                                   int rarity, int armor) {
    int preferred = arena_ladder_index(g, save, rarity, armor);
    int maximum = arena_max_ladder_index(save->round ? save->round : 1u,
                                         armor);
    if (rarity == ARENA_SUPER_ULTRA_RARE) {
        int jackpot_maximum = arena_base_ladder_index(
            save->round ? save->round : 1u) + 5;
        if (jackpot_maximum > 15) jackpot_maximum = 15;
        if (maximum < jackpot_maximum) maximum = jackpot_maximum;
        /* Never turn a yellow jackpot into below-band gear merely because
           its first rolled tier is already owned. */
        for (int tier = preferred; tier <= maximum; tier++) {
            int item = armor ? armor_ladder[tier] : weapon_ladder[tier];
            int owned = armor ?
                mw_armor_inventory_count(&save->character, item) :
                mw_weapon_inventory_count(&save->character, item);
            if (!owned) return tier;
        }
    }

    /* Prefer the rolled tier or something below it.  If that band has been
       exhausted, advance to the nearest still-legal unowned tier instead of
       wasting a card on equipment already in the inventory. */
    for (int tier = preferred; tier >= 0; tier--) {
        int item = armor ? armor_ladder[tier] : weapon_ladder[tier];
        int owned = armor ?
            mw_armor_inventory_count(&save->character, item) :
            mw_weapon_inventory_count(&save->character, item);
        if (!owned) return tier;
    }
    for (int tier = preferred + 1; tier <= maximum; tier++) {
        int item = armor ? armor_ladder[tier] : weapon_ladder[tier];
        int owned = armor ?
            mw_armor_inventory_count(&save->character, item) :
            mw_weapon_inventory_count(&save->character, item);
        if (!owned) return tier;
    }
    return -1;
}

static void arena_make_boon_reward(Game *g, ArenaSave *save,
                                   ArenaReward *reward) {
    int boon = (int)arena_random(g, ARENA_BOON_COUNT);
    if (reward->rarity < ARENA_ULTRA_RARE && boon == ARENA_BOON_RELIC)
        boon = ARENA_BOON_HEAL + (int)arena_random(g, ARENA_BOON_REGEN + 1);
    reward->kind = ARENA_REWARD_BOON;
    reward->item = (u16)boon;
    reward->amount = (u16)(reward->rarity + 1);
    reward->value = 5u + (u32)(reward->rarity + 1) * 5u;
    if (boon == ARENA_BOON_RELIC) {
        int unowned[MW_RELIC_COUNT];
        int count = 0;
        for (int relic = 0; relic < MW_RELIC_COUNT; relic++)
            if (!mw_relic_owned(&save->character, relic))
                unowned[count++] = relic;
        if (count)
            reward->value = (u32)unowned[arena_random(g, (u32)count)];
        else {
            reward->item = ARENA_BOON_HEAL +
                (u16)arena_random(g, ARENA_BOON_REGEN + 1);
            reward->value = 5u + (u32)(reward->rarity + 1) * 5u;
        }
    }
}

static ArenaReward arena_make_reward_for_draft(
        Game *g, ArenaSave *save, int kind,
        const ArenaReward *prior, int prior_count) {
    static const int spell_ahead[ARENA_RARITY_COUNT] = {0,0,1,1,1,2,3};
    ArenaReward reward;
    memset(&reward, 0, sizeof(reward));
    reward.kind = (u8)kind;
    reward.rarity = (u8)arena_roll_rarity(g, save);
    if (save->enemy_champion && reward.rarity < ARENA_SUPER_ULTRA_RARE)
        reward.rarity++;
    if (kind == ARENA_REWARD_WEAPON) {
        int tier = arena_pick_unowned_gear(g, save, reward.rarity, 0);
        if (tier < 0)
            arena_make_boon_reward(g, save, &reward);
        else {
            reward.item = (u16)weapon_ladder[tier];
            reward.value = (u32)arena_reward_enchant(save, reward.rarity);
        }
    } else if (kind == ARENA_REWARD_ARMOR) {
        int tier = arena_pick_unowned_gear(g, save, reward.rarity, 1);
        if (tier < 0)
            arena_make_boon_reward(g, save, &reward);
        else {
            reward.item = (u16)armor_ladder[tier];
            reward.value = (u32)arena_reward_enchant(save, reward.rarity);
        }
    } else if (kind == ARENA_REWARD_SPELL ||
               kind == ARENA_REWARD_SCROLL ||
               kind == ARENA_REWARD_WAND ||
               kind == ARENA_REWARD_PAPER) {
        unsigned round = save->round ? save->round : 1;
        int base_level = arena_base_spell_level(round);
        int ahead = spell_ahead[reward.rarity];
        int level = base_level + (ahead ?
            (int)arena_random(g, (u32)ahead + 1u) : 0);
        if (level > 15) level = 15;
        reward.source = kind == ARENA_REWARD_SPELL ? 0 :
                        kind == ARENA_REWARD_SCROLL ? 1 :
                        kind == ARENA_REWARD_WAND ? 2 : 3;
        reward.category = (u8)(SPELL_CAT_WIZARD + arena_random(g, 2));
        int rolled_rarity = reward.rarity;
        int spell = arena_pick_reward_spell(g, &save->character,
                                            reward.category, level,
                                            reward.source, prior,
                                            prior_count);
        if (spell < 0) {
            reward.category = reward.category == SPELL_CAT_WIZARD ?
                              SPELL_CAT_PRIEST : SPELL_CAT_WIZARD;
            spell = arena_pick_reward_spell(g, &save->character,
                                            reward.category, level,
                                            reward.source, prior,
                                            prior_count);
        }
        if (spell < 0)
            arena_make_boon_reward(g, save, &reward);
        else {
            reward.item = (u16)spell;
            reward.amount = reward.source == 2 ?
                (u16)(3 + rolled_rarity * 3 + arena_random(g, 5)) : 1;
            reward.rarity = (u8)arena_spell_rarity(spell);
        }
    } else {
        arena_make_boon_reward(g, save, &reward);
    }
    return reward;
}

static ArenaReward arena_make_reward(Game *g, ArenaSave *save, int kind) {
    return arena_make_reward_for_draft(g, save, kind, NULL, 0);
}

static void arena_roll_rewards(Game *g, ArenaSave *save) {
    for (int kind = 0; kind < ARENA_REWARD_COUNT; kind++) {
        ArenaReward reward;
        int attempts = 0;
        do {
            reward = arena_make_reward_for_draft(
                g, save, kind, save->rewards, kind);
            attempts++;
        } while (attempts < 64 && reward.kind == ARENA_REWARD_BOON &&
                 reward.item == ARENA_BOON_RELIC &&
                 mw_relic_owned(&save->character, (int)reward.value));
        save->rewards[kind] = reward;
    }
}

static void arena_generate_rewards(Game *g, ArenaSave *save) {
    arena_roll_rewards(g, save);
    /* Champion victories award two separate drafts. The second set is rolled
       only after the first choice, so neither pick is wasted on a duplicate
       screen. */
    save->pending_rewards = save->enemy_champion ? 2 : 1;
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

static const char *arena_relic_name(int relic) {
    static const char *const names[MW_RELIC_COUNT] = {
        "ARCANE RING", "BLOODSTONE SIGNET", "DEEPWARD AMULET",
        "SAGE PRISM", "PHOENIX SEAL"
    };
    return relic >= 0 && relic < MW_RELIC_COUNT ? names[relic] : "LOST RELIC";
}

static void arena_boon_detail(const ArenaReward *reward, char *detail,
                              size_t detail_size) {
    int power = reward->amount ? reward->amount : 1;
    switch (reward->item) {
    case ARENA_BOON_HEAL:
        snprintf(detail, detail_size, "RESTORES %d%% MAX HP%s.",
                 45 + power * 7,
                 power >= 4 ? " AND CURES POISON/DISEASE" : "");
        break;
    case ARENA_BOON_RESTORE:
        snprintf(detail, detail_size, "RESTORES %d%% OF MAX SPELL POINTS.",
                 45 + power * 7);
        break;
    case ARENA_BOON_VIGOR:
        snprintf(detail, detail_size,
                 "MAX HP +%d%% +5; HEALS THE SAME; VIGOR +%d.",
                 4 + power * 2, power);
        break;
    case ARENA_BOON_FOCUS:
        snprintf(detail, detail_size,
                 "MAX SP +%d AND FULL REFILL; FOCUS +%d.",
                 4 + power * 2, power);
        break;
    case ARENA_BOON_MIGHT:
        snprintf(detail, detail_size, "STR +%d, CON +%d; MIGHT +%d.",
                 2 + power, 1 + (power + 1) / 2, power);
        break;
    case ARENA_BOON_MIND:
        snprintf(detail, detail_size, "INT +%d, WIS +%d; MIND +%d.",
                 2 + power, 2 + power, power);
        break;
    case ARENA_BOON_SWIFTNESS:
        snprintf(detail, detail_size,
                 "AGILITY +%d, LUCK +%d; SWIFTNESS +%d.",
                 2 + power, 1 + (power + 1) / 2, power);
        break;
    case ARENA_BOON_WARD:
        snprintf(detail, detail_size,
                 "BODY ARMOR +%d, RING PROTECTION +%d; WARD +%d.",
                 2 + power, 1 + (power + 1) / 2, power);
        break;
    case ARENA_BOON_FURY:
        snprintf(detail, detail_size,
                 "COMBAT BONUS +%d, GAUNTLET +%d; FURY +%d.",
                 1 + power / 3, 2 + power, power);
        break;
    case ARENA_BOON_REGEN:
        snprintf(detail, detail_size,
                 "RESTORES +%d HP PER LIVING ACTION (MAX 12).",
                 1 + (power + 1) / 2);
        break;
    case ARENA_BOON_RELIC:
        switch ((int)reward->value) {
        case MW_RELIC_ARCANE_RING:
            snprintf(detail, detail_size, "ARCANE RING: RESTORES 1 SP EVERY 4 ACTIONS.");
            break;
        case MW_RELIC_BLOODSTONE_SIGNET:
            snprintf(detail, detail_size, "BLOODSTONE: MELEE DAMAGE RESTORES HP.");
            break;
        case MW_RELIC_DEEPWARD_AMULET:
            snprintf(detail, detail_size, "DEEPWARD: 15%% LESS DAMAGE; SHORTER AILMENTS.");
            break;
        case MW_RELIC_SAGE_PRISM:
            snprintf(detail, detail_size, "SAGE PRISM: +1 EXTRA LEVEL PER VICTORY.");
            break;
        default:
            snprintf(detail, detail_size, "PHOENIX SEAL: SURVIVE ONE LETHAL STRIKE.");
            break;
        }
        break;
    default:
        snprintf(detail, detail_size, "A MYSTERIOUS CROWD FAVOR.");
        break;
    }
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
    } else if (reward->kind == ARENA_REWARD_SPELL ||
               reward->kind == ARENA_REWARD_SCROLL ||
               reward->kind == ARENA_REWARD_WAND ||
               reward->kind == ARENA_REWARD_PAPER) {
        int category = reward->category <= SPELL_CAT_PRIEST ?
                       reward->category : SPELL_CAT_WIZARD;
        int spell = reward->item < MW_ENHANCED_SPELL_COUNT ? reward->item : 0;
        snprintf(line, line_size, "%s %s %s: %s", rarity_names[rarity],
                 category == SPELL_CAT_WIZARD ? "WIZARD" : "PRIEST",
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
        if (reward->item == ARENA_BOON_RELIC)
            snprintf(line, line_size, "%s RELIC: %s", rarity_names[rarity],
                     arena_relic_name((int)reward->value));
        else
            snprintf(line, line_size, "%s BOON: %s", rarity_names[rarity],
                     arena_boon_name(reward->item));
        arena_boon_detail(reward, detail, detail_size);
    }
}

static void arena_apply_boon(ArenaSave *save, const ArenaReward *reward) {
    Character *p = &save->character;
    int power = reward->amount ? reward->amount : 1;
    switch (reward->item) {
    case ARENA_BOON_HEAL: {
        uint64_t heal = (uint64_t)mw_hp_max(p) *
                        (45u + power * 7u) / 100u;
        uint64_t hp = (uint64_t)mw_hp_cur(p) + heal;
        mw_set_hp_cur(p, hp > mw_hp_max(p) ? mw_hp_max(p) : hp);
        if (power >= 4) p->poisoned_turns = p->diseased_turns = 0;
        break;
    }
    case ARENA_BOON_RESTORE:
        p->sp_cur += p->sp_max * (0.45f + power * 0.07f);
        if (p->sp_cur > p->sp_max) p->sp_cur = p->sp_max;
        break;
    case ARENA_BOON_VIGOR: {
        uint64_t gain = (uint64_t)mw_hp_max(p) *
                        (4u + power * 2u) / 100u + 5u;
        mw_set_hp_max(p, (uint64_t)mw_hp_max(p) + gain);
        mw_set_hp_cur(p, (uint64_t)mw_hp_cur(p) + gain);
        arena_add_perk(&save->perk_levels[0], power);
        break;
    }
    case ARENA_BOON_FOCUS:
        arena_add_float(&p->sp_max, 4.0 + power * 2.0);
        p->sp_cur = p->sp_max;
        arena_add_perk(&save->perk_levels[1], power);
        break;
    case ARENA_BOON_MIGHT:
        arena_add_stat(&p->stat_str, 2 + power);
        arena_add_stat(&p->stat_con, 1 + (power + 1) / 2);
        arena_add_perk(&save->perk_levels[2], power);
        break;
    case ARENA_BOON_MIND:
        arena_add_stat(&p->stat_int, 2 + power);
        arena_add_stat(&p->stat_wis, 2 + power);
        arena_add_perk(&save->perk_levels[3], power);
        break;
    case ARENA_BOON_SWIFTNESS:
        arena_add_stat(&p->stat_agi, 2 + power);
        arena_add_stat(&p->stat_luck, 1 + (power + 1) / 2);
        arena_add_perk(&save->perk_levels[4], power);
        break;
    case ARENA_BOON_WARD:
        mw_set_body_armor_plus(p, mw_body_armor_plus(p) +
                              2 + power);
        mw_set_ring_prot_plus(p, mw_ring_prot_plus(p) +
                              1 + (power + 1) / 2);
        arena_add_perk(&save->perk_levels[5], power);
        break;
    case ARENA_BOON_FURY:
        if (p->combat_bonus <= UINT8_MAX - (1 + power / 3))
            p->combat_bonus += (u8)(1 + power / 3);
        else
            p->combat_bonus = UINT8_MAX;
        mw_set_gauntlet(p, mw_gauntlet(p) + 2 + power);
        arena_add_perk(&save->perk_levels[6], power);
        break;
    case ARENA_BOON_REGEN:
        if (p->ring_regen < 12) {
            int regen = p->ring_regen + 1 + (power + 1) / 2;
            p->ring_regen = (u8)(regen > 12 ? 12 : regen);
        }
        arena_add_perk(&save->perk_levels[7], power);
        break;
    case ARENA_BOON_RELIC:
        mw_set_relic_owned(p, (int)(reward->value % MW_RELIC_COUNT), 1);
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
    } else if (reward->kind == ARENA_REWARD_SPELL ||
               reward->kind == ARENA_REWARD_SCROLL ||
               reward->kind == ARENA_REWARD_WAND ||
               reward->kind == ARENA_REWARD_PAPER) {
        int category = reward->category <= SPELL_CAT_PRIEST ?
                       reward->category : SPELL_CAT_WIZARD;
        int spell = reward->item < MW_ENHANCED_SPELL_COUNT ? reward->item : 0;
        if (reward->source == 0) {
            if (p->spells[category][spell]) {
                int level = spell / 3 + 1;
                arena_add_float(&p->sp_max,
                                1.0 + level / 3.0 + reward->rarity);
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

static u16 *arena_stat_slot(Character *p, int slot) {
    switch (slot) {
    case 0: return &p->stat_str;
    case 1: return &p->stat_int;
    case 2: return &p->stat_wis;
    case 3: return &p->stat_con;
    case 4: return &p->stat_agi;
    default: return &p->stat_luck;
    }
}

static void arena_advance_character(Game *g, ArenaSave *save) {
    Character *p = &save->character;
    unsigned level_gain = 1u;
    if (save->enemy_champion) level_gain++;
    if (save->current_streak && save->current_streak % 3u == 0)
        level_gain++;
    if (mw_relic_owned(p, MW_RELIC_SAGE_PRISM) &&
        save->current_streak % 5u == 0)
        level_gain++;
    if ((unsigned)p->level + level_gain > MW_PLAYER_LEVEL_MAX)
        p->level = MW_PLAYER_LEVEL_MAX;
    else
        p->level = (u16)(p->level + level_gain);

    /* One stat point per victory keeps accuracy and avoidance from scaling
       twice as fast as the monster level. Champions add two bonus rolls. */
    arena_add_stat(arena_stat_slot(p, (int)arena_random(g, 6)), 1);
    if (save->enemy_champion) {
        arena_add_stat(arena_stat_slot(p, (int)arena_random(g, 6)), 1);
        arena_add_stat(arena_stat_slot(p, (int)arena_random(g, 6)), 1);
    }

    uint64_t hp_gain = 6u + p->stat_con / 5u + p->level / 2u +
                       arena_random(g, 6);
    mw_set_hp_max(p, (uint64_t)mw_hp_max(p) + hp_gain);
    arena_add_float(&p->sp_max,
                    1.5 + (double)(p->stat_int + p->stat_wis) / 30.0);
    int difficulty = arena_difficulty(save);
    unsigned hp_recovery =
        difficulty == ARENA_DIFFICULTY_EASY ? 15u :
        difficulty == ARENA_DIFFICULTY_HARD ? 7u : 10u;
    unsigned sp_recovery =
        difficulty == ARENA_DIFFICULTY_EASY ? 8u :
        difficulty == ARENA_DIFFICULTY_HARD ? 3u : 5u;
    /* The first four victories are onboarding drafts, not an attrition test.
       Normal/Easy begin the next fight fresh; Hard retains damage but still
       receives enough recovery to establish a build.  Assistance then fades
       completely before the round-ten champion curve. */
    if (save->round <= 4u) {
        hp_recovery = difficulty == ARENA_DIFFICULTY_HARD ? 60u : 100u;
        sp_recovery = difficulty == ARENA_DIFFICULTY_HARD ? 50u : 100u;
    } else if (save->round <= 6u) {
        if (hp_recovery < 40u) hp_recovery = 40u;
        if (sp_recovery < 30u) sp_recovery = 30u;
    } else if (save->round <= 8u) {
        if (hp_recovery < 20u) hp_recovery = 20u;
        if (sp_recovery < 15u) sp_recovery = 15u;
    }
    arena_restore_percent(p, hp_recovery, sp_recovery);
}

static int arena_opening_monster_ok(const MonsterType *mt, unsigned round) {
    if (!mt || mt->boss || round > 5u) return round > 5u;
    if (round <= 2u)
        return mt->dmg <= 7 && mt->atk <= 12 && mt->hpF <= 8;
    return mt->dmg <= 10 && mt->atk <= 15 && mt->hpF <= 12;
}

static int arena_enemy_type_allowed(int type) {
    /* WORLD's Ball (57-71) and Puffball (72-83) families are special-purpose
       dungeon encounters.  Balls have Garbage Can-scale endurance and almost
       no offense; Puffballs use a non-damaging stat effect instead of a
       normal retaliation.  Neither creates a useful arena fight. */
    return type < 57 || type > 83;
}

static int arena_enemy_type(Game *g, int level, int champion, unsigned round) {
    if (champion) {
        int candidates[MONSTER_TYPE_COUNT];
        int count = 0;
        for (int type = 0; type < MONSTER_TYPE_COUNT; type++)
            if (combat_monster_type_spawnable(type) &&
                arena_enemy_type_allowed(type) &&
                monster_types[type].boss &&
                combat_monster_type_valid(type, level))
                candidates[count++] = type;
        if (count) return candidates[arena_random(g, (u32)count)];
    }
    for (int attempt = 0; attempt < 64; attempt++) {
        int type = combat_pick_monster_type(g, level);
        if (combat_monster_type_spawnable(type) &&
            arena_enemy_type_allowed(type) &&
            (!monster_types[type].boss || champion) &&
            (champion || arena_opening_monster_ok(&monster_types[type], round)))
            return type;
    }
    /* Walking Sword is part of the original low-floor roster and is a fair,
       status-free fallback if opening rejection sampling finds only
       high-risk rows.  Later rounds choose from a complete valid pool so a
       rejected Ball can never slip through the fallback. */
    if (!champion && round <= 5u) return 2;
    int candidates[MONSTER_TYPE_COUNT];
    int count = 0;
    for (int type = 0; type < MONSTER_TYPE_COUNT; type++)
        if (combat_monster_type_spawnable(type) &&
            arena_enemy_type_allowed(type) && !monster_types[type].boss &&
            combat_monster_type_valid(type, level))
            candidates[count++] = type;
    return count ? candidates[arena_random(g, (u32)count)] : 2;
}

static void arena_generate_enemy(Game *g, ArenaSave *save) {
    int champion = save->round > 0 && save->round % 10 == 0;
    int difficulty = arena_difficulty(save);
    /* Do not feed the player's exact build back into the opponent.  Instead,
       use a deterministic round curve which overtakes the player's scheduled
       bonus levels only after the first champion. */
    int threat = arena_threat_level(save->round);
    static const int normal_low[ARENA_DIFFICULTY_COUNT] = {80, 90, 100};
    static const int normal_high[ARENA_DIFFICULTY_COUNT] = {105, 115, 125};
    static const int champion_low[ARENA_DIFFICULTY_COUNT] = {100, 110, 125};
    static const int champion_high[ARENA_DIFFICULTY_COUNT] = {120, 130, 150};
    int low = threat * (champion ? champion_low[difficulty] :
                                   normal_low[difficulty]) / 100;
    int high = threat * (champion ? champion_high[difficulty] :
                                    normal_high[difficulty]) / 100 + 2;
    if (low < 1) low = 1;
    if (low > MW_PLAYER_LEVEL_MAX) low = MW_PLAYER_LEVEL_MAX;
    if (high > MW_PLAYER_LEVEL_MAX) high = MW_PLAYER_LEVEL_MAX;
    if (high < low) high = low;
    int level = low + (int)arena_random(g, (u32)(high - low + 1));
    int species_floor = level > MAX_DUNGEON_FLOOR ?
                        MAX_DUNGEON_FLOOR : level;
    int type = arena_enemy_type(g, species_floor, champion, save->round);
    const MonsterType *mt = &monster_types[type];
    uint64_t base_hp = (uint64_t)(mt->hpF > 0 ? mt->hpF : 1) *
                       (unsigned)level;
    if (mt->boss) base_hp += (uint64_t)level * 20u;
    /* Arena opponents use the upper part of their native HP range, then gain
       endurance smoothly with the run.  The prior curve nearly doubled HP by
       round nine, forcing attack-oriented builds through too many unanswered
       exchanges before their reward engine was established. */
    uint64_t hp = base_hp * (65u + arena_random(g, 36)) / 100u + 1u;
    unsigned endurance = arena_endurance_percent(save->round);
    if (difficulty == ARENA_DIFFICULTY_EASY) endurance = endurance * 80u / 100u;
    else if (difficulty == ARENA_DIFFICULTY_HARD) endurance = endurance * 135u / 100u;
    hp = hp * endurance / 100u;
    if (champion) hp = hp * 3u / 2u + (uint64_t)level * 6u;
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
    g->arena_difficulty = arena_difficulty(save);
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

static int arena_skip_percent(const ArenaSave *save) {
    int difficulty = arena_difficulty(save);
    if (difficulty == ARENA_DIFFICULTY_EASY)
        return save->enemy_champion ? 25 : 15;
    if (difficulty == ARENA_DIFFICULTY_HARD)
        return save->enemy_champion ? 45 : 25;
    return save->enemy_champion ? ARENA_CHAMPION_SKIP_COST_PERCENT :
                                  ARENA_SKIP_COST_PERCENT;
}

static void arena_apply_skip_penalty(ArenaSave *save) {
    Character *p = &save->character;
    int percent = arena_skip_percent(save);
    uint64_t loss = (uint64_t)mw_hp_max(p) * (unsigned)percent / 100u;
    if (loss < 1) loss = 1;
    u32 hp = mw_hp_cur(p);
    mw_set_hp_cur(p, hp > loss ? hp - loss : 1);
    p->sp_cur *= (100.0f - (float)percent) / 100.0f;
    if (p->sp_cur < 0.0f) p->sp_cur = 0.0f;
    save->current_streak = 0;
    save->enemy_active = 0;
    save->enemy_hp = save->enemy_max_hp = 0;
    save->enemy_asleep = save->enemy_held = save->enemy_stopped = 0;
    save->enemy_champion = 0;
    character_clear_battle_effects(p);
    arena_increment(&save->round);
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

int arena_select_difficulty(Game *g) {
    if (!g) return -1;
    arena_palette(g);
    video_clear(&g->video, 0);
    video_draw_text(&g->video, 28, 20, "SELECT COLOSSEUM DIFFICULTY", 8);
    video_draw_text_scaled(&g->video, 52, 110, "1) EASY", 10, 3, 4);
    video_draw_text_scaled(&g->video, 90, 165,
        "LOWER ENEMY THREAT, MORE RECOVERY, CHEAPER SKIPS.", 15, 3, 4);
    video_draw_text_scaled(&g->video, 52, 285, "2) NORMAL", 14, 3, 4);
    video_draw_text_scaled(&g->video, 90, 340,
        "THE INTENDED BALANCED COLOSSEUM EXPERIENCE.", 15, 3, 4);
    video_draw_text_scaled(&g->video, 52, 460, "3) HARD", 12, 3, 4);
    video_draw_text_scaled(&g->video, 90, 515,
        "STRONGER FOES, LESS RECOVERY, BETTER RARITY ODDS.", 15, 3, 4);
    video_draw_text_scaled(&g->video, 52, 675,
        "PRESS 1-3. ESCAPE CANCELS.", 8, 3, 4);
    video_present(&g->video);
    for (;;) {
        int key = input_wait_any_key(&g->input);
        if (key == INPUT_MOUSE_CLICK) {
            int x, y;
            key = 0;
            if (game_mouse_click_logical(g, &x, &y) && x >= 30 && x < 990) {
                if (y >= 80 && y < 250) key = '1';
                else if (y >= 255 && y < 425) key = '2';
                else if (y >= 430 && y < 610) key = '3';
            }
        }
        if (key >= '1' && key <= '3') return key - '1';
        if (key == 0x1B || input_poll_quit(&g->input)) return -1;
    }
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
        "I  USE BATTLE ITEM   K  SKIP WITH HP/SP PENALTY",
        "W/A  SELECT GEAR     T  WAIT ONE COMBAT TURN",
        "V  COMBATANT SHEET   S  SAVE RUN   O  SOUND ON/OFF",
        "H  HELP              Q/ESC  SAVE AND RETURN",
        "X  RETIRE THIS RUN (CAREER RECORDS REMAIN)",
        "CTRL-F1 TURBO ON/OFF; WHILE ON, +/- CHANGES SPEED",
        "",
        "EVERY VICTORY OFFERS SEVEN RANDOM REWARDS:",
        "WEAPON, ARMOR, SPELL, SCROLL, WAND, PAPER, OR BOON.",
        "RARITIES RUN FROM COMMON TO SUPER ULTRA RARE.",
        "GEAR AND MAGIC ADVANCE IN CONTROLLED POWER BANDS.",
        "DIFFICULTY CHANGES POWER; FOES SCALE FASTER AFTER ROUND 10.",
        "HARD ALSO IMPROVES RARITY ODDS. EVERY FIFTH WIN HEALS;",
        "EVERY TENTH ROUND IS A CHAMPION WITH TWO DRAFTS.",
        "",
        "AFTER DEATH YOU MAY RESET THE BUILD AND RESTART.",
        "THE SAVE, BEST STREAK, AND CAREER VICTORIES REMAIN.",
        "ADVENTURE SAVES ARE NEVER OPENED OR MODIFIED."
    };
    int y = 66;
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++, y += 35)
        video_draw_text_scaled(&g->video, 28, y, lines[i], i < 6 ? 14 : 15, 3, 4);
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
    snprintf(line, sizeof(line), "%s  %s  RUN %u  ROUND %u  BEST %u",
             p->name, arena_difficulty_name(arena_difficulty(save)),
             save->run_number, save->round, save->best_streak);
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
                 "ROUND %u WON - CHOOSE REWARD (%u PICK%s LEFT)",
                 save->round, save->pending_rewards,
                 save->pending_rewards == 1 ? "" : "S");
        video_draw_text(&g->video, 24, 18, header, 8);
        video_draw_text_scaled(&g->video, 24, 52,
            "THE OTHER SIX VANISH WHEN YOU CHOOSE.", 15, 3, 4);
        for (int i = 0; i < ARENA_REWARD_COUNT; i++) {
            const ArenaReward *reward = &save->rewards[i];
            arena_reward_text(reward, line, sizeof(line), detail, sizeof(detail));
            int y = 88 + i * 88;
            video_fill_rect(&g->video, 20, y, LOGICAL_W - 40, 80, 1);
            snprintf(header, sizeof(header), "%d) %s", i + 1, line);
            video_draw_text_scaled(&g->video, 40, y + 7, header,
                rarity_colors[reward->rarity < ARENA_RARITY_COUNT ?
                              reward->rarity : 0], 3, 4);
            video_draw_text_scaled(&g->video, 64, y + 42, detail, 15, 3, 4);
        }
        video_draw_text(&g->video, 24, 720,
                        "1-7 CHOOSE   Q/ESC SAVE REWARD FOR LATER", 14);
        video_present(&g->video);
        int key = input_wait_any_key(&g->input);
        if (key == INPUT_MOUSE_CLICK) {
            int x, y;
            key = 0;
            if (game_mouse_click_logical(g, &x, &y) && x >= 20 &&
                x < LOGICAL_W - 20 && y >= 88 && y < 704) {
                int row = (y - 88) / 88;
                if (row >= 0 && row < ARENA_REWARD_COUNT) key = '1' + row;
            }
        }
        if (key >= '1' && key < '1' + ARENA_REWARD_COUNT)
            return key - '1';
        if (key == 'q' || key == 'Q' || key == 0x1B ||
            input_poll_quit(&g->input)) return -1;
    }
}

static int arena_show_death(Game *g, ArenaSave *save) {
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
    video_draw_text_scaled(&g->video, 190, 440,
                           "RESTART THIS COMBATANT?", 8, 3, 4);
    video_draw_text_scaled(&g->video, 190, 505,
                           "Y) RESET RUN     N) RETURN TO SAVES", 15, 3, 4);
    video_draw_text_scaled(&g->video, 190, 570,
                           "CAREER RECORDS AND THIS SAVE SLOT REMAIN.", 10, 3, 4);
    video_present(&g->video);
    for (;;) {
        int key = input_wait_any_key(&g->input);
        if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
        if (key == 'Y') return 1;
        if (key == 'N' || key == 0x1B || input_poll_quit(&g->input)) return 0;
    }
}

static int arena_restart(Game *g, int slot, ArenaSave *save,
                         int show_death) {
    int restart = show_death ? arena_show_death(g, save) :
        arena_confirm(g, "RESTART THIS COLOSSEUM COMBATANT?",
                      "RUN STATS/ITEMS RESET; CAREER RECORDS REMAIN.");
    if (!restart) return 0;
    int difficulty = arena_select_difficulty(g);
    if (difficulty < 0) return 0;
    save->difficulty = (u8)difficulty;
    arena_begin_new_run(save);
    return arena_save_save(g, slot, save) == 0;
}

static void arena_victory(Game *g, int slot, ArenaSave *save) {
    save->enemy_active = 0;
    arena_increment(&save->current_streak);
    arena_increment(&save->total_victories);
    if (save->current_streak > save->best_streak)
        save->best_streak = save->current_streak;
    arena_advance_character(g, save);
    int before_champion = (save->round + 1u) % 10u == 0;
    if (save->round % 5u == 0 || before_champion) {
        unsigned milestone;
        if (before_champion)
            milestone = arena_difficulty(save) == ARENA_DIFFICULTY_EASY ? 75u :
                        arena_difficulty(save) == ARENA_DIFFICULTY_HARD ? 45u : 60u;
        else
            milestone = arena_difficulty(save) == ARENA_DIFFICULTY_EASY ? 45u :
                        arena_difficulty(save) == ARENA_DIFFICULTY_HARD ? 25u : 35u;
        arena_restore_percent(&save->character, milestone, milestone);
        save->character.poisoned_turns = 0;
        save->character.diseased_turns = 0;
    }
    arena_generate_rewards(g, save);
    arena_save_save(g, slot, save);
}

void arena_run(Game *g, int slot, ArenaSave *save) {
    if (!g || !save || slot < 0 || slot >= MAX_PLAYERS) return;
    int outside_sound = g->sound_enabled;
    g->active_save_slot = -1;
    if (!save->sound_mode)
        save->sound_mode = g->sound_enabled ? 1 : 2;
    g->sound_enabled = save->sound_mode == 1;
    mw_audio_set_enabled(&g->audio, g->sound_enabled);
    /* Saves may contain an active Ball/Puffball rolled by an older build.
       Retire it immediately so updating the executable fixes the current
       encounter as well as all future selections. */
    if (save->enemy_active && !arena_enemy_type_allowed(save->enemy_type)) {
        save->enemy_active = 0;
        save->enemy_hp = save->enemy_max_hp = 0;
        save->enemy_asleep = save->enemy_held = save->enemy_stopped = 0;
        save->enemy_champion = 0;
        arena_save_save(g, slot, save);
    }
    /* Repair version-1 runs that regeneration left marked active at zero HP.
       Count the defeat once, persist it immediately, and return to selection. */
    if (save->in_run && !mw_hp_cur(&save->character)) {
        arena_increment(&save->total_deaths);
        save->in_run = 0;
        save->enemy_active = 0;
        save->pending_rewards = 0;
        if (save->current_streak > save->best_streak)
            save->best_streak = save->current_streak;
        arena_save_save(g, slot, save);
        if (!arena_restart(g, slot, save, 1)) {
            g->sound_enabled = outside_sound;
            mw_audio_set_enabled(&g->audio, outside_sound);
            arena_palette(g);
            return;
        }
    }
    if (!save->in_run) {
        if (!arena_restart(g, slot, save, 0)) {
            g->sound_enabled = outside_sound;
            mw_audio_set_enabled(&g->audio, outside_sound);
            return;
        }
    }

    while (!input_poll_quit(&g->input) && save->in_run) {
        if (save->pending_rewards) {
            int choice = arena_choose_reward(g, save);
            if (choice < 0) break;
            arena_apply_reward(save, &save->rewards[choice]);
            save->pending_rewards--;
            if (save->pending_rewards) {
                arena_roll_rewards(g, save);
            } else {
                arena_increment(&save->round);
                save->character.eff_hold_monster = 0;
                save->character.eff_stop_monster = 0;
                save->character.eff_slow_mon = 0;
            }
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
            g->sound_enabled ?
                "F FIGHT  C CAST  I ITEM  K SKIP  O SOUND OFF  H HELP" :
                "F FIGHT  C CAST  I ITEM  K SKIP  O SOUND ON   H HELP",
            "");
        video_present(&g->video);

        int key = input_getch(&g->input);
        if (key == 0) {
            (void)input_getch(&g->input);
            continue;
        }
        if (game_handle_turbo_key(g, key)) {
            char speed[96];
            snprintf(speed, sizeof(speed),
                g->turbo_enabled ? "GAME SPEED: %d%%   +/- ADJUSTS" :
                                   "NORMAL 100%% TIMING RESTORED",
                g->turbo_percent);
            arena_sync_game(g, save);
            game_draw_combat_overlay(g, &save->character, -1,
                cs.monster_type_idx, cs.monster_level, cs.monster_hp,
                g->turbo_enabled ? "TURBO MODE: ON" : "TURBO MODE: OFF",
                speed, "");
            video_present(&g->video);
            SDL_Delay(700); /* Readable regardless of the selected multiplier. */
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
        if (key == 'o' || key == 'O') {
            g->sound_enabled = !g->sound_enabled;
            save->sound_mode = g->sound_enabled ? 1 : 2;
            mw_audio_set_enabled(&g->audio, g->sound_enabled);
            if (g->sound_enabled) mw_audio_play(&g->audio, MW_SFX_UI);
            arena_state_from_combat(save, &cs);
            arena_save_save(g, slot, save);
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
        if (key == 'k' || key == 'K') {
            char cost[96];
            snprintf(cost, sizeof(cost),
                "LOSE %d%% HP/SP, YOUR STREAK, AND ALL REWARDS.",
                arena_skip_percent(save));
            if (arena_confirm(g, "SKIP THIS CHALLENGER?", cost)) {
                arena_apply_skip_penalty(save);
                arena_save_save(g, slot, save);
            }
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
            mw_set_hp_cur(&save->character, 0);
            arena_increment(&save->total_deaths);
            save->in_run = 0;
            save->enemy_active = 0;
            save->pending_rewards = 0;
            if (save->current_streak > save->best_streak)
                save->best_streak = save->current_streak;
            arena_save_save(g, slot, save);
            if (arena_restart(g, slot, save, 1)) continue;
            break;
        }
        if (cs.player_fled) {
            arena_apply_skip_penalty(save);
            arena_save_save(g, slot, save);
            continue;
        }
        if (cs.monster_hp <= 0 || cs.fled) {
            arena_victory(g, slot, save);
            continue;
        }
        arena_save_save(g, slot, save);
    }
    g->arena_active = 0;
    g->arena_champion = 0;
    g->arena_difficulty = ARENA_DIFFICULTY_NORMAL;
    g->active_save_slot = -1;
    g->sound_enabled = outside_sound;
    mw_audio_set_enabled(&g->audio, outside_sound);
    arena_palette(g);
}

int arena_self_test(void) {
    int failures = 0;
    Character created;
    memset(&created, 0, sizeof(created));
    strcpy(created.name, "ARENA TEST");
    created.class_id = CLASS_SAGE;
    created.stat_str = created.stat_int = created.stat_wis = 20;
    created.stat_con = created.stat_agi = created.stat_luck = 20;
    created.hp_cur = created.hp_max = 40;
    created.sp_cur = created.sp_max = 5.0f;
    ArenaSave save;
    arena_initialize_save(&save, &created);
    if (save.magic != ARENA_SAVE_MAGIC || save.version != ARENA_SAVE_VERSION ||
        save.record_size != sizeof(save) ||
        !save.in_run || save.round != 1 || save.run_number != 1 ||
        save.difficulty != ARENA_DIFFICULTY_NORMAL ||
        mw_experience_mode(&save.character) != MW_EXPERIENCE_ENHANCED ||
        !mw_universal_access(&save.character) || mw_hp_cur(&save.character) < 40)
        failures++;
    if (save.character.equipped_weapon != 1 ||
        save.character.equipped_armor != 1 ||
        !mw_weapon_inventory_count(&save.character, 1) ||
        !mw_armor_inventory_count(&save.character, 1))
        failures++;

    /* Game owns a full 1024x768 software framebuffer.  Keeping a second Game
       on this nested self-test's stack overflowed Windows' default stack when
       game_ui_self_test already had its caller's Game alive. */
    Game *g = (Game *)calloc(1, sizeof(*g));
    if (!g) return failures + 1;
    game_srand(g, 0xC011055u);
    save.enemy_level = 500;
    arena_generate_rewards(g, &save);
    for (int i = 0; i < ARENA_REWARD_COUNT; i++) {
        if (save.rewards[i].kind != i ||
            save.rewards[i].rarity >= ARENA_RARITY_COUNT)
            failures++;
        if (save.rewards[i].kind == ARENA_REWARD_WEAPON &&
            mw_weapon_inventory_count(&save.character,
                                      save.rewards[i].item))
            failures++;
        if (save.rewards[i].kind == ARENA_REWARD_ARMOR &&
            mw_armor_inventory_count(&save.character,
                                     save.rewards[i].item))
            failures++;
        if (save.rewards[i].kind >= ARENA_REWARD_SPELL &&
            save.rewards[i].kind <= ARENA_REWARD_PAPER &&
            arena_has_magic(&save.character, save.rewards[i].category,
                            save.rewards[i].item,
                            save.rewards[i].source))
            failures++;
        if (arena_reward_is_magic(&save.rewards[i])) {
            if (save.rewards[i].rarity !=
                arena_spell_rarity(save.rewards[i].item))
                failures++;
            for (int j = 0; j < i; j++)
                if (arena_reward_is_magic(&save.rewards[j]) &&
                    !strcmp(combat_spell_name(save.rewards[i].category,
                                              save.rewards[i].item),
                            combat_spell_name(save.rewards[j].category,
                                              save.rewards[j].item)))
                    failures++;
        }
    }
    /* Rarity is an intrinsic property of spell level.  The same spell can
       never be advertised as a different rarity merely because it came from
       a scroll, wand, paper, or learned-spell card. */
    if (arena_spell_rarity(0) != ARENA_COMMON ||
        arena_spell_rarity(5) != ARENA_UNCOMMON ||
        arena_spell_rarity(9) != ARENA_RARE ||
        arena_spell_rarity(42) != ARENA_SUPER_ULTRA_RARE)
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
    if (!combat_spell_arena_eligible(SPELL_CAT_PRIEST, 5) ||
        !combat_spell_arena_eligible(SPELL_CAT_PRIEST, 38) ||
        combat_spell_arena_eligible(SPELL_CAT_PRIEST, 7) ||
        combat_spell_arena_eligible(SPELL_CAT_WIZARD, 10) ||
        combat_spell_arena_eligible(SPELL_CAT_PERMANENT, 1))
        failures++;
    save.character.spells[SPELL_CAT_WIZARD][10] = 1;
    save.character.wands[SPELL_CAT_PREPARATION][0] = 9;
    save.character.scrolls[SPELL_CAT_PRIEST][5] = 1;
    arena_sanitize_magic(&save.character);
    if (save.character.spells[SPELL_CAT_WIZARD][10] ||
        save.character.wands[SPELL_CAT_PREPARATION][0] ||
        !save.character.scrolls[SPELL_CAT_PRIEST][5])
        failures++;
    u32 before_hp = mw_hp_max(&save.character);
    ArenaReward vigor = {ARENA_REWARD_BOON, ARENA_RARE, 0, 0,
                         ARENA_BOON_VIGOR, 3, 0};
    {
        char line[160], detail[160];
        arena_reward_text(&vigor, line, sizeof(line), detail, sizeof(detail));
        if (!strstr(line, "VIGOR") || !strstr(detail, "MAX HP +10%") ||
            !strstr(detail, "VIGOR +3"))
            failures++;
        ArenaReward phoenix = {ARENA_REWARD_BOON, ARENA_ULTRA_RARE, 0, 0,
                               ARENA_BOON_RELIC, 6,
                               MW_RELIC_PHOENIX_SEAL};
        arena_reward_text(&phoenix, line, sizeof(line), detail, sizeof(detail));
        if (!strstr(line, "PHOENIX SEAL") ||
            !strstr(detail, "SURVIVE ONE LETHAL STRIKE"))
            failures++;
    }
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
    for (int round = 1; round <= 60; round++) {
        save.round = (u32)round;
        int tier = arena_ladder_index(g, &save, ARENA_SUPER_ULTRA_RARE, 0);
        if (tier < 0 || tier > 15 ||
            weapon_ladder[tier] == 8 || weapon_ladder[tier] == 9 ||
            weapon_ladder[tier] == 10 || weapon_ladder[tier] == 11)
            failures++;
    }
    save.round = 12;
    if (arena_ladder_index(g, &save, ARENA_COMMON, 0) != 6) failures++;
    save.round = 18;
    if (arena_ladder_index(g, &save, ARENA_COMMON, 0) != 7 ||
        arena_ladder_index(g, &save, ARENA_SUPER_ULTRA_RARE, 0) < 10 ||
        arena_ladder_index(g, &save, ARENA_SUPER_ULTRA_RARE, 0) > 12 ||
        arena_reward_enchant(&save, ARENA_COMMON) > 4 ||
        arena_reward_enchant(&save, ARENA_SUPER_ULTRA_RARE) > 16)
        failures++;
    if (arena_threat_level(10) != 10 || arena_threat_level(18) != 22 ||
        arena_threat_level(50) != 70 || arena_threat_level(100) != 149 ||
        arena_threat_level(1000) != 1679 ||
        arena_endurance_percent(18) != 156 ||
        arena_endurance_percent(50) != 245 ||
        arena_endurance_percent(100) != 395 ||
        arena_endurance_percent(200) != 675 ||
        arena_endurance_percent(1000) != 750)
        failures++;
    for (int roll = 0; roll < 100; roll++) {
        ArenaReward magic = arena_make_reward(g, &save, ARENA_REWARD_SPELL);
        if (!combat_spell_arena_eligible(magic.category, magic.item))
            failures++;
    }
    save.enemy_champion = 1;
    arena_generate_rewards(g, &save);
    if (save.pending_rewards != 2) failures++;
    save.enemy_champion = 0;
    save.round = 5;
    save.current_streak = 4;
    save.enemy_active = 1;
    mw_set_hp_max(&save.character, 1000);
    mw_set_hp_cur(&save.character, 1000);
    save.character.sp_max = save.character.sp_cur = 100.0f;
    arena_apply_skip_penalty(&save);
    if (save.round != 6 || save.current_streak || save.enemy_active ||
        mw_hp_cur(&save.character) != 800 ||
        fabsf(save.character.sp_cur - 80.0f) > 0.01f)
        failures++;
    save.character.level = 1;
    save.current_streak = 1;
    u32 advance_hp = mw_hp_max(&save.character);
    arena_advance_character(g, &save);
    if (save.character.level != 2 || mw_hp_max(&save.character) <= advance_hp)
        failures++;
    save.character.level = 3;
    save.current_streak = 3;
    save.enemy_champion = 0;
    arena_advance_character(g, &save);
    if (save.character.level != 5) failures++;
    {
        ArenaSave easy, hard;
        arena_initialize_save(&easy, &created);
        arena_initialize_save(&hard, &created);
        easy.difficulty = ARENA_DIFFICULTY_EASY;
        hard.difficulty = ARENA_DIFFICULTY_HARD;
        easy.round = hard.round = 18;
        easy.character.level = hard.character.level = 20;
        game_srand(g, 0xD1FF1C01u);
        arena_generate_enemy(g, &easy);
        game_srand(g, 0xD1FF1C01u);
        arena_generate_enemy(g, &hard);
        if (easy.enemy_level < 17 || easy.enemy_level > 25 ||
            hard.enemy_level < 22 || hard.enemy_level > 29 ||
            arena_skip_percent(&easy) != 15 ||
            arena_skip_percent(&hard) != 25 ||
            strcmp(arena_difficulty_name(easy.difficulty), "EASY") ||
            strcmp(arena_difficulty_name(hard.difficulty), "HARD"))
            failures++;
        easy.enemy_champion = hard.enemy_champion = 1;
        if (arena_skip_percent(&easy) != 25 ||
            arena_skip_percent(&hard) != 45)
            failures++;
    }
    {
        ArenaSave opening;
        arena_initialize_save(&opening, &created);
        for (unsigned round = 1; round <= 5; round++) {
            opening.round = round;
            opening.character.level = (u16)round;
            for (int roll = 0; roll < 100; roll++) {
                arena_generate_enemy(g, &opening);
                const MonsterType *mt = &monster_types[opening.enemy_type];
                if (!arena_opening_monster_ok(mt, round) ||
                    opening.enemy_champion || !opening.enemy_active)
                    failures++;
            }
        }
    }
    /* The original Ball and Puffball rows remain available in Adventure and
       the bestiary, but never enter an arena draft. */
    for (int type = 57; type <= 83; type++)
        if (arena_enemy_type_allowed(type)) failures++;
    if (!arena_enemy_type_allowed(56) || !arena_enemy_type_allowed(84))
        failures++;
    {
        ArenaSave pool;
        arena_initialize_save(&pool, &created);
        for (unsigned round = 1; round <= 200; round++) {
            pool.round = round;
            for (int roll = 0; roll < 20; roll++) {
                arena_generate_enemy(g, &pool);
                if (!arena_enemy_type_allowed(pool.enemy_type)) failures++;
            }
        }
        pool.round = 1000;
        pool.difficulty = ARENA_DIFFICULTY_NORMAL;
        arena_generate_enemy(g, &pool);
        if (pool.enemy_level <= MAX_DUNGEON_FLOOR ||
            pool.enemy_level > MW_PLAYER_LEVEL_MAX ||
            !arena_enemy_type_allowed(pool.enemy_type))
            failures++;
    }
    {
        ArenaSave rescued;
        arena_initialize_save(&rescued, &created);
        rescued.version = 4;
        rescued.round = 4;
        rescued.enemy_active = 1;
        rescued.enemy_hp = rescued.enemy_max_hp = 999;
        mw_set_hp_cur(&rescued.character, 1);
        rescued.character.sp_cur = 0.0f;
        arena_migrate_opening_v5(&rescued);
        if (mw_hp_cur(&rescued.character) != mw_hp_max(&rescued.character) ||
            rescued.character.sp_cur != rescued.character.sp_max ||
            rescued.enemy_active || rescued.enemy_hp || rescued.enemy_max_hp)
            failures++;
    }
    {
        ArenaSave progressed;
        arena_initialize_save(&progressed, &created);
        progressed.version = 5;
        progressed.round = 13;
        progressed.current_streak = 12;
        progressed.character.level = 13;
        arena_migrate_progression_v6(&progressed);
        if (progressed.character.level != 17) failures++;
    }
    {
        ArenaSave progressed;
        arena_initialize_save(&progressed, &created);
        progressed.version = 6;
        progressed.round = 13;
        progressed.current_streak = 12;
        progressed.character.level = 17;
        arena_migrate_progression_v7(&progressed);
        if (progressed.character.level != 18) failures++;
    }
    {
        /* Opening survivability guard: an ordinary 20-stat combatant using
           only the starter equipment (and deliberately taking no reward)
           should normally reach the round-five draft. */
        int survived = 0;
        const int trials = 500;
        for (int trial = 0; trial < trials; trial++) {
            ArenaSave opening;
            arena_initialize_save(&opening, &created);
            game_srand(g, 0xA3E00000u + (u32)trial);
            int alive = 1;
            for (unsigned round = 1; round <= 4 && alive; round++) {
                opening.round = round;
                opening.current_streak = round - 1u;
                arena_generate_enemy(g, &opening);
                arena_sync_game(g, &opening);
                CombatState cs = arena_combat_from_state(&opening);
                for (int exchange = 0; exchange < 100 && cs.monster_hp > 0;
                     exchange++) {
                    int dealt = combat_player_attack(g, &cs,
                                                     &opening.character);
                    cs.monster_hp -= dealt;
                    if (cs.monster_hp <= 0) break;
                    int damage = combat_monster_attack(g, &cs,
                                                       &opening.character);
                    uint64_t hp = mw_hp_cur(&opening.character);
                    mw_set_hp_cur(&opening.character,
                                  damage >= (int)hp ? 0u :
                                  hp - (unsigned)damage);
                    character_tick_effects(g, &opening.character);
                    if (!mw_hp_cur(&opening.character)) {
                        alive = 0;
                        break;
                    }
                }
                if (alive && cs.monster_hp <= 0) {
                    opening.enemy_champion = 0;
                    arena_advance_character(g, &opening);
                } else {
                    alive = 0;
                }
            }
            if (alive) survived++;
        }
        if (survived < trials * 19 / 20) failures++;
        printf("Colosseum conservative round-1-to-4 survival guard: %d/%d\n",
               survived, trials);
        g->arena_active = 0;
        g->arena_champion = 0;
    }
    {
        /* Representative attack-first campaign guard.  Alternate the weapon
           and armor cards, cast nothing, and require the build to reach and
           defeat the first champion consistently.  Raw monster damage is
           applied without the round-1-to-20 cap, making this stricter than
           actual play while still exercising native specials/statuses. */
        int survived = 0;
        const int trials = 300;
        for (int trial = 0; trial < trials; trial++) {
            ArenaSave campaign;
            arena_initialize_save(&campaign, &created);
            campaign.difficulty = ARENA_DIFFICULTY_NORMAL;
            game_srand(g, 0xC0100000u + (u32)trial);
            int alive = 1;
            for (unsigned round = 1; round <= 10 && alive; round++) {
                campaign.round = round;
                arena_generate_enemy(g, &campaign);
                arena_sync_game(g, &campaign);
                CombatState cs = arena_combat_from_state(&campaign);
                for (int exchange = 0; exchange < 100 && cs.monster_hp > 0;
                     exchange++) {
                    cs.monster_hp -= combat_player_attack(
                        g, &cs, &campaign.character);
                    if (cs.monster_hp <= 0) break;
                    int damage = combat_monster_attack(
                        g, &cs, &campaign.character);
                    uint64_t hp = mw_hp_cur(&campaign.character);
                    mw_set_hp_cur(&campaign.character,
                        damage >= (int)hp ? 0u : hp - (unsigned)damage);
                    character_tick_effects(g, &campaign.character);
                    if (!mw_hp_cur(&campaign.character)) alive = 0;
                }
                if (!alive || cs.monster_hp > 0) {
                    alive = 0;
                    break;
                }
                campaign.enemy_champion = (u8)(round % 10u == 0);
                campaign.current_streak++;
                arena_advance_character(g, &campaign);
                if (round % 5u == 0)
                    arena_restore_percent(&campaign.character, 35, 35);
                if ((round + 1u) % 10u == 0)
                    arena_restore_percent(&campaign.character, 60, 60);
                arena_generate_rewards(g, &campaign);
                int first_kind = round % 2u ? ARENA_REWARD_WEAPON :
                                              ARENA_REWARD_ARMOR;
                arena_apply_reward(&campaign,
                                   &campaign.rewards[first_kind]);
                if (campaign.enemy_champion) {
                    arena_roll_rewards(g, &campaign);
                    int second_kind = first_kind == ARENA_REWARD_WEAPON ?
                                      ARENA_REWARD_ARMOR :
                                      ARENA_REWARD_WEAPON;
                    arena_apply_reward(&campaign,
                                       &campaign.rewards[second_kind]);
                }
                campaign.pending_rewards = 0;
                campaign.enemy_active = 0;
            }
            if (alive) survived++;
        }
        if (survived < trials * 4 / 5) failures++;
        printf("Colosseum attack-first round-10 guard: %d/%d\n",
               survived, trials);
        g->arena_active = 0;
        g->arena_champion = 0;
    }
    {
        ArenaSave restarted;
        arena_initialize_save(&restarted, &created);
        restarted.difficulty = ARENA_DIFFICULTY_HARD;
        restarted.sound_mode = 2;
        restarted.total_victories = 77;
        restarted.total_deaths = 4;
        restarted.best_streak = 19;
        restarted.character.level = 88;
        restarted.character.stat_str = 999;
        restarted.character.equipped_weapon = 15;
        restarted.round = 20;
        restarted.current_streak = 19;
        u32 old_run = restarted.run_number;
        arena_begin_new_run(&restarted);
        if (restarted.difficulty != ARENA_DIFFICULTY_HARD ||
            restarted.sound_mode != 2 ||
            restarted.run_number != old_run + 1 || restarted.round != 1 ||
            restarted.current_streak || restarted.character.level != 1 ||
            restarted.character.stat_str != restarted.base_character.stat_str ||
            restarted.character.equipped_weapon != 1 ||
            restarted.character.equipped_armor != 1 ||
            restarted.total_victories != 77 || restarted.total_deaths != 4 ||
            restarted.best_streak != 19)
            failures++;
    }
    {
        ArenaSave legacy;
        arena_initialize_save(&legacy, &created);
        legacy.version = 1;
        legacy.round = 21;
        legacy.current_streak = 20;
        legacy.character.level = 53;
        legacy.character.equipped_weapon = 18;
        mw_set_weapon_inventory_count(&legacy.character, 18, 1);
        mw_set_weapon_enchant(&legacy.character, 18, 1237);
        mw_set_hp_max(&legacy.character, 2003);
        mw_set_hp_cur(&legacy.character, 2003);
        legacy.character.ring_regen = 12;
        arena_migrate_balance_v2(&legacy);
        if (legacy.version != ARENA_SAVE_VERSION ||
            legacy.character.level > 27 ||
            legacy.character.equipped_weapon == 18 ||
            mw_weapon_inventory_count(&legacy.character, 18) ||
            legacy.character.ring_regen > 2 ||
            mw_hp_max(&legacy.character) >= 2003)
            failures++;
    }
    {
        /* Even an implausibly lucky player who takes a Super Ultra Rare
           weapon every round must stay far below the former five-digit
           round-18 melee output. */
        ArenaSave projected;
        arena_initialize_save(&projected, &created);
        for (unsigned round = 1; round < 18; round++) {
            projected.round = round;
            projected.current_streak = round;
            projected.enemy_champion = (u8)(round % 10u == 0);
            arena_advance_character(g, &projected);
            int tier = arena_max_ladder_index(round, 0);
            ArenaReward lucky = {ARENA_REWARD_WEAPON,
                                 ARENA_SUPER_ULTRA_RARE, 0, 0,
                                 (u16)weapon_ladder[tier], 0,
                                 (u32)arena_reward_enchant(
                                     &projected, ARENA_SUPER_ULTRA_RARE)};
            arena_apply_reward(&projected, &lucky);
        }
        projected.round = 18;
        projected.current_streak = 17;
        projected.enemy_champion = 0;
        arena_generate_enemy(g, &projected);
        arena_sync_game(g, &projected);
        CombatState target = arena_combat_from_state(&projected);
        int maximum_damage = 0;
        for (int attack = 0; attack < 2000; attack++) {
            int damage = combat_player_attack(g, &target,
                                              &projected.character);
            if (damage > maximum_damage) maximum_damage = damage;
        }
        if (projected.character.level >= 30 ||
            mw_hp_max(&projected.character) >= 1000 ||
            projected.enemy_max_hp < 1 || maximum_damage <= 0 ||
            maximum_damage >= 2500)
            failures++;
        printf("Colosseum round-18 maximum-loot guard: level %u, HP %u, "
               "enemy HP %u, maximum melee %d\n",
               projected.character.level, mw_hp_max(&projected.character),
               projected.enemy_max_hp, maximum_damage);
        g->arena_active = 0;
        g->arena_champion = 0;
    }
    free(g);
    return failures;
}
