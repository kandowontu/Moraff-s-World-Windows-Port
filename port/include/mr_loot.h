#ifndef MR_LOOT_H
#define MR_LOOT_H

#include <stddef.h>

#include "mr_character.h"

typedef double (*MRLootRandomUnit)(void *context);

typedef struct MRCoinLoot {
    int copper;
    int silver;
    int ivory;
    int gold;
    int platinum;
    int jewels;
    int coin_units;
    int carry_weight;
    int treasure_value;
} MRCoinLoot;

typedef enum MRExtraRewardKind {
    MR_EXTRA_REWARD_NONE = 0,
    MR_EXTRA_REWARD_ARMOR,
    MR_EXTRA_REWARD_SWORD,
    MR_EXTRA_REWARD_MACE,
    MR_EXTRA_REWARD_HEALTH_RING,
    MR_EXTRA_REWARD_BAG_OF_HOLDING,
    MR_EXTRA_REWARD_MAGIC_SWORD,
    MR_EXTRA_REWARD_MAGIC_MACE,
    MR_EXTRA_REWARD_MAGIC_RING,
    MR_EXTRA_REWARD_MAGIC_ARMOR,
    MR_EXTRA_REWARD_HOLY_HAND_GRENADE,
    MR_EXTRA_REWARD_FLOOR_SLOSHER,
    MR_EXTRA_REWARD_CARRIED_ITEM,
    MR_EXTRA_REWARD_ATTRIBUTE_BOOK
} MRExtraRewardKind;

typedef struct MRPostCombatRewards {
    int spellbook_found;
    int spellbook_level;       /* BASIC level 1..6. */
    int spellbook_type;        /* MRSpellbookMaskType. */
    int spellbook_bit;         /* The original only awards bit 1 or 2. */

    MRExtraRewardKind weapon_kind;
    int weapon_value;          /* New armor tier for an armor award. */

    int wand_found;
    int wand_color;            /* Original color slot 1..9. */
    int wand_charges;          /* Exactly 1 or 2. */
    int pill_found;
    int pill_color;            /* Original color slot 1..6. */

    int generic_drop_triggered;
    int generic_roll;          /* Initial table roll 1..22. */
    int generic_enchantment;
    MRExtraRewardKind generic_kind;
    int generic_value;         /* Item 1..9 or attribute 1..5. */
} MRPostCombatRewards;

typedef enum MRPostCombatRewardStage {
    MR_POST_COMBAT_REWARD_SPELLBOOK = 0,
    MR_POST_COMBAT_REWARD_WEAPON,
    MR_POST_COMBAT_REWARD_WAND,
    MR_POST_COMBAT_REWARD_PILL,
    MR_POST_COMBAT_REWARD_GENERIC
} MRPostCombatRewardStage;

/* A9FE-B2CE does not calculate the complete reward set before displaying it.
 * It rolls and presents one category before evaluating the next category.
 * The callback preserves those chronological mutation/display boundaries;
 * the ordinary reward acknowledgements themselves do not consume RND. */
typedef void (*MRPostCombatRewardStageCallback)(
    MRPostCombatRewardStage stage, const MRPostCombatRewards *rewards,
    void *context);

/* Exact A561-A886 coin-generation path. These are the six categories printed
 * by the original post-combat reward screen. */
int mr_generate_coin_loot(float dungeon_level, int monster_level,
                          MRLootRandomUnit random_unit, void *random_context,
                          MRCoinLoot *loot);
int mr_generate_coin_loot_rng(float dungeon_level, int monster_level,
                              MRRng *rng, MRCoinLoot *loot);

/* Exact A9FE-B2CE reward sequence after the coin prompt. It mutates the same
 * fields as the original and deliberately consumes gate RND calls even when a
 * monster is ineligible for the gated reward. */
int mr_generate_post_combat_rewards(MRCharacter *character,
                                    int monster_behavior_class,
                                    MRLootRandomUnit random_unit,
                                    void *random_context,
                                    MRPostCombatRewards *rewards);
int mr_generate_post_combat_rewards_staged(
    MRCharacter *character, int monster_behavior_class,
    MRLootRandomUnit random_unit, void *random_context,
    MRPostCombatRewards *rewards,
    MRPostCombatRewardStageCallback stage_callback,
    void *stage_context);
int mr_generate_post_combat_rewards_rng(MRCharacter *character,
                                        int monster_behavior_class,
                                        MRRng *rng,
                                        MRPostCombatRewards *rewards);
int mr_generate_post_combat_rewards_rng_staged(
    MRCharacter *character, int monster_behavior_class, MRRng *rng,
    MRPostCombatRewards *rewards,
    MRPostCombatRewardStageCallback stage_callback,
    void *stage_context);

int mr_loot_self_test(char *error, size_t error_size);

#endif
