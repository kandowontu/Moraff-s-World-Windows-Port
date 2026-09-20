#ifndef MR_ITEMS_H
#define MR_ITEMS_H

#include <stddef.h>
#include <stdint.h>

#include "mr_spells.h"

typedef enum MRPreparationItem {
    MR_PREP_ITEM_TELEPORT_SCROLL = 0,
    MR_PREP_ITEM_SEEING_SCROLL,
    MR_PREP_ITEM_HEALING_SCROLL,
    MR_PREP_ITEM_SPELL_POINT_SCROLL,
    MR_PREP_ITEM_BAG_OF_HOLDING,
    MR_PREP_ITEM_FLOOR_SLOSHER,
    MR_PREP_ITEM_COUNT
} MRPreparationItem;

typedef enum MRBattleItem {
    MR_BATTLE_ITEM_SPEED_POTION = 0,
    MR_BATTLE_ITEM_FIRE_POTION,
    MR_BATTLE_ITEM_SHIELDING_POTION,
    MR_BATTLE_ITEM_HEALTH_POTION,
    MR_BATTLE_ITEM_RELOCATION_POTION,
    MR_BATTLE_ITEM_HOLY_HAND_GRENADE,
    MR_BATTLE_ITEM_COUNT
} MRBattleItem;

typedef enum MRWand {
    /* DUNSMALL 7AE9-7B12 prints color_strings[10-choice] while storing
     * charges in persistent_values[27+choice].  The selector and the save
     * record therefore run PURPLE through BLUE, not the pill color order. */
    MR_WAND_PURPLE = 0,
    MR_WAND_BROWN,
    MR_WAND_BLACK,
    MR_WAND_WHITE,
    MR_WAND_ORANGE,
    MR_WAND_YELLOW,
    MR_WAND_GREEN,
    MR_WAND_RED,
    MR_WAND_BLUE,
    MR_WAND_COUNT
} MRWand;

typedef enum MRPill {
    MR_PILL_BLUE = 0,
    MR_PILL_RED,
    MR_PILL_GREEN,
    MR_PILL_YELLOW,
    MR_PILL_ORANGE,
    MR_PILL_WHITE,
    MR_PILL_COUNT
} MRPill;

typedef enum MRItemContext {
    MR_ITEM_CONTEXT_EXPLORATION = 0,
    MR_ITEM_CONTEXT_COMBAT = 1
} MRItemContext;

typedef struct MRItemCombatState {
    int movement_turn;
    int monster_level;
    float monster_health;
    int monster_cannot_strike_counter;
    int one_shot_damage_bonus;
    int shield_defense_value;
} MRItemCombatState;

typedef struct MRItemUseResult {
    int used;
    int unavailable;
    int no_effect;
    int dungeon_transition;
    int town_transition;
    int left_combat;
    int monster_defeated;
    int rewarded_defeat;
    int monster_takes_turn;
    int damage;
    int floor_slosher_too_deep;
    int map_revealed;
    int random_x;
    int random_y;
} MRItemUseResult;

typedef struct MRTimedEffectResult {
    int breathe_fire_expired;
    int shielding_expired;
    int agility_expired;
} MRTimedEffectResult;

/* Read-only transcription of DUNSMALL's first inventory page at 3B36-3CD7.
 * The second page is represented directly by its nine wand and six pill
 * counters because that page has no conditional reordering. */
typedef enum MRMagicInventoryKind {
    MR_INVENTORY_HEALTH_RINGS = 0,
    MR_INVENTORY_BAG_COINS,
    MR_INVENTORY_MAGIC_SWORD,
    MR_INVENTORY_MAGIC_MACE,
    MR_INVENTORY_MAGIC_RING,
    MR_INVENTORY_MAGIC_ARMOR,
    MR_INVENTORY_FLOOR_SLOSHER,
    MR_INVENTORY_HOLY_GRENADES,
    MR_INVENTORY_CARRIED_ITEM
} MRMagicInventoryKind;

enum {
    MR_ORIGINAL_CARRIED_MAGIC_ITEMS = 13,
    MR_MAGIC_INVENTORY_FIRST_PAGE_MAX =
        8 + MR_ORIGINAL_CARRIED_MAGIC_ITEMS
};

typedef struct MRMagicInventoryEntry {
    MRMagicInventoryKind kind;
    int source_index;
    float amount;
} MRMagicInventoryEntry;

typedef struct MRMagicInventorySnapshot {
    MRMagicInventoryEntry first_page[MR_MAGIC_INVENTORY_FIRST_PAGE_MAX];
    size_t first_page_count;
    float wand_charges[MR_WAND_COUNT];
    float pill_counts[MR_PILL_COUNT];
} MRMagicInventorySnapshot;

typedef enum MRDropTreasureResult {
    MR_DROP_TREASURE_NONE_OWNED = 0,
    MR_DROP_TREASURE_CANCELLED,
    MR_DROP_TREASURE_DROPPED
} MRDropTreasureResult;

const char *mr_preparation_item_name(int item);
const char *mr_battle_item_name(int item);
const char *mr_wand_name(int wand);
const char *mr_pill_name(int pill);
void mr_magic_inventory_snapshot(const MRCharacter *player,
                                 MRMagicInventorySnapshot *snapshot);
MRDropTreasureResult mr_drop_all_carried_treasure(MRCharacter *player,
                                                   int confirmed);

int mr_use_preparation_item(MRCharacter *player, MRPreparationItem item,
                            uint32_t explored[MR_EXPLORED_LEVELS]
                                             [MR_EXPLORED_ROWS],
                            MRItemUseResult *result);
int mr_use_battle_item(MRCharacter *player, MRBattleItem item,
                       float timer_seconds, MRItemCombatState *combat,
                       MRSpellRandomUnit random_unit, void *random_context,
                       MRItemUseResult *result);
int mr_use_battle_item_rng(MRCharacter *player, MRBattleItem item,
                           float timer_seconds, MRItemCombatState *combat,
                           MRRng *rng, MRItemUseResult *result);

int mr_use_wand(MRCharacter *player, MRWand wand, MRItemContext context,
                MRItemCombatState *combat, MRSpellRandomUnit random_unit,
                void *random_context, MRItemUseResult *result);
int mr_use_wand_rng(MRCharacter *player, MRWand wand, MRItemContext context,
                    MRItemCombatState *combat, MRRng *rng,
                    MRItemUseResult *result);
int mr_use_pill(MRCharacter *player, MRPill pill, MRItemUseResult *result);

/* DUNSMALL checks these three wall-clock expiries at the top of every combat
 * input cycle.  A baseline shield-defense value of 16 is restored when the
 * shielding potion expires; its freshly-used value is 15. */
void mr_expire_timed_combat_effects(MRCharacter *player,
                                    float timer_seconds,
                                    MRItemCombatState *combat,
                                    MRTimedEffectResult *result);

int mr_items_self_test(char *error, size_t error_size);

#endif
