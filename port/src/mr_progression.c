#include "mr_progression.h"
#include "mr_math.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int mr_basic_int(float value) {
    return (int)floorf(value);
}

static int mr_basic_fix(float value) {
    return value < 0.0f ? (int)ceilf(value) : (int)floorf(value);
}

double mr_experience_threshold(int player_level) {
    if (player_level < 0) player_level = 0;
    /* DUNSMALL 2094-20E9. The two consecutive $FEXC calls are intentional,
     * and every operation is an MBF single: 1.2^((level^1.1)^1.1). */
    float level = (float)player_level;
    float nested = mr_qb3_pow(level, 1.1f);
    nested = mr_qb3_pow(nested, 1.1f);
    float first = mr_qb3_pow(1.2f, nested);
    first = first * 900.0f;
    float second = mr_qb3_pow(level, 2.4f);
    second = second * 180.0f;
    float result = first + second;
    result = result + -650.0f;
    return result;
}

int mr_gain_one_level(MRCharacter *character, MRRng *rng) {
    if (!character || !rng) return 0;
    /* DUNSMALL 2044-2093. HP max and current receive the same roll. */
    character->player_level += 1.0f;
    int gain = mr_basic_int((float)mr_rng_next(rng) * 15.0f) +
               mr_basic_int(character->health_growth_factor) + 1;
    character->health_max += gain;
    character->health_current += gain;
    if (character->health_max < 1.0f) character->health_max = 1.0f;
    if (character->health_current > character->health_max)
        character->health_current = character->health_max;
    /* award_one_level_at_inn ends at 2090 by calling 1C93. This applies to
     * experience settlement and the Temple's direct level purchase. */
    mr_clear_preparation_effects(character);
    return gain;
}

float mr_recompute_spell_points(const MRCharacter *character) {
    if (!character) return 0.0f;
    /* BASIC attributes 2 and 3 are Intelligence and Wisdom. DUNSMALL
     * 2115-21F2 first creates an attribute contribution, scales it by level,
     * then adds the class's fixed level contribution. */
    float intelligence = character->attributes[1];
    float wisdom = character->attributes[2];
    float level = character->player_level;
    int aptitude = mr_basic_int((intelligence - 12.0f) / 3.0f +
                                wisdom * 0.25f - 3.0f);
    int divisor = character->player_class == MR_CLASS_WIZARD ? 3 : 6;
    int scaled = mr_basic_int(aptitude * level / divisor);
    /* 21BA-21CC multiplies a Wizard's level by three and then divides by
     * two.  This is 1.5 points per level, not `3 * level + 2`. */
    float points = character->player_class == MR_CLASS_WIZARD
                       ? level * 3.0f / 2.0f
                       : level - 4.0f;
    points += scaled;
    /* 21DD-21EF compares the completed result with one and stores zero for
     * every smaller value, including positive fractional results. */
    return points < 1.0f ? 0.0f : points;
}

int mr_settle_pending_experience(MRCharacter *character, MRRng *rng) {
    if (!character || !rng) return 0;
    /* The original compares the combined total against each threshold but
     * does not subtract thresholds. Pending experience is folded into the
     * stored total only after all eligible levels have been awarded. */
    double total = character->experience + character->pending_experience;
    int levels = 0;
    while (total > mr_experience_threshold((int)character->player_level)) {
        mr_gain_one_level(character, rng);
        if (++levels > 10000) break;
    }
    character->spell_points = mr_recompute_spell_points(character);
    character->experience = total;
    character->pending_experience = 0.0f;
    return levels;
}

void mr_clamp_character_attributes(MRCharacter *character) {
    if (!character) return;
    /* DUNSMALL 2F43-2F70 iterates BASIC attributes 1..6 and raises every
     * value below one to exactly one. The original calls this after pills,
     * each monster attribute drain, Flea Bag sickness, and selected input
     * acknowledgements. */
    for (int index = 0; index < MR_SAVE_ATTRIBUTES; ++index)
        if (character->attributes[index] < 1.0f)
            character->attributes[index] = 1.0f;
}

void mr_clear_preparation_effects(MRCharacter *character) {
    if (!character) return;
    /* DUNSMALL 1C93-1CF4. Despite an earlier provisional disassembly
     * label, this is not a numeric clamp. It clears invisibility and
     * removes only preparation bonuses whose marker is exactly one. */
    character->active_effects[MR_EFFECT_INVISIBILITY] = 0.0f;
    if (character->active_effects[MR_EFFECT_PREPARATION_STRENGTH] == 1.0f) {
        character->active_effects[MR_EFFECT_PREPARATION_STRENGTH] = 0.0f;
        character->attributes[0] -= 6.0f;
    }
    if (character->active_effects[MR_EFFECT_PREPARATION_SPEED] == 1.0f) {
        character->active_effects[MR_EFFECT_PREPARATION_SPEED] = 0.0f;
        character->attributes[4] -= 7.0f;
    }
}

void mr_recompute_town_entry_factors(MRCharacter *character) {
    if (!character) return;
    /* DUNSMALL 1CF5-1D92 is the shared town-entry routine used by ordinary
     * upward traversal, teleportation, town-warp spells, the fountain, and
     * surviving death. It deliberately uses harsher coefficients than
     * character creation. */
    character->dungeon_level = 0.0f;
    int health_factor = mr_basic_int(character->attributes[3] * 2.0f - 26.0f);
    if (health_factor < 1) health_factor = mr_basic_fix(health_factor * 0.5f);
    character->health_growth_factor = health_factor;

    float attack = mr_basic_fix(character->attributes[0] - 11.0f) * 0.7f;
    if (attack < 1.0f) attack = mr_basic_fix(attack * 0.5f);
    character->combat_attack_factor = attack;

    float agility = character->attributes[4] - 12.0f;
    character->agility_defense_factor = agility < 1.0f ? 0.0f : agility;
}

int mr_drink_fountain_of_youth(MRSave *save, MRRng *rng,
                               MRSave *save_boundary,
                               MRFountainResult *result) {
    if (!save || !rng) return 0;
    MRFountainResult local = {0};

    /* DUNSMALL 3DC3-3E2E clears BASIC indices [level 1..70][row 1..20].
     * Level zero and every row-zero element deliberately survive. */
    for (int level = 1; level <= 70; level++) {
        for (int row = 1; row <= 20; row++) {
            save->explored[level][row] = 0;
            memset(save->explored_raw[level][row], 0, 4);
        }
    }

    /* B2CF-B307 chooses the next fountain independently on each axis. */
    local.next_fountain_x = mr_basic_int(mr_rng_next(rng) * 15.0f) + 2;
    local.next_fountain_y = mr_basic_int(mr_rng_next(rng) * 15.0f) + 2;
    save->character.persistent_values[MR_PERSISTENT_FOUNTAIN_X] =
        local.next_fountain_x;
    save->character.persistent_values[MR_PERSISTENT_FOUNTAIN_Y] =
        local.next_fountain_y;

    MRCharacter *character = &save->character;
    character->player_level = 0.0f;
    character->experience = 0.0;
    character->player_x = 10.0f;
    character->player_y = 10.0f;

    /* 3E5A-3E6D converts the carried-treasure SINGLE to DOUBLE and assigns
     * it to pocket money. It replaces pocket money rather than adding to
     * it, then clears carried treasure. */
    character->pocket_money = character->carried_treasure;
    character->carried_treasure = 0.0f;

    int health_factor =
        mr_basic_int(character->attributes[3] * 2.0f - 26.0f);
    if (health_factor < 1) health_factor = mr_basic_fix(health_factor * 0.5f);
    character->health_growth_factor = health_factor;
    local.health_random_part = mr_basic_int((float)mr_rng_next(rng) * 10.0f);
    character->health_max =
        local.health_random_part + character->health_growth_factor + 5.0f;
    character->health_current = character->health_max;

    character->dungeon_generation_seed += 2.0f;
    character->pending_experience = 0.0f;
    for (int index = 0; index < MR_SAVE_ATTRIBUTES; index++)
        character->attributes[index] += 5.0f;
    character->spell_points = mr_recompute_spell_points(character);
    mr_clear_preparation_effects(character);
    character->dungeon_level = 0.0f;

    /* The compiled program invokes SAVE at 3F4E before recalculating the
     * three derived factors at 3F51. Exposing that snapshot lets the native
     * persistence layer retain this otherwise invisible ordering quirk. */
    if (save_boundary) *save_boundary = *save;
    mr_recompute_town_entry_factors(character);
    if (result) *result = local;
    return 1;
}

static void mr_clear_death_combat_state(MRCharacter *character) {
    if (character->player_level < 0.0f) character->player_level = 0.0f;
    if (character->experience < 0.0) character->experience = 0.0;
    character->pending_experience = 0.0f;
    if (character->active_effects[MR_EFFECT_BATTLE_SPEED] > 0.0f) {
        character->active_effects[MR_EFFECT_BATTLE_SPEED] = 0.0f;
        character->attributes[4] -= 11.0f;
    }
    if (character->active_effects[MR_EFFECT_BATTLE_STRENGTH] > 0.0f) {
        character->active_effects[MR_EFFECT_BATTLE_STRENGTH] = 0.0f;
        character->combat_attack_factor -= 7.0f;
    }
}

MRDeathOutcome mr_resolve_death(MRCharacter *character, MRRng *rng) {
    if (!character || !rng) return MR_DEATH_PERMANENT;
    mr_clear_death_combat_state(character);

    /* DUNSMALL A0A4-A0DA: 50% permanent, then equal reincarnate/raise
     * branches. $FSHD 02 and 01 are binary shifts (*4 and *2). */
    if (mr_basic_int((float)mr_rng_next(rng) * 4.0f) + 1 > 2)
        return MR_DEATH_PERMANENT;

    if (mr_basic_int((float)mr_rng_next(rng) * 2.0f) + 1 == 1) {
        character->health_current = character->health_max;
        character->player_level = 0.0f;
        character->experience = 0.0;
        for (int i = 0; i < MR_SAVE_ATTRIBUTES; i++)
            character->attributes[i] =
                mr_basic_int((float)mr_rng_next(rng) * 13.0f) + 3;
        int favored = mr_basic_int((float)mr_rng_next(rng) * 6.0f);
        character->attributes[favored] += 10.0f;
        character->dungeon_level = 0.0f;
        character->player_x = 14.0f;
        character->player_y = 12.0f;
        /* A209 waits through 2F3C before A20C-A227 installs the town
         * coordinates and recomputes the three derived factors.  The live
         * presenter owns that acknowledgement boundary. */
        return MR_DEATH_REINCARNATED;
    }

    character->health_current = character->health_max;
    int raise_roll = mr_basic_int((float)mr_rng_next(rng) * 23.0f) + 1;
    /* A113 compares the roll with DS:1FA2.  The BASIC attribute array has
     * an unused element zero at 1F92, making 1FA2 attribute four (Health),
     * not attribute five (Agility at 1FA6). */
    if (raise_roll > character->attributes[3])
        return MR_DEATH_RAISE_FAILED;
    character->dungeon_level = 0.0f;
    character->player_x = 14.0f;
    character->player_y = 12.0f;
    character->attributes[3] -= 1.0f;
    /* A148 subtracts one Health. The state transition itself can therefore
     * temporarily leave Health at zero; the caller's A156 -> 2F3C -> C5B0
     * acknowledgement reaches 2F71 and clamps it back to one. Keeping that
     * input-owned side effect out of this formula routine preserves the
     * original ordering. */
    /* A156 waits through 2F3C (and therefore 2F71's minimum-one attribute
     * clamp) before A159 recomputes the town-entry factors.  Recomputing here
     * used a transient zero Health value after a successful raise. */
    return MR_DEATH_RAISED;
}

int mr_progression_self_test(char *error, size_t error_size) {
    int failed = 0;
    failed |= fabs(mr_experience_threshold(0) - 250.0) > 1e-9;
    failed |= fabs(mr_experience_threshold(1) - 610.0) > 1e-9;
    /* Host pow() produced 11147.529762... here. BRUN30 $FEXC and the
     * intervening MBF-single stores produce the exact value below. */
    failed |= mr_experience_threshold(5) != 11147.5302734375;

    MRCharacter character = {0};
    for (int index = 0; index < MR_SAVE_ATTRIBUTES; ++index)
        character.attributes[index] = index - 2.0;
    mr_clamp_character_attributes(&character);
    failed |= character.attributes[0] != 1.0 ||
              character.attributes[2] != 1.0 ||
              character.attributes[5] != 3.0;

    character = (MRCharacter){0};
    character.player_class = MR_CLASS_WIZARD;
    character.attributes[1] = 18;
    character.attributes[2] = 16;
    character.player_level = 5;
    /* DUNSMALL 2115-21F2: aptitude 3 contributes INT(3*5/3)=5 and the
     * Wizard base is 3*5/2=7.5. */
    failed |= mr_recompute_spell_points(&character) != 12.5f;
    character.player_class = MR_CLASS_FIGHTER;
    failed |= mr_recompute_spell_points(&character) != 3.0;
    character = (MRCharacter){0};
    character.player_class = MR_CLASS_WIZARD;
    character.attributes[1] = 3;
    character.attributes[2] = 12;
    character.player_level = 1;
    /* Base 1.5 plus a -1 aptitude contribution produces .5, which the
     * original's final `< 1` gate clears to zero. */
    failed |= mr_recompute_spell_points(&character) != 0.0f;

    MRRng rng;
    mr_rng_seed_default(&rng);
    character = (MRCharacter){0};
    character.player_class = MR_CLASS_WIZARD;
    character.health_growth_factor = 3;
    character.health_max = 12;
    character.health_current = 8;
    character.attributes[1] = 15;
    character.attributes[2] = 10;
    character.pending_experience = 700;
    character.attributes[0] = 20;
    character.attributes[4] = 20;
    character.active_effects[MR_EFFECT_INVISIBILITY] = 1;
    character.active_effects[MR_EFFECT_PREPARATION_STRENGTH] = 1;
    character.active_effects[MR_EFFECT_PREPARATION_SPEED] = 1;
    failed |= mr_settle_pending_experience(&character, &rng) != 2;
    failed |= character.player_level != 2 || character.health_max != 38 ||
              character.health_current != 34 || character.experience != 700 ||
              character.pending_experience != 0 ||
              character.active_effects[MR_EFFECT_INVISIBILITY] != 0 ||
              character.active_effects[MR_EFFECT_PREPARATION_STRENGTH] != 0 ||
              character.active_effects[MR_EFFECT_PREPARATION_SPEED] != 0 ||
              character.attributes[0] != 14 ||
              character.attributes[4] != 13;

    /* Seeded first roll is .7055, selecting permanent death. */
    mr_rng_seed_default(&rng);
    character = (MRCharacter){0};
    failed |= mr_resolve_death(&character, &rng) != MR_DEATH_PERMANENT;

    /* Script exact branch values through seeds found from the QB3 generator. */
    rng.state = 0x000000U;
    character = (MRCharacter){0};
    character.health_max = 20;
    character.attributes[4] = 30;
    MRDeathOutcome outcome = mr_resolve_death(&character, &rng);
    failed |= outcome < MR_DEATH_PERMANENT || outcome > MR_DEATH_RAISE_FAILED;

    /* Seed 147 selects the raise branch and a raise roll of one. This core
     * transition ends at zero Health; the live A156 acknowledgement applies
     * 2F71's later minimum-one clamp before recomputing town-entry factors. */
    rng.state = 147;
    character = (MRCharacter){0};
    character.health_max = 20;
    character.attributes[3] = 1;
    character.attributes[4] = 19;
    character.health_growth_factor = 77;
    failed |= mr_resolve_death(&character, &rng) != MR_DEATH_RAISED ||
              character.attributes[3] != 0 ||
              character.attributes[4] != 19 ||
              character.health_growth_factor != 77 ||
              character.player_x != 14 || character.player_y != 12 ||
              character.dungeon_level != 0;
    mr_clamp_character_attributes(&character);
    mr_recompute_town_entry_factors(&character);
    failed |= character.attributes[3] != 1 ||
              character.health_growth_factor != -12;

    /* The Fountain of Youth is a complete 3DAE-3F57 state transition, not
     * just a level reset. The fixed QB seed gives x=12, y=10, HP part=5. */
    MRSave save = {0};
    save.character.player_class = MR_CLASS_WIZARD;
    save.character.player_level = 17;
    save.character.experience = 1234;
    save.character.pending_experience = 99;
    save.character.player_x = 4;
    save.character.player_y = 8;
    save.character.dungeon_level = 70;
    save.character.dungeon_generation_seed = 1;
    save.character.carried_treasure = 777;
    save.character.pocket_money = 42;
    save.character.health_growth_factor = 66;
    save.character.combat_attack_factor = 77;
    save.character.agility_defense_factor = 88;
    save.character.attributes[0] = 16;
    save.character.attributes[1] = 10;
    save.character.attributes[2] = 10;
    save.character.attributes[3] = 20;
    save.character.attributes[4] = 27;
    save.character.attributes[5] = 10;
    save.character.active_effects[MR_EFFECT_PREPARATION_STRENGTH] = 1;
    save.character.active_effects[MR_EFFECT_PREPARATION_SPEED] = 1;
    save.character.active_effects[MR_EFFECT_INVISIBILITY] = 1;
    save.explored[0][1] = 9;
    save.explored[1][1] = 7;
    save.explored[70][20] = 3;
    memset(save.explored_raw[1][1], 0x7f, 4);
    mr_rng_seed_default(&rng);
    MRSave fountain_boundary;
    MRFountainResult fountain;
    failed |= !mr_drink_fountain_of_youth(
        &save, &rng, &fountain_boundary, &fountain);
    failed |= fountain.next_fountain_x != 12 ||
              fountain.next_fountain_y != 10 ||
              fountain.health_random_part != 5 ||
              save.character.player_level != 0 ||
              save.character.experience != 0 ||
              save.character.pending_experience != 0 ||
              save.character.player_x != 10 ||
              save.character.player_y != 10 ||
              save.character.dungeon_level != 0 ||
              save.character.dungeon_generation_seed != 3 ||
              save.character.pocket_money != 777 ||
              save.character.carried_treasure != 0 ||
              save.character.health_max != 24 ||
              save.character.health_current != 24 ||
              save.character.health_growth_factor != 24 ||
              save.character.combat_attack_factor != 2.8f ||
              save.character.agility_defense_factor != 13 ||
              save.character.attributes[0] != 15 ||
              save.character.attributes[4] != 25 ||
              save.character.attributes[5] != 15 ||
              save.character.active_effects[MR_EFFECT_INVISIBILITY] != 0 ||
              save.explored[0][1] != 9 || save.explored[1][1] != 0 ||
              save.explored[70][20] != 0 ||
              fountain_boundary.character.health_growth_factor != 14 ||
              fountain_boundary.character.combat_attack_factor != 77 ||
              fountain_boundary.character.agility_defense_factor != 88;

    if (failed) {
        if (error && error_size)
            snprintf(error, error_size,
                     "DUNSMALL progression/death formula self-test mismatch");
        return 0;
    }
    return 1;
}
