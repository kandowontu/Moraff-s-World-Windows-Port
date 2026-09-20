#include "mr_loot.h"
#include "mr_math.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int mr_loot_int(float value) {
    return (int)floorf(value);
}

static float mr_loot_random(MRLootRandomUnit random_unit, void *context) {
    float value = (float)(random_unit ? random_unit(context) : 0.0);
    if (value < 0.0f) return 0.0f;
    if (value >= 1.0f) return nextafterf(1.0f, 0.0f);
    return value;
}

static double mr_loot_rng_adapter(void *context) {
    return mr_rng_next((MRRng *)context);
}

int mr_generate_coin_loot(float dungeon_level, int monster_level,
                          MRLootRandomUnit random_unit, void *random_context,
                          MRCoinLoot *loot) {
    if (!loot || dungeon_level < 0.0f || monster_level < 1) return 0;
    memset(loot, 0, sizeof(*loot));
    float level = (float)dungeon_level;
    int half_level = (int)floorf(level * 0.5f);

    if ((int)floorf((float)mr_loot_random(random_unit, random_context) *
                    4.0f + (float)half_level) == 1) {
        float first = (float)mr_loot_random(random_unit, random_context);
        float second = (float)mr_loot_random(random_unit, random_context);
        float amount = mr_qb3_pow(first, 2.3f);
        amount = amount * 8000.0f;
        float random_part = second * 1000.0f;
        amount = amount + random_part;
        amount = amount + 1.0f;
        loot->copper = (int)floorf(amount);
    }
    if ((int)floorf((float)mr_loot_random(random_unit, random_context) *
                    3.0f + (float)half_level) == 1) {
        float first = (float)mr_loot_random(random_unit, random_context);
        float second = (float)mr_loot_random(random_unit, random_context);
        float amount = first * first;
        amount = amount * level;
        amount = amount * (float)monster_level;
        amount = amount * 400.0f;
        float random_part = second * 400.0f;
        amount = amount + random_part;
        amount = amount + 1.0f;
        loot->silver = (int)floorf(amount);
    }
    if ((int)floorf((float)mr_loot_random(random_unit, random_context) *
                    5.0f + (float)half_level) == 1) {
        float first = (float)mr_loot_random(random_unit, random_context);
        float second = (float)mr_loot_random(random_unit, random_context);
        float amount = first * first;
        amount = amount * mr_qb3_pow((float)monster_level, 0.8f);
        amount = amount * level;
        amount = amount * 45.0f;
        float random_part = second * 200.0f;
        amount = amount + random_part;
        amount = amount + 1.0f;
        loot->ivory = (int)floorf(amount);
    }
    if ((int)floorf((float)mr_loot_random(random_unit, random_context) *
                    3.0f) == 1) {
        float first = (float)mr_loot_random(random_unit, random_context);
        float second = (float)mr_loot_random(random_unit, random_context);
        float amount = first * mr_qb3_pow(level + 1.0f, 0.7f);
        amount = amount * second;
        amount = amount * (float)monster_level;
        amount = amount * 25.0f;
        amount = amount + 1.0f;
        loot->gold = (int)floorf(amount);
    }
    if ((int)floorf((float)mr_loot_random(random_unit, random_context) *
                    5.0f) == 1) {
        float first = (float)mr_loot_random(random_unit, random_context);
        float second = (float)mr_loot_random(random_unit, random_context);
        float amount = first * first;
        amount = amount * mr_qb3_pow(level + 1.0f, 1.85f);
        amount = amount * 6.0f;
        float random_part = second * 30.0f;
        amount = amount + random_part;
        amount = amount + 1.0f;
        loot->platinum = (int)floorf(amount);
    }
    int jewel_roll = (int)floorf(
        (float)mr_loot_random(random_unit, random_context) * 4.0f);
    if (jewel_roll == 1 && level > 3.0f) {
        float amount = (float)mr_loot_random(random_unit, random_context);
        amount = amount * (float)monster_level;
        amount = amount * level;
        amount = amount * 29.0f;
        loot->jewels = (int)floorf(amount) + 3;
    }

    loot->coin_units = loot->copper + loot->silver + loot->ivory +
                       loot->gold + loot->platinum;
    loot->carry_weight = (int)floorf((float)loot->coin_units * 0.0625f);
    float treasure = (float)loot->copper / 100.0f;
    treasure = treasure + (float)loot->silver / 10.0f;
    treasure = treasure + (float)loot->ivory * 0.5f;
    treasure = treasure + (float)loot->gold;
    treasure = treasure + (float)loot->platinum * 6.0f;
    treasure = treasure + (float)loot->jewels;
    loot->treasure_value = (int)floorf(treasure);
    return 1;
}

int mr_generate_coin_loot_rng(float dungeon_level, int monster_level,
                              MRRng *rng, MRCoinLoot *loot) {
    if (!rng) return 0;
    return mr_generate_coin_loot(dungeon_level, monster_level,
                                 mr_loot_rng_adapter, rng, loot);
}

static void mr_set_status_bit(MRCharacter *character, int bit) {
    int flags = mr_loot_int(character->status_flags);
    character->status_flags = (float)(flags | bit);
}

static int mr_has_status_bit(const MRCharacter *character, int bit) {
    return (mr_loot_int(character->status_flags) & bit) != 0;
}

static void mr_award_random_weapon(MRCharacter *character,
                                   MRLootRandomUnit random_unit,
                                   void *random_context,
                                   MRPostCombatRewards *rewards) {
    /* B1DF tests class before calling RND: a wizard consumes no inner roll. */
    if (mr_loot_int(character->player_class) == MR_CLASS_WIZARD) return;
    int selected = mr_loot_int(mr_loot_random(random_unit, random_context) *
                               5.0f) + 1;
    if (selected <= 3) {
        int armor = mr_loot_int(character->equipped_armor) + 1;
        if (armor > 3) return;
        character->equipped_armor = armor;
        rewards->weapon_kind = MR_EXTRA_REWARD_ARMOR;
        rewards->weapon_value = armor;
        return;
    }
    if (selected == 4 &&
        character->persistent_values[MR_PERSISTENT_SWORD_OWNED] == 0.0f) {
        character->persistent_values[MR_PERSISTENT_SWORD_OWNED] = 1.0f;
        rewards->weapon_kind = MR_EXTRA_REWARD_SWORD;
        return;
    }
    /* Roll 4 deliberately falls through when the sword is already owned. */
    if (character->persistent_values[MR_PERSISTENT_MACE_OWNED] == 0.0f) {
        character->persistent_values[MR_PERSISTENT_MACE_OWNED] = 1.0f;
        rewards->weapon_kind = MR_EXTRA_REWARD_MACE;
    }
}

static void mr_award_random_wand(MRCharacter *character,
                                 MRLootRandomUnit random_unit,
                                 void *random_context,
                                 MRPostCombatRewards *rewards) {
    int selected = mr_loot_int(mr_loot_random(random_unit, random_context) *
                               9.0f) + 1;
    int charges = mr_loot_int(mr_loot_random(random_unit, random_context) *
                               2.0f) + 1;
    character->persistent_values[MR_PERSISTENT_PURPLE_WAND_CHARGES +
                                 selected - 1] += charges;
    rewards->wand_found = 1;
    rewards->wand_color = selected;
    rewards->wand_charges = charges;
}

static void mr_award_random_pill(MRCharacter *character,
                                 MRLootRandomUnit random_unit,
                                 void *random_context,
                                 MRPostCombatRewards *rewards) {
    int selected = mr_loot_int(mr_loot_random(random_unit, random_context) *
                                6.0f) + 1;
    character->persistent_values[MR_PERSISTENT_BLUE_PILLS + selected - 1] +=
        1.0f;
    rewards->pill_found = 1;
    rewards->pill_color = selected;
}

static void mr_award_magic_sword(MRCharacter *character, int enchantment,
                                 MRPostCombatRewards *rewards) {
    if (mr_loot_int(character->player_class) == MR_CLASS_WIZARD ||
        character->magic_item_values[MR_MAGIC_SWORD_BONUS] >= enchantment) {
        rewards->generic_kind = MR_EXTRA_REWARD_NONE;
        return;
    }
    character->persistent_values[MR_PERSISTENT_SWORD_OWNED] = 1.0f;
    mr_set_status_bit(character, 4);
    character->magic_item_values[MR_MAGIC_SWORD_BONUS] = enchantment;
    rewards->generic_kind = MR_EXTRA_REWARD_MAGIC_SWORD;
}

static void mr_award_magic_mace(MRCharacter *character, int enchantment,
                                MRPostCombatRewards *rewards) {
    if (mr_loot_int(character->player_class) == MR_CLASS_WIZARD ||
        character->magic_item_values[MR_MAGIC_MACE_BONUS] >= enchantment) {
        rewards->generic_kind = MR_EXTRA_REWARD_NONE;
        return;
    }
    character->persistent_values[MR_PERSISTENT_MACE_OWNED] = 1.0f;
    mr_set_status_bit(character, 8);
    character->magic_item_values[MR_MAGIC_MACE_BONUS] = enchantment;
    rewards->generic_kind = MR_EXTRA_REWARD_MAGIC_MACE;
}

static void mr_award_generic_drop(MRCharacter *character,
                                  MRLootRandomUnit random_unit,
                                  void *random_context,
                                  MRPostCombatRewards *rewards) {
    int dungeon_level = mr_loot_int(character->dungeon_level);
    int enchantment =
        mr_loot_int(mr_loot_int(mr_loot_random(random_unit, random_context) *
                                 dungeon_level) / 3.0f) + 1;
    int selected = mr_loot_int(mr_loot_random(random_unit, random_context) *
                               22.0f) + 1;
    rewards->generic_drop_triggered = 1;
    rewards->generic_roll = selected;
    rewards->generic_enchantment = enchantment;
    rewards->generic_kind = MR_EXTRA_REWARD_NONE;

    switch (selected) {
    case 1:
        mr_set_status_bit(character, 1);
        character->magic_item_values[MR_MAGIC_HEALTH_RINGS] += 1.0f;
        rewards->generic_kind = MR_EXTRA_REWARD_HEALTH_RING;
        return;
    case 2:
        if (!mr_has_status_bit(character, 2)) {
            mr_set_status_bit(character, 2);
            rewards->generic_kind = MR_EXTRA_REWARD_BAG_OF_HOLDING;
            return;
        }
        /* AD70-AD9D: an already-owned Bag of Holding falls through to the
         * magic-sword case instead of becoming NOTHING. */
        mr_award_magic_sword(character, enchantment, rewards);
        return;
    case 3:
        mr_award_magic_sword(character, enchantment, rewards);
        return;
    case 4:
        mr_award_magic_mace(character, enchantment, rewards);
        return;
    case 5:
        if (character->magic_item_values[MR_MAGIC_RING_BONUS] >= enchantment)
            return;
        mr_set_status_bit(character, 16);
        character->magic_item_values[MR_MAGIC_RING_BONUS] = enchantment;
        rewards->generic_kind = MR_EXTRA_REWARD_MAGIC_RING;
        return;
    case 6:
        if (mr_loot_int(character->player_class) == MR_CLASS_WIZARD ||
            character->magic_item_values[MR_MAGIC_ARMOR_BONUS] >= enchantment)
            return;
        mr_set_status_bit(character, 32);
        character->magic_item_values[MR_MAGIC_ARMOR_BONUS] = enchantment;
        character->equipped_armor = 4.0f;
        rewards->generic_kind = MR_EXTRA_REWARD_MAGIC_ARMOR;
        return;
    case 7:
        character->magic_item_values[MR_MAGIC_HOLY_GRENADES] += 1.0f;
        rewards->generic_kind = MR_EXTRA_REWARD_HOLY_HAND_GRENADE;
        return;
    case 8:
        if (mr_has_status_bit(character, 64)) return;
        mr_set_status_bit(character, 64);
        rewards->generic_kind = MR_EXTRA_REWARD_FLOOR_SLOSHER;
        return;
    default:
        break;
    }
    if (selected <= 17) {
        int item = selected - 8;
        character->carried_items[item - 1] += 1.0f;
        rewards->generic_kind = MR_EXTRA_REWARD_CARRIED_ITEM;
        rewards->generic_value = item;
        return;
    }

    /* Any initial roll 18..22 opens the book path, which makes a fresh and
     * independent 1..5 attribute selection. Luck is never selected. */
    int attribute =
        mr_loot_int(mr_loot_random(random_unit, random_context) * 5.0f) + 1;
    character->attributes[attribute - 1] += 1.0f;
    rewards->generic_kind = MR_EXTRA_REWARD_ATTRIBUTE_BOOK;
    rewards->generic_value = attribute;
}

int mr_generate_post_combat_rewards_staged(
    MRCharacter *character, int monster_behavior_class,
    MRLootRandomUnit random_unit, void *random_context,
    MRPostCombatRewards *rewards,
    MRPostCombatRewardStageCallback stage_callback,
    void *stage_context) {
    if (!character || !rewards || character->dungeon_level < 0.0f) return 0;
    memset(rewards, 0, sizeof(*rewards));
    int dungeon_level = mr_loot_int(character->dungeon_level);

    if (mr_loot_int(mr_loot_random(random_unit, random_context) * 5.0f) == 1) {
        int level_limit = mr_loot_int(dungeon_level * .5f + 1.0f);
        int spell_level;
        do {
            spell_level =
                mr_loot_int(mr_loot_random(random_unit, random_context) *
                            level_limit) + 1;
        } while (spell_level > 6);
        int spell_bit =
            mr_loot_int(mr_loot_random(random_unit, random_context) * 2.0f) + 1;
        int book_type =
            mr_loot_int(mr_loot_random(random_unit, random_context) * 2.0f) == 1
                ? MR_SPELLBOOK_BATTLE
                : MR_SPELLBOOK_PREPARATION;
        int row = spell_level - 1;
        int mask = mr_loot_int(character->spellbook_masks[row][book_type]);
        if ((mask & spell_bit) == 0) {
            character->spellbook_masks[row][book_type] =
                (float)(mask | spell_bit);
            rewards->spellbook_found = 1;
            rewards->spellbook_level = spell_level;
            rewards->spellbook_type = book_type;
            rewards->spellbook_bit = spell_bit;
        }
    }
    if (stage_callback)
        stage_callback(MR_POST_COMBAT_REWARD_SPELLBOOK, rewards,
                       stage_context);

    /* All four outer gates consume their RND values regardless of the other
     * half of the BASIC AND expression. */
    int weapon_roll =
        mr_loot_int(mr_loot_random(random_unit, random_context) * 8.0f);
    if (dungeon_level < 9 && weapon_roll == 1)
        mr_award_random_weapon(character, random_unit, random_context, rewards);
    if (stage_callback)
        stage_callback(MR_POST_COMBAT_REWARD_WEAPON, rewards, stage_context);

    int wand_roll =
        mr_loot_int(mr_loot_random(random_unit, random_context) * 300.0f);
    if ((monster_behavior_class == 5 || monster_behavior_class == 7) &&
        wand_roll < dungeon_level + 40)
        mr_award_random_wand(character, random_unit, random_context, rewards);
    if (stage_callback)
        stage_callback(MR_POST_COMBAT_REWARD_WAND, rewards, stage_context);

    int pill_roll =
        mr_loot_int(mr_loot_random(random_unit, random_context) * 160.0f);
    if (monster_behavior_class == 5 && pill_roll < dungeon_level + 25)
        mr_award_random_pill(character, random_unit, random_context, rewards);
    if (stage_callback)
        stage_callback(MR_POST_COMBAT_REWARD_PILL, rewards, stage_context);

    int item_level_gate =
        mr_loot_int(mr_loot_random(random_unit, random_context) *
                    dungeon_level + 7.0f) + 1;
    int item_chance =
        mr_loot_int(mr_loot_random(random_unit, random_context) * 5.0f) + 1;
    if (item_level_gate > 10 && item_chance == 1)
        mr_award_generic_drop(character, random_unit, random_context, rewards);
    if (stage_callback)
        stage_callback(MR_POST_COMBAT_REWARD_GENERIC, rewards, stage_context);
    return 1;
}

int mr_generate_post_combat_rewards(MRCharacter *character,
                                    int monster_behavior_class,
                                    MRLootRandomUnit random_unit,
                                    void *random_context,
                                    MRPostCombatRewards *rewards) {
    return mr_generate_post_combat_rewards_staged(
        character, monster_behavior_class, random_unit, random_context,
        rewards, NULL, NULL);
}

int mr_generate_post_combat_rewards_rng(MRCharacter *character,
                                        int monster_behavior_class,
                                        MRRng *rng,
                                        MRPostCombatRewards *rewards) {
    if (!rng) return 0;
    return mr_generate_post_combat_rewards(character, monster_behavior_class,
                                           mr_loot_rng_adapter, rng, rewards);
}

int mr_generate_post_combat_rewards_rng_staged(
    MRCharacter *character, int monster_behavior_class, MRRng *rng,
    MRPostCombatRewards *rewards,
    MRPostCombatRewardStageCallback stage_callback,
    void *stage_context) {
    if (!rng) return 0;
    return mr_generate_post_combat_rewards_staged(
        character, monster_behavior_class, mr_loot_rng_adapter, rng, rewards,
        stage_callback, stage_context);
}

typedef struct MRScriptedLootRandom {
    const double *values;
    size_t count;
    size_t position;
} MRScriptedLootRandom;

static double mr_scripted_loot_random(void *context) {
    MRScriptedLootRandom *script = (MRScriptedLootRandom *)context;
    if (!script || script->position >= script->count) return 0.0;
    return script->values[script->position++];
}

typedef struct MRScriptedRewardStages {
    MRScriptedLootRandom *random;
    int count;
    MRPostCombatRewardStage stages[5];
    size_t positions[5];
} MRScriptedRewardStages;

static void mr_scripted_reward_stage(
    MRPostCombatRewardStage stage, const MRPostCombatRewards *rewards,
    void *context) {
    (void)rewards;
    MRScriptedRewardStages *trace = (MRScriptedRewardStages *)context;
    if (!trace || !trace->random || trace->count >= 5) return;
    trace->stages[trace->count] = stage;
    trace->positions[trace->count] = trace->random->position;
    trace->count++;
}

int mr_loot_self_test(char *error, size_t error_size) {
    int failed = 0;
    MRCoinLoot loot;
    const double shallow_values[] = {
        .1, .5, .2, .1, .5, .2, .1, .5, .2,
        .4, .5, .2, .3, .5, .2, .3
    };
    MRScriptedLootRandom shallow = {
        shallow_values, sizeof(shallow_values) / sizeof(shallow_values[0]), 0
    };
    failed |= !mr_generate_coin_loot(2, 3, mr_scripted_loot_random,
                                     &shallow, &loot);
    failed |= loot.copper != 1825 || loot.silver != 681 ||
              loot.ivory != 95 || loot.gold != 17 || loot.platinum != 18 ||
              loot.jewels != 0 || loot.coin_units != 2636 ||
              loot.carry_weight != 164 || loot.treasure_value != 258 ||
              shallow.position != 16;
    MRCoinLoot shallow_result = loot;
    size_t shallow_position = shallow.position;

    const double deep_values[] = {
        .1, .1, .1, .4, .5, .2, .3, .5, .2, .3, .5
    };
    MRScriptedLootRandom deep = {
        deep_values, sizeof(deep_values) / sizeof(deep_values[0]), 0
    };
    failed |= !mr_generate_coin_loot(10, 12, mr_scripted_loot_random,
                                     &deep, &loot);
    failed |= loot.copper != 0 || loot.silver != 0 || loot.ivory != 0 ||
              loot.gold != 161 || loot.platinum != 133 ||
              loot.jewels != 1743 || loot.coin_units != 294 ||
              loot.carry_weight != 18 || loot.treasure_value != 2702 ||
              deep.position != 11;

    MRCharacter reward_character;
    MRPostCombatRewards rewards;
    memset(&reward_character, 0, sizeof(reward_character));
    reward_character.player_class = MR_CLASS_FIGHTER;
    reward_character.dungeon_level = 4;
    const double reward_values[] = {
        .21, .90, .10, .60,       /* level-3 battle spellbook, bit 1 */
        .14, .01,                 /* weapon gate, armor roll */
        .00, .20, .90,            /* wand gate, color 2, two charges */
        .00, .99,                 /* pill gate, color 6 */
        .90, .10, .70, .33        /* item gate, +1 floor slosher */
    };
    MRScriptedLootRandom reward_script = {
        reward_values, sizeof(reward_values) / sizeof(reward_values[0]), 0
    };
    failed |= !mr_generate_post_combat_rewards(
        &reward_character, 5, mr_scripted_loot_random, &reward_script,
        &rewards);
    failed |= !rewards.spellbook_found || rewards.spellbook_level != 3 ||
              rewards.spellbook_type != MR_SPELLBOOK_BATTLE ||
              rewards.spellbook_bit != 1 ||
              reward_character.spellbook_masks[2][MR_SPELLBOOK_BATTLE] != 1 ||
              rewards.weapon_kind != MR_EXTRA_REWARD_ARMOR ||
              reward_character.equipped_armor != 1 ||
              !rewards.wand_found || rewards.wand_color != 2 ||
              rewards.wand_charges != 2 ||
              reward_character.persistent_values[MR_PERSISTENT_BROWN_WAND_CHARGES] != 2 ||
              !rewards.pill_found || rewards.pill_color != 6 ||
              reward_character.persistent_values[MR_PERSISTENT_WHITE_PILLS] != 1 ||
              rewards.generic_kind != MR_EXTRA_REWARD_FLOOR_SLOSHER ||
              (mr_loot_int(reward_character.status_flags) & 64) == 0 ||
              reward_script.position != 15;

    /* A9FE-B2CE presents each category before rolling the next one.  Record
     * the scripted stream position at every callback to guard that ordering
     * without inventing RND consumption in the raw acknowledgement waits. */
    memset(&reward_character, 0, sizeof(reward_character));
    reward_character.player_class = MR_CLASS_FIGHTER;
    reward_character.dungeon_level = 4;
    MRScriptedLootRandom staged_script = {
        reward_values, sizeof(reward_values) / sizeof(reward_values[0]), 0
    };
    MRScriptedRewardStages staged_trace = {0};
    staged_trace.random = &staged_script;
    failed |= !mr_generate_post_combat_rewards_staged(
        &reward_character, 5, mr_scripted_loot_random, &staged_script,
        &rewards, mr_scripted_reward_stage, &staged_trace);
    failed |= staged_trace.count != 5 ||
              staged_trace.stages[0] != MR_POST_COMBAT_REWARD_SPELLBOOK ||
              staged_trace.stages[1] != MR_POST_COMBAT_REWARD_WEAPON ||
              staged_trace.stages[2] != MR_POST_COMBAT_REWARD_WAND ||
              staged_trace.stages[3] != MR_POST_COMBAT_REWARD_PILL ||
              staged_trace.stages[4] != MR_POST_COMBAT_REWARD_GENERIC ||
              staged_trace.positions[0] != 4 ||
              staged_trace.positions[1] != 6 ||
              staged_trace.positions[2] != 9 ||
              staged_trace.positions[3] != 11 ||
              staged_trace.positions[4] != 15 ||
              staged_script.position != 15;

    memset(&reward_character, 0, sizeof(reward_character));
    reward_character.player_class = MR_CLASS_FIGHTER;
    reward_character.dungeon_level = 10;
    reward_character.status_flags = 2; /* duplicate Bag of Holding */
    const double fallthrough_values[] = {
        .00, .00, .99, .99, .40, .10, .90, .05
    };
    MRScriptedLootRandom fallthrough = {
        fallthrough_values,
        sizeof(fallthrough_values) / sizeof(fallthrough_values[0]), 0
    };
    failed |= !mr_generate_post_combat_rewards(
        &reward_character, 1, mr_scripted_loot_random, &fallthrough,
        &rewards);
    failed |= rewards.generic_roll != 2 ||
              rewards.generic_enchantment != 4 ||
              rewards.generic_kind != MR_EXTRA_REWARD_MAGIC_SWORD ||
              reward_character.magic_item_values[MR_MAGIC_SWORD_BONUS] != 4 ||
              reward_character.persistent_values[MR_PERSISTENT_SWORD_OWNED] != 1 ||
              (mr_loot_int(reward_character.status_flags) & 4) == 0 ||
              fallthrough.position != 8;

    /* award_random_weapon, B1DF-B2CE, has several easy-to-flatten control
     * edges: Wizards return before consuming RND; roll four falls through
     * to the mace only when the sword is already owned; armor above three
     * is not replaced. Exercise those edges independently from the outer
     * A9FE reward gates. */
    memset(&reward_character, 0, sizeof(reward_character));
    memset(&rewards, 0, sizeof(rewards));
    reward_character.player_class = MR_CLASS_WIZARD;
    const double wizard_weapon_values[] = {.65};
    MRScriptedLootRandom wizard_weapon = {
        wizard_weapon_values, 1, 0
    };
    mr_award_random_weapon(&reward_character, mr_scripted_loot_random,
                           &wizard_weapon, &rewards);
    failed |= wizard_weapon.position != 0 ||
              rewards.weapon_kind != MR_EXTRA_REWARD_NONE;

    memset(&reward_character, 0, sizeof(reward_character));
    memset(&rewards, 0, sizeof(rewards));
    reward_character.player_class = MR_CLASS_FIGHTER;
    const double sword_weapon_values[] = {.65}; /* INT(.65*5)+1 = 4 */
    MRScriptedLootRandom sword_weapon = {sword_weapon_values, 1, 0};
    mr_award_random_weapon(&reward_character, mr_scripted_loot_random,
                           &sword_weapon, &rewards);
    failed |= sword_weapon.position != 1 ||
              rewards.weapon_kind != MR_EXTRA_REWARD_SWORD ||
              reward_character.persistent_values[
                  MR_PERSISTENT_SWORD_OWNED] != 1.0f;

    memset(&rewards, 0, sizeof(rewards));
    const double mace_fallthrough_values[] = {.65};
    MRScriptedLootRandom mace_fallthrough = {
        mace_fallthrough_values, 1, 0
    };
    mr_award_random_weapon(&reward_character, mr_scripted_loot_random,
                           &mace_fallthrough, &rewards);
    failed |= mace_fallthrough.position != 1 ||
              rewards.weapon_kind != MR_EXTRA_REWARD_MACE ||
              reward_character.persistent_values[
                  MR_PERSISTENT_MACE_OWNED] != 1.0f;

    memset(&reward_character, 0, sizeof(reward_character));
    memset(&rewards, 0, sizeof(rewards));
    reward_character.player_class = MR_CLASS_FIGHTER;
    reward_character.equipped_armor = 3.0f;
    const double max_armor_values[] = {.01};
    MRScriptedLootRandom max_armor = {max_armor_values, 1, 0};
    mr_award_random_weapon(&reward_character, mr_scripted_loot_random,
                           &max_armor, &rewards);
    failed |= max_armor.position != 1 ||
              rewards.weapon_kind != MR_EXTRA_REWARD_NONE ||
              reward_character.equipped_armor != 3.0f;

    if (failed) {
        if (error && error_size)
            snprintf(error, error_size,
                     "DUNSMALL loot mismatch: shallow=%d,%d,%d,%d,%d,%d "
                     "units=%d weight=%d value=%d rnd=%llu; "
                     "deep=%d,%d,%d,%d,%d,%d units=%d weight=%d value=%d "
                     "rnd=%llu post=%llu fallthrough=%llu",
                     shallow_result.copper, shallow_result.silver,
                     shallow_result.ivory, shallow_result.gold,
                     shallow_result.platinum, shallow_result.jewels,
                     shallow_result.coin_units, shallow_result.carry_weight,
                     shallow_result.treasure_value,
                     (unsigned long long)shallow_position,
                     loot.copper, loot.silver, loot.ivory, loot.gold,
                     loot.platinum, loot.jewels, loot.coin_units,
                     loot.carry_weight, loot.treasure_value,
                     (unsigned long long)deep.position,
                     (unsigned long long)reward_script.position,
                     (unsigned long long)fallthrough.position);
        return 0;
    }
    return 1;
}
