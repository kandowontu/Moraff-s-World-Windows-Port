#include "mr_spells.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const mr_preparation_spell_names[MR_PREP_SPELL_COUNT] = {
    "CURE", "SENSE LEVEL", "STRENGTH", "SPEED", "SENSE LOCATION",
    "DESCEND", "FEATHER", "ASCEND", "CHANGE LEVEL", "INVISIBILITY",
    "HEAL", "MOCCIOLO"
};

static const char *const mr_battle_spell_names[MR_BATTLE_SPELL_COUNT] = {
    "GAS", "MAGIC ZOT", "MAGIC BOLT", "SPEED", "LIGHTNING",
    "STRENGTH", "GO AWAY!", "RISE...", "AUTO KILL", "EXPLOSION",
    "HEAL", "GOD?"
};

static int mr_basic_int(float value) {
    return (int)floorf(value);
}

const char *mr_preparation_spell_name(int spell) {
    return spell >= 0 && spell < MR_PREP_SPELL_COUNT ?
           mr_preparation_spell_names[spell] : NULL;
}

const char *mr_battle_spell_name(int spell) {
    return spell >= 0 && spell < MR_BATTLE_SPELL_COUNT ?
           mr_battle_spell_names[spell] : NULL;
}

int mr_spell_cost(int zero_based_spell) {
    if (zero_based_spell < 0 || zero_based_spell >= MR_PREP_SPELL_COUNT)
        return 0;
    return zero_based_spell / 2 + 1;
}

int mr_spellbook_mask(const MRCharacter *player, MRSpellbookMaskType type,
                      int one_based_level) {
    if (!player || type < MR_SPELLBOOK_BATTLE ||
        type > MR_SPELLBOOK_PREPARATION || one_based_level < 1 ||
        one_based_level > 6)
        return 0;
    return (int)player->spellbook_masks[one_based_level - 1][type];
}

int mr_spellbook_knows_choice(const MRCharacter *player,
                              MRSpellbookMaskType type,
                              int one_based_level, int one_based_choice) {
    if (one_based_choice < 1 || one_based_choice > 2) return 0;
    return (mr_spellbook_mask(player, type, one_based_level) &
            one_based_choice) != 0;
}

int mr_spell_dispatch_index(int one_based_level, int one_based_choice) {
    if (one_based_level < 1 || one_based_level > 6 ||
        one_based_choice < 1 || one_based_choice > 2)
        return -1;
    /* DUNSMALL 366F-368E/9187-91A6 computes level*2+choice-2 before the
     * twelve-arm ON GOTO dispatch. */
    return one_based_level * 2 + one_based_choice - 3;
}

int mr_prep_change_level_delta(float random_unit) {
    int delta = mr_basic_int(random_unit * 10.0f) - 5;
    /* DUNSMALL 3895-38A2 maps the zero result to -1, doubling that result's
     * probability and never producing +5. */
    return delta == 0 ? -1 : delta;
}

int mr_prep_mocciolo_outcome(float random_unit) {
    return mr_basic_int(random_unit * 6.0f) + 1;
}

int mr_prep_mocciolo_level_loss_health(int health_max, int health_growth,
                                        float first_random,
                                        float second_random) {
    int value = health_max - mr_basic_int(first_random * 10.0f) -
                mr_basic_int(second_random * 10.0f) -
                health_growth * 2 + 2;
    return value < 1 ? 1 : value;
}

int mr_battle_gas_succeeds(float random_unit, int monster_level) {
    int roll = mr_basic_int(random_unit * 2.0f) + 1;
    return roll != 1 && monster_level < 4;
}

int mr_battle_magic_zot_damage(float random_unit, int player_level) {
    return (mr_basic_int(random_unit * 4.0f) + 1) * player_level + 3;
}

int mr_battle_magic_bolt_damage(float random_unit) {
    return mr_basic_int(random_unit * 27.0f) + 11;
}

int mr_battle_lightning_damage(float random_unit, int player_level) {
    return mr_basic_int(random_unit * player_level * 4.0f) +
           player_level * 2 + 5;
}

int mr_battle_go_away_threshold(float random_unit, int player_level) {
    return mr_basic_int(random_unit * player_level * 2.0f) + 5;
}

int mr_battle_auto_kill_threshold(float random_unit, int player_level) {
    return mr_basic_int(random_unit * player_level * 3.0f) + 5;
}

int mr_battle_explosion_damage(float random_unit, int player_level) {
    return mr_basic_int(random_unit * player_level * 4.0f) +
           player_level * 3 + 20;
}

int mr_battle_large_random_damage(float random_unit) {
    return mr_basic_int(random_unit * 5000.0f) + 1376;
}

int mr_battle_god_outcome(float random_unit) {
    return mr_basic_int(random_unit * 5.0f) + 1;
}

static float mr_spell_next_random(MRSpellRandomUnit random_unit,
                                  void *context) {
    float value = (float)(random_unit ? random_unit(context) : 0.0);
    if (value < 0.0f) return 0.0f;
    if (value >= 1.0f) return nextafterf(1.0f, 0.0f);
    return value;
}

static double mr_spell_rng_adapter(void *context) {
    return mr_rng_next((MRRng *)context);
}

static void mr_spend_points(MRCharacter *player, int amount,
                            int *spent_total) {
    player->spell_points -= amount;
    if (spent_total) *spent_total += amount;
}

int mr_cast_preparation_spell(MRCharacter *player, MRPreparationSpell spell,
                              MRSpellRandomUnit random_unit,
                              void *random_context,
                              MRPreparationCastResult *result) {
    if (!player || !result || spell < 0 || spell >= MR_PREP_SPELL_COUNT)
        return 0;
    memset(result, 0, sizeof(*result));
    int cost = mr_spell_cost(spell);
    if (player->spell_points < cost) {
        result->insufficient_spell_points = 1;
        return 1;
    }
    result->cast = 1;

    switch (spell) {
        case MR_PREP_CURE:
            player->health_current += player->attributes[2];
            if (player->health_current > player->health_max)
                player->health_current = player->health_max;
            mr_spend_points(player, 1, &result->spell_points_spent);
            break;
        case MR_PREP_SENSE_LEVEL:
            mr_spend_points(player, 1, &result->spell_points_spent);
            break;
        case MR_PREP_STRENGTH:
            if (player->active_effects[MR_EFFECT_PREPARATION_STRENGTH] != 0.0f) {
                result->duplicate_effect = 1;
                break;
            }
            player->attributes[0] += 6.0f;
            player->active_effects[MR_EFFECT_PREPARATION_STRENGTH] = 1.0f;
            mr_spend_points(player, 2, &result->spell_points_spent);
            break;
        case MR_PREP_SPEED:
            if (player->active_effects[MR_EFFECT_PREPARATION_SPEED] != 0.0f) {
                result->duplicate_effect = 1;
                break;
            }
            /* The compiled BASIC array is one-based: DS:1F92 is its unused
             * element zero, so DS:1FA6 is gameplay attribute five, Agility. */
            player->attributes[4] += 7.0f;
            player->active_effects[MR_EFFECT_PREPARATION_SPEED] = 1.0f;
            mr_spend_points(player, 2, &result->spell_points_spent);
            break;
        case MR_PREP_SENSE_LOCATION:
            mr_spend_points(player, 3, &result->spell_points_spent);
            break;
        case MR_PREP_DESCEND:
            mr_spend_points(player, 3, &result->spell_points_spent);
            player->dungeon_level += 1.0f;
            result->dungeon_transition = 1;
            break;
        case MR_PREP_FEATHER:
            player->carried_weight -= 250.0f;
            if (player->carried_weight < 0.0f) {
                player->carried_weight = 0.0f;
                mr_spend_points(player, 4, &result->spell_points_spent);
                break;
            }
            result->feather_fell_through_to_ascend = 1;
            if (player->dungeon_level == 0.0f) break;
            mr_spend_points(player, 4, &result->spell_points_spent);
            player->dungeon_level -= 1.0f;
            result->dungeon_transition = 1;
            if (player->dungeon_level == 0.0f) result->town_transition = 1;
            break;
        case MR_PREP_ASCEND:
            if (player->dungeon_level == 0.0f) break;
            mr_spend_points(player, 4, &result->spell_points_spent);
            player->dungeon_level -= 1.0f;
            result->dungeon_transition = 1;
            if (player->dungeon_level == 0.0f) result->town_transition = 1;
            break;
        case MR_PREP_CHANGE_LEVEL: {
            int delta = mr_prep_change_level_delta(
                mr_spell_next_random(random_unit, random_context));
            player->dungeon_level += delta;
            if (player->dungeon_level < 0.0f) player->dungeon_level = 0.5f;
            if (player->dungeon_level > 70.0f) player->dungeon_level = 70.0f;
            mr_spend_points(player, 5, &result->spell_points_spent);
            result->dungeon_transition = 1;
            break;
        }
        case MR_PREP_INVISIBILITY:
            player->active_effects[MR_EFFECT_INVISIBILITY] = 1.0f;
            mr_spend_points(player, 5, &result->spell_points_spent);
            break;
        case MR_PREP_HEAL:
            player->health_current = player->health_max +
                mr_basic_int(player->attributes[2] * .5f);
            mr_spend_points(player, 6, &result->spell_points_spent);
            break;
        case MR_PREP_MOCCIOLO: {
            mr_spend_points(player, 6, &result->spell_points_spent);
            int outcome = mr_prep_mocciolo_outcome(
                mr_spell_next_random(random_unit, random_context));
            result->random_outcome = outcome;
            if (outcome == 1 || outcome == 5) {
                float amount = outcome == 1 ? 1.0f : -1.0f;
                for (int i = 0; i < MR_SAVE_ATTRIBUTES; ++i)
                    player->attributes[i] += amount;
            } else if (outcome == 2) {
                player->dungeon_level = 0.0f;
                player->player_x = 10.0f;
                player->player_y = 10.0f;
                result->dungeon_transition = 1;
                result->town_transition = 1;
            } else if (outcome == 3) {
                player->health_current = player->health_max;
                player->spell_points += 30.0f;
            } else if (outcome == 4) {
                player->dungeon_level = 50.0f;
                result->dungeon_transition = 1;
            } else {
                player->player_level -= 2.0f;
                player->experience *= .25;
                /* The two QuickBASIC RND calls execute left-to-right.  C
                 * argument order is unspecified even though the final sum
                 * happens to be symmetric, so retain the stream boundary
                 * explicitly for scripted/random callback parity. */
                float first_random =
                    mr_spell_next_random(random_unit, random_context);
                float second_random =
                    mr_spell_next_random(random_unit, random_context);
                player->health_max = mr_prep_mocciolo_level_loss_health(
                    (int)player->health_max, (int)player->health_growth_factor,
                    first_random, second_random);
                if (player->health_current > player->health_max)
                    player->health_current = player->health_max;
            }
            break;
        }
        default:
            return 0;
    }
    return 1;
}

int mr_cast_preparation_spell_rng(MRCharacter *player,
                                  MRPreparationSpell spell, MRRng *rng,
                                  MRPreparationCastResult *result) {
    if (!rng) return 0;
    return mr_cast_preparation_spell(player, spell, mr_spell_rng_adapter, rng,
                                     result);
}

static void mr_finish_battle_damage(MRCharacter *player, int cost,
                                    float *monster_health,
                                    MRBattleCastResult *result) {
    mr_spend_points(player, cost, &result->spell_points_spent);
    *monster_health -= result->damage;
    if (*monster_health < 1.0f) {
        result->monster_defeated = 1;
        result->rewarded_defeat = 1;
        result->left_combat = 1;
    } else {
        result->monster_takes_turn = 1;
    }
}

int mr_cast_battle_spell(MRCharacter *player, MRBattleSpell spell,
                         int movement_turn, int monster_level,
                         float *monster_health,
                         MRSpellRandomUnit random_unit, void *random_context,
                         MRBattleCastResult *result) {
    if (!player || !monster_health || !result || spell < 0 ||
        spell >= MR_BATTLE_SPELL_COUNT)
        return 0;
    memset(result, 0, sizeof(*result));
    int cost = mr_spell_cost(spell);
    if (player->spell_points < cost) {
        result->insufficient_spell_points = 1;
        return 1;
    }
    result->cast = 1;

    switch (spell) {
        case MR_BATTLE_GAS:
            if (!mr_battle_gas_succeeds(
                    mr_spell_next_random(random_unit, random_context),
                    monster_level)) {
                result->no_effect = 1;
                mr_spend_points(player, cost, &result->spell_points_spent);
                result->monster_takes_turn = 1;
                break;
            }
            result->damage = (int)mr_battle_large_random_damage(
                mr_spell_next_random(random_unit, random_context));
            mr_finish_battle_damage(player, cost, monster_health, result);
            break;
        case MR_BATTLE_MAGIC_ZOT:
            result->damage = (int)mr_battle_magic_zot_damage(
                mr_spell_next_random(random_unit, random_context),
                (int)player->player_level);
            mr_finish_battle_damage(player, cost, monster_health, result);
            break;
        case MR_BATTLE_MAGIC_BOLT:
            result->damage = (int)mr_battle_magic_bolt_damage(
                mr_spell_next_random(random_unit, random_context));
            mr_finish_battle_damage(player, cost, monster_health, result);
            break;
        case MR_BATTLE_SPEED: {
            float marker = movement_turn == 1 ? 16.0f : (float)movement_turn;
            player->attributes[4] += 11.0f;
            player->active_effects[MR_EFFECT_BATTLE_SPEED] = marker - 1.0f;
            mr_spend_points(player, cost, &result->spell_points_spent);
            result->monster_takes_turn = 1;
            break;
        }
        case MR_BATTLE_LIGHTNING:
            result->damage = (int)mr_battle_lightning_damage(
                mr_spell_next_random(random_unit, random_context),
                (int)player->player_level);
            mr_finish_battle_damage(player, cost, monster_health, result);
            break;
        case MR_BATTLE_STRENGTH: {
            float marker = movement_turn == 1 ? 16.0f : (float)movement_turn;
            player->active_effects[MR_EFFECT_BATTLE_STRENGTH] = marker - 1.0f;
            player->combat_attack_factor += 7.0f;
            mr_spend_points(player, cost, &result->spell_points_spent);
            result->monster_takes_turn = 1;
            break;
        }
        case MR_BATTLE_GO_AWAY:
            if (mr_battle_go_away_threshold(
                    mr_spell_next_random(random_unit, random_context),
                    (int)player->player_level) > monster_level) {
                mr_spend_points(player, cost, &result->spell_points_spent);
                result->monster_defeated = 1;
                result->rewarded_defeat = 1;
                result->suppress_kill_message = 1;
                result->left_combat = 1;
            } else {
                result->no_effect = 1;
                mr_spend_points(player, cost, &result->spell_points_spent);
                result->monster_takes_turn = 1;
            }
            break;
        case MR_BATTLE_RISE:
            mr_spend_points(player, cost, &result->spell_points_spent);
            player->dungeon_level -= 1.0f;
            result->left_combat = 1;
            if (player->dungeon_level == 0.0f) result->town_transition = 1;
            break;
        case MR_BATTLE_AUTO_KILL:
            mr_spend_points(player, cost, &result->spell_points_spent);
            if (mr_battle_auto_kill_threshold(
                    mr_spell_next_random(random_unit, random_context),
                    (int)player->player_level) > monster_level) {
                result->monster_defeated = 1;
                result->rewarded_defeat = 1;
                result->left_combat = 1;
            } else {
                result->no_effect = 1;
                result->monster_takes_turn = 1;
            }
            break;
        case MR_BATTLE_EXPLOSION:
            result->damage = (int)mr_battle_explosion_damage(
                mr_spell_next_random(random_unit, random_context),
                (int)player->player_level);
            mr_finish_battle_damage(player, cost, monster_health, result);
            break;
        case MR_BATTLE_HEAL:
            player->health_current = player->health_max;
            mr_spend_points(player, cost, &result->spell_points_spent);
            result->monster_takes_turn = 1;
            break;
        case MR_BATTLE_GOD: {
            int outcome = mr_battle_god_outcome(
                mr_spell_next_random(random_unit, random_context));
            result->random_outcome = outcome;
            if (outcome == 1) {
                result->damage = (int)mr_battle_large_random_damage(
                    mr_spell_next_random(random_unit, random_context));
                mr_finish_battle_damage(player, cost, monster_health, result);
            } else if (outcome == 2) {
                mr_spend_points(player, 6, &result->spell_points_spent);
                player->dungeon_level = 0.0f;
                player->player_x = 10.0f;
                player->player_y = 10.0f;
                result->left_combat = 1;
                result->town_transition = 1;
            } else if (outcome == 3) {
                mr_spend_points(player, 6, &result->spell_points_spent);
                player->health_current = player->health_max;
            } else if (outcome == 4) {
                mr_spend_points(player, 6, &result->spell_points_spent);
                result->no_effect = 1;
                result->monster_takes_turn = 1;
            } else {
                mr_spend_points(player, 6, &result->spell_points_spent);
                for (int i = 0; i < MR_SAVE_ATTRIBUTES; ++i)
                    player->attributes[i] -= 1.0f;
                /* 94B9-9515 then falls into 9555, charging level six again. */
                mr_spend_points(player, 6, &result->spell_points_spent);
                result->monster_takes_turn = 1;
            }
            break;
        }
        default:
            return 0;
    }
    return 1;
}

int mr_cast_battle_spell_rng(MRCharacter *player, MRBattleSpell spell,
                             int movement_turn, int monster_level,
                             float *monster_health, MRRng *rng,
                             MRBattleCastResult *result) {
    if (!rng) return 0;
    return mr_cast_battle_spell(player, spell, movement_turn, monster_level,
                                monster_health, mr_spell_rng_adapter, rng,
                                result);
}

void mr_expire_battle_spell_effects_on_movement(
    MRCharacter *player, unsigned long *movement_turn) {
    if (!player || !movement_turn) return;
    /* DUNSMALL 0A4F-0AB0. The movement counter is cyclic 1..16. Battle
     * Agility and Strength are tagged with the preceding counter value, so
     * they survive combat and expire only when that value comes around
     * again. Recasting stacks the numeric bonus but overwrites the one tag;
     * this routine therefore removes only one stack, matching the original. */
    if (*movement_turn > 16UL) *movement_turn = 1UL;
    if (player->active_effects[MR_EFFECT_BATTLE_SPEED] ==
        (float)*movement_turn) {
        player->active_effects[MR_EFFECT_BATTLE_SPEED] = 0.0f;
        player->attributes[4] -= 11.0f;
    }
    if (player->active_effects[MR_EFFECT_BATTLE_STRENGTH] ==
        (float)*movement_turn) {
        player->active_effects[MR_EFFECT_BATTLE_STRENGTH] = 0.0f;
        player->combat_attack_factor -= 7.0f;
    }
}

typedef struct MRSpellTestRandom {
    const double *values;
    size_t count;
    size_t index;
} MRSpellTestRandom;

static double mr_spell_test_random(void *context) {
    MRSpellTestRandom *script = (MRSpellTestRandom *)context;
    if (!script || script->index >= script->count) return 0.0;
    return script->values[script->index++];
}

int mr_spells_self_test(char *error, size_t error_size) {
    int failed = 0;
    failed |= mr_spell_cost(0) != 1 || mr_spell_cost(1) != 1;
    failed |= mr_spell_cost(10) != 6 || mr_spell_cost(11) != 6;
    failed |= mr_spell_cost(-1) != 0 || mr_spell_cost(12) != 0;
    failed |= mr_prep_change_level_delta(0.0) != -5;
    failed |= mr_prep_change_level_delta(0.5) != -1;
    failed |= mr_prep_change_level_delta(0.999999) != 4;
    failed |= mr_prep_mocciolo_outcome(0.0) != 1;
    failed |= mr_prep_mocciolo_outcome(0.999999) != 6;
    failed |= mr_prep_mocciolo_level_loss_health(100, 3, .99, .51) != 82;
    failed |= !mr_battle_gas_succeeds(.75, 3);
    failed |= mr_battle_gas_succeeds(.75, 4);
    failed |= mr_battle_gas_succeeds(.25, 3);
    failed |= mr_battle_magic_zot_damage(.0, 10) != 13;
    failed |= mr_battle_magic_zot_damage(.999999, 10) != 43;
    failed |= mr_battle_magic_bolt_damage(.999999) != 37;
    failed |= mr_battle_lightning_damage(.5, 10) != 45;
    failed |= mr_battle_go_away_threshold(.5, 10) != 15;
    failed |= mr_battle_auto_kill_threshold(.5, 10) != 20;
    failed |= mr_battle_explosion_damage(.5, 10) != 70;
    failed |= mr_battle_large_random_damage(.5) != 3876;
    failed |= mr_battle_god_outcome(.0) != 1;
    failed |= mr_battle_god_outcome(.999999) != 5;
    failed |= !mr_preparation_spell_name(MR_PREP_MOCCIOLO) ||
              !mr_battle_spell_name(MR_BATTLE_GOD);

    MRCharacter learned;
    memset(&learned, 0, sizeof(learned));
    learned.spellbook_masks[0][MR_SPELLBOOK_BATTLE] = 2;
    learned.spellbook_masks[0][MR_SPELLBOOK_PREPARATION] = 1;
    learned.spellbook_masks[5][MR_SPELLBOOK_PREPARATION] = 3;
    failed |= mr_spellbook_mask(&learned, MR_SPELLBOOK_BATTLE, 1) != 2 ||
              !mr_spellbook_knows_choice(&learned, MR_SPELLBOOK_BATTLE,
                                         1, 2) ||
              mr_spellbook_knows_choice(&learned, MR_SPELLBOOK_BATTLE,
                                         1, 1) ||
              !mr_spellbook_knows_choice(&learned,
                                         MR_SPELLBOOK_PREPARATION, 6, 1) ||
              !mr_spellbook_knows_choice(&learned,
                                         MR_SPELLBOOK_PREPARATION, 6, 2) ||
              mr_spell_dispatch_index(1, 1) != 0 ||
              mr_spell_dispatch_index(6, 2) != 11 ||
              mr_spell_dispatch_index(0, 1) != -1;

    MRCharacter player;
    MRPreparationCastResult prep;
    MRBattleCastResult battle;
    memset(&player, 0, sizeof(player));
    player.spell_points = 100.0;
    player.dungeon_level = 0.0;
    player.carried_weight = 300.0;
    failed |= !mr_cast_preparation_spell(&player, MR_PREP_FEATHER, NULL, NULL,
                                         &prep);
    failed |= player.carried_weight != 50.0 || player.spell_points != 100.0 ||
              !prep.feather_fell_through_to_ascend || prep.spell_points_spent;

    player.dungeon_level = 2.0;
    player.carried_weight = 100.0;
    failed |= !mr_cast_preparation_spell(&player, MR_PREP_FEATHER, NULL, NULL,
                                         &prep);
    failed |= player.carried_weight != 0.0 || player.dungeon_level != 2.0 ||
              prep.spell_points_spent != 4;

    player.spell_points = 100.0;
    player.dungeon_level = 2.0;
    const double change_values[] = {0.0};
    MRSpellTestRandom change_rng = {change_values, 1, 0};
    failed |= !mr_cast_preparation_spell(
        &player, MR_PREP_CHANGE_LEVEL, mr_spell_test_random, &change_rng,
        &prep);
    failed |= player.dungeon_level != .5 || prep.spell_points_spent != 5 ||
              change_rng.index != 1;

    memset(&player, 0, sizeof(player));
    player.spell_points = 100.0;
    player.player_level = 10.0;
    player.experience = 80.0;
    player.health_max = 100.0;
    player.health_current = 100.0;
    player.health_growth_factor = 3.0;
    const double mocciolo_values[] = {.999999, .99, .51};
    MRSpellTestRandom mocciolo_rng = {mocciolo_values, 3, 0};
    failed |= !mr_cast_preparation_spell(
        &player, MR_PREP_MOCCIOLO, mr_spell_test_random, &mocciolo_rng,
        &prep);
    failed |= prep.random_outcome != 6 || player.player_level != 8.0 ||
              player.experience != 20.0 || player.health_max != 82.0 ||
              player.health_current != 82.0 || mocciolo_rng.index != 3;

    memset(&player, 0, sizeof(player));
    player.spell_points = 100.0;
    player.attributes[4] = 20.0;
    player.attributes[5] = 3.0;
    float monster_health = 100.0f;
    failed |= !mr_cast_battle_spell(&player, MR_BATTLE_SPEED, 1, 3,
                                    &monster_health, NULL, NULL, &battle);
    failed |= !mr_cast_battle_spell(&player, MR_BATTLE_SPEED, 1, 3,
                                    &monster_health, NULL, NULL, &battle);
    failed |= player.attributes[4] != 42.0 || player.attributes[5] != 3.0 ||
              player.active_effects[MR_EFFECT_BATTLE_SPEED] != 15.0;
    /* The marker written at movement turn one is 15. It does not clear on
     * combat exit: only a later successful step whose cyclic counter equals
     * 15 removes one of the two stacked bonuses. */
    unsigned long movement_turn = 14;
    mr_expire_battle_spell_effects_on_movement(&player, &movement_turn);
    failed |= player.attributes[4] != 42.0 || player.attributes[5] != 3.0 ||
              player.active_effects[MR_EFFECT_BATTLE_SPEED] != 15.0;
    movement_turn = 15;
    mr_expire_battle_spell_effects_on_movement(&player, &movement_turn);
    failed |= player.attributes[4] != 31.0 || player.attributes[5] != 3.0 ||
              player.active_effects[MR_EFFECT_BATTLE_SPEED] != 0.0;
    movement_turn = 17;
    mr_expire_battle_spell_effects_on_movement(&player, &movement_turn);
    failed |= movement_turn != 1;

    player.combat_attack_factor = 20.0;
    player.active_effects[MR_EFFECT_BATTLE_STRENGTH] = 6.0;
    movement_turn = 5;
    mr_expire_battle_spell_effects_on_movement(&player, &movement_turn);
    failed |= player.combat_attack_factor != 20.0 ||
              player.active_effects[MR_EFFECT_BATTLE_STRENGTH] != 6.0;
    movement_turn = 6;
    mr_expire_battle_spell_effects_on_movement(&player, &movement_turn);
    failed |= player.combat_attack_factor != 13.0 ||
              player.active_effects[MR_EFFECT_BATTLE_STRENGTH] != 0.0;

    player.spell_points = 100.0;
    player.player_level = 10.0;
    const double go_away_values[] = {.999999};
    MRSpellTestRandom go_away_rng = {go_away_values, 1, 0};
    failed |= !mr_cast_battle_spell(
        &player, MR_BATTLE_GO_AWAY, 2, 3, &monster_health,
        mr_spell_test_random, &go_away_rng, &battle);
    failed |= !battle.rewarded_defeat || !battle.suppress_kill_message ||
              !battle.left_combat || battle.spell_points_spent != 4;

    player.spell_points = 6.0;
    for (int i = 0; i < MR_SAVE_ATTRIBUTES; ++i) player.attributes[i] = 10.0;
    const double god_values[] = {.999999};
    MRSpellTestRandom god_rng = {god_values, 1, 0};
    failed |= !mr_cast_battle_spell(&player, MR_BATTLE_GOD, 2, 3,
                                    &monster_health, mr_spell_test_random,
                                    &god_rng, &battle);
    failed |= battle.random_outcome != 5 || battle.spell_points_spent != 12 ||
              player.spell_points != -6.0 || player.attributes[0] != 9.0;
    if (failed) {
        if (error && error_size)
            snprintf(error, error_size,
                     "DUNSMALL spell formula self-test mismatch");
        return 0;
    }
    return 1;
}
