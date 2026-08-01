#ifndef MW_TYPES_H
#define MW_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

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
#define MW_NATIVE_CHARACTER_VERSION 6u

#define MW_EXPERIENCE_ENHANCED 0u
#define MW_EXPERIENCE_CLASSIC  1u
#define MW_ORIGINAL_SPELL_COUNT 30
#define MW_ENHANCED_SPELL_COUNT 45
#define MW_DEEP_SPELL_FIRST     30
#define MW_DEEP_SPELL_COUNT     15
#define MW_DEEP_MAGIC_MARKER   0xD5u
#define MW_FINAL_GEAR_QUEST_FLAG ((u16)(1u << 14))
#define MW_UNIVERSAL_ACCESS_FLAG ((u16)(1u << 15))
#define MW_PLAYER_LEVEL_MAX    3000u
#define MW_PLAYER_STAT_MAX     INT16_MAX
#define MW_PLAYER_SP_MAX_U32   4294967040u
#define MW_PLAYER_SP_MAX       ((float)MW_PLAYER_SP_MAX_U32)
#define MW_PLAYER_HP_MAX       UINT32_MAX
/* WORLD stores character age in minutes.  Character creation writes whole
   years with this exact DOS constant; resting and magic add smaller minute
   intervals to the same counter. */
#define MW_AGE_DAY_UNITS       1440u
#define MW_AGE_YEAR_UNITS    525600u

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
    /* Native Enhanced equipment lives in slots outside WORLD's original
       eight-item arrays.  Keeping it here preserves the exact 0x928-byte
       character file while allowing the late dungeon to add real gear. */
    s16 enhanced_weapon_enchant[2]; /* physical weapon slots 12 and 13 */
    s16 enhanced_armor_enchant[2];  /* armor slots 8 and 9 */
    u8  enhanced_weapon_inventory[2];
    u8  enhanced_armor_inventory[2];
    /* Enhanced-only deep relics.  V3 left these nine bytes reserved, so V4
       can add the system without changing the original 0x928-byte save. */
    u8  deep_magic_marker;
    u8  relic_arcane_ring;
    u8  relic_bloodstone_signet;
    u8  relic_deepward_amulet;
    u8  relic_sage_prism;
    u8  relic_phoenix_seal;
    u8  relic_regen_phase;
    u16 relic_phoenix_cooldown;
    /* V5 appends the floor-950 final gear instead of resizing the established
       V3 arrays above, preserving every V4 field offset during migration. */
    s16 final_weapon_enchant;       /* physical weapon slot 14 */
    s16 final_armor_enchant;        /* armor slot 10 */
    u8  final_weapon_inventory;
    u8  final_armor_inventory;
    /* V6 widens Enhanced health and fills the second equipment page.
       Slots 15-19 and 11-15 are the five appended weapon/armor records. */
    u32 hp_cur_wide;
    u32 hp_max_wide;
    s16 late_weapon_enchant[5];
    s16 late_armor_enchant[5];
    u8  late_weapon_inventory[5];
    u8  late_armor_inventory[5];
    u16 deep_spell_unlocks;
    u8  late_gear_unlocks;
} NativeCharacterExtension;
#pragma pack(pop)

_Static_assert(sizeof(NativeCharacterExtension) == 127,
               "native character extension layout changed");

/* Character data structure - 2344 bytes total (0x928)
 * Layout reverse-engineered from WORLD.EXE disassembly.
 * Save files are a raw dump of this struct. */
#pragma pack(push, 1)
typedef struct {
    char name[20];                  /* 0x000 - player name (null-terminated) */
    char _pad_014[20];              /* 0x014 */
    u8   race;                      /* 0x028 - original 8 races + 2 Enhanced races */
    u8   sex;                       /* 0x029 - 0=Male, 1=Female */
    u8   class_id;                  /* 0x02A - original 7 classes + 2 Enhanced classes */
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

/* Slots 30-44 existed in the DOS record but were outside its visible
 * 30-spell catalogs.  In particular, old Monk creation filled all 45 bytes
 * with one.  Clear those formerly-unused bytes exactly once, then reconstruct
 * legitimate Enhanced spellbook unlocks from the native tier mask. */
static inline void mw_character_deep_magic_ensure(Character *p) {
    if (!p || !mw_character_native_valid(p) ||
        p->native.deep_magic_marker == MW_DEEP_MAGIC_MARKER)
        return;
    for (int category = 0; category < 4; category++) {
        memset(&p->spells[category][MW_DEEP_SPELL_FIRST], 0,
               sizeof(p->spells[category]) - MW_DEEP_SPELL_FIRST);
        memset(&p->scrolls[category][MW_DEEP_SPELL_FIRST], 0,
               sizeof(p->scrolls[category]) - MW_DEEP_SPELL_FIRST);
        memset(&p->wands[category][MW_DEEP_SPELL_FIRST], 0,
               sizeof(p->wands[category]) - MW_DEEP_SPELL_FIRST);
        memset(&p->papers[category][MW_DEEP_SPELL_FIRST], 0,
               sizeof(p->papers[category]) - MW_DEEP_SPELL_FIRST);
    }
    if (p->native.experience_mode != MW_EXPERIENCE_CLASSIC) {
        for (int deep = 0; deep < MW_DEEP_SPELL_COUNT; deep++)
            if (p->native.deep_spell_unlocks & (1u << deep))
                for (int category = 0; category < 4; category++)
                    p->spells[category][MW_DEEP_SPELL_FIRST + deep] = 1;
    }
    p->native.deep_magic_marker = MW_DEEP_MAGIC_MARKER;
}

/* Import the byte-sized original values once.  This also converts legacy
 * saves automatically the first time the native port loads/saves them. */
static inline void mw_character_native_ensure(Character *p) {
    if (!p) return;
    if (mw_character_native_valid(p)) {
        mw_character_deep_magic_ensure(p);
        return;
    }
    NativeCharacterExtension ext = {0};
    int native_v2 = p->native.magic == MW_NATIVE_CHARACTER_MAGIC &&
                    p->native.version == 2u;
    int native_v3 = p->native.magic == MW_NATIVE_CHARACTER_MAGIC &&
                    p->native.version == 3u;
    int native_v4 = p->native.magic == MW_NATIVE_CHARACTER_MAGIC &&
                    p->native.version == 4u;
    int native_v5 = p->native.magic == MW_NATIVE_CHARACTER_MAGIC &&
                    p->native.version == 5u;
    if (native_v2 || native_v3 || native_v4 || native_v5) {
        /* V3 assigned equipment fields that were reserved in V2, V4 assigned
           relics, and V5 appends final gear. Preserve fields established by
           each source version and clear only bytes that were still unused. */
        ext = p->native;
        if (native_v2) {
            memset(ext.enhanced_weapon_enchant, 0,
                   sizeof(ext.enhanced_weapon_enchant));
            memset(ext.enhanced_armor_enchant, 0,
                   sizeof(ext.enhanced_armor_enchant));
            memset(ext.enhanced_weapon_inventory, 0,
                   sizeof(ext.enhanced_weapon_inventory));
            memset(ext.enhanced_armor_inventory, 0,
                   sizeof(ext.enhanced_armor_inventory));
            ext.deep_magic_marker = 0;
        }
        if (native_v2 || native_v3) {
            ext.relic_arcane_ring = 0;
            ext.relic_bloodstone_signet = 0;
            ext.relic_deepward_amulet = 0;
            ext.relic_sage_prism = 0;
            ext.relic_phoenix_seal = 0;
            ext.relic_regen_phase = 0;
            ext.relic_phoenix_cooldown = 0;
        }
        if (!native_v5) {
            ext.final_weapon_enchant = 0;
            ext.final_armor_enchant = 0;
            ext.final_weapon_inventory = 0;
            ext.final_armor_inventory = 0;
            /* Bit 14 had no meaning before V5. */
            ext.quest_flags &= (u16)~MW_FINAL_GEAR_QUEST_FLAG;
        }
        if (native_v2 && ext.experience_mode != MW_EXPERIENCE_CLASSIC) {
            if (ext.quest_flags & (1u << 9)) {
                ext.enhanced_weapon_inventory[0] = 1;
                ext.enhanced_armor_inventory[0] = 1;
            }
            if (ext.quest_flags & (1u << 12)) {
                ext.enhanced_weapon_inventory[1] = 1;
                ext.enhanced_armor_inventory[1] = 1;
            }
        }

        /* V6 reorders the old three-item late set into an eight-step curve.
           Preserve V5 Eternity gear at slot 16 and Ascendant gear at slot 18,
           then clear their former storage for the new earlier tiers. */
        s16 old_eternity_weapon = ext.enhanced_weapon_enchant[1];
        s16 old_eternity_armor = ext.enhanced_armor_enchant[1];
        u8 old_eternity_weapon_count = ext.enhanced_weapon_inventory[1];
        u8 old_eternity_armor_count = ext.enhanced_armor_inventory[1];
        s16 old_ascendant_weapon = ext.final_weapon_enchant;
        s16 old_ascendant_armor = ext.final_armor_enchant;
        u8 old_ascendant_weapon_count = ext.final_weapon_inventory;
        u8 old_ascendant_armor_count = ext.final_armor_inventory;
        memset(ext.late_weapon_enchant, 0,
               sizeof(ext.late_weapon_enchant));
        memset(ext.late_armor_enchant, 0,
               sizeof(ext.late_armor_enchant));
        memset(ext.late_weapon_inventory, 0,
               sizeof(ext.late_weapon_inventory));
        memset(ext.late_armor_inventory, 0,
               sizeof(ext.late_armor_inventory));
        ext.enhanced_weapon_enchant[1] = 0;
        ext.enhanced_armor_enchant[1] = 0;
        ext.enhanced_weapon_inventory[1] = 0;
        ext.enhanced_armor_inventory[1] = 0;
        ext.final_weapon_enchant = 0;
        ext.final_armor_enchant = 0;
        ext.final_weapon_inventory = 0;
        ext.final_armor_inventory = 0;
        ext.late_weapon_enchant[1] = old_eternity_weapon;
        ext.late_armor_enchant[1] = old_eternity_armor;
        ext.late_weapon_inventory[1] = old_eternity_weapon_count;
        ext.late_armor_inventory[1] = old_eternity_armor_count;
        ext.late_weapon_enchant[3] = old_ascendant_weapon;
        ext.late_armor_enchant[3] = old_ascendant_armor;
        ext.late_weapon_inventory[3] = old_ascendant_weapon_count;
        ext.late_armor_inventory[3] = old_ascendant_armor_count;

        ext.hp_cur_wide = p->hp_cur;
        ext.hp_max_wide = p->hp_max;
        ext.deep_spell_unlocks = 0;
        for (int deep = 0; deep < 5; deep++)
            if (ext.quest_flags & (1u << (8 + deep)))
                ext.deep_spell_unlocks |= (u16)(1u << deep);
        ext.late_gear_unlocks = 0;
        if (ext.enhanced_weapon_inventory[0] ||
            ext.enhanced_armor_inventory[0])
            ext.late_gear_unlocks |= 1u << 0;
        if (old_eternity_weapon_count || old_eternity_armor_count)
            ext.late_gear_unlocks |= 1u << 4;
        if (old_ascendant_weapon_count || old_ascendant_armor_count ||
            (ext.quest_flags & MW_FINAL_GEAR_QUEST_FLAG))
            ext.late_gear_unlocks |= 1u << 6;
        ext.version = MW_NATIVE_CHARACTER_VERSION;
        p->native = ext;
        mw_character_deep_magic_ensure(p);
        return;
    }
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
    ext.hp_cur_wide = p->hp_cur;
    ext.hp_max_wide = p->hp_max;
    for (int deep = 0; deep < 5; deep++)
        if (ext.quest_flags & (1u << (8 + deep)))
            ext.deep_spell_unlocks |= (u16)(1u << deep);
    p->native = ext;
    mw_character_deep_magic_ensure(p);
}

static inline s16 mw_weapon_enchant(const Character *p, int slot) {
    if (!p || slot < 0) return 0;
    if (slot < 12)
        return mw_character_native_valid(p) ?
               p->native.weapon_enchant[slot] :
               (s8)p->eq_wep_enchant[slot];
    if (slot < 14 && mw_character_native_valid(p))
        return p->native.enhanced_weapon_enchant[slot - 12];
    if (slot == 14 && mw_character_native_valid(p))
        return p->native.final_weapon_enchant;
    if (slot >= 15 && slot < 20 && mw_character_native_valid(p))
        return p->native.late_weapon_enchant[slot - 15];
    return 0;
}

static inline void mw_set_weapon_enchant(Character *p, int slot, int value) {
    if (!p || slot < 0 || slot >= 20) return;
    if (value > INT16_MAX) value = INT16_MAX;
    if (value < INT16_MIN) value = INT16_MIN;
    mw_character_native_ensure(p);
    if (slot < 12) {
        p->native.weapon_enchant[slot] = (s16)value;
        p->eq_wep_enchant[slot] = (u8)(value > 127 ? 127 :
                                          value < -128 ? -128 : (s8)value);
    } else if (slot < 14) {
        p->native.enhanced_weapon_enchant[slot - 12] = (s16)value;
    } else if (slot == 14) {
        p->native.final_weapon_enchant = (s16)value;
    } else {
        p->native.late_weapon_enchant[slot - 15] = (s16)value;
    }
}

static inline s16 mw_armor_enchant(const Character *p, int slot) {
    if (!p || slot < 0) return 0;
    if (slot < 8)
        return mw_character_native_valid(p) ?
               p->native.armor_enchant[slot] :
               (s8)p->armor_enchant[slot];
    if (slot < 10 && mw_character_native_valid(p))
        return p->native.enhanced_armor_enchant[slot - 8];
    if (slot == 10 && mw_character_native_valid(p))
        return p->native.final_armor_enchant;
    if (slot >= 11 && slot < 16 && mw_character_native_valid(p))
        return p->native.late_armor_enchant[slot - 11];
    return 0;
}

static inline void mw_set_armor_enchant(Character *p, int slot, int value) {
    if (!p || slot < 0 || slot >= 16) return;
    if (value > INT16_MAX) value = INT16_MAX;
    if (value < INT16_MIN) value = INT16_MIN;
    mw_character_native_ensure(p);
    if (slot < 8) {
        p->native.armor_enchant[slot] = (s16)value;
        p->armor_enchant[slot] = (u8)(value > 127 ? 127 :
                                        value < -128 ? -128 : (s8)value);
    } else if (slot < 10) {
        p->native.enhanced_armor_enchant[slot - 8] = (s16)value;
    } else if (slot == 10) {
        p->native.final_armor_enchant = (s16)value;
    } else {
        p->native.late_armor_enchant[slot - 11] = (s16)value;
    }
}

/* Inventory accessors keep callers from indexing past WORLD's original
   eight-byte arrays when selecting, dropping, weighing, or dissolving native
   Enhanced gear. */
static inline int mw_weapon_inventory_count(const Character *p, int slot) {
    if (!p || slot < 0) return 0;
    if (slot < 8) return p->weapon_inventory[slot];
    if (slot >= 12 && slot < 14 && mw_character_native_valid(p))
        return p->native.enhanced_weapon_inventory[slot - 12];
    if (slot == 14 && mw_character_native_valid(p))
        return p->native.final_weapon_inventory;
    if (slot >= 15 && slot < 20 && mw_character_native_valid(p))
        return p->native.late_weapon_inventory[slot - 15];
    return 0;
}

static inline void mw_set_weapon_inventory_count(Character *p, int slot,
                                                  int count) {
    if (!p || slot < 0) return;
    if (count < 0) count = 0;
    if (count > 255) count = 255;
    if (slot < 8) {
        p->weapon_inventory[slot] = (u8)count;
    } else if (slot >= 12 && slot < 14) {
        mw_character_native_ensure(p);
        p->native.enhanced_weapon_inventory[slot - 12] = (u8)count;
    } else if (slot == 14) {
        mw_character_native_ensure(p);
        p->native.final_weapon_inventory = (u8)count;
    } else if (slot >= 15 && slot < 20) {
        mw_character_native_ensure(p);
        p->native.late_weapon_inventory[slot - 15] = (u8)count;
    }
}

static inline int mw_armor_inventory_count(const Character *p, int slot) {
    if (!p || slot < 0) return 0;
    if (slot < 8) return p->armor_inventory[slot];
    if (slot < 10 && mw_character_native_valid(p))
        return p->native.enhanced_armor_inventory[slot - 8];
    if (slot == 10 && mw_character_native_valid(p))
        return p->native.final_armor_inventory;
    if (slot >= 11 && slot < 16 && mw_character_native_valid(p))
        return p->native.late_armor_inventory[slot - 11];
    return 0;
}

static inline void mw_set_armor_inventory_count(Character *p, int slot,
                                                 int count) {
    if (!p || slot < 0) return;
    if (count < 0) count = 0;
    if (count > 255) count = 255;
    if (slot < 8) {
        p->armor_inventory[slot] = (u8)count;
    } else if (slot < 10) {
        mw_character_native_ensure(p);
        p->native.enhanced_armor_inventory[slot - 8] = (u8)count;
    } else if (slot == 10) {
        mw_character_native_ensure(p);
        p->native.final_armor_inventory = (u8)count;
    } else if (slot >= 11 && slot < 16) {
        mw_character_native_ensure(p);
        p->native.late_armor_inventory[slot - 11] = (u8)count;
    }
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

/* The native max-character shortcut persists its class-restriction bypass in
 * the only unused quest bit.  The bit grants access, but callers must still
 * enforce catalog bounds and Classic/Enhanced content boundaries. */
static inline int mw_universal_access(const Character *p) {
    return p && (mw_quest_flags(p) & MW_UNIVERSAL_ACCESS_FLAG) != 0;
}

/* Existing native/legacy saves default to Enhanced because the formerly
 * reserved byte is zero.  Only newly-created characters explicitly choosing
 * Classic receive the nonzero marker. */
static inline int mw_experience_mode(const Character *p) {
    return mw_character_native_valid(p) &&
           p->native.experience_mode == MW_EXPERIENCE_CLASSIC ?
           MW_EXPERIENCE_CLASSIC : MW_EXPERIENCE_ENHANCED;
}

static inline int mw_spell_catalog_count(const Character *p) {
    return mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED ?
           MW_ENHANCED_SPELL_COUNT : MW_ORIGINAL_SPELL_COUNT;
}

static inline u32 mw_player_hp_cap(const Character *p) {
    return mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED ?
           MW_PLAYER_HP_MAX : (u32)INT16_MAX;
}

static inline u16 mw_effect_turn_cap(const Character *p) {
    return mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED ?
           UINT16_MAX : (u16)INT16_MAX;
}

static inline u32 mw_hp_cur(const Character *p) {
    if (!p) return 0;
    return mw_character_native_valid(p) ?
           p->native.hp_cur_wide : p->hp_cur;
}

static inline u32 mw_hp_max(const Character *p) {
    if (!p) return 0;
    return mw_character_native_valid(p) ?
           p->native.hp_max_wide : p->hp_max;
}

static inline void mw_set_hp_cur(Character *p, uint64_t value) {
    if (!p) return;
    uint64_t cap = mw_player_hp_cap(p);
    if (value > cap) value = cap;
    mw_character_native_ensure(p);
    p->native.hp_cur_wide = (u32)value;
    p->hp_cur = (u16)(value > UINT16_MAX ? UINT16_MAX : value);
}

static inline void mw_set_hp_max(Character *p, uint64_t value) {
    if (!p) return;
    uint64_t cap = mw_player_hp_cap(p);
    if (value > cap) value = cap;
    mw_character_native_ensure(p);
    p->native.hp_max_wide = (u32)value;
    p->hp_max = (u16)(value > UINT16_MAX ? UINT16_MAX : value);
    if (mw_hp_cur(p) > value) mw_set_hp_cur(p, value);
}

static inline u16 mw_deep_spell_unlocks(const Character *p) {
    return p && mw_character_native_valid(p) ?
           p->native.deep_spell_unlocks : 0;
}

static inline void mw_unlock_deep_spell_tier(Character *p, int deep) {
    if (!p || deep < 0 || deep >= MW_DEEP_SPELL_COUNT ||
        mw_experience_mode(p) != MW_EXPERIENCE_ENHANCED)
        return;
    mw_character_native_ensure(p);
    p->native.deep_spell_unlocks |= (u16)(1u << deep);
    for (int category = 0; category < 4; category++)
        p->spells[category][MW_DEEP_SPELL_FIRST + deep] = 1;
}

static inline int mw_late_gear_unlocked(const Character *p, int tier) {
    return p && tier >= 0 && tier < 8 &&
           mw_character_native_valid(p) &&
           (p->native.late_gear_unlocks & (1u << tier)) != 0;
}

static inline void mw_set_late_gear_unlocked(Character *p, int tier) {
    if (!p || tier < 0 || tier >= 8) return;
    mw_character_native_ensure(p);
    p->native.late_gear_unlocks |= (u8)(1u << tier);
}

static inline void mw_set_experience_mode(Character *p, int mode) {
    if (!p) return;
    mw_character_native_ensure(p);
    p->native.experience_mode = mode == MW_EXPERIENCE_CLASSIC ?
                                MW_EXPERIENCE_CLASSIC :
                                MW_EXPERIENCE_ENHANCED;
}

enum {
    MW_RELIC_ARCANE_RING = 0,
    MW_RELIC_BLOODSTONE_SIGNET,
    MW_RELIC_DEEPWARD_AMULET,
    MW_RELIC_SAGE_PRISM,
    MW_RELIC_PHOENIX_SEAL,
    MW_RELIC_COUNT
};

/* Relics remain serialized if a save is inspected in Classic mode, but they
 * are deliberately inert and hidden there.  Experience mode is immutable in
 * normal play, so this chiefly protects converted/test saves. */
static inline int mw_relic_owned(const Character *p, int relic) {
    if (!p || mw_experience_mode(p) != MW_EXPERIENCE_ENHANCED ||
        !mw_character_native_valid(p))
        return 0;
    switch (relic) {
    case MW_RELIC_ARCANE_RING: return p->native.relic_arcane_ring != 0;
    case MW_RELIC_BLOODSTONE_SIGNET:
        return p->native.relic_bloodstone_signet != 0;
    case MW_RELIC_DEEPWARD_AMULET:
        return p->native.relic_deepward_amulet != 0;
    case MW_RELIC_SAGE_PRISM: return p->native.relic_sage_prism != 0;
    case MW_RELIC_PHOENIX_SEAL: return p->native.relic_phoenix_seal != 0;
    default: return 0;
    }
}

static inline void mw_set_relic_owned(Character *p, int relic, int owned) {
    if (!p || relic < 0 || relic >= MW_RELIC_COUNT) return;
    mw_character_native_ensure(p);
    u8 value = owned ? 1u : 0u;
    switch (relic) {
    case MW_RELIC_ARCANE_RING: p->native.relic_arcane_ring = value; break;
    case MW_RELIC_BLOODSTONE_SIGNET:
        p->native.relic_bloodstone_signet = value; break;
    case MW_RELIC_DEEPWARD_AMULET:
        p->native.relic_deepward_amulet = value; break;
    case MW_RELIC_SAGE_PRISM: p->native.relic_sage_prism = value; break;
    case MW_RELIC_PHOENIX_SEAL:
        p->native.relic_phoenix_seal = value;
        if (!value) p->native.relic_phoenix_cooldown = 0;
        break;
    default: break;
    }
}

static inline int mw_relic_count(const Character *p) {
    int count = 0;
    for (int relic = 0; relic < MW_RELIC_COUNT; relic++)
        count += mw_relic_owned(p, relic);
    return count;
}

/* Race IDs */
enum {
    RACE_HUMAN = 0, RACE_ELF, RACE_DWARF, RACE_HOBBIT,
    RACE_GNOME, RACE_OGRE, RACE_SPRITE, RACE_IMP,
    RACE_DRAGONKIN, RACE_CELESTIAL, RACE_COUNT
};
#define MW_CLASSIC_RACE_COUNT 8

/* Class IDs */
enum {
    CLASS_FIGHTER = 0, CLASS_WORSHIPPER, CLASS_MONK, CLASS_WIZARD,
    CLASS_PRIEST, CLASS_SAGE, CLASS_MAGE,
    CLASS_SPELLBLADE, CLASS_PALADIN, CLASS_COUNT
};
#define MW_CLASSIC_CLASS_COUNT 7

static const char *race_names[] = {
    "HUMAN", "ELF", "DWARF", "HOBBIT", "GNOME", "OGRE", "SPRITE", "IMP",
    "DRAGONKIN", "CELESTIAL"
};

static const char *class_names[] = {
    "FIGHTER", "WORSHIPPER", "MONK", "WIZARD", "PRIEST", "SAGE", "MAGE",
    "SPELLBLADE", "PALADIN"
};

/* Max players/save slots */
#define MAX_PLAYERS     10
#define MAX_DUNGEON_FLOOR  1000
#define MAX_DUNGEON_FLOORS (MAX_DUNGEON_FLOOR + 1)
#define CLASSIC_DUNGEON_FLOOR 250
#define SPELLS_PER_TYPE 45
#define SPELL_TYPES     4

#endif /* MW_TYPES_H */
