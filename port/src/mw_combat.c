#include "mw_combat.h"
#include "mw_game.h"
#include "mw_arena.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

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
    /* MW_EXTENSION: quest gear awarded only in Enhanced mode.  Slots 8-11
       remain the temporary Power Weapon spell dice used by WORLD.EXE. */
    { "WORLDFORGED BLADE", 75, 28, 18, 10 },
    { "RIFTCARVER",       130, 42, 17, 12 },
    { "STARFORGED SABER", 210, 58, 16, 14 },
    { "VOIDREAVER",       310, 74, 15, 16 },
    { "ETERNITY EDGE",    430, 90, 14, 18 },
    { "CELESTIAL BRAND",  570,105, 13, 20 },
    { "ASCENDANT EDGE",   750,118, 12, 22 },
    { "MORAFF'S LEGACY",  980,127, 10, 24 },
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
    {"Prismatic World King",420,260,480,220,280,220,500,500,500,1,420,450},
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

    {"Crimson Death Mask",260,110,260,205,190,120,100,401,475,0,260,255},
    {"Frost Skeleton",   245,130,270,210,210,120,100,401,475,0,270,265},
    {"Plague Zombie",    260,145,280,220,175,130,100,401,475,0,275,270},
    {"Violet Wraith",    285,120,300,235,225,130,100,401,475,0,290,285},

    {"Obsidian Mummy",   310,155,325,250,210,130,100,451,500,0,315,310},
    {"Astral Vampire",   330,145,350,270,245,130,100,451,500,0,335,330},
    {"Blood Medusa",     350,170,365,285,260,145,100,451,500,0,350,345},
    {"Void Demon",       380,190,400,310,275,150,100,451,500,0,380,375},

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
static const int armor_defense[ARMOR_STAT_COUNT] = {
    0, 2, 4, 6, 9, 12, 16, 14,
    75, 130, 210, 310, 430, 570, 750, 980
};

static const int armor_weight[ARMOR_STAT_COUNT] = {
    0, 14, 24, 40, 60, 72, 48, 0,
    36, 42, 48, 54, 60, 66, 72, 80
};

static const char *const armor_type_names[ARMOR_STAT_COUNT] = {
    "SKIN", "LEATHER", "CHAIN", "SCALE", "PLATE",
    "FIELD PLATE", "TITANIUM", "OGRE",
    "PRISMATIC MAIL", "RIFTWARD PLATE", "STARFORGED MAIL",
    "VOID BASTION", "ETERNITY PLATE", "CELESTIAL AEGIS",
    "ASCENDANT AEGIS", "MORAFF'S BULWARK"
};

const char *combat_armor_name(int armor) {
    return armor >= 0 && armor < ARMOR_STAT_COUNT ?
           armor_type_names[armor] : "UNKNOWN";
}

int combat_armor_defense(int armor) {
    return armor >= 0 && armor < ARMOR_STAT_COUNT ?
           armor_defense[armor] : 0;
}

int combat_armor_weight(int armor) {
    return armor >= 0 && armor < ARMOR_STAT_COUNT ?
           armor_weight[armor] : 0;
}

int combat_weapon_allowed(const Character *player, int weapon) {
    if (!player || weapon < 0 || weapon >= WEAPON_STAT_COUNT ||
        (weapon >= 8 && weapon <= 11))
        return 0;
    if (weapon >= 12 &&
        mw_experience_mode(player) != MW_EXPERIENCE_ENHANCED)
        return 0;
    if (weapon == 0) return 1;
    if (mw_universal_access(player)) return 1;
    if (weapon == 12)
        return player->class_id != CLASS_WORSHIPPER &&
               player->class_id != CLASS_MONK;
    if (weapon >= 13)
        return player->class_id == CLASS_FIGHTER ||
               player->class_id == CLASS_PRIEST ||
               player->class_id == CLASS_MAGE ||
               player->class_id == CLASS_SPELLBLADE ||
               player->class_id == CLASS_PALADIN;
    switch (player->class_id) {
    case CLASS_FIGHTER:
    case CLASS_SPELLBLADE:
    case CLASS_PALADIN:
        return 1;
    case CLASS_WORSHIPPER:
    case CLASS_MONK:
        return 0;
    case CLASS_WIZARD:
    case CLASS_SAGE:
        return weapon == 1 || weapon == 4;
    default:
        return weapon != 7;
    }
}

int combat_armor_allowed(const Character *player, int armor) {
    if (!player || armor < 0 || armor >= ARMOR_STAT_COUNT) return 0;
    if (armor >= 8 &&
        mw_experience_mode(player) != MW_EXPERIENCE_ENHANCED)
        return 0;
    if (armor == 0) return 1;
    if (mw_universal_access(player)) return 1;
    if (armor == 8)
        return player->class_id == CLASS_FIGHTER ||
               player->class_id == CLASS_MONK ||
               player->class_id == CLASS_PRIEST ||
               player->class_id == CLASS_SAGE ||
               player->class_id == CLASS_MAGE ||
               player->class_id == CLASS_SPELLBLADE ||
               player->class_id == CLASS_PALADIN;
    if (armor >= 9)
        return player->class_id == CLASS_FIGHTER ||
               player->class_id == CLASS_PRIEST ||
               player->class_id == CLASS_MAGE ||
               player->class_id == CLASS_SPELLBLADE ||
               player->class_id == CLASS_PALADIN;
    if (player->class_id == CLASS_WORSHIPPER ||
        player->class_id == CLASS_WIZARD)
        return 0;
    if (player->class_id == CLASS_MONK ||
        player->class_id == CLASS_SAGE)
        return armor == 1;
    return 1;
}

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
    BS_HEAL_FIXED, BS_HEAL_ALL, BS_DRAIN_SCALE, BS_DAMAGE_PERCENT,
    BS_RESTORE_ALL, BS_LIFE_CONVERGENCE, BS_PHOENIX_PRAYER, BS_AEGIS,
} BattleSpellType;

typedef struct {
    BattleSpellType type;
    int param1;
    int param2;
} BattleSpellDef;

/* Wizard battle spells: the original 30 followed by fifteen Enhanced entries. */
static const BattleSpellDef wiz_spells[MW_ENHANCED_SPELL_COUNT] = {
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
    /* L11*/ {BS_DAMAGE_SCALE,25,500},{BS_STOP,30,0},          {BS_DAMAGE_RANGE,5000,12000},
    /* L12*/ {BS_DRAIN_SCALE,4,0},  {BS_DAMAGE_PERCENT,25,5000},
              {BS_DAMAGE_RANGE,15000,30000},
    /* L13*/ {BS_STOP,120,0},       {BS_DAMAGE_PERCENT,40,25000},
              {BS_DAMAGE_SCALE,50,2000},
    /* L14*/ {BS_DAMAGE_RANGE,60000,120000},{BS_POWER_WEAPON,4,0},
              {BS_DAMAGE_PERCENT,60,50000},
    /* L15*/ {BS_POWER_WEAPON,5,0},{BS_DAMAGE_RANGE,200000,400000},
              {BS_POWER_WEAPON,6,0},
};

/* Priest battle spells: the original 30 followed by fifteen Enhanced entries. */
static const BattleSpellDef priest_spells[MW_ENHANCED_SPELL_COUNT] = {
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
    /* L11*/ {BS_RESTORE_ALL,0,0},  {BS_AEGIS,6,180},         {BS_DAMAGE_RANGE,3500,9000},
    /* L12*/ {BS_STOP,30,0},        {BS_DAMAGE_PERCENT,20,4000},
              {BS_LIFE_CONVERGENCE,10,2000},
    /* L13*/ {BS_AEGIS,8,1200},     {BS_DAMAGE_RANGE,12000,26000},
              {BS_PHOENIX_PRAYER,7,300},
    /* L14*/ {BS_DAMAGE_PERCENT,50,30000},{BS_POWER_WEAPON,4,0},
              {BS_PHOENIX_PRAYER,8,1200},
    /* L15*/ {BS_POWER_WEAPON,5,0},{BS_DAMAGE_PERCENT,75,100000},
              {BS_POWER_WEAPON,6,0},
};

int combat_spell_arena_eligible(int category, int index) {
    const BattleSpellDef *spell;
    if (index < 0 || index >= MW_ENHANCED_SPELL_COUNT ||
        (category != SPELL_CAT_WIZARD && category != SPELL_CAT_PRIEST))
        return 0;
    spell = category == SPELL_CAT_WIZARD ?
            &wiz_spells[index] : &priest_spells[index];
    switch (spell->type) {
    case BS_SLEEP:
    case BS_DAMAGE_SCALE:
    case BS_DAMAGE_FIXED:
    case BS_DAMAGE_MULTI:
    case BS_DAMAGE_RANGE:
    case BS_HOLD:
    case BS_DRAIN:
    case BS_AUTOKILL:
    case BS_BUFF_SLOW:
    case BS_STOP:
    case BS_SHOCK_125:
    case BS_SHOCK_300:
    case BS_HEAL_FIXED:
    case BS_HEAL_ALL:
    case BS_DRAIN_SCALE:
    case BS_DAMAGE_PERCENT:
    case BS_RESTORE_ALL:
    case BS_LIFE_CONVERGENCE:
    case BS_PHOENIX_PRAYER:
        return 1;
    default:
        return 0;
    }
}

/* Spell names (same arrays as in mw_game.c, duplicated here for self-containment) */
static const char *wiz_spell_names[MW_ENHANCED_SPELL_COUNT] = {
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
    "ABYSSAL LANCE","TIME STOP","VOID NOVA",
    "SOUL REND","OBLIVION",
    "STARFIRE","CHRONO LOCK","REALITY RUPTURE",
    "MANA TEMPEST","ANNIHILATION",
    "POWER WEAPON IV","COSMIC IMPLOSION","POWER WEAPON V",
    "END OF AGES","POWER WEAPON VI",
};

static const char *priest_spell_names[MW_ENHANCED_SPELL_COUNT] = {
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
    "GREATER RESTORATION","DIVINE AEGIS","HOLY CATACLYSM",
    "CELESTIAL STASIS","FINAL JUDGMENT",
    "LIFE CONVERGENCE","ETERNAL WARD","WRATH OF HEAVEN",
    "PHOENIX PRAYER","DIVINE VERDICT",
    "POWER WEAPON IV","SERAPHIC REPRIEVE","POWER WEAPON V",
    "CREATION'S WRATH","POWER WEAPON VI",
};

static const char *permanent_spell_names[MW_ENHANCED_SPELL_COUNT] = {
    "ENCHANT WEAPON LEVEL 1", "EXTRA HEALTH POINT", "WRITE SCROLL TO LEVEL 3",
    "ENCHANT ARMOR LEVEL 1", "EXTRA 3 HEALTH POINTS", "ENCHANT WAND LEVEL 3",
    "ENCHANT WEAPON LEVEL 2", "EXTRA 5 HEALTH POINTS", "ENCHANT RING LEVEL 1",
    "ENCHANT ARMOR LEVEL 2", "ANTI-MAGIC RING LEVEL 1", "WRITE SCROLL - LEVEL 10",
    "ENCHANT WEAPON LEVEL 3", "ENCHANT RING LEVEL 2", "BODY ARMOR LEVEL 1",
    "ENCHANT ARMOR LEVEL 3", "ANTI-MAGIC RING LEVEL 2", "ENCHANT WAND LEVEL 8",
    "ENCHANT RING LEVEL 3", "ANTI-MAGIC RING LEVEL 3", "BODY ARMOR LEVEL 2",
    "ENCHANT WEAPON LEVEL 4", "ENCHANT ARMOR LEVEL 4", "ENCHANT WAND ANY LEVEL",
    "PERMANENT FEATHER", "ANTI-MAGIC RING LEVEL 5", "EXTRA 25 HEALTH POINTS",
    "PERMANENT INVISIBILITY", "YOUTH", "BODY ARMOR LEVEL 4",
    "ENCHANT WEAPON LEVEL 150","ENCHANT ARMOR LEVEL 100",
    "BODY ARMOR LEVEL 100","WRITE DEEP SCROLL","CHARGE DEEP WAND",
    "ENCHANT WEAPON LEVEL 500","ENCHANT ARMOR LEVEL 350",
    "BODY ARMOR LEVEL 300","WRITE ASCENDANT SCROLL",
    "CHARGE ASCENDANT WAND",
    "ENCHANT WEAPON LEVEL 1000","ENCHANT ARMOR LEVEL 750",
    "BODY ARMOR LEVEL 650","WRITE MYTHIC SCROLL",
    "CHARGE MYTHIC WAND"
};

static const char *preparation_spell_names[MW_ENHANCED_SPELL_COUNT] = {
    "ENCHANT ARMOR LEVEL 1", "ENCHANT WEAPON LEVEL 1", "LITTLE CURE",
    "ENCHANT WEAPON LEVEL 2", "RELOCATE", "DETECT LEVEL",
    "CURE", "ENCHANT ARMOR LEVEL 2", "STRENGTH",
    "ENCHANT WEAPON LEVEL 3", "AGILITY", "DESCEND",
    "ASCEND", "DETECT POSITION", "FEATHER",
    "BIG CURE", "DOUBLE ASCEND", "ENCHANT WEAPON LEVEL 4",
    "INVISIBILITY", "ENCHANT ARMOR LEVEL 3", "FAST MOVE",
    "SUPER STRENGTH", "ENCHANT WEAPON LEVEL 5", "MAJOR DESCEND",
    "SUPER AGILITY", "CURE POISON", "HEAL ALL WOUNDS",
    "MAJOR ASCEND", "CURE DISEASE", "ENCHANT ARMOR LEVEL 4",
    "ABYSS DESCEND","ABYSS ASCEND","DEEP SANCTUARY",
    "CARTOGRAPHER'S EYE","TOWN PORTAL",
    "RIFT DESCEND","RIFT ASCEND","ETERNAL SANCTUARY",
    "WORLD REVEAL","SOUL ANCHOR",
    "TITAN DESCEND","TITAN ASCEND","MYTHIC SANCTUARY",
    "ASTRAL FORM","PERFECT VITALITY"
};

static const char *const *spell_names_for_category(int category) {
    if (category == SPELL_CAT_PERMANENT) return permanent_spell_names;
    if (category == SPELL_CAT_PREPARATION) return preparation_spell_names;
    if (category == SPELL_CAT_WIZARD) return wiz_spell_names;
    return priest_spell_names;
}

const char *combat_spell_name(int category, int index) {
    if (category < SPELL_CAT_PERMANENT || category > SPELL_CAT_PRIEST ||
        index < 0 || index >= MW_ENHANCED_SPELL_COUNT)
        return "UNKNOWN SPELL";
    return spell_names_for_category(category)[index];
}

typedef struct MonsterSpellProfile {
    u8 category;
    u8 index;
    u8 chance; /* denominator: one cast attempt in this many responses */
} MonsterSpellProfile;

enum {
    LATE_CASTER_FIRST = 112,
    LATE_CASTER_COUNT = MONSTER_TYPE_COUNT - LATE_CASTER_FIRST
};

/* MW_EXTENSION: the Enhanced roster previously had only melee/breath
 * specials.  Its magical creatures now use the same named level 11-15
 * battle catalog earned by the player.  Brute silhouettes deliberately
 * retain physical-only identities, while spellcasters become more frequent
 * and more dangerous toward floor 1000. */
static const MonsterSpellProfile
late_monster_spell_profiles[LATE_CASTER_COUNT] = {
    [112 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 32, 3}, /* Void Nova */
    [113 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 34, 3}, /* Oblivion */
    [116 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 30, 5},
    [118 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 32, 5},
    [120 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 30, 5},
    [122 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 30, 5},
    [123 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 32, 5},
    [125 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 34, 5},
    [128 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 30, 4},
    [129 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 34, 4},
    [131 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 30, 4},
    [132 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 32, 4},
    [133 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 34, 4},

    [135 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 35, 4},
    [136 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 35, 4},
    [137 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 35, 4},
    [138 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 37, 4},
    [139 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 37, 4},
    [140 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 38, 4},
    [141 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 38, 4},
    [142 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 37, 4},
    [143 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 37, 4},
    [144 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 38, 4},
    [145 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 35, 4},

    [146 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 35, 3},
    [147 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 37, 3},
    [148 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 37, 3},
    [149 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 38, 3},
    [150 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 35, 3},
    [151 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 38, 3},
    [152 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 39, 3},
    [153 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 37, 3},
    [154 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 37, 3},
    [155 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 35, 3},
    [156 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 39, 3},
    [157 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 38, 3},
    [158 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 39, 3},
    [159 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 37, 3},
    [160 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 38, 3},
    [161 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 35, 3},
    [162 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 39, 3},
    [163 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 37, 3},
    [164 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 37, 3},
    [165 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 39, 3},
    [166 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 37, 3},
    [167 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 38, 3},
    [168 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 39, 3},
    [169 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 39, 3},
    [170 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 38, 3},
    [171 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 35, 3},
    [172 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 39, 3},
    [173 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 39, 3},

    [174 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 41, 2},
    [175 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 43, 2},
    [176 - LATE_CASTER_FIRST] = {SPELL_CAT_PRIEST, 43, 2},
    [177 - LATE_CASTER_FIRST] = {SPELL_CAT_WIZARD, 43, 2},
};

static const MonsterSpellProfile *monster_spell_profile(int type) {
    if (type < LATE_CASTER_FIRST || type >= MONSTER_TYPE_COUNT) return NULL;
    const MonsterSpellProfile *profile =
        &late_monster_spell_profiles[type - LATE_CASTER_FIRST];
    return profile->chance ? profile : NULL;
}

const char *combat_monster_spell_name(int type_idx) {
    const MonsterSpellProfile *profile = monster_spell_profile(type_idx);
    return profile ? combat_spell_name(profile->category, profile->index) :
                     "NONE";
}

int combat_monster_spell_chance(int type_idx) {
    const MonsterSpellProfile *profile = monster_spell_profile(type_idx);
    return profile ? profile->chance : 0;
}

static void dec_u16(u16 *value) {
    if (*value) --*value;
}

static void add_stat_capped(u16 *value, unsigned amount) {
    unsigned total = (unsigned)*value + amount;
    *value = (u16)(total > MW_PLAYER_STAT_MAX ?
                   MW_PLAYER_STAT_MAX : total);
}

static int add_temporary_stat_stack(u16 *value, u16 *turns,
                                    unsigned amount, u16 duration) {
    if ((unsigned)*value + amount > MW_PLAYER_STAT_MAX ||
        *turns > (u16)(UINT16_MAX - duration))
        return 0;
    *value = (u16)(*value + amount);
    *turns = (u16)(*turns + duration);
    return 1;
}

static void add_hp_capped(Character *p, unsigned amount) {
    uint64_t cap = mw_player_hp_cap(p);
    uint64_t maximum = mw_hp_max(p);
    uint64_t current = mw_hp_cur(p);
    maximum = maximum + amount > cap ? cap : maximum + amount;
    current = current + amount > maximum ? maximum : current + amount;
    mw_set_hp_max(p, maximum);
    mw_set_hp_cur(p, current);
}

static void heal_hp_capped(Character *p, unsigned amount) {
    uint64_t total = (uint64_t)mw_hp_cur(p) + amount;
    u32 maximum = mw_hp_max(p);
    mw_set_hp_cur(p, total > maximum ? maximum : total);
}

/* MW_PORT: WORLD func_0CA5F, func_0CDDD, func_0D2C9, plus duration/reset
 * work performed by spell-effect helpers 0x10E9A..0x11DA5. */
static int character_has_maxed_prep_effect(const Character *p, u8 value) {
    return mw_universal_access(p) && value == 60;
}

static int character_has_maxed_battle_effect(const Character *p, u16 value) {
    return mw_universal_access(p) && value == mw_effect_turn_cap(p);
}

void character_clear_battle_effects(Character *p) {
    if (p->eff_battle_str &&
        !character_has_maxed_battle_effect(p, p->eff_battle_str)) {
        unsigned remove = ((unsigned)p->eff_battle_str + 59) / 60 * 7;
        p->stat_str = p->stat_str >= remove ? (u16)(p->stat_str - remove) : 0;
        p->eff_battle_str = 0;
    }
    if (p->eff_battle_spd &&
        !character_has_maxed_battle_effect(p, p->eff_battle_spd)) {
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
    if (p->eff_str_bonus &&
        !character_has_maxed_prep_effect(p, p->eff_str_bonus)) {
        p->stat_str = p->stat_str >= 5 ? (u16)(p->stat_str - 5) : 0;
        p->eff_str_bonus = 0;
    }
    if (p->eff_agi_bonus &&
        !character_has_maxed_prep_effect(p, p->eff_agi_bonus)) {
        p->stat_agi = p->stat_agi >= 5 ? (u16)(p->stat_agi - 5) : 0;
        p->eff_agi_bonus = 0;
    }
    if (p->eff_super_str &&
        !character_has_maxed_prep_effect(p, p->eff_super_str)) {
        p->stat_str = p->stat_str >= 10 ? (u16)(p->stat_str - 10) : 0;
        p->eff_super_str = 0;
    }
    if (p->eff_super_agi &&
        !character_has_maxed_prep_effect(p, p->eff_super_agi)) {
        p->stat_agi = p->stat_agi >= 10 ? (u16)(p->stat_agi - 10) : 0;
        p->eff_super_agi = 0;
    }
}

void character_tick_effects(Game *g, Character *p) {
    if (p->eff_battle_str &&
        !character_has_maxed_battle_effect(p, p->eff_battle_str) &&
        --p->eff_battle_str % 60 == 0)
        p->stat_str = p->stat_str >= 7 ? (u16)(p->stat_str - 7) : 0;
    if (p->eff_battle_spd &&
        !character_has_maxed_battle_effect(p, p->eff_battle_spd) &&
        --p->eff_battle_spd % 60 == 0)
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
            p->poisoned_turns =
                mw_relic_owned(p, MW_RELIC_DEEPWARD_AMULET) ? 900 : 450;
        }
    }
    if (p->diseased_turns && !p->eff_resist_disease) {
        if (--p->diseased_turns == 0) {
            if (p->stat_con > 1) --p->stat_con;
            p->diseased_turns =
                mw_relic_owned(p, MW_RELIC_DEEPWARD_AMULET) ? 900 : 450;
        }
    }
    dec_u16(&p->eff_resist_poison);
    dec_u16(&p->eff_resist_disease);
    dec_u16(&p->eff_anti_cold);
    dec_u16(&p->eff_anti_fire);
    dec_u16(&p->eff_resist_drain);
    dec_u16(&p->eff_stop_monster);
    dec_u16(&p->eff_hold_monster);
    /* Regeneration heals the living; it must never resurrect a character
       between lethal damage and the caller's death check. */
    if (mw_hp_cur(p) && p->ring_regen && mw_hp_cur(p) < mw_hp_max(p))
        heal_hp_capped(p, p->ring_regen);
    if (mw_relic_owned(p, MW_RELIC_ARCANE_RING)) {
        p->native.relic_regen_phase++;
        if (p->native.relic_regen_phase >= 4) {
            p->native.relic_regen_phase = 0;
            if (p->sp_cur < p->sp_max) {
                p->sp_cur += 1.0f;
                if (p->sp_cur > p->sp_max) p->sp_cur = p->sp_max;
            }
        }
    } else if (mw_character_native_valid(p)) {
        p->native.relic_regen_phase = 0;
    }
    if (mw_relic_owned(p, MW_RELIC_PHOENIX_SEAL) &&
        p->native.relic_phoenix_cooldown)
        --p->native.relic_phoenix_cooldown;
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

/* Shared by every place that displays monster art.  Original balls,
 * puffballs, and dragons replace WORLD.PIC color 17.  Enhanced monsters then
 * remap the remaining chromatic VGA colors into their assigned tint family,
 * preserving black, transparency, grayscale shading, and white glints. */
int combat_remap_monster_color(int color, int replace_color, int tint) {
    if (color == 17 && replace_color >= 0)
        color = replace_color;
    if (!tint || color <= 0 || color >= 16)
        return color;
    if (color == 7 || color == 8 || color == 15)
        return color;
    int family = tint & 7;
    if (!family) return 8;
    return color >= 9 ? family + 8 : family;
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
   Get effective weapon die (handles Power Weapon spell override)
   MW_PORT: select_weapon and weapon_effect/func_11B18.
   ══════════════════════════════════════════════════════════════════════ */

int combat_power_weapon_max_damage(int power_weapon_level) {
    static const int max_damage[] = {0, 129, 199, 399, 800, 1300, 2000};
    if (power_weapon_level < 1 ||
        power_weapon_level >= (int)(sizeof(max_damage) / sizeof(max_damage[0])))
        return 0;
    return max_damage[power_weapon_level];
}

int combat_effective_damage_max(int equipped_weapon, int power_weapon_level,
                                int enhanced) {
    if (equipped_weapon < 0 || equipped_weapon >= WEAPON_STAT_COUNT ||
        (equipped_weapon >= 8 && equipped_weapon <= 11))
        equipped_weapon = 0;
    int base = weapon_stats[equipped_weapon].maxDmg;
    int power = combat_power_weapon_max_damage(power_weapon_level);
    if (!power || (!enhanced && power_weapon_level > 3)) return base;

    /* WORLD replaces the die outright.  Enhanced makes the spell a floor so
       Power Weapon IV cannot weaken Moraff's Legacy, while V and VI improve it. */
    if (!enhanced) return power;
    return power > base ? power : base;
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
    if (base_wpn >= WEAPON_STAT_COUNT ||
        (base_wpn >= 8 && base_wpn <= 11))
        base_wpn = 0;
    const WeaponStats *equipped = &weapon_stats[base_wpn];
    int damage_max = combat_effective_damage_max(
        base_wpn, player->eff_pwr_weapon,
        mw_experience_mode(player) == MW_EXPERIENCE_ENHANCED);

    /* Phase 1: Compute hit score */
    int hit_score = game_rand(g) % 80;  /* rand(0-79) */

    hit_score += player->level * 2;
    hit_score += player->stat_str;
    hit_score += player->stat_luck;
    hit_score += player->combat_bonus;
    /* Preparation and battle stat spells modify the saved stats themselves;
       their flag/counter fields only track how and when to reverse them. */
    /* Power Weapon affects only the damage die. Accuracy, enchantment, speed
       and weight continue to come from the equipped physical weapon. Classic
       uses WORLD's replacement; Enhanced retains a stronger late-game die. */
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
        int max_dmg = damage_max;
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
    if (g->arena_active) {
        /* Most Colosseum turns are deliberately ordinary attacks.  Give a
           successful hit modest level-based momentum so a viable run does
           not depend on drawing an early damage spell or jackpot weapon. */
        int64_t boosted = (int64_t)total_damage * 5 / 4 +
                          (int64_t)player->level / 2 + 1;
        total_damage = boosted > INT_MAX ? INT_MAX : (int)boosted;
    }
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

static int combat_monster_spell_heal(CombatState *cs,
                                     const MonsterSpellProfile *profile) {
    if (profile->category != SPELL_CAT_PRIEST ||
        (profile->index != 30 && profile->index != 38))
        return -1;
    const MonsterType *mt = &monster_types[cs->monster_type_idx];
    int cap = combat_calc_monster_hp(mt, cs->monster_level);
    if (cap < cs->monster_hp) cap = cs->monster_hp;
    int divisor = profile->index == 30 ? 10 : 5;
    int64_t amount = cap / divisor + cs->monster_level;
    int64_t healed = (int64_t)cs->monster_hp + amount;
    if (healed > cap) healed = cap;
    int restored = (int)(healed - cs->monster_hp);
    if (restored < 0) restored = 0;
    cs->monster_hp = (int)healed;
    if (cs->monster_max_hp < cs->monster_hp)
        cs->monster_max_hp = cs->monster_hp;
    return restored;
}

/* Enemy versions of the player's deep spells retain their identity but use
 * player-scale damage.  Copying the player's five-figure flat damage would
 * make the first post-250 caster an unavoidable instant kill; percentage
 * spells instead remain threatening across normal and trainer-expanded HP. */
static int combat_monster_spell_effect(Game *g, CombatState *cs,
                                       Character *player,
                                       const MonsterSpellProfile *profile) {
    const MonsterType *mt = &monster_types[cs->monster_type_idx];
    const char *spell = combat_spell_name(profile->category, profile->index);
    if (profile->category == SPELL_CAT_PRIEST && profile->index == 35) {
        int cap = combat_calc_monster_hp(mt, cs->monster_level);
        if (cap < cs->monster_hp) cap = cs->monster_hp;
        int64_t heal = cap / 12 + cs->monster_level;
        int64_t healed = (int64_t)cs->monster_hp + heal;
        if (healed > cap) healed = cap;
        int restored = (int)(healed - cs->monster_hp);
        if (restored < 0) restored = 0;
        cs->monster_hp = (int)healed;
        if (cs->monster_max_hp < cs->monster_hp)
            cs->monster_max_hp = cs->monster_hp;

        uint64_t damage = (uint64_t)mw_hp_max(player) / 14u +
                          (unsigned)(cs->monster_level > 0 ?
                                     cs->monster_level : 1) / 2u +
                          (unsigned)mt->dmg / 2u;
        damage += (unsigned)game_rand(g) %
                  ((unsigned)(cs->monster_level > 0 ?
                              cs->monster_level : 1) / 4u + 1u);
        if (damage < 1) damage = 1;
        if (damage > INT_MAX) damage = INT_MAX;
        cs->special_used = 1;
        snprintf(cs->special_message, sizeof(cs->special_message),
                 "THE %.24s CASTS LIFE CONVERGENCE, DRAINING YOU AND HEALING %d!",
                 mt->name, restored);
        return (int)damage;
    }
    int restored = combat_monster_spell_heal(cs, profile);
    cs->special_used = 1;
    if (restored >= 0) {
        snprintf(cs->special_message, sizeof(cs->special_message),
                 "THE %.28s CASTS %.25s AND HEALS %d!",
                 mt->name, spell, restored);
        return 0;
    }

    uint64_t maximum_hp = mw_hp_max(player);
    uint64_t damage = 0;
    int level = cs->monster_level > 0 ? cs->monster_level : 1;
    if (profile->category == SPELL_CAT_WIZARD) {
        switch (profile->index) {
        case 30: /* Abyssal Lance */
            damage = (uint64_t)level / 2u + (unsigned)mt->dmg / 2u;
            break;
        case 32: /* Void Nova */
            damage = (uint64_t)level * 3u / 4u + (unsigned)mt->dmg;
            break;
        case 34: /* Oblivion */
            damage = maximum_hp / 12u + (unsigned)level / 3u;
            break;
        case 35: /* Starfire */
            damage = (unsigned)level + (unsigned)mt->dmg;
            break;
        case 37: /* Reality Rupture */
            damage = maximum_hp / 8u + (unsigned)level / 2u;
            break;
        case 38: { /* Mana Tempest */
            damage = (unsigned)level / 2u + (unsigned)mt->dmg;
            if (!g->cheat_god_mode) {
                float drain = (float)(level / 5 + mt->dmg / 10);
                if (drain > player->sp_cur) drain = player->sp_cur;
                if (drain > 0.0f) player->sp_cur -= drain;
            }
            break;
        }
        case 39: /* Annihilation */
            damage = maximum_hp / 6u + (unsigned)level +
                     (unsigned)mt->dmg / 2u;
            break;
        case 41: /* Cosmic Implosion */
            damage = maximum_hp / 5u + (unsigned)level / 2u;
            break;
        case 43: /* End of Ages */
            damage = maximum_hp / 4u + (unsigned)level / 2u;
            break;
        default:
            damage = (unsigned)level / 2u + (unsigned)mt->dmg / 2u;
            break;
        }
    } else {
        switch (profile->index) {
        case 32: /* Holy Cataclysm */
            damage = (unsigned)level / 2u + (unsigned)mt->dmg;
            break;
        case 34: /* Final Judgment */
            damage = maximum_hp / 10u + (unsigned)level / 2u;
            break;
        case 37: /* Wrath of Heaven */
            damage = (uint64_t)level * 3u / 4u + (unsigned)mt->dmg;
            break;
        case 39: /* Divine Verdict */
            damage = maximum_hp / 7u + (unsigned)level / 2u;
            break;
        case 43: /* Creation's Wrath */
            damage = maximum_hp / 4u + (unsigned)level / 2u;
            break;
        default:
            damage = (unsigned)level / 2u + (unsigned)mt->dmg / 2u;
            break;
        }
    }
    damage += (unsigned)game_rand(g) % ((unsigned)level / 4u + 1u);
    if (damage < 1) damage = 1;
    if (damage > INT_MAX) damage = INT_MAX;
    snprintf(cs->special_message, sizeof(cs->special_message),
             "THE %.32s CASTS %.32s!", mt->name, spell);
    return (int)damage;
}

static int combat_monster_try_spell(Game *g, CombatState *cs,
                                    Character *player, int *damage) {
    cs->special_used = 0;
    cs->special_message[0] = '\0';
    const MonsterSpellProfile *profile =
        monster_spell_profile(cs->monster_type_idx);
    if (!profile || game_rand(g) % profile->chance != 0) return 0;
    cs->special_used = 1;
    if (player->antimagic_ring &&
        game_rand(g) % 100 < player->antimagic_ring * 8) {
        snprintf(cs->special_message, sizeof(cs->special_message),
                 "YOUR ANTI-MAGIC RING DISPELS %.40s!",
                 combat_spell_name(profile->category, profile->index));
        *damage = 0;
        return 1;
    }
    *damage = combat_monster_spell_effect(g, cs, player, profile);
    return 1;
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
    /* Adventure keeps each monster's source-derived drain strength. Arena
       opponents already track the combatant's rapidly rising level, so one
       response must not erase several victories. */
    if (g->arena_active && drain_amount > 1) drain_amount = 1;
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
        if (!p->poisoned_turns)
            p->poisoned_turns =
                mw_relic_owned(p, MW_RELIC_DEEPWARD_AMULET) ? 900 : 450;
        cs->special_used = 1;
        snprintf(cs->special_message, sizeof(cs->special_message),
                 "THE %s POISONS YOU!", monster_types[type].name);
    } else if (type >= 50 && type <= 56 && !p->eff_resist_disease) {
        if (!p->diseased_turns)
            p->diseased_turns =
                mw_relic_owned(p, MW_RELIC_DEEPWARD_AMULET) ? 900 : 450;
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
            p->diseased_turns =
                mw_relic_owned(p, MW_RELIC_DEEPWARD_AMULET) ? 900 : 450;
        if (breath == BREATH_POISON && !p->eff_resist_poison)
            p->poisoned_turns =
                mw_relic_owned(p, MW_RELIC_DEEPWARD_AMULET) ? 900 : 450;
        if (breath == BREATH_ACID && p->equipped_armor > 0) {
            int armor = p->equipped_armor;
            mw_set_armor_enchant(p, armor, 0);
            int owned = mw_armor_inventory_count(p, armor);
            if (owned > 0) mw_set_armor_inventory_count(p, armor, owned - 1);
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
    if (g->arena_active) {
        unsigned round = g->arena_round ? g->arena_round : 1u;
        int pressure = 10 + (int)(round > 240u ? 480u : round * 2u);
        /* Accuracy pressure now ramps continuously.  The former curve jumped
           from 73 at round four to 100 at round five and exceeded 100 at
           round six, before most builds owned meaningful armor. */
        if (round > 20u) pressure += 20;
        if (g->arena_champion) pressure += 18;
        if (g->arena_difficulty == ARENA_DIFFICULTY_EASY) pressure -= 15;
        else if (g->arena_difficulty == ARENA_DIFFICULTY_HARD) pressure += 25;
        if (pressure < 0) pressure = 0;
        hit_score += pressure;
    }

    /* Subtract player defenses */
    hit_score -= player->level * 2;
    hit_score -= player->stat_agi;
    hit_score -= player->stat_luck;
    hit_score -= player->combat_bonus;
    int armor = player->equipped_armor < ARMOR_STAT_COUNT ? player->equipped_armor : 0;
    int equipment_defense = armor_defense[armor] +
        mw_armor_enchant(player, armor) + mw_body_armor_plus(player) +
        mw_ring_prot_plus(player) + mw_armor_plus(player);
    if (g->arena_active) {
        unsigned round = g->arena_round ? g->arena_round : 1u;
        int soft_cap = 15 + (int)(round > 500u ? 500u : round);
        if (g->arena_difficulty == ARENA_DIFFICULTY_EASY) soft_cap += 10;
        else if (g->arena_difficulty == ARENA_DIFFICULTY_HARD && soft_cap > 5)
            soft_cap -= 5;
        if (equipment_defense > soft_cap)
            equipment_defense = soft_cap + (equipment_defense - soft_cap) / 8;
    }
    hit_score -= equipment_defense;
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
            (uint64_t)total >= mw_hp_cur(player))
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
static int combat_apply_player_damage(Game *g, Character *player, int damage,
                                      int *phoenix_saved) {
    if (phoenix_saved) *phoenix_saved = 0;
    if (damage <= 0 || g->cheat_god_mode) return 0;
    if (mw_relic_owned(player, MW_RELIC_DEEPWARD_AMULET)) {
        /* Fifteen-percent mitigation, rounded so a real hit still does at
           least one point.  Status durations are halved at their source. */
        damage = (int)((int64_t)damage * 85 / 100);
        if (damage < 1) damage = 1;
    }
    if (g->arena_active) {
        /* Scale and cap a whole counterattack, including weight repeats and
           specials.  The old safeguard vanished after round nine, creating a
           cliff precisely at the first champion.  This curve tapers through
           round twenty and leaves room for a combat cure to make net progress. */
        unsigned round = g->arena_round ? g->arena_round : 1u;
        int percent = 42 + (int)round * 3;
        int cap_percent = round <= 20u ? 10 + (int)round :
                          (round >= 110u ? 75 :
                           30 + (int)((round - 20u) / 2u));
        if (g->arena_difficulty == ARENA_DIFFICULTY_EASY) {
            percent -= 10;
            cap_percent -= 4;
        } else if (g->arena_difficulty == ARENA_DIFFICULTY_HARD) {
            percent += 10;
            cap_percent += 4;
        }
        if (percent > 100) percent = 100;
        damage = (int)(((int64_t)damage * percent + 99) / 100);
        uint64_t cap = (uint64_t)mw_hp_max(player) *
                       (unsigned)cap_percent / 100u;
        if (cap < 1u) cap = 1u;
        if ((uint64_t)damage > cap) damage = (int)cap;
        if (damage < 1) damage = 1;
    }
    u32 current_hp = mw_hp_cur(player);
    if ((u32)damage >= current_hp &&
        mw_relic_owned(player, MW_RELIC_PHOENIX_SEAL) &&
        player->native.relic_phoenix_cooldown == 0) {
        int applied = current_hp > 1 ? (int)(current_hp - 1) : 1;
        mw_set_hp_cur(player, 1);
        /* The triggering exchange ticks effects once after combat, so start
           at 301 to leave exactly 300 future player actions on the display. */
        player->native.relic_phoenix_cooldown = 301;
        if (phoenix_saved) *phoenix_saved = 1;
        return applied;
    }
    if ((u32)damage >= current_hp)
        mw_set_hp_cur(player, 0);
    else
        mw_set_hp_cur(player, current_hp - (u32)damage);
    return damage;
}

/* ══════════════════════════════════════════════════════════════════════
   Cast a battle spell — returns damage dealt or special code
   MW_PORT: spell_menu (0x00436), combat_event (0x005DB), cast_spell
   (0x0079A), and effect routines 0x10E9A..0x11DA5.
   -1 = instant kill, -2 = immune, -3 = effect only (sleep/hold/buff), 0 = miss/fail
   ══════════════════════════════════════════════════════════════════════ */

static int arena_adjust_spell_damage(const Game *g, const Character *player,
                                     int spell_level, int raw_damage) {
    if (!g->arena_active || raw_damage <= 0) return raw_damage;
    int mental = player->stat_int > player->stat_wis ?
                 player->stat_int : player->stat_wis;
    int64_t floor = (int64_t)player->level * (spell_level + 3) + mental / 2;
    int64_t adjusted = raw_damage;
    if (adjusted < floor) adjusted = floor;
    /* Levels one through five were particularly poor compared with a free
       melee swing.  A small novice multiplier makes scarce SP/charges worth
       spending without inflating the already-large deep spell formulas. */
    if (spell_level <= 5) adjusted = adjusted * 5 / 4;
    return adjusted > INT_MAX ? INT_MAX : (int)adjusted;
}

static int apply_battle_spell(Game *g, CombatState *cs, Character *player,
                              const BattleSpellDef *sd, int spell_level) {
    const MonsterType *mt = &monster_types[cs->monster_type_idx];

    /* Arena magic competes with a repeatable melee command and always gives
       the opponent a response.  Preserve WORLD's spell formulas everywhere
       else, but give early Colosseum damage a useful level/mental-stat floor. */
#define ARENA_SPELL_DAMAGE(raw_value) \
    arena_adjust_spell_damage(g, player, spell_level, (raw_value))

    switch (sd->type) {
    case BS_NONE:
        return 0;

    case BS_SLEEP:
        if (mt->imm >= 100 || cs->monster_level > (int)player->level * 2) return -2;
        cs->monster_asleep = 10;
        /* Combat is not modal in WORLD.  Mirror the remaining duration in
           the save-backed encounter counter so it survives returning to the
           ordinary dispatcher between attacks. */
        player->eff_hold_monster = 10;
        return -3;

    case BS_DAMAGE_SCALE:
        return ARENA_SPELL_DAMAGE(player->level * sd->param1 + sd->param2);

    case BS_DAMAGE_FIXED:
        return ARENA_SPELL_DAMAGE(sd->param1);

    case BS_DAMAGE_MULTI: {
        int total = 0;
        int missiles = player->level + 1;
        int range = sd->param2 - sd->param1 + 1;
        for (int i = 0; i < missiles; i++)
            total += (game_rand(g) % range) + sd->param1;
        return ARENA_SPELL_DAMAGE(total);
    }

    case BS_DAMAGE_RANGE: {
        int range = sd->param2 - sd->param1 + 1;
        return ARENA_SPELL_DAMAGE((game_rand(g) % range) + sd->param1);
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
        cs->monster_stopped = sd->param1 > 0 ? sd->param1 : 10;
        player->eff_stop_monster = (u16)cs->monster_stopped;
        return -3;

    case BS_DRAIN: {
        int drain = player->stat_wis;
        if (drain < 1) drain = 1;
        cs->monster_level -= drain;
        if (cs->monster_level < 1) return -1;
        return -3;
    }

    case BS_DRAIN_SCALE: {
        int64_t drain = (int64_t)player->stat_wis * sd->param1 +
                        (int64_t)player->level / 2;
        if (drain < 1) drain = 1;
        if (drain > INT_MAX) drain = INT_MAX;
        cs->monster_level -= (int)drain;
        if (cs->monster_level < 1) return -1;
        return -3;
    }

    case BS_DAMAGE_PERCENT: {
        int64_t damage = (int64_t)cs->monster_max_hp * sd->param1 / 100 +
                         sd->param2;
        if (damage < 1) damage = 1;
        if (damage > INT_MAX) damage = INT_MAX;
        return ARENA_SPELL_DAMAGE((int)damage);
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
        return ARENA_SPELL_DAMAGE(125);

    case BS_SHOCK_300:
        return ARENA_SPELL_DAMAGE(300);

    case BS_BUFF_STR:
        (void)add_temporary_stat_stack(&player->stat_str,
                                       &player->eff_battle_str, 7, 60);
        return -3;

    case BS_BUFF_SPD:
        (void)add_temporary_stat_stack(&player->stat_agi,
                                       &player->eff_battle_spd, 7, 60);
        return -3;

    case BS_BUFF_STR_SPD:
        (void)add_temporary_stat_stack(&player->stat_str,
                                       &player->eff_battle_str, 7, 60);
        (void)add_temporary_stat_stack(&player->stat_agi,
                                       &player->eff_battle_spd, 7, 60);
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
        unsigned amount = (unsigned)sd->param1;
        if (g->arena_active) {
            uint64_t scaled = (uint64_t)mw_hp_max(player) *
                              (unsigned)(30 + spell_level * 5) / 100u;
            scaled += (unsigned)player->level;
            if (scaled > UINT_MAX) scaled = UINT_MAX;
            if (amount < scaled) amount = (unsigned)scaled;
        }
        heal_hp_capped(player, amount);
        return -3;
    }

    case BS_HEAL_ALL:
        mw_set_hp_cur(player, mw_hp_max(player));
        return -3;

    case BS_RESTORE_ALL:
        mw_set_hp_cur(player, mw_hp_max(player));
        player->poisoned_turns = 0;
        player->diseased_turns = 0;
        return -3;

    case BS_LIFE_CONVERGENCE: {
        int divisor = sd->param1 > 0 ? sd->param1 : 10;
        int64_t damage = (cs->monster_hp > 0 ?
                          (int64_t)cs->monster_hp : 0) / divisor +
                         sd->param2;
        uint64_t heal = damage > 0 ? (uint64_t)damage / 2u : 0u;
        uint64_t heal_cap = (uint64_t)mw_hp_max(player) / 3u;
        if (heal_cap < 1) heal_cap = 1;
        if (heal > heal_cap) heal = heal_cap;
        if (heal > UINT_MAX) heal = UINT_MAX;
        heal_hp_capped(player, (unsigned)heal);
        if (damage < 1) damage = 1;
        if (damage > INT_MAX) damage = INT_MAX;
        return (int)damage;
    }

    case BS_PHOENIX_PRAYER: {
        unsigned heal = mw_hp_max(player) / 2u;
        u16 duration = (u16)(sd->param2 > 0 ? sd->param2 : 300);
        u16 fire_duration = duration <= UINT16_MAX / 2 ?
                            (u16)(duration * 2) : UINT16_MAX;
        if (heal < 1) heal = 1;
        heal_hp_capped(player, heal);
        player->poisoned_turns = 0;
        player->diseased_turns = 0;
        if (player->eff_protect_lv < (u8)sd->param1)
            player->eff_protect_lv = (u8)sd->param1;
        if (player->eff_protect_turns < duration)
            player->eff_protect_turns = duration;
        if (player->eff_anti_fire < fire_duration)
            player->eff_anti_fire = fire_duration;
        return -3;
    }

    case BS_AEGIS: {
        u16 duration = (u16)(sd->param2 > 0 ? sd->param2 : 60);
        if (player->eff_protect_lv < (u8)sd->param1)
            player->eff_protect_lv = (u8)sd->param1;
        if (player->eff_protect_turns < duration)
            player->eff_protect_turns = duration;
        if (player->eff_resist_poison < duration)
            player->eff_resist_poison = duration;
        if (player->eff_resist_disease < duration)
            player->eff_resist_disease = duration;
        if (player->eff_anti_cold < duration)
            player->eff_anti_cold = duration;
        if (player->eff_anti_fire < duration)
            player->eff_anti_fire = duration;
        if (player->eff_resist_drain < duration)
            player->eff_resist_drain = duration;
        return -3;
    }

    case BS_POWER_WEAPON:
        if (mw_experience_mode(player) == MW_EXPERIENCE_ENHANCED &&
            player->eff_pwr_weapon > (u8)sd->param1) {
            if (player->eff_pwr_wpn_turns <= UINT16_MAX - 60)
                player->eff_pwr_wpn_turns += 60;
        } else if (player->eff_pwr_weapon == (u8)sd->param1 &&
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
#undef ARENA_SPELL_DAMAGE
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

/* Legacy reconstruction retained only for comparison with the early port.
 * Nothing in the live dispatcher calls this full-screen selector; all cast
 * and item paths enter the pane-preserving unified interface below. */
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
        int key = input_wait_any_key(&g->input);
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

/* Legacy public entry point retained for source/API comparison.  The active
 * combat path calls cmd_cast_spell_menu(), which preserves the exploration
 * layout and monster viewport. */
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
        int key = input_wait_any_key(&g->input);
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

static int combat_direction(Game *g, const CombatState *cs) {
    int direction = g->last_move_dir & 3;
    if (g->monster_map_loaded && g->monster_layer >= 0 &&
        cs->entity_index >= 0 && cs->entity_index < MONSTERS_PER_FLOOR) {
        const MonsterRecord *m =
            &g->monster_map[g->monster_layer][cs->entity_index];
        int dx = (int)m->x - g->cur_x;
        int dy = (int)m->y - g->cur_y;
        if (dx == 0 && dy == -1) direction = 0;
        else if (dx == 0 && dy == 1) direction = 1;
        else if (dx == -1 && dy == 0) direction = 2;
        else if (dx == 1 && dy == 0) direction = 3;
    }
    return direction;
}

static void combat_counterattack(Game *g, CombatState *cs,
                                 Character *player, int direction,
                                 char *message, size_t message_size,
                                 char *death, size_t death_size) {
    static const char *const dir_name[4] = {
        "NORTH", "SOUTH", "WEST", "EAST"
    };
    const MonsterType *mt = &monster_types[cs->monster_type_idx];
    int strikes = 1;
    int phoenix_saved = 0;
    int damage = 0;
    int monster_disabled =
        cs->monster_asleep > 0 || cs->monster_held > 0 ||
        cs->monster_stopped > 0;
    int spell_cast = monster_disabled ? 0 :
        combat_monster_try_spell(g, cs, player, &damage);
    if (spell_cast) {
        strikes = 0;
        mw_audio_play(&g->audio, MW_SFX_MAGIC);
    } else {
        damage = combat_monster_attack_weighted(g, cs, player, &strikes);
    }
    damage = combat_apply_player_damage(g, player, damage, &phoenix_saved);
    if (damage <= 0) {
        if (cs->special_used)
            snprintf(message, message_size, "%s", cs->special_message);
        else if (cs->monster_asleep || cs->monster_held ||
                 cs->monster_stopped)
            snprintf(message, message_size, "THE %s CANNOT ATTACK!", mt->name);
        else
            snprintf(message, message_size, "%s MISSES", dir_name[direction]);
        return;
    }
    mw_audio_play(&g->audio, MW_SFX_HURT);
    if (spell_cast)
        snprintf(message, message_size, "%s %d HP LOST.",
                 cs->special_message, damage);
    else if (cs->special_used)
        snprintf(message, message_size, "%s", cs->special_message);
    else if (strikes > 1)
        snprintf(message, message_size,
                 "%s STRIKES %d TIMES FOR %d POINTS",
                 dir_name[direction], strikes, damage);
    else
        snprintf(message, message_size, "%s DOES %d POINTS",
                 dir_name[direction], damage);
    if (phoenix_saved)
        snprintf(death, death_size,
                 "THE PHOENIX SEAL FLARES! YOU SURVIVE WITH 1 HP.");
    if (!mw_hp_cur(player)) {
        character_clear_battle_effects(player);
        snprintf(death, death_size, "YOU HAVE BEEN KILLED!");
    }
}

static int combat_apply_bloodstone(Character *player, int damage) {
    if (!mw_relic_owned(player, MW_RELIC_BLOODSTONE_SIGNET) ||
        damage <= 0 || mw_hp_cur(player) >= mw_hp_max(player))
        return 0;
    int healed = (damage + 19) / 20;
    int cap = 1 + player->level / 10;
    if (healed > cap) healed = cap;
    uint64_t missing = (uint64_t)mw_hp_max(player) - mw_hp_cur(player);
    if ((uint64_t)healed > missing) healed = (int)missing;
    mw_set_hp_cur(player, (uint64_t)mw_hp_cur(player) + healed);
    return healed;
}

/* MW_PORT: WORLD func_0EAE9 and the F/C/I branches of func_0F6E5.  WORLD
   has no modal combat dispatcher: one selected battle action resolves, the
   monster may answer, and control immediately returns to the ordinary main
   loop.  This is what permits movement, equipment, map/help, save, and every
   other main-screen command while a monster remains adjacent. */
int combat_take_turn(Game *g, CombatState *cs, Character *player, int action) {
    char msg1[128] = "";
    char msg2[128] = "";
    char msg3[128] = "";
    const MonsterType *mt = &monster_types[cs->monster_type_idx];
    int direction = combat_direction(g, cs);
    int result = 0;

    if (action == COMBAT_ACTION_FIGHT) {
        mw_audio_play(&g->audio, MW_SFX_ATTACK);
        int damage = combat_player_attack(g, cs, player);
        if (damage <= 0)
            snprintf(msg1, sizeof(msg1), "YOU MISS");
        else {
            int dealt = damage > cs->monster_hp ? cs->monster_hp : damage;
            cs->monster_hp -= damage;
            int healed = combat_apply_bloodstone(player, dealt);
            if (healed)
                snprintf(msg1, sizeof(msg1),
                         "YOU DO %d POINTS; BLOODSTONE HEALS %d",
                         damage, healed);
            else
                snprintf(msg1, sizeof(msg1), "YOU DO %d POINTS", damage);
        }
        result = 1;
    } else if (action == COMBAT_ACTION_CAST ||
               action == COMBAT_ACTION_ITEM) {
        int spell_result = action == COMBAT_ACTION_ITEM ?
                           cmd_use_item(g, player, cs) :
                           cmd_cast_spell_menu(g, player, cs);
        if (!spell_result) return 0; /* cancelled/help/failed: no turn */
        mw_audio_play(&g->audio, MW_SFX_MAGIC);
        result = 1;
        if (spell_result == -1) {
            cs->monster_hp = 0;
            snprintf(msg1, sizeof(msg1), "THE %s IS DESTROYED!", mt->name);
        } else if (spell_result == -2) {
            snprintf(msg1, sizeof(msg1),
                     "THE %s IS IMMUNE TO THAT SPELL!", mt->name);
        } else if (spell_result == -3) {
            if (action == COMBAT_ACTION_CAST)
                snprintf(msg1, sizeof(msg1), "SPELL CAST SUCCESSFULLY!");
        } else if (spell_result == -4) {
            cs->player_fled = 1;
            if (action == COMBAT_ACTION_CAST)
                snprintf(msg1, sizeof(msg1), "YOU ESCAPE THE MONSTER!");
        } else if (spell_result == -6) {
            /* Original item pages already report their complete result before
               returning to the ordinary one-action combat exchange. */
        } else if (spell_result == -5) {
            snprintf(msg1, sizeof(msg1),
                     "THE SPELL FAILS TO AFFECT THE %s!", mt->name);
        } else if (spell_result > 0) {
            cs->monster_hp -= spell_result;
            snprintf(msg1, sizeof(msg1),
                     "YOUR SPELL HITS FOR %d DAMAGE!", spell_result);
        }
    } else if (action == COMBAT_ACTION_WAIT) {
        snprintf(msg1, sizeof(msg1), "YOU WAIT...");
        result = 1;
    }

    if (cs->monster_hp <= 0) {
        cs->active = 0;
        snprintf(msg2, sizeof(msg2), "THE %s IS DEAD!", mt->name);
        snprintf(msg3, sizeof(msg3), "SEARCHING THE REMAINS...");
    } else if (cs->fled) {
        cs->active = 0;
        snprintf(msg2, sizeof(msg2), "THE %s FLEES!", mt->name);
    } else if (!cs->player_fled) {
        combat_counterattack(g, cs, player, direction,
                             msg2, sizeof(msg2), msg3, sizeof(msg3));
    }

    if (result) character_tick_effects(g, player);
    draw_combat_screen(g, cs, player, msg1, msg2, msg3);
    /* A tapped attack keeps the native port's readable result pause.  Once
       the BIOS-style held-F stream begins, do not let that pause throttle
       WORLD's verified 92 ms typematic cadence. */
    game_delay(g, mw_hp_cur(player) && cs->monster_hp > 0 ?
               (action == COMBAT_ACTION_FIGHT &&
                g->input.fight_repeating ? 92 : 450) : 700);
    return result;
}

/* Compatibility entry point used by synthetic tests and the exploration
   Fight command.  It intentionally performs one exchange and returns. */
void combat_run(Game *g, CombatState *cs, Character *player) {
    (void)combat_take_turn(g, cs, player, COMBAT_ACTION_FIGHT);
}

/* ══════════════════════════════════════════════════════════════════════
   Weapon selection command (W key from exploration)
   MW_PORT: select_weapon (0x04538) and its inventory helpers.
   ══════════════════════════════════════════════════════════════════════ */

static int equipment_x(int dos_x) {
    return dos_x * LOGICAL_W / 1600;
}

static int equipment_y(int dos_y) {
    return dos_y * LOGICAL_H / 1200;
}

#define PAGE_BADGE_H 25
#define EQUIPMENT_PAGE_BADGE_W 94

/* Enhanced-only selectors have more entries than WORLD's original screens.
 * Draw the paging affordance instead of relying on a small text hint: the
 * outlined badge names the key and page, while the filled arrow makes the
 * direction visible at a glance even at the native 1024x768 resolution. */
static void draw_page_badge(Video *v, int x, int y, int w, int page,
                            int page_count, int compact) {
    char label[24];
    const int arrow_w = 19;
    const int arrow_x = x + w - arrow_w - 4;
    const int cx = arrow_x + arrow_w / 2;
    const int label_xsn = compact ? 7 : 5;
    const int label_xsd = compact ? 10 : 6;

    if (compact)
        snprintf(label, sizeof(label), "%s P%d",
                 page ? "PGUP" : "PGDN", page + 1);
    else if (page_count > 2 && page > 0 && page + 1 < page_count)
        snprintf(label, sizeof(label), "PGUP-DN P%d OF %d",
                 page + 1, page_count);
    else
        snprintf(label, sizeof(label), "%s P%d OF %d",
                 page + 1 == page_count ? "PGUP" : "PGDN",
                 page + 1, page_count);
    video_fill_rect(v, x, y, w, PAGE_BADGE_H, 1);
    video_hline(v, x, y, w, 14);
    video_hline(v, x, y + PAGE_BADGE_H - 1, w, 14);
    video_vline(v, x, y, PAGE_BADGE_H, 14);
    video_vline(v, x + w - 1, y, PAGE_BADGE_H, 14);
    video_draw_text_scaled_xy(v, x + 4, y + 3, label, 15,
                              label_xsn, label_xsd, 12, 17);

    if (page + 1 == page_count) {
        video_fill_rect(v, cx - 2, y + 9, 5, 11, 14);
        for (int row = 0; row < 7; row++)
            video_hline(v, cx - row, y + 8 - row, row * 2 + 1, 14);
    } else {
        video_fill_rect(v, cx - 2, y + 4, 5, 11, 14);
        for (int row = 0; row < 7; row++)
            video_hline(v, cx - row, y + 15 + row, row * 2 + 1, 14);
    }
}

static int equipment_page_badge_x(void) {
    return equipment_x(0x2D3) - EQUIPMENT_PAGE_BADGE_W;
}

static void equipment_clear_pane(Video *v) {
    video_fill_rect(v, 0, 0, equipment_x(0x2D3), equipment_y(0x1AE), 0);
}

static void equipment_text(Video *v, int y, const char *text, u8 color) {
    video_draw_text_scaled_xy(v, 0, y, text, color, 7, 6, 12, 17);
}

static void equipment_error(Game *g, int armor) {
    Video *v = &g->video;
    const char *const weapon_lines[] = {
        "CHARACTERS OF YOUR CLASS",
        "  CAN NOT USE THAT WEAPON.",
        "  YOU SHOULD, THEREFORE",
        "  DROP THE WEAPON TO SAVE",
        "  WEIGHT.",
        "HIT ANY KEY..."
    };
    const char *const armor_lines[] = {
        "CHARACTERS OF YOUR PROFESSION",
        "  CAN NOT WEAR ARMOR OF THIS TYPE",
        "  YOU CONTINUE TO WEAR YOUR OLD",
        "  ARMOR.",
        "HIT ANY KEY..."
    };
    const char *const *line = armor ? armor_lines : weapon_lines;
    int count = armor ? 5 : 6;
    equipment_clear_pane(v);
    for (int i = 0; i < count; i++)
        equipment_text(v, equipment_y(i * 0x32), line[i], 5);
    video_present(v);
    input_wait_any_key(&g->input);
}

static void format_equipment_line(const Character *player, int armor,
                                  int slot, int row, char *line,
                                  size_t line_size) {
    int owned = armor ? mw_armor_inventory_count(player, slot) :
                        mw_weapon_inventory_count(player, slot);
    const char *name = owned ?
        (armor ? combat_armor_name(slot) : weapon_stats[slot].name) :
        "--------";
    int enchant = owned ?
        (armor ? mw_armor_enchant(player, slot) :
                 mw_weapon_enchant(player, slot)) : 0;
    if (enchant)
        snprintf(line, line_size, "%d) %s, PLUS %d",
                 row + 1, name, enchant);
    else
        snprintf(line, line_size, "%d) %s", row + 1, name);
}

static void draw_equipment_page(Game *g, Character *player,
                                int armor, int page) {
    Video *v = &g->video;
    char line[96];
    equipment_clear_pane(v);
    equipment_text(v, 0,
                   page ? (armor ? "ENHANCED ARMOR:" : "ENHANCED WEAPONS:")
                        : (armor ? "PLEASE SELECT YOUR ARMOR:"
                                 : "PLEASE SELECT YOUR WEAPON:"),
                   15);

    for (int row = 0; row < 8; row++) {
        int slot = page ? (armor ? 8 + row : 12 + row) : row;
        int valid = page ? row < 8 : 1;
        if (valid)
            format_equipment_line(player, armor, slot, row,
                                  line, sizeof(line));
        else
            snprintf(line, sizeof(line), "%d) --------", row + 1);
        /* shop_weapon/func_0691B renders all eight original rows with
           attribute five; ownership changes text, not color. */
        equipment_text(v, equipment_y(0x28 + row * 0x32), line, 5);
    }

    /* WORLD's original page is untouched in Classic. */
    if (mw_experience_mode(player) == MW_EXPERIENCE_ENHANCED)
        draw_page_badge(v, equipment_page_badge_x(), 0,
                        EQUIPMENT_PAGE_BADGE_W, page, 2, 1);
    video_present(v);
}

static int equipment_mouse_key(Game *g, int key, int *page,
                               const Character *player) {
    int x, y;
    if (key != INPUT_MOUSE_CLICK) return key;
    if (!game_mouse_click_logical(g, &x, &y)) return -2;
    if (mw_experience_mode(player) == MW_EXPERIENCE_ENHANCED &&
        x >= equipment_page_badge_x() &&
        x < equipment_page_badge_x() + EQUIPMENT_PAGE_BADGE_W &&
        y >= 0 && y < PAGE_BADGE_H) {
        *page = !*page;
        return -1;
    }
    if (x >= 0 && x < equipment_x(0x2D3) &&
        y >= equipment_y(0x28)) {
        int row = (y - equipment_y(0x28)) / equipment_y(0x32);
        if (row >= 0 && row < 8) return '1' + row;
    }
    return -2;
}

static int equipment_page_key(Game *g, int key, int *page,
                              const Character *player) {
    if (key == 0) {
        int scan = input_getch(&g->input);
        if (mw_experience_mode(player) == MW_EXPERIENCE_ENHANCED &&
            (scan == 0x49 || scan == 0x51)) {
            *page = scan == 0x51;
            return -1;
        }
        return -2;
    }
    if (mw_experience_mode(player) == MW_EXPERIENCE_ENHANCED) {
        if (key == 'n' || key == 'N') {
            *page = 1;
            return -1;
        }
        if (key == 'p' || key == 'P') {
            *page = 0;
            return -1;
        }
    }
    return key;
}

void cmd_weapons(Game *g, Character *player) {
    int page = 0;
    for (;;) {
        draw_equipment_page(g, player, 0, page);
        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input) || key == 0x1B) return;
        key = equipment_mouse_key(g, key, &page, player);
        key = equipment_page_key(g, key, &page, player);
        if (key == -1) continue;
        if (key < '1' || key > '8') continue;
        int weapon = page ? 12 + key - '1' : key - '1';
        if (!mw_weapon_inventory_count(player, weapon)) return;
        if (!combat_weapon_allowed(player, weapon)) {
            equipment_error(g, 0);
            return;
        }
        player->equipped_weapon = (u8)weapon;
        return;
    }
}

/* ══════════════════════════════════════════════════════════════════════
   Armor selection command (A key from exploration)
   MW_PORT: armor branch of func_0F6E5 and the original armor records.
   ══════════════════════════════════════════════════════════════════════ */

void cmd_armor(Game *g, Character *player) {
    int page = 0;
    for (;;) {
        draw_equipment_page(g, player, 1, page);
        int key = input_getch(&g->input);
        if (input_poll_quit(&g->input) || key == 0x1B) return;
        key = equipment_mouse_key(g, key, &page, player);
        key = equipment_page_key(g, key, &page, player);
        if (key == -1) continue;
        if (key < '1' || key > '8') continue;
        int armor = page ? 8 + key - '1' : key - '1';
        if (!mw_armor_inventory_count(player, armor)) return;
        if (!combat_armor_allowed(player, armor)) {
            equipment_error(g, 1);
            return;
        }
        player->equipped_armor = (u8)armor;
        return;
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

/* Legacy preparation-only screen retained for source comparison.  The live
 * C command also goes through cmd_cast_spell_menu(); do not dispatch here. */
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
        int key = input_wait_any_key(&g->input);
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
                heal_hp_capped(player, 5);
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
                heal_hp_capped(player, 20);
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
                heal_hp_capped(player, 50);
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
                mw_set_hp_cur(player, mw_hp_max(player));
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
#define SPELL_DEEP_COL_2 (LOGICAL_W / 2)
#define SPELL_PAGE_BADGE_W 210
#define SPELL_PAGE_BADGE_X (LOGICAL_W - 120 - SPELL_PAGE_BADGE_W - 6)
#define SPELL_NOTICE_WRAP_CHARS 29
#define SPELL_NOTICE_LAST_ROW 9

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

/* Extract one word-wrapped line without ever advancing beyond the message
 * pane.  Result strings from the original 30 spells happened to be short
 * enough for the DOS window; Enhanced spells made the missing bound visible.
 * Keep the wrapping in the shared notice renderer so books, scrolls, wands,
 * papers, failure messages, and every original spell use the same rule. */
static int spell_wrap_next(const char **cursor,
                           char line[SPELL_NOTICE_WRAP_CHARS + 1]) {
    const char *p = cursor ? *cursor : NULL;
    size_t remaining, take;
    if (!p) return 0;
    while (*p == ' ' || *p == '\n' || *p == '\r') ++p;
    if (!*p) {
        *cursor = p;
        return 0;
    }

    remaining = strcspn(p, "\r\n");
    take = remaining < SPELL_NOTICE_WRAP_CHARS ?
           remaining : SPELL_NOTICE_WRAP_CHARS;
    if (remaining > SPELL_NOTICE_WRAP_CHARS) {
        size_t split = take;
        while (split > 0 && p[split] != ' ') --split;
        if (split > 0) take = split;
    }

    memcpy(line, p, take);
    line[take] = '\0';
    while (take > 0 && line[take - 1] == ' ') line[--take] = '\0';
    p += take;
    while (*p == ' ') ++p;
    if (*p == '\r') ++p;
    if (*p == '\n') ++p;
    *cursor = p;
    return 1;
}

static int spell_draw_wrapped(Game *g, int row, const char *text, u8 color) {
    const char *cursor = text;
    char line[SPELL_NOTICE_WRAP_CHARS + 1];
    while (row <= SPELL_NOTICE_LAST_ROW &&
           spell_wrap_next(&cursor, line))
        spell_line(g, row++, line, color);
    return row;
}

static void spell_selector_text(Game *g, int x, int row,
                                const char *text, u8 color) {
    video_draw_text_scaled_xy(&g->video, x, row * SPELL_SELECTOR_ROW_H,
                              text, color, 1, 1, 12, 17);
}

static void spell_draw_selector_backdrop(Game *g, Character *p,
                                         CombatState *cs) {
    /* WORLD's 30-spell casting/using selector is a full-width TOP STRIP,
     * never a full-screen page.  The only full-display 30-item tables are
     * the read-only spell/scroll/wand/paper lists reached through Pockets. */
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
    int row = spell_draw_wrapped(g, 0, line1, 12);
    if (line2 && *line2) {
        /* Preserve WORLD's blank separator after a one-line heading while
         * allowing a wrapped heading to grow naturally. */
        row = row < 2 ? 2 : row + 1;
        spell_draw_wrapped(g, row, line2, 15);
    }
    spell_line(g, 10, "HIT ANY KEY...", 14);
    video_present(&g->video);
    input_wait_any_key(&g->input);
}

static int class_can_read_spellbook_vanilla(const Character *p,
                                             int category) {
    if (p->class_id == CLASS_MONK) return 1;
    if (p->class_id == CLASS_FIGHTER) return 0;
    if (category <= SPELL_CAT_PREPARATION) return 1;
    if (category == SPELL_CAT_WIZARD)
        return p->class_id == CLASS_WIZARD || p->class_id == CLASS_SAGE ||
               p->class_id == CLASS_MAGE ||
               p->class_id == CLASS_SPELLBLADE;
    return p->class_id == CLASS_WORSHIPPER || p->class_id == CLASS_PRIEST ||
           p->class_id == CLASS_SAGE || p->class_id == CLASS_PALADIN;
}

static int class_can_read_spellbook(const Character *p, int category) {
    return mw_universal_access(p) ||
           class_can_read_spellbook_vanilla(p, category);
}

int combat_spell_source_allowed(const Character *p, int category,
                                int source) {
    if (!p || category < SPELL_CAT_WIZARD || category > SPELL_CAT_PRIEST ||
        source < 0 || source > 3)
        return 0;
    if (source == 0)
        return class_can_read_spellbook_vanilla(p, category);
    /* WORLD lets every non-Fighter invoke either battle tradition from an
       item. Fighters cannot use scrolls or wands, but can cast magic paper. */
    return p->class_id != CLASS_FIGHTER || source == 3;
}

static int spell_is_available(Character *p, int category, int index,
                              int source, int help) {
    if (category < SPELL_CAT_PERMANENT || category > SPELL_CAT_PRIEST ||
        index < 0 || index >= mw_spell_catalog_count(p))
        return 0;
    if (help) return 1;
    if (source == 0) {
        if (!class_can_read_spellbook(p, category)) return 0;
        /* Monks retain their original access to all 30 DOS spells, but the
         * Enhanced quest catalog must still be earned from its bosses. */
        return (p->class_id == CLASS_MONK &&
                index < MW_ORIGINAL_SPELL_COUNT) ||
               p->spells[category][index] != 0;
    }
    if (!mw_universal_access(p) &&
        p->class_id == CLASS_FIGHTER && source != 3) return 0;
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

static int spell_deep_grid_index(int column, int row) {
    int rows = (MW_DEEP_SPELL_COUNT + 1) / 2;
    int deep = column * rows + row;
    if (column < 0 || column > 1 || row < 0 || row >= rows ||
        deep >= MW_DEEP_SPELL_COUNT)
        return -1;
    return MW_DEEP_SPELL_FIRST + deep;
}

static int select_spell_index(Game *g, Character *p, CombatState *cs,
                              int category, int source, int help,
                              int maximum_level) {
    char line[128];
    const char *const *names = spell_names_for_category(category);
    int page = 0;
    int deep_enabled = mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED &&
                       maximum_level > 10;
    int page_count = deep_enabled ? 2 : 1;
    for (;;) {
        int deep_rows = (MW_DEEP_SPELL_COUNT + 1) / 2;
        spell_draw_selector_backdrop(g, p, cs);
        spell_selector_text(g, 0, 0,
            page == 1 ? "DEEP SPELLS - LEVELS 11 THROUGH 15:" :
            (help ? "PRESS A LETTER OR A NUMBER TO GET A DESCRIPTION:" :
            (source == 0 ?
             "SELECT A SPELL-SPELLS USE ONE SPELL POINT PER LEVEL:" :
             "SELECT A SPELL FROM THE FOLLOWING:")), 4);
        if (deep_enabled)
            draw_page_badge(&g->video, SPELL_PAGE_BADGE_X, 0,
                            SPELL_PAGE_BADGE_W, page, page_count, 0);
        spell_selector_text(g, LOGICAL_W - 90, 0, "ESCAPE", 3);

        if (!page) {
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
        } else {
            for (int deep = 0; deep < MW_DEEP_SPELL_COUNT; deep++) {
                int column = deep / deep_rows;
                int row = deep % deep_rows;
                int index = MW_DEEP_SPELL_FIRST + deep;
                int available = index / 3 < maximum_level &&
                    spell_is_available(p, category, index, source, help);
                snprintf(line, sizeof(line), "%d- %c)%s",
                         index / 3 + 1, 'A' + deep,
                         available ? names[index] : "NOT YET FOUND");
                spell_selector_text(g,
                    column ? SPELL_DEEP_COL_2 : 0, row + 1, line, 8);
            }
        }
        video_present(&g->video);
        int key = input_getch(&g->input);
        if (key == 0x1B || input_poll_quit(&g->input)) return -1;
        if (key == 0) {
            int scan = input_getch(&g->input);
            if (deep_enabled && scan == 0x49) {
                if (page > 0) --page;
                continue;
            }
            if (deep_enabled && scan == 0x51) {
                if (page + 1 < page_count) ++page;
                continue;
            }
            continue;
        }
        int index = -1;
        if (key == INPUT_MOUSE_CLICK) {
            int x, y;
            if (!game_mouse_click_logical(g, &x, &y)) continue;
            if (y < SPELL_SELECTOR_ROW_H && x >= LOGICAL_W - 120)
                return -1;
            if (deep_enabled && y < PAGE_BADGE_H &&
                x >= SPELL_PAGE_BADGE_X &&
                x < SPELL_PAGE_BADGE_X + SPELL_PAGE_BADGE_W) {
                page = (page + 1) % page_count;
                continue;
            }
            int row = y / SPELL_SELECTOR_ROW_H - 1;
            if (!page) {
                int slot = x < SPELL_SELECTOR_COL_2 ? 0 :
                           (x < SPELL_SELECTOR_COL_3 ? 1 : 2);
                if (row >= 0 && row < 10) index = row * 3 + slot;
            } else if (row >= 0 && row < deep_rows) {
                int column = x < SPELL_DEEP_COL_2 ? 0 : 1;
                index = spell_deep_grid_index(column, row);
            }
        } else {
            if (page) {
                int first_key = 'A';
                int last_key = first_key + MW_DEEP_SPELL_COUNT;
                if (key >= 'a' && key < 'a' + MW_DEEP_SPELL_COUNT)
                    key -= 'a' - 'A';
                if (key >= first_key && key < last_key)
                    index = MW_DEEP_SPELL_FIRST + key - 'A';
            } else {
                index = spell_index_from_hotkey(key);
            }
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
        if (index == 33)
            append_text(out, size, "CREATES A ONE-USE SCROLL CONTAINING ANY LEVEL 1-14 SPELL. ");
        else if (index == 34)
            append_text(out, size, "ADDS TEN CHARGES OF ANY LEVEL 1-14 SPELL TO A WAND. ");
        else if (index == 35)
            append_text(out, size, "RAISES THE EQUIPPED PHYSICAL WEAPON TO AT LEAST +500. ");
        else if (index == 36)
            append_text(out, size, "RAISES THE EQUIPPED ARMOR TO AT LEAST +350. ");
        else if (index == 37)
            append_text(out, size, "RAISES INNATE BODY ARMOR TO AT LEAST +300. ");
        else if (index == 38)
            append_text(out, size, "CREATES A ONE-USE SCROLL CONTAINING ANY LEVEL 1-14 SPELL. ");
        else if (index == 39)
            append_text(out, size, "ADDS TWENTY CHARGES OF ANY LEVEL 1-14 SPELL TO A WAND. ");
        else if (index == 40)
            append_text(out, size, "RAISES THE EQUIPPED PHYSICAL WEAPON TO AT LEAST +1000. ");
        else if (index == 41)
            append_text(out, size, "RAISES THE EQUIPPED ARMOR TO AT LEAST +750. ");
        else if (index == 42)
            append_text(out, size, "RAISES INNATE BODY ARMOR TO AT LEAST +650. ");
        else if (index == 43)
            append_text(out, size, "CREATES A ONE-USE SCROLL CONTAINING ANY LEVEL 1-15 SPELL. ");
        else if (index == 44)
            append_text(out, size, "ADDS FORTY CHARGES OF ANY LEVEL 1-15 SPELL TO A WAND. ");
        else if (index == 30)
            append_text(out, size, "RAISES THE EQUIPPED PHYSICAL WEAPON TO AT LEAST +150. ");
        else if (index == 31)
            append_text(out, size, "RAISES THE EQUIPPED ARMOR TO AT LEAST +100. ");
        else if (index == 32)
            append_text(out, size, "RAISES INNATE BODY ARMOR TO AT LEAST +100. ");
        else if (index == 2 || index == 11)
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
            append_text(out, size, "REMOVES TEN YEARS OF AGE. AGE IS TRACKED FOR RECORDS AND DOES NOT DIRECTLY REDUCE ATTRIBUTES. ");
        else if (index == 14 || index == 20 || index == 29)
            append_text(out, size, "ADDS PERMANENT INNATE BODY DEFENSE THAT STACKS WITH PHYSICAL ARMOR. ");
        else if (index == 8 || index == 13 || index == 18)
            append_text(out, size, "UPGRADES THE RING OF PROTECTION; ITS PLUS IS SUBTRACTED FROM ENEMY HIT SCORE. ");
        else if (index == 10 || index == 16 || index == 19 || index == 25)
            append_text(out, size, "UPGRADES THE ANTI-MAGIC RING. WORLD STORES AND DISPLAYS IT WITHOUT USING IT AGAINST ORIGINAL ENEMIES; ENHANCED DEEP MAGIC GIVES EACH PLUS AN 8 PERCENT DISPEL CHANCE. ");
        else
            append_text(out, size, "UPGRADES THE EQUIPPED ITEM ONLY IF THIS ENCHANTMENT IS BETTER; ITEM AND TEMPORARY ENCHANTS STACK. ");
    } else if (category == SPELL_CAT_PREPARATION) {
        append_text(out, size, "PREPARATION MAGIC TAKES THREE MINUTES OUTSIDE BATTLE; BUFFS LAST UNTIL AN INN REST. ");
        if (index == 30) append_text(out, size, "MOVES DOWN FIFTY FLOORS, CLAMPING AT THE EXPERIENCE'S FINAL FLOOR. ");
        else if (index == 31) append_text(out, size, "MOVES UP FIFTY FLOORS, CLAMPING AT TOWN. ");
        else if (index == 32) append_text(out, size, "GRANTS PROTECTION TIER 5 AND EVERY POISON, DISEASE, COLD, FIRE, AND LEVEL-DRAIN RESISTANCE FOR AT LEAST 600 TURNS. ");
        else if (index == 33) append_text(out, size, "REVEALS EVERY MAP CELL ON THE CURRENT FLOOR. ");
        else if (index == 34) append_text(out, size, "RETURNS THE CASTER TO A RANDOM SAFE POSITION IN TOWN. ");
        else if (index == 35) append_text(out, size, "MOVES DOWN ONE HUNDRED FLOORS, CLAMPING AT THE EXPERIENCE'S FINAL FLOOR. ");
        else if (index == 36) append_text(out, size, "MOVES UP ONE HUNDRED FLOORS, CLAMPING AT TOWN. ");
        else if (index == 37) append_text(out, size, "GRANTS PROTECTION TIER 8 AND EVERY RESISTANCE FOR AT LEAST 1200 TURNS. ");
        else if (index == 38) append_text(out, size, "REVEALS EVERY MAP CELL ON THE CURRENT FLOOR. ");
        else if (index == 39) append_text(out, size, "BINDS A ONE-USE RAISE-DEAD RETURN POINT TO THE CASTER'S CURRENT FLOOR AND POSITION; DEATH CONSUMES IT AND COSTS ONE CONSTITUTION. ");
        else if (index == 40) append_text(out, size, "MOVES DOWN TWO HUNDRED FLOORS, CLAMPING AT THE EXPERIENCE'S FINAL FLOOR. ");
        else if (index == 41) append_text(out, size, "MOVES UP TWO HUNDRED FLOORS, CLAMPING AT TOWN. ");
        else if (index == 42) append_text(out, size, "GRANTS PROTECTION TIER 10 AND EVERY RESISTANCE FOR AT LEAST 3000 TURNS. ");
        else if (index == 43) append_text(out, size, "GRANTS FEATHER, INVISIBILITY, AND FAST MOVE UNTIL THE NEXT INN REST. ");
        else if (index == 44) append_text(out, size, "RESTORES CURRENT HEALTH TO MAXIMUM AND CURES POISON AND DISEASE WITHOUT RESTORING SPELL POINTS. ");
        else if (index == 2) append_text(out, size, "HEALS 1-20 HP, WITH A SMALL WISDOM BONUS. ");
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
             "CONJURES POWER WEAPON %d (DAMAGE DIE 0-%d) FOR 60 TURNS; SAME-TIER RECASTS EXTEND IT. ENHANCED MODE NEVER REPLACES A STRONGER WEAPON DIE. ",
             sd->param1, combat_power_weapon_max_damage(sd->param1) - 1); break;
        case BS_SLEEP: append_text(out, size, "PUTS A NON-IMMUNE MONSTER TO SLEEP FOR ABOUT 10 TURNS. "); break;
        case BS_HOLD: append_text(out, size, "PARALYZES A NON-IMMUNE MONSTER FOR ABOUT 15 TURNS. "); break;
        case BS_STOP: snprintf(out + strlen(out), size - strlen(out),
             "STOPS A NON-IMMUNE MONSTER FOR ABOUT %d TURNS. ",
             sd->param1 > 0 ? sd->param1 : 10); break;
        case BS_DRAIN: append_text(out, size, "PERMANENTLY LOWERS MONSTER LEVEL BY THE CASTER'S WISDOM; LEVEL BELOW ONE DESTROYS IT. "); break;
        case BS_DRAIN_SCALE: snprintf(out + strlen(out), size - strlen(out),
             "LOWERS MONSTER LEVEL BY WISDOM X %d PLUS HALF CASTER LEVEL; LEVEL BELOW ONE DESTROYS IT. ",
             sd->param1); break;
        case BS_DAMAGE_PERCENT: snprintf(out + strlen(out), size - strlen(out),
             "DEALS %d PERCENT OF THE MONSTER'S MAXIMUM HP PLUS %d DAMAGE. ",
             sd->param1, sd->param2); break;
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
        case BS_RESTORE_ALL: append_text(out, size, "RESTORES CURRENT HP TO MAXIMUM AND CURES POISON AND DISEASE DURING COMBAT. "); break;
        case BS_LIFE_CONVERGENCE: snprintf(out + strlen(out), size - strlen(out),
             "DRAINS %d PERCENT OF CURRENT MONSTER HP PLUS %d DAMAGE; HALF THE DAMAGE HEALS THE CASTER, CAPPED AT ONE-THIRD MAXIMUM HP. ",
             sd->param1 > 0 ? 100 / sd->param1 : 10, sd->param2); break;
        case BS_PHOENIX_PRAYER: snprintf(out + strlen(out), size - strlen(out),
             "HEALS HALF MAXIMUM HP, CURES POISON AND DISEASE, GRANTS PROTECTION TIER %d FOR %d TURNS, AND FIRE RESISTANCE FOR %d TURNS. ",
             sd->param1, sd->param2, sd->param2 * 2); break;
        case BS_AEGIS: snprintf(out + strlen(out), size - strlen(out),
             "GRANTS PROTECTION TIER %d AND ALL FIVE RESISTANCES FOR AT LEAST %d TURNS. ",
             sd->param1, sd->param2); break;
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
    int key = input_wait_any_key(&g->input);
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
        int w = p->equipped_weapon < WEAPON_STAT_COUNT &&
                !(p->equipped_weapon >= 8 && p->equipped_weapon <= 11) ?
                p->equipped_weapon : 0;
        if (mw_weapon_enchant(p, w) >= value) return 0;
        mw_set_weapon_enchant(p, w, value);
        snprintf(message, message_size, "WEAPON PERMANENTLY ENCHANTED +%d!", value);
    } else if (index == 3 || index == 9 || index == 15 || index == 22) {
        value = index == 3 ? 1 : index == 9 ? 2 : index == 15 ? 3 : 4;
        int armor = p->equipped_armor < ARMOR_STAT_COUNT ?
                    p->equipped_armor : 0;
        if (mw_armor_enchant(p, armor) >= value) return 0;
        mw_set_armor_enchant(p, armor, value);
        snprintf(message, message_size, "ARMOR PERMANENTLY ENCHANTED +%d!", value);
    } else if (index == 1 || index == 4 || index == 7 || index == 26) {
        value = index == 1 ? 1 : index == 4 ? 3 : index == 7 ? 5 : 25;
        add_hp_capped(p, (unsigned)value);
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
    } else if (index == 30) {
        int w = p->equipped_weapon < WEAPON_STAT_COUNT &&
                !(p->equipped_weapon >= 8 && p->equipped_weapon <= 11) ?
                p->equipped_weapon : 0;
        if (mw_weapon_enchant(p, w) >= 150) return 0;
        mw_set_weapon_enchant(p, w, 150);
        snprintf(message, message_size,
                 "WEAPON PERMANENTLY ENCHANTED TO +150!");
    } else if (index == 31) {
        int armor = p->equipped_armor < ARMOR_STAT_COUNT ?
                    p->equipped_armor : 0;
        if (mw_armor_enchant(p, armor) >= 100) return 0;
        mw_set_armor_enchant(p, armor, 100);
        snprintf(message, message_size,
                 "ARMOR PERMANENTLY ENCHANTED TO +100!");
    } else if (index == 32) {
        if (mw_body_armor_plus(p) >= 100) return 0;
        mw_set_body_armor_plus(p, 100);
        snprintf(message, message_size, "BODY ARMOR IS NOW +100!");
    } else if (index == 33) {
        if (!select_created_spell(g, p, cs, 14,
                                  &target_category, &target_index))
            return 0;
        if (p->scrolls[target_category][target_index] < 255)
            ++p->scrolls[target_category][target_index];
        snprintf(message, message_size, "THE DEEP SCROLL IS COMPLETE!");
    } else if (index == 34) {
        if (!select_created_spell(g, p, cs, 14,
                                  &target_category, &target_index))
            return 0;
        int charges = p->wands[target_category][target_index] + 10;
        p->wands[target_category][target_index] =
            (u8)(charges > 255 ? 255 : charges);
        snprintf(message, message_size,
                 "THE DEEP WAND GAINS TEN CHARGES!");
    } else if (index == 35) {
        int w = p->equipped_weapon < WEAPON_STAT_COUNT &&
                !(p->equipped_weapon >= 8 && p->equipped_weapon <= 11) ?
                p->equipped_weapon : 0;
        if (mw_weapon_enchant(p, w) >= 500) return 0;
        mw_set_weapon_enchant(p, w, 500);
        snprintf(message, message_size,
                 "WEAPON PERMANENTLY ENCHANTED TO +500!");
    } else if (index == 36) {
        int armor = p->equipped_armor < ARMOR_STAT_COUNT ?
                    p->equipped_armor : 0;
        if (mw_armor_enchant(p, armor) >= 350) return 0;
        mw_set_armor_enchant(p, armor, 350);
        snprintf(message, message_size,
                 "ARMOR PERMANENTLY ENCHANTED TO +350!");
    } else if (index == 37) {
        if (mw_body_armor_plus(p) >= 300) return 0;
        mw_set_body_armor_plus(p, 300);
        snprintf(message, message_size, "BODY ARMOR IS NOW +300!");
    } else if (index == 38) {
        if (!select_created_spell(g, p, cs, 14,
                                  &target_category, &target_index))
            return 0;
        if (p->scrolls[target_category][target_index] < UINT8_MAX)
            ++p->scrolls[target_category][target_index];
        snprintf(message, message_size,
                 "THE ASCENDANT SCROLL IS COMPLETE!");
    } else if (index == 39) {
        if (!select_created_spell(g, p, cs, 14,
                                  &target_category, &target_index))
            return 0;
        int charges = p->wands[target_category][target_index] + 20;
        p->wands[target_category][target_index] =
            (u8)(charges > 255 ? 255 : charges);
        snprintf(message, message_size,
                 "THE ASCENDANT WAND GAINS TWENTY CHARGES!");
    } else if (index == 40) {
        int w = p->equipped_weapon < WEAPON_STAT_COUNT &&
                !(p->equipped_weapon >= 8 && p->equipped_weapon <= 11) ?
                p->equipped_weapon : 0;
        if (mw_weapon_enchant(p, w) >= 1000) return 0;
        mw_set_weapon_enchant(p, w, 1000);
        snprintf(message, message_size,
                 "WEAPON PERMANENTLY ENCHANTED TO +1000!");
    } else if (index == 41) {
        int armor = p->equipped_armor < ARMOR_STAT_COUNT ?
                    p->equipped_armor : 0;
        if (mw_armor_enchant(p, armor) >= 750) return 0;
        mw_set_armor_enchant(p, armor, 750);
        snprintf(message, message_size,
                 "ARMOR PERMANENTLY ENCHANTED TO +750!");
    } else if (index == 42) {
        if (mw_body_armor_plus(p) >= 650) return 0;
        mw_set_body_armor_plus(p, 650);
        snprintf(message, message_size, "BODY ARMOR IS NOW +650!");
    } else if (index == 43) {
        if (!select_created_spell(g, p, cs, 15,
                                  &target_category, &target_index))
            return 0;
        if (p->scrolls[target_category][target_index] < UINT8_MAX)
            ++p->scrolls[target_category][target_index];
        snprintf(message, message_size, "THE MYTHIC SCROLL IS COMPLETE!");
    } else if (index == 44) {
        if (!select_created_spell(g, p, cs, 15,
                                  &target_category, &target_index))
            return 0;
        int charges = p->wands[target_category][target_index] + 40;
        p->wands[target_category][target_index] =
            (u8)(charges > UINT8_MAX ? UINT8_MAX : charges);
        snprintf(message, message_size,
                 "THE MYTHIC WAND GAINS FORTY CHARGES!");
    } else return 0;
    return 1;
}

static void heal_random(Game *g, Character *p, int low, int high, char *message,
                        size_t message_size) {
    int amount = low + game_rand(g) % (high - low + 1) + p->stat_wis / 10;
    heal_hp_capped(p, (unsigned)amount);
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
    case 8: if (!p->eff_str_bonus && p->stat_str <= MW_PLAYER_STAT_MAX - 5) { p->stat_str += 5; p->eff_str_bonus=1; } snprintf(message,message_size,"STRENGTH INCREASED BY 5 UNTIL THE INN!"); break;
    case 9: if (mw_enchant_wpn_spell(p) < 3) mw_set_enchant_wpn_spell(p,3); snprintf(message,message_size,"WEAPONS ENCHANTED +3 UNTIL THE INN!"); break;
    case 10: if (!p->eff_agi_bonus && p->stat_agi <= MW_PLAYER_STAT_MAX - 5) { p->stat_agi += 5; p->eff_agi_bonus=1; } snprintf(message,message_size,"AGILITY INCREASED BY 5 UNTIL THE INN!"); break;
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
    case 21: if(!p->eff_super_str && p->stat_str <= MW_PLAYER_STAT_MAX - 10){p->stat_str+=10;p->eff_super_str=1;} snprintf(message,message_size,"STRENGTH INCREASED BY 10 UNTIL THE INN!"); break;
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
    case 24: if(!p->eff_super_agi && p->stat_agi <= MW_PLAYER_STAT_MAX - 10){p->stat_agi+=10;p->eff_super_agi=1;} snprintf(message,message_size,"AGILITY INCREASED BY 10 UNTIL THE INN!"); break;
    case 25: p->poisoned_turns=0; snprintf(message,message_size,"POISON CURED!"); break;
    case 26: mw_set_hp_cur(p,mw_hp_max(p)); snprintf(message,message_size,"ALL WOUNDS HEALED!"); break;
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
    case 30:
        if (g->cur_floor >= game_traversal_rules(g)->max_floor) {
            snprintf(message, message_size,
                     "YOU CANNOT DESCEND ANY FARTHER!");
            return 0;
        }
        target = g->cur_floor + 50;
        if (target > game_traversal_rules(g)->max_floor)
            target = game_traversal_rules(g)->max_floor;
        if (!game_change_floor(g, p, target) || !game_relocate(g, p))
            return 0;
        snprintf(message, message_size, "YOU DESCEND FIFTY LEVELS!");
        break;
    case 31:
        if (g->cur_floor <= 0) {
            snprintf(message, message_size,
                     "YOU CANNOT ASCEND ANY FARTHER!");
            return 0;
        }
        target = g->cur_floor - 50;
        if (target < 0) target = 0;
        if (!game_change_floor(g, p, target) || !game_relocate(g, p))
            return 0;
        snprintf(message, message_size, "YOU ASCEND FIFTY LEVELS!");
        break;
    case 32:
        if (p->eff_protect_lv < 5) p->eff_protect_lv = 5;
        if (p->eff_protect_turns < 600) p->eff_protect_turns = 600;
        if (p->eff_resist_poison < 600) p->eff_resist_poison = 600;
        if (p->eff_resist_disease < 600) p->eff_resist_disease = 600;
        if (p->eff_anti_cold < 600) p->eff_anti_cold = 600;
        if (p->eff_anti_fire < 600) p->eff_anti_fire = 600;
        if (p->eff_resist_drain < 600) p->eff_resist_drain = 600;
        snprintf(message, message_size,
                 "DEEP SANCTUARY PROTECTS YOU FOR 600 TURNS!");
        break;
    case 33:
        memset(g->visited, 1, sizeof(g->visited));
        snprintf(message, message_size,
                 "THE ENTIRE DUNGEON LEVEL IS REVEALED!");
        break;
    case 34:
        if (!game_change_floor(g, p, 0) || !game_relocate(g, p))
            return 0;
        snprintf(message, message_size, "A TOWN PORTAL CARRIES YOU HOME!");
        break;
    case 35:
        if (g->cur_floor >= game_traversal_rules(g)->max_floor) {
            snprintf(message, message_size,
                     "YOU CANNOT DESCEND ANY FARTHER!");
            return 0;
        }
        target = g->cur_floor + 100;
        if (target > game_traversal_rules(g)->max_floor)
            target = game_traversal_rules(g)->max_floor;
        if (!game_change_floor(g, p, target) || !game_relocate(g, p))
            return 0;
        snprintf(message, message_size, "YOU DESCEND ONE HUNDRED LEVELS!");
        break;
    case 36:
        if (g->cur_floor <= 0) {
            snprintf(message, message_size,
                     "YOU CANNOT ASCEND ANY FARTHER!");
            return 0;
        }
        target = g->cur_floor - 100;
        if (target < 0) target = 0;
        if (!game_change_floor(g, p, target) || !game_relocate(g, p))
            return 0;
        snprintf(message, message_size, "YOU ASCEND ONE HUNDRED LEVELS!");
        break;
    case 37:
        if (p->eff_protect_lv < 8) p->eff_protect_lv = 8;
        if (p->eff_protect_turns < 1200) p->eff_protect_turns = 1200;
        if (p->eff_resist_poison < 1200) p->eff_resist_poison = 1200;
        if (p->eff_resist_disease < 1200) p->eff_resist_disease = 1200;
        if (p->eff_anti_cold < 1200) p->eff_anti_cold = 1200;
        if (p->eff_anti_fire < 1200) p->eff_anti_fire = 1200;
        if (p->eff_resist_drain < 1200) p->eff_resist_drain = 1200;
        snprintf(message, message_size,
                 "ETERNAL SANCTUARY PROTECTS YOU FOR 1200 TURNS!");
        break;
    case 38:
        memset(g->visited, 1, sizeof(g->visited));
        snprintf(message, message_size,
                 "WORLD REVEAL MAPS EVERY CELL ON THIS FLOOR!");
        break;
    case 39: {
        int anchor_floor = g->cur_floor;
        int anchor_x = g->cur_x;
        int anchor_y = g->cur_y;
        if (anchor_floor < 0) anchor_floor = 0;
        if (anchor_floor > game_traversal_rules(g)->max_floor)
            anchor_floor = game_traversal_rules(g)->max_floor;
        if (anchor_x < 0 || anchor_x >= MAP_W) anchor_x = MAP_W / 2;
        if (anchor_y < 0 || anchor_y >= MAP_H) anchor_y = MAP_H / 2;
        p->raise_floor = (u16)anchor_floor;
        p->raise_x = (u16)anchor_x;
        p->raise_y = (u16)anchor_y;
        snprintf(message, message_size,
                 "YOUR SOUL IS ANCHORED HERE AGAINST ONE DEATH!");
        break;
    }
    case 40:
        if (g->cur_floor >= game_traversal_rules(g)->max_floor) {
            snprintf(message, message_size,
                     "YOU CANNOT DESCEND ANY FARTHER!");
            return 0;
        }
        target = g->cur_floor + 200;
        if (target > game_traversal_rules(g)->max_floor)
            target = game_traversal_rules(g)->max_floor;
        if (!game_change_floor(g, p, target) || !game_relocate(g, p))
            return 0;
        snprintf(message, message_size, "YOU DESCEND TWO HUNDRED LEVELS!");
        break;
    case 41:
        if (g->cur_floor <= 0) {
            snprintf(message, message_size,
                     "YOU CANNOT ASCEND ANY FARTHER!");
            return 0;
        }
        target = g->cur_floor - 200;
        if (target < 0) target = 0;
        if (!game_change_floor(g, p, target) || !game_relocate(g, p))
            return 0;
        snprintf(message, message_size, "YOU ASCEND TWO HUNDRED LEVELS!");
        break;
    case 42:
        if (p->eff_protect_lv < 10) p->eff_protect_lv = 10;
        if (p->eff_protect_turns < 3000) p->eff_protect_turns = 3000;
        if (p->eff_resist_poison < 3000) p->eff_resist_poison = 3000;
        if (p->eff_resist_disease < 3000) p->eff_resist_disease = 3000;
        if (p->eff_anti_cold < 3000) p->eff_anti_cold = 3000;
        if (p->eff_anti_fire < 3000) p->eff_anti_fire = 3000;
        if (p->eff_resist_drain < 3000) p->eff_resist_drain = 3000;
        snprintf(message, message_size,
                 "MYTHIC SANCTUARY PROTECTS YOU FOR 3000 TURNS!");
        break;
    case 43:
        if (p->eff_feather != 100) p->eff_feather = 1;
        if (p->eff_invisible != 100) p->eff_invisible = 1;
        p->eff_fast_move = 1;
        snprintf(message, message_size,
                 "ASTRAL FORM LASTS UNTIL THE INN!");
        break;
    case 44:
        mw_set_hp_cur(p, mw_hp_max(p));
        p->poisoned_turns = 0;
        p->diseased_turns = 0;
        snprintf(message, message_size,
                 "PERFECT VITALITY RESTORES YOUR BODY!");
        break;
    default: return 0;
    }
    return 1;
}

static int cast_selected_spell(Game *g, Character *p, CombatState *cs,
                               int category, int index, int source,
                               char *message, size_t message_size) {
    if (category < SPELL_CAT_PERMANENT || category > SPELL_CAT_PRIEST ||
        index < 0 || index >= mw_spell_catalog_count(p)) {
        snprintf(message, message_size,
                 index >= MW_DEEP_SPELL_FIRST ?
                 "DEEP SPELLS EXIST ONLY IN ENHANCED EXPERIENCE." :
                 "THAT SPELL DOES NOT EXIST.");
        return 0;
    }
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
    if (category == SPELL_CAT_PERMANENT)
        p->age = p->age > UINT32_MAX - 2592000u ?
                 UINT32_MAX : p->age + 2592000u;
    else if (category == SPELL_CAT_PREPARATION)
        p->age = p->age > UINT32_MAX - 180u ?
                 UINT32_MAX : p->age + 180u;
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
    int key = input_wait_any_key(&g->input);
    int max = include_help ? '8' : '4';
    key = mouse_list_key(g, key, 0, SPELL_PANE_W, SPELL_ROW_H,
                         SPELL_ROW_H, max - '0', '1');
    if (key < '1' || key > max) return -1;
    (void)source;
    return key - '1';
}

typedef struct ArenaCastChoice {
    u8 category;
    u8 index;
} ArenaCastChoice;

/* The Colosseum has no exploration spellbook to browse.  Its C command is a
   combat decision, so build one dense list containing only learned,
   battle-eligible spells the combatant can afford right now. */
static int arena_collect_usable_magic(Game *g, Character *p, int source,
                                     ArenaCastChoice *choices,
                                     int capacity) {
    int count = 0;
    int catalog = mw_spell_catalog_count(p);
    for (int category = SPELL_CAT_WIZARD;
         category <= SPELL_CAT_PRIEST; category++) {
        for (int index = 0; index < catalog; index++) {
            int level = index / 3 + 1;
            if (!combat_spell_arena_eligible(category, index) ||
                !combat_spell_source_allowed(p, category, source) ||
                !spell_is_available(p, category, index, source, 0) ||
                (source == 0 && !g->cheat_god_mode &&
                 p->sp_cur < (float)level))
                continue;
            if (choices && count < capacity) {
                choices[count].category = (u8)category;
                choices[count].index = (u8)index;
            }
            count++;
        }
    }
    return count;
}

static int select_arena_magic(Game *g, Character *p, CombatState *cs,
                              int source, int *category_out, int *index_out) {
    enum { PER_PAGE = 30 };
    ArenaCastChoice choices[SPELL_TYPES * MW_ENHANCED_SPELL_COUNT];
    int count = arena_collect_usable_magic(
        g, p, source, choices,
        (int)(sizeof(choices) / sizeof(choices[0])));
    if (!count) return 0;
    int page = 0;
    int page_count = (count + PER_PAGE - 1) / PER_PAGE;

    for (;;) {
        char line[128], title[128];
        spell_draw_selector_backdrop(g, p, cs);
        if (source == 0)
            snprintf(title, sizeof(title),
                     "CASTABLE BATTLE SPELLS ONLY - SP %.0f/%.0f:",
                     p->sp_cur, p->sp_max);
        else
            snprintf(title, sizeof(title), "USABLE BATTLE %sS ONLY:",
                     source == 1 ? "SCROLL" :
                     source == 2 ? "WAND" : "PAPER");
        spell_selector_text(g, 0, 0, title, 4);
        if (page_count > 1)
            draw_page_badge(&g->video, SPELL_PAGE_BADGE_X, 0,
                            SPELL_PAGE_BADGE_W, page, page_count, 0);
        spell_selector_text(g, LOGICAL_W - 90, 0, "ESCAPE", 3);

        int first = page * PER_PAGE;
        int shown = count - first;
        if (shown > PER_PAGE) shown = PER_PAGE;
        for (int position = 0; position < shown; position++) {
            const ArenaCastChoice *choice = &choices[first + position];
            int column = position / 10;
            int row = position % 10;
            int level = choice->index / 3 + 1;
            char hotkey = spell_hotkey_for_index(position);
            snprintf(line, sizeof(line), "%c)%c %s [%d]", hotkey,
                     choice->category == SPELL_CAT_WIZARD ? 'W' : 'P',
                     combat_spell_name(choice->category, choice->index),
                     level);
            spell_selector_text(g,
                column == 0 ? 0 :
                (column == 1 ? SPELL_SELECTOR_COL_2 : SPELL_SELECTOR_COL_3),
                row + 1, line,
                choice->category == SPELL_CAT_WIZARD ? 8 : 14);
        }
        video_present(&g->video);

        int key = input_getch(&g->input);
        if (key == 0x1B || input_poll_quit(&g->input)) return -1;
        if (key == 0) {
            int scan = input_getch(&g->input);
            if (scan == 0x49 && page > 0) --page;
            else if (scan == 0x51 && page + 1 < page_count) ++page;
            continue;
        }
        int position = -1;
        if (key == INPUT_MOUSE_CLICK) {
            int x, y;
            if (!game_mouse_click_logical(g, &x, &y)) continue;
            if (y < SPELL_SELECTOR_ROW_H && x >= LOGICAL_W - 120)
                return -1;
            if (page_count > 1 && y < PAGE_BADGE_H &&
                x >= SPELL_PAGE_BADGE_X &&
                x < SPELL_PAGE_BADGE_X + SPELL_PAGE_BADGE_W) {
                page = (page + 1) % page_count;
                continue;
            }
            int row = y / SPELL_SELECTOR_ROW_H - 1;
            int column = x < SPELL_SELECTOR_COL_2 ? 0 :
                         (x < SPELL_SELECTOR_COL_3 ? 1 : 2);
            if (row >= 0 && row < 10) position = column * 10 + row;
        } else {
            position = spell_index_from_hotkey(key);
        }
        if (position >= 0 && position < shown) {
            *category_out = choices[first + position].category;
            *index_out = choices[first + position].index;
            return 1;
        }
    }
}

int cmd_cast_spell_menu(Game *g, Character *p, CombatState *cs) {
    if (g->arena_active && cs) {
        int category, index;
        int selected = select_arena_magic(g, p, cs, 0, &category, &index);
        if (selected <= 0) {
            if (selected == 0)
                spell_notice(g, p, cs, "NO BATTLE SPELLS CAN BE CAST.",
                             "EARN MAGIC OR RECOVER SPELL POINTS.");
            return 0;
        }
        char message[160];
        int result = cast_selected_spell(g, p, cs, category, index, 0,
                                         message, sizeof(message));
        if (result == 0) spell_notice(g, p, cs, message, "");
        return result;
    }
    int choice = choose_cast_category(g, p, cs, 0, 1);
    if (choice < 0) return 0;
    int help = choice >= 4;
    int category = help ? choice - 4 : choice;
    if (!help && !class_can_read_spellbook(p, category)) {
        spell_notice(g, p, cs, "YOUR CLASS CANNOT READ", "THAT KIND OF SPELLBOOK.");
        return 0;
    }
    int index = select_spell_index(g, p, cs, category, 0, help,
                                   (mw_spell_catalog_count(p) + 2) / 3);
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

/* WORLD shop_magic uses the original 0x2D0 by 0x1AC upper-left pane.
 * Its title is separate from eight rows spaced 0x32 source pixels apart.
 * Keep this renderer independent of the denser spell category pages. */
#define ITEM_PANE_W (0x2D0 * LOGICAL_W / 1600)
#define ITEM_PANE_H (0x1AC * LOGICAL_H / 1200)
#define ITEM_MENU_Y (0x28 * LOGICAL_H / 1200)
#define ITEM_ROW_H  (0x32 * LOGICAL_H / 1200)

static const char *const use_item_menu[8] = {
    "WHICH TYPE OF ITEM?",
    "",
    "1) SCROLL",
    "2) WAND",
    "3) PAPER",
    "4) MAGIC VITAMIN PILL",
    "5) OTHER",
    ""
};

static const char *const vitamin_pill_menu[8] = {
    "PRESS 1-6 TO TAKE A PILL:",
    "1) GREEN PILL",
    "2) ORANGE PILL",
    "3) YELLOW PILL",
    "4) RED PILL",
    "5) BLUE PILL",
    "6) WHITE PILL",
    "HIT ESCAPE TO RETURN TO GAME"
};

static const char *const other_item_menu[8] = {
    "HIT A KEY (1-6):",
    "1) FLOOR SLOSHER",
    "2) POTION OF HEALING",
    "3) WIN GAME",
    "4) STONE OF SEEING",
    "5) STONE OF TELEPORTATION",
    "6) HOLY HAND GRENADE",
    "ANY OTHER KEY RETURNS TO GAME"
};

static void item_draw_page(Game *g, Character *p, CombatState *cs,
                           const char *const lines[8]) {
    if (cs)
        game_draw_combat_overlay(g, p, cs->entity_index, cs->monster_type_idx,
                                 cs->monster_level, cs->monster_hp, "", "", "");
    else
        game_draw_exploration(g, p);
    video_fill_rect(&g->video, 0, 0, ITEM_PANE_W, ITEM_PANE_H, 0);
    video_draw_text_scaled_xy(&g->video, 0, 0, "USE MAGIC MENU:", 8,
                              7, 6, 12, 17);
    for (int row = 0; row < 8; row++)
        if (lines[row] && lines[row][0])
            video_draw_text_scaled_xy(&g->video, 0,
                                      ITEM_MENU_Y + row * ITEM_ROW_H,
                                      lines[row], 5, 7, 6, 12, 17);
    video_present(&g->video);
}

static int item_get_list_key(Game *g, int first_row, int last_row,
                             int first_key) {
    int key = input_wait_any_key(&g->input);
    if (key == INPUT_MOUSE_CLICK) {
        int x, y;
        key = -1;
        if (game_mouse_click_logical(g, &x, &y) &&
            x >= 0 && x < ITEM_PANE_W && y >= ITEM_MENU_Y) {
            int row = (y - ITEM_MENU_Y) / ITEM_ROW_H;
            if (row >= first_row && row <= last_row)
                key = first_key + row - first_row;
        }
    }
    return key;
}

static void item_show_and_wait(Game *g, Character *p, CombatState *cs,
                               const char *const lines[8]) {
    item_draw_page(g, p, cs, lines);
    input_wait_any_key(&g->input);
}

static void subtract_stat_safely(u16 *value, unsigned amount) {
    *value = (u16)(*value >= amount ? *value - amount : 0);
}

static int apply_vitamin_pill(Character *p, int pill) {
    u8 *count = NULL;
    u16 *raised = NULL;
    u16 *lowered = NULL;
    switch (pill) {
    case 0: /* Green: intelligence for dexterity. */
        count = &p->green_pill;
        raised = &p->stat_int;
        lowered = &p->stat_agi;
        break;
    case 1: /* Orange: strength for luck. */
        count = &p->orange_pill;
        raised = &p->stat_str;
        lowered = &p->stat_luck;
        break;
    case 2: /* Yellow: luck for strength. */
        count = &p->yellow_pill;
        raised = &p->stat_luck;
        lowered = &p->stat_str;
        break;
    case 3: /* Red: constitution for wisdom. */
        count = &p->red_pill;
        raised = &p->stat_con;
        lowered = &p->stat_wis;
        break;
    case 4: /* Blue: wisdom for constitution. */
        count = &p->blue_pill;
        raised = &p->stat_wis;
        lowered = &p->stat_con;
        break;
    case 5: /* White: dexterity for intelligence. */
        count = &p->white_pill;
        raised = &p->stat_agi;
        lowered = &p->stat_int;
        break;
    default:
        return 0;
    }
    if (!*count) return 0;
    --*count;
    add_stat_capped(raised, 4);
    subtract_stat_safely(lowered, 2);
    return 1;
}

static int use_vitamin_pill(Game *g, Character *p, CombatState *cs) {
    static const char *const success[6][8] = {
        {
            "YOUR INTELLIGENCE HAS BEEN",
            "  RAISED FOUR AND YOUR",
            "  DEXTERITY HAS DROPPED TWO",
            "  POINTS.",
            "",
            "HIT ANY KEY...",
            "",
            ""
        },
        {
            "YOUR STRENGTH HAS BEEN RAISED",
            "  FOUR AND YOUR LUCK HAS",
            "  DROPPED TWO POINTS.",
            "",
            "HIT ANY KEY...",
            "",
            "",
            ""
        },
        {
            "YOUR LUCK HAS BEEN RAISED",
            "  FOUR AND YOUR STRENGTH HAS",
            "  DROPPED TWO POINTS.",
            "",
            "HIT ANY KEY...",
            "",
            "",
            ""
        },
        {
            "YOUR CONSTITUTION HAS BEEN",
            "  RAISED FOUR AND YOUR WISDOM",
            "  HAS DROPPED TWO POINTS.",
            "",
            "HIT ANY KEY...",
            "",
            "",
            ""
        },
        {
            "YOUR WISDOM HAS BEEN RAISED",
            "  FOUR AND YOUR CONSTITUTION",
            "  HAS DROPPED TWO POINTS.",
            "",
            "HIT ANY KEY...",
            "",
            "",
            ""
        },
        {
            "YOUR DEXTERITY HAS BEEN",
            "  RAISED FOUR AND YOUR",
            "  INTELLIGENCE HAS DROPPED",
            "  TWO POINTS.",
            "",
            "HIT ANY KEY...",
            "",
            ""
        }
    };
    static const char *const missing[8] = {
        "DON'T YOU THINK YOU'D BETTER",
        "  FIND ONE FIRST? TRY KILLING",
        "  LEVEL DRAINERS.",
        "",
        "HIT ANY KEY...",
        "",
        "",
        ""
    };

    item_draw_page(g, p, cs, vitamin_pill_menu);
    int key = item_get_list_key(g, 1, 6, '1');
    if (key < '1' || key > '6') return 0;
    int pill = key - '1';
    if (!apply_vitamin_pill(p, pill)) {
        item_show_and_wait(g, p, cs, missing);
        return 0;
    }
    item_show_and_wait(g, p, cs, success[pill]);
    return -6; /* Item consumed; no generic "spell cast" message. */
}

void game_draw_use_item_test(Game *g, Character *p, CombatState *cs,
                             int page) {
    const char *const *lines = use_item_menu;
    if (page == 1) lines = vitamin_pill_menu;
    else if (page == 2) lines = other_item_menu;
    item_draw_page(g, p, cs, lines);
}

static int use_misc_item(Game *g, Character *p, CombatState *cs) {
    static const char *const missing_item[8] = {
        "MAGIC ITEMS ARE MUCH MORE",
        "  EFFECTIVE WHEN YOU",
        "  ACTUALLY POSESS THEM.",
        "KILL SOME MORE MONSTERS,",
        "  YOU'RE BOUND FIND ONE",
        "  EVENTUALLY.",
        "",
        "HIT ANY KEY..."
    };
    static const char *const win_menu[8] = {
        "PLEASE SELECT ONE:",
        "",
        "1) WIN LIFE",
        "2) BECOME RICH",
        "3) RULE THE WORLD",
        "4) LIVE FOREVER",
        "5) CONTINUE PLAYING GAME",
        "PRESS 1-5 TO MAKE SELECTION"
    };
    static const char *const win_address[8] = {
        "TO EXECUTE THIS REQUEST SEND",
        "  ONE MILLION ZILLION DOLLARS",
        "  TO THE FOLLOWING ADDRESS:",
        "",
        "THE MILLION ZILLION DOLLAR CLUB",
        "13713 MAIN STREET, 317",
        "MILLIONVILLE, MW, 78701-2509",
        "HIT ANY KEY..."
    };
    static const char *const win_stamp[8] = {
        "BY THE WAY, A FIRST CLASS STAMP",
        "TO MORAFF'S WORLD COSTS ONE",
        "MILLION ZILLION CENTS.",
        "STAMPS ARE AVAILABLE AT POST",
        "OFFICES EVERYWHERE (EXCEPT,",
        "PERHAPS, WHERE YOU ARE).",
        "",
        "HIT ANY KEY..."
    };
    static const char *const slosher_deep[8] = {
        "DOESN'T WORK THIS DEEP!",
        "",
        "HIT ANY KEY...",
        "", "", "", "", ""
    };
    static const char *const slosher_success[8] = {
        "YOU ARE SLIPPING THROUGH THE",
        "  FLOOR. HIT ANY KEY...",
        "", "", "", "", "", ""
    };
    static const char *const potion_success[8] = {
        "YOU FEEL GREAT! HIT A KEY...",
        "", "", "", "", "", "", ""
    };
    static const char *const seeing_success[8] = {
        "SUDDENLY YOU FEEL THAT",
        "  YOU CAN SEE RIGHT",
        "  THROUGH ALL OF THE",
        "  WALLS ON THIS LEVEL.",
        "",
        "THE STONE VANISHES.",
        "",
        "HIT ANY KEY..."
    };
    static const char *const teleport_success[8] = {
        "YOU ARE FLOATING THROUGH",
        "  SPACE... YOU SEE LOTS",
        "  OF FANCY COLORS SWIRLING",
        "  ALL AROUND YOU.",
        "",
        "HIT ANY KEY TO MAKE YOUR",
        "  BODY REMATERIALIZE IN",
        "  THE TOWN."
    };
    static const char *const empty_grenade[8] = {
        "ARE YOU SURE THAT YOU WANT",
        "  TO TOSS ONE OF THE MOST",
        "  POWERFUL MAGIC ITEMS IN",
        "  MORAFF'S WORLD ONTO AN",
        "  EMPTY FLOOR?",
        "",
        "HIT ANY KEY TO RETHINK THE",
        "  LOGIC OF THAT MOVE!"
    };
    static const char *const caught_grenade[8] = {
        "   THE MONSTER CATCHES THE",
        "GRADADE.",
        "", "", "", "", "", ""
    };
    static const char *const returned_grenade[8] = {
        "  HE THEN LAUGHS HYSTERICALLY",
        "AND HANDS IT BACK TO YOU.",
        "  HE REMARKS, 'YOU SHOULDN'T",
        "PLAY WITH THESE', AND LAUGHS",
        "EVEN LOUDER AS HE RISES UP",
        "FOR A DEVESTATING ATTACK!",
        "",
        "HIT ANY KEY..."
    };
    static const char *const grenade_success[8] = {
        "A MASSIVE EXPLOSION KILLS",
        "  THE MONSTER INSTANTLY.",
        "",
        "HIT ANY KEY...",
        "", "", "", ""
    };

    item_draw_page(g, p, cs, other_item_menu);
    int key = item_get_list_key(g, 1, 6, '1');
    if (key < '1' || key > '6') return 0;

    if (key == '3') {
        item_draw_page(g, p, cs, win_menu);
        int wish = item_get_list_key(g, 2, 6, '1');
        if (wish >= '1' && wish <= '4') {
            item_show_and_wait(g, p, cs, win_address);
            item_show_and_wait(g, p, cs, win_stamp);
        }
        return 0;
    }

    int has_item =
        (key == '1' && p->floor_slosher) ||
        (key == '2' && p->potion_heal) ||
        (key == '4' && p->stone_see) ||
        (key == '5' && p->stone_teleport) ||
        (key == '6' && p->holy_grenade);
    if (!has_item) {
        item_show_and_wait(g, p, cs, missing_item);
        return 0;
    }

    if (key == '1') {
        int cap = game_traversal_rules(g)->prep_major_descend_cap;
        if (g->cur_floor > cap ||
            g->cur_floor >= game_traversal_rules(g)->max_floor) {
            item_show_and_wait(g, p, cs, slosher_deep);
            return 0;
        }
        item_show_and_wait(g, p, cs, slosher_success);
        if (!game_change_floor(g, p, g->cur_floor + 1) ||
            !game_relocate(g, p))
            return 0;
        return -4;
    }
    if (key == '2') {
        --p->potion_heal;
        mw_set_hp_cur(p, mw_hp_max(p));
        item_show_and_wait(g, p, cs, potion_success);
        return -6;
    }
    if (key == '4') {
        --p->stone_see;
        memset(g->visited, 1, sizeof(g->visited));
        item_show_and_wait(g, p, cs, seeing_success);
        return -6;
    }
    if (key == '5') {
        --p->stone_teleport;
        item_show_and_wait(g, p, cs, teleport_success);
        if (!game_change_floor(g, p, 0) || !game_relocate(g, p))
            return 0;
        return -4;
    }

    if (!cs || !cs->active || cs->monster_hp <= 0 ||
        cs->monster_type_idx < 0 ||
        cs->monster_type_idx >= MONSTER_TYPE_COUNT) {
        item_show_and_wait(g, p, cs, empty_grenade);
        return 0;
    }
    if (monster_types[cs->monster_type_idx].imm == 100) {
        item_show_and_wait(g, p, cs, caught_grenade);
        item_show_and_wait(g, p, cs, returned_grenade);
        return -6;
    }
    --p->holy_grenade;
    cs->monster_hp = 0;
    item_show_and_wait(g, p, cs, grenade_success);
    return -1;
}

int cmd_use_item(Game *g, Character *p, CombatState *cs) {
    if (g->arena_active && cs) {
        static const char *const arena_magic_menu[8] = {
            "USE WHICH BATTLE MAGIC?",
            "",
            "1) SCROLL",
            "2) WAND",
            "3) MAGIC PAPER",
            "",
            "ONLY CURRENTLY USABLE ITEMS WILL BE LISTED.",
            "ESCAPE RETURNS TO THE FIGHT"
        };
        item_draw_page(g, p, cs, arena_magic_menu);
        int key = item_get_list_key(g, 2, 4, '1');
        if (key < '1' || key > '3') return 0;
        int source = key - '0';
        int category, index;
        int selected = select_arena_magic(g, p, cs, source,
                                          &category, &index);
        if (selected <= 0) {
            if (selected == 0) {
                char line[96];
                snprintf(line, sizeof(line), "NO USABLE BATTLE %sS.",
                         source == 1 ? "SCROLL" :
                         source == 2 ? "WAND" : "PAPER");
                spell_notice(g, p, cs, line,
                    p->class_id == CLASS_FIGHTER && source != 3 ?
                    "FIGHTERS CAN CAST ONLY FROM MAGIC PAPER." :
                    "EARN ONE FROM A COLOSSEUM REWARD.");
            }
            return 0;
        }
        char message[160];
        int result = cast_selected_spell(g, p, cs, category, index, source,
                                         message, sizeof(message));
        if (result == 0) spell_notice(g, p, cs, message, "");
        return result;
    }
    item_draw_page(g, p, cs, use_item_menu);
    int key = item_get_list_key(g, 2, 6, '1');
    if (key == '4') return use_vitamin_pill(g, p, cs);
    if (key == '5') return use_misc_item(g, p, cs);
    if(key<'1'||key>'3')return 0;
    int source=key-'0';
    if(!mw_universal_access(p) &&
       p->class_id==CLASS_FIGHTER && source!=3){spell_notice(g,p,cs,"FIGHTERS CAN CAST ONLY","FROM MAGIC PAPER.");return 0;}
    int category=choose_cast_category(g,p,cs,source,0);
    if(category<0)return 0;
    int index=select_spell_index(g,p,cs,category,source,0,
                                 (mw_spell_catalog_count(p)+2)/3);
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
    mw_set_experience_mode(&p, MW_EXPERIENCE_ENHANCED);
    p.level = 20;
    p.stat_str = 20;
    p.stat_agi = 20;
    p.stat_wis = 20;
    mw_set_hp_max(&p, 100);
    mw_set_hp_cur(&p, 100);
    p.sp_cur = p.sp_max = 100.0f;
    cs.monster_type_idx = 0;
    cs.monster_level = 10;
    cs.monster_hp = cs.monster_max_hp = 100;

#define CHECK(expr, label) do { if (!(expr)) { \
    fprintf(stderr, "MAGIC TEST FAIL: %s\n", label); failures++; } } while (0)
    {
        ArenaCastChoice available[4];
        memset(p.spells, 0, sizeof(p.spells));
        p.spells[SPELL_CAT_WIZARD][0] = 1; /* level-one Sleep */
        p.spells[SPELL_CAT_WIZARD][3] = 1; /* level-two Slow */
        p.spells[SPELL_CAT_PREPARATION][0] = 1; /* never an arena cast */
        p.sp_cur = 1.0f;
        g.arena_active = 1;
        int count = arena_collect_usable_magic(&g, &p, 0, available, 4);
        CHECK(count == 1 && available[0].category == SPELL_CAT_WIZARD &&
              available[0].index == 0,
              "Colosseum cast menu contains only affordable battle spells");
        p.sp_cur = 2.0f;
        count = arena_collect_usable_magic(&g, &p, 0, available, 4);
        CHECK(count == 2,
              "Colosseum cast menu reveals newly affordable battle magic");
        memset(p.wands, 0, sizeof(p.wands));
        memset(p.papers, 0, sizeof(p.papers));
        p.wands[SPELL_CAT_WIZARD][3] = 2;
        p.papers[SPELL_CAT_WIZARD][3] = 1;
        p.class_id = CLASS_FIGHTER;
        CHECK(arena_collect_usable_magic(&g, &p, 2, available, 4) == 0 &&
              arena_collect_usable_magic(&g, &p, 3, available, 4) == 1,
              "Colosseum item menus preserve Fighter paper-only casting");
        p.class_id = CLASS_WIZARD;
        g.arena_active = 0;
        p.sp_cur = p.sp_max = 100.0f;
    }
    {
        const char *cursor =
            "ETERNAL SANCTUARY PROTECTS YOU FOR 1200 TURNS!";
        char line[SPELL_NOTICE_WRAP_CHARS + 1];
        char first[SPELL_NOTICE_WRAP_CHARS + 1] = "";
        char second[SPELL_NOTICE_WRAP_CHARS + 1] = "";
        int count = 0;
        while (spell_wrap_next(&cursor, line)) {
            CHECK(strlen(line) <= SPELL_NOTICE_WRAP_CHARS,
                  "spell notice line remains inside pane");
            if (count == 0) snprintf(first, sizeof(first), "%s", line);
            if (count == 1) snprintf(second, sizeof(second), "%s", line);
            ++count;
        }
        CHECK(count == 2 &&
              strcmp(first, "ETERNAL SANCTUARY PROTECTS") == 0 &&
              strcmp(second, "YOU FOR 1200 TURNS!") == 0,
              "long Enhanced spell notice wraps at a word boundary");
    }
    {
        Input input;
        memset(&input, 0, sizeof(input));
        input.keys[0] = 0;
        input.keys[1] = 0x51;
        input.keys[2] = 'v';
        input.tail = 3;
        CHECK(input_wait_any_key(&input) == 0 &&
              input_getch(&input) == 'v' &&
              input.head == input.tail,
              "modal input drains Page Down without leaking ASCII Q");
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
    CHECK(spell_deep_grid_index(0, 0) == 30 &&
          spell_deep_grid_index(0, 7) == 37 &&
          spell_deep_grid_index(1, 0) == 38 &&
          spell_deep_grid_index(1, 6) == 44 &&
          spell_deep_grid_index(1, 7) == -1,
          "all fifteen deep spells fit the two-column second page");
    {
        static const u16 expected[6][6] = {
            /* STR INT WIS CON AGI LUCK */
            {20, 24, 20, 20, 18, 20}, /* green */
            {24, 20, 20, 20, 20, 18}, /* orange */
            {18, 20, 20, 20, 20, 24}, /* yellow */
            {20, 20, 18, 24, 20, 20}, /* red */
            {20, 20, 24, 18, 20, 20}, /* blue */
            {20, 18, 20, 20, 24, 20}  /* white */
        };
        int all_pills_match = 1;
        for (int pill = 0; pill < 6; pill++) {
            Character vitamin = {0};
            vitamin.stat_str = vitamin.stat_int = vitamin.stat_wis = 20;
            vitamin.stat_con = vitamin.stat_agi = vitamin.stat_luck = 20;
            u8 *counts[6] = {
                &vitamin.green_pill, &vitamin.orange_pill,
                &vitamin.yellow_pill, &vitamin.red_pill,
                &vitamin.blue_pill, &vitamin.white_pill
            };
            *counts[pill] = 1;
            if (!apply_vitamin_pill(&vitamin, pill) || *counts[pill] ||
                vitamin.stat_str != expected[pill][0] ||
                vitamin.stat_int != expected[pill][1] ||
                vitamin.stat_wis != expected[pill][2] ||
                vitamin.stat_con != expected[pill][3] ||
                vitamin.stat_agi != expected[pill][4] ||
                vitamin.stat_luck != expected[pill][5])
                all_pills_match = 0;
        }
        CHECK(all_pills_match,
              "original six vitamin pill stat trades and inventory order");
        Character bounded = {0};
        bounded.green_pill = 1;
        bounded.stat_int = MW_PLAYER_STAT_MAX;
        bounded.stat_agi = 1;
        CHECK(apply_vitamin_pill(&bounded, 0) &&
              bounded.stat_int == MW_PLAYER_STAT_MAX &&
              bounded.stat_agi == 0,
              "vitamin pills respect widened stat caps without underflow");
    }
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
          monster_types[113].hpF == 220 &&
          monster_types[177].minL == 1000 && monster_types[177].atk > 1000 &&
          combat_calc_monster_hp(&monster_types[177], 1000) > UINT16_MAX,
          "native deep bosses use widened stats and HP");
    {
        static const int boss_type[] = {112, 113, 174, 175, 176, 177};
        static const int boss_floor[] = {375, 500, 625, 750, 875, 1000};
        int previous_hp = 0;
        int boss_hp_rises = 1;
        for (int i = 0; i < 6; i++) {
            int hp = combat_calc_monster_hp(
                &monster_types[boss_type[i]], boss_floor[i]);
            if (hp <= previous_hp) boss_hp_rises = 0;
            previous_hp = hp;
        }
        CHECK(boss_hp_rises,
              "Enhanced milestone boss HP rises at every quest floor");
    }
    {
        int previous_average = 0;
        int bands_rise = 1;
        int every_band_casts = 1;
        int caster_count = 0;
        for (int band = 0; band < 15; band++) {
            int hp_total = 0;
            int band_casters = 0;
            for (int slot = 0; slot < 4; slot++) {
                int type = DEEP_MONSTER_FIRST + band * 4 + slot;
                hp_total += monster_types[type].hpF;
                if (combat_monster_spell_chance(type)) {
                    band_casters++;
                    caster_count++;
                }
            }
            int average = hp_total / 4;
            if (average < previous_average) bands_rise = 0;
            if (!band_casters) every_band_casts = 0;
            previous_average = average;
        }
        CHECK(bands_rise && every_band_casts && caster_count >= 40,
              "deep HP bands rise and every generation contains casters");
        CHECK(!strcmp(combat_monster_spell_name(116), "ABYSSAL LANCE") &&
              !strcmp(combat_monster_spell_name(135), "STARFIRE") &&
              !strcmp(combat_monster_spell_name(151), "MANA TEMPEST") &&
              !strcmp(combat_monster_spell_name(174), "COSMIC IMPLOSION") &&
              !strcmp(combat_monster_spell_name(176), "CREATION'S WRATH") &&
              !strcmp(combat_monster_spell_name(177), "END OF AGES") &&
              combat_monster_spell_chance(114) == 0 &&
              combat_monster_spell_chance(177) == 2,
              "late monsters receive progression-appropriate signature magic");
    }
    {
        Character victim = {0};
        mw_set_experience_mode(&victim, MW_EXPERIENCE_ENHANCED);
        mw_set_hp_max(&victim, 10000);
        mw_set_hp_cur(&victim, 10000);
        victim.sp_cur = victim.sp_max = 5000.0f;
        CombatState caster = {0};
        caster.active = 1;
        caster.monster_type_idx = 177;
        caster.monster_level = 1000;
        caster.monster_hp = caster.monster_max_hp = 100000;
        const MonsterSpellProfile *profile = monster_spell_profile(177);
        int damage = combat_monster_spell_effect(
            &g, &caster, &victim, profile);
        CHECK(damage >= 3000 && damage <= 3250 &&
              strstr(caster.special_message, "END OF AGES"),
              "enemy End of Ages scales against late-game player HP");

        caster.monster_type_idx = 151;
        caster.monster_level = 725;
        profile = monster_spell_profile(151);
        float old_sp = victim.sp_cur;
        damage = combat_monster_spell_effect(&g, &caster, &victim, profile);
        CHECK(damage > 0 && victim.sp_cur < old_sp &&
              strstr(caster.special_message, "MANA TEMPEST"),
              "enemy Mana Tempest damages health and drains spell points");

        caster.monster_type_idx = 136;
        caster.monster_level = 625;
        caster.monster_hp = 1;
        caster.monster_max_hp = 100000;
        profile = monster_spell_profile(136);
        damage = combat_monster_spell_effect(&g, &caster, &victim, profile);
        CHECK(damage > 0 && caster.monster_hp > 1 &&
              strstr(caster.special_message, "LIFE CONVERGENCE"),
              "enemy Life Convergence drains the player and heals its caster");

        caster.monster_type_idx = 140;
        caster.monster_level = 575;
        caster.monster_hp = caster.monster_max_hp = 1;
        profile = monster_spell_profile(140);
        damage = combat_monster_spell_effect(&g, &caster, &victim, profile);
        CHECK(damage == 0 && caster.monster_hp > 1 &&
              strstr(caster.special_message, "PHOENIX PRAYER"),
              "enemy restoration magic heals instead of making a melee attack");

        caster.monster_type_idx = 177;
        caster.monster_level = 1000;
        victim.antimagic_ring = 5;
        int dispelled = 0;
        game_srand(&g, 17);
        for (int i = 0; i < 128 && !dispelled; i++) {
            int spell_damage = 0;
            if (combat_monster_try_spell(
                    &g, &caster, &victim, &spell_damage) &&
                strstr(caster.special_message, "DISPELS"))
                dispelled = 1;
        }
        CHECK(dispelled,
              "anti-magic ring can dispel native enemy spells");
    }
    mw_set_weapon_enchant(&p, 1, 300);
    mw_set_armor_enchant(&p, 3, 275);
    CHECK(mw_weapon_enchant(&p, 1) == 300 &&
          mw_armor_enchant(&p, 3) == 275,
          "16-bit equipment enchantments exceed byte range");
    mw_set_weapon_inventory_count(&p, 12, 1);
    mw_set_armor_inventory_count(&p, 8, 1);
    mw_set_weapon_inventory_count(&p, 14, 1);
    mw_set_armor_inventory_count(&p, 10, 1);
    mw_set_weapon_inventory_count(&p, 19, 1);
    mw_set_armor_inventory_count(&p, 15, 1);
    mw_set_weapon_enchant(&p, 12, 725);
    mw_set_armor_enchant(&p, 8, 640);
    mw_set_weapon_enchant(&p, 14, 950);
    mw_set_armor_enchant(&p, 10, 925);
    mw_set_weapon_enchant(&p, 19, 1400);
    mw_set_armor_enchant(&p, 15, 1350);
    CHECK(mw_weapon_inventory_count(&p, 12) == 1 &&
          mw_armor_inventory_count(&p, 8) == 1 &&
          mw_weapon_enchant(&p, 12) == 725 &&
          mw_armor_enchant(&p, 8) == 640 &&
          mw_weapon_inventory_count(&p, 14) == 1 &&
          mw_armor_inventory_count(&p, 10) == 1 &&
          mw_weapon_enchant(&p, 14) == 950 &&
          mw_armor_enchant(&p, 10) == 925 &&
          mw_weapon_inventory_count(&p, 19) == 1 &&
          mw_armor_inventory_count(&p, 15) == 1 &&
          mw_weapon_enchant(&p, 19) == 1400 &&
          mw_armor_enchant(&p, 15) == 1350,
          "Enhanced equipment inventory and enchant storage");
    {
        Character v2 = {0};
        v2.native.magic = MW_NATIVE_CHARACTER_MAGIC;
        v2.native.version = 2;
        v2.native.quest_flags = (u16)((1u << 9) | (1u << 12));
        v2.native.weapon_enchant[1] = 333;
        v2.native.experience_mode = MW_EXPERIENCE_ENHANCED;
        mw_character_native_ensure(&v2);
        CHECK(mw_character_native_valid(&v2) &&
              mw_weapon_enchant(&v2, 1) == 333 &&
              mw_weapon_inventory_count(&v2, 12) == 1 &&
              mw_armor_inventory_count(&v2, 8) == 1 &&
              mw_weapon_inventory_count(&v2, 16) == 1 &&
              mw_armor_inventory_count(&v2, 12) == 1 &&
              v2.native.deep_magic_marker == MW_DEEP_MAGIC_MARKER &&
              v2.spells[0][31] && v2.spells[3][34],
              "V2 Enhanced save equipment migration");
    }
    {
        Character old_monk = {0};
        memset(old_monk.spells, 1, sizeof(old_monk.spells));
        memset(old_monk.scrolls, 1, sizeof(old_monk.scrolls));
        old_monk.native.magic = MW_NATIVE_CHARACTER_MAGIC;
        old_monk.native.version = 5;
        old_monk.native.experience_mode = MW_EXPERIENCE_ENHANCED;
        old_monk.native.quest_flags = (u16)(1u << 8);
        mw_character_native_ensure(&old_monk);
        CHECK(old_monk.spells[0][30] &&
              !old_monk.spells[0][31] &&
              !old_monk.spells[3][34] &&
              !old_monk.scrolls[0][30] &&
              old_monk.native.deep_magic_marker == MW_DEEP_MAGIC_MARKER,
              "legacy reserved spell slots migrate to earned deep magic");
    }
    {
        Character v3 = {0};
        v3.native.magic = MW_NATIVE_CHARACTER_MAGIC;
        v3.native.version = 3;
        v3.native.experience_mode = MW_EXPERIENCE_ENHANCED;
        v3.native.deep_magic_marker = MW_DEEP_MAGIC_MARKER;
        v3.native.relic_arcane_ring = 0xFF;
        v3.native.relic_phoenix_cooldown = 0xFFFF;
        mw_character_native_ensure(&v3);
        CHECK(mw_character_native_valid(&v3) &&
              v3.native.deep_magic_marker == MW_DEEP_MAGIC_MARKER &&
              !mw_relic_count(&v3) &&
              !v3.native.relic_phoenix_cooldown,
              "V3 reserved bytes migrate safely to empty V6 relic storage");
    }
    {
        Character v4 = {0};
        v4.native.magic = MW_NATIVE_CHARACTER_MAGIC;
        v4.native.version = 4;
        v4.native.experience_mode = MW_EXPERIENCE_ENHANCED;
        v4.native.quest_flags = MW_FINAL_GEAR_QUEST_FLAG;
        v4.native.relic_arcane_ring = 1;
        v4.native.relic_phoenix_seal = 1;
        v4.native.relic_phoenix_cooldown = 123;
        v4.native.final_weapon_inventory = 0xFF;
        v4.native.final_armor_enchant = INT16_MAX;
        mw_character_native_ensure(&v4);
        CHECK(mw_character_native_valid(&v4) &&
              mw_relic_owned(&v4, MW_RELIC_ARCANE_RING) &&
              mw_relic_owned(&v4, MW_RELIC_PHOENIX_SEAL) &&
              v4.native.relic_phoenix_cooldown == 123 &&
              !(mw_quest_flags(&v4) & MW_FINAL_GEAR_QUEST_FLAG) &&
              !mw_weapon_inventory_count(&v4, 14) &&
              !mw_armor_inventory_count(&v4, 10) &&
              !mw_weapon_enchant(&v4, 14) &&
              !mw_armor_enchant(&v4, 10),
              "V4 relic save migrates safely to empty V6 late gear");
    }
    {
        Character v5 = {0};
        v5.hp_cur = 54321;
        v5.hp_max = 60000;
        v5.native.magic = MW_NATIVE_CHARACTER_MAGIC;
        v5.native.version = 5;
        v5.native.experience_mode = MW_EXPERIENCE_ENHANCED;
        v5.native.quest_flags = MW_FINAL_GEAR_QUEST_FLAG;
        v5.native.relic_arcane_ring = 1;
        v5.native.enhanced_weapon_inventory[1] = 2;
        v5.native.enhanced_weapon_enchant[1] = 450;
        v5.native.final_armor_inventory = 3;
        v5.native.final_armor_enchant = 900;
        mw_character_native_ensure(&v5);
        CHECK(mw_character_native_valid(&v5) &&
              mw_relic_owned(&v5, MW_RELIC_ARCANE_RING) &&
              mw_hp_cur(&v5) == 54321 && mw_hp_max(&v5) == 60000 &&
              mw_weapon_inventory_count(&v5, 16) == 2 &&
              mw_weapon_enchant(&v5, 16) == 450 &&
              mw_armor_inventory_count(&v5, 14) == 3 &&
              mw_armor_enchant(&v5, 14) == 900 &&
              mw_late_gear_unlocked(&v5, 4) &&
              mw_late_gear_unlocked(&v5, 6),
              "V5 health, relics, and late gear migrate into V6");
    }
    {
        Character hidden = {0};
        char line[96];
        format_equipment_line(&hidden, 0, 7, 7, line, sizeof(line));
        CHECK(strstr(line, "--------") && !strstr(line, "GREAT"),
              "uncollected weapon name remains hidden");
        mw_set_weapon_inventory_count(&hidden, 7, 1);
        mw_set_weapon_enchant(&hidden, 7, 42);
        format_equipment_line(&hidden, 0, 7, 7, line, sizeof(line));
        CHECK(strstr(line, "GREAT SWORD") && strstr(line, ", PLUS 42"),
              "collected weapon name and enchant are shown");
    }
    p.class_id = CLASS_FIGHTER;
    CHECK(combat_weapon_allowed(&p, 7) &&
          combat_weapon_allowed(&p, 12) &&
          combat_weapon_allowed(&p, 13) &&
          combat_weapon_allowed(&p, 14) &&
          combat_weapon_allowed(&p, 19) &&
          combat_armor_allowed(&p, 8) &&
          combat_armor_allowed(&p, 9) &&
          combat_armor_allowed(&p, 10) &&
          combat_armor_allowed(&p, 15),
          "fighter equipment permissions");
    p.class_id = CLASS_WIZARD;
    CHECK(combat_weapon_allowed(&p, 1) &&
          combat_weapon_allowed(&p, 4) &&
          combat_weapon_allowed(&p, 12) &&
          !combat_weapon_allowed(&p, 2) &&
          !combat_weapon_allowed(&p, 13) &&
          !combat_weapon_allowed(&p, 14) &&
          !combat_armor_allowed(&p, 8) &&
          !combat_armor_allowed(&p, 10),
          "wizard equipment permissions");
    p.class_id = CLASS_MONK;
    CHECK(!combat_weapon_allowed(&p, 12) &&
          !combat_weapon_allowed(&p, 14) &&
          combat_armor_allowed(&p, 1) &&
          combat_armor_allowed(&p, 8) &&
          !combat_armor_allowed(&p, 9) &&
          !combat_armor_allowed(&p, 10),
          "monk equipment permissions");
    p.class_id = CLASS_WIZARD;
    CHECK(class_can_read_spellbook(&p, SPELL_CAT_WIZARD), "wizard book access");
    p.class_id = CLASS_SPELLBLADE;
    CHECK(class_can_read_spellbook(&p, SPELL_CAT_WIZARD) &&
          !class_can_read_spellbook(&p, SPELL_CAT_PRIEST) &&
          combat_weapon_allowed(&p, 19) && combat_armor_allowed(&p, 15),
          "Enhanced Spellblade magic and late gear access");
    p.class_id = CLASS_PALADIN;
    CHECK(class_can_read_spellbook(&p, SPELL_CAT_PRIEST) &&
          !class_can_read_spellbook(&p, SPELL_CAT_WIZARD) &&
          combat_weapon_allowed(&p, 19) && combat_armor_allowed(&p, 15),
          "Enhanced Paladin magic and late gear access");
    p.class_id = CLASS_FIGHTER;
    CHECK(!class_can_read_spellbook(&p, SPELL_CAT_PREPARATION), "fighter book denial");
    p.class_id = CLASS_MONK;
    p.spells[SPELL_CAT_PREPARATION][30] = 0;
    CHECK(spell_is_available(&p, SPELL_CAT_PREPARATION, 0, 0, 0) &&
          !spell_is_available(&p, SPELL_CAT_PREPARATION, 30, 0, 0),
          "monk retains original catalog but must earn deep spells");
    p.class_id = CLASS_FIGHTER;
    p.scrolls[SPELL_CAT_WIZARD][0] = 1;
    mw_set_quest_flags(&p, (u16)(mw_quest_flags(&p) |
                                  MW_UNIVERSAL_ACCESS_FLAG));
    CHECK(combat_weapon_allowed(&p, 14) &&
          combat_armor_allowed(&p, 10) &&
          class_can_read_spellbook(&p, SPELL_CAT_WIZARD) &&
          spell_is_available(&p, SPELL_CAT_WIZARD, 0, 1, 0),
          "max-character universal equipment and magic access");
    mw_set_quest_flags(&p, (u16)(mw_quest_flags(&p) &
                                  ~MW_UNIVERSAL_ACCESS_FLAG));
    p.class_id = CLASS_WIZARD;

    p.stat_str = MW_PLAYER_STAT_MAX;
    p.eff_battle_str = 0;
    apply_battle_spell(&g, &cs, &p, &wiz_spells[4], 2);
    CHECK(p.stat_str == MW_PLAYER_STAT_MAX && p.eff_battle_str == 0,
          "battle strength cannot overflow max stat");
    mw_set_hp_max(&p, MW_PLAYER_HP_MAX);
    mw_set_hp_cur(&p, MW_PLAYER_HP_MAX);
    apply_permanent_spell(&g, &p, &cs, 1, message, sizeof(message));
    CHECK(mw_hp_cur(&p) == MW_PLAYER_HP_MAX &&
          mw_hp_max(&p) == MW_PLAYER_HP_MAX,
          "permanent health cannot overflow max HP");
    p.stat_str = 20;
    mw_set_hp_max(&p, 100);
    mw_set_hp_cur(&p, 100);

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
    p.eff_pwr_weapon = 0;
    p.eff_pwr_wpn_turns = 0;
    apply_battle_spell(&g, &cs, &p, &wiz_spells[44], 15);
    apply_battle_spell(&g, &cs, &p, &wiz_spells[40], 14);
    CHECK(p.eff_pwr_weapon == 6 && p.eff_pwr_wpn_turns == 120 &&
          combat_effective_damage_max(19, p.eff_pwr_weapon, 1) == 2000,
          "Enhanced Power Weapon VI and lower-tier no-downgrade rule");
    p.equipped_weapon = 1;
    p.equipped_armor = 1;
    CHECK(apply_permanent_spell(&g, &p, NULL, 40,
                                message, sizeof(message)) &&
          mw_weapon_enchant(&p, 1) == 1000,
          "mythic permanent weapon enchant");
    CHECK(apply_permanent_spell(&g, &p, NULL, 41,
                                message, sizeof(message)) &&
          mw_armor_enchant(&p, 1) == 750,
          "mythic permanent armor enchant");
    CHECK(apply_permanent_spell(&g, &p, NULL, 42,
                                message, sizeof(message)) &&
          mw_body_armor_plus(&p) == 650,
          "mythic permanent body armor");
    p.eff_protect_lv = 0;
    p.eff_protect_turns = 0;
    CHECK(apply_preparation_spell(&g, &p, 42,
                                  message, sizeof(message)) &&
          p.eff_protect_lv == 10 && p.eff_protect_turns == 3000 &&
          p.eff_resist_drain == 3000,
          "mythic sanctuary applies tier ten and all resistances");
    p.poisoned_turns = p.diseased_turns = 99;
    mw_set_hp_cur(&p, 1);
    CHECK(apply_preparation_spell(&g, &p, 44,
                                  message, sizeof(message)) &&
          mw_hp_cur(&p) == mw_hp_max(&p) && !p.poisoned_turns &&
          !p.diseased_turns,
          "perfect vitality heals and cures without restoring SP");
    mw_set_weapon_enchant(&p, 1, 0);
    mw_set_armor_enchant(&p, 1, 0);
    mw_set_body_armor_plus(&p, 0);
    p.eff_pwr_weapon = 0;
    p.eff_pwr_wpn_turns = 0;
    p.eff_protect_lv = 0;
    p.eff_protect_turns = 0;
    p.eff_resist_poison = 120;
    p.eff_resist_disease = 0;
    p.eff_anti_cold = 0;
    p.eff_anti_fire = 0;
    p.eff_resist_drain = 0;
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

    p.level = 20;
    p.eff_resist_drain = 0;
    memset(&cs, 0, sizeof(cs));
    cs.active = 1;
    cs.monster_type_idx = 35;
    cs.monster_level = 100;
    cs.monster_hp = 1000;
    g.arena_active = 1;
    combat_monster_special(&g, &cs, &p, 0);
    CHECK(p.level == 19, "Colosseum caps enemy level drain at one");
    g.arena_active = 0;

    {
        Character warded;
        memset(&warded, 0, sizeof(warded));
        mw_character_native_ensure(&warded);
        mw_set_hp_max(&warded, 100);
        mw_set_hp_cur(&warded, 100);
        g.cheat_god_mode = 0;
        g.arena_active = 1;
        g.arena_difficulty = ARENA_DIFFICULTY_NORMAL;
        g.arena_round = 1;
        CHECK(combat_apply_player_damage(&g, &warded, 100, NULL) == 11 &&
              mw_hp_cur(&warded) == 89,
              "Colosseum round-one damage is scaled and capped");
        mw_set_hp_cur(&warded, 100);
        g.arena_round = 10;
        CHECK(combat_apply_player_damage(&g, &warded, 100, NULL) == 20 &&
              mw_hp_cur(&warded) == 80,
              "Colosseum protection remains smooth through first champion");
        mw_set_hp_cur(&warded, 100);
        g.arena_round = 20;
        CHECK(combat_apply_player_damage(&g, &warded, 100, NULL) == 30 &&
              mw_hp_cur(&warded) == 70,
              "Colosseum opening protection tapers through round twenty");
        mw_set_hp_cur(&warded, 100);
        g.arena_round = 21;
        CHECK(combat_apply_player_damage(&g, &warded, 100, NULL) == 30 &&
              mw_hp_cur(&warded) == 70,
              "Colosseum damage cap has no post-champion cliff");
        mw_set_hp_cur(&warded, 100);
        g.arena_round = 200;
        CHECK(combat_apply_player_damage(&g, &warded, 100, NULL) == 75 &&
              mw_hp_cur(&warded) == 25,
              "Colosseum late damage cap still prevents a one-hit loss");
        g.arena_active = 0;
        g.arena_round = 0;
    }

    {
        Character caster;
        memset(&caster, 0, sizeof(caster));
        mw_character_native_ensure(&caster);
        caster.level = 5;
        caster.stat_int = caster.stat_wis = 20;
        mw_set_hp_max(&caster, 100);
        mw_set_hp_cur(&caster, 10);
        memset(&cs, 0, sizeof(cs));
        cs.monster_type_idx = 2;
        cs.monster_level = 5;
        cs.monster_hp = cs.monster_max_hp = 100;
        g.arena_active = 1;
        CHECK(arena_adjust_spell_damage(&g, &caster, 1, 12) >= 37,
              "Colosseum novice damage magic beats its unscaled formula");
        CHECK(apply_battle_spell(&g, &cs, &caster, &priest_spells[5], 2) == -3 &&
              mw_hp_cur(&caster) >= 55,
              "Colosseum Fast Cure restores enough HP to survive a response");
        mw_set_hp_cur(&caster, 10);
        g.arena_active = 0;
        CHECK(apply_battle_spell(&g, &cs, &caster, &priest_spells[5], 2) == -3 &&
              mw_hp_cur(&caster) == 30,
              "Adventure Fast Cure retains WORLD's fixed twenty HP");
    }

    p.ring_regen = 20;
    mw_set_hp_max(&p, 100);
    mw_set_hp_cur(&p, 0);
    character_tick_effects(&g, &p);
    CHECK(mw_hp_cur(&p) == 0, "regeneration cannot resurrect a dead player");
    p.ring_regen = 0;
    p.poisoned_turns = 0;
    p.diseased_turns = 0;
    mw_set_hp_cur(&p, 100);

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
          mw_hp_max(&p) == 125 && mw_hp_cur(&p) == 125,
          "permanent health");
    CHECK(apply_permanent_spell(&g, &p, NULL, 30, message, sizeof(message)) &&
          mw_weapon_enchant(&p, 1) == 150,
          "deep permanent weapon enchant");
    p.equipped_armor = 3;
    mw_set_armor_enchant(&p, 3, 0);
    CHECK(apply_permanent_spell(&g, &p, NULL, 31, message, sizeof(message)) &&
          mw_armor_enchant(&p, 3) == 100,
          "deep permanent armor enchant");
    CHECK(apply_permanent_spell(&g, &p, NULL, 32, message, sizeof(message)) &&
          mw_body_armor_plus(&p) == 100,
          "deep permanent body armor");
    CHECK(apply_permanent_spell(&g, &p, NULL, 35, message, sizeof(message)) &&
          mw_weapon_enchant(&p, 1) == 500,
          "ascendant permanent weapon enchant");
    CHECK(apply_permanent_spell(&g, &p, NULL, 36, message, sizeof(message)) &&
          mw_armor_enchant(&p, 3) == 350,
          "ascendant permanent armor enchant");
    CHECK(apply_permanent_spell(&g, &p, NULL, 37, message, sizeof(message)) &&
          mw_body_armor_plus(&p) == 300,
          "ascendant permanent body armor");
    memset(g.visited, 0, sizeof(g.visited));
    CHECK(apply_preparation_spell(&g, &p, 32, message, sizeof(message)) &&
          p.eff_protect_lv == 5 && p.eff_protect_turns == 600 &&
          p.eff_resist_poison == 600 && p.eff_resist_disease == 600 &&
          p.eff_anti_cold == 600 && p.eff_anti_fire == 600 &&
          p.eff_resist_drain == 600,
          "deep sanctuary grants every defense");
    CHECK(apply_preparation_spell(&g, &p, 33, message, sizeof(message)) &&
          g.visited[0][0] && g.visited[MAP_H - 1][MAP_W - 1],
          "cartographer eye reveals the current floor");
    p.eff_protect_lv = 0;
    p.eff_protect_turns = p.eff_resist_poison = 0;
    CHECK(apply_preparation_spell(&g, &p, 37, message, sizeof(message)) &&
          p.eff_protect_lv == 8 && p.eff_protect_turns == 1200 &&
          p.eff_resist_poison == 1200,
          "eternal sanctuary grants tier-eight defense");
    g.cur_floor = 777;
    g.cur_x = 12;
    g.cur_y = 34;
    p.raise_x = UINT16_MAX;
    p.sp_cur = 7.0f;
    mw_set_hp_cur(&p, 3);
    p.poisoned_turns = 11;
    p.diseased_turns = 22;
    CHECK(apply_preparation_spell(&g, &p, 39, message, sizeof(message)) &&
          p.raise_floor == 777 && p.raise_x == 12 && p.raise_y == 34 &&
          mw_hp_cur(&p) == 3 && p.sp_cur == 7.0f &&
          p.poisoned_turns == 11 && p.diseased_turns == 22,
          "soul anchor binds one raise point without restoring resources");
    memset(&cs, 0, sizeof(cs));
    cs.active = 1;
    cs.monster_type_idx = 0;
    cs.monster_level = 1000;
    cs.monster_hp = cs.monster_max_hp = 100000;
    CHECK(apply_battle_spell(&g, &cs, &p, &wiz_spells[31], 11) == -3 &&
          cs.monster_stopped == 30 && p.eff_stop_monster == 30,
          "deep time stop duration");
    CHECK(apply_battle_spell(&g, &cs, &p, &wiz_spells[34], 12) == 30000,
          "deep percent damage");
    {
        int annihilation =
            apply_battle_spell(&g, &cs, &p, &wiz_spells[39], 14);
        CHECK(annihilation >= 60000 && annihilation <= 120000,
              "level-fourteen annihilation damage");
    }
    mw_set_hp_cur(&p, 1);
    p.poisoned_turns = 10;
    p.diseased_turns = 20;
    CHECK(apply_battle_spell(&g, &cs, &p, &priest_spells[30], 11) == -3 &&
          mw_hp_cur(&p) == mw_hp_max(&p) &&
          !p.poisoned_turns && !p.diseased_turns,
          "greater restoration cures all");
    p.eff_protect_lv = 0;
    p.eff_protect_turns = p.eff_resist_poison = 0;
    CHECK(apply_battle_spell(&g, &cs, &p, &priest_spells[31], 11) == -3 &&
          p.eff_protect_lv == 6 && p.eff_protect_turns == 180 &&
          p.eff_resist_poison == 180,
          "divine aegis protection and resistance");
    p.sp_cur = 1.0f;
    mw_set_hp_cur(&p, 1);
    p.poisoned_turns = p.diseased_turns = 10;
    {
        int convergence =
            apply_battle_spell(&g, &cs, &p, &priest_spells[35], 12);
        CHECK(convergence == 12000 &&
              mw_hp_cur(&p) == 1 + mw_hp_max(&p) / 3 &&
              p.sp_cur == 1.0f &&
              p.poisoned_turns == 10 && p.diseased_turns == 10,
              "life convergence trades monster vitality for capped healing");
    }
    p.sp_cur = 9.0f;
    mw_set_hp_cur(&p, 1);
    p.poisoned_turns = p.diseased_turns = 10;
    p.eff_protect_lv = 0;
    p.eff_protect_turns = p.eff_anti_fire = 0;
    CHECK(apply_battle_spell(&g, &cs, &p, &priest_spells[38], 13) == -3 &&
          mw_hp_cur(&p) == 1 + mw_hp_max(&p) / 2 &&
          p.sp_cur == 9.0f &&
          !p.poisoned_turns && !p.diseased_turns &&
          p.eff_protect_lv == 7 && p.eff_protect_turns == 300 &&
          p.eff_anti_fire == 600,
          "phoenix prayer heals and wards without restoring spell points");
    p.wands[SPELL_CAT_WIZARD][32] = 2;
    {
        int result = cast_selected_spell(&g, &p, &cs, SPELL_CAT_WIZARD,
                                         32, 2, message, sizeof(message));
        CHECK(result >= 5000 && result <= 12000 &&
              p.wands[SPELL_CAT_WIZARD][32] == 1,
              "deep wand casts and consumes one charge");
    }
    p.scrolls[SPELL_CAT_PRIEST][30] = 1;
    mw_set_hp_cur(&p, 1);
    p.poisoned_turns = p.diseased_turns = 10;
    CHECK(cast_selected_spell(&g, &p, &cs, SPELL_CAT_PRIEST, 30, 1,
                              message, sizeof(message)) == -3 &&
          !p.scrolls[SPELL_CAT_PRIEST][30] &&
          mw_hp_cur(&p) == mw_hp_max(&p) &&
          !p.poisoned_turns && !p.diseased_turns,
          "deep scroll casts and is consumed");
    p.papers[SPELL_CAT_WIZARD][30] = 1;
    CHECK(cast_selected_spell(&g, &p, &cs, SPELL_CAT_WIZARD, 30, 3,
                              message, sizeof(message)) ==
              (int)p.level * 25 + 500 &&
          !p.papers[SPELL_CAT_WIZARD][30],
          "deep paper casts and is consumed");
    mw_set_experience_mode(&p, MW_EXPERIENCE_CLASSIC);
    p.scrolls[SPELL_CAT_WIZARD][30] = 1;
    CHECK(!spell_is_available(&p, SPELL_CAT_WIZARD, 30, 1, 0) &&
          cast_selected_spell(&g, &p, &cs, SPELL_CAT_WIZARD, 30, 1,
                              message, sizeof(message)) == 0 &&
          p.scrolls[SPELL_CAT_WIZARD][30] == 1 &&
          strstr(message, "ENHANCED"),
          "Classic hides and preserves Enhanced spell items");
    mw_set_experience_mode(&p, MW_EXPERIENCE_ENHANCED);

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

    mw_set_hp_max(&p, 100);
    mw_set_hp_cur(&p, 100);
    g.cheat_god_mode = 1;
    CHECK(combat_apply_player_damage(&g, &p, 999, NULL) == 0 &&
          mw_hp_cur(&p) == 100,
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
    CHECK(combat_apply_player_damage(&g, &p, 25, NULL) == 25 &&
          mw_hp_cur(&p) == 75,
          "normal incoming HP damage restored");
    mw_set_hp_max(&p, 100000);
    mw_set_hp_cur(&p, 100000);
    CHECK(combat_apply_player_damage(&g, &p, 25000, NULL) == 25000 &&
          mw_hp_cur(&p) == 75000 && mw_hp_max(&p) == 100000,
          "Enhanced HP remains live above the legacy 16-bit ceiling");
    {
        Character relic = {0};
        mw_set_experience_mode(&relic, MW_EXPERIENCE_ENHANCED);
        relic.level = 100;
        mw_set_hp_max(&relic, 100);
        mw_set_hp_cur(&relic, 20);
        relic.sp_cur = 0.0f;
        relic.sp_max = 20.0f;
        mw_set_relic_owned(&relic, MW_RELIC_ARCANE_RING, 1);
        for (int i = 0; i < 4; i++) character_tick_effects(&g, &relic);
        CHECK(relic.sp_cur == 1.0f &&
              relic.native.relic_regen_phase == 0,
              "Arcane Renewal restores one SP every four actions");

        mw_set_relic_owned(&relic, MW_RELIC_BLOODSTONE_SIGNET, 1);
        CHECK(combat_apply_bloodstone(&relic, 200) == 10 &&
              mw_hp_cur(&relic) == 30,
              "Bloodstone heals five percent with level cap");

        mw_set_relic_owned(&relic, MW_RELIC_DEEPWARD_AMULET, 1);
        mw_set_hp_cur(&relic, 100);
        CHECK(combat_apply_player_damage(&g, &relic, 20, NULL) == 17 &&
              mw_hp_cur(&relic) == 83,
              "Deepward reduces incoming monster damage fifteen percent");
        relic.poisoned_turns = 1;
        relic.stat_str = 20;
        character_tick_effects(&g, &relic);
        CHECK(relic.poisoned_turns == 900 && relic.stat_str == 19,
              "Deepward doubles time between poison and disease drains");

        mw_set_relic_owned(&relic, MW_RELIC_PHOENIX_SEAL, 1);
        mw_set_hp_cur(&relic, 10);
        int phoenix_saved = 0;
        CHECK(combat_apply_player_damage(&g, &relic, 999,
                                         &phoenix_saved) == 9 &&
              phoenix_saved && mw_hp_cur(&relic) == 1 &&
              relic.native.relic_phoenix_cooldown == 301,
              "Phoenix Seal prevents lethal strike and starts cooldown");
        character_tick_effects(&g, &relic);
        CHECK(relic.native.relic_phoenix_cooldown == 300,
              "Phoenix Seal cooldown advances with player actions");

        mw_set_relic_owned(&relic, MW_RELIC_SAGE_PRISM, 1);
        CHECK(mw_relic_count(&relic) == MW_RELIC_COUNT,
              "all five Enhanced relics persist independently");
        mw_set_experience_mode(&relic, MW_EXPERIENCE_CLASSIC);
        CHECK(mw_relic_count(&relic) == 0,
              "Enhanced relics are inert and hidden in Classic mode");
    }
#undef CHECK
    printf("Magic/equipment/status self-test: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
