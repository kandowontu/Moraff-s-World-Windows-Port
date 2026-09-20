#ifndef MR_MONSTERS_H
#define MR_MONSTERS_H

#include <stddef.h>
#include <stdint.h>

#define MR_MONSTER_TYPES_PER_SET 22
#define MR_MONSTER_BASE_TYPES 20
#define MR_DEEP_MONSTER_RESOURCE_FLOOR 35

/* Global indices are the original BASIC indices 1..2800. Floors 1..70 use
 * indices 40*floor-39 through 40*floor. */
int mr_monster_index(int dungeon_level, int floor_slot);
int mr_monster_base_type(int global_index);
int mr_monster_type(int global_index, int dungeon_level,
                    int persistent_health);
int mr_monster_level(int global_index);
int mr_monster_generated_health(int dungeon_level, float random_unit);
float mr_monster_experience_reward(int monster_level, int behavior_class);
int mr_monster_guarantees_coin_loot(int global_index);
/* DUNSMALL A528-A55E: even indices always enter the complete loot pipeline;
 * odd indices enter unless INT(RND*5) equals one. The RND is consumed for
 * both parities by QuickBASIC's eager OR evaluation. */
int mr_monster_coin_loot_gate(int global_index, float random_unit);
int mr_monster_pack_position(int x, int y);
int mr_monster_position_x(int packed_position);
int mr_monster_position_y(int packed_position);

typedef struct MRMonsterRespawnRoll {
    int health;
    int x;
    int y;
    int packed_position;
} MRMonsterRespawnRoll;

/* One A3C8-A45D respawn candidate. The original repeats the x/y portion while
 * the candidate cell is occupied; health is rolled only once before that
 * loop. */
int mr_monster_respawn_roll(int dungeon_level, float health_random,
                            float x_random, float y_random,
                            MRMonsterRespawnRoll *roll);
int mr_monster_uses_deep_resources(int dungeon_level);

int mr_monsters_self_test(char *error, size_t error_size);

#endif
