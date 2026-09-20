#include "mr_combat.h"
#include "mr_monsters.h"
#include "mr_progression.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int mr_basic_int(float value) {
    return (int)floorf(value);
}

static double mr_rng_adapter(void *context) {
    return mr_rng_next((MRRng *)context);
}

void mr_monster_combat_profile(int resource_set, int type, int level,
                               MRMonsterCombatProfile *profile) {
    if (!profile) return;
    memset(profile, 0, sizeof(*profile));
    profile->resource_set = resource_set == 2 ? 2 : 1;
    profile->type = type;
    profile->level = level;

    /* DUNSMALL 82DC-841C. Later assignments deliberately replace earlier
     * broad type ranges in the same order as the compiled BASIC. */
    if (type < 6) profile->behavior_class = 1;
    if (type > 5 && type < 9) profile->behavior_class = 2;
    if (type > 8 && type < 14) {
        profile->behavior_class = 3;
        profile->defense_modifier = -4;
    }
    if (type > 13 && type < 19) profile->behavior_class = 4;
    if (type > 18) profile->behavior_class = 5;
    if (profile->resource_set == 2 && type < 6)
        profile->behavior_class = type;
    if (profile->resource_set == 2 && type > 13 && type < 19)
        profile->behavior_class = 7;
    if (profile->behavior_class == 2) profile->defense_modifier = 4;
    if (profile->resource_set == 2 && type == 16)
        profile->turn_speed_bonus = 20;
    profile->experience_reward =
        mr_monster_experience_reward(level, profile->behavior_class);
}

float mr_player_combat_defense(const MRCharacter *player) {
    if (!player) return 0.0f;
    float defense = player->equipped_armor * 2.0f +
                     player->magic_item_values[MR_MAGIC_MACE_BONUS] +
                     player->magic_item_values[MR_MAGIC_RING_BONUS] +
                     player->agility_defense_factor + 17.0f;
    if ((int)player->player_class == MR_CLASS_FIGHTER)
        defense += floorf(player->player_level / 6.0f) + 5.0f;
    return defense;
}

static float mr_next_random(MRCombatRandomUnit random_unit, void *context) {
    float value = (float)(random_unit ? random_unit(context) : 0.0);
    if (value < 0.0f) return 0.0f;
    if (value >= 1.0f) return nextafterf(1.0f, 0.0f);
    return value;
}

static void mr_clamp_current_health_to_max(MRCharacter *player) {
    if (player->health_current > player->health_max)
        player->health_current = player->health_max;
}

static void mr_monster_strike(MRCharacter *player,
                              const MRMonsterCombatProfile *monster,
                              MRMonsterAttackState *state,
                              MRCombatRandomUnit random_unit, void *context,
                              MRMonsterStrikeResult *strike) {
    memset(strike, 0, sizeof(*strike));
    strike->drain_phrase_index = -1;

    if (state->cannot_strike_counter > 1) {
        state->cannot_strike_counter--;
        strike->could_not_strike = 1;
        return;
    }

    /* Behavior three retains its preceding attack score whenever its prior
     * strike did damage.  The 9A58-9A84 shortcut also replaces the carried
     * damage with the embedded constant 21 before skipping the ordinary
     * d20/defense setup.  Resetting it to zero (as the first native draft
     * did) made every attached-monster follow-up materially too weak and
     * changed the later damage/RNG path. */
    if (monster->behavior_class == 3 && state->damage > 0.0f) {
        state->damage = 21.0f;
    } else {
        state->attack_score = 0.0f;
        state->damage = 0.0f;
        do {
            state->shared_roll +=
                mr_basic_int(mr_next_random(random_unit, context) * 20.0f) + 1;
            state->attack_score += monster->level - 1.0f;
        } while (state->shared_roll == 20.0f);
        state->attack_score += state->shared_roll - 2.0f;
        if (monster->level == 1) state->attack_score -= 2.0f;
    }

    float defense = mr_player_combat_defense(player) +
                    state->shield_defense_bonus;
    if (state->attack_score > defense)
        state->damage +=
            mr_basic_int(mr_next_random(random_unit, context) * 4.0f) + 1;
    if (monster->level < 4 && state->damage > 0.0f)
        state->damage -= 3.0f;
    if (monster->behavior_class == 3 && state->damage > 0.0f)
        strike->stuck_message = 1;
    if (state->attack_score - 15.0f > defense)
        state->damage +=
            mr_basic_int(mr_next_random(random_unit, context) * 12.0f) + 1;
    if (state->attack_score - 30.0f > defense)
        state->damage +=
            mr_basic_int(mr_next_random(random_unit, context) * 26.0f) + 1;

    int level_term = mr_basic_int((monster->level - player->player_level) * 4.0f);
    /* Original control-flow bug: ABS is only applied in the less-than -10
     * branch, turning every value below -10 into positive 10. */
    if (level_term < -10) {
        level_term = -10;
        if (level_term < 0) level_term = -level_term;
    }
    /* 9CB1 consumes the monster-level roll before 9CCC consumes the
     * level-difference roll.  The operands of C's + operator have no
     * specified evaluation order, so sequence the stateful calls. */
    int monster_level_damage =
        mr_basic_int(mr_next_random(random_unit, context) * monster->level);
    int level_difference_damage =
        mr_basic_int(mr_next_random(random_unit, context) * level_term);
    state->damage += monster_level_damage + level_difference_damage;
    if (monster->level > 60)
        state->damage +=
            mr_basic_int(mr_next_random(random_unit, context) * 49.0f) + 18;
    if (monster->behavior_class == 6) state->damage *= 2.0f;
    if (state->damage < 0.0f) state->damage = 0.0f;

    if (state->damage == 0.0f) {
        strike->missed = 1;
        return;
    }

    if (monster->resource_set == 2 && monster->type == 18) {
        state->damage = mr_basic_int(player->health_current * .25f);
        strike->squash_message = 1;
    }
    strike->damage = (int)state->damage;
    player->health_current -= state->damage;

    if (monster->behavior_class == 5) {
        /* 9E02-9E17 multiplies the DOUBLE experience field by the recovered
         * MBF constant .7.  The old transcription mistook the ambiguous
         * single view of that eight-byte constant for zero. */
        player->experience *= .7;
        player->player_level -= 1.0f;
        player->health_max = player->health_max -
            mr_basic_int(mr_next_random(random_unit, context) * 10.0f) -
            player->health_growth_factor + 1.0f;
        /* 9E54-9E79 selects original F2.COM combat message 11..15 before
         * `LEVEL DRAINED!'. Keep the zero-based index so the presentation
         * layer can reproduce the actual line instead of discarding it. */
        strike->drain_phrase_index =
            mr_basic_int(mr_next_random(random_unit, context) * 5.0f) + 10;
        strike->level_drained = 1;
    }

    if (monster->resource_set == 2) {
        if (monster->type == 14 && player->player_level < 25.0f) {
            player->attributes[3] -= 5.0f;
            mr_clamp_character_attributes(player);
            strike->health_drained = 1;
        }
        if (monster->behavior_class == 5) {
            player->attributes[0] -= 1.0f;
            mr_clamp_character_attributes(player);
            strike->strength_drained = 1;
        }
        if (monster->type == 15) {
            player->persistent_values[MR_PERSISTENT_DISEASE] = 1.0f;
            /* DS:1FA6 is BASIC attribute five (Agility); DS:1F92 is the
             * array's unused physical element zero. */
            player->attributes[4] -= 1.0f;
            mr_clamp_character_attributes(player);
            strike->agility_drained = 1;
            strike->disease_applied = 1;
        }
    }
    mr_clamp_current_health_to_max(player);
}

int mr_monster_take_turn(MRCharacter *player,
                         const MRMonsterCombatProfile *monster,
                         MRMonsterAttackState *state,
                         MRCombatRandomUnit random_unit, void *random_context,
                         MRMonsterTurnResult *result) {
    if (!player || !monster || !state || !result || monster->level < 1)
        return 0;
    memset(result, 0, sizeof(*result));
    int repeat_guard = 0;
    for (;;) {
        MRMonsterStrikeResult *strike = &result->strikes[result->strike_count++];
        mr_monster_strike(player, monster, state, random_unit, random_context,
                          strike);
        result->player_dead =
            player->player_level < 0.0f || player->health_current < 0.0f;
        if (result->player_dead || result->strike_count >= 2) break;

        int repeat_roll =
            mr_basic_int(mr_next_random(random_unit, random_context) *
                          (monster->turn_speed_bonus + 13.0f)) + 1;
        if (!repeat_guard && repeat_roll > player->attributes[4]) {
            repeat_guard = 1;
            result->repeated_attack = 1;
            continue;
        }
        break;
    }
    return 1;
}

int mr_monster_take_turn_rng(MRCharacter *player,
                             const MRMonsterCombatProfile *monster,
                             MRMonsterAttackState *state, MRRng *rng,
                             MRMonsterTurnResult *result) {
    if (!rng) return 0;
    return mr_monster_take_turn(player, monster, state, mr_rng_adapter, rng,
                                result);
}

static int mr_valid_player_attack(MRPlayerAttackCode attack) {
    return attack == MR_PLAYER_ATTACK_SWORD ||
           attack == MR_PLAYER_ATTACK_MACE ||
           attack == MR_PLAYER_ATTACK_KNIFE ||
           attack == MR_PLAYER_ATTACK_FIST ||
           attack == MR_PLAYER_ATTACK_BREATHE_FIRE;
}

static int mr_monster_defense_against_player(const MRCharacter *player,
                                             const MRMonsterCombatProfile *monster,
                                             float dungeon_level) {
    int level_difference = mr_basic_int(
        (monster->level - player->player_level) * .7f);
    if (level_difference > 5) level_difference = 5;
    int defense = level_difference + 5 +
                  mr_basic_int(dungeon_level * .25f) +
                  monster->defense_modifier + monster->level;
    if ((int)player->player_class == MR_CLASS_FIGHTER)
        defense = defense - 5 - mr_basic_int(player->player_level / 6.0f);
    return defense;
}

static void mr_finish_player_attack(const MRCharacter *player,
                                    const MRMonsterCombatProfile *monster,
                                    MRPlayerAttackCode attack,
                                    float *monster_health,
                                    MRCombatRandomUnit random_unit,
                                    void *random_context,
                                    MRPlayerAttackResult *result) {
    if (result->damage > 0) {
        *monster_health -= result->damage;
    }
    /* 8D38-8D83 selects original strings 1..5 only for a zero-damage
     * ordinary weapon attack. 8D86-8DBB selects strings 6..10 for every
     * positive attack, including BREATHE FIRE. The C table is zero based,
     * so those ranges are 0..4 and 5..9. Exactly one cosmetic RND is consumed
     * on either path before the defeat and initiative tests. */
    if (result->damage <= 0 && attack != MR_PLAYER_ATTACK_BREATHE_FIRE)
        result->combat_phrase_index =
            mr_basic_int(mr_next_random(random_unit, random_context) * 5.0f);
    else if (result->damage > 0)
        result->combat_phrase_index =
            mr_basic_int(mr_next_random(random_unit, random_context) * 5.0f) + 5;
    result->monster_defeated = *monster_health < 1.0f;
    if (result->monster_defeated) return;

    int initiative =
        mr_basic_int(mr_next_random(random_unit, random_context) * 50.0f) +
        monster->turn_speed_bonus + 1;
    /* 8E68 compares against DS:1FA6. The BASIC array has an unused element
     * zero at DS:1F92, making 1FA6 attribute five (Agility), native [4]. */
    if (initiative < player->attributes[4])
        result->player_retains_turn = 1;
    else
        result->monster_takes_turn = 1;
}

int mr_player_attack(const MRCharacter *player,
                     const MRMonsterCombatProfile *monster,
                     float dungeon_level, MRPlayerAttackCode attack,
                     float *monster_health, MRPlayerAttackState *state,
                     MRCombatRandomUnit random_unit, void *random_context,
                     MRPlayerAttackResult *result) {
    if (!player || !monster || !monster_health || !state || !result ||
        monster->level < 1 || !mr_valid_player_attack(attack))
        return 0;
    memset(result, 0, sizeof(*result));
    result->combat_phrase_index = -1;
    result->monster_defense =
        mr_monster_defense_against_player(player, monster, dungeon_level);

    if (attack == MR_PLAYER_ATTACK_BREATHE_FIRE) {
        result->damage =
            mr_basic_int(mr_next_random(random_unit, random_context) * 30.0f) + 10;
        result->damage_before_weapon_modifier = result->damage;
        mr_finish_player_attack(player, monster, attack, monster_health,
                                random_unit, random_context, result);
        return 1;
    }

    int roll;
    do {
        roll = mr_basic_int(
                   mr_next_random(random_unit, random_context) * 20.0f) + 1;
        result->d20_rolls++;
        result->last_d20_roll = roll;
        if (roll == 20) result->natural_twenties++;
        result->attack_score +=
            mr_basic_int(player->combat_attack_factor * .7f) + roll +
            player->player_level;
    } while (roll == 20);

    if (attack == MR_PLAYER_ATTACK_SWORD)
        result->attack_score +=
            player->magic_item_values[MR_MAGIC_SWORD_BONUS];
    if (attack == MR_PLAYER_ATTACK_MACE)
        result->attack_score +=
            player->magic_item_values[MR_MAGIC_MACE_BONUS];

    float damage = 0.0f;
    if (result->monster_defense < result->attack_score)
        damage += mr_next_random(random_unit, random_context) * 10.0f +
                  player->combat_attack_factor + 1.0f;
    if (result->attack_score - 15.0f > result->monster_defense)
        damage += mr_next_random(random_unit, random_context) * 10.0f +
                  player->combat_attack_factor + 1.0f;
    if (result->attack_score - 30.0f > result->monster_defense)
        damage += mr_next_random(random_unit, random_context) * 12.0f +
                  player->combat_attack_factor * 2.0f + 1.0f;

    damage = mr_basic_int(damage);
    result->damage_before_weapon_modifier = (int)damage;
    if (damage >= 1.0f) {
        if (monster->behavior_class == 1 &&
            attack == MR_PLAYER_ATTACK_MACE)
            damage = mr_basic_int(damage * .5f + 1.0f);
        if (attack == MR_PLAYER_ATTACK_KNIFE)
            damage = mr_basic_int(damage * .5f) + 1.0f;
        if (attack == MR_PLAYER_ATTACK_FIST)
            damage = mr_basic_int(damage / 3.0f) + 1.0f;
        if (monster->behavior_class == 2 &&
            attack == MR_PLAYER_ATTACK_SWORD)
            damage = mr_basic_int(damage * .5f + 1.0f);
        damage += state->one_shot_damage_bonus;
        state->one_shot_damage_bonus = 0;
    }
    if (damage < 0.0f) damage = 0.0f;
    result->damage = (int)damage;
    mr_finish_player_attack(player, monster, attack, monster_health,
                            random_unit, random_context, result);
    return 1;
}

int mr_player_attack_rng(const MRCharacter *player,
                         const MRMonsterCombatProfile *monster,
                         float dungeon_level, MRPlayerAttackCode attack,
                         float *monster_health, MRPlayerAttackState *state,
                         MRRng *rng, MRPlayerAttackResult *result) {
    if (!rng) return 0;
    return mr_player_attack(player, monster, dungeon_level, attack,
                            monster_health, state, mr_rng_adapter, rng, result);
}

typedef struct MRScriptedRandom {
    const double *values;
    size_t count;
    size_t position;
} MRScriptedRandom;

static double mr_scripted_random(void *context) {
    MRScriptedRandom *script = (MRScriptedRandom *)context;
    if (!script || script->position >= script->count) return 0.0;
    return script->values[script->position++];
}

static void mr_test_player(MRCharacter *player) {
    memset(player, 0, sizeof(*player));
    player->player_class = MR_CLASS_WIZARD;
    player->player_level = 1;
    player->health_max = 100;
    player->health_current = 100;
    player->attributes[0] = 12;
    player->attributes[3] = 12;
    player->attributes[4] = 100;
    player->attributes[5] = 100;
}

int mr_combat_self_test(char *error, size_t error_size) {
    int failed = 0;
    MRMonsterCombatProfile profile;
    mr_monster_combat_profile(1, 7, 10, &profile);
    failed |= profile.behavior_class != 2 || profile.defense_modifier != 4 ||
              profile.experience_reward != 0;
    mr_monster_combat_profile(1, 10, 10, &profile);
    failed |= profile.behavior_class != 3 || profile.defense_modifier != -4;
    mr_monster_combat_profile(2, 3, 10, &profile);
    failed |= profile.behavior_class != 3;
    mr_monster_combat_profile(2, 16, 10, &profile);
    failed |= profile.behavior_class != 7 || profile.turn_speed_bonus != 20;
    failed |= profile.experience_reward !=
              mr_monster_experience_reward(10, 7);

    MRCharacter player;
    mr_test_player(&player);
    MRMonsterAttackState state = {0};
    MRMonsterTurnResult result;
    const double normal_values[] = {.9, .75, .5, .5, 0.0};
    MRScriptedRandom normal = {normal_values, 5, 0};
    mr_monster_combat_profile(1, 1, 10, &profile);
    failed |= !mr_monster_take_turn(&player, &profile, &state,
                                    mr_scripted_random, &normal, &result);
    failed |= result.strike_count != 1 || result.strikes[0].damage != 27;
    failed |= state.shared_roll != 19.0f || state.attack_score != 26.0f;
    failed |= player.health_current != 73.0f || normal.position != 5;

    /* DUNSMALL 9A58-9A84: an attached behavior-three monster whose prior
     * strike did damage skips the d20 loop, keeps its prior attack score,
     * and begins the next strike at exactly 21 damage. */
    mr_test_player(&player);
    state = (MRMonsterAttackState){
        .attack_score = 0,
        .damage = 7
    };
    const double stuck_values[] = {0, 0, 0};
    MRScriptedRandom stuck = {stuck_values, 3, 0};
    mr_monster_combat_profile(1, 10, 10, &profile);
    failed |= !mr_monster_take_turn(&player, &profile, &state,
                                    mr_scripted_random, &stuck, &result);
    failed |= result.strike_count != 1 ||
              !result.strikes[0].stuck_message ||
              result.strikes[0].damage != 21 ||
              player.health_current != 79 ||
              state.attack_score != 0 || stuck.position != 3;

    mr_test_player(&player);
    state = (MRMonsterAttackState){.shared_roll = 100};
    const double squash_values[] = {0, 0, 0, 0, 0, 0, 0};
    MRScriptedRandom squash = {squash_values, 7, 0};
    mr_monster_combat_profile(2, 18, 10, &profile);
    failed |= !mr_monster_take_turn(&player, &profile, &state,
                                    mr_scripted_random, &squash, &result);
    failed |= !result.strikes[0].squash_message ||
              result.strikes[0].damage != 25 || player.health_current != 75;

    mr_test_player(&player);
    player.player_level = 5;
    player.health_max = player.health_current = 200;
    player.health_growth_factor = 3;
    player.experience = 123;
    state = (MRMonsterAttackState){.shared_roll = 100};
    const double drain_values[] = {0, 0, 0, 0, 0, 0, 0, 0};
    MRScriptedRandom drain = {drain_values, 8, 0};
    mr_monster_combat_profile(2, 19, 10, &profile);
    failed |= !mr_monster_take_turn(&player, &profile, &state,
                                    mr_scripted_random, &drain, &result);
    failed |= result.strikes[0].damage != 3 ||
              !result.strikes[0].level_drained ||
              !result.strikes[0].strength_drained ||
              result.strikes[0].drain_phrase_index != 10;
    failed |= player.player_level != 4 || fabs(player.experience - 86.1) > 1e-9 ||
              player.health_max != 198 || player.health_current != 197 ||
              player.attributes[0] != 11;

    /* The 2F43 helper runs immediately after the class-five Strength drain,
     * so a one-point attribute remains one rather than becoming zero. */
    mr_test_player(&player);
    player.player_level = 5;
    player.health_max = player.health_current = 200;
    player.health_growth_factor = 3;
    player.attributes[0] = 1;
    state = (MRMonsterAttackState){.shared_roll = 100};
    MRScriptedRandom clamped_drain = {drain_values, 8, 0};
    failed |= !mr_monster_take_turn(&player, &profile, &state,
                                    mr_scripted_random, &clamped_drain,
                                    &result);
    failed |= !result.strikes[0].strength_drained ||
              player.attributes[0] != 1;

    /* Player attack: 10 on the d20 gives score 27. The class-2 sword
     * resistance turns floor(5+10+1)=16 damage into floor(8+1)=9. */
    mr_test_player(&player);
    player.player_level = 10;
    player.combat_attack_factor = 10;
    /* Keep Laziness high but Agility at the exact threshold needed to retain the
     * turn. This catches accidental use of the sixth gameplay attribute. */
    player.attributes[4] = 15;
    player.attributes[5] = 1;
    mr_monster_combat_profile(1, 7, 10, &profile);
    float monster_health = 100.0f;
    MRPlayerAttackState player_attack_state = {0};
    MRPlayerAttackResult player_attack_result;
    const double player_attack_values[] = {.45, .5, .25, 0};
    MRScriptedRandom player_attack = {player_attack_values, 4, 0};
    failed |= !mr_player_attack(&player, &profile, 5,
                                MR_PLAYER_ATTACK_SWORD, &monster_health,
                                &player_attack_state, mr_scripted_random,
                                &player_attack, &player_attack_result);
    failed |= player_attack_result.attack_score != 27 ||
              player_attack_result.monster_defense != 20 ||
              player_attack_result.damage_before_weapon_modifier != 16 ||
              player_attack_result.damage != 9 || monster_health != 91 ||
              !player_attack_result.player_retains_turn ||
              player_attack_result.combat_phrase_index != 6 ||
              player_attack.position != 4;

    /* A natural 20 explodes and adds an entire second attack contribution. */
    mr_test_player(&player);
    player.player_level = 5;
    player.combat_attack_factor = 10;
    player.attributes[4] = 100;
    mr_monster_combat_profile(1, 1, 10, &profile);
    monster_health = 500;
    player_attack_state = (MRPlayerAttackState){0};
    const double exploding_values[] = {.999, 0, 0, 0, 0, 0};
    MRScriptedRandom exploding = {exploding_values, 6, 0};
    failed |= !mr_player_attack(&player, &profile, 5,
                                MR_PLAYER_ATTACK_FIST, &monster_health,
                                &player_attack_state, mr_scripted_random,
                                &exploding, &player_attack_result);
    failed |= player_attack_result.d20_rolls != 2 ||
              player_attack_result.natural_twenties != 1 ||
              player_attack_result.attack_score != 45 ||
              player_attack_result.damage_before_weapon_modifier != 22 ||
              player_attack_result.damage != 8 || monster_health != 492 ||
              exploding.position != 6;

    /* The 240-point wand bonus survives a complete miss; DUNSMALL jumps
     * around both the addition and the reset when pre-modifier damage < 1. */
    mr_test_player(&player);
    player.combat_attack_factor = 1;
    player.attributes[4] = 100;
    mr_monster_combat_profile(1, 18, 100, &profile);
    monster_health = 500;
    player_attack_state = (MRPlayerAttackState){240};
    const double miss_values[] = {0, .999, 0};
    MRScriptedRandom miss = {miss_values, 3, 0};
    failed |= !mr_player_attack(&player, &profile, 70,
                                MR_PLAYER_ATTACK_SWORD, &monster_health,
                                &player_attack_state, mr_scripted_random,
                                &miss, &player_attack_result);
    failed |= player_attack_result.damage != 0 || monster_health != 500 ||
              player_attack_state.one_shot_damage_bonus != 240 ||
              player_attack_result.combat_phrase_index != 4 ||
              miss.position != 3;

    if (failed) {
        if (error && error_size)
            snprintf(error, error_size,
                     "DUNSMALL monster-turn formula self-test mismatch");
        return 0;
    }
    return 1;
}
