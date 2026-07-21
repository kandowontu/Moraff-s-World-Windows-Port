#include "mw_combat.h"
#include "mw_game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ══════════════════════════════════════════════════════════════════════
   Weapon stats table — extracted from WORLD.EXE DS:0x1C0 (7 bytes/entry)
   Fields: maxDmg, hit bonus, speed, weight
   ══════════════════════════════════════════════════════════════════════ */

const WeaponStats weapon_stats[WEAPON_STAT_COUNT] = {
    { "FIST",             2,   0,  6,   0 },
    { "STICK",            4,   1,  9,   4 },
    { "CLUB",             7,   2,  13,  7 },
    { "MACE",             12,  3,  18,  11 },
    { "KNIFE",            3,   0,  8,   1 },
    { "SHORTSWORD",       5,   1,  11,  3 },
    { "LONG SWORD",       9,   2,  16,  6 },
    { "GREAT SWORD",      19,  3,  25,  15 },
    { "POWER WPN (BASE)", 69,  4,  8,   0 },
    { "POWER WEAPON I",   129, 6,  8,   0 },
    { "POWER WEAPON II",  199, 10, 8,   0 },
    { "POWER WEAPON III", 399, 20, 8,   0 },
};

/* ══════════════════════════════════════════════════════════════════════
   Monster type table — 112 entries from WORLD.EXE DS:0x237 (35 bytes/entry)
   Data verified against disassembly and reference guide.
   ══════════════════════════════════════════════════════════════════════ */

const MonsterType monster_types[MONSTER_TYPE_COUNT] = {
    /* 0-8: Humanoids */
    {"Ogre",             14,15,18, 9,11,10, 99, 0,127, 0, 10,9},
    {"Werewolf",         14,10,15, 9,10,10, 99, 0,127, 0, 9,8},
    {"Walking Sword",    13, 6,10, 8, 9, 6, 99, 0,127, 0, 7,7},
    {"Kobald",           13, 6,11, 8, 9, 7, 99, 0,127, 0, 7,7},
    {"Orc",              13, 8,13, 9, 9, 9, 99, 0,127, 0, 7,7},
    {"Dwarf",            16, 8,14,13, 8, 8, 99, 0,127, 0, 7,9},
    {"Hobbit",           16, 8, 9,18,14, 8, 99, 0,127, 0, 10,10},
    {"Armored Fighter",  18,10,15,12,11,12, 99, 0,127, 0, 9,9},
    {"Ape",              10, 6,12, 9,11, 6, 99, 0,127, 0, 5,4},
    /* 9: Boss - Moraff */
    {"Moraff",           90,100,40,40,40,100,99,120,90, 0, 40,40},
    /* 10-13: Misc creatures */
    {"Cat Head",         11, 0, 6, 9,16, 1, 99, 0,127, 0, 14,13},
    {"Pixie",            12, 4, 6,15,16, 4, 99, 0, 28, 0, 14,11},
    {"Leprechuan",       11, 4, 6,15,16, 5, 99, 45,127, 0, 14,11},
    {"Unicorn",          12,10,15,10,15, 8, 99, 25,127, 0, 15,15},
    /* 14-15: Gods/Titans */
    {"Zeus",             35,70,99,58,48,110,100,100,127, 0, 50,45},
    {"Titan",            25,50,35, 7,14,20, 99,100,127, 0, 20,18},
    /* 16-17: Giants/Dogs */
    {"Stone Giant",      20,35,28, 7,11,16, 99, 80,127, 0, 14,12},
    {"Pit Bull",         14, 7,10,13,11, 4, 99, 50,127, 0, 5,4},
    /* 18-19: Misc */
    {"Troll",            12, 7,11,12,11, 6, 99, 5, 50, 0, 13,9},
    {"Garbage Can",      30, 1,12, 5,11,100,99, 5,127, 0, 3,3},
    /* 20-23: Mid-tier */
    {"Goblin",           22,20,15,10,11,10, 99, 40, 80, 0, 7,7},
    {"Foot Stomper",      6,30,19, 7,11, 8, 99, 70,127, 0, 6,6},
    {"Flesh Eater",      22,30,15, 9,11,10, 99, 65,127, 0, 8,8},
    {"Face of Death",    13, 0, 7,10,11, 4, 99, 90,127, 0, 10,9},
    /* 24-25: Eyes/Gargoyles */
    {"Floating Eye",     14,10,11,11,11, 8, 99, 30, 80, 0, 10,9},
    {"Gargoyle",         19, 8,14,12,11,10, 99, 25,127, 0, 10,9},
    /* 26-29: Undead */
    {"Skeleton",         11, 8, 9, 9, 9, 8, 99, 8, 60, 0, 3,3},
    {"Zombie",            9, 8,11, 9, 9, 8, 99, 11, 80, 0, 3,3},
    {"Ghoul",            12, 9, 9,10, 9, 6, 99, 14,100, 0, 10,9},
    {"Wraith",           14, 6, 9, 9, 9, 6, 99, 17,110, 0, 13,9},
    /* 30-35: More undead + demons */
    {"Mummy",             8,10,16, 3, 9,10, 99, 19, 90, 0, 6,6},
    {"Specter",          14, 8,10,13, 9, 7, 99, 25,127, 0, 15,12},
    {"Medusa",           14,10,11,13, 9, 8, 99, 40,127, 0, 13,10},
    {"Vampire",          18,12,14,14, 9,10, 99, 55,127, 0, 17,12},
    {"Demon",            20,12,20,18,13,12, 99, 88,127, 0, 18,16},
    {"Devil",            30,30,90,68,46,90,100,119,127, 0, 62,74},
    /* 36-42: Yellow animals */
    {"Yellow Bat",       11, 7, 6,11,11, 5, 99, 0,127, 0, 10,8},
    {"Yellow Rat",       11, 6, 7,13,11, 5, 99, 0,127, 0, 3,3},
    {"Yellow Ant",       14, 6, 7,13,11, 5, 99, 0,127, 0, 3,3},
    {"Yellow Spider",    11, 6, 7,12,11, 5, 99, 0,127, 0, 3,3},
    {"Yellow Scorpion",  13, 7, 7,11,11, 5, 99, 0,127, 0, 3,3},
    {"Yellow Centipede",  6, 6, 7, 9,11, 5, 99, 0,127, 0, 3,3},
    {"Yellow Giant Toad", 9, 8, 6, 8,11, 5, 99, 0,127, 0, 3,3},
    /* 43-49: Black animals */
    {"Black Bat",        11, 7, 6,11,11, 5, 1, 7,127, 0, 10,8},
    {"Black Rat",        11, 6, 7,13,11, 5, 1, 7,127, 0, 3,3},
    {"Black Ant",        14, 6, 7,13,11, 5, 1, 7,127, 0, 3,3},
    {"Black Spider",     11, 6, 7,12,11, 5, 1, 7,127, 0, 3,3},
    {"Black Scorpion",   13, 7, 7,11,11, 5, 1, 7,127, 0, 3,3},
    {"Black Centipede",   6, 6, 7, 9,11, 5, 1, 7,127, 0, 3,3},
    {"Black Giant Toad",  9, 8, 6, 8,11, 5, 1, 7,127, 0, 3,3},
    /* 50-56: Green animals */
    {"Green Bat",        11, 7, 6,11,11, 5, 2,11,127, 0, 10,8},
    {"Green Rat",        11, 6, 7,13,11, 5, 2,11,127, 0, 3,3},
    {"Green Ant",        14, 6, 7,13,11, 5, 2,11,127, 0, 3,3},
    {"Green Spider",     11, 6, 7,12,11, 5, 2,11,127, 0, 3,3},
    {"Green Scorpion",   13, 7, 7,11,11, 5, 2,11,127, 0, 3,3},
    {"Green Centipede",   6, 6, 7, 9,11, 5, 2,11,127, 0, 3,3},
    {"Green Giant Toad",  9, 8, 6, 8,11, 5, 2,11,127, 0, 3,3},
    /* 57-71: Balls (all same stats as Garbage Can) */
    {"Dark Blue Ball",   30, 1,12, 5,11,100, 99, 0,127, 0, 3,3},
    {"Giant Blue Ball",  30, 1,12, 5,11,100, 99, 0,127, 0, 3,3},
    {"Light Blue Ball",  30, 1,12, 5,11,100, 99, 0,127, 0, 3,3},
    {"Yellow Ball",      30, 1,12, 5,11,100, 99, 0,127, 0, 3,3},
    {"Orange Ball",      30, 1,12, 5,11,100, 99, 0,127, 0, 3,3},
    {"Light Red Ball",   30, 1,12, 5,11,100, 99, 0,127, 0, 3,3},
    {"Brown Ball",       30, 1,12, 5,11,100, 99, 0,127, 0, 3,3},
    {"Lt Green Ball",    30, 1,12, 5,11,100, 99, 0,127, 0, 3,3},
    {"Dk Green Ball",    30, 1,12, 5,11,100, 99, 0,127, 0, 3,3},
    {"Red Ball",         30, 1,12, 5,11,100, 99, 0,127, 0, 3,3},
    {"Dk Green Ball 2",  30, 1,12, 5,11,100, 99, 0,127, 0, 3,3},
    {"Dark Red Ball",    30, 1,12, 5,11,100, 99, 0,127, 0, 3,3},
    {"Dark Gray Ball",   30, 1,12, 5,11,100, 99, 0,127, 0, 3,3},
    {"Gray Ball",        30, 1,12, 5,11,100, 99, 0,127, 0, 3,3},
    {"White Ball",       30, 1,12, 5,11,100, 99, 0,127, 0, 3,3},
    /* 72-83: Puffballs */
    {"Lt Blue Puffball",  9, 0, 5, 9,10, 1, 6, 0,127, 0, 7,6},
    {"Lt Red Puffball",   9, 0, 5, 9,10, 1, 6, 0,127, 0, 7,6},
    {"Lt Green Puffball", 9, 0, 5, 9,10, 1, 6, 0,127, 0, 7,6},
    {"Yellow Puffball",   9, 0, 5, 9,10, 1, 6, 0,127, 0, 7,6},
    {"White Puffball",    9, 0, 5, 9,10, 1, 6, 0,127, 0, 7,6},
    {"Gray Puffball",     9, 0, 5, 9,10, 1, 6, 0,127, 0, 7,6},
    {"Sky Blue Puffball", 9, 0, 5, 9,10, 1, 6, 0,127, 0, 7,6},
    {"Dk Red Puffball",   9, 0, 5, 9,10, 1, 6, 0,127, 0, 7,6},
    {"Dk Green Puffball", 9, 0, 5, 9,10, 1, 6, 0,127, 0, 7,6},
    {"Brown Puffball",    9, 0, 5, 9,10, 1, 6, 0,127, 0, 7,6},
    {"Black Puffball",    9, 0, 5, 9,10, 1, 6, 0,127, 0, 7,6},
    {"Dk Gray Puffball",  9, 0, 5, 9,10, 1, 6, 0,127, 0, 7,6},
    /* 84-87: Orange Dragons */
    {"Orange Dragonfly",  14,10, 9,13,12, 6, 99, 2, 25, 0, 11,9},
    {"Orange Minidragon", 17,25,14,11,13, 9, 99, 8, 50, 0, 14,12},
    {"Orange Dragon",     20,40,20, 9,14,17, 99, 20,100, 0, 17,15},
    {"Orange Dragonking", 35,75,30, 7,15,25, 99, 80,127, 0, 20,18},
    /* 88-91: Blue Dragons */
    {"Blue Dragonfly",   14,10, 9,13,12, 6, 99, 2, 25, 0, 11,9},
    {"Blue Mini-Dragon", 17,25,14,11,13, 9, 99, 8, 50, 0, 14,12},
    {"Blue Dragon",      20,40,20, 9,14,17, 99, 20,100, 0, 17,15},
    {"Blue Dragon King", 35,75,30, 7,15,25, 99, 80,127, 0, 20,18},
    /* 92-95: White Dragons */
    {"White Dragonfly",  14,10, 9,13,12, 6, 99, 2, 25, 0, 11,9},
    {"White Mini-Dragon",17,25,14,11,13, 9, 99, 8, 50, 0, 14,12},
    {"White Dragon",     20,40,20, 9,14,17, 99, 20,100, 0, 17,15},
    {"White Dragon King",35,75,30, 7,15,25, 99, 80,127, 0, 20,18},
    /* 96-99: Green Dragons */
    {"Green Dragonfly",  14,10, 9,13,12, 6, 99, 2, 25, 0, 11,9},
    {"Green Mini-Dragon",17,25,14,11,13, 9, 99, 8, 50, 0, 14,12},
    {"Green Dragon",     20,40,20, 9,14,17, 99, 20,100, 0, 17,15},
    {"Green Dragon King",35,75,30, 7,15,25, 99, 80,127, 0, 20,18},
    /* 100-103: Black Dragons */
    {"Black Dragonfly",  14,10, 9,13,12, 6, 99, 2, 25, 0, 11,9},
    {"Black Mini-Dragon",17,25,14,11,13, 9, 99, 8, 50, 0, 14,12},
    {"Black Dragon",     20,40,20, 9,14,17, 99, 20,100, 0, 17,15},
    {"Black Dragon King",35,75,30, 7,15,25, 99, 80,127, 0, 20,18},
    /* 104-107: Shadow Dragons (bosses) */
    {"Shadow Dragonfly",  14,10,49,13,12,42,100, 1,127, 1, 41,49},
    {"Shadow Minidragon", 17,20,54,11,13,55,100, 1,127, 1, 84,82},
    {"Shadow Dragon",     20,33,60, 9,14,62,100, 1,127, 1, 170,150},
    {"Shadow Dragonking", 35,45,70, 7,15,62,100, 1,127, 1, 120,118},
    /* 108-111: Red Dragons (bosses) */
    {"Red Dragonfly",     14,10,59,43,52,86,100, 1,127, 1, 41,49},
    {"Red Mini-Dragon",   17,25,84,81,83,99,100, 1,127, 1, 84,82},
    {"Red Major Dragon",  20,40,100,90,104,117,100,1,127, 1, 170,150},
    {"Red Dragon King",   35,55,120,120,115,126,100,1,127, 1, 120,118},
};

/* ══════════════════════════════════════════════════════════════════════
   Armor defense values
   ══════════════════════════════════════════════════════════════════════ */

static const int armor_defense[] = { 0, 2, 4, 6, 8, 10, 14 };

/* ══════════════════════════════════════════════════════════════════════
   Battle spell data
   Wizard spells (indices 0-29) and Priest spells (indices 0-29)
   SP cost = spell_level (level = index/3 + 1)
   ══════════════════════════════════════════════════════════════════════ */

typedef enum {
    BS_NONE, BS_SLEEP, BS_DAMAGE_SCALE, BS_DAMAGE_FIXED, BS_DAMAGE_MULTI,
    BS_DAMAGE_RANGE, BS_GO_AWAY, BS_HOLD, BS_DRAIN, BS_AUTOKILL,
    BS_BUFF_STR, BS_BUFF_SPD, BS_BUFF_SLOW, BS_BUFF_PROTECT,
    BS_RESIST_POISON, BS_RESIST_DISEASE, BS_ANTI_COLD, BS_ANTI_FIRE,
    BS_RESIST_DRAIN, BS_PASS_WALL, BS_RELOCATE, BS_POWER_WEAPON,
    BS_BUFF_STR_SPD, BS_STOP, BS_SHOCK_125, BS_SHOCK_300,
    BS_HEAL_FIXED, BS_HEAL_ALL,
} BattleSpellType;

typedef struct {
    BattleSpellType type;
    int param1;
    int param2;
} BattleSpellDef;

/* Wizard battle spells (30 entries, matching wiz_names order) */
static const BattleSpellDef wiz_spells[30] = {
    /* L1 */ {BS_SLEEP,0,0},        {BS_DAMAGE_SCALE,2,2},   {BS_BUFF_PROTECT,0,0},
    /* L2 */ {BS_BUFF_SLOW,0,0},    {BS_BUFF_STR,0,0},       {BS_DAMAGE_FIXED,25,0},
    /* L3 */ {BS_DAMAGE_SCALE,4,4}, {BS_DAMAGE_FIXED,50,0},  {BS_BUFF_SPD,0,0},
    /* L4 */ {BS_GO_AWAY,0,0},      {BS_RELOCATE,0,0},       {BS_POWER_WEAPON,1,0},
    /* L5 */ {BS_DAMAGE_RANGE,75,175},{BS_BUFF_PROTECT,0,0},  {BS_RESIST_POISON,0,0},
    /* L6 */ {BS_DAMAGE_MULTI,4,8}, {BS_SHOCK_125,0,0},      {BS_ANTI_COLD,0,0},
    /* L7 */ {BS_DAMAGE_RANGE,125,225},{BS_PASS_WALL,0,0},    {BS_ANTI_FIRE,0,0},
    /* L8 */ {BS_DAMAGE_MULTI,7,11},{BS_BUFF_PROTECT,0,0},    {BS_POWER_WEAPON,2,0},
    /* L9 */ {BS_HOLD,0,0},         {BS_DRAIN,0,0},          {BS_SHOCK_300,0,0},
    /* L10*/ {BS_DAMAGE_RANGE,200,500},{BS_AUTOKILL,0,0},     {BS_POWER_WEAPON,3,0},
};

/* Priest battle spells (30 entries, matching priest_names order) */
static const BattleSpellDef priest_spells[30] = {
    /* L1 */ {BS_SLEEP,0,0},        {BS_BUFF_PROTECT,0,0},   {BS_BUFF_STR,0,0},
    /* L2 */ {BS_RESIST_POISON,0,0},{BS_BUFF_SPD,0,0},       {BS_HEAL_FIXED,20,0},
    /* L3 */ {BS_RESIST_DISEASE,0,0},{BS_RELOCATE,0,0},      {BS_BUFF_SLOW,0,0},
    /* L4 */ {BS_ANTI_COLD,0,0},    {BS_GO_AWAY,0,0},        {BS_POWER_WEAPON,1,0},
    /* L5 */ {BS_BUFF_PROTECT,0,0}, {BS_ANTI_FIRE,0,0},      {BS_PASS_WALL,0,0},
    /* L6 */ {BS_RESIST_DRAIN,0,0}, {BS_DRAIN,0,0},          {BS_HEAL_FIXED,50,0},
    /* L7 */ {BS_HOLD,0,0},         {BS_POWER_WEAPON,2,0},   {BS_SHOCK_125,0,0},
    /* L8 */ {BS_BUFF_PROTECT,0,0}, {BS_DAMAGE_RANGE,125,225},{BS_DAMAGE_MULTI,4,8},
    /* L9 */ {BS_AUTOKILL,0,0},     {BS_POWER_WEAPON,3,0},   {BS_BUFF_STR_SPD,0,0},
    /* L10*/ {BS_BUFF_PROTECT,0,0}, {BS_HEAL_ALL,0,0},       {BS_SHOCK_300,0,0},
};

/* Spell names (same arrays as in mw_game.c, duplicated here for self-containment) */
static const char *wiz_spell_names[30] = {
    "SLEEP","MAGIC ZAP","MINOR PROTECTION",
    "SLOW ENEMIES","STRENGTH","MINOR SHOCK",
    "LIGHTNING","MAGIC MISSLE","SPEED",
    "GO AWAY","RELOCATE","POWER WEAPON I",
    "MINOR EXPLOSION","PROTECTION","RESIST POISON",
    "MAGIC ZOT","SHOCK","ANTI-COLD",
    "EXPLOSION","PASS WALL","ANTI-FIRE",
    "MAGIC BOLT","MAJOR PROTECTION","POWER WEAPON II",
    "HOLD MONSTER","DRAIN MONSTER","MAJOR SHOCK",
    "MAJOR EXPLOSION","AUTOKILL","POWER WEAPON III",
};

static const char *priest_spell_names[30] = {
    "SLEEP","MINOR PROTECTION","STRENGTH",
    "RESIST POISON","SPEED","FAST CURE",
    "RESIST DISEASE","RELOCATE","SLOW ENEMIES",
    "ANTI-COLD","GO AWAY","POWER WEAPON I",
    "PROTECTION","ANTI-FIRE","PASS WALL",
    "RESIST LEVEL DRAIN","DRAIN MONSTER","FAST BIG CURE",
    "HOLD MONSTER","POWER WEAPON II","SHOCK",
    "MAJOR PROTECTION","EXPLOSION","MAGIC ZOT",
    "AUTOKILL","POWER WEAPON III","STRENGTH AND SPEED",
    "ULTRA PROTECTION","FAST HEAL","MAJOR SHOCK",
};

/* ══════════════════════════════════════════════════════════════════════
   Helper: compute monster HP
   HP = rand(0 .. hpF*level) + 1, boss gets +level*20
   We use average for display: (hpF*level + 2) / 2, clamped to [1, 32000]
   ══════════════════════════════════════════════════════════════════════ */

int combat_calc_monster_hp(const MonsterType *mt, int level) {
    int base = mt->hpF * level;
    if (base < 1) base = 1;
    int hp = base + 1;
    if (mt->boss) hp += level * 20;
    if (hp > 32000) hp = 32000;
    if (hp < 1) hp = 1;
    return hp;
}

/* ══════════════════════════════════════════════════════════════════════
   Pick a random monster type valid for the current floor
   ══════════════════════════════════════════════════════════════════════ */

int combat_pick_monster_type(Game *g, int floor_depth) {
    int valid[MONSTER_TYPE_COUNT];
    int count = 0;
    int encounter_depth = floor_depth;
    if (encounter_depth < 1) encounter_depth = 1;
    if (encounter_depth > 127) encounter_depth = 127;

    for (int i = 0; i < MONSTER_TYPE_COUNT; i++) {
        const MonsterType *mt = &monster_types[i];
        if (encounter_depth >= mt->minL && encounter_depth <= mt->maxL) {
            valid[count++] = i;
        }
    }

    if (count == 0) return 0;
    return valid[game_rand(g) % count];
}

/* ══════════════════════════════════════════════════════════════════════
   Monster type → WORLD.PIC image index mapping.
   The game has 37 base sprite images shared across 112 types.
   Color variants of the same creature shape share one image.
   ══════════════════════════════════════════════════════════════════════ */

static const int monster_pic_index[MONSTER_TYPE_COUNT] = {
    /* Extracted from WORLD.EXE: DS:0x259 (pic_raw) + slot/flag mapping.
       -1 = no sprite image for this monster type. */
      2,   3,   4,   5,   6,   7,  -1,   8,  /*  0- 7: humanoids */
      9,  -1,  -1,  -1,  -1,  10,  11,  12,  /*  8-15: ape,moraff,misc,gods */
     13,  -1,  -1,  14,  -1,  15,  16,  17,  /* 16-23: giant,troll,garbage,mid */
     -1,  -1,  19,  20,  21,  22,  23,  -1,  /* 24-31: eye,undead */
     24,  25,  26,  27,  18,  28,  29,  30,  /* 32-39: undead,demons,yellow animals */
     31,  -1,  -1,  18,  28,  29,  30,  31,  /* 40-47: yellow/black animals */
     -1,  -1,  18,  28,  29,  30,  31,  -1,  /* 48-55: black/green animals */
     -1,  32,  32,  32,  32,  32,  32,  32,  /* 56-63: green toad, balls */
     32,  32,  32,  32,  32,  32,  32,  32,  /* 64-71: balls */
     33,  33,  33,  33,  33,  33,  33,  33,  /* 72-79: puffballs */
     33,  33,  33,  33,  34,  36,  36,  35,  /* 80-87: puffballs, orange dragons */
     34,  36,  36,  35,  34,  36,  36,  35,  /* 88-95: blue/white dragons */
     34,  36,  36,  35,  34,  36,  36,  35,  /* 96-103: green/black dragons */
     34,  36,  36,  35,  34,  36,  36,  35,  /* 104-111: shadow/red dragons */
};

static int get_monster_pic_index(int type_idx) {
    if (type_idx < 0 || type_idx >= MONSTER_TYPE_COUNT) return 0;
    return monster_pic_index[type_idx];
}

int get_monster_pic_index_ext(int type_idx) {
    return get_monster_pic_index(type_idx);
}

/* ══════════════════════════════════════════════════════════════════════
   Initialize a combat encounter
   ══════════════════════════════════════════════════════════════════════ */

void combat_init_encounter(Game *g, CombatState *cs) {
    memset(cs, 0, sizeof(*cs));
    cs->active = 1;
    cs->entity_index = -1;
    cs->monster_type_idx = combat_pick_monster_type(g, g->cur_floor);

    const MonsterType *mt = &monster_types[cs->monster_type_idx];

    cs->monster_level = g->cur_floor;
    if (cs->monster_level < 1) cs->monster_level = 1;

    int hp_range = mt->hpF * cs->monster_level;
    if (hp_range < 1) hp_range = 1;
    cs->monster_hp = (game_rand(g) % hp_range) + 1;
    if (mt->boss) cs->monster_hp += cs->monster_level * 20;
    if (cs->monster_hp > 32000) cs->monster_hp = 32000;
    if (cs->monster_hp < 1) cs->monster_hp = 1;
    cs->monster_max_hp = cs->monster_hp;
}

void combat_init_entity(Game *g, CombatState *cs, int entity_index) {
    memset(cs, 0, sizeof(*cs));
    cs->active = 1;
    cs->entity_index = entity_index;
    if (!g->monster_map_loaded || entity_index < 0 ||
        entity_index >= MONSTERS_PER_FLOOR) {
        combat_init_encounter(g, cs);
        return;
    }
    const MonsterRecord *m = &g->monster_map[g->monster_layer][entity_index];
    cs->monster_type_idx = m->type < MONSTER_TYPE_COUNT ? m->type : 0;
    cs->monster_level = m->level ? m->level : (g->cur_floor > 0 ? g->cur_floor : 1);
    cs->monster_hp = game_monster_hp(g, entity_index);
    if (cs->monster_hp < 1) cs->monster_hp = 1;
    cs->monster_max_hp = cs->monster_hp;
}

/* ══════════════════════════════════════════════════════════════════════
   Get effective weapon index (handles Power Weapon spell override)
   ══════════════════════════════════════════════════════════════════════ */

static int get_effective_weapon(Character *p) {
    if (p->eff_pwr_weapon > 0 && p->eff_pwr_weapon <= 3)
        return 8 + p->eff_pwr_weapon;
    int wpn = p->equipped_weapon;
    if (wpn >= WEAPON_STAT_COUNT) wpn = 0;
    return wpn;
}

/* ══════════════════════════════════════════════════════════════════════
   Player melee attack — exact formula from WORLD.EXE disassembly
   Lines 16888-17010 of WORLD.C

   Phase 1: Hit Score → number of swings
   Phase 2: Each swing does rand(0..maxDmg-1) damage
   Phase 3: Bonus damage (STR bonus, level bonus, low-level bonus)

   Returns damage dealt (0 = miss)
   ══════════════════════════════════════════════════════════════════════ */

int combat_player_attack(Game *g, CombatState *cs, Character *player) {
    const MonsterType *mt = &monster_types[cs->monster_type_idx];
    int eff_wpn = get_effective_weapon(player);
    const WeaponStats *ws = &weapon_stats[eff_wpn];

    /* Phase 1: Compute hit score */
    int hit_score = game_rand(g) % 80;  /* rand(0-79) */

    hit_score += player->level * 2;
    hit_score += player->stat_str;
    hit_score += player->stat_luck;
    hit_score += player->combat_bonus;
    /* Preparation and battle stat spells modify the saved stats themselves;
       their flag/counter fields only track how and when to reverse them. */
    hit_score += ws->hit;
    hit_score += player->gauntlet;

    /* Permanent weapon enchant for equipped weapon */
    int base_wpn = player->equipped_weapon;
    if (base_wpn >= WEAPON_STAT_COUNT) base_wpn = 0;
    hit_score += player->eq_wep_enchant[base_wpn];

    /* Temporary Enchant Weapon spell */
    hit_score += player->enchant_wpn_spell;

    /* Subtract monster defenses */
    hit_score -= mt->atk * 2;  /* monsterLevel scaling uses atk as level proxy */
    hit_score -= mt->def;
    hit_score -= mt->defMod;
    hit_score -= mt->agi;

    /* High-level monster bonus: 1/30 chance of +40 */
    if (cs->monster_level > 75) {
        if ((game_rand(g) % 30) == 1) {
            hit_score += 40;
        }
    }

    /* Phase 2: Swings — each 40 points above 40 = one swing */
    int total_damage = 0;
    int swings = 0;
    int si = hit_score;
    while (si > 40) {
        int max_dmg = ws->maxDmg;
        if (max_dmg < 1) max_dmg = 1;
        total_damage += game_rand(g) % max_dmg;
        si -= 40;
        swings++;
    }

    if (total_damage <= 0 && swings == 0) return 0; /* MISS */

    /* Phase 3: Bonus damage (applied once per hit, not per swing) */
    /* STR bonus: 50% chance big (rand STR), 50% chance small (rand STR/3) */
    if (player->stat_str > 0) {
        if ((game_rand(g) % 20) < (int)player->level) {
            total_damage += game_rand(g) % player->stat_str;
        } else {
            int small_str = player->stat_str / 3;
            if (small_str > 0) total_damage += game_rand(g) % small_str;
        }
    }

    /* Level bonus */
    if (player->level > 0) {
        total_damage += game_rand(g) % player->level;
    }

    /* Low-level bonus */
    if (player->level < 5) {
        int bonus_range = 5 - player->level;
        if (bonus_range > 0) total_damage += game_rand(g) % bonus_range;
    }

    if (total_damage <= 0) return 0;
    return total_damage;
}

/* ══════════════════════════════════════════════════════════════════════
   Monster attacks player
   Returns damage dealt to player (0 = miss)
   ══════════════════════════════════════════════════════════════════════ */

int combat_monster_attack(Game *g, CombatState *cs, Character *player) {
    const MonsterType *mt = &monster_types[cs->monster_type_idx];

    if (cs->monster_asleep > 0 || cs->monster_held > 0 || cs->monster_stopped > 0)
        return 0;

    /* Monster hit score */
    int hit_score = game_rand(g) % 80;
    hit_score += cs->monster_level * 2;
    hit_score += mt->atk;

    /* Subtract player defenses */
    hit_score -= player->level * 2;
    hit_score -= player->stat_agi;
    hit_score -= player->combat_bonus;
    int armor = player->equipped_armor < ARMOR_STAT_COUNT ? player->equipped_armor : 0;
    hit_score -= armor_defense[armor];
    hit_score -= player->armor_enchant[armor];
    hit_score -= player->body_armor_plus;
    hit_score -= player->ring_prot_plus;
    hit_score -= player->armor_plus;
    if (player->eff_slow_mon && (game_rand(g) % 4) != 0)
        hit_score -= player->stat_agi / 3;

    /* Protection spell: squared effect */
    if (player->eff_protect_lv > 0) {
        int prot = player->eff_protect_lv * player->eff_protect_lv * 2;
        hit_score -= prot;
    }

    int total_damage = 0;
    int si = hit_score;
    while (si > 40) {
        if (mt->dmg > 0)
            total_damage += game_rand(g) % mt->dmg;
        si -= 40;
    }

    if (total_damage <= 0) return 0;
    return total_damage;
}

/* ══════════════════════════════════════════════════════════════════════
   Cast a battle spell — returns damage dealt or special code
   -1 = instant kill, -2 = immune, -3 = effect only (sleep/hold/buff), 0 = miss/fail
   ══════════════════════════════════════════════════════════════════════ */

static int apply_battle_spell(Game *g, CombatState *cs, Character *player,
                              const BattleSpellDef *sd, int spell_level) {
    const MonsterType *mt = &monster_types[cs->monster_type_idx];

    switch (sd->type) {
    case BS_NONE:
        return 0;

    case BS_SLEEP:
        if (mt->imm >= 100) return -2;
        cs->monster_asleep = 10;
        return -3;

    case BS_DAMAGE_SCALE:
        return player->level * sd->param1 + sd->param2;

    case BS_DAMAGE_FIXED:
        return sd->param1;

    case BS_DAMAGE_MULTI: {
        int total = 0;
        int missiles = player->level + 1;
        int range = sd->param2 - sd->param1 + 1;
        for (int i = 0; i < missiles; i++)
            total += (game_rand(g) % range) + sd->param1;
        return total;
    }

    case BS_DAMAGE_RANGE: {
        int range = sd->param2 - sd->param1 + 1;
        return (game_rand(g) % range) + sd->param1;
    }

    case BS_GO_AWAY:
        if (mt->imm >= 100) return -2;
        cs->fled = 1;
        return -3;

    case BS_HOLD:
        if (mt->imm >= 100) return -2;
        cs->monster_held = 15;
        return -3;

    case BS_STOP:
        if (mt->imm >= 100) return -2;
        cs->monster_stopped = 10;
        return -3;

    case BS_DRAIN: {
        if (cs->monster_level < player->stat_wis) return -1;
        int half_hpf = mt->hpF / 2;
        if (half_hpf < 1) half_hpf = 1;
        return half_hpf * player->stat_wis;
    }

    case BS_AUTOKILL: {
        if (mt->imm >= 100) return -2;
        int mon_save_rand = game_rand(g) % (mt->saveA + mt->saveB + 1);
        int mon_score = game_rand(g) % (cs->monster_level + mon_save_rand + 1);
        int player_mental = game_rand(g) % (player->stat_wis + player->stat_int + 1);
        int player_score = game_rand(g) % (player->level + player_mental + 1);
        return (player_score >= mon_score) ? -1 : 0;
    }

    case BS_SHOCK_125:
        return 125;

    case BS_SHOCK_300:
        return 300;

    case BS_BUFF_STR:
        player->eff_battle_str = player->level * 2 + 10;
        return -3;

    case BS_BUFF_SPD:
        player->eff_battle_spd = player->level * 2 + 10;
        return -3;

    case BS_BUFF_STR_SPD:
        player->eff_battle_str = player->level * 3 + 15;
        player->eff_battle_spd = player->level * 3 + 15;
        return -3;

    case BS_BUFF_SLOW:
        player->eff_slow_mon = player->level + 5;
        return -3;

    case BS_BUFF_PROTECT:
        player->eff_protect_lv = (u8)spell_level;
        player->eff_protect_turns = player->level * 10 + 20;
        return -3;

    case BS_RESIST_POISON:
        player->eff_resist_poison = player->level * 5 + 10;
        return -3;

    case BS_RESIST_DISEASE:
        player->eff_resist_disease = player->level * 5 + 10;
        return -3;

    case BS_ANTI_COLD:
        player->eff_anti_cold = player->level * 5 + 10;
        return -3;

    case BS_ANTI_FIRE:
        player->eff_anti_fire = player->level * 5 + 10;
        return -3;

    case BS_RESIST_DRAIN:
        player->eff_resist_drain = player->level * 5 + 10;
        return -3;

    case BS_PASS_WALL:
        if (!game_pass_wall(g, player)) return 0;
        cs->player_fled = 1;
        return -4;

    case BS_RELOCATE:
        cs->player_fled = 1;
        game_relocate(g, player);
        return -4;

    case BS_HEAL_FIXED: {
        int hp = (int)player->hp_cur + sd->param1;
        player->hp_cur = (u16)(hp > player->hp_max ? player->hp_max : hp);
        return -3;
    }

    case BS_HEAL_ALL:
        player->hp_cur = player->hp_max;
        return -3;

    case BS_POWER_WEAPON:
        player->eff_pwr_weapon = (u8)sd->param1;
        player->eff_pwr_wpn_turns = player->level * 10 + 20;
        return -3;

    default:
        return 0;
    }
}

/* ══════════════════════════════════════════════════════════════════════
   Draw combat in the active exploration viewport
   ══════════════════════════════════════════════════════════════════════ */

static void draw_combat_screen(Game *g, CombatState *cs, Character *player,
                                const char *msg1, const char *msg2, const char *msg3) {
    game_draw_combat_overlay(g, player, cs->entity_index,
                             cs->monster_type_idx, cs->monster_level,
                             cs->monster_hp, msg1, msg2, msg3);
    video_present(&g->video);
}

/* ══════════════════════════════════════════════════════════════════════
   Spell selection UI for battle spells
   Returns: spell index (0-29) or -1 if cancelled
   category: 2=wizard, 3=priest
   ══════════════════════════════════════════════════════════════════════ */

static int select_battle_spell(Game *g, Character *player, int category) {
    Video *v = &g->video;
    int fh = v->font_char_h;
    char line[80];

    const char **names = (category == 2) ? wiz_spell_names : priest_spell_names;
    u8 *spellbook = player->spells[category];

    video_clear(v, 0);

    const char *header = (category == 2) ? "WIZARD BATTLE SPELLS" : "PRIEST BATTLE SPELLS";
    video_draw_text_scaled(v, 8, 2, header, 14, 3, 4);

    int row_h = fh * 3 / 4;
    if (row_h < 8) row_h = 8;
    int y = 2 + fh + 4;
    int num_available = 0;

    static const u8 level_colors[10] = { 6, 8, 3, 4, 5, 7, 6, 8, 3, 4 };

    for (int i = 0; i < 30; i++) {
        if (!spellbook[i]) continue;
        num_available++;

        int lv = i / 3 + 1;
        u8 color = level_colors[lv - 1];

        snprintf(line, sizeof(line), "%c) L%d %s", 'A' + (num_available - 1), lv, names[i]);
        video_draw_text_scaled(v, 8, y, line, color, 3, 4);
        y += row_h;

        if (num_available >= 26) break;
    }

    if (num_available == 0) {
        video_draw_text(v, 8, y, "YOU HAVE NO SPELLS OF THIS TYPE!", 12);
        y += fh + 4;
        video_draw_text(v, 8, y, "HIT ANY KEY...", 15);
        video_present(v);
        input_getch(&g->input);
        return -1;
    }

    video_draw_text_scaled(v, 8, LOGICAL_H - fh - 4, "SELECT SPELL (ESC TO CANCEL)...", 15, 3, 4);
    video_present(v);

    while (1) {
        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input)) return -1;
        if (key == 0x1B) return -1;

        int selection = -1;
        if (key >= 'a' && key <= 'z') selection = key - 'a';
        if (key >= 'A' && key <= 'Z') selection = key - 'A';

        if (selection >= 0 && selection < num_available) {
            int count = 0;
            for (int i = 0; i < 30; i++) {
                if (!spellbook[i]) continue;
                if (count == selection) return i;
                count++;
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
   Handle spell casting during combat
   Returns: damage dealt to monster, or special codes
   ══════════════════════════════════════════════════════════════════════ */

int combat_cast_battle_spell(Game *g, CombatState *cs, Character *player) {
    Video *v = &g->video;
    int fh = v->font_char_h + 2;

    /* Ask: Wizard or Priest? */
    video_clear(v, 0);
    video_draw_text(v, 8, 4, "CAST WHICH TYPE OF SPELL?", 14);
    video_draw_text(v, 8, 4 + fh * 2, "1) WIZARD BATTLE SPELL", 7);
    video_draw_text(v, 8, 4 + fh * 3, "2) PRIEST BATTLE SPELL", 7);
    video_draw_text(v, 8, 4 + fh * 5, "ESC TO CANCEL", 8);
    video_present(v);

    int category = -1;
    while (1) {
        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input)) return 0;
        if (key == 0x1B) return 0;
        if (key == '1') { category = 2; break; }
        if (key == '2') { category = 3; break; }
    }

    int spell_idx = select_battle_spell(g, player, category);
    if (spell_idx < 0) return 0;

    /* Check SP cost */
    int spell_level = spell_idx / 3 + 1;
    float sp_cost = (float)spell_level;

    if (player->sp_cur < sp_cost) {
        video_clear(v, 0);
        video_draw_text(v, 8, 4, "NOT ENOUGH SPELL POINTS!", 12);
        char line[64];
        snprintf(line, sizeof(line), "NEED: %d   HAVE: %.0f", spell_level, player->sp_cur);
        video_draw_text(v, 8, 4 + fh, line, 7);
        video_draw_text(v, 8, 4 + fh * 3, "HIT ANY KEY...", 15);
        video_present(v);
        input_getch(&g->input);
        return 0;
    }

    /* Spend SP */
    player->sp_cur -= sp_cost;

    /* Apply spell */
    const BattleSpellDef *sd = (category == 2) ? &wiz_spells[spell_idx] : &priest_spells[spell_idx];
    return apply_battle_spell(g, cs, player, sd, spell_level);
}

/* ══════════════════════════════════════════════════════════════════════
   Main combat loop
   ══════════════════════════════════════════════════════════════════════ */

void combat_run(Game *g, CombatState *cs, Character *player) {
    char msg1[128] = "";
    char msg2[128] = "";
    char msg3[128] = "";
    const MonsterType *mt = &monster_types[cs->monster_type_idx];
    static const char *dir_name[4] = {"NORTH", "SOUTH", "WEST", "EAST"};
    int combat_dir = g->last_move_dir & 3;
    if (g->monster_map_loaded && cs->entity_index >= 0 &&
        cs->entity_index < MONSTERS_PER_FLOOR) {
        const MonsterRecord *m = &g->monster_map[g->monster_layer][cs->entity_index];
        int dx = (int)m->x - g->cur_x;
        int dy = (int)m->y - g->cur_y;
        if (dx == 0 && dy == -1) combat_dir = 0;
        else if (dx == 0 && dy == 1) combat_dir = 1;
        else if (dx == -1 && dy == 0) combat_dir = 2;
        else if (dx == 1 && dy == 0) combat_dir = 3;
    }

    while (cs->active && !input_poll_quit(&g->input)) {
        draw_combat_screen(g, cs, player, msg1, msg2, msg3);
        msg1[0] = msg2[0] = msg3[0] = 0;

        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input)) break;

        if (key == 'f' || key == 'F') {
            /* ── Player attacks ── */
            int dmg = combat_player_attack(g, cs, player);
            if (dmg <= 0) {
                snprintf(msg1, sizeof(msg1), "YOU MISS");
            } else {
                cs->monster_hp -= dmg;
                snprintf(msg1, sizeof(msg1), "YOU DO %d POINTS", dmg);
                if (cs->monster_hp < 1) {
                    snprintf(msg2, sizeof(msg2), "THE %s IS DEAD!", mt->name);

                    /* XP reward */
                    int xp = cs->monster_level * mt->hpF + mt->atk + mt->def;
                    snprintf(msg3, sizeof(msg3), "YOU GAIN %d EXPERIENCE!", xp);

                    draw_combat_screen(g, cs, player, msg1, msg2, msg3);
                    input_getch(&g->input);
                    cs->active = 0;
                    break;
                }
            }

            /* ── Monster counter-attacks ── */
            if (cs->active) {
                int mon_dmg = combat_monster_attack(g, cs, player);
                if (mon_dmg <= 0) {
                    if (cs->monster_asleep > 0 || cs->monster_held > 0 || cs->monster_stopped > 0) {
                        snprintf(msg2, sizeof(msg2), "THE %s CANNOT ATTACK!", mt->name);
                    } else {
                        snprintf(msg2, sizeof(msg2), "%s MISSES", dir_name[combat_dir]);
                    }
                } else {
                    if (mon_dmg >= (int)player->hp_cur)
                        player->hp_cur = 0;
                    else
                        player->hp_cur -= (u16)mon_dmg;
                    snprintf(msg2, sizeof(msg2), "%s DOES %d POINTS",
                             dir_name[combat_dir], mon_dmg);

                    if (player->hp_cur == 0) {
                        player->hp_cur = 0;
                        snprintf(msg3, sizeof(msg3), "YOU HAVE BEEN KILLED!");
                        draw_combat_screen(g, cs, player, msg1, msg2, msg3);
                        input_getch(&g->input);
                        cs->active = 0;
                        break;
                    }
                }
            }

            /* Decrement status effect timers */
            if (cs->monster_asleep > 0) cs->monster_asleep--;
            if (cs->monster_held > 0) cs->monster_held--;
            if (cs->monster_stopped > 0) cs->monster_stopped--;

        } else if (key == 'c' || key == 'C') {
            /* ── Cast spell ── */
            int result = combat_cast_battle_spell(g, cs, player);

            if (result == 0) {
                /* Cancelled or failed */
                continue;
            } else if (result == -1) {
                /* Instant kill */
                cs->monster_hp = 0;
                snprintf(msg1, sizeof(msg1), "THE %s IS DESTROYED!", mt->name);
                int xp = cs->monster_level * mt->hpF + mt->atk + mt->def;
                snprintf(msg2, sizeof(msg2), "YOU GAIN %d EXPERIENCE!", xp);
                draw_combat_screen(g, cs, player, msg1, msg2, "");
                input_getch(&g->input);
                cs->active = 0;
                break;
            } else if (result == -2) {
                /* Immune */
                snprintf(msg1, sizeof(msg1), "THE %s IS IMMUNE TO THAT SPELL!", mt->name);
            } else if (result == -3) {
                /* Effect applied (buff/debuff) */
                snprintf(msg1, sizeof(msg1), "SPELL CAST SUCCESSFULLY!");
            } else if (result == -4) {
                snprintf(msg1, sizeof(msg1), "YOU ESCAPE THE MONSTER!");
                draw_combat_screen(g, cs, player, msg1, "", "");
                input_getch(&g->input);
                cs->active = 0;
                break;
            } else if (result > 0) {
                /* Damage */
                cs->monster_hp -= result;
                snprintf(msg1, sizeof(msg1), "YOUR SPELL HITS FOR %d DAMAGE!", result);
                if (cs->monster_hp <= 0) {
                    snprintf(msg2, sizeof(msg2), "THE %s IS DEAD!", mt->name);
                    int xp = cs->monster_level * mt->hpF + mt->atk + mt->def;
                    snprintf(msg3, sizeof(msg3), "YOU GAIN %d EXPERIENCE!", xp);
                    draw_combat_screen(g, cs, player, msg1, msg2, msg3);
                    input_getch(&g->input);
                    cs->active = 0;
                    break;
                }
            }

            /* Monster counter-attack after spell */
            if (cs->active && result != 0) {
                /* Check if Go Away worked */
                if (cs->fled) {
                    snprintf(msg2, sizeof(msg2), "THE %s FLEES!", mt->name);
                    draw_combat_screen(g, cs, player, msg1, msg2, "");
                    input_getch(&g->input);
                    cs->active = 0;
                    break;
                }

                int mon_dmg = combat_monster_attack(g, cs, player);
                if (mon_dmg <= 0) {
                    if (cs->monster_asleep > 0 || cs->monster_held > 0 || cs->monster_stopped > 0) {
                        snprintf(msg2, sizeof(msg2), "THE %s CANNOT ATTACK!", mt->name);
                    } else {
                        snprintf(msg2, sizeof(msg2), "%s MISSES", dir_name[combat_dir]);
                    }
                } else {
                    if (mon_dmg >= (int)player->hp_cur)
                        player->hp_cur = 0;
                    else
                        player->hp_cur -= (u16)mon_dmg;
                    snprintf(msg2, sizeof(msg2), "%s DOES %d POINTS",
                             dir_name[combat_dir], mon_dmg);
                    if (player->hp_cur == 0) {
                        player->hp_cur = 0;
                        snprintf(msg3, sizeof(msg3), "YOU HAVE BEEN KILLED!");
                        draw_combat_screen(g, cs, player, msg1, msg2, msg3);
                        input_getch(&g->input);
                        cs->active = 0;
                        break;
                    }
                }

                if (cs->monster_asleep > 0) cs->monster_asleep--;
                if (cs->monster_held > 0) cs->monster_held--;
                if (cs->monster_stopped > 0) cs->monster_stopped--;
            }

        } else if (key == 'r' || key == 'R') {
            /* ── Run away ── */
            int flee_chance = player->stat_agi + player->stat_luck + player->level * 2;
            int mon_hold = mt->atk + mt->agi + cs->monster_level;
            if ((game_rand(g) % (flee_chance + mon_hold)) < flee_chance) {
                snprintf(msg1, sizeof(msg1), "YOU RUN AWAY!");
                cs->player_fled = 1;
                draw_combat_screen(g, cs, player, msg1, "", "");
                input_getch(&g->input);
                cs->active = 0;
                break;
            } else {
                snprintf(msg1, sizeof(msg1), "YOU FAIL TO ESCAPE!");
                /* Monster gets a free attack */
                int mon_dmg = combat_monster_attack(g, cs, player);
                if (mon_dmg > 0) {
                    if (mon_dmg >= (int)player->hp_cur)
                        player->hp_cur = 0;
                    else
                        player->hp_cur -= (u16)mon_dmg;
                    snprintf(msg2, sizeof(msg2), "%s DOES %d POINTS",
                             dir_name[combat_dir], mon_dmg);
                    if (player->hp_cur == 0) {
                        player->hp_cur = 0;
                        snprintf(msg3, sizeof(msg3), "YOU HAVE BEEN KILLED!");
                        draw_combat_screen(g, cs, player, msg1, msg2, msg3);
                        input_getch(&g->input);
                        cs->active = 0;
                        break;
                    }
                }
            }
        }
        /* Any other key: redraw, no action */
    }
}

/* ══════════════════════════════════════════════════════════════════════
   Weapon selection command (W key from exploration)
   ══════════════════════════════════════════════════════════════════════ */

void cmd_weapons(Game *g, Character *player) {
    Video *v = &g->video;
    int fh = v->font_char_h + 2;
    char line[128];

    video_clear(v, 0);
    int y = 4;

    video_draw_text(v, 8, y, "SELECT WEAPON:", 14);
    y += fh + 4;

    for (int i = 0; i < 8; i++) {
        u8 color = (i == player->equipped_weapon) ? 15 : 7;
        snprintf(line, sizeof(line), "%d) %s (DMG:%d HIT:+%d)",
                 i + 1, weapon_stats[i].name, weapon_stats[i].maxDmg, weapon_stats[i].hit);
        video_draw_text(v, 8, y, line, color);
        if (i == player->equipped_weapon) {
            video_draw_text(v, 450, y, "<-- EQUIPPED", 14);
        }
        y += fh;
    }

    y += 4;
    video_draw_text(v, 8, y, "HIT 1-8 TO SELECT, ESC TO CANCEL", 15);
    video_present(v);

    while (1) {
        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input)) return;
        if (key == 0x1B) return;
        if (key >= '1' && key <= '8') {
            player->equipped_weapon = key - '1';
            return;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
   Armor selection command (A key from exploration)
   ══════════════════════════════════════════════════════════════════════ */

static const char *armor_type_names[] = {
    "SKIN", "LEATHER", "CHAIN", "SCALE", "PLATE", "FIELD PLATE", "TITANIUM"
};

void cmd_armor(Game *g, Character *player) {
    Video *v = &g->video;
    int fh = v->font_char_h + 2;
    char line[128];

    video_clear(v, 0);
    int y = 4;

    video_draw_text(v, 8, y, "SELECT ARMOR:", 14);
    y += fh + 4;

    for (int i = 0; i < ARMOR_STAT_COUNT; i++) {
        u8 color = (i == player->body_armor_lv) ? 15 : 7;
        snprintf(line, sizeof(line), "%d) %s (DEF:%d)", i + 1, armor_type_names[i], armor_defense[i]);
        video_draw_text(v, 8, y, line, color);
        if (i == player->body_armor_lv) {
            video_draw_text(v, 350, y, "<-- EQUIPPED", 14);
        }
        y += fh;
    }

    y += 4;
    video_draw_text(v, 8, y, "HIT 1-7 TO SELECT, ESC TO CANCEL", 15);
    video_present(v);

    while (1) {
        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input)) return;
        if (key == 0x1B) return;
        if (key >= '1' && key <= '7') {
            player->body_armor_lv = key - '1';
            return;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
   Preparation spell casting (C key from exploration mode)
   Handles non-combat spells: cure, teleport, buff, etc.
   ══════════════════════════════════════════════════════════════════════ */

void cmd_cast_prep_spell(Game *g, Character *player) {
    Video *v = &g->video;
    int fh = v->font_char_h + 2;
    char line[80];

    /* Show available preparation spells */
    u8 *spellbook = player->spells[SPELL_CAT_PREPARATION];

    static const char *prep_spell_names[30] = {
        "ENCHANT ARMOR L1","ENCHANT WEAPON L1","LITTLE CURE",
        "ENCHANT WEAPON L2","RELOCATE","DETECT LEVEL",
        "CURE","ENCHANT ARMOR L2","STRENGTH",
        "ENCHANT WEAPON L3","AGILITY","DESCEND",
        "ASCEND","DETECT POSITION","FEATHER",
        "BIG CURE","DOUBLE ASCEND","ENCHANT WEAPON L4",
        "INVISIBILITY","ENCHANT ARMOR L3","FAST MOVE",
        "SUPER STRENGTH","ENCHANT WEAPON L5","MAJOR DESCEND",
        "SUPER AGILITY","CURE POISON","HEAL ALL WOUNDS",
        "MAJOR ASCEND","CURE DISEASE","ENCHANT ARMOR L4",
    };

    video_clear(v, 0);
    video_draw_text_scaled(v, 8, 2, "PREPARATION SPELLS", 14, 3, 4);

    int row_h = fh * 3 / 4;
    if (row_h < 8) row_h = 8;
    int y = 2 + fh + 4;
    int num_available = 0;

    static const u8 level_colors[10] = { 6, 8, 3, 4, 5, 7, 6, 8, 3, 4 };

    for (int i = 0; i < 30; i++) {
        if (!spellbook[i]) continue;
        num_available++;

        int lv = i / 3 + 1;
        u8 color = level_colors[lv - 1];

        snprintf(line, sizeof(line), "%c) L%d %s", 'A' + (num_available - 1), lv, prep_spell_names[i]);
        video_draw_text_scaled(v, 8, y, line, color, 3, 4);
        y += row_h;

        if (num_available >= 26) break;
    }

    if (num_available == 0) {
        video_draw_text(v, 8, y, "YOU HAVE NO PREPARATION SPELLS!", 12);
        y += fh + 4;
        video_draw_text(v, 8, y, "HIT ANY KEY...", 15);
        video_present(v);
        input_getch(&g->input);
        return;
    }

    video_draw_text_scaled(v, 8, LOGICAL_H - fh - 4, "SELECT SPELL (ESC TO CANCEL)...", 15, 3, 4);
    video_present(v);

    while (1) {
        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input)) return;
        if (key == 0x1B) return;

        int selection = -1;
        if (key >= 'a' && key <= 'z') selection = key - 'a';
        if (key >= 'A' && key <= 'Z') selection = key - 'A';

        if (selection >= 0 && selection < num_available) {
            int count = 0;
            int spell_idx = -1;
            for (int i = 0; i < 30; i++) {
                if (!spellbook[i]) continue;
                if (count == selection) { spell_idx = i; break; }
                count++;
            }
            if (spell_idx < 0) continue;

            int spell_level = spell_idx / 3 + 1;
            float sp_cost = (float)spell_level;

            if (player->sp_cur < sp_cost) {
                video_clear(v, 0);
                video_draw_text(v, 8, 4, "NOT ENOUGH SPELL POINTS!", 12);
                snprintf(line, sizeof(line), "NEED: %d   HAVE: %.0f", spell_level, player->sp_cur);
                video_draw_text(v, 8, 4 + fh, line, 7);
                video_draw_text(v, 8, 4 + fh * 3, "HIT ANY KEY...", 15);
                video_present(v);
                input_getch(&g->input);
                return;
            }

            player->sp_cur -= sp_cost;

            /* Apply preparation spell effect */
            video_clear(v, 0);
            switch (spell_idx) {
            case 0:  /* Enchant Armor L1 */
                if (player->armor_plus < 1) player->armor_plus = 1;
                video_draw_text(v, 8, 4, "YOUR ARMOR IS ENCHANTED +1!", 10);
                break;
            case 1:  /* Enchant Weapon L1 */
                if (player->enchant_wpn_spell < 1) player->enchant_wpn_spell = 1;
                video_draw_text(v, 8, 4, "YOUR WEAPON IS ENCHANTED +1!", 10);
                break;
            case 2:  /* Little Cure */
                player->hp_cur += 5;
                if (player->hp_cur > player->hp_max) player->hp_cur = player->hp_max;
                video_draw_text(v, 8, 4, "YOU ARE HEALED FOR 5 HP!", 10);
                break;
            case 3:  /* Enchant Weapon L2 */
                if (player->enchant_wpn_spell < 2) player->enchant_wpn_spell = 2;
                video_draw_text(v, 8, 4, "YOUR WEAPON IS ENCHANTED +2!", 10);
                break;
            case 4:  /* Relocate */
                if (game_relocate(g, player))
                    video_draw_text(v, 8, 4, "YOU RELOCATE ELSEWHERE ON THIS LEVEL!", 10);
                else
                    video_draw_text(v, 8, 4, "THE SPELL CANNOT FIND A SAFE PLACE!", 12);
                break;
            case 5:  /* Detect Level */
                snprintf(line, sizeof(line), "YOU ARE ON DUNGEON LEVEL %d.", g->cur_floor);
                video_draw_text(v, 8, 4, line, 10);
                break;
            case 6:  /* Cure */
                player->hp_cur += 20;
                if (player->hp_cur > player->hp_max) player->hp_cur = player->hp_max;
                video_draw_text(v, 8, 4, "YOU ARE HEALED FOR 20 HP!", 10);
                break;
            case 7:  /* Enchant Armor L2 */
                if (player->armor_plus < 2) player->armor_plus = 2;
                video_draw_text(v, 8, 4, "YOUR ARMOR IS ENCHANTED +2!", 10);
                break;
            case 8:  /* Strength */
                player->eff_str_bonus = 60;
                video_draw_text(v, 8, 4, "YOU FEEL STRONGER!", 10);
                break;
            case 10: /* Agility */
                player->eff_agi_bonus = 60;
                video_draw_text(v, 8, 4, "YOU FEEL MORE AGILE!", 10);
                break;
            case 9:  /* Enchant Weapon L3 */
                if (player->enchant_wpn_spell < 3) player->enchant_wpn_spell = 3;
                video_draw_text(v, 8, 4, "YOUR WEAPON IS ENCHANTED +3!", 10);
                break;
            case 11: /* Descend */
                if (g->cur_floor <= 123) {
                    game_change_floor(g, player, g->cur_floor + 1);
                    game_relocate(g, player);
                    video_draw_text(v, 8, 4, "YOU DESCEND ONE LEVEL!", 10);
                } else video_draw_text(v, 8, 4, "THAT SPELL DOES NOT WORK THIS DEEP!", 12);
                break;
            case 12: /* Ascend */
                if (g->cur_floor > 65) {
                    video_draw_text(v, 8, 4, "THAT SPELL DOES NOT WORK BELOW LEVEL 64!", 12);
                } else if (g->cur_floor > 0) {
                    game_change_floor(g, player, g->cur_floor - 1);
                    game_relocate(g, player);
                    video_draw_text(v, 8, 4, "YOU ASCEND ONE LEVEL!", 10);
                } else video_draw_text(v, 8, 4, "YOU CANNOT ASCEND ANY FARTHER!", 12);
                break;
            case 13: /* Detect Position */
                snprintf(line, sizeof(line), "YOUR POSITION IS X:%d Y:%d.",
                         g->cur_x, g->cur_y);
                video_draw_text(v, 8, 4, line, 10);
                break;
            case 14: /* Feather */
                player->eff_feather = 60;
                video_draw_text(v, 8, 4, "YOU FEEL LIGHT AS A FEATHER!", 10);
                break;
            case 15: /* Big Cure */
                player->hp_cur += 50;
                if (player->hp_cur > player->hp_max) player->hp_cur = player->hp_max;
                video_draw_text(v, 8, 4, "YOU ARE HEALED FOR 50 HP!", 10);
                break;
            case 16: /* Double Ascend */
                if (g->cur_floor > 65) {
                    video_draw_text(v, 8, 4, "THAT SPELL DOES NOT WORK BELOW LEVEL 64!", 12);
                } else if (g->cur_floor > 0) {
                    game_change_floor(g, player, g->cur_floor - 2);
                    game_relocate(g, player);
                    video_draw_text(v, 8, 4, "YOU ASCEND TWO LEVELS!", 10);
                } else video_draw_text(v, 8, 4, "YOU CANNOT ASCEND ANY FARTHER!", 12);
                break;
            case 17: /* Enchant Weapon L4 */
                if (player->enchant_wpn_spell < 4) player->enchant_wpn_spell = 4;
                video_draw_text(v, 8, 4, "YOUR WEAPON IS ENCHANTED +4!", 10);
                break;
            case 18: /* Invisibility */
                player->eff_invisible = 60;
                video_draw_text(v, 8, 4, "YOU BECOME INVISIBLE!", 10);
                break;
            case 19: /* Enchant Armor L3 */
                if (player->armor_plus < 3) player->armor_plus = 3;
                video_draw_text(v, 8, 4, "YOUR ARMOR IS ENCHANTED +3!", 10);
                break;
            case 20: /* Fast Move */
                player->eff_fast_move = 60;
                video_draw_text(v, 8, 4, "YOU CAN MOVE FASTER!", 10);
                break;
            case 21: /* Super Strength */
                player->eff_super_str = 60;
                video_draw_text(v, 8, 4, "INCREDIBLE STRENGTH!", 10);
                break;
            case 22: /* Enchant Weapon L5 */
                if (player->enchant_wpn_spell < 5) player->enchant_wpn_spell = 5;
                video_draw_text(v, 8, 4, "YOUR WEAPON IS ENCHANTED +5!", 10);
                break;
            case 23: /* Major Descend */
                if (g->cur_floor <= 65) {
                    int target = g->cur_floor + 25;
                    if (target > 75) target = 75;
                    game_change_floor(g, player, target);
                    game_relocate(g, player);
                    video_draw_text(v, 8, 4, "YOU DESCEND TWENTY-FIVE LEVELS!", 10);
                } else video_draw_text(v, 8, 4, "THAT SPELL DOES NOT WORK BELOW LEVEL 64!", 12);
                break;
            case 24: /* Super Agility */
                player->eff_super_agi = 60;
                video_draw_text(v, 8, 4, "INCREDIBLE AGILITY!", 10);
                break;
            case 25: /* Cure Poison */
                player->poisoned_turns = 0;
                video_draw_text(v, 8, 4, "POISON CURED!", 10);
                break;
            case 26: /* Heal All Wounds */
                player->hp_cur = player->hp_max;
                video_draw_text(v, 8, 4, "ALL WOUNDS HEALED!", 10);
                break;
            case 27: /* Major Ascend */
                if (g->cur_floor > 65) {
                    video_draw_text(v, 8, 4, "THAT SPELL DOES NOT WORK BELOW LEVEL 64!", 12);
                } else if (g->cur_floor > 0) {
                    game_change_floor(g, player, g->cur_floor - 25);
                    game_relocate(g, player);
                    video_draw_text(v, 8, 4, "YOU ASCEND TWENTY-FIVE LEVELS!", 10);
                } else video_draw_text(v, 8, 4, "YOU CANNOT ASCEND ANY FARTHER!", 12);
                break;
            case 28: /* Cure Disease */
                player->diseased_turns = 0;
                video_draw_text(v, 8, 4, "DISEASE CURED!", 10);
                break;
            case 29: /* Enchant Armor L4 */
                if (player->armor_plus < 4) player->armor_plus = 4;
                video_draw_text(v, 8, 4, "YOUR ARMOR IS ENCHANTED +4!", 10);
                break;
            default:
                snprintf(line, sizeof(line), "SPELL CAST: %s", prep_spell_names[spell_idx]);
                video_draw_text(v, 8, 4, line, 10);
                break;
            }

            video_draw_text(v, 8, LOGICAL_H - fh - 4, "HIT ANY KEY...", 15);
            video_present(v);
            input_getch(&g->input);
            return;
        }
    }
}
