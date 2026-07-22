#ifndef MW_TYPES_H
#define MW_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;

/* Native extensions stored in the otherwise-unused tail of the original
 * character record.  WORLD.EXE only knew byte-sized enchantments; the port
 * uses signed 16-bit values so positive bonuses can pass +127 while old
 * cursed equipment remains representable. */
#define MW_NATIVE_CHARACTER_MAGIC   0x3243574Du /* "MWC2" */
#define MW_NATIVE_CHARACTER_VERSION 2u

#define MW_EXPERIENCE_ENHANCED 0u
#define MW_EXPERIENCE_CLASSIC  1u

#pragma pack(push, 1)
typedef struct NativeCharacterExtension {
    u32 magic;
    u16 version;
    u16 quest_flags;
    s16 weapon_enchant[12];
    s16 armor_enchant[8];
    s16 enchant_wpn_spell;
    s16 armor_plus;
    s16 body_armor_plus;
    s16 ring_prot_plus;
    s16 gauntlet;
    u8  experience_mode;
    u8  reserved[21];
} NativeCharacterExtension;
#pragma pack(pop)

_Static_assert(sizeof(NativeCharacterExtension) == 80,
               "native character extension layout changed");

/* Character data structure - 2344 bytes total (0x928)
 * Layout reverse-engineered from WORLD.EXE disassembly.
 * Save files are a raw dump of this struct. */
#pragma pack(push, 1)
typedef struct {
    char name[20];                  /* 0x000 - player name (null-terminated) */
    char _pad_014[20];              /* 0x014 */
    u8   race;                      /* 0x028 - Human,Elf,Dwarf,Hobbit,Gnome,Ogre,Sprite,Imp */
    u8   sex;                       /* 0x029 - 0=Male, 1=Female */
    u8   class_id;                  /* 0x02A - 0=Fighter,1=Worshipper,2=Monk,3=Wizard,4=Priest,5=Sage,6=Mage */
    char _pad_02B[6];               /* 0x02B */
    u16  hp_cur;                    /* 0x031 */
    u16  hp_max;                    /* 0x033 */
    float sp_cur;                   /* 0x035 - spell points (float32) */
    float sp_max;                   /* 0x039 */
    u16  height_inches;             /* 0x03D - rolled from race height */
    u16  weight_pounds;             /* 0x03F - rolled from race weight */
    char _pad_041[64];              /* 0x041 - 0x080 */
    u8   weapon_inventory[8];       /* 0x081 - owned mundane weapons (Fist is implicit) */
    char _pad_089[5];               /* 0x089 - 0x08D */
    u8   eq_wep_enchant[12];        /* 0x08E - permanent weapon enchant per weapon slot */
    char _pad_09A[1];               /* 0x09A */
    u8   equipped_weapon;           /* 0x09B - index into weapon table (0-11) */
    char _pad_09C[20];              /* 0x09C - 0x0AF */
    u8   armor_inventory[8];        /* 0x0B0 - owned armor (Skin is implicit) */
    u8   armor_enchant[8];          /* 0x0B8 - permanent enchant per armor */
    u8   equipped_armor;            /* 0x0C0 - index into armor table */
    char _pad_0C1[156];             /* 0x0C1 - 0x15C */
    u8   orange_pill;               /* 0x15D */
    u8   green_pill;                /* 0x15E */
    u8   blue_pill;                 /* 0x15F */
    u8   red_pill;                  /* 0x160 */
    u8   white_pill;                /* 0x161 */
    u8   yellow_pill;               /* 0x162 */
    char _pad_163[20];              /* 0x163 */

    /* Spells: 4 types x 45 entries */
    u8   spells[4][45];             /* 0x177 - type 0-3 */
    /* Scrolls: 4 types x 45 entries */
    u8   scrolls[4][45];            /* 0x22B */
    /* Wands: 4 types x 45 entries (charge counts) */
    u8   wands[4][45];              /* 0x2DF */
    /* Papers: 4 types x 45 entries */
    u8   papers[4][45];             /* 0x393 */

    char _pad_447[13];              /* 0x447 */
    u32  jewels_pocket;             /* 0x454 */
    u32  jewels_bank;               /* 0x458 */
    u32  copper_stones;             /* 0x45C */
    u32  silver_stones;             /* 0x460 */
    u32  ivory_stones;              /* 0x464 */
    u32  gold_stones;               /* 0x468 */
    u32  platinum_stones;           /* 0x46C */
    u32  jewel_stones;              /* 0x470 */
    char _pad_474[820];             /* 0x474 */

    u16  level;                     /* 0x7A8 */
    u16  facing_dir;                /* 0x7AA - 0=N, 1=S, 2=W, 3=E */
    u16  x_pos;                     /* 0x7AC */
    u16  y_pos;                     /* 0x7AE */
    u16  floor_depth;               /* 0x7B0 */
    u16  dungeon_number;            /* 0x7B2 - procedural dungeon seed */
    char _pad_7B4[18];              /* 0x7B4 */
    u8   ring_regen;                /* 0x7C6 */
    u8   combat_bonus;              /* 0x7C7 - mystery byte: adds to both attack and defense */
    u8   holy_grenade;              /* 0x7C8 */
    u8   stone_see;                 /* 0x7C9 */
    u16  diseased_turns;            /* 0x7CA */
    u16  poisoned_turns;            /* 0x7CC */
    u8   enchant_wpn_spell;          /* 0x7CE - temp Enchant Weapon spell buff (clears at inn) */
    /* NOTE: permanent weapon enchant is eq_wep_enchant[weapon_id] at 0x08E */
    u8   armor_plus;                /* 0x7CF */
    u8   body_armor_plus;           /* 0x7D0 - permanent Body Armor spell bonus */
    u8   ring_prot_plus;            /* 0x7D1 */
    u8   antimagic_ring;            /* 0x7D2 */
    u8   eff_feather;               /* 0x7D3 */
    u8   eff_fast_move;             /* 0x7D4 */
    u8   eff_invisible;             /* 0x7D5 */
    u32  age;                       /* 0x7D6 */
    u8   eff_str_bonus;             /* 0x7DA */
    u8   eff_agi_bonus;             /* 0x7DB */
    u8   eff_super_str;             /* 0x7DC */
    u8   eff_super_agi;             /* 0x7DD */
    u16  eff_battle_str;            /* 0x7DE */
    u16  eff_battle_spd;            /* 0x7E0 */
    u16  eff_slow_mon;              /* 0x7E2 */
    u8   eff_pwr_weapon;            /* 0x7E4 */
    u16  eff_pwr_wpn_turns;         /* 0x7E5 */
    u8   eff_protect_lv;            /* 0x7E7 */
    u16  eff_protect_turns;         /* 0x7E8 */
    u16  eff_resist_poison;         /* 0x7EA */
    u16  eff_resist_disease;        /* 0x7EC */
    u16  eff_anti_cold;             /* 0x7EE */
    u16  eff_anti_fire;             /* 0x7F0 */
    u16  eff_resist_drain;          /* 0x7F2 */
    u16  eff_stop_monster;          /* 0x7F4 */
    u16  eff_hold_monster;          /* 0x7F6 */
    char _pad_7F8[6];               /* 0x7F8 */
    u8   floor_slosher;             /* 0x7FE */
    char _pad_7FF[5];               /* 0x7FF */
    u16  raise_floor;                /* 0x804 - raise-dead return floor/seed */
    u16  raise_x;                    /* 0x806 - 0xFFFF means no contract */
    u16  raise_y;                    /* 0x808 - raise-dead return Y */
    char _pad_80A[4];               /* 0x80A */
    u8   potion_heal;               /* 0x80E */
    char _pad_80F[1];               /* 0x80F */
    u8   stone_teleport;            /* 0x810 */
    char _pad_811[1];               /* 0x811 */
    u16  stat_str;                  /* 0x812 */
    u16  stat_int;                  /* 0x814 */
    u16  stat_wis;                  /* 0x816 */
    u16  stat_con;                  /* 0x818 */
    u16  stat_agi;                  /* 0x81A */
    u16  stat_luck;                 /* 0x81C */
    u8   trapdoor_keys[18];         /* 0x81E - keys 1..17, indexed by label/10 */
    char _pad_830[21];              /* 0x830 */
    u8   quest_flags;                /* 0x845 - shadow/red dragon reward chain */
    u8   gauntlet;                  /* 0x846 */
    char _pad_847[17];              /* 0x847 */
    double experience;              /* 0x858 - original 64-bit XP value */
    union {
        char _pad_860[200];         /* original unused tail, 0x860 - 0x927 */
        NativeCharacterExtension native;
    };
} Character;
#pragma pack(pop)

_Static_assert(sizeof(Character) == 0x928, "Character struct must be 2344 bytes");
_Static_assert(offsetof(Character, weapon_inventory) == 0x081, "weapon inventory offset");
_Static_assert(offsetof(Character, height_inches) == 0x03D, "height offset");
_Static_assert(offsetof(Character, weight_pounds) == 0x03F, "weight offset");
_Static_assert(offsetof(Character, eq_wep_enchant) == 0x08E, "weapon enchant offset");
_Static_assert(offsetof(Character, equipped_weapon) == 0x09B, "equipped weapon offset");
_Static_assert(offsetof(Character, armor_inventory) == 0x0B0, "armor inventory offset");
_Static_assert(offsetof(Character, armor_enchant) == 0x0B8, "armor enchant offset");
_Static_assert(offsetof(Character, equipped_armor) == 0x0C0, "equipped armor offset");
_Static_assert(offsetof(Character, facing_dir) == 0x7AA, "facing direction offset");
_Static_assert(offsetof(Character, dungeon_number) == 0x7B2, "dungeon number offset");
_Static_assert(offsetof(Character, raise_floor) == 0x804, "raise floor offset");
_Static_assert(offsetof(Character, raise_x) == 0x806, "raise X offset");
_Static_assert(offsetof(Character, trapdoor_keys) == 0x81E, "trapdoor key offset");
_Static_assert(offsetof(Character, quest_flags) == 0x845, "quest flags offset");
_Static_assert(offsetof(Character, experience) == 0x858, "experience offset");
_Static_assert(offsetof(Character, native) == 0x860, "native extension offset");

static inline int mw_character_native_valid(const Character *p) {
    return p && p->native.magic == MW_NATIVE_CHARACTER_MAGIC &&
           p->native.version == MW_NATIVE_CHARACTER_VERSION;
}

/* Import the byte-sized original values once.  This also converts legacy
 * saves automatically the first time the native port loads/saves them. */
static inline void mw_character_native_ensure(Character *p) {
    if (!p || mw_character_native_valid(p)) return;
    NativeCharacterExtension ext = {0};
    ext.magic = MW_NATIVE_CHARACTER_MAGIC;
    ext.version = MW_NATIVE_CHARACTER_VERSION;
    ext.quest_flags = p->quest_flags;
    for (int i = 0; i < 12; i++)
        ext.weapon_enchant[i] = (s8)p->eq_wep_enchant[i];
    for (int i = 0; i < 8; i++)
        ext.armor_enchant[i] = (s8)p->armor_enchant[i];
    ext.enchant_wpn_spell = p->enchant_wpn_spell;
    ext.armor_plus = p->armor_plus;
    ext.body_armor_plus = p->body_armor_plus;
    ext.ring_prot_plus = p->ring_prot_plus;
    ext.gauntlet = p->gauntlet;
    p->native = ext;
}

static inline s16 mw_weapon_enchant(const Character *p, int slot) {
    if (!p || slot < 0 || slot >= 12) return 0;
    return mw_character_native_valid(p) ? p->native.weapon_enchant[slot] :
                                         (s8)p->eq_wep_enchant[slot];
}

static inline void mw_set_weapon_enchant(Character *p, int slot, int value) {
    if (!p || slot < 0 || slot >= 12) return;
    if (value > INT16_MAX) value = INT16_MAX;
    if (value < INT16_MIN) value = INT16_MIN;
    mw_character_native_ensure(p);
    p->native.weapon_enchant[slot] = (s16)value;
    p->eq_wep_enchant[slot] = (u8)(value > 127 ? 127 :
                                      value < -128 ? -128 : (s8)value);
}

static inline s16 mw_armor_enchant(const Character *p, int slot) {
    if (!p || slot < 0 || slot >= 8) return 0;
    return mw_character_native_valid(p) ? p->native.armor_enchant[slot] :
                                         (s8)p->armor_enchant[slot];
}

static inline void mw_set_armor_enchant(Character *p, int slot, int value) {
    if (!p || slot < 0 || slot >= 8) return;
    if (value > INT16_MAX) value = INT16_MAX;
    if (value < INT16_MIN) value = INT16_MIN;
    mw_character_native_ensure(p);
    p->native.armor_enchant[slot] = (s16)value;
    p->armor_enchant[slot] = (u8)(value > 127 ? 127 :
                                    value < -128 ? -128 : (s8)value);
}

#define MW_NATIVE_BONUS_ACCESSORS(NAME, LEGACY)                              \
static inline s16 mw_##NAME(const Character *p) {                            \
    return mw_character_native_valid(p) ? p->native.NAME : (s16)p->LEGACY;   \
}                                                                            \
static inline void mw_set_##NAME(Character *p, int value) {                  \
    if (!p) return;                                                           \
    if (value > INT16_MAX) value = INT16_MAX;                                \
    if (value < 0) value = 0;                                                 \
    mw_character_native_ensure(p);                                            \
    p->native.NAME = (s16)value;                                              \
    p->LEGACY = (u8)(value > 255 ? 255 : value);                             \
}

MW_NATIVE_BONUS_ACCESSORS(enchant_wpn_spell, enchant_wpn_spell)
MW_NATIVE_BONUS_ACCESSORS(armor_plus, armor_plus)
MW_NATIVE_BONUS_ACCESSORS(body_armor_plus, body_armor_plus)
MW_NATIVE_BONUS_ACCESSORS(ring_prot_plus, ring_prot_plus)
MW_NATIVE_BONUS_ACCESSORS(gauntlet, gauntlet)
#undef MW_NATIVE_BONUS_ACCESSORS

static inline u16 mw_quest_flags(const Character *p) {
    return mw_character_native_valid(p) ? p->native.quest_flags :
                                         (u16)p->quest_flags;
}

static inline void mw_set_quest_flags(Character *p, u16 flags) {
    if (!p) return;
    mw_character_native_ensure(p);
    p->native.quest_flags = flags;
    p->quest_flags = (u8)flags;
}

/* Existing native/legacy saves default to Enhanced because the formerly
 * reserved byte is zero.  Only newly-created characters explicitly choosing
 * Classic receive the nonzero marker. */
static inline int mw_experience_mode(const Character *p) {
    return mw_character_native_valid(p) &&
           p->native.experience_mode == MW_EXPERIENCE_CLASSIC ?
           MW_EXPERIENCE_CLASSIC : MW_EXPERIENCE_ENHANCED;
}

static inline void mw_set_experience_mode(Character *p, int mode) {
    if (!p) return;
    mw_character_native_ensure(p);
    p->native.experience_mode = mode == MW_EXPERIENCE_CLASSIC ?
                                MW_EXPERIENCE_CLASSIC :
                                MW_EXPERIENCE_ENHANCED;
}

/* Race IDs */
enum {
    RACE_HUMAN = 0, RACE_ELF, RACE_DWARF, RACE_HOBBIT,
    RACE_GNOME, RACE_OGRE, RACE_SPRITE, RACE_IMP, RACE_COUNT
};

/* Class IDs */
enum {
    CLASS_FIGHTER = 0, CLASS_WORSHIPPER, CLASS_MONK, CLASS_WIZARD,
    CLASS_PRIEST, CLASS_SAGE, CLASS_MAGE, CLASS_COUNT
};

static const char *race_names[] = {
    "HUMAN", "ELF", "DWARF", "HOBBIT", "GNOME", "OGRE", "SPRITE", "IMP"
};

static const char *class_names[] = {
    "FIGHTER", "WORSHIPPER", "MONK", "WIZARD", "PRIEST", "SAGE", "MAGE"
};

/* Max players/save slots */
#define MAX_PLAYERS     10
#define MAX_DUNGEON_FLOOR  1000
#define MAX_DUNGEON_FLOORS (MAX_DUNGEON_FLOOR + 1)
#define CLASSIC_DUNGEON_FLOOR 250
#define SPELLS_PER_TYPE 45
#define SPELL_TYPES     4

#endif /* MW_TYPES_H */
