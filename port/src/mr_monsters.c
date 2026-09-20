#include "mr_monsters.h"
#include "mr_math.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int mr_monster_index(int dungeon_level, int floor_slot) {
    if (dungeon_level < 1 || dungeon_level > 70 ||
        floor_slot < 1 || floor_slot > 40)
        return 0;
    return (dungeon_level - 1) * 40 + floor_slot;
}

int mr_monster_base_type(int global_index) {
    if (global_index < 1 || global_index > 2800) return 0;
    /* DUNSMALL 6BE3-6C0C and 80B0-80DB:
     * index - INT(index/20)*20 + 1. */
    return global_index % 20 + 1;
}

int mr_monster_type(int global_index, int dungeon_level,
                    int persistent_health) {
    int type = mr_monster_base_type(global_index);
    if (!type || dungeon_level < 1 || dungeon_level > 70) return 0;
    /* Types 19/20 are suppressed on the first six floors. From floor seven,
     * health above 140 selects their extra 21/22 art/name variants. */
    if (type >= 19 && type <= 20) {
        if (dungeon_level < 7) type -= 8;
        else if (abs(persistent_health) > 140) type += 2;
    }
    return type;
}

static int mr_divides_evenly(int value, int divisor) {
    return value / divisor * divisor == value;
}

int mr_monster_level(int global_index) {
    if (global_index < 1 || global_index > 2800) return 0;
    /* DUNSMALL 80DE-81A3. The base expression is
     * INT(global_index/40 + 1), followed by one bonus for each exact
     * divisibility test at 1/2, 1/4, 1/8, and 1/16. */
    int level = global_index / 40 + 1;
    if (mr_divides_evenly(global_index, 2)) level++;
    if (mr_divides_evenly(global_index, 4)) level++;
    if (mr_divides_evenly(global_index, 8)) level++;
    if (mr_divides_evenly(global_index, 16)) level++;
    return level;
}

int mr_monster_generated_health(int dungeon_level, float random_unit) {
    if (dungeon_level < 0 || dungeon_level > 70) return 0;
    /* Replacement-monster health at A3C8-A404. */
    return (int)floorf(random_unit * dungeon_level * 8.0f) +
           dungeon_level * 2 + 1;
}

float mr_monster_experience_reward(int monster_level, int behavior_class) {
    if (monster_level < 1) return 0.0f;
    /* DUNSMALL 845D-84BE. The first term is a nested exponent, not the more
     * tempting (level^.96)*1.5 interpretation. Behavior class 2 is then
     * explicitly multiplied by zero. */
    float level = (float)monster_level;
    float exponent = mr_qb3_pow(level, 0.96f);
    float first = mr_qb3_pow(1.5f, exponent);
    first = first * 5.0f;
    float second = mr_qb3_pow(level - 1.0f, 1.4f);
    second = second * 30.0f;
    float combined = first + second;
    combined = combined + 15.0f;
    float reward = floorf(combined);
    return behavior_class == 2 ? 0.0f : reward;
}

int mr_monster_guarantees_coin_loot(int global_index) {
    if (global_index < 1 || global_index > 2800) return 0;
    /* DS:B6EC is set by comparing index*.5 with INT(index*.5). */
    return (global_index & 1) == 0;
}

int mr_monster_coin_loot_gate(int global_index, float random_unit) {
    if (global_index < 1 || global_index > 2800) return 0;
    return mr_monster_guarantees_coin_loot(global_index) ||
           (int)floorf(random_unit * 5.0f) != 1;
}

int mr_monster_pack_position(int x, int y) {
    if (x < 0 || x > 31 || y < 0 || y > 2047) return -1;
    return y * 32 + x;
}

int mr_monster_position_x(int packed_position) {
    if (packed_position < 0) packed_position = -packed_position;
    return packed_position - (packed_position / 32) * 32;
}

int mr_monster_position_y(int packed_position) {
    if (packed_position < 0) packed_position = -packed_position;
    return packed_position / 32;
}

int mr_monster_respawn_roll(int dungeon_level, float health_random,
                            float x_random, float y_random,
                            MRMonsterRespawnRoll *roll) {
    if (!roll || dungeon_level < 0 || dungeon_level > 70) return 0;
    if (health_random < 0.0f) health_random = 0.0f;
    if (x_random < 0.0f) x_random = 0.0f;
    if (y_random < 0.0f) y_random = 0.0f;
    if (health_random >= 1.0f) health_random = nextafterf(1.0f, 0.0f);
    if (x_random >= 1.0f) x_random = nextafterf(1.0f, 0.0f);
    if (y_random >= 1.0f) y_random = nextafterf(1.0f, 0.0f);
    roll->health = mr_monster_generated_health(dungeon_level, health_random);
    roll->x = (int)floorf(x_random * 18.0f) + 2;
    roll->y = (int)floorf(y_random * 17.0f) + 1;
    roll->packed_position = mr_monster_pack_position(roll->x, roll->y);
    return 1;
}

int mr_monster_uses_deep_resources(int dungeon_level) {
    return dungeon_level >= MR_DEEP_MONSTER_RESOURCE_FLOOR;
}

int mr_monsters_self_test(char *error, size_t error_size) {
    int failed = 0;
    failed |= mr_monster_index(1, 1) != 1;
    failed |= mr_monster_index(35, 40) != 1400;
    failed |= mr_monster_index(70, 40) != 2800;
    failed |= mr_monster_base_type(1) != 2;
    failed |= mr_monster_base_type(19) != 20;
    failed |= mr_monster_base_type(20) != 1;
    failed |= mr_monster_type(18, 1, 200) != 11;
    failed |= mr_monster_type(19, 1, 200) != 12;
    failed |= mr_monster_type(18, 7, 140) != 19;
    failed |= mr_monster_type(18, 7, 141) != 21;
    failed |= mr_monster_type(19, 7, -141) != 22;
    failed |= mr_monster_level(1) != 1;
    failed |= mr_monster_level(2) != 2;
    failed |= mr_monster_level(4) != 3;
    failed |= mr_monster_level(8) != 4;
    failed |= mr_monster_level(16) != 5;
    failed |= mr_monster_level(40) != 5;
    failed |= mr_monster_level(80) != 7;
    failed |= mr_monster_generated_health(10, .5) != 61;
    failed |= mr_monster_experience_reward(1, 1) != 22;
    failed |= mr_monster_experience_reward(5, 1) != 257;
    /* The host double-precision formula yields 5,980,053 at level 40;
     * BRUN30's nested MBF-single $FEXC sequence yields 5,980,042. */
    failed |= mr_monster_experience_reward(40, 1) != 5980042;
    failed |= mr_monster_experience_reward(5, 2) != 0;
    failed |= !mr_monster_guarantees_coin_loot(80);
    failed |= mr_monster_guarantees_coin_loot(79);
    failed |= mr_monster_coin_loot_gate(79, .25);
    failed |= !mr_monster_coin_loot_gate(79, .05);
    failed |= !mr_monster_coin_loot_gate(80, .25);
    MRMonsterRespawnRoll respawn;
    failed |= !mr_monster_respawn_roll(10, .5, .5, .5, &respawn);
    failed |= respawn.health != 61 || respawn.x != 11 || respawn.y != 9 ||
              respawn.packed_position != 299;
    failed |= mr_monster_position_x(392) != 8 ||
              mr_monster_position_y(392) != 12;
    failed |= mr_monster_uses_deep_resources(34);
    failed |= !mr_monster_uses_deep_resources(35);
    if (failed) {
        if (error && error_size)
            snprintf(error, error_size,
                     "DUNSMALL monster formula self-test mismatch");
        return 0;
    }
    return 1;
}
