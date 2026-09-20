#ifndef MR_TOWN_H
#define MR_TOWN_H

#include <stddef.h>

#include "mr_character.h"
#include "mr_progression.h"
#include "mr_save.h"

typedef enum MRInnType {
    MR_INN_FLEA_BAG = 0,
    MR_INN_YUPPYDOM = 1,
    MR_INN_KINGS = 2
} MRInnType;

/* DUNSMALL 10FD-12C5 assigns these one-based values and 132A dispatches
 * them through the recovered ON GOTO table.  They are locations in the
 * ordinary floor-zero maze, not an abstract town-menu ordering. */
typedef enum MRTownLocation {
    MR_TOWN_LOCATION_NONE = 0,
    MR_TOWN_LOCATION_FLEA_BAG_INN = 1,
    MR_TOWN_LOCATION_YUPPYDOM_INN = 2,
    MR_TOWN_LOCATION_KINGS_INN = 3,
    MR_TOWN_LOCATION_BANK = 4,
    MR_TOWN_LOCATION_TEMPLE = 5,
    MR_TOWN_LOCATION_STORE = 6,
    MR_TOWN_LOCATION_WIZARD_GUILD = 7
} MRTownLocation;

typedef enum MRTownResult {
    MR_TOWN_OK = 0,
    MR_TOWN_INSUFFICIENT_FUNDS = 1,
    MR_TOWN_ALREADY_OWNED = 2,
    MR_TOWN_NO_UPGRADE = 3,
    MR_TOWN_CLASS_FORBIDDEN = 4,
    MR_TOWN_NO_EFFECT = 5,
    MR_TOWN_INVALID_SELECTION = 6
} MRTownResult;

typedef struct MRInnOutcome {
    MRTownResult result;
    int price;
    int robbed;
    int became_diseased;
    int levels_gained;
} MRInnOutcome;

typedef enum MRTempleService {
    MR_TEMPLE_CURE_WOUNDS = 1,
    MR_TEMPLE_HEAL_ALL_WOUNDS = 2,
    MR_TEMPLE_CURE_DISEASE = 3,
    MR_TEMPLE_REMOVE_POISON = 4,
    MR_TEMPLE_GAIN_LEVEL = 5
} MRTempleService;

typedef enum MRStoreItem {
    MR_STORE_KNIFE = 1,
    MR_STORE_MACE = 2,
    MR_STORE_SWORD = 3,
    MR_STORE_LEATHER_ARMOR = 4,
    MR_STORE_CHAIN_ARMOR = 5,
    MR_STORE_PLATE_ARMOR = 6,
    MR_STORE_FIELD_PLATE_ARMOR = 7,
    MR_STORE_THE_TOWN = 8
} MRStoreItem;

MRInnOutcome mr_inn_stay(MRCharacter *character, MRInnType inn, MRRng *rng);
MRTownLocation mr_town_location_at(int x, int y);
void mr_bank_exchange_carried_treasure(MRCharacter *character);
double mr_bank_transfer(MRCharacter *character, double requested,
                        int withdraw);
MRTownResult mr_temple_purchase(MRCharacter *character,
                                MRTempleService service, MRRng *rng);
int mr_store_price(MRStoreItem item);
int mr_store_town_offer_visible(const MRCharacter *character);
MRTownResult mr_store_purchase(MRCharacter *character, MRStoreItem item);
MRTownResult mr_wizard_guild_charge_information(MRCharacter *character,
                                                int valid_selection);
int mr_wizard_guild_spell_information_price(int one_based_spell_level);
MRTownResult mr_wizard_guild_charge_spell_information(
    MRCharacter *character, int one_based_spell_level, int valid_selection);
int mr_town_self_test(char *error, size_t error_size);

#endif
