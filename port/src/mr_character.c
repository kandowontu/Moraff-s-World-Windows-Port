#include "mr_character.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const int mr_race_attributes[4][MR_SAVE_ATTRIBUTES] = {
    {4, 4, 4, 4, 4, 4},
    {4, 1, 1, 7, 7, 4},
    {2, 6, 5, 3, 5, 3},
    {2, 2, 2, 5, 9, 4},
};

static int mr_basic_int(float value) {
    return (int)floorf(value);
}

static int mr_basic_fix(float value) {
    return value < 0.0f ? (int)ceilf(value) : (int)floorf(value);
}

static unsigned mr_round_mantissa_nearest_even(double value) {
    double integral = floor(value);
    double fraction = value - integral;
    unsigned rounded = (unsigned)integral;
    if (fraction > 0.5 || (fraction == 0.5 && (rounded & 1U))) rounded++;
    return rounded;
}

static void mr_encode_mbf_single(double value, uint8_t bytes[4]) {
    memset(bytes, 0, 4);
    if (value == 0.0 || !isfinite(value)) return;
    int negative = value < 0.0;
    int exponent2 = 0;
    double fraction = frexp(fabs(value), &exponent2);
    int exponent = exponent2 + 128;
    /* BRUN30's mantissa conversion uses round-to-nearest/even. llround()
     * instead rounds an exact half away from zero, changing RANDOMIZE TIMER
     * at half-ULP boundaries even though ordinary seeds look identical. */
    unsigned mantissa = mr_round_mantissa_nearest_even(
        (fraction * 2.0 - 1.0) * 8388608.0);
    if (mantissa >= 8388608U) {
        mantissa = 0;
        exponent++;
    }
    bytes[0] = (uint8_t)(mantissa & 0xFF);
    bytes[1] = (uint8_t)((mantissa >> 8) & 0xFF);
    bytes[2] = (uint8_t)((mantissa >> 16) & 0x7F);
    if (negative) bytes[2] |= 0x80;
    bytes[3] = (uint8_t)exponent;
}

void mr_rng_seed_default(MRRng *rng) {
    if (rng) rng->state = 0x050000U;
}

void mr_rng_seed_timer(MRRng *rng, double seconds_since_midnight) {
    if (!rng) return;
    uint8_t mbf[4];
    mr_encode_mbf_single(seconds_since_midnight, mbf);
    uint16_t low = (uint16_t)(mbf[0] | ((uint16_t)mbf[1] << 8));
    uint16_t high = (uint16_t)(mbf[2] | ((uint16_t)mbf[3] << 8));
    uint16_t mixed = (uint16_t)(low ^ high);
    /* BRUN30 $RZ1 writes the XOR word at state+1, preserving byte zero. */
    rng->state = (rng->state & 0xFFU) | ((uint32_t)mixed << 8);
}

float mr_rng_next(MRRng *rng) {
    if (!rng) return 0.0f;
    /* BRUN30 $RAN at B2D6-B302, reduced modulo its 24-bit state. */
    rng->state = (rng->state * 0xFD43FDU + 0xC39EC3U) & 0xFFFFFFU;
    return rng->state / 16777216.0f;
}

static int mr_random_index(MRRng *rng, int count) {
    return mr_basic_int(mr_rng_next(rng) * count);
}

int mr_character_roll(MRRng *rng, int race, int color_enabled,
                      MRCharacterRoll *roll) {
    if (!rng || !roll || race < MR_RACE_HUMAN || race > MR_RACE_HOBBIT)
        return 0;
    memset(roll, 0, sizeof(*roll));
    roll->race = race;
    memcpy(roll->attributes, mr_race_attributes[race - 1],
           sizeof(roll->attributes));
    roll->distributed_points = mr_random_index(rng, 10) + 52;
    for (int point = 0; point < roll->distributed_points; point++)
        roll->attributes[mr_random_index(rng, MR_SAVE_ATTRIBUTES)]++;
    roll->total_attributes = 24 + roll->distributed_points;

    /* CHCHAR 0BB1-0BD6 gates this cosmetic RND call on the value read from
     * NAME, not on the selected race.  Consequently color mode advances the
     * gameplay RNG before the money/health rolls for every race. */
    roll->display_color = color_enabled ? mr_random_index(rng, 7) + 9 : 7;

    int health = roll->attributes[3];
    int strength = roll->attributes[0];
    int agility = roll->attributes[4];
    roll->health_growth_factor = mr_basic_int(health * 3.0f - 39.0f);
    if (roll->health_growth_factor < 1)
        roll->health_growth_factor =
            mr_basic_fix(roll->health_growth_factor / 3.0f);
    roll->combat_attack_factor = mr_basic_fix(strength - 11.0f);
    if (roll->combat_attack_factor < 1)
        roll->combat_attack_factor =
            mr_basic_fix(roll->combat_attack_factor * 0.5f);
    roll->agility_defense_factor = agility - 12;
    if (roll->agility_defense_factor < 1)
        roll->agility_defense_factor = 0;

    roll->pocket_money = mr_random_index(rng, 10) + 11;
    int health_random_ceiling = mr_random_index(rng, 15) + 2;
    int health_random_part = mr_random_index(rng, health_random_ceiling);
    roll->health_max = mr_random_index(rng, 10) +
                       roll->health_growth_factor + health_random_part;
    return 1;
}

int mr_character_accept_roll(const MRCharacterRoll *roll, int player_class,
                             MRCharacter *character) {
    if (!roll || !character ||
        (player_class != MR_CLASS_FIGHTER && player_class != MR_CLASS_WIZARD))
        return 0;
    memset(character, 0, sizeof(*character));
    for (int i = 0; i < MR_SAVE_ATTRIBUTES; i++)
        character->attributes[i] = roll->attributes[i];
    character->combat_attack_factor = roll->combat_attack_factor;
    character->health_growth_factor = roll->health_growth_factor;
    character->agility_defense_factor = roll->agility_defense_factor;
    character->player_class = player_class;
    character->equipped_armor = 0;
    character->health_max = roll->health_max;
    character->health_current = roll->health_max;
    character->carried_weight = 150;
    character->pocket_money = roll->pocket_money;
    character->player_x = 10;
    character->player_y = 10;
    character->dungeon_level = 0;
    character->dungeon_generation_seed = 1;

    int intelligence = roll->attributes[1];
    int wisdom = roll->attributes[2];
    int base_spell_capacity =
        mr_basic_int(intelligence * 0.5f + wisdom * 0.4f - 10.8f);
    int class_adjustment = player_class == MR_CLASS_WIZARD ? 2 : -4;
    int spell_points = base_spell_capacity + class_adjustment;
    character->spell_points = spell_points > 0 ? spell_points : 0;

    character->persistent_values[0] = 1;  /* starting knife */
    character->persistent_values[20] = roll->race;
    if (player_class == MR_CLASS_WIZARD) {
        character->spellbook_masks[0][0] = 2;
        character->spellbook_masks[0][1] = 1;
    }
    return 1;
}

int mr_character_self_test(char *error, size_t error_size) {
    /* Halfway between mantissas zero/one rounds down to even zero; halfway
     * between one/two rounds up to even two. */
    uint8_t mbf_tie[4];
    mr_encode_mbf_single(1.0 + 0.5 / 8388608.0, mbf_tie);
    if (mbf_tie[0] != 0 || mbf_tie[1] != 0 ||
        (mbf_tie[2] & 0x7f) != 0 || mbf_tie[3] != 129) {
        if (error && error_size)
            snprintf(error, error_size,
                     "QuickBASIC MBF tie-to-even lower boundary mismatch");
        return 0;
    }
    mr_encode_mbf_single(1.0 + 1.5 / 8388608.0, mbf_tie);
    if (mbf_tie[0] != 2 || mbf_tie[1] != 0 ||
        (mbf_tie[2] & 0x7f) != 0 || mbf_tie[3] != 129) {
        if (error && error_size)
            snprintf(error, error_size,
                     "QuickBASIC MBF tie-to-even upper boundary mismatch");
        return 0;
    }
    MRRng rng;
    mr_rng_seed_default(&rng);
    double first = mr_rng_next(&rng);
    if (rng.state != 0xB49EC3U || fabs(first - 0.7055475115776062) > 1e-12) {
        if (error && error_size)
            snprintf(error, error_size, "QuickBASIC 3 RND sequence mismatch");
        return 0;
    }
    /* BRUN30 $RZ1 preserves byte zero of the initialized random state while
     * installing the XOR of TIMER's two MBF words in bytes one through
     * three.  This fixture guards the live DUNSMALL/CHCHAR startup order;
     * seeding an uninitialized native object used to make that low byte
     * nondeterministic. */
    rng.state = 0x05005AU;
    mr_rng_seed_timer(&rng, 12345.25);
    if (rng.state != 0x6B405AU) {
        if (error && error_size)
            snprintf(error, error_size,
                     "QuickBASIC 3 RANDOMIZE TIMER state mismatch");
        return 0;
    }
    mr_rng_seed_default(&rng);
    MRCharacterRoll roll;
    if (!mr_character_roll(&rng, MR_RACE_HUMAN, 1, &roll) ||
        roll.distributed_points != 59 || roll.total_attributes != 83 ||
        roll.attributes[0] != 15 || roll.attributes[1] != 15 ||
        roll.attributes[2] != 10 || roll.attributes[3] != 14 ||
        roll.attributes[4] != 15 || roll.attributes[5] != 14 ||
        roll.display_color != 11 || roll.health_growth_factor != 3 ||
        roll.combat_attack_factor != 4 || roll.agility_defense_factor != 3 ||
        roll.pocket_money != 15 || roll.health_max != 12) {
        if (error && error_size)
            snprintf(error, error_size, "CHCHAR deterministic roll mismatch");
        return 0;
    }
    mr_rng_seed_default(&rng);
    if (!mr_character_roll(&rng, MR_RACE_HUMAN, 0, &roll) ||
        roll.display_color != 7 || roll.pocket_money != 15 ||
        roll.health_max != 11) {
        if (error && error_size)
            snprintf(error, error_size,
                     "CHCHAR monochrome RNG/display-color mismatch");
        return 0;
    }
    MRCharacter fighter, wizard;
    if (!mr_character_accept_roll(&roll, MR_CLASS_FIGHTER, &fighter) ||
        !mr_character_accept_roll(&roll, MR_CLASS_WIZARD, &wizard) ||
        fighter.spell_points != 0 || wizard.spell_points != 2 ||
        fighter.health_current != fighter.health_max ||
        fighter.carried_weight != 150 || fighter.pocket_money != 15 ||
        fighter.persistent_values[0] != 1 ||
        fighter.persistent_values[20] != MR_RACE_HUMAN ||
        wizard.spellbook_masks[0][0] != 2 ||
        wizard.spellbook_masks[0][1] != 1) {
        if (error && error_size)
            snprintf(error, error_size, "CHCHAR initial-state mismatch");
        return 0;
    }
    return 1;
}
