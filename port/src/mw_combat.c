#include "mw_combat.h"
#include "mw_game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Convert a click on a uniformly spaced numbered/lettered list into the
 * same key byte consumed by its original keyboard path. */
static int mouse_list_key(Game *g, int key, int x0, int x1, int y0,
                          int row_height, int row_count, int first_key) {
    if (key != INPUT_MOUSE_CLICK) return key;
    int row = game_mouse_row(g, x0, x1, y0, row_height, row_count);
    return row >= 0 ? first_key + row : -1;
}

/*
 * MW_PORT tags map native subsystems back to WORLD.C/WORLD.ASM. Original
 * combat and magic are highly fragmented, so one native function may cover
 * several tiny original routines. PORT_STATUS.md is the authoritative
 * coverage matrix; untagged functions here are implementation helpers.
 */

/* ══════════════════════════════════════════════════════════════════════
   Weapon stats table — extracted from WORLD.EXE DS:0x1C0 (7 bytes/entry)
   MW_PORT: select_weapon (0x04538) and weapon effects 0x117EF..0x11DA5.
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
   MW_PORT: func_09185/func_091B1/func_091CD and the combat type records.
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
    /* 112-113: native deep-dungeon quest bosses.  These deliberately use
       values above 255 to exercise the widened monster stat model. */
    {"Violet Abyss King",275,180,310,140,190,150,300,375,375,1,300,320},
    {"Prismatic World King",420,260,480,220,280,108,500,500,500,1,420,450},
    /* 114-133: native deep-floor generations.  Each group enters for 75
       floors and overlaps the next group for 25, giving floors 251-500 a
       changing roster instead of extending the original 8-bit bands. */
    {"Azure Ogre",       110, 55,125, 70, 75, 70, 99,251,325,0,120,110},
    {"Crimson Werewolf", 115, 65,135, 75, 95, 65, 99,251,325,0,125,115},
    {"Jade Swordwraith", 130, 60,145,110,110, 75,100,251,325,0,135,130},
    {"Ashen Kobald",     105, 50,130, 80,120, 60, 99,251,325,0,120,125},

    {"Violet Orc Warden",150, 75,165,120,125, 85,100,301,375,0,155,150},
    {"Emerald Deep Dwarf",180,70,155,145,105, 90,100,301,375,0,165,160},
    {"Sapphire War Knight",200,80,180,160,120,105,100,301,375,0,180,175},
    {"Blood Ape",        145, 95,175,110,140, 80, 99,301,375,0,165,160},

    {"Violet Unicorn",   190, 90,205,150,170,100,100,351,425,0,200,195},
    {"Storm Titan",      240,120,230,180,145,125,100,351,425,0,225,220},
    {"Abyss Giant",      260,130,245,190,135,130,100,351,425,0,240,235},
    {"Golden Devourer",  220,150,235,160,165,115,100,351,425,0,230,225},

    {"Crimson Death Mask",260,110,260,205,190,105,100,401,475,0,260,255},
    {"Frost Skeleton",   245,130,270,210,210,100,100,401,475,0,270,265},
    {"Plague Zombie",    260,145,280,220,175,115,100,401,475,0,275,270},
    {"Violet Wraith",    285,120,300,235,225,110,100,401,475,0,290,285},

    {"Obsidian Mummy",   310,155,325,250,210,115,100,451,500,0,315,310},
    {"Astral Vampire",   330,145,350,270,245,110,100,451,500,0,335,330},
    {"Blood Medusa",     350,170,365,285,260,120,100,451,500,0,350,345},
    {"Void Demon",       380,190,400,310,275,125,100,451,500,0,380,375},

    /* 134-173: ascended-floor variants.  Ten overlapping generations carry
       the native progression from floor 501 through floor 1000. */
    {"Cobalt Gargoyle",  410,205,440,330,300,135,100,501,575,0,420,410},
    {"Ash Titan",        440,230,460,345,270,150,100,501,575,0,445,435},
    {"Venom Unicorn",    400,215,450,320,340,130,100,501,575,0,430,425},
    {"Scarlet Dragonkin",430,250,475,335,315,145,100,501,575,0,455,450},

    {"Runic Stone Lord", 480,245,500,380,300,160,100,551,625,0,480,475},
    {"Umbral Devourer",  455,280,510,350,345,150,100,551,625,0,490,485},
    {"Glacial Vampire",  470,235,525,370,380,145,100,551,625,0,500,495},
    {"Ember Medusa",     490,270,540,390,360,155,100,551,625,0,515,510},

    {"Crystal War Golem",540,290,570,430,330,180,100,601,675,0,545,540},
    {"Plague Wyrm",      510,310,585,400,390,165,100,601,675,0,560,555},
    {"Storm Reaper",     525,275,600,420,420,160,100,601,675,0,575,570},
    {"Void Centaur",     555,300,620,440,400,175,100,601,675,0,590,585},

    {"Molten Ogre",      590,330,640,470,370,190,100,651,725,0,610,605},
    {"Spectral Werewolf",570,315,655,450,445,175,100,651,725,0,625,620},
    {"Obsidian Knight",  630,340,680,500,410,205,100,651,725,0,650,645},
    {"Emerald Hydra",    605,365,695,475,430,200,100,651,725,0,665,660},

    {"Astral Mummy Lord",660,350,720,525,425,215,100,701,775,0,690,685},
    {"Crimson Lich",     640,385,740,505,470,200,100,701,775,0,710,705},
    {"Sapphire Demon",   690,400,760,540,455,225,100,701,775,0,730,725},
    {"Golden Behemoth",  720,430,780,560,405,240,100,701,775,0,750,745},

    {"Rift Stalker",     730,410,800,575,520,210,100,751,825,0,770,765},
    {"Frost Ape",        750,450,820,590,480,235,100,751,825,0,790,785},
    {"Jade Death Mask",  770,420,840,610,540,220,100,751,825,0,810,805},
    {"Solar Wraith",     790,440,860,630,560,225,100,751,825,0,830,825},

    {"Ebon Titan",       830,470,890,660,500,255,100,801,875,0,850,845},
    {"Prismatic Basilisk",810,490,910,640,570,240,100,801,875,0,870,865},
    {"Blood Warlock",    850,460,930,680,600,230,100,801,875,0,890,885},
    {"Celestial Giant",  880,510,950,700,520,270,100,801,875,0,910,905},

    {"Abyssal Dragon",   910,540,980,730,590,275,100,851,925,0,935,930},
    {"Chrono Knight",    930,500,1000,750,640,250,100,851,925,0,955,950},
    {"Viridian Reaver",  900,525,1020,720,670,245,100,851,925,0,975,970},
    {"Starlight Medusa", 940,555,1040,760,650,265,100,851,925,0,995,990},

    {"Void Colossus",    1000,580,1080,800,610,290,100,901,975,0,1020,1015},
    {"Scarlet Vampire Lord",970,560,1100,780,700,260,100,901,975,0,1040,1035},
    {"Storm Demon",      1020,600,1130,820,680,285,100,901,975,0,1070,1065},
    {"Crystal Doom",     1050,620,1150,840,660,300,100,901,975,0,1090,1085},

    {"Eternity Wraith",  1080,610,1180,860,750,275,100,951,1000,0,1120,1115},
    {"Radiant Titan",    1150,650,1210,900,690,320,100,951,1000,0,1150,1145},
    {"Umbral Dragonking",1120,680,1240,880,730,310,100,951,1000,0,1180,1175},
    {"Chaos Incarnate",  1200,720,1280,940,780,340,100,951,1000,0,1220,1215},

    /* 174-177: ascended quest bosses, inserted only on their milestone
       floors and never admitted to the random spawn pool. */
    {"Cobalt Rift Tyrant",600,350,680,470,430,230,400,625,625,1,620,650},
    {"Crimson Star Eater",850,500,920,650,600,300,500,750,750,1,870,900},
    {"Viridian Eternity Dragon",1150,680,1240,860,760,380,600,875,875,1,1180,1210},
    {"Radiant Moraff Ascendant",1500,900,1650,1100,980,520,700,1000,1000,1,1550,1600},
};

/* ══════════════════════════════════════════════════════════════════════
   Armor defense values
   MW_PORT: armor data used by combat_attack and the armor command in
   func_0F6E5.
   ══════════════════════════════════════════════════════════════════════ */

/* Index 7 is the quest-only Ogre armor.  In the DOS data it deliberately
   reads the 14 at the boundary immediately following the seven shop rows. */
static const int armor_defense[] = { 0, 2, 4, 6, 9, 12, 16, 14 };

/* ══════════════════════════════════════════════════════════════════════
   Battle spell data
   MW_PORT: spell_menu, combat_event, cast_spell and func_10E9A..func_11DA5.
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
    /* L1 */ {BS_SLEEP,0,0},        {BS_DAMAGE_SCALE,2,2},   {BS_BUFF_PROTECT,1,0},
    /* L2 */ {BS_BUFF_SLOW,0,0},    {BS_BUFF_STR,0,0},       {BS_DAMAGE_FIXED,25,0},
    /* L3 */ {BS_DAMAGE_SCALE,4,4}, {BS_DAMAGE_FIXED,50,0},  {BS_BUFF_SPD,0,0},
    /* L4 */ {BS_GO_AWAY,0,0},      {BS_RELOCATE,0,0},       {BS_POWER_WEAPON,1,0},
    /* L5 */ {BS_DAMAGE_RANGE,75,175},{BS_BUFF_PROTECT,2,0},  {BS_RESIST_POISON,0,0},
    /* L6 */ {BS_DAMAGE_MULTI,4,8}, {BS_SHOCK_125,0,0},      {BS_ANTI_COLD,0,0},
    /* L7 */ {BS_DAMAGE_RANGE,125,225},{BS_PASS_WALL,0,0},    {BS_ANTI_FIRE,0,0},
    /* L8 */ {BS_DAMAGE_MULTI,7,11},{BS_BUFF_PROTECT,3,0},    {BS_POWER_WEAPON,2,0},
    /* L9 */ {BS_HOLD,0,0},         {BS_DRAIN,0,0},          {BS_SHOCK_300,0,0},
    /* L10*/ {BS_DAMAGE_RANGE,200,500},{BS_AUTOKILL,0,0},     {BS_POWER_WEAPON,3,0},
};

/* Priest battle spells (30 entries, matching priest_names order) */
static const BattleSpellDef priest_spells[30] = {
    /* L1 */ {BS_SLEEP,0,0},        {BS_BUFF_PROTECT,1,0},   {BS_BUFF_STR,0,0},
    /* L2 */ {BS_RESIST_POISON,0,0},{BS_BUFF_SPD,0,0},       {BS_HEAL_FIXED,20,0},
    /* L3 */ {BS_RESIST_DISEASE,0,0},{BS_RELOCATE,0,0},      {BS_BUFF_SLOW,0,0},
    /* L4 */ {BS_ANTI_COLD,0,0},    {BS_GO_AWAY,0,0},        {BS_POWER_WEAPON,1,0},
    /* L5 */ {BS_BUFF_PROTECT,2,0}, {BS_ANTI_FIRE,0,0},      {BS_PASS_WALL,0,0},
    /* L6 */ {BS_RESIST_DRAIN,0,0}, {BS_DRAIN,0,0},          {BS_HEAL_FIXED,50,0},
    /* L7 */ {BS_HOLD,0,0},         {BS_POWER_WEAPON,2,0},   {BS_SHOCK_125,0,0},
    /* L8 */ {BS_BUFF_PROTECT,3,0}, {BS_DAMAGE_RANGE,125,225},{BS_DAMAGE_MULTI,4,8},
    /* L9 */ {BS_AUTOKILL,0,0},     {BS_POWER_WEAPON,3,0},   {BS_BUFF_STR_SPD,0,0},
    /* L10*/ {BS_BUFF_PROTECT,4,0}, {BS_HEAL_ALL,0,0},       {BS_SHOCK_300,0,0},
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

static const char *permanent_spell_names[30] = {
    "ENCHANT WEAPON LEVEL 1", "EXTRA HEALTH POINT", "WRITE SCROLL TO LEVEL 3",
    "ENCHANT ARMOR LEVEL 1", "EXTRA 3 HEALTH POINTS", "ENCHANT WAND LEVEL 3",
    "ENCHANT WEAPON LEVEL 2", "EXTRA 5 HEALTH POINTS", "ENCHANT RING LEVEL 1",
    "ENCHANT ARMOR LEVEL 2", "ANTI-MAGIC RING LEVEL 1", "WRITE SCROLL - LEVEL 10",
    "ENCHANT WEAPON LEVEL 3", "ENCHANT RING LEVEL 2", "BODY ARMOR LEVEL 1",
    "ENCHANT ARMOR LEVEL 3", "ANTI-MAGIC RING LEVEL 2", "ENCHANT WAND LEVEL 8",
    "ENCHANT RING LEVEL 3", "ANTI-MAGIC RING LEVEL 3", "BODY ARMOR LEVEL 2",
    "ENCHANT WEAPON LEVEL 4", "ENCHANT ARMOR LEVEL 4", "ENCHANT WAND ANY LEVEL",
    "PERMANENT FEATHER", "ANTI-MAGIC RING LEVEL 5", "EXTRA 25 HEALTH POINTS",
    "PERMANENT INVISIBILITY", "YOUTH", "BODY ARMOR LEVEL 4"
};

static const char *preparation_spell_names[30] = {
    "ENCHANT ARMOR LEVEL 1", "ENCHANT WEAPON LEVEL 1", "LITTLE CURE",
    "ENCHANT WEAPON LEVEL 2", "RELOCATE", "DETECT LEVEL",
    "CURE", "ENCHANT ARMOR LEVEL 2", "STRENGTH",
    "ENCHANT WEAPON LEVEL 3", "AGILITY", "DESCEND",
    "ASCEND", "DETECT POSITION", "FEATHER",
    "BIG CURE", "DOUBLE ASCEND", "ENCHANT WEAPON LEVEL 4",
    "INVISIBILITY", "ENCHANT ARMOR LEVEL 3", "FAST MOVE",
    "SUPER STRENGTH", "ENCHANT WEAPON LEVEL 5", "MAJOR DESCEND",
    "SUPER AGILITY", "CURE POISON", "HEAL ALL WOUNDS",
    "MAJOR ASCEND", "CURE DISEASE", "ENCHANT ARMOR LEVEL 4"
};

static const char *const *spell_names_for_category(int category) {
    if (category == SPELL_CAT_PERMANENT) return permanent_spell_names;
    if (category == SPELL_CAT_PREPARATION) return preparation_spell_names;
    if (category == SPELL_CAT_WIZARD) return wiz_spell_names;
    return priest_spell_names;
}

const char *combat_spell_name(int category, int index) {
    if (category < SPELL_CAT_PERMANENT || category > SPELL_CAT_PRIEST ||
        index < 0 || index >= 30)
        return "UNKNOWN SPELL";
    return spell_names_for_category(category)[index];
}

static void dec_u16(u16 *value) {
    if (*value) --*value;
}

/* MW_PORT: WORLD func_0CA5F, func_0CDDD, func_0D2C9, plus duration/reset
 * work performed by spell-effect helpers 0x10E9A..0x11DA5. */
void character_clear_battle_effects(Character *p) {
    if (p->eff_battle_str) {
        unsigned remove = ((unsigned)p->eff_battle_str + 59) / 60 * 7;
        p->stat_str = p->stat_str >= remove ? (u16)(p->stat_str - remove) : 0;
        p->eff_battle_str = 0;
    }
    if (p->eff_battle_spd) {
        unsigned remove = ((unsigned)p->eff_battle_spd + 59) / 60 * 7;
        p->stat_agi = p->stat_agi >= remove ? (u16)(p->stat_agi - remove) : 0;
        p->eff_battle_spd = 0;
    }
    p->eff_slow_mon = 0;
    p->eff_pwr_weapon = 0;
    p->eff_pwr_wpn_turns = 0;
    p->eff_protect_lv = 0;
    p->eff_protect_turns = 0;
    p->eff_resist_poison = 0;
    p->eff_resist_disease = 0;
    p->eff_anti_cold = 0;
    p->eff_anti_fire = 0;
    p->eff_resist_drain = 0;
    p->eff_stop_monster = 0;
    p->eff_hold_monster = 0;
}

void character_clear_town_effects(Character *p) {
    mw_set_enchant_wpn_spell(p, 0);
    mw_set_armor_plus(p, 0);
    p->eff_fast_move = 0;
    if (p->eff_feather == 1) p->eff_feather = 0;
    if (p->eff_invisible == 1) p->eff_invisible = 0;
    if (p->eff_str_bonus) {
        p->stat_str = p->stat_str >= 5 ? (u16)(p->stat_str - 5) : 0;
        p->eff_str_bonus = 0;
    }
    if (p->eff_agi_bonus) {
        p->stat_agi = p->stat_agi >= 5 ? (u16)(p->stat_agi - 5) : 0;
        p->eff_agi_bonus = 0;
    }
    if (p->eff_super_str) {
        p->stat_str = p->stat_str >= 10 ? (u16)(p->stat_str - 10) : 0;
        p->eff_super_str = 0;
    }
    if (p->eff_super_agi) {
        p->stat_agi = p->stat_agi >= 10 ? (u16)(p->stat_agi - 10) : 0;
        p->eff_super_agi = 0;
    }
}

void character_tick_effects(Game *g, Character *p) {
    if (p->eff_battle_str && --p->eff_battle_str % 60 == 0)
        p->stat_str = p->stat_str >= 7 ? (u16)(p->stat_str - 7) : 0;
    if (p->eff_battle_spd && --p->eff_battle_spd % 60 == 0)
        p->stat_agi = p->stat_agi >= 7 ? (u16)(p->stat_agi - 7) : 0;
    dec_u16(&p->eff_slow_mon);
    if (p->eff_pwr_wpn_turns && --p->eff_pwr_wpn_turns == 0)
        p->eff_pwr_weapon = 0;
    if (p->eff_protect_turns && --p->eff_protect_turns == 0)
        p->eff_protect_lv = 0;
    /* The resistance counters freeze an already-active condition.  Once the
       resistance wears off, the saved poison/disease countdown resumes. */
    if (p->poisoned_turns && !p->eff_resist_poison) {
        if (--p->poisoned_turns == 0) {
            if (p->stat_str > 1) --p->stat_str;
            p->poisoned_turns = 450;
        }
    }
    if (p->diseased_turns && !p->eff_resist_disease) {
        if (--p->diseased_turns == 0) {
            if (p->stat_con > 1) --p->stat_con;
            p->diseased_turns = 450;
        }
    }
    dec_u16(&p->eff_resist_poison);
    dec_u16(&p->eff_resist_disease);
    dec_u16(&p->eff_anti_cold);
    dec_u16(&p->eff_anti_fire);
    dec_u16(&p->eff_resist_drain);
    dec_u16(&p->eff_stop_monster);
    dec_u16(&p->eff_hold_monster);
    if (p->ring_regen && p->hp_cur < p->hp_max) {
        unsigned healed = p->hp_cur + p->ring_regen;
        p->hp_cur = (u16)(healed > p->hp_max ? p->hp_max : healed);
    }
    (void)g;
}

/* ══════════════════════════════════════════════════════════════════════
   Helper: compute monster HP
   MW_PORT: func_091CD generation and func_09148 encounter initialization.
   HP = rand(0 .. hpF*level) + 1, boss gets +level*20.  Native-v3
   records hold 32-bit HP for the extended 1,000-floor progression.
   ══════════════════════════════════════════════════════════════════════ */

int combat_calc_monster_hp(const MonsterType *mt, int level) {
    int64_t base64 = (int64_t)mt->hpF * level;
    if (base64 > INT32_MAX - 1) base64 = INT32_MAX - 1;
    int base = (int)base64;
    if (base < 1) base = 1;
    int hp = base + 1;
    if (mt->boss && hp <= INT32_MAX - level * 20) hp += level * 20;
    if (hp < 1) hp = 1;
    return hp;
}

/* ══════════════════════════════════════════════════════════════════════
   Pick a random monster type valid for the current floor.
   MW_PORT: func_091CD, including ordinary-only records and floor ranges.

   WORLD's func_091CD only rolls ordinary types 0..103.  Types 104..111
   are the eight quest dragons and are inserted separately on their exact
   quest floors.  During startup WORLD also changes every ordinary maxL
   greater than 120 are open-ended, so they remain legal through floor 250;
   native progression tables take over below that point.
   ══════════════════════════════════════════════════════════════════════ */

int combat_monster_max_floor(int type_idx) {
    if (type_idx < 0 || type_idx >= MONSTER_TYPE_COUNT) return -1;
    const MonsterType *mt = &monster_types[type_idx];
    if (type_idx >= DEEP_MONSTER_FIRST) return mt->maxL;
    return mt->maxL > 120 ? MAX_DUNGEON_FLOOR : mt->maxL;
}

static int combat_monster_type_is_quest(int type_idx) {
    return (type_idx >= QUEST_MONSTER_FIRST &&
            type_idx < QUEST_MONSTER_FIRST + QUEST_MONSTER_COUNT) ||
           (type_idx >= ASCENDED_BOSS_FIRST &&
            type_idx < ASCENDED_BOSS_FIRST + ASCENDED_BOSS_COUNT);
}

int combat_monster_type_valid(int type_idx, int floor_depth) {
    if (type_idx < 0 || type_idx >= MONSTER_TYPE_COUNT || floor_depth < 1 ||
        !combat_monster_type_spawnable(type_idx) ||
        combat_monster_type_is_quest(type_idx)) return 0;
    /* The native lower dungeon has its own roster.  This also causes cached
       post-250 layers to migrate to the correct generation when read. */
    if (floor_depth > 250 && type_idx < DEEP_MONSTER_FIRST) return 0;
    if (floor_depth <= 250 && type_idx >= DEEP_MONSTER_FIRST) return 0;
    const MonsterType *mt = &monster_types[type_idx];
    int max_floor = combat_monster_max_floor(type_idx);
    return floor_depth >= mt->minL && floor_depth <= max_floor;
}

int combat_pick_monster_type(Game *g, int floor_depth) {
    if (floor_depth > 250) {
        int eligible[DEEP_MONSTER_COUNT];
        int count = 0;
        for (int type = DEEP_MONSTER_FIRST;
             type < DEEP_MONSTER_FIRST + DEEP_MONSTER_COUNT; type++)
            if (combat_monster_type_valid(type, floor_depth))
                eligible[count++] = type;
        if (count > 0)
            return eligible[game_rand(g) % count];
    }
    /* func_091CD starts with the player-specific member of the first nine
       humanoids, then independently gives a 1/2 chance to choose among
       those nine and a 1/3 chance to choose among all 104 ordinary types. */
    int candidate = ((g ? g->cur_player : 0) + 6) % 9;
    for (;;) {
        if (game_rand(g) * 2 / 0x8000 == 1)
            candidate = game_rand(g) * 9 / 0x8000;
        if (game_rand(g) * 3 / 0x8000 == 1)
            candidate = game_rand(g) * 104 / 0x8000;
        if (combat_monster_type_valid(candidate, floor_depth))
            return candidate;
    }
}

/* ══════════════════════════════════════════════════════════════════════
   Monster type -> WORLD.PIC artwork and replacement color.
   MW_PORT: func_073B5/func_07783 image-slot use and func_14B85 actor draw data.

   These are the final two bytes of each original 35-byte WORLD.EXE monster
   record.  WORLD.PIC has 50 virtual slots but the 1024x768 asset contains
   only the 37 slots selected by DS:11ED; the other 13 have no record in the
   file.  Color 17 in a base drawing is replaced by the record color at
   render time; 32 means transparent and gives the shadow dragons their
   silhouette artwork.  Keeping both tables complete is important: balls,
   puffballs and all seven dragon families deliberately reuse a shape while
   changing its color.
   ══════════════════════════════════════════════════════════════════════ */

static const u8 monster_pic_raw[MONSTER_TYPE_COUNT] = {
     0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,
    16,17,18,19,20,21,22,23,24,25,27,28,29,30,31,32,
    33,34,35,36,26,37,38,39,40,41,42,26,37,38,39,40,
    41,42,26,37,38,39,40,41,42,
    43,43,43,43,43,43,43,43,43,43,43,43,43,43,43,
    44,44,44,44,44,44,44,44,44,44,44,44,
    45,47,47,46, 45,47,47,46, 45,47,47,46, 45,47,47,46,
    45,47,47,46, 45,47,47,46, 45,47,47,46,
    46,46,
    /* Deep variants reuse one-off original silhouettes. */
     0, 1, 2, 3, 4, 5, 7, 8,13,15,16,22,23,27,28,30,
    31,34,33,35,
    /* Ascended variants cycle those silhouettes through new palettes. */
     7,15,13,35,16,22,34,33, 2,30,23, 5, 0, 1, 7,13,
    31,30,35,16,23, 8,23,34,15,22,33,22,46, 2, 4,30,
    16,34,35,30,31,15,46,14,
    /* Milestone bosses: titan, devil, dragonking, and Zeus silhouettes. */
    15,35,46,14
};

static const u8 monster_replace_color[MONSTER_TYPE_COUNT] = {
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0,
     4, 4, 4, 4, 4, 4, 4,
     0, 0, 0, 0, 0, 0, 0,
     9, 9, 9, 9, 9, 9, 9,
     1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,
     3, 6, 8, 4,15,14, 1,12,11, 7, 0,13,
     5, 5, 5, 5,  2, 2, 2, 2, 15,15,15,15,  9, 9, 9, 9,
     0, 0, 0, 0, 32,32,32,32, 10,10,10,10,
    13,15,
     9,12,10,15,13,10, 9,12,13,11, 5,14,12,11,10,13,
     8,11,12, 5,
    11, 8,10,12,14,13, 9, 6, 3,10,11, 5,12, 8, 7, 2,
    13,12, 9,14, 5,11,10, 6, 8,15,12, 9, 5,13,10,14,
     1,12,11,15, 8,14, 5,13,
    11,12,10,15
};

/* Unlike balls/dragons, the one-off art does not consistently use color 17.
 * A tint family remaps its other VGA hues while retaining gray highlights
 * and black/transparent pixels.  Zero leaves all original records exact. */
static const u8 deep_monster_tint[DEEP_MONSTER_COUNT] = {
     9,12,10,15,13,10, 9,12,13,11, 5,14,12,11,10,13,
     8,11,12, 5,
    11, 8,10,12,14,13, 9, 6, 3,10,11, 5,12, 8, 7, 2,
    13,12, 9,14, 5,11,10, 6, 8,15,12, 9, 5,13,10,14,
     1,12,11,15, 8,14, 5,13,
    11,12,10,15
};

/* Original DS:11ED virtual-slot load flags.  Slots zero and one (ladder and
 * trapdoor) are always read despite their flag values. */
static const u8 world_pic_slot_loaded[50] = {
    1,0,1,1,1,1,1,1,0,1,1,0,0,0,0,1,
    1,1,1,0,0,1,0,1,1,1,0,0,1,1,1,1,
    1,1,0,1,1,1,1,1,1,1,1,0,0,1,1,1,
    1,1
};

_Static_assert(MONSTER_TYPE_COUNT == BESTIARY_MONSTER_COUNT,
               "bestiary and combat monster counts must match");
_Static_assert(sizeof(monster_pic_raw) / sizeof(monster_pic_raw[0]) ==
               MONSTER_TYPE_COUNT, "monster picture table is incomplete");
_Static_assert(sizeof(monster_replace_color) /
               sizeof(monster_replace_color[0]) == MONSTER_TYPE_COUNT,
               "monster color table is incomplete");

int get_monster_pic_index_ext(int type_idx) {
    if (type_idx < 0 || type_idx >= MONSTER_TYPE_COUNT) return -1;
    int virtual_slot = (int)monster_pic_raw[type_idx] + 2;
    if (virtual_slot >= (int)(sizeof(world_pic_slot_loaded) /
                              sizeof(world_pic_slot_loaded[0])) ||
        !world_pic_slot_loaded[virtual_slot])
        return -1;
    int file_index = 1; /* slots zero and one occupy compact indices 0 and 1 */
    for (int slot = 2; slot <= virtual_slot; slot++)
        if (world_pic_slot_loaded[slot]) file_index++;
    return file_index;
}

int get_monster_color_ext(int type_idx) {
    if (type_idx < 0 || type_idx >= MONSTER_TYPE_COUNT) return -1;
    return monster_replace_color[type_idx];
}

int get_monster_tint_ext(int type_idx) {
    if (type_idx < DEEP_MONSTER_FIRST ||
        type_idx >= DEEP_MONSTER_FIRST + DEEP_MONSTER_COUNT)
        return 0;
    return deep_monster_tint[type_idx - DEEP_MONSTER_FIRST];
}

int combat_monster_type_spawnable(int type_idx) {
    /* The last eligibility test in WORLD func_091CD follows the monster's
     * raw picture byte through DS:11EF and rerolls when that virtual slot was
     * not loaded.  Those dormant table records are not potential monsters. */
    return type_idx >= 0 && type_idx < MONSTER_TYPE_COUNT &&
           get_monster_pic_index_ext(type_idx) >= 2;
}

/* ══════════════════════════════════════════════════════════════════════
   Initialize a combat encounter
   MW_PORT: func_09148 and opening portion of combat_encounter (0x018FE).
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
    u32 hp_roll = ((u32)game_rand(g) << 15) | (u32)game_rand(g);
    cs->monster_hp = (int)(hp_roll % (u32)hp_range) + 1;
    if (mt->boss) cs->monster_hp += cs->monster_level * 20;
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
   MW_PORT: select_weapon and weapon_effect/func_11B18.
   ══════════════════════════════════════════════════════════════════════ */

static int get_effective_weapon(const Character *p) {
    if (p->eff_pwr_weapon > 0 && p->eff_pwr_weapon <= 3)
        return 8 + p->eff_pwr_weapon;
    int wpn = p->equipped_weapon;
    if (wpn >= WEAPON_STAT_COUNT) wpn = 0;
    return wpn;
}

/* ══════════════════════════════════════════════════════════════════════
   Player melee attack — exact formula from WORLD.EXE disassembly
   MW_PORT: combat_attack (0x00AFB), func_0A7FF, func_0AC4F, func_0AD33,
   func_0AD6C, select_weapon, weapon_glow and weapon_effect.
   Lines 16888-17010 of WORLD.C

   Phase 1: Hit Score → number of swings
   Phase 2: Each swing does rand(0..maxDmg-1) damage
   Phase 3: Bonus damage (STR bonus, level bonus, low-level bonus)

   Returns damage dealt (0 = miss)
   ══════════════════════════════════════════════════════════════════════ */

int combat_player_attack(Game *g, CombatState *cs, Character *player) {
    const MonsterType *mt = &monster_types[cs->monster_type_idx];
    int base_wpn = player->equipped_weapon;
    if (base_wpn >= 8) base_wpn = 0;
    const WeaponStats *equipped = &weapon_stats[base_wpn];
    const WeaponStats *damage_weapon = &weapon_stats[get_effective_weapon(player)];

    /* Phase 1: Compute hit score */
    int hit_score = game_rand(g) % 80;  /* rand(0-79) */

    hit_score += player->level * 2;
    hit_score += player->stat_str;
    hit_score += player->stat_luck;
    hit_score += player->combat_bonus;
    /* Preparation and battle stat spells modify the saved stats themselves;
       their flag/counter fields only track how and when to reverse them. */
    /* Power Weapon replaces only the damage die.  Accuracy, enchantment,
       speed and weight continue to come from the equipped physical weapon. */
    hit_score += equipped->hit;
    hit_score += mw_gauntlet(player);

    /* Permanent weapon enchant for equipped weapon */
    hit_score += mw_weapon_enchant(player, base_wpn);

    /* Temporary Enchant Weapon spell */
    hit_score += mw_enchant_wpn_spell(player);

    /* Subtract monster defenses */
    hit_score -= cs->monster_level * 2; /* per-instance monster armor */
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
        int max_dmg = damage_weapon->maxDmg;
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
   MW_PORT: combat_event/combat_attack and special attack helpers in
   0x00D04..0x0446B, including poison, disease, breath and stat drains.
   Returns damage dealt to player (0 = miss)
   ══════════════════════════════════════════════════════════════════════ */

enum { BREATH_NONE, BREATH_FIRE, BREATH_COLD, BREATH_ACID,
       BREATH_DISEASE, BREATH_POISON };

static int monster_breath_type(int type) {
    if (type >= 84 && type <= 87) return BREATH_FIRE;
    if (type >= 88 && type <= 91) return BREATH_COLD;
    if (type >= 92 && type <= 95) return BREATH_ACID;
    if (type >= 96 && type <= 99) return BREATH_DISEASE;
    if (type >= 100 && type <= 103) return BREATH_POISON;
    /* Shadow/Red quest dragons have individual table entries. */
    if (type == 104) return BREATH_FIRE;
    if (type == 105) return BREATH_POISON;
    if (type == 106) return BREATH_DISEASE;
    if (type == 107) return BREATH_ACID;
    if (type == 108) return BREATH_COLD;
    if (type == 109) return BREATH_POISON;
    if (type == 110) return BREATH_DISEASE;
    if (type == 111) return BREATH_ACID;
    if (type == 112) return BREATH_COLD;
    if (type == 113) return BREATH_FIRE;
    /* Native deep variants retain the normal breath/resistance mechanics,
       but distribute them across the five new generations. */
    switch (type) {
    case 114: return BREATH_COLD;     /* Azure Ogre */
    case 115: return BREATH_DISEASE;  /* Crimson Werewolf */
    case 117: return BREATH_POISON;   /* Ashen Kobald */
    case 118: return BREATH_ACID;     /* Violet Orc Warden */
    case 121: return BREATH_FIRE;     /* Blood Ape */
    case 123: return BREATH_COLD;     /* Storm Titan */
    case 124: return BREATH_ACID;     /* Abyss Giant */
    case 126: return BREATH_DISEASE;  /* Crimson Death Mask */
    case 127: return BREATH_COLD;     /* Frost Skeleton */
    case 128: return BREATH_DISEASE;  /* Plague Zombie */
    case 130: return BREATH_POISON;   /* Obsidian Mummy */
    case 132: return BREATH_POISON;   /* Blood Medusa */
    case 133: return BREATH_FIRE;     /* Void Demon */
    case 134: return BREATH_COLD;     /* Cobalt Gargoyle */
    case 136: return BREATH_POISON;   /* Venom Unicorn */
    case 137: return BREATH_FIRE;     /* Scarlet Dragonkin */
    case 138: return BREATH_ACID;     /* Runic Stone Lord */
    case 139: return BREATH_DISEASE;  /* Umbral Devourer */
    case 140: return BREATH_COLD;     /* Glacial Vampire */
    case 141: return BREATH_FIRE;     /* Ember Medusa */
    case 142: return BREATH_ACID;     /* Crystal War Golem */
    case 143: return BREATH_DISEASE;  /* Plague Wyrm */
    case 144: return BREATH_COLD;     /* Storm Reaper */
    case 145: return BREATH_POISON;   /* Void Centaur */
    case 146: return BREATH_FIRE;     /* Molten Ogre */
    case 147: return BREATH_COLD;     /* Spectral Werewolf */
    case 149: return BREATH_POISON;   /* Emerald Hydra */
    case 150: return BREATH_DISEASE;  /* Astral Mummy Lord */
    case 151: return BREATH_POISON;   /* Crimson Lich */
    case 152: return BREATH_FIRE;     /* Sapphire Demon */
    case 154: return BREATH_POISON;   /* Rift Stalker */
    case 155: return BREATH_COLD;     /* Frost Ape */
    case 157: return BREATH_FIRE;     /* Solar Wraith */
    case 158: return BREATH_ACID;     /* Ebon Titan */
    case 159: return BREATH_POISON;   /* Prismatic Basilisk */
    case 160: return BREATH_DISEASE;  /* Blood Warlock */
    case 162: return BREATH_FIRE;     /* Abyssal Dragon */
    case 164: return BREATH_POISON;   /* Viridian Reaver */
    case 165: return BREATH_COLD;     /* Starlight Medusa */
    case 166: return BREATH_ACID;     /* Void Colossus */
    case 167: return BREATH_DISEASE;  /* Scarlet Vampire Lord */
    case 168: return BREATH_COLD;     /* Storm Demon */
    case 169: return BREATH_ACID;     /* Crystal Doom */
    case 170: return BREATH_POISON;   /* Eternity Wraith */
    case 171: return BREATH_FIRE;     /* Radiant Titan */
    case 172: return BREATH_COLD;     /* Umbral Dragonking */
    case 173: return BREATH_DISEASE;  /* Chaos Incarnate */
    case 174: return BREATH_COLD;     /* Cobalt Rift Tyrant */
    case 175: return BREATH_FIRE;     /* Crimson Star Eater */
    case 176: return BREATH_POISON;   /* Viridian Eternity Dragon */
    case 177: return BREATH_ACID;     /* Radiant Moraff Ascendant */
    default: break;
    }
    return BREATH_NONE;
}

int combat_monster_drain_amount(int type) {
    static const u8 undead_drain[10] = {1,1,1,1,1,2,1,2,2,3};
    if (type >= 26 && type <= 35) return undead_drain[type - 26];
    if (type >= 108 && type <= 111) return type - 107;
    if (type == 112) return 5;
    if (type == 113) return 7;
    if (type == 116 || type == 120) return 2; /* enchanted weapons */
    if (type == 126 || type == 127) return 3;
    if (type == 129) return 4;
    if (type == 131) return 5;
    if (type == 140 || type == 144) return 6;
    if (type == 147) return 7;
    if (type == 150 || type == 151 || type == 154) return 8;
    if (type == 156) return 9;
    if (type == 157 || type == 160) return 10;
    if (type == 163 || type == 165 || type == 167) return 12;
    if (type == 170 || type == 173) return 15;
    if (type == 174) return 12;
    if (type == 175) return 15;
    if (type == 176) return 18;
    if (type == 177) return 25;
    return 0;
}

static int combat_monster_special(Game *g, CombatState *cs, Character *p,
                                  int physical_damage) {
    int type = cs->monster_type_idx;
    int breath = monster_breath_type(type);

    if (type >= 72 && type <= 83) {
        static const char *stat_name[6] = {
            "STRENGTH", "INTELLIGENCE", "WISDOM",
            "CONSTITUTION", "AGILITY", "LUCK"
        };
        u16 *stats[6] = { &p->stat_str, &p->stat_int, &p->stat_wis,
                          &p->stat_con, &p->stat_agi, &p->stat_luck };
        int slot = (type - 72) % 6;
        int raises = type < 78;
        if (raises) ++*stats[slot];
        else if (*stats[slot]) --*stats[slot];
        snprintf(cs->special_message, sizeof(cs->special_message),
                 "THE PUFFBALL %s YOUR %s!", raises ? "RAISES" : "DRAINS",
                 stat_name[slot]);
        cs->special_used = 1;
        cs->monster_hp = 0; /* Original puffballs vanish after discharging. */
        cs->active = 0;
        return 0;
    }

    int drain_amount = combat_monster_drain_amount(type);
    if (drain_amount) {
        cs->special_used = 1;
        if (p->eff_resist_drain) {
            snprintf(cs->special_message, sizeof(cs->special_message),
                     "YOU RESIST THE LEVEL DRAIN!");
        } else {
            p->level = p->level > drain_amount ?
                       (u16)(p->level - drain_amount) : 1;
            snprintf(cs->special_message, sizeof(cs->special_message),
                     "THE %s DRAINS %d LEVEL%s!", monster_types[type].name,
                     drain_amount, drain_amount == 1 ? "" : "S");
        }
    }

    /* The black and green animal families use the table's special byte,
       rather than a breath byte, to inflict their persistent condition. */
    if (type >= 43 && type <= 49 && !p->eff_resist_poison) {
        if (!p->poisoned_turns) p->poisoned_turns = 450;
        cs->special_used = 1;
        snprintf(cs->special_message, sizeof(cs->special_message),
                 "THE %s POISONS YOU!", monster_types[type].name);
    } else if (type >= 50 && type <= 56 && !p->eff_resist_disease) {
        if (!p->diseased_turns) p->diseased_turns = 450;
        cs->special_used = 1;
        snprintf(cs->special_message, sizeof(cs->special_message),
                 "THE %s DISEASES YOU!", monster_types[type].name);
    }

    /* WORLD calls a two-way random roll before taking the breath branch. */
    if (breath && (game_rand(g) & 1) == 0) {
        static const char *attack_name[] = {
            "", "FIRE", "FREEZING COLD", "ACID", "GREEN PHLEGM", "BLACK SLIME"
        };
        int level = cs->monster_level > 0 ? cs->monster_level : 1;
        int damage = level + game_rand(g) % level;
        int resisted = (breath == BREATH_FIRE && p->eff_anti_fire) ||
                       (breath == BREATH_COLD && p->eff_anti_cold) ||
                       (breath == BREATH_DISEASE && p->eff_resist_disease) ||
                       (breath == BREATH_POISON && p->eff_resist_poison);
        if (resisted) damage /= 2;
        if (breath == BREATH_DISEASE && !p->eff_resist_disease)
            p->diseased_turns = 450;
        if (breath == BREATH_POISON && !p->eff_resist_poison)
            p->poisoned_turns = 450;
        if (breath == BREATH_ACID && p->equipped_armor > 0) {
            int armor = p->equipped_armor;
            mw_set_armor_enchant(p, armor, 0);
            if (p->armor_inventory[armor]) --p->armor_inventory[armor];
            p->equipped_armor = 0;
            snprintf(cs->special_message, sizeof(cs->special_message),
                     "ACID AND MELEE DO %d; YOUR ARMOR DISSOLVES!",
                     damage + physical_damage);
        } else {
            snprintf(cs->special_message, sizeof(cs->special_message),
                     "%s BREATH AND MELEE DO %d POINTS%s", attack_name[breath],
                     damage + physical_damage,
                     resisted ? " (RESISTED)" : "");
        }
        cs->special_used = 1;
        return damage;
    }
    return 0;
}

int combat_monster_attack(Game *g, CombatState *cs, Character *player) {
    const MonsterType *mt = &monster_types[cs->monster_type_idx];

    if (cs->monster_asleep > 0 || cs->monster_held > 0 || cs->monster_stopped > 0)
        return 0;

    cs->special_used = 0;
    cs->special_message[0] = '\0';
    if (cs->monster_type_idx >= 72 && cs->monster_type_idx <= 83) {
        combat_monster_special(g, cs, player, 0);
        return 0;
    }

    /* Monster hit score */
    int hit_score = game_rand(g) % 80;
    hit_score += cs->monster_level * 2;
    hit_score += mt->atk;

    /* Subtract player defenses */
    hit_score -= player->level * 2;
    hit_score -= player->stat_agi;
    hit_score -= player->stat_luck;
    hit_score -= player->combat_bonus;
    int armor = player->equipped_armor < ARMOR_STAT_COUNT ? player->equipped_armor : 0;
    hit_score -= armor_defense[armor];
    hit_score -= mw_armor_enchant(player, armor);
    hit_score -= mw_body_armor_plus(player);
    hit_score -= mw_ring_prot_plus(player);
    hit_score -= mw_armor_plus(player);
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
    if (total_damage > 1)
        total_damage += combat_monster_special(g, cs, player, total_damage);
    return total_damage;
}

/* Loaded weight gives monsters additional attack time in WORLD func_0E8C8.
   Keep the monster's native multi-hit calculation intact and repeat that
   attack for each encumbrance opportunity. */
static int combat_monster_attack_weighted(Game *g, CombatState *cs,
                                          Character *player,
                                          int *opportunities) {
    int count = game_weight_monster_turns(player);
    int total = 0;
    int executed = 0;
    int had_special = 0;
    char last_special[sizeof(cs->special_message)];
    last_special[0] = '\0';
    for (int i = 0; i < count; i++) {
        total += combat_monster_attack(g, cs, player);
        executed++;
        if (cs->special_used) {
            had_special = 1;
            snprintf(last_special, sizeof(last_special), "%s",
                     cs->special_message);
        }
        if (!cs->active || cs->monster_hp <= 0 ||
            total >= (int)player->hp_cur)
            break;
    }
    if (had_special) {
        cs->special_used = 1;
        snprintf(cs->special_message, sizeof(cs->special_message), "%s",
                 last_special);
    }
    if (opportunities) *opportunities = executed;
    return total;
}

/* Native God Mode keeps the original hit/special rolls intact so combat
 * behavior remains observable, but makes the final HP transaction atomic. */
static int combat_apply_player_damage(Game *g, Character *player, int damage) {
    if (damage <= 0 || g->cheat_god_mode) return 0;
    if (damage >= (int)player->hp_cur)
        player->hp_cur = 0;
    else
        player->hp_cur -= (u16)damage;
    return damage;
}

/* ══════════════════════════════════════════════════════════════════════
   Cast a battle spell — returns damage dealt or special code
   MW_PORT: spell_menu (0x00436), combat_event (0x005DB), cast_spell
   (0x0079A), and effect routines 0x10E9A..0x11DA5.
   -1 = instant kill, -2 = immune, -3 = effect only (sleep/hold/buff), 0 = miss/fail
   ══════════════════════════════════════════════════════════════════════ */

static int apply_battle_spell(Game *g, CombatState *cs, Character *player,
                              const BattleSpellDef *sd, int spell_level) {
    const MonsterType *mt = &monster_types[cs->monster_type_idx];

    switch (sd->type) {
    case BS_NONE:
        return 0;

    case BS_SLEEP:
        if (mt->imm >= 100 || cs->monster_level > (int)player->level * 2) return -2;
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
        if ((int)player->level * 4 < cs->monster_level) return 0;
        cs->fled = 1;
        return -3;

    case BS_HOLD:
        if (mt->imm >= 100) return -2;
        cs->monster_held = 15;
        player->eff_hold_monster = 15;
        return -3;

    case BS_STOP:
        if (mt->imm >= 100) return -2;
        cs->monster_stopped = 10;
        player->eff_stop_monster = 10;
        return -3;

    case BS_DRAIN: {
        int drain = player->stat_wis;
        if (drain < 1) drain = 1;
        cs->monster_level -= drain;
        if (cs->monster_level < 1) return -1;
        return -3;
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
        if (player->eff_battle_str <= 0xFFFF - 60) {
            player->stat_str += 7;
            player->eff_battle_str += 60;
        }
        return -3;

    case BS_BUFF_SPD:
        if (player->eff_battle_spd <= 0xFFFF - 60) {
            player->stat_agi += 7;
            player->eff_battle_spd += 60;
        }
        return -3;

    case BS_BUFF_STR_SPD:
        if (player->eff_battle_str <= 0xFFFF - 60) {
            player->stat_str += 7;
            player->eff_battle_str += 60;
        }
        if (player->eff_battle_spd <= 0xFFFF - 60) {
            player->stat_agi += 7;
            player->eff_battle_spd += 60;
        }
        return -3;

    case BS_BUFF_SLOW:
        if (player->eff_slow_mon <= 0xFFFF - 60) player->eff_slow_mon += 60;
        return -3;

    case BS_BUFF_PROTECT:
        if (player->eff_protect_lv == (u8)sd->param1 &&
            player->eff_protect_turns <= 0xFFFF - 60)
            player->eff_protect_turns += 60;
        else {
            player->eff_protect_lv = (u8)sd->param1;
            player->eff_protect_turns = 60;
        }
        return -3;

    case BS_RESIST_POISON:
        if (player->eff_resist_poison <= 0xFFFF - 60) player->eff_resist_poison += 60;
        return -3;

    case BS_RESIST_DISEASE:
        if (player->eff_resist_disease <= 0xFFFF - 60) player->eff_resist_disease += 60;
        return -3;

    case BS_ANTI_COLD:
        if (player->eff_anti_cold <= 0xFFFF - 60) player->eff_anti_cold += 60;
        return -3;

    case BS_ANTI_FIRE:
        if (player->eff_anti_fire <= 0xFFFF - 60) player->eff_anti_fire += 60;
        return -3;

    case BS_RESIST_DRAIN:
        if (player->eff_resist_drain <= 0xFFFF - 60) player->eff_resist_drain += 60;
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
        if (player->eff_pwr_weapon == (u8)sd->param1 &&
            player->eff_pwr_wpn_turns <= 0xFFFF - 60)
            player->eff_pwr_wpn_turns += 60;
        else {
            player->eff_pwr_weapon = (u8)sd->param1;
            player->eff_pwr_wpn_turns = 60;
        }
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
        input_wait_any_key(&g->input);
        return -1;
    }

    video_draw_text_scaled(v, 8, LOGICAL_H - fh - 4, "SELECT SPELL (ESC TO CANCEL)...", 15, 3, 4);
    video_present(v);

    while (1) {
        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input)) return -1;
        if (key == 0x1B) return -1;
        key = mouse_list_key(g, key, 0, LOGICAL_W,
                             2 + fh + 4, row_h, num_available, 'A');

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
        key = mouse_list_key(g, key, 0, LOGICAL_W,
                             4 + fh * 2, fh, 2, '1');
        if (key == '1') { category = 2; break; }
        if (key == '2') { category = 3; break; }
    }

    int spell_idx = select_battle_spell(g, player, category);
    if (spell_idx < 0) return 0;

    /* Check SP cost */
    int spell_level = spell_idx / 3 + 1;
    float sp_cost = (float)spell_level;

    if (!g->cheat_god_mode && player->sp_cur < sp_cost) {
        video_clear(v, 0);
        video_draw_text(v, 8, 4, "NOT ENOUGH SPELL POINTS!", 12);
        char line[64];
        snprintf(line, sizeof(line), "NEED: %d   HAVE: %.0f", spell_level, player->sp_cur);
        video_draw_text(v, 8, 4 + fh, line, 7);
        video_draw_text(v, 8, 4 + fh * 3, "HIT ANY KEY...", 15);
        video_present(v);
        input_wait_any_key(&g->input);
        return 0;
    }

    /* Spend SP */
    if (!g->cheat_god_mode) player->sp_cur -= sp_cost;

    /* Apply spell */
    const BattleSpellDef *sd = (category == 2) ? &wiz_spells[spell_idx] : &priest_spells[spell_idx];
    return apply_battle_spell(g, cs, player, sd, spell_level);
}

/* ══════════════════════════════════════════════════════════════════════
   Main combat loop
   MW_PORT: combat_encounter (0x018FE), func_0EAE9, and combat branch of
   func_0F6E5. The caller keeps the fight inside the four viewports.
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

        if (key == INPUT_MOUSE_CLICK) {
            key = game_mouse_command_key(g);
            if (!key && game_mouse_view_direction(g) == combat_dir)
                key = 'f';
            if (!key) continue;
        }

        if (key == INPUT_GOD_TOGGLE) {
            g->cheat_god_mode = !g->cheat_god_mode;
            snprintf(msg1, sizeof(msg1), "GOD MODE: %s",
                     g->cheat_god_mode ? "ON" : "OFF");
            snprintf(msg2, sizeof(msg2), "%s",
                     g->cheat_god_mode ?
                     "DAMAGE AND SPELL COSTS DISABLED" :
                     "NORMAL DAMAGE AND SPELL COSTS RESTORED");
            mw_audio_play(&g->audio, MW_SFX_UI);
            continue;
        }

        if (key == 'f' || key == 'F') {
            /* ── Player attacks ── */
            mw_audio_play(&g->audio, MW_SFX_ATTACK);
            int dmg = combat_player_attack(g, cs, player);
            if (dmg <= 0) {
                snprintf(msg1, sizeof(msg1), "YOU MISS");
            } else {
                cs->monster_hp -= dmg;
                snprintf(msg1, sizeof(msg1), "YOU DO %d POINTS", dmg);
                if (cs->monster_hp < 1) {
                    snprintf(msg2, sizeof(msg2), "THE %s IS DEAD!", mt->name);

                    snprintf(msg3, sizeof(msg3), "SEARCHING THE REMAINS...");

                    draw_combat_screen(g, cs, player, msg1, msg2, msg3);
                    input_wait_any_key(&g->input);
                    cs->active = 0;
                    break;
                }
            }

            /* ── Monster counter-attacks ── */
            if (cs->active) {
                int mon_strikes = 1;
                int mon_dmg = combat_monster_attack_weighted(
                    g, cs, player, &mon_strikes);
                mon_dmg = combat_apply_player_damage(g, player, mon_dmg);
                if (mon_dmg <= 0) {
                    if (cs->special_used) {
                        snprintf(msg2, sizeof(msg2), "%s", cs->special_message);
                    } else if (cs->monster_asleep > 0 || cs->monster_held > 0 || cs->monster_stopped > 0) {
                        snprintf(msg2, sizeof(msg2), "THE %s CANNOT ATTACK!", mt->name);
                    } else {
                        snprintf(msg2, sizeof(msg2), "%s MISSES", dir_name[combat_dir]);
                    }
                } else {
                    mw_audio_play(&g->audio, MW_SFX_HURT);
                    snprintf(msg2, sizeof(msg2), "%s", cs->special_used ?
                             cs->special_message : dir_name[combat_dir]);
                    if (!cs->special_used) {
                        if (mon_strikes > 1)
                            snprintf(msg2, sizeof(msg2),
                                     "%s STRIKES %d TIMES FOR %d POINTS",
                                     dir_name[combat_dir], mon_strikes,
                                     mon_dmg);
                        else
                            snprintf(msg2, sizeof(msg2), "%s DOES %d POINTS",
                                     dir_name[combat_dir], mon_dmg);
                    }

                    if (player->hp_cur == 0) {
                        player->hp_cur = 0;
                        character_clear_battle_effects(player);
                        snprintf(msg3, sizeof(msg3), "YOU HAVE BEEN KILLED!");
                        draw_combat_screen(g, cs, player, msg1, msg2, msg3);
                        input_wait_any_key(&g->input);
                        cs->active = 0;
                        break;
                    }
                }
            }

            /* Decrement status effect timers */
            if (cs->monster_asleep > 0) cs->monster_asleep--;
            if (cs->monster_held > 0) cs->monster_held--;
            if (cs->monster_stopped > 0) cs->monster_stopped--;

        } else if (key == 'c' || key == 'C' || key == 'i' || key == 'I') {
            /* ── Cast spell ── */
            int result = (key == 'i' || key == 'I') ?
                         cmd_use_item(g, player, cs) :
                         cmd_cast_spell_menu(g, player, cs);

            if (result != 0) mw_audio_play(&g->audio, MW_SFX_MAGIC);

            if (result == 0) {
                /* Cancelled or failed */
                continue;
            } else if (result == -1) {
                /* Instant kill */
                cs->monster_hp = 0;
                snprintf(msg1, sizeof(msg1), "THE %s IS DESTROYED!", mt->name);
                snprintf(msg2, sizeof(msg2), "SEARCHING THE REMAINS...");
                draw_combat_screen(g, cs, player, msg1, msg2, "");
                input_wait_any_key(&g->input);
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
                input_wait_any_key(&g->input);
                cs->active = 0;
                break;
            } else if (result == -5) {
                snprintf(msg1, sizeof(msg1), "THE SPELL FAILS TO AFFECT THE %s!", mt->name);
            } else if (result > 0) {
                /* Damage */
                cs->monster_hp -= result;
                snprintf(msg1, sizeof(msg1), "YOUR SPELL HITS FOR %d DAMAGE!", result);
                if (cs->monster_hp <= 0) {
                    snprintf(msg2, sizeof(msg2), "THE %s IS DEAD!", mt->name);
                    snprintf(msg3, sizeof(msg3), "SEARCHING THE REMAINS...");
                    draw_combat_screen(g, cs, player, msg1, msg2, msg3);
                    input_wait_any_key(&g->input);
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
                    input_wait_any_key(&g->input);
                    cs->active = 0;
                    break;
                }

                int mon_strikes = 1;
                int mon_dmg = combat_monster_attack_weighted(
                    g, cs, player, &mon_strikes);
                mon_dmg = combat_apply_player_damage(g, player, mon_dmg);
                if (mon_dmg <= 0) {
                    if (cs->special_used) {
                        snprintf(msg2, sizeof(msg2), "%s", cs->special_message);
                    } else if (cs->monster_asleep > 0 || cs->monster_held > 0 || cs->monster_stopped > 0) {
                        snprintf(msg2, sizeof(msg2), "THE %s CANNOT ATTACK!", mt->name);
                    } else {
                        snprintf(msg2, sizeof(msg2), "%s MISSES", dir_name[combat_dir]);
                    }
                } else {
                    mw_audio_play(&g->audio, MW_SFX_HURT);
                    if (cs->special_used)
                        snprintf(msg2, sizeof(msg2), "%s", cs->special_message);
                    else if (mon_strikes > 1)
                        snprintf(msg2, sizeof(msg2),
                                 "%s STRIKES %d TIMES FOR %d POINTS",
                                 dir_name[combat_dir], mon_strikes, mon_dmg);
                    else
                        snprintf(msg2, sizeof(msg2), "%s DOES %d POINTS",
                                 dir_name[combat_dir], mon_dmg);
                    if (player->hp_cur == 0) {
                        player->hp_cur = 0;
                        character_clear_battle_effects(player);
                        snprintf(msg3, sizeof(msg3), "YOU HAVE BEEN KILLED!");
                        draw_combat_screen(g, cs, player, msg1, msg2, msg3);
                        input_wait_any_key(&g->input);
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
                input_wait_any_key(&g->input);
                cs->active = 0;
                break;
            } else {
                snprintf(msg1, sizeof(msg1), "YOU FAIL TO ESCAPE!");
                /* Monster gets a free attack */
                int mon_strikes = 1;
                int mon_dmg = combat_monster_attack_weighted(
                    g, cs, player, &mon_strikes);
                mon_dmg = combat_apply_player_damage(g, player, mon_dmg);
                if (mon_dmg > 0) {
                    mw_audio_play(&g->audio, MW_SFX_HURT);
                    if (cs->special_used)
                        snprintf(msg2, sizeof(msg2), "%s", cs->special_message);
                    else if (mon_strikes > 1)
                        snprintf(msg2, sizeof(msg2),
                                 "%s STRIKES %d TIMES FOR %d POINTS",
                                 dir_name[combat_dir], mon_strikes, mon_dmg);
                    else
                        snprintf(msg2, sizeof(msg2), "%s DOES %d POINTS",
                                 dir_name[combat_dir], mon_dmg);
                    if (player->hp_cur == 0) {
                        player->hp_cur = 0;
                        character_clear_battle_effects(player);
                        snprintf(msg3, sizeof(msg3), "YOU HAVE BEEN KILLED!");
                        draw_combat_screen(g, cs, player, msg1, msg2, msg3);
                        input_wait_any_key(&g->input);
                        cs->active = 0;
                        break;
                    }
                } else if (cs->special_used) {
                    snprintf(msg2, sizeof(msg2), "%s", cs->special_message);
                }
            }
        }
        character_tick_effects(g, player);
        /* Any other key: redraw, no action */
    }
}

/* ══════════════════════════════════════════════════════════════════════
   Weapon selection command (W key from exploration)
   MW_PORT: select_weapon (0x04538) and its inventory helpers.
   ══════════════════════════════════════════════════════════════════════ */

void cmd_weapons(Game *g, Character *player) {
    Video *v = &g->video;
    int fh = v->font_char_h + 2;
    char line[128];

    video_clear(v, 0);
    int y = 4;

    video_draw_text(v, 8, y, "SELECT WEAPON:", 14);
    y += fh + 4;
    int option_y = y;

    for (int i = 0; i < 8; i++) {
        int owned = (i == 0) || player->weapon_inventory[i] != 0;
        u8 color = (i == player->equipped_weapon) ? 15 : 7;
        if (!owned) color = 8;
        snprintf(line, sizeof(line), "%d) %-12s DMG:%d HIT:+%d  %s",
                 i + 1, weapon_stats[i].name, weapon_stats[i].maxDmg,
                 weapon_stats[i].hit, owned ? "" : "NOT OWNED");
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
        key = mouse_list_key(g, key, 0, LOGICAL_W,
                             option_y, fh, 8, '1');
        if (key >= '1' && key <= '8') {
            int w = key - '1';
            int allowed = 0;
            switch (player->class_id) {
            case CLASS_FIGHTER: allowed = 1; break;
            case CLASS_WORSHIPPER:
            case CLASS_MONK: allowed = (w == 0); break;
            case CLASS_WIZARD:
            case CLASS_SAGE: allowed = (w == 0 || w == 1 || w == 4); break;
            default: allowed = (w != 7); break;
            }
            if ((w == 0 || player->weapon_inventory[w]) && allowed) {
                player->equipped_weapon = (u8)w;
                return;
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
   Armor selection command (A key from exploration)
   MW_PORT: armor branch of func_0F6E5 and the original armor records.
   ══════════════════════════════════════════════════════════════════════ */

static const char *armor_type_names[] = {
    "SKIN", "LEATHER", "CHAIN", "SCALE", "PLATE", "FIELD PLATE", "TITANIUM", "OGRE"
};

void cmd_armor(Game *g, Character *player) {
    Video *v = &g->video;
    int fh = v->font_char_h + 2;
    char line[128];

    video_clear(v, 0);
    int y = 4;

    video_draw_text(v, 8, y, "SELECT ARMOR:", 14);
    y += fh + 4;
    int option_y = y;

    for (int i = 0; i < ARMOR_STAT_COUNT; i++) {
        int owned = (i == 0) || player->armor_inventory[i] != 0;
        u8 color = (i == player->equipped_armor) ? 15 : (owned ? 7 : 8);
        snprintf(line, sizeof(line), "%d) %-12s DEF:%d +%d %s", i + 1,
                 armor_type_names[i], armor_defense[i], mw_armor_enchant(player, i),
                 owned ? "" : "NOT OWNED");
        video_draw_text(v, 8, y, line, color);
        if (i == player->equipped_armor) {
            video_draw_text(v, 350, y, "<-- EQUIPPED", 14);
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
        key = mouse_list_key(g, key, 0, LOGICAL_W,
                             option_y, fh, ARMOR_STAT_COUNT, '1');
        if (key >= '1' && key <= '8') {
            int armor = key - '1';
            int max_armor = 7;
            if (player->class_id == CLASS_WORSHIPPER || player->class_id == CLASS_WIZARD)
                max_armor = 0;
            else if (player->class_id == CLASS_MONK || player->class_id == CLASS_SAGE)
                max_armor = 1;
            if ((armor == 0 || player->armor_inventory[armor]) && armor <= max_armor) {
                player->equipped_armor = (u8)armor;
                return;
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
   Preparation spell casting (C key from exploration mode)
   MW_PORT: spell_menu/cast_spell and effect routines 0x10E9A..0x11DA5.
   Handles non-combat spells: cure, teleport, buff, etc.
   ══════════════════════════════════════════════════════════════════════ */

static int prep_vertical_max_floor(Game *g, int spell_index) {
    const GameTraversalRules *rules = game_traversal_rules(g);
    return spell_index == 11 ? rules->prep_descend_max_floor :
                               rules->prep_ascend_max_floor;
}

static int prep_vertical_depth_allowed(Game *g, int spell_index, int floor) {
    const GameTraversalRules *rules = game_traversal_rules(g);
    switch (spell_index) {
    case 11: /* Descend */
        return floor <= rules->prep_descend_max_floor &&
               floor < rules->max_floor;
    case 12: /* Ascend */
    case 16: /* Double Ascend */
    case 27: /* Major Ascend */
        return floor <= rules->prep_ascend_max_floor;
    case 23: /* Major Descend */
        return floor <= rules->prep_ascend_max_floor &&
               floor < rules->max_floor;
    default:
        return 1;
    }
}

static void prep_vertical_failure_message(Game *g, int spell_index,
                                          char *message,
                                          size_t message_size) {
    snprintf(message, message_size,
             "THAT SPELL DOES NOT WORK PAST DUNGEON LEVEL %d!",
             prep_vertical_max_floor(g, spell_index));
}

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
        input_wait_any_key(&g->input);
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

            if (!g->cheat_god_mode && player->sp_cur < sp_cost) {
                video_clear(v, 0);
                video_draw_text(v, 8, 4, "NOT ENOUGH SPELL POINTS!", 12);
                snprintf(line, sizeof(line), "NEED: %d   HAVE: %.0f", spell_level, player->sp_cur);
                video_draw_text(v, 8, 4 + fh, line, 7);
                video_draw_text(v, 8, 4 + fh * 3, "HIT ANY KEY...", 15);
                video_present(v);
                input_wait_any_key(&g->input);
                return;
            }

            if (!g->cheat_god_mode) player->sp_cur -= sp_cost;

            /* Apply preparation spell effect */
            video_clear(v, 0);
            switch (spell_idx) {
            case 0:  /* Enchant Armor L1 */
                if (mw_armor_plus(player) < 1) mw_set_armor_plus(player, 1);
                video_draw_text(v, 8, 4, "YOUR ARMOR IS ENCHANTED +1!", 10);
                break;
            case 1:  /* Enchant Weapon L1 */
                if (mw_enchant_wpn_spell(player) < 1) mw_set_enchant_wpn_spell(player, 1);
                video_draw_text(v, 8, 4, "YOUR WEAPON IS ENCHANTED +1!", 10);
                break;
            case 2:  /* Little Cure */
                player->hp_cur += 5;
                if (player->hp_cur > player->hp_max) player->hp_cur = player->hp_max;
                video_draw_text(v, 8, 4, "YOU ARE HEALED FOR 5 HP!", 10);
                break;
            case 3:  /* Enchant Weapon L2 */
                if (mw_enchant_wpn_spell(player) < 2) mw_set_enchant_wpn_spell(player, 2);
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
                if (mw_armor_plus(player) < 2) mw_set_armor_plus(player, 2);
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
                if (mw_enchant_wpn_spell(player) < 3) mw_set_enchant_wpn_spell(player, 3);
                video_draw_text(v, 8, 4, "YOUR WEAPON IS ENCHANTED +3!", 10);
                break;
            case 11: /* Descend */
                if (prep_vertical_depth_allowed(g, 11, g->cur_floor)) {
                    game_change_floor(g, player, g->cur_floor + 1);
                    game_relocate(g, player);
                    video_draw_text(v, 8, 4, "YOU DESCEND ONE LEVEL!", 10);
                } else {
                    prep_vertical_failure_message(g, 11, line, sizeof(line));
                    video_draw_text(v, 8, 4, line, 12);
                }
                break;
            case 12: /* Ascend */
                if (!prep_vertical_depth_allowed(g, 12, g->cur_floor)) {
                    prep_vertical_failure_message(g, 12, line, sizeof(line));
                    video_draw_text(v, 8, 4, line, 12);
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
                if (player->eff_feather != 100) player->eff_feather = 1;
                video_draw_text(v, 8, 4, "YOU FEEL LIGHT AS A FEATHER!", 10);
                break;
            case 15: /* Big Cure */
                player->hp_cur += 50;
                if (player->hp_cur > player->hp_max) player->hp_cur = player->hp_max;
                video_draw_text(v, 8, 4, "YOU ARE HEALED FOR 50 HP!", 10);
                break;
            case 16: /* Double Ascend */
                if (!prep_vertical_depth_allowed(g, 16, g->cur_floor)) {
                    prep_vertical_failure_message(g, 16, line, sizeof(line));
                    video_draw_text(v, 8, 4, line, 12);
                } else if (g->cur_floor > 0) {
                    game_change_floor(g, player, g->cur_floor - 2);
                    game_relocate(g, player);
                    video_draw_text(v, 8, 4, "YOU ASCEND TWO LEVELS!", 10);
                } else video_draw_text(v, 8, 4, "YOU CANNOT ASCEND ANY FARTHER!", 12);
                break;
            case 17: /* Enchant Weapon L4 */
                if (mw_enchant_wpn_spell(player) < 4) mw_set_enchant_wpn_spell(player, 4);
                video_draw_text(v, 8, 4, "YOUR WEAPON IS ENCHANTED +4!", 10);
                break;
            case 18: /* Invisibility */
                player->eff_invisible = 60;
                video_draw_text(v, 8, 4, "YOU BECOME INVISIBLE!", 10);
                break;
            case 19: /* Enchant Armor L3 */
                if (mw_armor_plus(player) < 3) mw_set_armor_plus(player, 3);
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
                if (mw_enchant_wpn_spell(player) < 5) mw_set_enchant_wpn_spell(player, 5);
                video_draw_text(v, 8, 4, "YOUR WEAPON IS ENCHANTED +5!", 10);
                break;
            case 23: /* Major Descend */
                if (prep_vertical_depth_allowed(g, 23, g->cur_floor)) {
                    int target = g->cur_floor + 25;
                    int cap = game_traversal_rules(g)->prep_major_descend_cap;
                    if (target > cap) target = cap;
                    game_change_floor(g, player, target);
                    game_relocate(g, player);
                    video_draw_text(v, 8, 4, "YOU DESCEND TWENTY-FIVE LEVELS!", 10);
                } else {
                    prep_vertical_failure_message(g, 23, line, sizeof(line));
                    video_draw_text(v, 8, 4, line, 12);
                }
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
                if (!prep_vertical_depth_allowed(g, 27, g->cur_floor)) {
                    prep_vertical_failure_message(g, 27, line, sizeof(line));
                    video_draw_text(v, 8, 4, line, 12);
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
                if (mw_armor_plus(player) < 4) mw_set_armor_plus(player, 4);
                video_draw_text(v, 8, 4, "YOUR ARMOR IS ENCHANTED +4!", 10);
                break;
            default:
                snprintf(line, sizeof(line), "SPELL CAST: %s", prep_spell_names[spell_idx]);
                video_draw_text(v, 8, 4, line, 10);
                break;
            }

            video_draw_text(v, 8, LOGICAL_H - fh - 4, "HIT ANY KEY...", 15);
            video_present(v);
            input_wait_any_key(&g->input);
            return;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
   Unified original-style spell and magic-item interface

   MW_PORT: spell_menu, combat_event, cast_spell, func_10E9A..func_11156,
   func_1158A..func_11DA5, weapon_glow and weapon_effect.

   Books, scrolls, wands and papers deliberately converge here.  The old
   preparation-only routine above is retained for binary/source comparison,
   but all game commands use this engine.
   ══════════════════════════════════════════════════════════════════════ */

#define SPELL_PANE_W (0x2D3 * LOGICAL_W / 1600)
#define SPELL_PANE_H (0x1AE * LOGICAL_H / 1200)
#define SPELL_ROW_H  22
#define SPELL_SELECTOR_ROW_H 25
#define SPELL_SELECTOR_COL_2 (0x20C * LOGICAL_W / 1600)
#define SPELL_SELECTOR_COL_3 (0x430 * LOGICAL_W / 1600)

static void spell_draw_backdrop(Game *g, Character *p, CombatState *cs) {
    if (cs)
        game_draw_combat_overlay(g, p, cs->entity_index, cs->monster_type_idx,
                                 cs->monster_level, cs->monster_hp, "", "", "");
    else
        game_draw_exploration(g, p);
    video_fill_rect(&g->video, 0, 0, SPELL_PANE_W, SPELL_PANE_H, 0);
}

static void spell_line(Game *g, int row, const char *text, u8 color) {
    video_draw_text_scaled_xy(&g->video, 0, row * SPELL_ROW_H, text, color,
                              7, 6, 12, 17);
}

static void spell_selector_text(Game *g, int x, int row,
                                const char *text, u8 color) {
    video_draw_text_scaled_xy(&g->video, x, row * SPELL_SELECTOR_ROW_H,
                              text, color, 1, 1, 12, 17);
}

static void spell_draw_selector_backdrop(Game *g, Character *p,
                                         CombatState *cs) {
    if (cs)
        game_draw_combat_overlay(g, p, cs->entity_index, cs->monster_type_idx,
                                 cs->monster_level, cs->monster_hp, "", "", "");
    else
        game_draw_exploration(g, p);
    video_fill_rect(&g->video, 0, 0, LOGICAL_W, SPELL_PANE_H, 0);
}

static void spell_notice(Game *g, Character *p, CombatState *cs,
                         const char *line1, const char *line2) {
    spell_draw_backdrop(g, p, cs);
    spell_line(g, 0, line1, 12);
    if (line2 && *line2) spell_line(g, 2, line2, 15);
    spell_line(g, 10, "HIT ANY KEY...", 14);
    video_present(&g->video);
    input_wait_any_key(&g->input);
}

static int class_can_read_spellbook(const Character *p, int category) {
    if (p->class_id == CLASS_MONK) return 1;
    if (p->class_id == CLASS_FIGHTER) return 0;
    if (category <= SPELL_CAT_PREPARATION) return 1;
    if (category == SPELL_CAT_WIZARD)
        return p->class_id == CLASS_WIZARD || p->class_id == CLASS_SAGE ||
               p->class_id == CLASS_MAGE;
    return p->class_id == CLASS_WORSHIPPER || p->class_id == CLASS_PRIEST ||
           p->class_id == CLASS_SAGE;
}

static int spell_is_available(Character *p, int category, int index,
                              int source, int help) {
    if (help) return 1;
    if (source == 0) {
        if (!class_can_read_spellbook(p, category)) return 0;
        return p->class_id == CLASS_MONK || p->spells[category][index] != 0;
    }
    if (p->class_id == CLASS_FIGHTER && source != 3) return 0;
    if (source == 1) return p->scrolls[category][index] != 0;
    if (source == 2) return p->wands[category][index] != 0;
    return p->papers[category][index] != 0;
}

static char spell_hotkey_for_index(int index) {
    if (index < 0 || index >= 30) return '\0';
    return index < 26 ? (char)('A' + index) : (char)('1' + index - 26);
}

static int spell_index_from_hotkey(int key) {
    if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
    if (key >= 'A' && key <= 'Z') return key - 'A';
    if (key >= '1' && key <= '4') return 26 + key - '1';
    return -1;
}

static int select_spell_index(Game *g, Character *p, CombatState *cs,
                              int category, int source, int help,
                              int maximum_level) {
    char line[128];
    const char *const *names = spell_names_for_category(category);
    for (;;) {
        spell_draw_selector_backdrop(g, p, cs);
        spell_selector_text(g, 0, 0,
            help ? "PRESS A LETTER OR A NUMBER TO GET A DESCRIPTION:" :
            (source == 0 ?
             "SELECT A SPELL-SPELLS USE ONE SPELL POINT PER LEVEL:" :
             "SELECT A SPELL FROM THE FOLLOWING:"), 4);
        spell_selector_text(g, LOGICAL_W - 90, 0, "ESCAPE", 3);

        for (int level = 0; level < 10; level++) {
            for (int slot = 0; slot < 3; slot++) {
                int index = level * 3 + slot;
                int available = level < maximum_level &&
                    spell_is_available(p, category, index, source, help);
                char key = spell_hotkey_for_index(index);
                if (slot == 0)
                    snprintf(line, sizeof(line), "%d- %c)%s", level + 1,
                             key, available ? names[index] : "NOT YET FOUND");
                else
                    snprintf(line, sizeof(line), " %c)%s", key,
                             available ? names[index] : "NOT YET FOUND");
                spell_selector_text(g,
                    slot == 0 ? 0 :
                    (slot == 1 ? SPELL_SELECTOR_COL_2 : SPELL_SELECTOR_COL_3),
                    level + 1, line, 8);
            }
        }
        video_present(&g->video);
        int key = input_getch(&g->input);
        if (key == 0x1B || input_poll_quit(&g->input)) return -1;
        int index = -1;
        if (key == INPUT_MOUSE_CLICK) {
            int x, y;
            if (!game_mouse_click_logical(g, &x, &y)) continue;
            if (y < SPELL_SELECTOR_ROW_H && x >= LOGICAL_W - 120)
                return -1;
            int level = y / SPELL_SELECTOR_ROW_H - 1;
            int slot = x < SPELL_SELECTOR_COL_2 ? 0 :
                       (x < SPELL_SELECTOR_COL_3 ? 1 : 2);
            if (level >= 0 && level < 10) index = level * 3 + slot;
        } else {
            index = spell_index_from_hotkey(key);
        }
        if (index >= 0 && index / 3 < maximum_level &&
            spell_is_available(p, category, index, source, help))
            return index;
    }
}

static void append_text(char *dst, size_t size, const char *text) {
    size_t used = strlen(dst);
    if (used + 1 < size) snprintf(dst + used, size - used, "%s", text);
}

static void spell_help_text(Game *g, int category, int index,
                            char *out, size_t size) {
    int level = index / 3 + 1;
    const char *name = spell_names_for_category(category)[index];
    snprintf(out, size, "%s. LEVEL %d; COST %d SP. ", name, level, level);
    if (category == SPELL_CAT_PERMANENT) {
        append_text(out, size, "PERMANENT: CAST ONLY IN TOWN, TAKES ONE MONTH, SURVIVES INNS AND DEATH, AND A BOOK CAST PERMANENTLY LOWERS MAXIMUM SP. ");
        if (index == 2 || index == 11)
            append_text(out, size, "CREATES A ONE-USE SCROLL FOR A CHOSEN SPELL WITHOUT AN SP COST WHEN USED. ");
        else if (index == 5 || index == 17 || index == 23)
            append_text(out, size, "CREATES A WAND WITH FIVE NO-SP CHARGES OF A CHOSEN SPELL. ");
        else if (index == 1 || index == 4 || index == 7 || index == 26)
            append_text(out, size, "RAISES BOTH CURRENT AND MAXIMUM HEALTH; THE BONUS IS 1, 3, 5, OR 25 HP. ");
        else if (index == 24)
            append_text(out, size, "SETS PERMANENT FEATHER (VALUE 100): THE CASTER REMAINS WEIGHTLESS. ");
        else if (index == 27)
            append_text(out, size, "SETS PERMANENT INVISIBILITY (VALUE 100), REDUCING ENCOUNTERS. ");
        else if (index == 28)
            append_text(out, size, "REMOVES TEN YEARS OF AGE BUT DOES NOT RESTORE STATS ALREADY LOST TO AGE. ");
        else if (index == 14 || index == 20 || index == 29)
            append_text(out, size, "ADDS PERMANENT INNATE BODY DEFENSE THAT STACKS WITH PHYSICAL ARMOR. ");
        else if (index == 8 || index == 13 || index == 18)
            append_text(out, size, "UPGRADES THE RING OF PROTECTION; ITS PLUS IS SUBTRACTED FROM ENEMY HIT SCORE. ");
        else if (index == 10 || index == 16 || index == 19 || index == 25)
            append_text(out, size, "UPGRADES THE ANTI-MAGIC RING. THE ORIGINAL GAME STORES AND DISPLAYS IT BUT NEVER READS IT IN COMBAT. ");
        else
            append_text(out, size, "UPGRADES THE EQUIPPED ITEM ONLY IF THIS ENCHANTMENT IS BETTER; ITEM AND TEMPORARY ENCHANTS STACK. ");
    } else if (category == SPELL_CAT_PREPARATION) {
        append_text(out, size, "PREPARATION MAGIC TAKES THREE MINUTES OUTSIDE BATTLE; BUFFS LAST UNTIL AN INN REST. ");
        if (index == 2) append_text(out, size, "HEALS 1-20 HP, WITH A SMALL WISDOM BONUS. ");
        else if (index == 6) append_text(out, size, "HEALS 10-40 HP, WITH A SMALL WISDOM BONUS. ");
        else if (index == 15) append_text(out, size, "HEALS 20-90 HP, WITH A SMALL WISDOM BONUS. ");
        else if (index == 4) append_text(out, size, "MOVES THE CASTER TO A RANDOM SAFE SQUARE ON THE CURRENT FLOOR. ");
        else if (index == 11) snprintf(out + strlen(out), size - strlen(out),
             "MOVES DOWN ONE FLOOR TO A SAFE OPEN SPACE; FAILS PAST FLOOR %d. ",
             game_traversal_rules(g)->prep_descend_max_floor);
        else if (index == 12) snprintf(out + strlen(out), size - strlen(out),
             "MOVES UP ONE FLOOR TO A SAFE OPEN SPACE; FAILS PAST FLOOR %d. ",
             game_traversal_rules(g)->prep_ascend_max_floor);
        else if (index == 16) snprintf(out + strlen(out), size - strlen(out),
             "MOVES UP TWO FLOORS, CLAMPING AT TOWN; FAILS PAST FLOOR %d. ",
             game_traversal_rules(g)->prep_ascend_max_floor);
        else if (index == 23) snprintf(out + strlen(out), size - strlen(out),
             "MOVES DOWN TWENTY-FIVE FLOORS, NEVER PAST FLOOR %d, AND FAILS WHEN CAST PAST FLOOR %d. ",
             game_traversal_rules(g)->prep_major_descend_cap,
             game_traversal_rules(g)->prep_ascend_max_floor);
        else if (index == 27) snprintf(out + strlen(out), size - strlen(out),
             "MOVES UP TWENTY-FIVE FLOORS, CLAMPING AT TOWN; FAILS PAST FLOOR %d. ",
             game_traversal_rules(g)->prep_ascend_max_floor);
        else if (index == 8 || index == 21) append_text(out, size, "ADDS 5 OR 10 TO STRENGTH; THE BONUS IS REVERSED AT THE INN. ");
        else if (index == 10 || index == 24) append_text(out, size, "ADDS 5 OR 10 TO AGILITY; THE BONUS IS REVERSED AT THE INN. ");
        else if (index == 25 || index == 28) append_text(out, size, "REMOVES THE POISON OR DISEASE COUNTER IMMEDIATELY. ");
        else if (index == 26) append_text(out, size, "RESTORES CURRENT HEALTH TO MAXIMUM. ");
        else append_text(out, size, "THE ENCHANTMENT OR MOVEMENT/DEFENSIVE EFFECT IS APPLIED IMMEDIATELY. ");
    } else {
        const BattleSpellDef *sd = category == SPELL_CAT_WIZARD ? &wiz_spells[index] : &priest_spells[index];
        append_text(out, size, "BATTLE MAGIC; SELECTING IT USES ONE COMBAT ACTION. ");
        switch (sd->type) {
        case BS_DAMAGE_SCALE: snprintf(out + strlen(out), size - strlen(out),
             "DEALS CASTER LEVEL X %d PLUS %d DAMAGE. ", sd->param1, sd->param2); break;
        case BS_DAMAGE_FIXED: snprintf(out + strlen(out), size - strlen(out),
             "DEALS %d DAMAGE. ", sd->param1); break;
        case BS_DAMAGE_RANGE: snprintf(out + strlen(out), size - strlen(out),
             "DEALS %d-%d RANDOM DAMAGE. ", sd->param1, sd->param2); break;
        case BS_DAMAGE_MULTI: snprintf(out + strlen(out), size - strlen(out),
             "FIRES LEVEL+1 HITS, EACH FOR %d-%d DAMAGE. ", sd->param1, sd->param2); break;
        case BS_BUFF_STR: append_text(out, size, "ADDS 7 STR FOR EACH 60-TURN CAST; RECASTS STACK BOTH BONUS AND DURATION. "); break;
        case BS_BUFF_SPD: append_text(out, size, "ADDS 7 AGILITY FOR EACH 60-TURN CAST; RECASTS STACK BOTH BONUS AND DURATION. "); break;
        case BS_BUFF_STR_SPD: append_text(out, size, "ADDS 7 STR AND AGILITY FOR 60 TURNS. "); break;
        case BS_BUFF_PROTECT: snprintf(out + strlen(out), size - strlen(out),
             "PROTECTION TIER %d SUBTRACTS TIER-SQUARED X 2 FROM ENEMY HIT SCORE FOR 60 TURNS. ", sd->param1); break;
        case BS_POWER_WEAPON: snprintf(out + strlen(out), size - strlen(out),
             "CONJURES POWER WEAPON %d FOR 60 TURNS; SAME-TIER RECASTS EXTEND IT. ", sd->param1); break;
        case BS_SLEEP: append_text(out, size, "PUTS A NON-IMMUNE MONSTER TO SLEEP FOR ABOUT 10 TURNS. "); break;
        case BS_HOLD: append_text(out, size, "PARALYZES A NON-IMMUNE MONSTER FOR ABOUT 15 TURNS. "); break;
        case BS_STOP: append_text(out, size, "STOPS A NON-IMMUNE MONSTER FOR ABOUT 10 TURNS. "); break;
        case BS_DRAIN: append_text(out, size, "PERMANENTLY LOWERS MONSTER LEVEL BY THE CASTER'S WISDOM; LEVEL BELOW ONE DESTROYS IT. "); break;
        case BS_AUTOKILL: append_text(out, size, "OPPOSED LEVEL, INTELLIGENCE, WISDOM, AND MONSTER SAVE ROLLS DETERMINE INSTANT DEATH. "); break;
        case BS_RELOCATE: append_text(out, size, "RELOCATES THE CASTER ON THIS FLOOR AND ENDS CONTACT. "); break;
        case BS_PASS_WALL: append_text(out, size, "PHASES THROUGH AN ADJACENT WALL AND ENDS CONTACT. "); break;
        case BS_GO_AWAY: append_text(out, size, "SENDS A NON-IMMUNE MONSTER AWAY. "); break;
        case BS_RESIST_POISON: append_text(out, size, "ADDS 60 TURNS OF POISON RESISTANCE; CASTS STACK DURATION. "); break;
        case BS_RESIST_DISEASE: append_text(out, size, "ADDS 60 TURNS OF DISEASE RESISTANCE; CASTS STACK DURATION. "); break;
        case BS_ANTI_COLD: append_text(out, size, "ADDS 60 TURNS OF COLD RESISTANCE AND HALVES COLD BREATH. "); break;
        case BS_ANTI_FIRE: append_text(out, size, "ADDS 60 TURNS OF FIRE RESISTANCE AND HALVES FIRE BREATH. "); break;
        case BS_RESIST_DRAIN: append_text(out, size, "ADDS 60 TURNS OF COMPLETE LEVEL-DRAIN PROTECTION. "); break;
        case BS_HEAL_FIXED: snprintf(out + strlen(out), size - strlen(out),
             "RESTORES %d HP DURING COMBAT. ", sd->param1); break;
        case BS_HEAL_ALL: append_text(out, size, "RESTORES CURRENT HP TO MAXIMUM DURING COMBAT. "); break;
        case BS_BUFF_SLOW: append_text(out, size, "SLOWS THE MONSTER FOR 60 TURNS; THREE OF FOUR ATTACKS TAKE AN INITIATIVE PENALTY. "); break;
        case BS_SHOCK_125: append_text(out, size, "DEALS 125 DAMAGE. "); break;
        case BS_SHOCK_300: append_text(out, size, "DEALS 300 DAMAGE. "); break;
        default: append_text(out, size, "APPLIES ITS LISTED EFFECT IMMEDIATELY. "); break;
        }
    }
}

static void show_spell_help(Game *g, Character *p, CombatState *cs,
                            int category, int index) {
    char text[1024], words[1024], lines[48][32];
    int count = 0;
    spell_help_text(g, category, index, text, sizeof(text));
    snprintf(words, sizeof(words), "%s", text);
    char *word = strtok(words, " ");
    lines[0][0] = '\0';
    while (word && count < 48) {
        size_t have = strlen(lines[count]);
        size_t need = strlen(word);
        if (have && have + need + 1 > 29) {
            count++;
            if (count >= 48) break;
            lines[count][0] = '\0';
            have = 0;
        }
        if (have) append_text(lines[count], sizeof(lines[count]), " ");
        append_text(lines[count], sizeof(lines[count]), word);
        word = strtok(NULL, " ");
    }
    count++;
    for (int first = 0; first < count; first += 10) {
        spell_draw_backdrop(g, p, cs);
        for (int row = 0; row < 10 && first + row < count; row++)
            spell_line(g, row, lines[first + row], 7);
        spell_line(g, 10, first + 10 < count ? "ANY KEY: MORE  ESC: EXIT" : "HIT ANY KEY TO RETURN", 14);
        video_present(&g->video);
        int key = input_wait_any_key(&g->input);
        if (key == 0x1B) break;
    }
}

static int select_created_spell(Game *g, Character *p, CombatState *cs,
                                int maximum_level, int *category, int *index) {
    spell_draw_backdrop(g, p, cs);
    spell_line(g, 0, "PUT WHICH SPELL IN ITEM?", 8);
    spell_line(g, 2, "1) PERMANENT", 7);
    spell_line(g, 4, "2) PREPARATION", 7);
    spell_line(g, 6, "3) WIZARD BATTLE", 7);
    spell_line(g, 8, "4) PRIEST BATTLE", 7);
    video_present(&g->video);
    int key = input_getch(&g->input);
    if (key == INPUT_MOUSE_CLICK) {
        int x, y;
        key = -1;
        if (game_mouse_click_logical(g, &x, &y) && x < SPELL_PANE_W) {
            int row = y / SPELL_ROW_H;
            if (row >= 2 && row <= 8 && !(row & 1))
                key = '1' + (row - 2) / 2;
        }
    }
    if (key < '1' || key > '4') return 0;
    *category = key - '1';
    *index = select_spell_index(g, p, cs, *category, 0, 1, maximum_level);
    return *index >= 0;
}

static int apply_permanent_spell(Game *g, Character *p, CombatState *cs,
                                 int index, char *message, size_t message_size) {
    int value = 0, target_category, target_index;
    if (index == 0 || index == 6 || index == 12 || index == 21) {
        value = index == 0 ? 1 : index == 6 ? 2 : index == 12 ? 3 : 4;
        int w = p->equipped_weapon < 8 ? p->equipped_weapon : 0;
        if (mw_weapon_enchant(p, w) >= value) return 0;
        mw_set_weapon_enchant(p, w, value);
        snprintf(message, message_size, "WEAPON PERMANENTLY ENCHANTED +%d!", value);
    } else if (index == 3 || index == 9 || index == 15 || index == 22) {
        value = index == 3 ? 1 : index == 9 ? 2 : index == 15 ? 3 : 4;
        int armor = p->equipped_armor < 8 ? p->equipped_armor : 0;
        if (mw_armor_enchant(p, armor) >= value) return 0;
        mw_set_armor_enchant(p, armor, value);
        snprintf(message, message_size, "ARMOR PERMANENTLY ENCHANTED +%d!", value);
    } else if (index == 1 || index == 4 || index == 7 || index == 26) {
        value = index == 1 ? 1 : index == 4 ? 3 : index == 7 ? 5 : 25;
        p->hp_max = (u16)(p->hp_max + value);
        p->hp_cur = (u16)(p->hp_cur + value);
        snprintf(message, message_size, "MAXIMUM HEALTH INCREASED BY %d!", value);
    } else if (index == 2 || index == 11) {
        int max = index == 2 ? 3 : 10;
        if (!select_created_spell(g, p, cs, max, &target_category, &target_index)) return 0;
        if (p->scrolls[target_category][target_index] < 255)
            ++p->scrolls[target_category][target_index];
        snprintf(message, message_size, "THE SCROLL IS COMPLETE!");
    } else if (index == 5 || index == 17 || index == 23) {
        int max = index == 5 ? 3 : index == 17 ? 8 : 10;
        if (!select_created_spell(g, p, cs, max, &target_category, &target_index)) return 0;
        int charges = p->wands[target_category][target_index] + 5;
        p->wands[target_category][target_index] = (u8)(charges > 255 ? 255 : charges);
        snprintf(message, message_size, "THE WAND NOW HAS FIVE MORE CHARGES!");
    } else if (index == 8 || index == 13 || index == 18) {
        value = index == 8 ? 1 : index == 13 ? 2 : 3;
        if (mw_ring_prot_plus(p) >= value) return 0;
        mw_set_ring_prot_plus(p, value);
        snprintf(message, message_size, "RING OF PROTECTION IS NOW +%d!", value);
    } else if (index == 10 || index == 16 || index == 19 || index == 25) {
        value = index == 10 ? 1 : index == 16 ? 2 : index == 19 ? 3 : 5;
        if (p->antimagic_ring >= value) return 0;
        p->antimagic_ring = (u8)value;
        snprintf(message, message_size, "ANTI-MAGIC RING IS NOW +%d!", value);
    } else if (index == 14 || index == 20 || index == 29) {
        value = index == 14 ? 1 : index == 20 ? 2 : 4;
        if (mw_body_armor_plus(p) >= value) return 0;
        mw_set_body_armor_plus(p, value);
        snprintf(message, message_size, "BODY ARMOR IS NOW +%d!", value);
    } else if (index == 24) {
        if (p->eff_feather == 100) return 0;
        p->eff_feather = 100;
        snprintf(message, message_size, "YOU ARE PERMANENTLY WEIGHTLESS!");
    } else if (index == 27) {
        if (p->eff_invisible == 100) return 0;
        p->eff_invisible = 100;
        snprintf(message, message_size, "YOU ARE PERMANENTLY INVISIBLE!");
    } else if (index == 28) {
        const u32 ten_years = 315360000u;
        p->age = p->age > ten_years ? p->age - ten_years : 0;
        snprintf(message, message_size, "YOU BECOME TEN YEARS YOUNGER!");
    } else return 0;
    return 1;
}

static void heal_random(Game *g, Character *p, int low, int high, char *message,
                        size_t message_size) {
    int amount = low + game_rand(g) % (high - low + 1) + p->stat_wis / 10;
    int hp = p->hp_cur + amount;
    p->hp_cur = (u16)(hp > p->hp_max ? p->hp_max : hp);
    snprintf(message, message_size, "YOU ARE HEALED FOR %d POINTS!", amount);
}

static int apply_preparation_spell(Game *g, Character *p, int index,
                                   char *message, size_t message_size) {
    int target;
    switch (index) {
    case 0: if (mw_armor_plus(p) < 1) mw_set_armor_plus(p,1); snprintf(message,message_size,"ARMOR ENCHANTED +1 UNTIL THE INN!"); break;
    case 1: if (mw_enchant_wpn_spell(p) < 1) mw_set_enchant_wpn_spell(p,1); snprintf(message,message_size,"WEAPONS ENCHANTED +1 UNTIL THE INN!"); break;
    case 2: heal_random(g,p,1,20,message,message_size); break;
    case 3: if (mw_enchant_wpn_spell(p) < 2) mw_set_enchant_wpn_spell(p,2); snprintf(message,message_size,"WEAPONS ENCHANTED +2 UNTIL THE INN!"); break;
    case 4: if (!game_relocate(g,p)) return 0; snprintf(message,message_size,"YOU RELOCATE ON THIS LEVEL!"); break;
    case 5: snprintf(message,message_size,"YOU ARE ON DUNGEON LEVEL %d.",g->cur_floor); break;
    case 6: heal_random(g,p,10,40,message,message_size); break;
    case 7: if (mw_armor_plus(p) < 2) mw_set_armor_plus(p,2); snprintf(message,message_size,"ARMOR ENCHANTED +2 UNTIL THE INN!"); break;
    case 8: if (!p->eff_str_bonus) { p->stat_str += 5; p->eff_str_bonus=1; } snprintf(message,message_size,"STRENGTH INCREASED BY 5 UNTIL THE INN!"); break;
    case 9: if (mw_enchant_wpn_spell(p) < 3) mw_set_enchant_wpn_spell(p,3); snprintf(message,message_size,"WEAPONS ENCHANTED +3 UNTIL THE INN!"); break;
    case 10: if (!p->eff_agi_bonus) { p->stat_agi += 5; p->eff_agi_bonus=1; } snprintf(message,message_size,"AGILITY INCREASED BY 5 UNTIL THE INN!"); break;
    case 11:
        if (!prep_vertical_depth_allowed(g, index, g->cur_floor)) {
            prep_vertical_failure_message(g, index, message, message_size);
            return 0;
        }
        target = g->cur_floor + 1;
        if (!game_change_floor(g,p,target) || !game_relocate(g,p)) return 0;
        snprintf(message,message_size,"YOU DESCEND ONE LEVEL!");
        break;
    case 12:
        if (!prep_vertical_depth_allowed(g, index, g->cur_floor)) {
            prep_vertical_failure_message(g, index, message, message_size);
            return 0;
        }
        if (g->cur_floor <= 0) {
            snprintf(message, message_size, "YOU CANNOT ASCEND ANY FARTHER!");
            return 0;
        }
        target = g->cur_floor - 1;
        if (!game_change_floor(g,p,target) || !game_relocate(g,p)) return 0;
        snprintf(message,message_size,"YOU ASCEND ONE LEVEL!");
        break;
    case 13: snprintf(message,message_size,"YOUR POSITION IS X:%d Y:%d.",g->cur_x,g->cur_y); break;
    case 14: if (p->eff_feather != 100) p->eff_feather=1; snprintf(message,message_size,"FEATHER LASTS UNTIL THE INN!"); break;
    case 15: heal_random(g,p,20,90,message,message_size); break;
    case 16:
        if (!prep_vertical_depth_allowed(g, index, g->cur_floor)) {
            prep_vertical_failure_message(g, index, message, message_size);
            return 0;
        }
        if (g->cur_floor <= 0) {
            snprintf(message, message_size, "YOU CANNOT ASCEND ANY FARTHER!");
            return 0;
        }
        target = g->cur_floor - 2;
        if (target < 0) target = 0;
        if (!game_change_floor(g,p,target) || !game_relocate(g,p)) return 0;
        snprintf(message,message_size,"YOU ASCEND TWO LEVELS!");
        break;
    case 17: if(mw_enchant_wpn_spell(p)<4)mw_set_enchant_wpn_spell(p,4); snprintf(message,message_size,"WEAPONS ENCHANTED +4 UNTIL THE INN!"); break;
    case 18: if(p->eff_invisible!=100)p->eff_invisible=1; snprintf(message,message_size,"INVISIBILITY LASTS UNTIL THE INN!"); break;
    case 19: if(mw_armor_plus(p)<3)mw_set_armor_plus(p,3); snprintf(message,message_size,"ARMOR ENCHANTED +3 UNTIL THE INN!"); break;
    case 20: p->eff_fast_move=1; snprintf(message,message_size,"FAST MOVE LASTS UNTIL THE INN!"); break;
    case 21: if(!p->eff_super_str){p->stat_str+=10;p->eff_super_str=1;} snprintf(message,message_size,"STRENGTH INCREASED BY 10 UNTIL THE INN!"); break;
    case 22: if(mw_enchant_wpn_spell(p)<5)mw_set_enchant_wpn_spell(p,5); snprintf(message,message_size,"WEAPONS ENCHANTED +5 UNTIL THE INN!"); break;
    case 23:
        if (!prep_vertical_depth_allowed(g, index, g->cur_floor)) {
            prep_vertical_failure_message(g, index, message, message_size);
            return 0;
        }
        target = g->cur_floor + 25;
        {
            int cap = game_traversal_rules(g)->prep_major_descend_cap;
            if (target > cap) target = cap;
        }
        if (!game_change_floor(g,p,target) || !game_relocate(g,p)) return 0;
        snprintf(message,message_size,"YOU DESCEND TWENTY-FIVE LEVELS!");
        break;
    case 24: if(!p->eff_super_agi){p->stat_agi+=10;p->eff_super_agi=1;} snprintf(message,message_size,"AGILITY INCREASED BY 10 UNTIL THE INN!"); break;
    case 25: p->poisoned_turns=0; snprintf(message,message_size,"POISON CURED!"); break;
    case 26: p->hp_cur=p->hp_max; snprintf(message,message_size,"ALL WOUNDS HEALED!"); break;
    case 27:
        if (!prep_vertical_depth_allowed(g, index, g->cur_floor)) {
            prep_vertical_failure_message(g, index, message, message_size);
            return 0;
        }
        if (g->cur_floor <= 0) {
            snprintf(message, message_size, "YOU CANNOT ASCEND ANY FARTHER!");
            return 0;
        }
        target = g->cur_floor - 25;
        if (target < 0) target = 0;
        if (!game_change_floor(g,p,target) || !game_relocate(g,p)) return 0;
        snprintf(message,message_size,"YOU ASCEND TWENTY-FIVE LEVELS!");
        break;
    case 28: p->diseased_turns=0; snprintf(message,message_size,"DISEASE CURED!"); break;
    case 29: if(mw_armor_plus(p)<4)mw_set_armor_plus(p,4); snprintf(message,message_size,"ARMOR ENCHANTED +4 UNTIL THE INN!"); break;
    default: return 0;
    }
    return 1;
}

static int cast_selected_spell(Game *g, Character *p, CombatState *cs,
                               int category, int index, int source,
                               char *message, size_t message_size) {
    int level = index / 3 + 1;
    int result = -3;
    if (message_size) message[0] = '\0';
    if (category == SPELL_CAT_PERMANENT && (cs || g->cur_floor != 0)) {
        snprintf(message, message_size,
                 "PERMANENT SPELLS TAKE A MONTH AND REQUIRE TOWN.");
        return 0;
    }
    if (category == SPELL_CAT_PREPARATION && cs) {
        snprintf(message, message_size,
                 "PREPARATION SPELLS TAKE THREE MINUTES; NOT IN BATTLE.");
        return 0;
    }
    if (category >= SPELL_CAT_WIZARD && !cs) {
        snprintf(message, message_size, "BATTLE SPELLS REQUIRE A MONSTER!");
        return 0;
    }
    if (source == 0 && !g->cheat_god_mode && p->sp_cur < level) {
        snprintf(message, message_size, "NOT ENOUGH SPELL POINTS: NEED %d.", level);
        return 0;
    }
    int applied = 1;
    if (category == SPELL_CAT_PERMANENT)
        applied = apply_permanent_spell(g, p, cs, index, message, message_size);
    else if (category == SPELL_CAT_PREPARATION)
        applied = apply_preparation_spell(g, p, index, message, message_size);
    else {
        const BattleSpellDef *sd = category == SPELL_CAT_WIZARD ?
                                   &wiz_spells[index] : &priest_spells[index];
        result = apply_battle_spell(g, cs, p, sd, level);
        snprintf(message, message_size, "%s CAST!", spell_names_for_category(category)[index]);
        if (result == 0) result = -5; /* Cast consumed the action but failed its roll. */
    }
    if (!applied) {
        if (!message[0])
            snprintf(message, message_size,
                     "THE SPELL HAS NO VALID EFFECT OR TARGET.");
        return 0;
    }
    if (source == 0) {
        if (!g->cheat_god_mode) {
            p->sp_cur -= (float)level;
            if (category == SPELL_CAT_PERMANENT) {
                p->sp_max = p->sp_max > level ?
                            p->sp_max - (float)level : 0.0f;
                if (p->sp_cur > p->sp_max) p->sp_cur = p->sp_max;
            }
        }
    } else if (source == 1) {
        --p->scrolls[category][index];
    } else if (source == 2) {
        --p->wands[category][index];
    } else {
        --p->papers[category][index];
    }
    if (category == SPELL_CAT_PERMANENT) p->age += 2592000u;
    else if (category == SPELL_CAT_PREPARATION) p->age += 180u;
    return result;
}

static int choose_cast_category(Game *g, Character *p, CombatState *cs,
                                int source, int include_help) {
    spell_draw_backdrop(g, p, cs);
    spell_line(g, 0, "SELECT THE TYPE OF SPELL:", 8);
    spell_line(g, 1, "1) PERMANENT SPELLS", 7);
    spell_line(g, 2, "2) PREPARATION SPELLS", 7);
    spell_line(g, 3, "3) WIZARD BATTLE SPELLS", 7);
    spell_line(g, 4, "4) PRIEST BATTLE SPELLS", 7);
    if (include_help) {
        spell_line(g, 5, "5) HELP-PERMANENT SPELLS", 7);
        spell_line(g, 6, "6) HELP-PREPARATION SPELLS", 7);
        spell_line(g, 7, "7) HELP-WIZARD BATTLE SP.", 7);
        spell_line(g, 8, "8) HELP-PRIEST BATTLE SP.", 7);
    }
    video_present(&g->video);
    int key = input_getch(&g->input);
    int max = include_help ? '8' : '4';
    key = mouse_list_key(g, key, 0, SPELL_PANE_W, SPELL_ROW_H,
                         SPELL_ROW_H, max - '0', '1');
    if (key < '1' || key > max) return -1;
    (void)source;
    return key - '1';
}

int cmd_cast_spell_menu(Game *g, Character *p, CombatState *cs) {
    int choice = choose_cast_category(g, p, cs, 0, 1);
    if (choice < 0) return 0;
    int help = choice >= 4;
    int category = help ? choice - 4 : choice;
    if (!help && !class_can_read_spellbook(p, category)) {
        spell_notice(g, p, cs, "YOUR CLASS CANNOT READ", "THAT KIND OF SPELLBOOK.");
        return 0;
    }
    int index = select_spell_index(g, p, cs, category, 0, help, 10);
    if (index < 0) return 0;
    if (help) {
        show_spell_help(g, p, cs, category, index);
        return 0;
    }
    char message[160];
    int result = cast_selected_spell(g, p, cs, category, index, 0,
                                     message, sizeof(message));
    if (!cs || result == 0) spell_notice(g, p, cs, message, "");
    return result;
}

static int use_misc_item(Game *g, Character *p, CombatState *cs) {
    spell_draw_backdrop(g,p,cs);
    spell_line(g,0,"USE WHICH MAGIC ITEM?",4);
    spell_line(g,2,"1) HOLY HAND GRENADE",8);
    spell_line(g,3,"2) STONE OF TELEPORTATION",8);
    spell_line(g,4,"3) STONE OF SEEING",8);
    spell_line(g,5,"4) FLOOR SLOSHER",8);
    spell_line(g,6,"5) POTION OF HEALING",8);
    spell_line(g,8,"6) VITAMIN PILL",8);
    video_present(&g->video);
    int key=input_getch(&g->input);
    if (key == INPUT_MOUSE_CLICK) {
        int x, y;
        key = -1;
        if (game_mouse_click_logical(g, &x, &y) && x < SPELL_PANE_W) {
            int row = y / SPELL_ROW_H;
            if (row >= 2 && row <= 6) key = '1' + row - 2;
            else if (row == 8) key = '6';
        }
    }
    char msg[128]="";
    if(key=='1' && cs && p->holy_grenade){--p->holy_grenade;cs->monster_hp=0;snprintf(msg,sizeof(msg),"THE HOLY GRENADE DESTROYS THE MONSTER!");spell_notice(g,p,cs,msg,"");return -1;}
    if(key=='2' && p->stone_teleport){--p->stone_teleport;game_change_floor(g,p,0);game_relocate(g,p);snprintf(msg,sizeof(msg),"THE STONE TELEPORTS YOU TO TOWN!");}
    else if(key=='3' && p->stone_see){--p->stone_see;memset(g->visited,1,sizeof(g->visited));snprintf(msg,sizeof(msg),"THE ENTIRE LEVEL IS REVEALED!");}
    else if(key=='4' && p->floor_slosher){int t=g->cur_floor+1+game_rand(g)%25;int max_floor=game_traversal_rules(g)->max_floor;if(t>max_floor)t=max_floor;game_change_floor(g,p,t);game_relocate(g,p);snprintf(msg,sizeof(msg),"THE FLOOR SLOSHES AWAY BENEATH YOU!");}
    else if(key=='5' && p->potion_heal){--p->potion_heal;p->hp_cur=p->hp_max;snprintf(msg,sizeof(msg),"THE POTION HEALS ALL WOUNDS!");}
    else if(key=='6'){
        spell_draw_backdrop(g,p,cs);spell_line(g,0,"1:S 2:I 3:W 4:C 5:A 6:L",4);video_present(&g->video);int k=input_getch(&g->input);
        if(k==INPUT_MOUSE_CLICK){int x,y;if(game_mouse_click_logical(g,&x,&y)&&y<SPELL_ROW_H)k='1'+(x*6/LOGICAL_W);else k=-1;}
        u8 *count=NULL;u16 *up=NULL,*down1=NULL,*down2=NULL;
        if(k=='1'){count=&p->orange_pill;up=&p->stat_str;down1=&p->stat_agi;down2=&p->stat_wis;}
        else if(k=='2'){count=&p->green_pill;up=&p->stat_int;down1=&p->stat_con;down2=&p->stat_str;}
        else if(k=='3'){count=&p->blue_pill;up=&p->stat_wis;down1=&p->stat_str;down2=&p->stat_agi;}
        else if(k=='4'){count=&p->red_pill;up=&p->stat_con;down1=&p->stat_int;down2=&p->stat_wis;}
        else if(k=='5'){count=&p->white_pill;up=&p->stat_agi;down1=&p->stat_wis;down2=&p->stat_con;}
        else if(k=='6'){count=&p->yellow_pill;up=&p->stat_luck;down1=&p->stat_con;down2=&p->stat_int;}
        if(count&&*count){--*count;*up+=4;*down1=*down1>=2?*down1-2:0;*down2=*down2>=2?*down2-2:0;snprintf(msg,sizeof(msg),"THE VITAMIN PERMANENTLY CHANGES YOUR STATS!");}
    }
    if(*msg)spell_notice(g,p,cs,msg,"");
    return 0;
}

int cmd_use_item(Game *g, Character *p, CombatState *cs) {
    spell_draw_backdrop(g,p,cs);
    spell_line(g,0,"USE WHICH TYPE OF ITEM?",4);
    spell_line(g,2,"1) SCROLL",8);
    spell_line(g,3,"2) WAND",8);
    spell_line(g,4,"3) MAGIC PAPER",8);
    spell_line(g,5,"4) MISC. MAGIC ITEM",8);
    video_present(&g->video);
    int key=input_getch(&g->input);
    key=mouse_list_key(g,key,0,SPELL_PANE_W,2*SPELL_ROW_H,SPELL_ROW_H,4,'1');
    if(key=='4')return use_misc_item(g,p,cs);
    if(key<'1'||key>'3')return 0;
    int source=key-'0';
    if(p->class_id==CLASS_FIGHTER && source!=3){spell_notice(g,p,cs,"FIGHTERS CAN CAST ONLY","FROM MAGIC PAPER.");return 0;}
    int category=choose_cast_category(g,p,cs,source,0);
    if(category<0)return 0;
    int index=select_spell_index(g,p,cs,category,source,0,10);
    if(index<0)return 0;
    char message[160];
    int result=cast_selected_spell(g,p,cs,category,index,source,message,sizeof(message));
    if(!cs||result==0)spell_notice(g,p,cs,message,"");
    return result;
}

int combat_self_test(void) {
    Game g;
    Character p;
    CombatState cs;
    char message[160];
    int failures = 0;
    memset(&g, 0, sizeof(g));
    memset(&p, 0, sizeof(p));
    memset(&cs, 0, sizeof(cs));
    game_srand(&g, 1);
    p.class_id = CLASS_WIZARD;
    p.level = 20;
    p.stat_str = 20;
    p.stat_agi = 20;
    p.stat_wis = 20;
    p.hp_cur = p.hp_max = 100;
    p.sp_cur = p.sp_max = 100.0f;
    cs.monster_type_idx = 0;
    cs.monster_level = 10;
    cs.monster_hp = cs.monster_max_hp = 100;

#define CHECK(expr, label) do { if (!(expr)) { \
    fprintf(stderr, "MAGIC TEST FAIL: %s\n", label); failures++; } } while (0)
    {
        Input input;
        memset(&input, 0, sizeof(input));
        input.keys[0] = 0;
        input.keys[1] = 0x50;
        input.tail = 2;
        CHECK(input_wait_any_key(&input) == 0 && input.head == input.tail,
              "modal input drains extended Down key");
    }
    CHECK(spell_hotkey_for_index(0) == 'A' &&
          spell_hotkey_for_index(25) == 'Z' &&
          spell_hotkey_for_index(26) == '1' &&
          spell_hotkey_for_index(29) == '4' &&
          spell_hotkey_for_index(30) == '\0',
          "original 30-entry spell selector hotkeys");
    CHECK(spell_index_from_hotkey('A') == 0 &&
          spell_index_from_hotkey('z') == 25 &&
          spell_index_from_hotkey('1') == 26 &&
          spell_index_from_hotkey('4') == 29 &&
          spell_index_from_hotkey('0') == -1,
          "spell selector direct-key decoding");
    CHECK(combat_monster_type_valid(84, 2) &&
          !combat_monster_type_valid(84, 1) &&
          combat_monster_type_valid(14, 200) &&
          !combat_monster_type_valid(14, 251) &&
          !combat_monster_type_valid(104, 4) &&
          combat_monster_type_valid(114, 251) &&
          combat_monster_type_valid(114, 325) &&
          !combat_monster_type_valid(114, 326) &&
          combat_monster_type_valid(133, 500) &&
          combat_monster_type_valid(134, 501) &&
          combat_monster_type_valid(145, 625) &&
          !combat_monster_type_valid(145, 676) &&
          combat_monster_type_valid(173, 1000) &&
          !combat_monster_type_valid(174, 625) &&
          !combat_monster_type_valid(177, 1000),
          "original/deep monster floor bands and boss exclusion");
    {
        static const int floors[] = {
            1, 4, 20, 80, 125, 150, 200, 256, 375, 499,
            501, 625, 750, 875, 999, 1000
        };
        int generated_are_valid = 1;
        for (int f = 0; f < (int)(sizeof(floors) / sizeof(floors[0])); f++)
            for (int i = 0; i < 512; i++) {
                int type = combat_pick_monster_type(&g, floors[f]);
                if (!combat_monster_type_valid(type, floors[f]) ||
                    (floors[f] <= 250 && type >= DEEP_MONSTER_FIRST) ||
                    (floors[f] > 250 && type < DEEP_MONSTER_FIRST))
                    generated_are_valid = 0;
            }
        CHECK(generated_are_valid,
              "generated monsters obey original/deep floor bands");
    }
    {
        int every_deep_type_has_art = 1;
        for (int type = DEEP_MONSTER_FIRST; type < MONSTER_TYPE_COUNT; type++)
            if (!combat_monster_type_spawnable(type) ||
                get_monster_pic_index_ext(type) < 2 ||
                get_monster_tint_ext(type) < 1)
                every_deep_type_has_art = 0;
        CHECK(every_deep_type_has_art,
              "every native deep monster has loaded recolored art");
    }
    CHECK(monster_types[112].minL == 375 && monster_types[112].atk > 255 &&
          monster_types[113].minL == 500 && monster_types[113].def > 255 &&
          monster_types[177].minL == 1000 && monster_types[177].atk > 1000 &&
          combat_calc_monster_hp(&monster_types[177], 1000) > UINT16_MAX,
          "native deep bosses use widened stats and HP");
    mw_set_weapon_enchant(&p, 1, 300);
    mw_set_armor_enchant(&p, 3, 275);
    CHECK(mw_weapon_enchant(&p, 1) == 300 &&
          mw_armor_enchant(&p, 3) == 275,
          "16-bit equipment enchantments exceed byte range");
    CHECK(class_can_read_spellbook(&p, SPELL_CAT_WIZARD), "wizard book access");
    p.class_id = CLASS_FIGHTER;
    CHECK(!class_can_read_spellbook(&p, SPELL_CAT_PREPARATION), "fighter book denial");
    p.class_id = CLASS_WIZARD;

    apply_preparation_spell(&g, &p, 8, message, sizeof(message));
    CHECK(p.stat_str == 25 && p.eff_str_bonus == 1, "preparation strength");
    character_clear_town_effects(&p);
    CHECK(p.stat_str == 20 && p.eff_str_bonus == 0, "inn reverses strength");

    apply_battle_spell(&g, &cs, &p, &wiz_spells[4], 2);
    CHECK(p.stat_str == 27 && p.eff_battle_str == 60, "battle strength begins");
    apply_battle_spell(&g, &cs, &p, &wiz_spells[4], 2);
    CHECK(p.stat_str == 34 && p.eff_battle_str == 120, "battle strength stacks");
    for (int i = 0; i < 60; i++) character_tick_effects(&g, &p);
    CHECK(p.stat_str == 27 && p.eff_battle_str == 60, "first strength stack expires");
    for (int i = 0; i < 60; i++) character_tick_effects(&g, &p);
    CHECK(p.stat_str == 20 && p.eff_battle_str == 0, "battle strength expires");

    apply_battle_spell(&g, &cs, &p, &wiz_spells[14], 5);
    apply_battle_spell(&g, &cs, &p, &wiz_spells[14], 5);
    CHECK(p.eff_resist_poison == 120, "resistance duration stacks");
    p.poisoned_turns = 1;
    character_tick_effects(&g, &p);
    CHECK(p.stat_str == 20 && p.poisoned_turns == 1,
          "resistance freezes poison countdown");
    p.eff_resist_poison = 0;
    character_tick_effects(&g, &p);
    CHECK(p.stat_str == 19 && p.poisoned_turns == 450,
          "poison expires, drains strength and repeats");
    p.stat_str = 20;

    CHECK(monster_breath_type(84) == BREATH_FIRE &&
          monster_breath_type(92) == BREATH_ACID &&
          monster_breath_type(108) == BREATH_COLD &&
          monster_breath_type(111) == BREATH_ACID &&
          monster_breath_type(112) == BREATH_COLD &&
          monster_breath_type(113) == BREATH_FIRE &&
          monster_breath_type(114) == BREATH_COLD &&
          monster_breath_type(128) == BREATH_DISEASE &&
          monster_breath_type(133) == BREATH_FIRE,
          "exact dragon and deep-variant breath table");
    CHECK(combat_monster_drain_amount(26) == 1 &&
          combat_monster_drain_amount(31) == 2 &&
          combat_monster_drain_amount(35) == 3 &&
          combat_monster_drain_amount(111) == 4 &&
          combat_monster_drain_amount(112) == 5 &&
          combat_monster_drain_amount(113) == 7 &&
          combat_monster_drain_amount(116) == 2 &&
          combat_monster_drain_amount(129) == 4 &&
          combat_monster_drain_amount(131) == 5,
          "exact original and deep level-drain table");

    memset(&cs, 0, sizeof(cs));
    cs.active = 1;
    cs.monster_type_idx = 72;
    cs.monster_level = 10;
    cs.monster_hp = 10;
    combat_monster_special(&g, &cs, &p, 0);
    CHECK(p.stat_str == 21 && !cs.active && cs.monster_hp == 0,
          "beneficial puffball raises stat and vanishes");
    p.stat_str = 20;

    memset(&cs, 0, sizeof(cs));
    cs.active = 1;
    cs.monster_type_idx = 43;
    cs.monster_level = 10;
    cs.monster_hp = 10;
    combat_monster_special(&g, &cs, &p, 2);
    CHECK(p.poisoned_turns == 450, "black animal inflicts poison");
    p.poisoned_turns = 0;

    memset(&cs, 0, sizeof(cs));
    cs.active = 1;
    cs.monster_type_idx = 107;
    cs.monster_level = 10;
    cs.monster_hp = 10;
    p.equipped_armor = 3;
    p.armor_inventory[3] = 1;
    p.armor_enchant[3] = 4;
    for (int i = 0; i < 32 && p.equipped_armor; ++i)
        combat_monster_special(&g, &cs, &p, 2);
    CHECK(p.equipped_armor == 0 && p.armor_inventory[3] == 0 &&
          p.armor_enchant[3] == 0, "acid breath destroys equipped armor");

    p.equipped_weapon = 1;
    mw_set_weapon_enchant(&p, 1, 0);
    CHECK(apply_permanent_spell(&g, &p, NULL, 6, message, sizeof(message)) &&
          mw_weapon_enchant(&p, 1) == 2, "permanent weapon enchant");
    CHECK(apply_permanent_spell(&g, &p, NULL, 26, message, sizeof(message)) &&
          p.hp_max == 125 && p.hp_cur == 125, "permanent health");

    g.dungeon_max_floor = MAX_DUNGEON_FLOOR;
    CHECK(game_traversal_rules(&g)->prep_ascend_max_floor == 260 &&
          game_traversal_rules(&g)->prep_descend_max_floor == 492 &&
          game_traversal_rules(&g)->prep_major_descend_cap == 300 &&
          prep_vertical_depth_allowed(&g, 11, 492) &&
          !prep_vertical_depth_allowed(&g, 11, 493) &&
          prep_vertical_depth_allowed(&g, 12, 260) &&
          !prep_vertical_depth_allowed(&g, 12, 261) &&
          prep_vertical_depth_allowed(&g, 23, 260) &&
          !prep_vertical_depth_allowed(&g, 23, 261),
          "scaled vertical spell depth limits");
    g.dungeon_max_floor = CLASSIC_DUNGEON_FLOOR;
    CHECK(game_traversal_rules(&g)->max_floor == 250 &&
          game_traversal_rules(&g)->prep_ascend_max_floor == 65 &&
          game_traversal_rules(&g)->prep_descend_max_floor == 123 &&
          game_traversal_rules(&g)->prep_major_descend_cap == 75 &&
          prep_vertical_depth_allowed(&g, 11, 123) &&
          !prep_vertical_depth_allowed(&g, 11, 124) &&
          prep_vertical_depth_allowed(&g, 12, 65) &&
          !prep_vertical_depth_allowed(&g, 12, 66) &&
          prep_vertical_depth_allowed(&g, 23, 65) &&
          !prep_vertical_depth_allowed(&g, 23, 66),
          "classic vertical spell depth limits");
    g.dungeon_max_floor = MAX_DUNGEON_FLOOR;
    g.cur_floor = game_traversal_rules(&g)->prep_descend_max_floor + 1;
    p.scrolls[SPELL_CAT_PREPARATION][11] = 1;
    p.wands[SPELL_CAT_PREPARATION][11] = 2;
    p.papers[SPELL_CAT_PREPARATION][11] = 3;
    p.sp_cur = 100.0f;
    CHECK(cast_selected_spell(&g, &p, NULL, SPELL_CAT_PREPARATION, 11, 1,
                              message, sizeof(message)) == 0 &&
          p.scrolls[SPELL_CAT_PREPARATION][11] == 1 &&
          strstr(message, "492"),
          "blocked deep Descend preserves item and reports limit");
    CHECK(cast_selected_spell(&g, &p, NULL, SPELL_CAT_PREPARATION, 11, 0,
                              message, sizeof(message)) == 0 &&
          p.sp_cur == 100.0f &&
          cast_selected_spell(&g, &p, NULL, SPELL_CAT_PREPARATION, 11, 2,
                              message, sizeof(message)) == 0 &&
          p.wands[SPELL_CAT_PREPARATION][11] == 2 &&
          cast_selected_spell(&g, &p, NULL, SPELL_CAT_PREPARATION, 11, 3,
                              message, sizeof(message)) == 0 &&
          p.papers[SPELL_CAT_PREPARATION][11] == 3,
          "all blocked vertical casting sources remain unspent");
    g.cur_floor = 0;

    p.scrolls[SPELL_CAT_PREPARATION][25] = 1;
    p.poisoned_turns = 200;
    CHECK(cast_selected_spell(&g, &p, NULL, SPELL_CAT_PREPARATION, 25, 1,
                              message, sizeof(message)) == -3 &&
          p.scrolls[SPELL_CAT_PREPARATION][25] == 0 && !p.poisoned_turns,
          "scroll consumes and casts");
    p.wands[SPELL_CAT_WIZARD][5] = 2;
    CHECK(cast_selected_spell(&g, &p, &cs, SPELL_CAT_WIZARD, 5, 2,
                              message, sizeof(message)) == 25 &&
          p.wands[SPELL_CAT_WIZARD][5] == 1, "wand charge and battle damage");

    p.hp_cur = p.hp_max = 100;
    g.cheat_god_mode = 1;
    CHECK(combat_apply_player_damage(&g, &p, 999) == 0 && p.hp_cur == 100,
          "god mode blocks incoming HP damage");
    p.sp_cur = 0.0f;
    p.sp_max = 50.0f;
    p.papers[SPELL_CAT_PREPARATION][2] = 7;
    CHECK(cast_selected_spell(&g, &p, NULL, SPELL_CAT_PREPARATION, 2, 0,
                              message, sizeof(message)) == -3 &&
          p.sp_cur == 0.0f && p.sp_max == 50.0f &&
          p.papers[SPELL_CAT_PREPARATION][2] == 7,
          "god mode casts learned spells free without consuming paper");
    g.cheat_god_mode = 0;
    CHECK(combat_apply_player_damage(&g, &p, 25) == 25 && p.hp_cur == 75,
          "normal incoming HP damage restored");
#undef CHECK
    printf("Magic/equipment/status self-test: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
