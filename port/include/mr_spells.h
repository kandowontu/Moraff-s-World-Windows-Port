#ifndef MR_SPELLS_H
#define MR_SPELLS_H

#include <stddef.h>
#include <stdint.h>

#include "mr_character.h"
#include "mr_save.h"

typedef enum MRPreparationSpell {
    MR_PREP_CURE = 0,
    MR_PREP_SENSE_LEVEL,
    MR_PREP_STRENGTH,
    MR_PREP_SPEED,
    MR_PREP_SENSE_LOCATION,
    MR_PREP_DESCEND,
    MR_PREP_FEATHER,
    MR_PREP_ASCEND,
    MR_PREP_CHANGE_LEVEL,
    MR_PREP_INVISIBILITY,
    MR_PREP_HEAL,
    MR_PREP_MOCCIOLO,
    MR_PREP_SPELL_COUNT
} MRPreparationSpell;

typedef enum MRBattleSpell {
    MR_BATTLE_GAS = 0,
    MR_BATTLE_MAGIC_ZOT,
    MR_BATTLE_MAGIC_BOLT,
    MR_BATTLE_SPEED,
    MR_BATTLE_LIGHTNING,
    MR_BATTLE_STRENGTH,
    MR_BATTLE_GO_AWAY,
    MR_BATTLE_RISE,
    MR_BATTLE_AUTO_KILL,
    MR_BATTLE_EXPLOSION,
    MR_BATTLE_HEAL,
    MR_BATTLE_GOD,
    MR_BATTLE_SPELL_COUNT
} MRBattleSpell;

typedef double (*MRSpellRandomUnit)(void *context);

typedef struct MRPreparationCastResult {
    int cast;
    int insufficient_spell_points;
    int duplicate_effect;
    int spell_points_spent;
    int dungeon_transition;
    int town_transition;
    int feather_fell_through_to_ascend;
    int random_outcome;
} MRPreparationCastResult;

typedef struct MRBattleCastResult {
    int cast;
    int insufficient_spell_points;
    int spell_points_spent;
    int no_effect;
    int damage;
    int monster_defeated;
    int rewarded_defeat;
    int suppress_kill_message;
    int left_combat;
    int town_transition;
    int monster_takes_turn;
    int random_outcome;
} MRBattleCastResult;

const char *mr_preparation_spell_name(int spell);
const char *mr_battle_spell_name(int spell);
int mr_spell_cost(int zero_based_spell);

/* DUNSMALL C5D0-C7A4 stores one two-bit learned-spell mask for each
 * 1-based spell level. Bit one exposes the first spell and bit two exposes
 * the second. These helpers are the non-visual translation of that selector. */
int mr_spellbook_mask(const MRCharacter *player, MRSpellbookMaskType type,
                      int one_based_level);
int mr_spellbook_knows_choice(const MRCharacter *player,
                              MRSpellbookMaskType type,
                              int one_based_level, int one_based_choice);
int mr_spell_dispatch_index(int one_based_level, int one_based_choice);

/* Pure translations of DUNSMALL's QuickBASIC/BRUN30 arithmetic. `random_unit`
 * is one RND value in [0,1). Keeping RNG consumption outside these helpers
 * makes the original branch-specific draw order explicit to callers. */
int mr_prep_change_level_delta(float random_unit);
int mr_prep_mocciolo_outcome(float random_unit);
int mr_prep_mocciolo_level_loss_health(int health_max, int health_growth,
                                        float first_random,
                                        float second_random);

int mr_battle_gas_succeeds(float random_unit, int monster_level);
int mr_battle_magic_zot_damage(float random_unit, int player_level);
int mr_battle_magic_bolt_damage(float random_unit);
int mr_battle_lightning_damage(float random_unit, int player_level);
int mr_battle_go_away_threshold(float random_unit, int player_level);
int mr_battle_auto_kill_threshold(float random_unit, int player_level);
int mr_battle_explosion_damage(float random_unit, int player_level);
int mr_battle_large_random_damage(float random_unit);
int mr_battle_god_outcome(float random_unit);

/* Complete state transitions from DUNSMALL 35AC-3B01 and 90D3-95B7.
 * Selection/learned-spell UI is deliberately outside these routines; all
 * spell-point checks, branch-specific costs, random consumption and original
 * fall-through bugs are inside them. */
int mr_cast_preparation_spell(MRCharacter *player, MRPreparationSpell spell,
                              MRSpellRandomUnit random_unit,
                              void *random_context,
                              MRPreparationCastResult *result);
int mr_cast_preparation_spell_rng(MRCharacter *player,
                                  MRPreparationSpell spell, MRRng *rng,
                                  MRPreparationCastResult *result);

int mr_cast_battle_spell(MRCharacter *player, MRBattleSpell spell,
                         int movement_turn, int monster_level,
                         float *monster_health,
                         MRSpellRandomUnit random_unit, void *random_context,
                         MRBattleCastResult *result);
int mr_cast_battle_spell_rng(MRCharacter *player, MRBattleSpell spell,
                             int movement_turn, int monster_level,
                             float *monster_health, MRRng *rng,
                             MRBattleCastResult *result);

/* DUNSMALL 0A4F-0AB0 removes one Speed/Strength increment when a later
 * successful movement reaches its cyclic marker. Combat exit does not. */
void mr_expire_battle_spell_effects_on_movement(
    MRCharacter *player, unsigned long *movement_turn);

int mr_spells_self_test(char *error, size_t error_size);

#endif
