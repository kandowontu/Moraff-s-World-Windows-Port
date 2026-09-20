#include "mr_town.h"
#include "mr_math.h"

#include <math.h>
#include <stdio.h>

static int mr_basic_int(float value) {
    return (int)floorf(value);
}

static int mr_inn_price(MRInnType inn) {
    static const int prices[] = {10, 200, 6000};
    return inn >= MR_INN_FLEA_BAG && inn <= MR_INN_KINGS
               ? prices[(int)inn]
               : 0;
}

MRTownLocation mr_town_location_at(int x, int y) {
    /* Exact coordinate comparisons in show_rope_prompt_and_climb,
     * DUNSMALL 10FD-12C5.  Temple and store deliberately have several
     * entrances; later matches assign the same dispatch value. */
    if (x == 7 && y == 3) return MR_TOWN_LOCATION_FLEA_BAG_INN;
    if (x == 3 && y == 2) return MR_TOWN_LOCATION_YUPPYDOM_INN;
    if (x == 18 && y == 17) return MR_TOWN_LOCATION_KINGS_INN;
    if (x == 13 && y == 3) return MR_TOWN_LOCATION_BANK;
    if ((x == 7 && y == 15) || (x == 14 && y == 12))
        return MR_TOWN_LOCATION_TEMPLE;
    if ((x == 18 && y == 3) || (x == 13 && y == 18) ||
        (x == 2 && y == 8))
        return MR_TOWN_LOCATION_STORE;
    if (x == 6 && y == 14) return MR_TOWN_LOCATION_WIZARD_GUILD;
    return MR_TOWN_LOCATION_NONE;
}

static int mr_inn_robbery(MRCharacter *character, MRRng *rng,
                          int denominator) {
    /* DUNSMALL 1EE2-1F3C: the selected inn first stores 10 or 20, then the
     * shared helper tests INT(RND*denominator)=1. */
    if (mr_basic_int((float)mr_rng_next(rng) * denominator) != 1) return 0;
    character->pocket_money = 0.0;
    character->persistent_values[MR_PERSISTENT_KNIFE_OWNED] = 0.0f;
    character->persistent_values[MR_PERSISTENT_SWORD_OWNED] = 0.0f;
    character->persistent_values[MR_PERSISTENT_MACE_OWNED] = 0.0f;
    character->magic_item_values[MR_MAGIC_SWORD_BONUS] = 0.0f;
    character->magic_item_values[MR_MAGIC_MACE_BONUS] = 0.0f;
    return 1;
}

MRInnOutcome mr_inn_stay(MRCharacter *character, MRInnType inn, MRRng *rng) {
    MRInnOutcome outcome = {MR_TOWN_INVALID_SELECTION, 0, 0, 0, 0};
    if (!character || !rng || inn < MR_INN_FLEA_BAG || inn > MR_INN_KINGS)
        return outcome;
    outcome.price = mr_inn_price(inn);
    if (character->pocket_money < outcome.price) {
        outcome.result = MR_TOWN_INSUFFICIENT_FUNDS;
        return outcome;
    }
    character->pocket_money -= outcome.price;
    outcome.result = MR_TOWN_OK;

    /* DS:B558 bit zero is tested by both inexpensive inns.  It is left as a
     * literal recovered flag until the corresponding magic-item award
     * routine has been named conclusively. */
    if (inn == MR_INN_FLEA_BAG) {
        character->health_current += 1.0f;
        if (((int)character->status_flags & 1) != 0)
            character->health_current = character->health_max;
        if (character->health_current > character->health_max)
            character->health_current = character->health_max;
        /* Flea Bag 1E72 and Yuppydom 1FA8 call 3B02 before sleeping. */
        mr_clear_preparation_effects(character);
        outcome.robbed = mr_inn_robbery(character, rng, 10);
        if (mr_basic_int((float)mr_rng_next(rng) * 10.0f) == 1) {
            character->attributes[3] -= 1.0f;
            mr_clamp_character_attributes(character);
            character->persistent_values[MR_PERSISTENT_DISEASE] = 1.0f;
            outcome.became_diseased = 1;
        }
    } else if (inn == MR_INN_YUPPYDOM) {
        character->health_current += 3.0f;
        if (((int)character->status_flags & 1) != 0)
            character->health_current = character->health_max;
        if (character->health_current > character->health_max)
            character->health_current = character->health_max;
        mr_clear_preparation_effects(character);
        outcome.robbed = mr_inn_robbery(character, rng, 20);
    } else {
        /* King's Inn 2006-2041 deliberately omits the 3B02 clear. */
        character->health_current = character->health_max;
    }
    outcome.levels_gained = mr_settle_pending_experience(character, rng);
    return outcome;
}

void mr_bank_exchange_carried_treasure(MRCharacter *character) {
    if (!character) return;
    /* DUNSMALL 22F7-2355. Entering the bank automatically cashes in all
     * dungeon treasure and leaves the bag's coin-content sentinel at .5. */
    character->magic_item_values[MR_MAGIC_BAG_COINS] = 0.5f;
    character->pocket_money += character->carried_treasure;
    character->carried_treasure = 0.0f;
    /* 231F-2355 performs the same base-load recomputation as abandoning
     * treasure: 150 pounds plus 25 per ordinary armor level, while magic
     * armor (status bit 32) is weightless. */
    float armor_weight = ((int)character->status_flags &
                          MR_STATUS_MAGIC_ARMOR) != 0
        ? 0.0f : character->equipped_armor * 25.0f;
    character->carried_weight = 150.0f + armor_weight;
}

double mr_bank_transfer(MRCharacter *character, double requested,
                        int withdraw) {
    if (!character || requested <= 0.0) return 0.0;
    double *source = withdraw ? &character->bank_money
                              : &character->pocket_money;
    double *destination = withdraw ? &character->pocket_money
                                   : &character->bank_money;
    double amount = requested > *source ? *source : requested;
    *source -= amount;
    *destination += amount;
    return amount;
}

static int mr_temple_price(MRTempleService service) {
    static const int prices[] = {0, 75, 1000, 400, 20000, 500000};
    return service >= MR_TEMPLE_CURE_WOUNDS && service <= MR_TEMPLE_GAIN_LEVEL
               ? prices[(int)service]
               : 0;
}

MRTownResult mr_temple_purchase(MRCharacter *character,
                                MRTempleService service, MRRng *rng) {
    if (!character || !rng || service < MR_TEMPLE_CURE_WOUNDS ||
        service > MR_TEMPLE_GAIN_LEVEL)
        return MR_TOWN_INVALID_SELECTION;
    int price = mr_temple_price(service);
    if (character->pocket_money < price) return MR_TOWN_INSUFFICIENT_FUNDS;
    /* The original charges before discovering that disease/poison is absent. */
    character->pocket_money -= price;
    switch (service) {
        case MR_TEMPLE_CURE_WOUNDS:
            character->health_current +=
                mr_basic_int((float)mr_rng_next(rng) * 8.0f) + 4;
            if (character->health_current > character->health_max)
                character->health_current = character->health_max;
            return MR_TOWN_OK;
        case MR_TEMPLE_HEAL_ALL_WOUNDS:
            character->health_current = character->health_max;
            return MR_TOWN_OK;
        case MR_TEMPLE_CURE_DISEASE:
            if (character->persistent_values[MR_PERSISTENT_DISEASE] == 0.0f)
                return MR_TOWN_NO_EFFECT;
            character->persistent_values[MR_PERSISTENT_DISEASE] = 0.0f;
            return MR_TOWN_OK;
        case MR_TEMPLE_REMOVE_POISON:
            if (character->persistent_values[MR_PERSISTENT_POISON] == 0.0f)
                return MR_TOWN_NO_EFFECT;
            character->persistent_values[MR_PERSISTENT_POISON] = 0.0f;
            return MR_TOWN_OK;
        case MR_TEMPLE_GAIN_LEVEL:
            mr_gain_one_level(character, rng);
            return MR_TOWN_OK;
        default:
            return MR_TOWN_INVALID_SELECTION;
    }
}

int mr_store_price(MRStoreItem item) {
    static const int prices[] = {0, 10, 200, 200, 200, 500, 3000, 10000,
                                 1000000};
    return item >= MR_STORE_KNIFE && item <= MR_STORE_THE_TOWN
               ? prices[(int)item]
               : 0;
}

int mr_store_town_offer_visible(const MRCharacter *character) {
    if (!character) return 0;
    /* DUNSMALL 28B5-28DF uses 99,999 for visibility even though the actual
     * purchase below costs one million pocket JP. */
    return character->persistent_values[MR_PERSISTENT_OWNS_TOWN] == 0.0f &&
           character->pocket_money + character->bank_money > 99999.0;
}

MRTownResult mr_store_purchase(MRCharacter *character, MRStoreItem item) {
    if (!character || item < MR_STORE_KNIFE || item > MR_STORE_THE_TOWN)
        return MR_TOWN_INVALID_SELECTION;
    /* DUNSMALL 2974-29B6 tests `selection > 1 AND class = 2`.
     * Wizards may therefore buy the Knife (choice 1), but not the Mace,
     * Sword, or any of the four armor upgrades (choices 2..7). */
    if (item >= MR_STORE_MACE && item <= MR_STORE_FIELD_PLATE_ARMOR &&
        (int)character->player_class == MR_CLASS_WIZARD)
        return MR_TOWN_CLASS_FORBIDDEN;

    if (item == MR_STORE_THE_TOWN) {
        if (character->persistent_values[MR_PERSISTENT_OWNS_TOWN] != 0.0f)
            return MR_TOWN_ALREADY_OWNED;
    } else if (item <= MR_STORE_SWORD) {
        int index = item == MR_STORE_KNIFE
                        ? MR_PERSISTENT_KNIFE_OWNED
                        : item == MR_STORE_MACE ? MR_PERSISTENT_MACE_OWNED
                                                : MR_PERSISTENT_SWORD_OWNED;
        if (character->persistent_values[index] != 0.0f)
            return MR_TOWN_ALREADY_OWNED;
    } else {
        int armor_level = (int)item - 3;
        if (character->equipped_armor >= armor_level)
            return MR_TOWN_NO_UPGRADE;
    }

    int price = mr_store_price(item);
    if (character->pocket_money < price) return MR_TOWN_INSUFFICIENT_FUNDS;
    character->pocket_money -= price;
    if (item == MR_STORE_THE_TOWN) {
        character->persistent_values[MR_PERSISTENT_OWNS_TOWN] = 1.0f;
    } else if (item <= MR_STORE_SWORD) {
        int index = item == MR_STORE_KNIFE
                        ? MR_PERSISTENT_KNIFE_OWNED
                        : item == MR_STORE_MACE ? MR_PERSISTENT_MACE_OWNED
                                                : MR_PERSISTENT_SWORD_OWNED;
        character->persistent_values[index] = 1.0f;
    } else {
        character->equipped_armor = (float)((int)item - 3);
    }
    return MR_TOWN_OK;
}

MRTownResult mr_wizard_guild_charge_information(MRCharacter *character,
                                                int valid_selection) {
    if (!character) return MR_TOWN_INVALID_SELECTION;
    if (character->pocket_money < 800.0)
        return MR_TOWN_INSUFFICIENT_FUNDS;
    /* DUNSMALL 2C35-2D43 only takes the advertised fee after the nested spell
     * or item picker returns a nonzero entry. */
    if (!valid_selection) return MR_TOWN_NO_EFFECT;
    character->pocket_money -= 800.0;
    return MR_TOWN_OK;
}

int mr_wizard_guild_spell_information_price(int one_based_spell_level) {
    if (one_based_spell_level < 1 || one_based_spell_level > 6) return 0;
    /* DUNSMALL 2DAC-2DC3: BRUN30 $FEXC and all intermediates are MBF
     * singles before INT(220 * level ^ 1.75). */
    float price = mr_qb3_pow((float)one_based_spell_level, 1.75f);
    price = price * 220.0f;
    return (int)floorf(price);
}

MRTownResult mr_wizard_guild_charge_spell_information(
    MRCharacter *character, int one_based_spell_level, int valid_selection) {
    if (!character || one_based_spell_level < 1 ||
        one_based_spell_level > 6)
        return MR_TOWN_INVALID_SELECTION;
    int price = mr_wizard_guild_spell_information_price(
        one_based_spell_level);
    if (character->pocket_money < price)
        return MR_TOWN_INSUFFICIENT_FUNDS;
    /* As with magic-item information, leaving the nested P/B prompt does
     * not charge the character. */
    if (!valid_selection) return MR_TOWN_NO_EFFECT;
    character->pocket_money -= price;
    return MR_TOWN_OK;
}

int mr_town_self_test(char *error, size_t error_size) {
    int failed = 0;
    MRCharacter c = {0};
    MRRng rng;
    mr_rng_seed_default(&rng);
    failed |= mr_town_location_at(7, 3) != MR_TOWN_LOCATION_FLEA_BAG_INN ||
              mr_town_location_at(3, 2) != MR_TOWN_LOCATION_YUPPYDOM_INN ||
              mr_town_location_at(18, 17) != MR_TOWN_LOCATION_KINGS_INN ||
              mr_town_location_at(13, 3) != MR_TOWN_LOCATION_BANK ||
              mr_town_location_at(14, 12) != MR_TOWN_LOCATION_TEMPLE ||
              mr_town_location_at(2, 8) != MR_TOWN_LOCATION_STORE ||
              mr_town_location_at(6, 14) !=
                  MR_TOWN_LOCATION_WIZARD_GUILD ||
              mr_town_location_at(10, 10) != MR_TOWN_LOCATION_NONE;
    c.pocket_money = 10000;
    c.health_max = 50;
    c.health_current = 10;
    MRInnOutcome inn = mr_inn_stay(&c, MR_INN_KINGS, &rng);
    failed |= inn.result != MR_TOWN_OK || c.pocket_money != 4000 ||
              c.health_current != 50;

    /* Seed six makes Flea Bag's robbery roll 7 and its sickness roll 1.
     * Sickness removes one Health characteristic before setting disease,
     * then 2F43 clamps the result to one. */
    c = (MRCharacter){0};
    c.pocket_money = 100;
    c.health_max = 20;
    c.health_current = 10;
    c.attributes[3] = 1;
    rng.state = 0x000006U;
    inn = mr_inn_stay(&c, MR_INN_FLEA_BAG, &rng);
    failed |= inn.result != MR_TOWN_OK || inn.robbed ||
              !inn.became_diseased || c.attributes[3] != 1 ||
              c.persistent_values[MR_PERSISTENT_DISEASE] != 1;

    /* Seed 53 makes the first $RAN value land in INT(RND*10)=1. */
    c = (MRCharacter){0};
    c.pocket_money = 100;
    c.bank_money = 77;
    c.health_max = 20;
    c.health_current = 10;
    c.equipped_armor = 3;
    c.persistent_values[MR_PERSISTENT_KNIFE_OWNED] = 1;
    c.persistent_values[MR_PERSISTENT_SWORD_OWNED] = 1;
    c.persistent_values[MR_PERSISTENT_MACE_OWNED] = 1;
    c.magic_item_values[MR_MAGIC_SWORD_BONUS] = 9;
    c.magic_item_values[MR_MAGIC_MACE_BONUS] = 8;
    rng.state = 53;
    inn = mr_inn_stay(&c, MR_INN_FLEA_BAG, &rng);
    failed |= inn.result != MR_TOWN_OK || !inn.robbed ||
              c.pocket_money != 0 || c.bank_money != 77 ||
              c.equipped_armor != 3 ||
              c.persistent_values[MR_PERSISTENT_KNIFE_OWNED] != 0 ||
              c.persistent_values[MR_PERSISTENT_SWORD_OWNED] != 0 ||
              c.persistent_values[MR_PERSISTENT_MACE_OWNED] != 0 ||
              c.magic_item_values[MR_MAGIC_SWORD_BONUS] != 0 ||
              c.magic_item_values[MR_MAGIC_MACE_BONUS] != 0;

    c = (MRCharacter){0};
    c.pocket_money = 10000;
    c.health_max = 20;
    c.health_current = 10;
    c.attributes[0] = 20;
    c.attributes[4] = 20;
    c.active_effects[MR_EFFECT_INVISIBILITY] = 1;
    c.active_effects[MR_EFFECT_PREPARATION_STRENGTH] = 1;
    c.active_effects[MR_EFFECT_PREPARATION_SPEED] = 1;
    rng.state = 0x000006U;
    inn = mr_inn_stay(&c, MR_INN_YUPPYDOM, &rng);
    failed |= inn.result != MR_TOWN_OK ||
              c.active_effects[MR_EFFECT_INVISIBILITY] != 0 ||
              c.active_effects[MR_EFFECT_PREPARATION_STRENGTH] != 0 ||
              c.active_effects[MR_EFFECT_PREPARATION_SPEED] != 0 ||
              c.attributes[0] != 14 || c.attributes[4] != 13;

    c = (MRCharacter){0};
    c.pocket_money = 10000;
    c.health_max = 20;
    c.health_current = 10;
    c.attributes[0] = 20;
    c.attributes[4] = 20;
    c.active_effects[MR_EFFECT_INVISIBILITY] = 1;
    c.active_effects[MR_EFFECT_PREPARATION_STRENGTH] = 1;
    c.active_effects[MR_EFFECT_PREPARATION_SPEED] = 1;
    rng.state = 0x000006U;
    inn = mr_inn_stay(&c, MR_INN_KINGS, &rng);
    failed |= inn.result != MR_TOWN_OK ||
              c.active_effects[MR_EFFECT_INVISIBILITY] != 1 ||
              c.active_effects[MR_EFFECT_PREPARATION_STRENGTH] != 1 ||
              c.active_effects[MR_EFFECT_PREPARATION_SPEED] != 1 ||
              c.attributes[0] != 20 || c.attributes[4] != 20;

    c = (MRCharacter){0};
    c.pocket_money = 100;
    c.bank_money = 25;
    failed |= mr_bank_transfer(&c, 40, 1) != 25 || c.pocket_money != 125 ||
              c.bank_money != 0;
    failed |= mr_bank_transfer(&c, 30, 0) != 30 || c.pocket_money != 95 ||
              c.bank_money != 30;
    c.carried_treasure = 123;
    c.equipped_armor = 3;
    c.carried_weight = 999;
    mr_bank_exchange_carried_treasure(&c);
    failed |= c.pocket_money != 218 || c.carried_treasure != 0 ||
              c.magic_item_values[MR_MAGIC_BAG_COINS] != 0.5 ||
              c.carried_weight != 225;

    c = (MRCharacter){0};
    c.pocket_money = 1000000;
    c.health_max = 20;
    c.health_current = 1;
    mr_rng_seed_default(&rng);
    failed |= mr_temple_purchase(&c, MR_TEMPLE_HEAL_ALL_WOUNDS, &rng) !=
                  MR_TOWN_OK ||
              c.health_current != 20 || c.pocket_money != 999000;
    failed |= mr_temple_purchase(&c, MR_TEMPLE_CURE_DISEASE, &rng) !=
                  MR_TOWN_NO_EFFECT ||
              c.pocket_money != 998600;

    c = (MRCharacter){0};
    c.player_class = MR_CLASS_FIGHTER;
    c.pocket_money = 20000;
    failed |= mr_store_purchase(&c, MR_STORE_FIELD_PLATE_ARMOR) != MR_TOWN_OK ||
              c.equipped_armor != 4 || c.pocket_money != 10000;
    failed |= mr_store_purchase(&c, MR_STORE_LEATHER_ARMOR) !=
              MR_TOWN_NO_UPGRADE;
    c.player_class = MR_CLASS_WIZARD;
    c.pocket_money = 210;
    failed |= mr_store_purchase(&c, MR_STORE_KNIFE) != MR_TOWN_OK ||
              c.pocket_money != 200 ||
              c.persistent_values[MR_PERSISTENT_KNIFE_OWNED] != 1;
    failed |= mr_store_purchase(&c, MR_STORE_MACE) != MR_TOWN_CLASS_FORBIDDEN ||
              c.pocket_money != 200;

    c = (MRCharacter){0};
    c.pocket_money = 1000000;
    c.bank_money = 0;
    failed |= !mr_store_town_offer_visible(&c);
    failed |= mr_store_purchase(&c, MR_STORE_THE_TOWN) != MR_TOWN_OK ||
              c.pocket_money != 0 ||
              c.persistent_values[MR_PERSISTENT_OWNS_TOWN] != 1;

    c = (MRCharacter){0};
    c.pocket_money = 10000;
    failed |= mr_wizard_guild_spell_information_price(1) != 220 ||
              mr_wizard_guild_spell_information_price(6) != 5060 ||
              mr_wizard_guild_spell_information_price(0) != 0 ||
              mr_wizard_guild_charge_spell_information(&c, 2, 0) !=
                  MR_TOWN_NO_EFFECT ||
              c.pocket_money != 10000 ||
              mr_wizard_guild_charge_spell_information(&c, 2, 1) !=
                  MR_TOWN_OK ||
              c.pocket_money !=
                  10000 - mr_wizard_guild_spell_information_price(2);

    if (failed) {
        if (error && error_size)
            snprintf(error, error_size,
                     "DUNSMALL town/economy formula self-test mismatch");
        return 0;
    }
    return 1;
}
