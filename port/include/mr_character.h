#ifndef MR_CHARACTER_H
#define MR_CHARACTER_H

#include <stddef.h>
#include <stdint.h>

#include "mr_save.h"

#define MR_RACE_HUMAN 1
#define MR_RACE_DWARF 2
#define MR_RACE_ELF 3
#define MR_RACE_HOBBIT 4
#define MR_CLASS_FIGHTER 1
#define MR_CLASS_WIZARD 2

typedef struct MRRng {
    uint32_t state;
} MRRng;

typedef struct MRCharacterRoll {
    int race;
    int attributes[MR_SAVE_ATTRIBUTES];
    int total_attributes;
    int distributed_points;
    int display_color;
    int combat_attack_factor;
    int health_growth_factor;
    int agility_defense_factor;
    int pocket_money;
    int health_max;
} MRCharacterRoll;

void mr_rng_seed_default(MRRng *rng);
void mr_rng_seed_timer(MRRng *rng, double seconds_since_midnight);
float mr_rng_next(MRRng *rng);

int mr_character_roll(MRRng *rng, int race, int color_enabled,
                      MRCharacterRoll *roll);
int mr_character_accept_roll(const MRCharacterRoll *roll, int player_class,
                             MRCharacter *character);
int mr_character_self_test(char *error, size_t error_size);

#endif
