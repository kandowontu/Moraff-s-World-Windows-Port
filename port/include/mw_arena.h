#ifndef MW_ARENA_H
#define MW_ARENA_H

#include "mw_game.h"

#define ARENA_SAVE_MAGIC   0x314C4F43u /* "COL1" */
#define ARENA_SAVE_VERSION 9u
#define ARENA_REWARD_COUNT 7
#define ARENA_PERK_COUNT   8

enum ArenaRewardKind {
    ARENA_REWARD_WEAPON = 0,
    ARENA_REWARD_ARMOR,
    ARENA_REWARD_SPELL,
    ARENA_REWARD_SCROLL,
    ARENA_REWARD_WAND,
    ARENA_REWARD_PAPER,
    ARENA_REWARD_BOON
};

enum ArenaRarity {
    ARENA_COMMON = 0,
    ARENA_UNCOMMON,
    ARENA_RARE,
    ARENA_EPIC,
    ARENA_LEGENDARY,
    ARENA_ULTRA_RARE,
    ARENA_SUPER_ULTRA_RARE,
    ARENA_RARITY_COUNT
};

enum ArenaDifficulty {
    ARENA_DIFFICULTY_EASY = 0,
    ARENA_DIFFICULTY_NORMAL,
    ARENA_DIFFICULTY_HARD,
    ARENA_DIFFICULTY_COUNT
};

typedef struct ArenaReward {
    u8 kind;
    u8 rarity;
    u8 category;
    u8 source;
    u16 item;
    u16 amount;
    u32 value;
} ArenaReward;

typedef struct ArenaSave {
    u32 magic;
    u32 version;
    u32 record_size;
    Character base_character;
    Character character;
    u32 run_number;
    u32 round;
    u32 current_streak;
    u32 best_streak;
    u32 total_victories;
    u32 total_deaths;
    u16 perk_levels[ARENA_PERK_COUNT];
    u16 enemy_type;
    u16 enemy_level;
    u32 enemy_hp;
    u32 enemy_max_hp;
    u16 enemy_asleep;
    u16 enemy_held;
    u16 enemy_stopped;
    u8 enemy_active;
    u8 enemy_champion;
    u8 pending_rewards;
    u8 in_run;
    /* These two bytes replace structure padding present in versions 1-2, so
       the on-disk record remains exactly the same size. */
    u8 difficulty;
    u8 sound_mode; /* 0=inherit once, 1=on, 2=off */
    ArenaReward rewards[ARENA_REWARD_COUNT];
} ArenaSave;

_Static_assert(sizeof(ArenaSave) == 4848,
               "Colosseum save record layout changed");

int arena_load_save(Game *g, int slot, ArenaSave *save);
int arena_save_save(Game *g, int slot, ArenaSave *save);
int arena_delete_save(Game *g, int slot);
void arena_initialize_save(ArenaSave *save, const Character *created);
int arena_select_difficulty(Game *g);
const char *arena_difficulty_name(int difficulty);
void arena_run(Game *g, int slot, ArenaSave *save);
int arena_self_test(void);

#endif
