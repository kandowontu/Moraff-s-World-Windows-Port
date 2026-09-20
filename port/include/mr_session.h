#ifndef MR_SESSION_H
#define MR_SESSION_H

#include <stddef.h>

#include "mr_combat.h"
#include "mr_input.h"
#include "mr_items.h"
#include "mr_loot.h"
#include "mr_monsters.h"
#include "mr_progression.h"
#include "mr_spells.h"
#include "mr_world.h"

#define MR_OCCUPANCY_COLUMNS 22
#define MR_OCCUPANCY_ROWS 21

typedef enum MRSessionPhase {
    MR_SESSION_TOWN = 0,
    MR_SESSION_EXPLORING,
    MR_SESSION_COMBAT,
    MR_SESSION_DEFEAT_NOTICE,
    MR_SESSION_REWARDS,
    MR_SESSION_DEAD,
    MR_SESSION_RETURN_TO_ROSTER
} MRSessionPhase;

typedef enum MRSessionStepResult {
    MR_SESSION_STEP_IGNORED = 0,
    MR_SESSION_STEP_TURNED,
    MR_SESSION_STEP_MOVED,
    MR_SESSION_STEP_WALL,
    MR_SESSION_STEP_BOUNDARY,
    MR_SESSION_STEP_MONSTER,
    MR_SESSION_STEP_CHUTE
} MRSessionStepResult;

typedef enum MRCoinLootResolution {
    MR_COIN_LOOT_NOT_PENDING = 0,
    MR_COIN_LOOT_DECLINED,
    MR_COIN_LOOT_TAKEN,
    MR_COIN_LOOT_TOO_HEAVY
} MRCoinLootResolution;

typedef struct MRSessionCombatResult {
    MRPlayerAttackResult player;
    MRMonsterTurnResult monster;
    int player_attacked;
    int monster_attacked;
    int monster_defeated;
    int player_dead;
} MRSessionCombatResult;

typedef struct MRSessionBattleSpellResult {
    MRBattleCastResult spell;
    MRMonsterTurnResult monster;
    int monster_attacked;
    int player_dead;
} MRSessionBattleSpellResult;

typedef struct MRSessionItemResult {
    MRItemUseResult item;
    MRMonsterTurnResult monster;
    int monster_attacked;
    int player_dead;
} MRSessionItemResult;

typedef enum MRMonsterAdvanceCode {
    MR_MONSTER_ADVANCE_NONE = 0,
    MR_MONSTER_ADVANCE_WEIGHT_DELAY,
    MR_MONSTER_ADVANCE_WALL,
    MR_MONSTER_ADVANCE_OCCUPIED,
    MR_MONSTER_ADVANCE_MOVED,
    MR_MONSTER_ADVANCE_REACHED_PLAYER
} MRMonsterAdvanceCode;

typedef struct MRMonsterAdvanceResult {
    MRMonsterAdvanceCode code;
    int global_index;
    int old_x;
    int old_y;
    int new_x;
    int new_y;
    MRDirection direction;
} MRMonsterAdvanceResult;

typedef enum MRCombatSeparationCode {
    MR_COMBAT_SEPARATION_NOT_APPLICABLE = 0,
    MR_COMBAT_SEPARATION_ESCAPED,
    MR_COMBAT_SEPARATION_REENGAGED,
    MR_COMBAT_SEPARATION_REENGAGED_AND_ATTACKED
} MRCombatSeparationCode;

typedef struct MRCombatSeparationResult {
    MRCombatSeparationCode code;
    MRMonsterAdvanceResult advance;
    MRMonsterTurnResult monster;
} MRCombatSeparationResult;

typedef enum MRAmbientAdvice {
    MR_ADVICE_NONE = 0,
    MR_ADVICE_STAY_AT_INN,
    MR_ADVICE_GET_CURE,
    MR_ADVICE_DUNGEON_TOO_DEEP,
    MR_ADVICE_CASH_IN_TREASURE
} MRAmbientAdvice;

typedef struct MRSessionCycleResult {
    int health_regenerated;
    int disease_triggered;
    int disease_attribute;
    MRAmbientAdvice ambient_advice;
} MRSessionCycleResult;

typedef struct MRSession {
    MRSave save;
    MRWorldData world;
    MRInputState input;
    MRRng rng;
    MRSessionPhase phase;

    /* Unsaved DUNSMALL runtime globals.  These are recreated for every
     * invocation of the dungeon executable rather than being part of the
     * 311-record character file. */
    int configured_action_delay; /* DS:B542, entered as 0..3000 */
    /* DS:B5FA counts consecutive arrow strings seen by draw_status_and_map.
     * Once it exceeds three, 4100-416B bypasses the configurable busy-loop
     * delay until a non-arrow command resets the counter. */
    int consecutive_arrow_commands;
    int sound_disabled;          /* DS:B4BC, toggled modulo two */
    /* DS:B60E starts at zero and is set to one at 844E when the first
     * encounter is initialized. No recovered assignment clears it during
     * the DUNSMALL invocation; 7DC9 consequently tests this persistent
     * runtime flag, not merely whether the native controller is presently
     * in its combat phase. */
    int combat_initialized_flag;
    int primary_color;           /* DS:B46E, '#' cycles 0..16 */
    int secondary_color;         /* DS:B472, '@' cycles 2..3 */
    /* apply_color_palette 300C submits COLOR primary,secondary twice; its
     * XCHG/MOV shuffle rebuilds that order after the first preserved call.
     * The stored selectors can wrap after the hardware call, so retain the
     * resulting CGA state independently. */
    int effective_background_color;
    int effective_palette_selector;

    /* DUNSMALL uses a 22-column temporary grid (DS:4E90) and indexes it as
     * y*22+x.  Columns zero/21 and row zero remain available as its border
     * workspace even though ordinary actors occupy x=1..20, y=1..19. */
    int occupancy[MR_OCCUPANCY_ROWS][MR_OCCUPANCY_COLUMNS];

    /* DUNSMALL DS:B502/B506 are persistent AI tracking slots, not selections
     * made by the four-view renderer. 70A1 can acquire the primary slot. The
     * compiled renderer's always-zero B66A quirk only clears the secondary;
     * no reachable assignment fills that secondary slot with a monster. */
    int primary_tracking_global_index;
    int secondary_tracking_global_index;
    int active_monster_pursuit_target;
    int player_x_at_last_monster_update;
    int player_y_at_last_monster_update;
    /* DUNSMALL DS:B4FE is initialized to 40*level-39 by 7932.  The
     * empty-INKEY$ path advances this floor-global cursor independently of
     * the persistent AI tracking slots above. */
    int idle_monster_global_cursor;
    int last_chute_landing_x;
    int last_chute_landing_y;
    int last_chute_landing_level;

    int combat_global_index;
    int combat_floor_slot;
    float combat_health;
    MRMonsterCombatProfile combat_profile;
    MRMonsterAttackState monster_attack_state;
    MRPlayerAttackState player_attack_state;

    MRCoinLoot pending_coins;
    MRPostCombatRewards pending_rewards;
    float shared_menu_value;
    int coin_loot_pending;
    int coin_loot_resolved;
    int post_combat_rewards_generated;
    int suppress_defeat_message;

    /* DUNSMALL can enter 803A from inside the movement redraw at 49B1.
     * That path has already executed 3FFC's ring/disease boundary, whereas
     * an idle monster reaching the player enters through 091B and still
     * needs that boundary.  Keep the distinction until the host controller
     * transfers ownership to combat. */
    int combat_entry_redraw_already_applied;

    /* Some original SAVE calls occur before the rest of their transition.
     * This snapshot records the exact state at that boundary for a frontend
     * to persist without writing into the read-only original install. */
    MRSave pending_save_snapshot;
    int pending_save_snapshot_valid;
} MRSession;

int mr_session_load(MRSession *session, const char *resource_directory,
                    const char *character_text_path,
                    const char *character_binary_path, uint32_t rng_seed,
                    char *error, size_t error_size);
/* DUNSMALL B98F-B9B2 is the load epilogue reached after the startup review
 * has consumed its two RND values. It lazily places an uninitialized fountain
 * and clears the three persisted-but-transient combat potion timers. */
void mr_session_apply_original_load_epilogue(MRSession *session);
void mr_session_rebuild_occupancy(MRSession *session);
int mr_session_monster_at(const MRSession *session, int x, int y);
MRMonsterAdvanceCode mr_session_advance_active_monster(
    MRSession *session, int global_index, int projected_y,
    MRMonsterAdvanceResult *result);
int mr_session_update_active_monsters(MRSession *session,
                                      MRMonsterAdvanceResult results[2]);
int mr_session_idle_monster_interval(int current_monster_level,
                                     int player_level,
                                     float speed_calibration);
int mr_session_idle_monster_poll(MRSession *session,
                                 float speed_calibration,
                                 MRMonsterAdvanceResult *result);
void mr_session_reveal_current_cell(MRSession *session);
void mr_session_begin_command_cycle(MRSession *session,
                                    MRSessionCycleResult *result);
void mr_session_begin_combat_entry_status_cycle(
    MRSession *session, MRSessionCycleResult *result);
int mr_session_is_at_fountain(const MRSession *session);
int mr_session_drink_fountain(MRSession *session,
                              MRFountainResult *result);
int mr_session_take_save_snapshot(MRSession *session, MRSave *save);
MRSessionStepResult mr_session_arrow(MRSession *session, MRArrowKey key);
MRSessionStepResult mr_session_arrow_with_cycle(
    MRSession *session, MRArrowKey key, MRSessionCycleResult *cycle);
int mr_session_resolve_combat_separation(
    MRSession *session, MRCombatSeparationResult *result);
int mr_session_traverse(MRSession *session, int go_up);
int mr_session_begin_combat(MRSession *session, int global_index);
int mr_session_player_attack_only(MRSession *session,
                                  MRPlayerAttackCode attack,
                                  MRSessionCombatResult *result);
int mr_session_resolve_monster_turn(MRSession *session,
                                    MRMonsterTurnResult *result);
int mr_session_player_attack(MRSession *session, MRPlayerAttackCode attack,
                             MRSessionCombatResult *result);
int mr_session_cast_preparation_spell(MRSession *session,
                                      MRPreparationSpell spell,
                                      MRPreparationCastResult *result);
int mr_session_cast_battle_spell(MRSession *session, MRBattleSpell spell,
                                 MRSessionBattleSpellResult *result);
int mr_session_use_preparation_item(MRSession *session,
                                    MRPreparationItem item,
                                    MRItemUseResult *result);
int mr_session_use_battle_item(MRSession *session, MRBattleItem item,
                               float timer_seconds,
                               MRSessionItemResult *result);
int mr_session_use_wand(MRSession *session, MRWand wand,
                        MRSessionItemResult *result);
int mr_session_use_pill(MRSession *session, MRPill pill,
                        MRItemUseResult *result);
void mr_session_expire_combat_effects(MRSession *session,
                                      float timer_seconds,
                                      MRTimedEffectResult *result);
int mr_session_acknowledge_defeat(MRSession *session);
MRCoinLootResolution mr_session_resolve_coin_loot(MRSession *session,
                                                  int take_coins);
int mr_session_generate_post_combat_rewards_staged(
    MRSession *session, MRPostCombatRewardStageCallback stage_callback,
    void *stage_context);
int mr_session_finish_rewards(MRSession *session);
MRDeathOutcome mr_session_resolve_player_death(MRSession *session);

int mr_session_self_test(const char *resource_directory,
                         char *error, size_t error_size);

#endif
