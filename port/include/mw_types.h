#ifndef MW_TYPES_H
#define MW_TYPES_H

#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;

/* Character data structure - 2344 bytes total (0x928)
 * Layout reverse-engineered from WORLD.EXE disassembly.
 * Save files are a raw dump of this struct. */
#pragma pack(push, 1)
typedef struct {
    char name[20];                  /* 0x000 - player name (null-terminated) */
    char _pad_014[20];              /* 0x014 */
    u8   race;                      /* 0x028 - 0=Human,1=Dwarf,2=Hobbit,3=Elf,4=Gnome,5=Half-Elf,6=Half-Orc */
    u8   sex;                       /* 0x029 - 0=Male, 1=Female */
    u8   class_id;                  /* 0x02A - 0=Fighter,1=Worshipper,2=Monk,3=Wizard,4=Priest,5=Sage,6=Mage */
    char _pad_02B[6];               /* 0x02B */
    u16  hp_cur;                    /* 0x031 */
    u16  hp_max;                    /* 0x033 */
    float sp_cur;                   /* 0x035 - spell points (float32) */
    float sp_max;                   /* 0x039 */
    char _pad_03D[68];              /* 0x03D - 0x080 */
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
    char _pad_7AA[2];               /* 0x7AA */
    u16  x_pos;                     /* 0x7AC */
    u16  y_pos;                     /* 0x7AE */
    u16  floor_depth;               /* 0x7B0 */
    char _pad_7B2[20];              /* 0x7B2 */
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
    char _pad_7FF[7];               /* 0x7FF */
    u16  raise_contract;            /* 0x806 */
    char _pad_808[6];               /* 0x808 */
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
    char _pad_81E[40];              /* 0x81E */
    u8   gauntlet;                  /* 0x846 */
    char _pad_847[225];             /* 0x847 - pad to 0x928 */
} Character;
#pragma pack(pop)

_Static_assert(sizeof(Character) == 0x928, "Character struct must be 2344 bytes");

/* Race IDs */
enum {
    RACE_HUMAN = 0, RACE_DWARF, RACE_HOBBIT, RACE_ELF,
    RACE_GNOME, RACE_HALFELF, RACE_HALFORC, RACE_COUNT
};

/* Class IDs */
enum {
    CLASS_FIGHTER = 0, CLASS_WORSHIPPER, CLASS_MONK, CLASS_WIZARD,
    CLASS_PRIEST, CLASS_SAGE, CLASS_MAGE, CLASS_COUNT
};

static const char *race_names[] = {
    "HUMAN", "DWARF", "HOBBIT", "ELF", "GNOME", "HALF-ELF", "HALF-ORC"
};

static const char *class_names[] = {
    "FIGHTER", "WORSHIPPER", "MONK", "WIZARD", "PRIEST", "SAGE", "MAGE"
};

/* Max players/save slots */
#define MAX_PLAYERS     10
#define MAX_DUNGEON_FLOORS 200
#define SPELLS_PER_TYPE 45
#define SPELL_TYPES     4

#endif /* MW_TYPES_H */
