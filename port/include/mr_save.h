#ifndef MR_SAVE_H
#define MR_SAVE_H

#include <stddef.h>
#include <stdint.h>

#define MR_SAVE_RECORDS 311
#define MR_SAVE_ATTRIBUTES 6
#define MR_SAVE_TABLE_SIZE 30
#define MR_SAVE_SPELL_MASK_ROWS 12
#define MR_SAVE_PERSISTENT_VALUES 200
#define MR_PERSISTENT_RESERVED_TAIL_FIRST 36
#define MR_EXPLORED_LEVELS 71
#define MR_EXPLORED_ROWS 21
#define MR_BIN_TRAILING_BYTES 81

/* DUNSMALL stores QuickBASIC arrays with a physical element zero at the
 * address named in the disassembly, but its save loops persist elements
 * 1..N. Native arrays below are zero based, so these constants are the
 * persisted BASIC indices minus one. */
typedef enum MRMagicItemValueIndex {
    MR_MAGIC_BAG_COINS = 0,       /* BASIC magic value 1, DS:B422 */
    MR_MAGIC_HEALTH_RINGS = 1,    /* BASIC magic value 2, DS:B426 */
    MR_MAGIC_SWORD_BONUS = 2,     /* BASIC magic value 3, DS:B42A */
    MR_MAGIC_MACE_BONUS = 3,      /* BASIC magic value 4, DS:B42E */
    MR_MAGIC_RING_BONUS = 4,      /* BASIC magic value 5, DS:B432 */
    MR_MAGIC_ARMOR_BONUS = 5,     /* BASIC magic value 6, DS:B436 */
    MR_MAGIC_HOLY_GRENADES = 6    /* BASIC magic value 7, DS:B43A */
} MRMagicItemValueIndex;

typedef enum MRStatusFlag {
    MR_STATUS_RING_OF_HEALTH = 1,
    MR_STATUS_BAG_OF_HOLDING = 2,
    MR_STATUS_MAGIC_SWORD = 4,
    MR_STATUS_MAGIC_MACE = 8,
    MR_STATUS_MAGIC_RING = 16,
    MR_STATUS_MAGIC_ARMOR = 32,
    MR_STATUS_FLOOR_SLOSHER = 64
} MRStatusFlag;

typedef enum MRCarriedItemIndex {
    MR_ITEM_TELEPORT_SCROLL = 0,       /* BASIC carried item 1, DS:B2D6 */
    MR_ITEM_SEEING_SCROLL = 1,         /* BASIC carried item 2, DS:B2DA */
    MR_ITEM_HEALING_SCROLL = 2,        /* BASIC carried item 3, DS:B2DE */
    MR_ITEM_SPELL_POINT_SCROLL = 3,    /* BASIC carried item 4, DS:B2E2 */
    MR_ITEM_SPEED_POTION = 4,          /* BASIC carried item 5, DS:B2E6 */
    MR_ITEM_FIRE_POTION = 5,           /* BASIC carried item 6, DS:B2EA */
    MR_ITEM_SHIELDING_POTION = 6,      /* BASIC carried item 7, DS:B2EE */
    MR_ITEM_HEALTH_POTION = 7,         /* BASIC carried item 8, DS:B2F2 */
    MR_ITEM_RELOCATION_POTION = 8      /* BASIC carried item 9, DS:B2F6 */
} MRCarriedItemIndex;

typedef enum MRActiveEffectIndex {
    MR_EFFECT_BATTLE_SPEED = 0,          /* BASIC effect 1, DS:6024 */
    MR_EFFECT_BATTLE_STRENGTH = 1,       /* BASIC effect 2, DS:6028 */
    MR_EFFECT_PREPARATION_STRENGTH = 2,  /* BASIC effect 3, DS:602C */
    MR_EFFECT_PREPARATION_SPEED = 3,     /* BASIC effect 4, DS:6030 */
    MR_EFFECT_INVISIBILITY = 4           /* BASIC effect 5, DS:6034 */
} MRActiveEffectIndex;

typedef enum MRSpellbookMaskType {
    /* The original writes these two values in this order on every record. */
    MR_SPELLBOOK_BATTLE = 0,      /* DS:5EE0 */
    MR_SPELLBOOK_PREPARATION = 1  /* DS:5EAC */
} MRSpellbookMaskType;

typedef enum MRPersistentValueIndex {
    MR_PERSISTENT_KNIFE_OWNED = 0,       /* BASIC value 1, DS:1B96 */
    MR_PERSISTENT_SWORD_OWNED = 1,       /* BASIC value 2, DS:1B9A */
    MR_PERSISTENT_MACE_OWNED = 2,        /* BASIC value 3, DS:1B9E */
    MR_PERSISTENT_DISEASE = 3,           /* BASIC value 4, DS:1BA2 */
    MR_PERSISTENT_POISON = 4,            /* BASIC value 5, DS:1BA6 */
    MR_PERSISTENT_FOUNTAIN_X = 9,        /* BASIC value 10, DS:1BBA */
    MR_PERSISTENT_FOUNTAIN_Y = 10,       /* BASIC value 11, DS:1BBE */
    MR_PERSISTENT_BREATHE_FIRE_EXPIRY = 11, /* BASIC value 12, DS:1BC2 */
    MR_PERSISTENT_SHIELDING_EXPIRY = 12,    /* BASIC value 13, DS:1BC6 */
    MR_PERSISTENT_AGILITY_EXPIRY = 13,      /* BASIC value 14, DS:1BCA */
    MR_PERSISTENT_OWNS_TOWN = 16,        /* BASIC value 17, DS:1BD6 */
    MR_PERSISTENT_RACE = 20,             /* BASIC value 21, DS:1BE6 */
    MR_PERSISTENT_BLUE_PILLS = 21,       /* BASIC values 22..27 */
    MR_PERSISTENT_RED_PILLS = 22,
    MR_PERSISTENT_GREEN_PILLS = 23,
    MR_PERSISTENT_YELLOW_PILLS = 24,
    MR_PERSISTENT_ORANGE_PILLS = 25,
    MR_PERSISTENT_WHITE_PILLS = 26,
    MR_PERSISTENT_PURPLE_WAND_CHARGES = 27, /* BASIC values 28..36 */
    MR_PERSISTENT_BROWN_WAND_CHARGES = 28,
    MR_PERSISTENT_BLACK_WAND_CHARGES = 29,
    MR_PERSISTENT_WHITE_WAND_CHARGES = 30,
    MR_PERSISTENT_ORANGE_WAND_CHARGES = 31,
    MR_PERSISTENT_YELLOW_WAND_CHARGES = 32,
    MR_PERSISTENT_GREEN_WAND_CHARGES = 33,
    MR_PERSISTENT_RED_WAND_CHARGES = 34,
    MR_PERSISTENT_BLUE_WAND_CHARGES = 35
} MRPersistentValueIndex;

typedef struct MRCharacter {
    /* Match the original QuickBASIC record types, not a uniform host type.
     * DUNSMALL stores only experience and the two money balances as MBF
     * doubles; every other numeric field below is an MBF single.  Keeping
     * the distinction in memory preserves the source's assignment-rounding
     * boundaries throughout combat, progression, items, and traversal. */
    float attributes[MR_SAVE_ATTRIBUTES];
    float combat_attack_factor;
    float health_growth_factor;
    float agility_defense_factor;
    float player_class;
    float equipped_armor;
    double experience;
    float player_level;
    float health_max;
    float health_current;
    float status_flags;
    float carried_weight;
    float carried_treasure;
    double pocket_money;
    double bank_money;
    float pending_experience;
    float spell_points;
    float player_x;
    float player_y;
    float dungeon_level;
    float dungeon_generation_seed;
    float active_effects[MR_SAVE_TABLE_SIZE];
    float magic_item_values[MR_SAVE_TABLE_SIZE];
    float carried_items[MR_SAVE_TABLE_SIZE];
    float spellbook_masks[MR_SAVE_SPELL_MASK_ROWS][2];
    float persistent_values[MR_SAVE_PERSISTENT_VALUES];
} MRCharacter;

typedef struct MRSave {
    /* `stored` is the literal 311-record representation. `character` is the
     * gameplay representation after DUNSMALL's exact load-time transforms. */
    MRCharacter stored;
    MRCharacter character;
    uint32_t explored[MR_EXPLORED_LEVELS][MR_EXPLORED_ROWS];
    /* QuickBASIC treats any MBF value with exponent zero as numeric zero, but
     * leaves the three mantissa bytes unspecified. Preserve those source
     * bytes so an unmodified original sidecar round-trips byte-for-byte. */
    uint8_t explored_raw[MR_EXPLORED_LEVELS][MR_EXPLORED_ROWS][4];
    /* DUNSMALL's fixed BSAVE endpoint extends 81 bytes beyond the explored
     * grid. Static use-site analysis finds no fields in this zero-initialized
     * range and all five supplied sidecars contain zeroes there. Preserve it
     * verbatim so imported originals still round-trip byte-for-byte. */
    uint8_t binary_tail[MR_BIN_TRAILING_BYTES];
    /* B964-B97B BLOADs at an explicit DS:9B06 destination, so these header
     * words are metadata only. Original slots contain several values; retain
     * them verbatim for round-trip fidelity. */
    uint16_t bsave_segment;
    uint16_t bsave_offset;
    int text_dos_eof;
    int binary_dos_eof;
} MRSave;

int mr_save_load(MRSave *save, const char *text_path, const char *binary_path,
                 char *error, size_t error_size);
int mr_save_write(const MRSave *save, const char *text_path,
                  const char *binary_path, char *error, size_t error_size);
int mr_save_self_test(const char *directory, char *error, size_t error_size);

#endif
