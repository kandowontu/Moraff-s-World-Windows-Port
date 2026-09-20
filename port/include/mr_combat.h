#ifndef MR_COMBAT_H
#define MR_COMBAT_H

#include <stddef.h>

#include "mr_character.h"
#include "mr_save.h"

typedef double (*MRCombatRandomUnit)(void *context);

typedef struct MRMonsterCombatProfile {
    int resource_set;
    int type;
    int level;
    int behavior_class;
    int defense_modifier;
    /* DUNSMALL DS:B6F8. Deep type 16 sets this to 20. It biases both
     * post-hit initiative and the range of the monster repeat-strike roll. */
    int turn_speed_bonus;
    float experience_reward;
} MRMonsterCombatProfile;

typedef struct MRMonsterAttackState {
    /* DUNSMALL reuses the general numeric input variable at DS:52FC as the
     * monster's roll accumulator. It is intentionally persistent here. */
    float shared_roll;
    float attack_score;
    float damage;
    float shield_defense_bonus;
    int cannot_strike_counter;
} MRMonsterAttackState;

typedef struct MRMonsterStrikeResult {
    int could_not_strike;
    int missed;
    int stuck_message;
    int squash_message;
    int level_drained;
    int health_drained;
    int strength_drained;
    int agility_drained;
    int disease_applied;
    /* Zero-based F2.COM index 10..14 selected by a level-draining strike. */
    int drain_phrase_index;
    int damage;
} MRMonsterStrikeResult;

typedef struct MRMonsterTurnResult {
    MRMonsterStrikeResult strikes[2];
    int strike_count;
    int repeated_attack;
    int player_dead;
} MRMonsterTurnResult;

typedef enum MRPlayerAttackCode {
    MR_PLAYER_ATTACK_SWORD = 'S',
    MR_PLAYER_ATTACK_MACE = 'M',
    MR_PLAYER_ATTACK_KNIFE = 'K',
    MR_PLAYER_ATTACK_FIST = 'F',
    MR_PLAYER_ATTACK_BREATHE_FIRE = 'B'
} MRPlayerAttackCode;

typedef struct MRPlayerAttackState {
    /* DS:B6CE is set to 240 by one wand result. The original only consumes
     * it after an ordinary weapon attack has already produced damage. */
    int one_shot_damage_bonus;
} MRPlayerAttackState;

typedef struct MRPlayerAttackResult {
    float attack_score;
    int monster_defense;
    int damage_before_weapon_modifier;
    int damage;
    int d20_rolls;
    int last_d20_roll;
    int natural_twenties;
    /* Zero-based F2.COM combat-message index: 0..4 for a miss and 5..9 for
     * a positive attack. BREATHE FIRE uses the positive range too. */
    int combat_phrase_index;
    int monster_defeated;
    int player_retains_turn;
    int monster_takes_turn;
} MRPlayerAttackResult;

void mr_monster_combat_profile(int resource_set, int type, int level,
                               MRMonsterCombatProfile *profile);
float mr_player_combat_defense(const MRCharacter *player);

/* Runs the complete original monster turn, including its one permitted
 * agility-based repeat strike, status drains, and QuickBASIC RND call order. */
int mr_monster_take_turn(MRCharacter *player,
                         const MRMonsterCombatProfile *monster,
                         MRMonsterAttackState *state,
                         MRCombatRandomUnit random_unit, void *random_context,
                         MRMonsterTurnResult *result);

int mr_monster_take_turn_rng(MRCharacter *player,
                             const MRMonsterCombatProfile *monster,
                             MRMonsterAttackState *state, MRRng *rng,
                             MRMonsterTurnResult *result);

/* DUNSMALL 89D9-8E73. `monster_health` is the current encounter health and
 * is reduced in place. Natural twenties explode exactly as in the compiled
 * BASIC. BREATHE FIRE follows its adjacent 8985-89AE direct-damage path. */
int mr_player_attack(const MRCharacter *player,
                     const MRMonsterCombatProfile *monster,
                     float dungeon_level, MRPlayerAttackCode attack,
                     float *monster_health, MRPlayerAttackState *state,
                     MRCombatRandomUnit random_unit, void *random_context,
                     MRPlayerAttackResult *result);

int mr_player_attack_rng(const MRCharacter *player,
                         const MRMonsterCombatProfile *monster,
                         float dungeon_level, MRPlayerAttackCode attack,
                         float *monster_health, MRPlayerAttackState *state,
                         MRRng *rng, MRPlayerAttackResult *result);

int mr_combat_self_test(char *error, size_t error_size);

#endif
