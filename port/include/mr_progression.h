#ifndef MR_PROGRESSION_H
#define MR_PROGRESSION_H

#include <stddef.h>

#include "mr_character.h"
#include "mr_save.h"

typedef enum MRDeathOutcome {
    MR_DEATH_PERMANENT = 0,
    MR_DEATH_REINCARNATED = 1,
    MR_DEATH_RAISED = 2,
    MR_DEATH_RAISE_FAILED = 3
} MRDeathOutcome;

typedef struct MRFountainResult {
    int next_fountain_x;
    int next_fountain_y;
    int health_random_part;
} MRFountainResult;

double mr_experience_threshold(int player_level);
int mr_gain_one_level(MRCharacter *character, MRRng *rng);
float mr_recompute_spell_points(const MRCharacter *character);
int mr_settle_pending_experience(MRCharacter *character, MRRng *rng);
void mr_clamp_character_attributes(MRCharacter *character);
void mr_clear_preparation_effects(MRCharacter *character);
void mr_recompute_town_entry_factors(MRCharacter *character);
MRDeathOutcome mr_resolve_death(MRCharacter *character, MRRng *rng);
int mr_drink_fountain_of_youth(MRSave *save, MRRng *rng,
                               MRSave *save_boundary,
                               MRFountainResult *result);
int mr_progression_self_test(char *error, size_t error_size);

#endif
