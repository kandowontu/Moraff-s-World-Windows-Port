#include "mr_items.h"
#include "mr_progression.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const mr_preparation_item_names[MR_PREP_ITEM_COUNT] = {
    "TELEPORT SCROLL", "SCROLL OF SEEING", "SCROLL OF HEALING",
    "SPELL POINT SCROLL", "BAG OF HOLDING", "FLOOR SLOSHER"
};

static const char *const mr_battle_item_names[MR_BATTLE_ITEM_COUNT] = {
    "SPEED POTION", "FIRE POTION", "SHIELDING POTION", "HEALTH POTION",
    "RELOCATION POTION", "HOLY HAND GRENADE"
};

static const char *const mr_wand_color_names[MR_WAND_COUNT] = {
    "PURPLE", "BROWN", "BLACK", "WHITE", "ORANGE", "YELLOW", "GREEN",
    "RED", "BLUE"
};

static const char *const mr_pill_color_names[MR_PILL_COUNT] = {
    "BLUE", "RED", "GREEN", "YELLOW", "ORANGE", "WHITE"
};

static float mr_item_random(MRSpellRandomUnit random_unit, void *context) {
    float value = (float)(random_unit ? random_unit(context) : 0.0);
    if (value < 0.0f) return 0.0f;
    if (value >= 1.0f) return nextafterf(1.0f, 0.0f);
    return value;
}

static double mr_item_rng_adapter(void *context) {
    return mr_rng_next((MRRng *)context);
}

const char *mr_preparation_item_name(int item) {
    return item >= 0 && item < MR_PREP_ITEM_COUNT ?
           mr_preparation_item_names[item] : NULL;
}

const char *mr_battle_item_name(int item) {
    return item >= 0 && item < MR_BATTLE_ITEM_COUNT ?
           mr_battle_item_names[item] : NULL;
}

const char *mr_wand_name(int wand) {
    return wand >= 0 && wand < MR_WAND_COUNT ?
           mr_wand_color_names[wand] : NULL;
}

const char *mr_pill_name(int pill) {
    return pill >= 0 && pill < MR_PILL_COUNT ?
           mr_pill_color_names[pill] : NULL;
}

static int mr_has_status(const MRCharacter *player, int status) {
    return (((int)player->status_flags) & status) != 0;
}

static void mr_inventory_append(MRMagicInventorySnapshot *snapshot,
                                MRMagicInventoryKind kind, int source_index,
                                float amount) {
    if (snapshot->first_page_count >= MR_MAGIC_INVENTORY_FIRST_PAGE_MAX)
        return;
    MRMagicInventoryEntry *entry =
        &snapshot->first_page[snapshot->first_page_count++];
    entry->kind = kind;
    entry->source_index = source_index;
    entry->amount = amount;
}

void mr_magic_inventory_snapshot(const MRCharacter *player,
                                 MRMagicInventorySnapshot *snapshot) {
    if (!snapshot) return;
    memset(snapshot, 0, sizeof(*snapshot));
    if (!player) return;

    /* 3B36-3C81: these entries are printed in this exact order.  Sword and
     * mace visibility is tested from their numeric bonuses, whereas rings,
     * bag, armor, and slosher use status bits. */
    if (mr_has_status(player, MR_STATUS_RING_OF_HEALTH))
        mr_inventory_append(snapshot, MR_INVENTORY_HEALTH_RINGS, -1,
            player->magic_item_values[MR_MAGIC_HEALTH_RINGS]);
    if (mr_has_status(player, MR_STATUS_BAG_OF_HOLDING))
        mr_inventory_append(snapshot, MR_INVENTORY_BAG_COINS, -1,
            player->magic_item_values[MR_MAGIC_BAG_COINS]);
    if (player->magic_item_values[MR_MAGIC_SWORD_BONUS] > 0.0f)
        mr_inventory_append(snapshot, MR_INVENTORY_MAGIC_SWORD, -1,
            player->magic_item_values[MR_MAGIC_SWORD_BONUS]);
    if (player->magic_item_values[MR_MAGIC_MACE_BONUS] > 0.0f)
        mr_inventory_append(snapshot, MR_INVENTORY_MAGIC_MACE, -1,
            player->magic_item_values[MR_MAGIC_MACE_BONUS]);
    if (mr_has_status(player, MR_STATUS_MAGIC_RING))
        mr_inventory_append(snapshot, MR_INVENTORY_MAGIC_RING, -1,
            player->magic_item_values[MR_MAGIC_RING_BONUS]);
    if (mr_has_status(player, MR_STATUS_MAGIC_ARMOR))
        mr_inventory_append(snapshot, MR_INVENTORY_MAGIC_ARMOR, -1,
            player->magic_item_values[MR_MAGIC_ARMOR_BONUS]);
    if (mr_has_status(player, MR_STATUS_FLOOR_SLOSHER))
        mr_inventory_append(snapshot, MR_INVENTORY_FLOOR_SLOSHER, -1, 1.0f);
    if (player->magic_item_values[MR_MAGIC_HOLY_GRENADES] > 0.0f)
        mr_inventory_append(snapshot, MR_INVENTORY_HOLY_GRENADES, -1,
            player->magic_item_values[MR_MAGIC_HOLY_GRENADES]);

    /* 3C81-3CD7 intentionally lists only carried slots 1..13. */
    for (int item = 0; item < MR_ORIGINAL_CARRIED_MAGIC_ITEMS; item++) {
        if (player->carried_items[item] > 0.0f)
            mr_inventory_append(snapshot, MR_INVENTORY_CARRIED_ITEM, item,
                                player->carried_items[item]);
    }

    /* 3CEE-3D36 suppresses zero-charge wands at render time.  3D38-3D6E
     * prints every pill color, including a zero count. */
    for (int wand = 0; wand < MR_WAND_COUNT; wand++)
        snapshot->wand_charges[wand] = player->persistent_values[
            MR_PERSISTENT_PURPLE_WAND_CHARGES + wand];
    for (int pill = 0; pill < MR_PILL_COUNT; pill++)
        snapshot->pill_counts[pill] = player->persistent_values[
            MR_PERSISTENT_BLUE_PILLS + pill];
}

MRDropTreasureResult mr_drop_all_carried_treasure(MRCharacter *player,
                                                   int confirmed) {
    if (!player || player->carried_treasure == 0.0f)
        return MR_DROP_TREASURE_NONE_OWNED;
    if (!confirmed) return MR_DROP_TREASURE_CANCELLED;

    /* 1963-19C3 recomputes the post-drop load from body plus ordinary armor.
     * Magic armor has zero weight even though its equipped armor value is
     * retained. */
    float armor_weight = mr_has_status(player, MR_STATUS_MAGIC_ARMOR) ?
                         0.0f : player->equipped_armor * 25.0f;
    player->carried_treasure = 0.0f;
    player->carried_weight = 150.0f + armor_weight;
    return MR_DROP_TREASURE_DROPPED;
}

static void mr_decrement(float *value) {
    *value -= 1.0f;
}

/* 17D6-1886.  The bag moves all coin-weight it can identify into its
 * weightless storage.  The original represents sixteen coin units per pound,
 * reserves 150 pounds for the body and 25 pounds per ordinary armor level,
 * and caps bag contents at 15,008 units. */
static void mr_fill_bag_of_holding(MRCharacter *player) {
    float armor_weight = mr_has_status(player, MR_STATUS_MAGIC_ARMOR) ?
                         0.0f : player->equipped_armor * 25.0f;
    float coin_weight = player->carried_weight - 150.0f - armor_weight;
    if (coin_weight < 0.0f) coin_weight = 0.0f;
    float transferable = floorf(coin_weight * 16.0f);
    float remaining = 15008.0f -
        player->magic_item_values[MR_MAGIC_BAG_COINS];
    if (remaining < 0.0f) remaining = 0.0f;
    if (transferable > remaining) transferable = remaining;
    if (transferable > 15.0f)
        transferable = floorf(transferable / 16.0f) * 16.0f;
    player->magic_item_values[MR_MAGIC_BAG_COINS] += transferable;
    player->carried_weight -= transferable * .0625f;
}

int mr_use_preparation_item(MRCharacter *player, MRPreparationItem item,
                            uint32_t explored[MR_EXPLORED_LEVELS]
                                             [MR_EXPLORED_ROWS],
                            MRItemUseResult *result) {
    if (!player || !result || item < 0 || item >= MR_PREP_ITEM_COUNT)
        return 0;
    memset(result, 0, sizeof(*result));

    if (item <= MR_PREP_ITEM_SPELL_POINT_SCROLL) {
        float *count = &player->carried_items[item];
        if (*count < 1.0f) {
            result->unavailable = 1;
            return 1;
        }
        mr_decrement(count);
    } else if (item == MR_PREP_ITEM_BAG_OF_HOLDING) {
        if (!mr_has_status(player, MR_STATUS_BAG_OF_HOLDING)) {
            result->unavailable = 1;
            return 1;
        }
    } else if (!mr_has_status(player, MR_STATUS_FLOOR_SLOSHER)) {
        result->unavailable = 1;
        return 1;
    }

    result->used = 1;
    switch (item) {
        case MR_PREP_ITEM_TELEPORT_SCROLL:
            player->dungeon_level = .5f;
            player->player_x = 18.0f;
            player->player_y = 17.0f;
            result->dungeon_transition = 1;
            /* 1723 immediately calls the shared 1CF5 town-entry routine;
             * .5 is only the compiled intermediate value. */
            result->town_transition = 1;
            break;
        case MR_PREP_ITEM_SEEING_SCROLL: {
            int level = (int)player->dungeon_level;
            if (explored && level >= 0 && level < MR_EXPLORED_LEVELS) {
                for (int row = 0; row < MR_EXPLORED_ROWS; ++row)
                    explored[level][row] = (1U << 21) - 1U;
            }
            result->map_revealed = 1;
            break;
        }
        case MR_PREP_ITEM_HEALING_SCROLL:
            player->health_current = player->health_max;
            break;
        case MR_PREP_ITEM_SPELL_POINT_SCROLL:
            player->spell_points += 10.0f;
            break;
        case MR_PREP_ITEM_BAG_OF_HOLDING:
            mr_fill_bag_of_holding(player);
            break;
        case MR_PREP_ITEM_FLOOR_SLOSHER:
            if (player->dungeon_level > 40.0f) {
                result->floor_slosher_too_deep = 1;
                break;
            }
            player->dungeon_level += 1.0f;
            result->dungeon_transition = 1;
            break;
        default:
            return 0;
    }
    return 1;
}

static void mr_copy_battle_spell_result(const MRBattleCastResult *spell,
                                        MRItemUseResult *item) {
    item->no_effect = spell->no_effect;
    item->damage = spell->damage;
    item->monster_defeated = spell->monster_defeated;
    item->rewarded_defeat = spell->rewarded_defeat;
    item->left_combat = spell->left_combat;
    item->town_transition = spell->town_transition;
    item->monster_takes_turn = spell->monster_takes_turn;
}

int mr_use_battle_item(MRCharacter *player, MRBattleItem item,
                       float timer_seconds, MRItemCombatState *combat,
                       MRSpellRandomUnit random_unit, void *random_context,
                       MRItemUseResult *result) {
    if (!player || !combat || !result || item < 0 ||
        item >= MR_BATTLE_ITEM_COUNT)
        return 0;
    memset(result, 0, sizeof(*result));
    float *count = item == MR_BATTLE_ITEM_HOLY_HAND_GRENADE ?
        &player->magic_item_values[MR_MAGIC_HOLY_GRENADES] :
        &player->carried_items[MR_ITEM_SPEED_POTION + item];
    if (*count < 1.0f) {
        result->unavailable = 1;
        return 1;
    }
    mr_decrement(count);
    result->used = 1;

    switch (item) {
        case MR_BATTLE_ITEM_SPEED_POTION:
            /* DS:1FA6 is one-based BASIC attribute five (Agility); the
             * native zero-based representation stores it at index four. */
            player->attributes[4] += 13.0f;
            player->persistent_values[MR_PERSISTENT_AGILITY_EXPIRY] =
                timer_seconds + 100.0f;
            break;
        case MR_BATTLE_ITEM_FIRE_POTION:
            player->persistent_values[MR_PERSISTENT_BREATHE_FIRE_EXPIRY] =
                timer_seconds + 100.0f;
            break;
        case MR_BATTLE_ITEM_SHIELDING_POTION:
            player->persistent_values[MR_PERSISTENT_SHIELDING_EXPIRY] =
                timer_seconds + 100.0f;
            combat->shield_defense_value = 15;
            break;
        case MR_BATTLE_ITEM_HEALTH_POTION:
            player->health_current += 75.0f;
            if (player->health_current > player->health_max)
                player->health_current = player->health_max;
            break;
        case MR_BATTLE_ITEM_RELOCATION_POTION:
            player->player_x = floorf(mr_item_random(random_unit,
                                                     random_context) * 16.0f) + 3.0f;
            player->player_y = floorf(mr_item_random(random_unit,
                                                     random_context) * 16.0f) + 3.0f;
            result->random_x = (int)player->player_x;
            result->random_y = (int)player->player_y;
            result->left_combat = 1;
            break;
        case MR_BATTLE_ITEM_HOLY_HAND_GRENADE:
            combat->monster_health = 0.0f;
            result->monster_defeated = 1;
            result->rewarded_defeat = 1;
            result->left_combat = 1;
            break;
        default:
            return 0;
    }
    return 1;
}

int mr_use_battle_item_rng(MRCharacter *player, MRBattleItem item,
                           float timer_seconds, MRItemCombatState *combat,
                           MRRng *rng, MRItemUseResult *result) {
    if (!rng) return 0;
    return mr_use_battle_item(player, item, timer_seconds, combat,
                              mr_item_rng_adapter, rng, result);
}

int mr_use_wand(MRCharacter *player, MRWand wand, MRItemContext context,
                MRItemCombatState *combat, MRSpellRandomUnit random_unit,
                void *random_context, MRItemUseResult *result) {
    if (!player || !result || wand < 0 || wand >= MR_WAND_COUNT)
        return 0;
    /* 7BF6-7C48 updates the shared cannot-strike and one-shot-damage fields
     * for White and Orange in either caller.  Those values deliberately carry
     * into the next encounter, so the state object is required outside combat
     * as well. */
    if ((context == MR_ITEM_CONTEXT_COMBAT || wand == MR_WAND_WHITE ||
         wand == MR_WAND_ORANGE) && !combat)
        return 0;
    memset(result, 0, sizeof(*result));
    float *charges =
        &player->persistent_values[MR_PERSISTENT_PURPLE_WAND_CHARGES + wand];
    if (*charges < 1.0f) {
        result->unavailable = 1;
        return 1;
    }
    mr_decrement(charges);
    result->used = 1;

    if (wand == MR_WAND_PURPLE) {
        player->dungeon_level -= 1.0f;
        result->dungeon_transition = 1;
        if (player->dungeon_level <= 0.0f) result->town_transition = 1;
        if (context == MR_ITEM_CONTEXT_COMBAT) result->left_combat = 1;
        return 1;
    }
    if (wand == MR_WAND_BROWN) {
        player->player_x = 10.0f;
        player->player_y = 10.0f;
        if (context == MR_ITEM_CONTEXT_COMBAT) result->left_combat = 1;
        return 1;
    }
    if (wand == MR_WAND_BLACK) {
        player->health_current += 25.0f;
        if (player->health_current > player->health_max)
            player->health_current = player->health_max;
        return 1;
    }
    if (wand == MR_WAND_WHITE) {
        combat->monster_cannot_strike_counter += 10;
        return 1;
    }
    if (wand == MR_WAND_ORANGE) {
        combat->one_shot_damage_bonus = 240;
        return 1;
    }
    if (wand == MR_WAND_BLUE) {
        player->health_current = player->health_max;
        return 1;
    }
    if (context != MR_ITEM_CONTEXT_COMBAT) {
        result->no_effect = 1;
        return 1;
    }

    /* Combat caller 8926-896A refunds the normal spell cost, then branches
     * into the ordinary Strength (92C8), Lightning (9298), or Explosion
     * (93E9) implementation for selector choices 6, 7, and 8. */
    MRBattleSpell spell = wand == MR_WAND_YELLOW ? MR_BATTLE_STRENGTH :
                          wand == MR_WAND_GREEN ? MR_BATTLE_LIGHTNING :
                                                 MR_BATTLE_EXPLOSION;
    int refund = mr_spell_cost(spell);
    player->spell_points += (float)refund;
    MRBattleCastResult spell_result;
    if (!mr_cast_battle_spell(player, spell, combat->movement_turn,
                              combat->monster_level, &combat->monster_health,
                              random_unit, random_context, &spell_result))
        return 0;
    mr_copy_battle_spell_result(&spell_result, result);
    return 1;
}

int mr_use_wand_rng(MRCharacter *player, MRWand wand, MRItemContext context,
                    MRItemCombatState *combat, MRRng *rng,
                    MRItemUseResult *result) {
    if (!rng) return 0;
    return mr_use_wand(player, wand, context, combat, mr_item_rng_adapter, rng,
                       result);
}

int mr_use_pill(MRCharacter *player, MRPill pill, MRItemUseResult *result) {
    if (!player || !result || pill < 0 || pill >= MR_PILL_COUNT) return 0;
    memset(result, 0, sizeof(*result));
    float *count =
        &player->persistent_values[MR_PERSISTENT_BLUE_PILLS + pill];
    if (*count < 1.0f) {
        result->unavailable = 1;
        return 1;
    }
    mr_decrement(count);
    result->used = 1;

    /* 7D5B-7DC5: first index is color+3, wrapped back by six; the second is
     * 7-color.  BASIC's arrays are one based. */
    int subtract_attribute = pill + 3;
    if (subtract_attribute >= MR_SAVE_ATTRIBUTES)
        subtract_attribute -= MR_SAVE_ATTRIBUTES;
    int add_attribute = MR_SAVE_ATTRIBUTES - 1 - pill;
    player->attributes[subtract_attribute] -= 2.0f;
    mr_clamp_character_attributes(player);
    player->attributes[add_attribute] += 4.0f;
    return 1;
}

static int mr_timer_expired(float expiry, float timer_seconds) {
    /* The original's TIMER expression rejects stale values separated by more
     * than 400 seconds, which also handles the midnight wrap. */
    if (expiry <= 0.0f) return 1;
    float remaining = expiry - timer_seconds;
    return remaining <= 0.0f || remaining > 400.0f;
}

void mr_expire_timed_combat_effects(MRCharacter *player,
                                    float timer_seconds,
                                    MRItemCombatState *combat,
                                    MRTimedEffectResult *result) {
    if (!player || !combat) return;
    if (result) memset(result, 0, sizeof(*result));
    if (mr_timer_expired(
            player->persistent_values[MR_PERSISTENT_BREATHE_FIRE_EXPIRY],
            timer_seconds)) {
        player->persistent_values[MR_PERSISTENT_BREATHE_FIRE_EXPIRY] = 0.0f;
        if (result) result->breathe_fire_expired = 1;
    }
    if (mr_timer_expired(
            player->persistent_values[MR_PERSISTENT_SHIELDING_EXPIRY],
            timer_seconds)) {
        player->persistent_values[MR_PERSISTENT_SHIELDING_EXPIRY] = 0.0f;
        combat->shield_defense_value = 16;
        if (result) result->shielding_expired = 1;
    }
    if (mr_timer_expired(
            player->persistent_values[MR_PERSISTENT_AGILITY_EXPIRY],
            timer_seconds)) {
        if (player->persistent_values[MR_PERSISTENT_AGILITY_EXPIRY] != 0.0f)
            player->attributes[4] -= 13.0f;
        player->persistent_values[MR_PERSISTENT_AGILITY_EXPIRY] = 0.0f;
        if (result) result->agility_expired = 1;
    }
}

typedef struct MRItemTestRandom {
    const double *values;
    size_t count;
    size_t index;
} MRItemTestRandom;

static double mr_item_test_random(void *context) {
    MRItemTestRandom *script = (MRItemTestRandom *)context;
    if (!script || script->index >= script->count) return 0.0;
    return script->values[script->index++];
}

int mr_items_self_test(char *error, size_t error_size) {
    int failed = 0;
    MRCharacter player;
    MRItemUseResult used;
    MRItemCombatState combat;
    uint32_t explored[MR_EXPLORED_LEVELS][MR_EXPLORED_ROWS] = {{0}};
    memset(&player, 0, sizeof(player));
    memset(&combat, 0, sizeof(combat));

    /* Inventory entry inclusion/order is encoded directly by 3B36-3CD7. */
    player.status_flags = MR_STATUS_RING_OF_HEALTH |
                          MR_STATUS_BAG_OF_HOLDING |
                          MR_STATUS_MAGIC_ARMOR;
    player.magic_item_values[MR_MAGIC_HEALTH_RINGS] = 2.0;
    player.magic_item_values[MR_MAGIC_BAG_COINS] = 123.0;
    player.magic_item_values[MR_MAGIC_SWORD_BONUS] = 4.0;
    player.magic_item_values[MR_MAGIC_ARMOR_BONUS] = 6.0;
    player.carried_items[0] = 3.0;
    player.carried_items[13] = 9.0; /* Original inventory stops before this. */
    player.persistent_values[MR_PERSISTENT_PURPLE_WAND_CHARGES] = 5.0;
    player.persistent_values[MR_PERSISTENT_BLUE_PILLS] = 7.0;
    MRMagicInventorySnapshot inventory;
    mr_magic_inventory_snapshot(&player, &inventory);
    failed |= inventory.first_page_count != 5 ||
              inventory.first_page[0].kind != MR_INVENTORY_HEALTH_RINGS ||
              inventory.first_page[1].kind != MR_INVENTORY_BAG_COINS ||
              inventory.first_page[2].kind != MR_INVENTORY_MAGIC_SWORD ||
              inventory.first_page[3].kind != MR_INVENTORY_MAGIC_ARMOR ||
              inventory.first_page[4].kind != MR_INVENTORY_CARRIED_ITEM ||
              inventory.first_page[4].source_index != 0 ||
              inventory.wand_charges[MR_WAND_PURPLE] != 5.0 ||
              inventory.pill_counts[MR_PILL_BLUE] != 7.0;

    player.carried_treasure = 500.0;
    player.carried_weight = 800.0;
    player.equipped_armor = 4.0;
    failed |= mr_drop_all_carried_treasure(&player, 0) !=
                  MR_DROP_TREASURE_CANCELLED ||
              player.carried_treasure != 500.0 ||
              player.carried_weight != 800.0;
    failed |= mr_drop_all_carried_treasure(&player, 1) !=
                  MR_DROP_TREASURE_DROPPED ||
              player.carried_treasure != 0.0 ||
              player.carried_weight != 150.0;
    player.status_flags = 0.0;
    player.carried_treasure = 1.0;
    player.carried_weight = 999.0;
    failed |= mr_drop_all_carried_treasure(&player, 1) !=
                  MR_DROP_TREASURE_DROPPED ||
              player.carried_weight != 250.0;
    failed |= mr_drop_all_carried_treasure(&player, 1) !=
              MR_DROP_TREASURE_NONE_OWNED;

    memset(&player, 0, sizeof(player));

    player.carried_items[MR_ITEM_SEEING_SCROLL] = 1.0;
    player.dungeon_level = 3.0;
    failed |= !mr_use_preparation_item(&player, MR_PREP_ITEM_SEEING_SCROLL,
                                       explored, &used);
    failed |= player.carried_items[MR_ITEM_SEEING_SCROLL] != 0.0 ||
              explored[3][20] != 0x1fffffU || !used.map_revealed;

    player.carried_items[MR_ITEM_TELEPORT_SCROLL] = 1.0;
    player.dungeon_level = 12.0;
    player.player_x = 4.0;
    player.player_y = 5.0;
    failed |= !mr_use_preparation_item(&player,
                                       MR_PREP_ITEM_TELEPORT_SCROLL,
                                       explored, &used);
    failed |= player.dungeon_level != .5 || player.player_x != 18.0 ||
              player.player_y != 17.0 || !used.dungeon_transition ||
              !used.town_transition;

    player.status_flags = MR_STATUS_FLOOR_SLOSHER;
    player.dungeon_level = 40.0;
    failed |= !mr_use_preparation_item(&player, MR_PREP_ITEM_FLOOR_SLOSHER,
                                       explored, &used);
    failed |= player.dungeon_level != 41.0 || !used.dungeon_transition;
    failed |= !mr_use_preparation_item(&player, MR_PREP_ITEM_FLOOR_SLOSHER,
                                       explored, &used);
    failed |= player.dungeon_level != 41.0 || !used.floor_slosher_too_deep;

    player.carried_items[MR_ITEM_RELOCATION_POTION] = 1.0;
    const double relocation_values[] = {0.0, .999999};
    MRItemTestRandom relocation = {relocation_values, 2, 0};
    failed |= !mr_use_battle_item(&player, MR_BATTLE_ITEM_RELOCATION_POTION,
                                  100.0, &combat, mr_item_test_random,
                                  &relocation, &used);
    failed |= player.player_x != 3.0 || player.player_y != 18.0 ||
              relocation.index != 2 || !used.left_combat;

    player.persistent_values[MR_PERSISTENT_BLUE_PILLS] = 1.0;
    player.attributes[3] = 1.0;
    player.attributes[5] = 10.0;
    failed |= !mr_use_pill(&player, MR_PILL_BLUE, &used);
    failed |= player.attributes[3] != 1.0 || player.attributes[5] != 14.0;

    player.persistent_values[MR_PERSISTENT_YELLOW_WAND_CHARGES] = 1.0;
    player.spell_points = 0.0;
    player.combat_attack_factor = 10.0;
    combat.movement_turn = 1;
    combat.monster_level = 3;
    combat.monster_health = 100.0;
    failed |= !mr_use_wand(&player, MR_WAND_YELLOW, MR_ITEM_CONTEXT_COMBAT,
                           &combat, NULL, NULL, &used);
    failed |= player.spell_points != 0.0 || player.combat_attack_factor != 17.0;

    /* Selector choice 1 is Purple and choice 9 is Blue.  These assertions
     * protect the reversed display/storage order recovered at 7AE9-7B12. */
    failed |= strcmp(mr_wand_name(MR_WAND_PURPLE), "PURPLE") != 0 ||
              strcmp(mr_wand_name(MR_WAND_BLUE), "BLUE") != 0;
    player.persistent_values[MR_PERSISTENT_PURPLE_WAND_CHARGES] = 2.0;
    player.dungeon_level = 1.0;
    failed |= !mr_use_wand(&player, MR_WAND_PURPLE,
                           MR_ITEM_CONTEXT_EXPLORATION, NULL, NULL, NULL,
                           &used);
    failed |= player.dungeon_level != 0.0 || !used.dungeon_transition ||
              !used.town_transition;
    failed |= !mr_use_wand(&player, MR_WAND_PURPLE,
                           MR_ITEM_CONTEXT_EXPLORATION, NULL, NULL, NULL,
                           &used);
    failed |= player.dungeon_level != -1.0 || !used.dungeon_transition ||
              !used.town_transition;
    player.persistent_values[MR_PERSISTENT_BLUE_WAND_CHARGES] = 1.0;
    player.health_max = 80.0;
    player.health_current = 12.0;
    failed |= !mr_use_wand(&player, MR_WAND_BLUE,
                           MR_ITEM_CONTEXT_EXPLORATION, NULL, NULL, NULL,
                           &used);
    failed |= player.health_current != 80.0 ||
              player.persistent_values[MR_PERSISTENT_BLUE_WAND_CHARGES] != 0.0;

    /* White and Orange update their shared combat counters even when invoked
     * by the exploration command loop (7C3C-7C48). */
    memset(&combat, 0, sizeof(combat));
    player.persistent_values[MR_PERSISTENT_WHITE_WAND_CHARGES] = 1.0;
    failed |= !mr_use_wand(&player, MR_WAND_WHITE,
                           MR_ITEM_CONTEXT_EXPLORATION, &combat, NULL, NULL,
                           &used);
    failed |= used.no_effect || combat.monster_cannot_strike_counter != 10;
    player.persistent_values[MR_PERSISTENT_ORANGE_WAND_CHARGES] = 1.0;
    failed |= !mr_use_wand(&player, MR_WAND_ORANGE,
                           MR_ITEM_CONTEXT_EXPLORATION, &combat, NULL, NULL,
                           &used);
    failed |= used.no_effect || combat.one_shot_damage_bonus != 240;

    player.carried_items[MR_ITEM_SPEED_POTION] = 1.0;
    player.attributes[4] = 20.0;
    player.attributes[5] = 3.0;
    failed |= !mr_use_battle_item(&player, MR_BATTLE_ITEM_SPEED_POTION,
                                  500.0, &combat, NULL, NULL, &used);
    failed |= player.attributes[4] != 33.0 || player.attributes[5] != 3.0 ||
              player.persistent_values[MR_PERSISTENT_AGILITY_EXPIRY] != 600.0;
    MRTimedEffectResult expired;
    mr_expire_timed_combat_effects(&player, 550.0, &combat, &expired);
    failed |= expired.agility_expired || player.attributes[4] != 33.0 ||
              player.attributes[5] != 3.0;
    mr_expire_timed_combat_effects(&player, 601.0, &combat, &expired);
    failed |= !expired.agility_expired || player.attributes[4] != 20.0 ||
              player.attributes[5] != 3.0;

    if (failed) {
        if (error && error_size)
            snprintf(error, error_size,
                     "DUNSMALL item/wand/pill self-test mismatch");
        return 0;
    }
    return 1;
}
