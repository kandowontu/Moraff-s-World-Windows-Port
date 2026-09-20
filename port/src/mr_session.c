#include "mr_session.h"
#include "mr_spells.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int mr_session_int(float value) {
    return (int)floorf(value);
}

static void mr_session_error(char *error, size_t error_size,
                             const char *message) {
    if (error && error_size) snprintf(error, error_size, "%s", message);
}

static void mr_session_leave_combat(MRSession *session);
static int mr_session_monster_turn(MRSession *session,
                                   MRMonsterTurnResult *result);

static int mr_session_recover_native_row_twenty(MRSession *session) {
    if (!session) return 0;
    /* Releases before this parity correction treated the spare BASIC array
     * row 20 as playable. Original DUNSMALL never reaches it: south movement
     * stops at row 19. Recover only that identifiable native-port state so a
     * previously affected character can move again; do not rewrite ordinary
     * original coordinates or the preserved row-20 BSAVE data. */
    if (mr_session_int(session->save.character.player_y) != 20) return 0;
    session->save.character.player_y = 19.0f;
    return 1;
}

static void mr_session_join(char *out, size_t out_size, const char *directory,
                            const char *name) {
    size_t length = strlen(directory);
    const char *separator = length &&
        (directory[length - 1] == '/' || directory[length - 1] == '\\')
        ? "" : "/";
    snprintf(out, out_size, "%s%s%s", directory, separator, name);
}

int mr_session_load(MRSession *session, const char *resource_directory,
                    const char *character_text_path,
                    const char *character_binary_path, uint32_t rng_seed,
                    char *error, size_t error_size) {
    if (!session || !resource_directory || !character_text_path ||
        !character_binary_path) {
        mr_session_error(error, error_size, "Invalid Revenge session request");
        return 0;
    }
    memset(session, 0, sizeof(*session));
    if (!mr_world_load(&session->world, resource_directory, error,
                       error_size) ||
        !mr_save_load(&session->save, character_text_path,
                      character_binary_path, error, error_size))
        return 0;
    mr_session_recover_native_row_twenty(session);
    mr_input_init(&session->input);
    session->configured_action_delay = 0;
    session->sound_disabled = 1;
    session->primary_color = 0;
    session->secondary_color = 2;
    /* DUNSMALL 300C submits COLOR 0,2 twice. After the first call preserves
     * BX/DX, XCHG followed by MOV reconstructs the same primary,secondary
     * argument order for the second call; it does not swap the COLOR args. */
    session->effective_background_color = 0;
    session->effective_palette_selector = 2;
    session->rng.state = rng_seed & 0xFFFFFFU;
    if (!session->rng.state) mr_rng_seed_default(&session->rng);
    int level = mr_session_int(session->save.character.dungeon_level);
    session->phase = level == 0 ? MR_SESSION_TOWN : MR_SESSION_EXPLORING;
    session->player_x_at_last_monster_update =
        mr_session_int(session->save.character.player_x);
    session->player_y_at_last_monster_update =
        mr_session_int(session->save.character.player_y);
    mr_session_rebuild_occupancy(session);
    mr_session_reveal_current_cell(session);
    return 1;
}

void mr_session_apply_original_load_epilogue(MRSession *session) {
    if (!session) return;
    MRCharacter *character = &session->save.character;

    /* B98F-B99A tests only fountain X. A zero value calls B2CF, whose two
     * independent assignments are INT(RND*15)+2. This must remain outside
     * mr_session_load: original startup consumes the sound-color and both
     * loading-review random values before B674 reaches this epilogue. */
    if (character->persistent_values[MR_PERSISTENT_FOUNTAIN_X] == 0.0f) {
        character->persistent_values[MR_PERSISTENT_FOUNTAIN_X] =
            (float)(mr_session_int(mr_rng_next(&session->rng) * 15.0f) + 2);
        character->persistent_values[MR_PERSISTENT_FOUNTAIN_Y] =
            (float)(mr_session_int(mr_rng_next(&session->rng) * 15.0f) + 2);
    }

    /* B99D-B9AF explicitly zeroes BASIC persistent values 12..14 after the
     * generic reader loaded them. They are serialized by SAVE but never
     * survive starting a new DUNSMALL invocation. */
    character->persistent_values[MR_PERSISTENT_BREATHE_FIRE_EXPIRY] = 0.0f;
    character->persistent_values[MR_PERSISTENT_SHIELDING_EXPIRY] = 0.0f;
    character->persistent_values[MR_PERSISTENT_AGILITY_EXPIRY] = 0.0f;
}

void mr_session_rebuild_occupancy(MRSession *session) {
    if (!session) return;
    memset(session->occupancy, 0, sizeof(session->occupancy));
    int level = mr_session_int(session->save.character.dungeon_level);
    if (level < 1 || level > MR_DUNGEON_LEVELS) {
        session->idle_monster_global_cursor = 0;
        return;
    }
    /* rebuild_current_floor_monster_occupancy, 7932-7944. */
    session->idle_monster_global_cursor = mr_monster_index(level, 1);
    for (int slot = 1; slot <= MR_MONSTER_SLOTS_PER_LEVEL; slot++) {
        int index = mr_monster_index(level, slot);
        if (session->world.monster_health[index] <= 0) continue;
        int packed = session->world.monster_positions[index];
        int x = mr_monster_position_x(packed);
        int y = mr_monster_position_y(packed);
        if (x >= 1 && x <= 20 && y >= 1 && y <= 19)
            session->occupancy[y][x] = index;
    }
}

int mr_session_monster_at(const MRSession *session, int x, int y) {
    if (!session || x < 0 || x >= MR_OCCUPANCY_COLUMNS || y < 0 ||
        y >= MR_OCCUPANCY_ROWS)
        return 0;
    return session->occupancy[y][x];
}

static int mr_session_abs(int value) {
    return value < 0 ? -value : value;
}

static void mr_session_monster_candidate(int x, int y,
                                         MRDirection direction,
                                         int *next_x, int *next_y) {
    *next_x = x;
    *next_y = y;
    if (direction == MR_DIRECTION_NORTH) (*next_y)--;
    else if (direction == MR_DIRECTION_EAST) (*next_x)++;
    else if (direction == MR_DIRECTION_SOUTH) (*next_y)++;
    else if (direction == MR_DIRECTION_WEST) (*next_x)--;
    if (*next_x < 1) *next_x = 1;
    if (*next_x > 20) *next_x = 20;
    if (*next_y < 1) *next_y = 1;
    if (*next_y > 19) *next_y = 19;
}

MRMonsterAdvanceCode mr_session_advance_active_monster(
    MRSession *session, int global_index, int projected_y,
    MRMonsterAdvanceResult *result) {
    MRMonsterAdvanceResult local;
    if (!result) result = &local;
    memset(result, 0, sizeof(*result));
    result->global_index = global_index;
    if (!session || global_index < 1 ||
        global_index >= MR_MONSTER_TABLE_INDEX_COUNT ||
        session->world.monster_health[global_index] <= 0)
        return MR_MONSTER_ADVANCE_NONE;

    int level = mr_session_int(session->save.character.dungeon_level);
    int first = mr_monster_index(level, 1);
    int last = mr_monster_index(level, MR_MONSTER_SLOTS_PER_LEVEL);
    if (!first || global_index < first || global_index > last)
        return MR_MONSTER_ADVANCE_NONE;

    /* DUNSMALL 70A1-70EB eagerly consumes this RND before applying the
     * exploration-depth and combat-monster tests. More carried weight makes
     * the early return less likely, which is the source of the original
     * encumbrance warning about monsters receiving more strikes. */
    int weight_gate = mr_session_int(
        (float)mr_rng_next(&session->rng) * 700.0f) - 400;
    int combat_view = session->phase == MR_SESSION_COMBAT;
    if (!combat_view && session->combat_global_index != global_index &&
        weight_gate > session->save.character.carried_weight) {
        result->code = MR_MONSTER_ADVANCE_WEIGHT_DELAY;
        return result->code;
    }

    int packed = session->world.monster_positions[global_index];
    int x = mr_monster_position_x(packed);
    int y = mr_monster_position_y(packed);
    int player_x = mr_session_int(session->save.character.player_x);
    int player_y = mr_session_int(session->save.character.player_y);
    result->old_x = result->new_x = x;
    result->old_y = result->new_y = y;
    if (x == player_x && y == player_y)
        return MR_MONSTER_ADVANCE_NONE;

    int tracking = 1;
    int direction = 0;
    int next_x = x;
    int next_y = y;
    int skip_wall = 0;

    if (combat_view && session->combat_global_index == global_index) {
        int projected = mr_session_monster_at(session, player_x, player_y);
        session->active_monster_pursuit_target = projected;
        tracking = projected;
        next_x = player_x;
        next_y = player_y;
        skip_wall = 1;
    } else {
        int distance = mr_session_abs(x - player_x) +
                       mr_session_abs(y - player_y);
        if (distance < 4 &&
            global_index == session->primary_tracking_global_index) {
            session->active_monster_pursuit_target = 1;
        } else if (distance < 5 &&
                   global_index == session->secondary_tracking_global_index) {
            session->active_monster_pursuit_target = 1;
        } else {
            if (global_index == session->primary_tracking_global_index)
                session->primary_tracking_global_index = 0;
            if (global_index == session->secondary_tracking_global_index)
                session->secondary_tracking_global_index = 0;

            if (mr_session_abs(x - player_x) < 6 &&
                mr_session_abs(projected_y - player_y) < 6) {
                tracking = 1;
                int weight_acquire =
                    mr_session_int((float)mr_rng_next(&session->rng) * 600.0f);
                if (weight_acquire < session->save.character.carried_weight) {
                    session->active_monster_pursuit_target = 1;
                } else {
                    /* Compiled BASIC evaluates the random half even when the
                     * invisibility half is already false. */
                    int invisibility_roll =
                        mr_session_int((float)mr_rng_next(&session->rng) * 10.0f);
                    int invisible =
                        mr_session_int(session->save.character.active_effects[
                            MR_EFFECT_INVISIBILITY]) == 1;
                    if (!(invisible && invisibility_roll < 5)) {
                        session->active_monster_pursuit_target = 1;
                        if (!session->primary_tracking_global_index)
                            session->primary_tracking_global_index = global_index;
                    }
                }
            } else {
                tracking = 0;
                session->active_monster_pursuit_target = 0;
            }
        }

        /* The RND is consumed even when a zero pursuit target already makes
         * the OR expression true. */
        int random_direction_gate = mr_session_int(
            (float)mr_rng_next(&session->rng) *
            (mr_monster_level(global_index) + 35));
        if (!session->active_monster_pursuit_target ||
            random_direction_gate < 15) {
            direction = mr_session_int(
                (float)mr_rng_next(&session->rng) * 4.0f) + 1;
        } else if (tracking == 1) {
            if (session->input.facing == MR_DIRECTION_NORTH ||
                session->input.facing == MR_DIRECTION_SOUTH) {
                direction = x > player_x ? MR_DIRECTION_WEST
                                         : MR_DIRECTION_EAST;
            } else {
                direction = y > player_y ? MR_DIRECTION_NORTH
                                         : MR_DIRECTION_SOUTH;
            }
        } else {
            if (x == player_x)
                direction = y > player_y ? MR_DIRECTION_NORTH
                                         : MR_DIRECTION_SOUTH;
            if (y == player_y)
                direction = x > player_x ? MR_DIRECTION_WEST
                                         : MR_DIRECTION_EAST;
        }
        if (direction < MR_DIRECTION_NORTH || direction > MR_DIRECTION_WEST)
            return MR_MONSTER_ADVANCE_NONE;

        float factor_a = (float)y;
        float factor_b = (float)x;
        float factor_c = 1.0f;
        if (direction == MR_DIRECTION_EAST) {
            factor_b += 1.0f;
            factor_c = 2.0f;
        } else if (direction == MR_DIRECTION_SOUTH) {
            factor_a += 1.0f;
        } else if (direction == MR_DIRECTION_WEST) {
            factor_c = 2.0f;
        }
        if (mr_world_procedural_wall_value(
                level,
                (float)session->save.character.dungeon_generation_seed,
                factor_a, factor_b, factor_c) > 7) {
            result->direction = (MRDirection)direction;
            result->code = MR_MONSTER_ADVANCE_WALL;
            return result->code;
        }
        mr_session_monster_candidate(x, y, (MRDirection)direction,
                                     &next_x, &next_y);
    }

    result->direction = (MRDirection)direction;
    result->new_x = next_x;
    result->new_y = next_y;
    if (mr_session_monster_at(session, next_x, next_y) > 0) {
        result->code = MR_MONSTER_ADVANCE_OCCUPIED;
        return result->code;
    }
    if (next_x == x && next_y == y)
        return MR_MONSTER_ADVANCE_NONE;

    session->occupancy[next_y][next_x] = session->occupancy[y][x];
    session->occupancy[y][x] = 0;
    session->world.monster_positions[global_index] =
        (uint16_t)mr_monster_pack_position(next_x, next_y);
    result->code = next_x == player_x && next_y == player_y
        ? MR_MONSTER_ADVANCE_REACHED_PLAYER : MR_MONSTER_ADVANCE_MOVED;
    (void)skip_wall;
    return result->code;
}

int mr_session_update_active_monsters(MRSession *session,
                                      MRMonsterAdvanceResult results[2]) {
    if (!session || session->phase != MR_SESSION_EXPLORING) return 0;
    int player_x = mr_session_int(session->save.character.player_x);
    int player_y = mr_session_int(session->save.character.player_y);
    if (player_x == session->player_x_at_last_monster_update &&
        player_y == session->player_y_at_last_monster_update)
        return 0;
    session->player_x_at_last_monster_update = player_x;
    session->player_y_at_last_monster_update = player_y;

    MRMonsterAdvanceResult local[2];
    if (!results) results = local;
    memset(results, 0, sizeof(local));
    int count = 0;
    int primary = session->primary_tracking_global_index;
    if (primary) {
        mr_session_advance_active_monster(session, primary,
                                          mr_monster_position_y(
                                              session->world.monster_positions[
                                                  primary]),
                                          &results[count++]);
    }

    /* DUNSMALL 0C81-0CBF uses index*.5 versus INT(index*.5): the secondary
     * advances only on odd global indices. This is parity, not probability. */
    int secondary = session->secondary_tracking_global_index;
    if (secondary && (secondary & 1)) {
        mr_session_advance_active_monster(session, secondary,
                                          mr_monster_position_y(
                                              session->world.monster_positions[
                                                  secondary]),
                                          &results[count++]);
    }
    /* 0C0B-0CBF only writes B530 and calls 70A1. It never transfers to the
     * combat loop itself, even when 70A1 moves a monster into the player's
     * cell. The outer controller observes occupancy later at 08F6/49B1,
     * after the intervening status/render boundary. */
    return count;
}

int mr_session_idle_monster_interval(int current_monster_level,
                                     int player_level,
                                     float speed_calibration) {
    /* DUNSMALL 7EEC-7F25. QuickBASIC INT is floor, including for a
     * theoretically negative intermediate, and the result is clamped to a
     * minimum of eight before it is used as the RND multiplier. */
    int interval = mr_session_int(
        (165.0f - current_monster_level + player_level) *
        speed_calibration / 20.0f);
    return interval < 8 ? 8 : interval;
}

int mr_session_idle_monster_poll(MRSession *session,
                                 float speed_calibration,
                                 MRMonsterAdvanceResult *result) {
    MRMonsterAdvanceResult local;
    if (!result) result = &local;
    memset(result, 0, sizeof(*result));
    if (!session) return 0;

    int level = mr_session_int(session->save.character.dungeon_level);

    /* DUNSMALL 7EEC-7F42 consumes exactly one RND every time the helper is
     * called, including in combat and on town level zero. 7001 performs the
     * level-zero early return only after that random gate succeeds. The old
     * native guard froze every non-exploration caller and also skipped this
     * RNG boundary in the town Wizard Guild and post-combat menus. B6B4
     * retains the most recently initialized combat-monster level; it is zero
     * before the first encounter, just as the zeroed BASIC global was. */
    int interval = mr_session_idle_monster_interval(
        session->combat_profile.level,
        mr_session_int(session->save.character.player_level),
        speed_calibration);
    if (mr_session_int((float)mr_rng_next(&session->rng) * interval) != 1)
        return 0;

    /* cycle_floor_monster_cursor_and_maybe_advance, 7001-700C. */
    if (level == 0) return 1;
    int first = mr_monster_index(level, 1);
    int last = mr_monster_index(level, MR_MONSTER_SLOTS_PER_LEVEL);
    if (!first || !last) return 1;

    /* 7001-707C increments the floor-global record cursor, wraps it at
     * 40*level, and copies the selected record to B530. 7085-70A0 is the
     * exact index*.5 versus INT(index*.5) parity gate: even records return,
     * odd records enter the ordinary 70A1 pursuit routine. */
    if (session->idle_monster_global_cursor < first ||
        session->idle_monster_global_cursor > last)
        session->idle_monster_global_cursor = first;
    session->idle_monster_global_cursor++;
    if (session->idle_monster_global_cursor > last)
        session->idle_monster_global_cursor = first;
    result->global_index = session->idle_monster_global_cursor;
    if (!(result->global_index & 1))
        return 1;

    mr_session_advance_active_monster(
        session, result->global_index,
        mr_monster_position_y(
            session->world.monster_positions[result->global_index]),
        result);
    /* 7EEC itself never changes controller ownership. Its callers inspect
     * the resulting occupancy: 08F6 enters combat, while 7DC9 has its own
     * B60E/B578 rules. Keeping this primitive state-neutral is essential for
     * a monster that arrives while a preparation selector owns input. */
    return 1;
}

void mr_session_reveal_current_cell(MRSession *session) {
    if (!session) return;
    int level = mr_session_int(session->save.character.dungeon_level);
    int x = mr_session_int(session->save.character.player_x);
    int y = mr_session_int(session->save.character.player_y);
    if (level < 0 || level >= MR_EXPLORED_LEVELS || x < 1 || x > 20 ||
        y < 1 || y > 19)
        return;
    /* DUNSMALL 0965-099C: explored[level][y] OR 2^(20-x). */
    session->save.explored[level][y] |= 1U << (20 - x);
}

static void mr_session_apply_status_cycle(MRSession *session,
                                          MRSessionCycleResult *result,
                                          int allow_health_ring) {
    /* draw_status_and_map, DUNSMALL 4028-4068: Rings of Health restore their
     * count only when redraw scratch DS:B4C2 is zero.  The main controller,
     * rotations, and procedural-wall branches deliberately set it to one;
     * a completed map redraw leaves it zero for a successful/boundary move
     * and for the 091B combat-entry redraw.  Disease below is not guarded by
     * B4C2 and therefore still advances at every one of those 3FFC calls. */
    MRCharacter *player = &session->save.character;
    float rings = player->magic_item_values[MR_MAGIC_HEALTH_RINGS];
    if (allow_health_ring && rings > 0.0f &&
        player->health_current < player->health_max) {
        float before = player->health_current;
        player->health_current += rings;
        if (player->health_current > player->health_max)
            player->health_current = player->health_max;
        result->health_regenerated = (int)(player->health_current - before);
    }

    /* 406B-40F4 increments the disease counter on the same cycle boundary.
     * At each exact multiple of 100 it chooses INT(RND*6), drains that
     * attribute by one, displays a four-second warning, and clamps all six
     * attributes back to a minimum of one. */
    float *disease = &player->persistent_values[MR_PERSISTENT_DISEASE];
    if (*disease > 0.0f) {
        *disease += 1.0f;
        int disease_counter = (int)*disease;
        if (disease_counter > 0 && disease_counter % 100 == 0 &&
            (float)disease_counter == *disease) {
            int attribute = (int)floorf(
                (float)mr_rng_next(&session->rng) * 6.0f);
            if (attribute < 0) attribute = 0;
            if (attribute >= MR_SAVE_ATTRIBUTES)
                attribute = MR_SAVE_ATTRIBUTES - 1;
            player->attributes[attribute] -= 1.0f;
            mr_clamp_character_attributes(player);
            result->disease_triggered = 1;
            result->disease_attribute = attribute;
        }
    }
}

void mr_session_begin_combat_entry_status_cycle(
    MRSession *session, MRSessionCycleResult *result) {
    MRSessionCycleResult local;
    if (!result) result = &local;
    memset(result, 0, sizeof(*result));
    result->disease_attribute = -1;
    if (!session || session->phase != MR_SESSION_COMBAT) return;

    /* A direct player step onto a monster reaches combat through 49B1 from
     * the movement routine's own 3FFC call.  Replaying 3FFC here would add a
     * second disease tick and possibly a second ring heal. */
    if (session->combat_entry_redraw_already_applied) {
        session->combat_entry_redraw_already_applied = 0;
        return;
    }

    /* 091B calls draw_status_and_map once after an idle monster reaches the
     * player; the movement-driven path likewise reaches 3FFC before 49B1
     * transfers control to 803A.  Only 3FFC's ring/disease work occurs at
     * that boundary.  The ambient-advice roll lives outside 3FFC at
     * 06D2-0841 and is bypassed when rendering jumps into combat. */
    mr_session_apply_status_cycle(session, result, 1);
}

void mr_session_begin_command_cycle(MRSession *session,
                                    MRSessionCycleResult *result) {
    MRSessionCycleResult local;
    if (!result) result = &local;
    memset(result, 0, sizeof(*result));
    result->disease_attribute = -1;
    /* DUNSMALL's command-cycle status routine (3FFC) belongs to the
     * exploration/town loop.  Combat has a separate loop beginning at
     * 803A and never calls 3FFC once the encounter owns input. */
    if (!session ||
        (session->phase != MR_SESSION_EXPLORING &&
         session->phase != MR_SESSION_TOWN))
        return;

    /* Both main-loop entries at 0627/0636 assign B4C2=1 immediately before
     * 3FFC.  The old native cycle healed on every prompt, which the original
     * never does. */
    mr_session_apply_status_cycle(session, result, 0);
    MRCharacter *player = &session->save.character;

    /* 06D2-0841 executes once per positive-floor command cycle after the
     * status/disease update.  INT(RND*7) is consumed even when no advice is
     * eligible; this boundary affects all later monster and reward rolls. */
    if (session->phase != MR_SESSION_TOWN &&
        mr_session_int(player->dungeon_level) > 0) {
        int bucket = mr_session_int(
            (float)mr_rng_next(&session->rng) * 7.0f);
        if (bucket == 1 &&
            player->experience + player->pending_experience >
                mr_experience_threshold(mr_session_int(player->player_level)))
            result->ambient_advice = MR_ADVICE_STAY_AT_INN;
        else if (bucket == 2 &&
                  player->health_current < player->health_max * 0.25f)
            result->ambient_advice = MR_ADVICE_GET_CURE;
        else if (bucket == 3 &&
                  player->dungeon_level > player->player_level * 2.0f + 2.0f)
            result->ambient_advice = MR_ADVICE_DUNGEON_TOO_DEEP;
        else if (bucket == 4 && player->carried_treasure > 0.0f)
            result->ambient_advice = MR_ADVICE_CASH_IN_TREASURE;
    }
}

int mr_session_is_at_fountain(const MRSession *session) {
    if (!session || session->phase != MR_SESSION_EXPLORING) return 0;
    const MRCharacter *character = &session->save.character;
    return mr_session_int(character->dungeon_level) == 70 &&
           mr_session_int(character->player_x) ==
               mr_session_int(character->persistent_values[
                   MR_PERSISTENT_FOUNTAIN_X]) &&
           mr_session_int(character->player_y) ==
               mr_session_int(character->persistent_values[
                   MR_PERSISTENT_FOUNTAIN_Y]);
}

int mr_session_drink_fountain(MRSession *session,
                              MRFountainResult *result) {
    if (!mr_session_is_at_fountain(session)) return 0;
    if (!mr_drink_fountain_of_youth(
            &session->save, &session->rng,
            &session->pending_save_snapshot, result))
        return 0;
    session->pending_save_snapshot_valid = 1;
    session->phase = MR_SESSION_TOWN;
    session->primary_tracking_global_index = 0;
    session->secondary_tracking_global_index = 0;
    session->active_monster_pursuit_target = 0;
    session->combat_global_index = 0;
    mr_session_rebuild_occupancy(session);
    mr_session_reveal_current_cell(session);
    return 1;
}

int mr_session_take_save_snapshot(MRSession *session, MRSave *save) {
    if (!session || !save || !session->pending_save_snapshot_valid) return 0;
    *save = session->pending_save_snapshot;
    session->pending_save_snapshot_valid = 0;
    return 1;
}

static void mr_session_destination(int x, int y, MRDirection direction,
                                   int *next_x, int *next_y) {
    *next_x = x;
    *next_y = y;
    if (direction == MR_DIRECTION_NORTH) (*next_y)--;
    else if (direction == MR_DIRECTION_EAST) (*next_x)++;
    else if (direction == MR_DIRECTION_SOUTH) (*next_y)++;
    else if (direction == MR_DIRECTION_WEST) (*next_x)--;
}

MRSessionStepResult mr_session_arrow_with_cycle(
    MRSession *session, MRArrowKey key, MRSessionCycleResult *cycle) {
    MRSessionCycleResult local_cycle;
    if (!cycle) cycle = &local_cycle;
    memset(cycle, 0, sizeof(*cycle));
    cycle->disease_attribute = -1;
    if (!session || (session->phase != MR_SESSION_EXPLORING &&
                     session->phase != MR_SESSION_TOWN &&
                     session->phase != MR_SESSION_COMBAT))
        return MR_SESSION_STEP_IGNORED;
    const int was_combat = session->phase == MR_SESSION_COMBAT;
    MRInputResult input = mr_input_dispatch_arrow(&session->input, key);
    if (input.action == MR_INPUT_TURN) {
        /* Relative Left/Right/Down reaches 0627, which sets B4C2=1 before
         * drawing. Disease advances, but a health ring cannot heal. */
        mr_session_apply_status_cycle(session, cycle, 0);
        return MR_SESSION_STEP_TURNED;
    }
    if (input.action != MR_INPUT_MOVE) return MR_SESSION_STEP_IGNORED;

    int level = mr_session_int(session->save.character.dungeon_level);
    int x = mr_session_int(session->save.character.player_x);
    int y = mr_session_int(session->save.character.player_y);
    int next_x, next_y;
    mr_session_destination(x, y, input.direction, &next_x, &next_y);
    /* 30D9-33EA ANDs the destination-occupancy test with B50E=1.  B50E is
     * 50 during ordinary exploration and 1 in combat, not the reverse.
     * Exploration therefore permits stepping onto a monster; the outer
     * 08F6/49B1 occupancy check starts the encounter on the next controller
     * pass.  During combat, another occupied destination prints MONSTER
     * BLOCKS WAY while an empty step can begin the 8E76 separation logic. */
    int monster = (was_combat && session->phase != MR_SESSION_TOWN)
        ? mr_session_monster_at(session, next_x, next_y) : 0;
    if (monster) {
        return MR_SESSION_STEP_MONSTER;
    }

    MRMoveResult movement = mr_world_try_move(
        level, (float)session->save.character.dungeon_generation_seed,
        input.direction, &x, &y);
    if (movement == MR_MOVE_WALL) {
        /* 315A/3216/32D8/3396 assign B4C2=1 before 3FFC. */
        mr_session_apply_status_cycle(session, cycle, 0);
        return MR_SESSION_STEP_WALL;
    }
    if (movement == MR_MOVE_BOUNDARY) {
        /* The coordinate-edge arms fall through 33CB without setting B4C2,
         * so the completed prior redraw's zero permits ring healing. */
        mr_session_apply_status_cycle(session, cycle, 1);
        return MR_SESSION_STEP_BOUNDARY;
    }
    session->save.character.player_x = x;
    session->save.character.player_y = y;
    mr_input_record_successful_step(&session->input);
    mr_expire_battle_spell_effects_on_movement(
        &session->save.character, &session->input.movement_turn);
    /* The combat command loop calls the ordinary arrow handler first
     * (DUNSMALL 871F), then compares the monster and player coordinates at
     * 8752-877E.  Do not end combat here: 8E76-8F67 can make the monster
     * pursue and sometimes strike during the attempted separation. */
    mr_session_reveal_current_cell(session);

    /* 33E6 calls 3FFC before its 09E8 tail can advance tracked monsters or
     * inspect another command.  Preserve that disease/RNG ordering. */
    mr_session_apply_status_cycle(session, cycle, 1);

    int feature = mr_world_resolve_vertical_feature(
        &session->world, level, x, y);
    /* Chutes only exist on positive dungeon floors. Town-side reciprocal
     * endpoints resolve as ordinary positive/down ladders and require D. */
    if (feature == MR_VERTICAL_CHUTE && level > 0 &&
        level < MR_DUNGEON_LEVELS) {
        /* 3428 first suppresses the exact landing triple written by the
         * previous fall. For every reachable positive x/y, the apparent
         * relocation branch at 34A0 is dead because INT((x+y)*.5) != 0. */
        if (x != session->last_chute_landing_x ||
            y != session->last_chute_landing_y ||
            level != session->last_chute_landing_level) {
            /* 348D saves before 3490 increments the level. */
            session->pending_save_snapshot = session->save;
            session->pending_save_snapshot_valid = 1;
            session->save.character.dungeon_level = level + 1;
            session->last_chute_landing_x = x;
            session->last_chute_landing_y = y;
            session->last_chute_landing_level = level + 1;
            mr_session_rebuild_occupancy(session);
            mr_session_reveal_current_cell(session);
            /* 8739 observes the floor-transition flag set by the chute and
             * exits through 8F70 before the separation/pursuit calculation. */
            if (was_combat) mr_session_leave_combat(session);
            return MR_SESSION_STEP_CHUTE;
        }
    }

    /* 49B1 transfers directly to combat when the player deliberately steps
     * onto an exploration monster. The redraw cycle above already belongs
     * to that combat entry and must not be replayed by the host loop. */
    if (!was_combat && session->phase != MR_SESSION_TOWN &&
        mr_session_monster_at(session, x, y) > 0)
        session->combat_entry_redraw_already_applied = 1;
    /* process_arrow_input_and_expire_turn_effects 09E8 calls 0C0B only when
     * the pre-command view depth is not the combat value one.  A combat
     * escape therefore must not make the generic movement tail advance
     * tracked monsters retroactively. */
    if (!was_combat && session->phase != MR_SESSION_TOWN)
        mr_session_update_active_monsters(session, NULL);
    return MR_SESSION_STEP_MOVED;
}

MRSessionStepResult mr_session_arrow(MRSession *session, MRArrowKey key) {
    return mr_session_arrow_with_cycle(session, key, NULL);
}

int mr_session_resolve_combat_separation(
    MRSession *session, MRCombatSeparationResult *result) {
    MRCombatSeparationResult local;
    if (!result) result = &local;
    memset(result, 0, sizeof(*result));
    if (!session || session->phase != MR_SESSION_COMBAT ||
        session->combat_global_index < 1)
        return 0;

    int monster_position =
        session->world.monster_positions[session->combat_global_index];
    int monster_x = mr_monster_position_x(monster_position);
    int monster_y = mr_monster_position_y(monster_position);
    int player_x = mr_session_int(session->save.character.player_x);
    int player_y = mr_session_int(session->save.character.player_y);
    if (monster_x == player_x && monster_y == player_y)
        return 0;

    /* DUNSMALL 8E76-8F67. Reaching the level-70 fountain always breaks
     * contact. A behavior-three monster with a positive carried damage roll
     * skips directly to pursuit; all others use an exact three-RND armor
     * gate, followed by a fourth invisibility RND only when armor failed to
     * break contact. DS:1FA6 is BASIC attribute five (Agility): the array's
     * unused element zero occupies DS:1F92. */
    int level = mr_session_int(session->save.character.dungeon_level);
    int fountain_x = mr_session_int(session->save.character.persistent_values[
        MR_PERSISTENT_FOUNTAIN_X]);
    int fountain_y = mr_session_int(session->save.character.persistent_values[
        MR_PERSISTENT_FOUNTAIN_Y]);
    if (level == MR_DUNGEON_LEVELS && player_x == fountain_x &&
        player_y == fountain_y) {
        mr_session_leave_combat(session);
        result->code = MR_COMBAT_SEPARATION_ESCAPED;
        return 1;
    }

    int pursue = session->combat_profile.behavior_class == 3 &&
                   session->monster_attack_state.damage > 0.0f;
    if (!pursue) {
        int three_way = mr_session_int(
            (float)mr_rng_next(&session->rng) * 3.0f);
        if (three_way == 1) {
            mr_session_leave_combat(session);
            result->code = MR_COMBAT_SEPARATION_ESCAPED;
            return 1;
        }
        float pursuit_random = (float)mr_rng_next(&session->rng);
        pursuit_random *= (float)mr_rng_next(&session->rng);
        pursuit_random *= 100.0f;
        int pursuit_score = mr_session_int(pursuit_random);
        pursuit_score += session->combat_profile.turn_speed_bonus - 11;
        session->shared_menu_value = pursuit_score;
        float escape_defense = session->save.character.equipped_armor *
                               session->save.character.attributes[4];
        if (pursuit_score < escape_defense) {
            mr_session_leave_combat(session);
            result->code = MR_COMBAT_SEPARATION_ESCAPED;
            return 1;
        }
        int invisible = mr_session_int(
            session->save.character.active_effects[MR_EFFECT_INVISIBILITY]) ==
            1;
        /* 8F2F jumps around 8F32-8F62 when armor already won. The compiled
         * program therefore does not consume this RND in that branch. */
        int invisibility_roll =
            mr_session_int((float)mr_rng_next(&session->rng) * 5.0f);
        if (invisible && invisibility_roll < 2) {
            mr_session_leave_combat(session);
            result->code = MR_COMBAT_SEPARATION_ESCAPED;
            return 1;
        }
    }

    /* 7E8D moves the active combat monster through the ordinary 70A1
     * routine. In combat depth that routine consumes its eager weight RND,
     * then targets the player's new cell directly and ignores the wall. */
    mr_session_advance_active_monster(
        session, session->combat_global_index, monster_y, &result->advance);

    int no_attack_roll = mr_session_int(
        (float)mr_rng_next(&session->rng) * 2.0f);
    int followup_score =
        mr_session_int((float)mr_rng_next(&session->rng) * 50.0f) - 5;
    float followup_defense = session->save.character.equipped_armor *
                             session->save.character.attributes[4];
    if (no_attack_roll == 1 || followup_score < followup_defense) {
        result->code = MR_COMBAT_SEPARATION_REENGAGED;
        return 1;
    }

    if (mr_session_monster_turn(session, &result->monster)) {
        result->code = MR_COMBAT_SEPARATION_REENGAGED_AND_ATTACKED;
        return 1;
    }
    result->code = MR_COMBAT_SEPARATION_REENGAGED;
    return 1;
}

int mr_session_traverse(MRSession *session, int go_up) {
    if (!session || (session->phase != MR_SESSION_EXPLORING &&
                     session->phase != MR_SESSION_TOWN))
        return 0;
    int level = mr_session_int(session->save.character.dungeon_level);
    int x = mr_session_int(session->save.character.player_x);
    int y = mr_session_int(session->save.character.player_y);
    int feature = mr_world_resolve_vertical_feature(&session->world, level,
                                                    x, y);
    int target = level;
    if (go_up && feature < 0) target += feature;
    else if (!go_up && feature > 0 && feature <= 3) target += feature;
    else
        return 0;
    /* finish_dungeon_level_transition tail-calls 4C28, which clamps the
     * resulting level into 0..70 before rebuilding the floor. It does not
     * reject an otherwise valid 1..3-span ladder whose arithmetic crosses
     * an endpoint. */
    if (target < 0) target = 0;
    if (target > MR_DUNGEON_LEVELS) target = MR_DUNGEON_LEVELS;
    session->save.character.dungeon_level = (float)target;
    if (target == 0)
        mr_recompute_town_entry_factors(&session->save.character);
    session->phase = target == 0 ? MR_SESSION_TOWN : MR_SESSION_EXPLORING;
    mr_session_rebuild_occupancy(session);
    mr_session_reveal_current_cell(session);
    return 1;
}

int mr_session_begin_combat(MRSession *session, int global_index) {
    if (!session || global_index < 1 || global_index > 2800 ||
        session->world.monster_health[global_index] <= 0)
        return 0;
    int level = mr_session_int(session->save.character.dungeon_level);
    if (level < 1 || level > MR_DUNGEON_LEVELS) return 0;
    int first = mr_monster_index(level, 1);
    int last = mr_monster_index(level, MR_MONSTER_SLOTS_PER_LEVEL);
    if (global_index < first || global_index > last) return 0;
    /* DUNSMALL 803D-8043 seeds DS:B6A4 to 100 before it resolves the
     * occupant at the player's cell.  70A1 may replace that value on the
     * first combat movement update, but entry itself must not inherit the
     * previous encounter's pursuit state. */
    session->active_monster_pursuit_target = 100;
    session->combat_global_index = global_index;
    session->combat_floor_slot = global_index - first + 1;
    session->combat_health = session->world.monster_health[global_index];
    int type = mr_monster_type(global_index, level,
                               session->world.monster_health[global_index]);
    int monster_level = mr_monster_level(global_index);
    mr_monster_combat_profile(mr_monster_uses_deep_resources(level) ? 2 : 1,
                              type, monster_level,
                              &session->combat_profile);
    /* B6CC (wand cannot-strike turns), B6CE (one-shot damage), and B706
     * (shield threshold) are program globals. They are not encounter locals
     * and therefore survive walking away from one monster and meeting the
     * next. Only the ordinary strike accumulators are reset here. */
    int cannot_strike = session->monster_attack_state.cannot_strike_counter;
    float shield = session->monster_attack_state.shield_defense_bonus;
    int one_shot = session->player_attack_state.one_shot_damage_bonus;
    memset(&session->monster_attack_state, 0,
           sizeof(session->monster_attack_state));
    memset(&session->player_attack_state, 0,
           sizeof(session->player_attack_state));
    session->monster_attack_state.cannot_strike_counter = cannot_strike;
    session->monster_attack_state.shield_defense_bonus = shield;
    session->player_attack_state.one_shot_damage_bonus = one_shot;
    memset(&session->pending_coins, 0, sizeof(session->pending_coins));
    memset(&session->pending_rewards, 0, sizeof(session->pending_rewards));
    session->coin_loot_pending = 0;
    session->coin_loot_resolved = 0;
    session->post_combat_rewards_generated = 0;
    session->suppress_defeat_message = 0;
    session->combat_initialized_flag = 1;
    session->phase = MR_SESSION_COMBAT;
    return 1;
}

static int mr_session_respawn_defeated_monster(MRSession *session) {
    if (!session || session->combat_global_index < 1) return 0;
    int index = session->combat_global_index;
    int old_position = session->world.monster_positions[index];
    int old_x = mr_monster_position_x(old_position);
    int old_y = mr_monster_position_y(old_position);
    int level = mr_session_int(session->save.character.dungeon_level);

    /* A3A7-A3C7 clears the defeated monster's old (normally player) cell
     * before A3C8 rolls health and A408 begins rerolling coordinates.  That
     * order is observable: the replacement is allowed to reuse the square
     * on which it was defeated. */
    if (old_x >= 1 && old_x <= 20 && old_y >= 1 && old_y <= 19)
        session->occupancy[old_y][old_x] = 0;
    MRMonsterRespawnRoll roll;
    float health_random = mr_rng_next(&session->rng);
    do {
        /* C does not define argument-evaluation order.  A408 rolls X first
         * and A425 rolls Y second, so do not place both stateful calls in
         * one argument list. */
        float x_random = mr_rng_next(&session->rng);
        float y_random = mr_rng_next(&session->rng);
        mr_monster_respawn_roll(level, health_random, x_random, y_random,
                                &roll);
    } while (mr_session_monster_at(session, roll.x, roll.y));
    session->world.monster_positions[index] =
        (uint16_t)roll.packed_position;
    session->world.monster_health[index] = (int16_t)roll.health;
    session->occupancy[roll.y][roll.x] = index;
    session->combat_health = 0.0f;
    return 1;
}

int mr_session_generate_post_combat_rewards_staged(
    MRSession *session, MRPostCombatRewardStageCallback stage_callback,
    void *stage_context) {
    if (!session || session->phase != MR_SESSION_REWARDS ||
        !session->coin_loot_resolved)
        return 0;
    if (session->post_combat_rewards_generated) return 1;
    if (!mr_generate_post_combat_rewards_rng_staged(
            &session->save.character,
            session->combat_profile.behavior_class, &session->rng,
            &session->pending_rewards, stage_callback, stage_context))
        return 0;
    session->post_combat_rewards_generated = 1;
    return 1;
}

static void mr_session_leave_combat(MRSession *session) {
    if (!session) return;
    /* This marker belongs only to the immediate movement-to-combat transfer.
     * Never let a later idle encounter inherit a skipped 3FFC entry cycle. */
    session->combat_entry_redraw_already_applied = 0;
    session->combat_global_index = 0;
    session->combat_floor_slot = 0;
    session->combat_health = 0.0;
    session->coin_loot_pending = 0;
    session->coin_loot_resolved = 0;
    session->post_combat_rewards_generated = 0;
    session->suppress_defeat_message = 0;
    memset(&session->pending_coins, 0, sizeof(session->pending_coins));
    memset(&session->pending_rewards, 0, sizeof(session->pending_rewards));
    session->phase = session->save.character.dungeon_level == 0.0f
        ? MR_SESSION_TOWN : MR_SESSION_EXPLORING;
    mr_session_rebuild_occupancy(session);
    mr_session_reveal_current_cell(session);
}

static int mr_session_rewarded_defeat(MRSession *session,
                                      int suppress_message) {
    if (!session || session->combat_global_index < 1) return 0;
    session->world.monster_health[session->combat_global_index] = 0;
    session->save.character.pending_experience +=
        session->combat_profile.experience_reward;
    if (!mr_session_respawn_defeated_monster(session)) return 0;
    /* Successful GO AWAY sets B726 to one. A4CA suppresses only the text;
     * A4F0 immediately clears the flag and still waits for Return before the
     * eager loot-gate RND. Keep that input boundary even without the words. */
    session->suppress_defeat_message = suppress_message != 0;
    session->phase = MR_SESSION_DEFEAT_NOTICE;
    return 1;
}

static int mr_session_monster_turn(MRSession *session,
                                   MRMonsterTurnResult *result) {
    if (!session || session->phase != MR_SESSION_COMBAT) return 0;
    MRMonsterTurnResult local;
    if (!result) result = &local;
    /* DUNSMALL stores menu selections and both sides' attack rolls in the
     * same MBF single at DS:52FC.  The combat formula deliberately starts
     * with whatever the preceding command left there; it is not encounter-
     * local state.  Keep the formula helper synchronized with that session
     * slot before every monster response. */
    session->monster_attack_state.shared_roll = session->shared_menu_value;
    int attacked = mr_monster_take_turn_rng(
        &session->save.character, &session->combat_profile,
        &session->monster_attack_state, &session->rng, result);
    session->shared_menu_value = session->monster_attack_state.shared_roll;
    /* The class-five drain path calls SAVE at 9E92 after damage, experience,
     * level, and maximum-health changes, but before the deep-set Strength
     * drain at 9F11.  RND*10 is nonnegative, so the compiler's FC0A/JAE gate
     * reaches SAVE for every level drain.  If a monster repeats its strike,
     * the latest boundary includes the first drain and excludes only the
     * latest post-SAVE Strength decrement. */
    int last_level_drain = -1;
    for (int strike = 0; attacked && strike < result->strike_count; ++strike)
        if (result->strikes[strike].level_drained)
            last_level_drain = strike;
    if (last_level_drain >= 0) {
        session->pending_save_snapshot = session->save;
        if (result->strikes[last_level_drain].strength_drained)
            session->pending_save_snapshot.character.attributes[0] += 1.0f;
        session->pending_save_snapshot_valid = 1;
    }
    if (attacked && result->player_dead) session->phase = MR_SESSION_DEAD;
    return attacked;
}

static void mr_session_item_combat_state(const MRSession *session,
                                         MRItemCombatState *combat) {
    memset(combat, 0, sizeof(*combat));
    combat->movement_turn = (int)session->input.movement_turn;
    combat->monster_level = session->combat_profile.level;
    combat->monster_health = session->combat_health;
    combat->monster_cannot_strike_counter =
        session->monster_attack_state.cannot_strike_counter;
    combat->one_shot_damage_bonus =
        session->player_attack_state.one_shot_damage_bonus;
    combat->shield_defense_value =
        (int)session->monster_attack_state.shield_defense_bonus;
}

static void mr_session_commit_item_combat_state(
    MRSession *session, const MRItemCombatState *combat) {
    session->combat_health = combat->monster_health;
    session->monster_attack_state.cannot_strike_counter =
        combat->monster_cannot_strike_counter;
    session->player_attack_state.one_shot_damage_bonus =
        combat->one_shot_damage_bonus;
    session->monster_attack_state.shield_defense_bonus =
        combat->shield_defense_value;
    if (session->combat_global_index > 0)
        session->world.monster_health[session->combat_global_index] =
            (int16_t)session->combat_health;
}

int mr_session_player_attack_only(MRSession *session,
                                  MRPlayerAttackCode attack,
                                  MRSessionCombatResult *result) {
    if (!session || !result || session->phase != MR_SESSION_COMBAT) return 0;
    memset(result, 0, sizeof(*result));
    result->player_attacked = mr_player_attack_rng(
        &session->save.character, &session->combat_profile,
        session->save.character.dungeon_level, attack,
        &session->combat_health, &session->player_attack_state,
        &session->rng, &result->player);
    if (!result->player_attacked) return 0;
    if (result->player.last_d20_roll)
        session->shared_menu_value = result->player.last_d20_roll;
    session->world.monster_health[session->combat_global_index] =
        (int16_t)session->combat_health;
    if (result->player.monster_defeated) {
        result->monster_defeated = 1;
        if (!mr_session_rewarded_defeat(session, 0)) return 0;
        return 1;
    }
    return 1;
}

int mr_session_resolve_monster_turn(MRSession *session,
                                    MRMonsterTurnResult *result) {
    return mr_session_monster_turn(session, result);
}

int mr_session_player_attack(MRSession *session, MRPlayerAttackCode attack,
                             MRSessionCombatResult *result) {
    if (!mr_session_player_attack_only(session, attack, result)) return 0;
    if (session->phase == MR_SESSION_COMBAT &&
        result->player.monster_takes_turn) {
        result->monster_attacked =
            mr_session_monster_turn(session, &result->monster);
        result->player_dead = result->monster.player_dead;
    }
    return 1;
}

static void mr_session_apply_noncombat_transition(MRSession *session,
                                                  int town_transition) {
    /* Every DUNSMALL 0CC0 transition calls 4C28 first. That routine clamps
     * the playable dungeon to 0..70; 0CC0 then calls 1CF5 whenever the
     * resulting level is zero. Teleport's compiled .5 is the lone explicit
     * town-transition intermediate and is likewise replaced by 1CF5. */
    if (session->save.character.dungeon_level > MR_DUNGEON_LEVELS)
        session->save.character.dungeon_level = MR_DUNGEON_LEVELS;
    if (session->save.character.dungeon_level < 0.0f) {
        session->save.character.dungeon_level = 0.0f;
        town_transition = 1;
    }
    if (town_transition || session->save.character.dungeon_level == 0.0f)
        mr_recompute_town_entry_factors(&session->save.character);
    session->phase = session->save.character.dungeon_level == 0.0f
        ? MR_SESSION_TOWN : MR_SESSION_EXPLORING;
    mr_session_rebuild_occupancy(session);
    mr_session_reveal_current_cell(session);
    session->player_x_at_last_monster_update =
        mr_session_int(session->save.character.player_x);
    session->player_y_at_last_monster_update =
        mr_session_int(session->save.character.player_y);
}

int mr_session_cast_preparation_spell(MRSession *session,
                                      MRPreparationSpell spell,
                                      MRPreparationCastResult *result) {
    if (!session || !result || (session->phase != MR_SESSION_EXPLORING &&
                                session->phase != MR_SESSION_TOWN))
        return 0;
    /* 366F-368B replaces the selector's 1/2 choice in DS:52FC with the
     * one-based twelve-spell dispatch value before entering the handler. */
    session->shared_menu_value = (float)spell + 1.0f;
    if (!mr_cast_preparation_spell_rng(&session->save.character, spell,
                                       &session->rng, result))
        return 0;
    if (result->dungeon_transition || result->town_transition)
        mr_session_apply_noncombat_transition(session,
                                              result->town_transition);
    return 1;
}

int mr_session_cast_battle_spell(MRSession *session, MRBattleSpell spell,
                                 MRSessionBattleSpellResult *result) {
    if (!session || !result || session->phase != MR_SESSION_COMBAT) return 0;
    memset(result, 0, sizeof(*result));
    /* 9187-91A6 performs the same one-based dispatch rewrite.  A spell that
     * grants a monster response therefore feeds this value into 9CF3. */
    session->shared_menu_value = (float)spell + 1.0f;
    if (!mr_cast_battle_spell_rng(
            &session->save.character, spell,
            (int)session->input.movement_turn, session->combat_profile.level,
            &session->combat_health, &session->rng, &result->spell))
        return 0;
    session->world.monster_health[session->combat_global_index] =
        (int16_t)session->combat_health;
    if (result->spell.rewarded_defeat) {
        return mr_session_rewarded_defeat(
            session, result->spell.suppress_kill_message);
    }
    if (result->spell.left_combat) {
        if (result->spell.town_transition)
            mr_recompute_town_entry_factors(&session->save.character);
        mr_session_leave_combat(session);
        return 1;
    }
    if (result->spell.monster_takes_turn) {
        result->monster_attacked =
            mr_session_monster_turn(session, &result->monster);
        result->player_dead = result->monster.player_dead;
    }
    return 1;
}

int mr_session_use_preparation_item(MRSession *session,
                                    MRPreparationItem item,
                                    MRItemUseResult *result) {
    if (!session || !result || (session->phase != MR_SESSION_EXPLORING &&
                                session->phase != MR_SESSION_TOWN))
        return 0;
    /* 14A0-14E8 dispatches directly from the one-based DS:52FC choice. */
    session->shared_menu_value = (float)item + 1.0f;
    if (!mr_use_preparation_item(&session->save.character, item,
                                 session->save.explored, result))
        return 0;
    if (result->dungeon_transition || result->town_transition)
        mr_session_apply_noncombat_transition(session,
                                              result->town_transition);
    return 1;
}

int mr_session_use_battle_item(MRSession *session, MRBattleItem item,
                               float timer_seconds,
                               MRSessionItemResult *result) {
    if (!session || !result || session->phase != MR_SESSION_COMBAT) return 0;
    memset(result, 0, sizeof(*result));
    /* 96D9-98E7 likewise leaves its one-based item selection in DS:52FC. */
    session->shared_menu_value = (float)item + 1.0f;
    MRItemCombatState combat;
    mr_session_item_combat_state(session, &combat);
    if (!mr_use_battle_item_rng(&session->save.character, item, timer_seconds,
                                &combat, &session->rng, &result->item))
        return 0;
    mr_session_commit_item_combat_state(session, &combat);
    if (result->item.rewarded_defeat)
        return mr_session_rewarded_defeat(session, 0);
    if (result->item.left_combat) {
        if (result->item.town_transition)
            mr_recompute_town_entry_factors(&session->save.character);
        mr_session_leave_combat(session);
    }
    return 1;
}

int mr_session_use_wand(MRSession *session, MRWand wand,
                        MRSessionItemResult *result) {
    if (!session || !result || (session->phase != MR_SESSION_EXPLORING &&
                                session->phase != MR_SESSION_TOWN &&
                                session->phase != MR_SESSION_COMBAT))
        return 0;
    memset(result, 0, sizeof(*result));
    /* choose_wand stores the one-based color in DS:52FC before its charge
     * test and ON GOSUB dispatch. */
    session->shared_menu_value = (float)wand + 1.0f;
    MRItemContext context = session->phase == MR_SESSION_COMBAT
        ? MR_ITEM_CONTEXT_COMBAT : MR_ITEM_CONTEXT_EXPLORATION;
    MRItemCombatState combat;
    /* White/Orange wands mutate the same global counters even in the dungeon
     * command loop (7C3C-7C48), where they prime the next encounter. */
    mr_session_item_combat_state(session, &combat);
    if (!mr_use_wand_rng(&session->save.character, wand, context,
                         &combat, &session->rng, &result->item))
        return 0;
    mr_session_commit_item_combat_state(session, &combat);
    if (context != MR_ITEM_CONTEXT_COMBAT) {
        if (result->item.dungeon_transition)
            mr_session_apply_noncombat_transition(
                session, result->item.town_transition);
        else if (result->item.used)
            mr_session_reveal_current_cell(session);
        return 1;
    }

    if (result->item.rewarded_defeat)
        return mr_session_rewarded_defeat(session, 0);
    if (result->item.left_combat) {
        if (result->item.town_transition)
            mr_recompute_town_entry_factors(&session->save.character);
        mr_session_leave_combat(session);
        return 1;
    }
    if (result->item.monster_takes_turn) {
        result->monster_attacked =
            mr_session_monster_turn(session, &result->monster);
        result->player_dead = result->monster.player_dead;
    }
    return 1;
}

int mr_session_use_pill(MRSession *session, MRPill pill,
                        MRItemUseResult *result) {
    if (!session || !result || (session->phase != MR_SESSION_EXPLORING &&
                                session->phase != MR_SESSION_TOWN &&
                                session->phase != MR_SESSION_COMBAT))
        return 0;
    session->shared_menu_value = (float)pill + 1.0f;
    int used = mr_use_pill(&session->save.character, pill, result);
    /* 7DA0 computes 7-choice back into DS:52FC before using that reversed
     * index for the pill's +4 attribute.  Preserve the transformed value
     * only when the count test allowed the pill effect to run. */
    if (used && result->used)
        session->shared_menu_value = 6.0f - (float)pill;
    return used;
}

void mr_session_expire_combat_effects(MRSession *session,
                                      float timer_seconds,
                                      MRTimedEffectResult *result) {
    if (!session || session->phase != MR_SESSION_COMBAT) {
        if (result) memset(result, 0, sizeof(*result));
        return;
    }
    MRItemCombatState combat;
    mr_session_item_combat_state(session, &combat);
    mr_expire_timed_combat_effects(&session->save.character, timer_seconds,
                                   &combat, result);
    mr_session_commit_item_combat_state(session, &combat);
}

static int mr_session_has_coin_loot(const MRCoinLoot *coins) {
    /* DUNSMALL A886-A88D tests the already-computed treasure value at
     * DS:B770, not the individual coin counts.  Copper-only rolls below 100
     * can therefore exist but intentionally skip the take/leave display. */
    return coins && coins->treasure_value > 0;
}

int mr_session_acknowledge_defeat(MRSession *session) {
    if (!session || session->phase != MR_SESSION_DEFEAT_NOTICE ||
        session->combat_global_index < 1)
        return 0;

    /* A528-A55E always consumes the random roll. The compiled predicate is
     * `even-index flag = 1 OR INT(RND*5) <> 1`; the earlier native draft had
     * incorrectly inverted the odd-index comparison. Failure skips not only
     * coins but the entire post-combat reward sequence. */
    float gate_random = mr_rng_next(&session->rng);
    if (!mr_monster_coin_loot_gate(session->combat_global_index,
                                   gate_random)) {
        mr_session_leave_combat(session);
        return 1;
    }

    mr_generate_coin_loot_rng(session->save.character.dungeon_level,
                              session->combat_profile.level,
                              &session->rng, &session->pending_coins);
    session->coin_loot_pending =
        mr_session_has_coin_loot(&session->pending_coins);
    session->phase = MR_SESSION_REWARDS;
    if (!session->coin_loot_pending) {
        session->coin_loot_resolved = 1;
    }
    return 1;
}

MRCoinLootResolution mr_session_resolve_coin_loot(MRSession *session,
                                                  int take_coins) {
    if (!session || session->phase != MR_SESSION_REWARDS ||
        !session->coin_loot_pending || session->coin_loot_resolved)
        return MR_COIN_LOOT_NOT_PENDING;

    MRCoinLootResolution resolution = MR_COIN_LOOT_DECLINED;
    /* A956-A982 reuses DS:52FC rather than the calculated coin weight in this
     * pre-prompt check. At or above 350 the original prints the too-heavy
     * message and offers no T/L prompt. */
    if (session->save.character.carried_weight +
            session->shared_menu_value >= 350.0f) {
        resolution = MR_COIN_LOOT_TOO_HEAVY;
    } else if (take_coins) {
        session->save.character.carried_weight +=
            (float)session->pending_coins.carry_weight;
        session->save.character.carried_treasure +=
            (float)session->pending_coins.treasure_value;
        resolution = MR_COIN_LOOT_TAKEN;
    }
    session->coin_loot_resolved = 1;
    return resolution;
}

int mr_session_finish_rewards(MRSession *session) {
    if (!session || session->phase != MR_SESSION_REWARDS ||
        session->combat_global_index < 1 || !session->coin_loot_resolved)
        return 0;
    /* Headless callers still receive the same state transition.  The live
     * frontend invokes the staged form first so its input pauses occur
     * between the original RND groups. */
    if (!session->post_combat_rewards_generated &&
        !mr_session_generate_post_combat_rewards_staged(session, NULL, NULL))
        return 0;
    mr_session_leave_combat(session);
    return 1;
}

MRDeathOutcome mr_session_resolve_player_death(MRSession *session) {
    if (!session || session->phase != MR_SESSION_DEAD)
        return MR_DEATH_PERMANENT;
    MRDeathOutcome outcome =
        mr_resolve_death(&session->save.character, &session->rng);
    if (outcome == MR_DEATH_REINCARNATED || outcome == MR_DEATH_RAISED) {
        /* DUNSMALL A12D-A15C and A15D-A22A both return directly to the
         * ordinary command loop at town coordinates (14,12).  The death
         * resolver has already removed its battle Strength/Speed marker, so
         * the shared combat cleanup cannot subtract either bonus twice. */
        mr_session_leave_combat(session);
        session->primary_tracking_global_index = 0;
        session->secondary_tracking_global_index = 0;
        session->active_monster_pursuit_target = 0;
        session->player_x_at_last_monster_update =
            mr_session_int(session->save.character.player_x);
        session->player_y_at_last_monster_update =
            mr_session_int(session->save.character.player_y);
    }
    return outcome;
}

int mr_session_self_test(const char *resource_directory,
                         char *error, size_t error_size) {
    char text[512], binary[512];
    mr_session_join(text, sizeof(text), resource_directory, "1.EXE");
    mr_session_join(binary, sizeof(binary), resource_directory, "1.BIN");
    MRSession session;
    if (!mr_session_load(&session, resource_directory, text, binary,
                         0x050000U, error, error_size))
        return 0;
    int failed = 0;
    failed |= session.phase != MR_SESSION_TOWN ||
              session.primary_color != 0 || session.secondary_color != 2 ||
              session.effective_background_color != 0 ||
              session.effective_palette_selector != 2;

    /* B98F-B9B2 runs after the startup review, not inside the generic file
     * decoder. Pin both RND consumption and the transient-timer reset so a
     * future refactor cannot silently move this boundary earlier. */
    {
        MRSession load_epilogue = session;
        load_epilogue.rng.state = 0x050000U;
        load_epilogue.save.character.persistent_values[
            MR_PERSISTENT_FOUNTAIN_X] = 0.0f;
        load_epilogue.save.character.persistent_values[
            MR_PERSISTENT_FOUNTAIN_Y] = 99.0f;
        load_epilogue.save.character.persistent_values[
            MR_PERSISTENT_BREATHE_FIRE_EXPIRY] = 101.0f;
        load_epilogue.save.character.persistent_values[
            MR_PERSISTENT_SHIELDING_EXPIRY] = 102.0f;
        load_epilogue.save.character.persistent_values[
            MR_PERSISTENT_AGILITY_EXPIRY] = 103.0f;
        MRRng expected_rng = load_epilogue.rng;
        float expected_x =
            (float)(mr_session_int(mr_rng_next(&expected_rng) * 15.0f) + 2);
        float expected_y =
            (float)(mr_session_int(mr_rng_next(&expected_rng) * 15.0f) + 2);
        mr_session_apply_original_load_epilogue(&load_epilogue);
        failed |= load_epilogue.save.character.persistent_values[
                      MR_PERSISTENT_FOUNTAIN_X] != expected_x ||
                  load_epilogue.save.character.persistent_values[
                      MR_PERSISTENT_FOUNTAIN_Y] != expected_y ||
                  load_epilogue.rng.state != expected_rng.state ||
                  load_epilogue.save.character.persistent_values[
                      MR_PERSISTENT_BREATHE_FIRE_EXPIRY] != 0.0f ||
                  load_epilogue.save.character.persistent_values[
                      MR_PERSISTENT_SHIELDING_EXPIRY] != 0.0f ||
                  load_epilogue.save.character.persistent_values[
                      MR_PERSISTENT_AGILITY_EXPIRY] != 0.0f;
        uint32_t retained_state = load_epilogue.rng.state;
        mr_session_apply_original_load_epilogue(&load_epilogue);
        failed |= load_epilogue.rng.state != retained_state;
    }

    /* A868-A88D can produce a positive copper count whose integer treasure
     * value is zero.  The original skips the coin dialog in that case. */
    {
        MRCoinLoot coins;
        memset(&coins, 0, sizeof(coins));
        coins.copper = 99;
        coins.carry_weight = 6;
        coins.treasure_value = 0;
        failed |= mr_session_has_coin_loot(&coins);
        coins.treasure_value = 1;
        failed |= !mr_session_has_coin_loot(&coins);
    }

    /* A chute is not operated with U or D. DUNSMALL 3428 enters the fall
     * automatically after an ordinary successful step reaches its circular
     * map cell. Find one real, traversable chute from 7.NUM and exercise the
     * complete same-coordinate, next-floor, pre-increment-save transition. */
    {
        MRSession chute_session = session;
        int chute_level = 0, chute_x = 0, chute_y = 0;
        int start_x = 0, start_y = 0;
        MRArrowKey chute_key = 0;
        static const int dx[] = {0, 0, -1, 0, 1};
        static const int dy[] = {0, 1, 0, -1, 0};
        static const MRArrowKey keys[] = {
            0, MR_ARROW_UP, MR_ARROW_RIGHT, MR_ARROW_DOWN, MR_ARROW_LEFT
        };
        for (int level = 1; level < MR_DUNGEON_LEVELS && !chute_key;
             ++level) {
            for (int y = 1; y <= 19 && !chute_key; ++y) {
                for (int x = 1; x <= 20 && !chute_key; ++x) {
                    if (mr_world_resolve_vertical_feature(
                            &chute_session.world, level, x, y) !=
                        MR_VERTICAL_CHUTE)
                        continue;
                    for (int direction = MR_DIRECTION_NORTH;
                         direction <= MR_DIRECTION_WEST; ++direction) {
                        int candidate_x = x + dx[direction];
                        int candidate_y = y + dy[direction];
                        int moved_x = candidate_x;
                        int moved_y = candidate_y;
                        if (candidate_x < 1 || candidate_x > 20 ||
                            candidate_y < 1 || candidate_y > 19)
                            continue;
                        if (mr_world_try_move(
                                level,
                                (float)chute_session.save.character.
                                    dungeon_generation_seed,
                                (MRDirection)direction,
                                &moved_x, &moved_y) != MR_MOVE_OK ||
                            moved_x != x || moved_y != y)
                            continue;
                        chute_level = level;
                        chute_x = x;
                        chute_y = y;
                        start_x = candidate_x;
                        start_y = candidate_y;
                        chute_key = keys[direction];
                        break;
                    }
                }
            }
        }
        if (!chute_key) {
            failed = 1;
        } else {
            chute_session.save.character.dungeon_level = chute_level;
            chute_session.save.character.player_x = start_x;
            chute_session.save.character.player_y = start_y;
            chute_session.phase = MR_SESSION_EXPLORING;
            chute_session.input.movement_mode = MR_MOVEMENT_CARDINAL;
            chute_session.last_chute_landing_x = 0;
            chute_session.last_chute_landing_y = 0;
            chute_session.last_chute_landing_level = 0;
            chute_session.pending_save_snapshot_valid = 0;
            mr_session_rebuild_occupancy(&chute_session);
            chute_session.occupancy[chute_y][chute_x] = 0;
            failed |= mr_session_arrow(&chute_session, chute_key) !=
                          MR_SESSION_STEP_CHUTE ||
                      chute_session.save.character.dungeon_level !=
                          chute_level + 1 ||
                      chute_session.save.character.player_x != chute_x ||
                      chute_session.save.character.player_y != chute_y ||
                      !chute_session.pending_save_snapshot_valid ||
                      chute_session.pending_save_snapshot.character.
                              dungeon_level != chute_level;
        }

        /* A new character enters the dungeon by walking onto a filled-square
         * down ladder in town and pressing D. Verify both halves: walking
         * onto it does not auto-fall, and D follows its 1..3-floor span. */
        MRSession surface_session = session;
        int surface_x = 0, surface_y = 0;
        int surface_delta = 0;
        int surface_start_x = 0, surface_start_y = 0;
        MRArrowKey surface_key = 0;
        for (int y = 1; y <= 19 && !surface_key; ++y) {
            for (int x = 1; x <= 20 && !surface_key; ++x) {
                int feature = mr_world_resolve_vertical_feature(
                    &surface_session.world, 0, x, y);
                if (feature < 1 || feature > 3)
                    continue;
                for (int direction = MR_DIRECTION_NORTH;
                     direction <= MR_DIRECTION_WEST; ++direction) {
                    int candidate_x = x + dx[direction];
                    int candidate_y = y + dy[direction];
                    int moved_x = candidate_x;
                    int moved_y = candidate_y;
                    if (candidate_x < 1 || candidate_x > 20 ||
                        candidate_y < 1 || candidate_y > 19)
                        continue;
                    if (mr_world_try_move(
                            0,
                            (float)surface_session.save.character.
                                dungeon_generation_seed,
                            (MRDirection)direction, &moved_x, &moved_y) !=
                            MR_MOVE_OK ||
                        moved_x != x || moved_y != y)
                        continue;
                    surface_x = x;
                    surface_y = y;
                    surface_delta = feature;
                    surface_start_x = candidate_x;
                    surface_start_y = candidate_y;
                    surface_key = keys[direction];
                    break;
                }
            }
        }
        if (!surface_key) {
            failed = 1;
        } else {
            surface_session.save.character.dungeon_level = 0;
            surface_session.save.character.player_x = surface_start_x;
            surface_session.save.character.player_y = surface_start_y;
            surface_session.phase = MR_SESSION_TOWN;
            surface_session.input.movement_mode = MR_MOVEMENT_CARDINAL;
            surface_session.pending_save_snapshot_valid = 0;
            failed |= mr_session_arrow(&surface_session, surface_key) !=
                          MR_SESSION_STEP_MOVED ||
                      surface_session.save.character.dungeon_level != 0 ||
                      surface_session.save.character.player_x != surface_x ||
                      surface_session.save.character.player_y != surface_y ||
                      surface_session.pending_save_snapshot_valid ||
                      !mr_session_traverse(&surface_session, 0) ||
                      surface_session.save.character.dungeon_level !=
                          surface_delta ||
                      surface_session.phase != MR_SESSION_EXPLORING;
        }

        /* DUNSMALL 55DD-561C can return canonical numeric zero for a
         * level-zero endpoint whose matching deeper ladder overshoots the
         * surface.  It is drawn as the same circle as a chute, but 3428
         * refuses to fall at level zero and 0DE0 accepts only positive
         * values for D.  Preserve that confusing but observable original
         * distinction instead of treating every town circle as an entrance. */
        MRSession inert_surface_session = session;
        inert_surface_session.save.character.dungeon_level = 0;
        inert_surface_session.save.character.player_x = 1;
        inert_surface_session.save.character.player_y = 2;
        inert_surface_session.phase = MR_SESSION_TOWN;
        inert_surface_session.pending_save_snapshot_valid = 0;
        if (mr_world_resolve_vertical_feature(
                &inert_surface_session.world, 0, 1, 2) !=
                MR_VERTICAL_CHUTE ||
            mr_session_traverse(&inert_surface_session, 0) ||
            inert_surface_session.save.character.dungeon_level != 0 ||
            inert_surface_session.pending_save_snapshot_valid) {
            mr_session_error(error, error_size,
                             "DUNSMALL inert town chute mismatch");
            return 0;
        }

        /* The original's false-floor prompt is misleading. 567C prints
         * "D-GO DOWN", but the actual D dispatcher at 0DE0-0E1C requires
         * vertical_feature_value > 0 AND < 4. The sentinel installed by
         * 5549-5551 is 25, so D is silently rejected. Do not repair that
         * original bug in a literal-parity port. */
        MRSession false_floor_session = session;
        const int false_level = 1, false_x = 6, false_y = 11;
        /* 5793's feature hash is -1 at this coordinate. The shipped 7.NUM
         * need not enable every possible generated feature, so install only
         * its sparse special-cell bit in the isolated test copy. */
        false_floor_session.world.special_cells[false_level][false_y] |=
            1U << (20 - false_x);
        false_floor_session.save.character.dungeon_level = false_level;
        false_floor_session.save.character.player_x = false_x;
        false_floor_session.save.character.player_y = false_y;
        false_floor_session.phase = MR_SESSION_EXPLORING;
        if (mr_world_resolve_vertical_feature(
                &false_floor_session.world, false_level, false_x, false_y) !=
                MR_VERTICAL_FALSE_FLOOR ||
            mr_session_traverse(&false_floor_session, 0) ||
            false_floor_session.save.character.dungeon_level != false_level) {
            mr_session_error(error, error_size,
                             "DUNSMALL false-floor D gate mismatch");
            return 0;
        }

        /* 5793's feature hash is 8 at floor 1,(11,6), which 5649 normalizes
         * to a two-floor upward ladder. Install its sparse bit in the test
         * copy: 4C28 clamps the raw -1 destination to town floor 0 rather
         * than rejecting the otherwise valid ladder. */
        MRSession endpoint_session = session;
        const int endpoint_level = 1, endpoint_x = 11, endpoint_y = 6;
        endpoint_session.world.special_cells[endpoint_level][endpoint_y] |=
            1U << (20 - endpoint_x);
        endpoint_session.save.character.dungeon_level = endpoint_level;
        endpoint_session.save.character.player_x = endpoint_x;
        endpoint_session.save.character.player_y = endpoint_y;
        endpoint_session.phase = MR_SESSION_EXPLORING;
        if (mr_world_resolve_vertical_feature(
                &endpoint_session.world, endpoint_level, endpoint_x,
                endpoint_y) != -2 ||
            !mr_session_traverse(&endpoint_session, 1) ||
            endpoint_session.save.character.dungeon_level != 0 ||
            endpoint_session.phase != MR_SESSION_TOWN) {
            mr_session_error(error, error_size,
                             "DUNSMALL endpoint ladder clamp mismatch");
            return 0;
        }
    }

    session.save.character.player_y = 20;
    failed |= !mr_session_recover_native_row_twenty(&session) ||
              session.save.character.player_y != 19 ||
              mr_session_recover_native_row_twenty(&session);
    session.save.explored[0][20] = 0;
    session.save.character.player_y = 20;
    mr_session_reveal_current_cell(&session);
    failed |= session.save.explored[0][20] != 0;
    session.save.character.player_y = 10;
    failed |= mr_session_arrow(&session, MR_ARROW_DOWN) !=
                  MR_SESSION_STEP_TURNED ||
              session.phase != MR_SESSION_TOWN;
    session.save.character.dungeon_level = 1;
    session.save.character.player_x = 10;
    session.save.character.player_y = 10;
    session.phase = MR_SESSION_EXPLORING;
    mr_session_rebuild_occupancy(&session);
    mr_session_reveal_current_cell(&session);
    failed |= !(session.save.explored[1][10] & (1U << 10));
    failed |= mr_session_monster_at(&session, 8, 12) != 1;
    failed |= mr_session_monster_at(&session, 5, 7) != 2;

    /* 30D9-311E gates the destination occupancy test with B50E=1, the
     * combat view depth.  Exploration uses B50E=50 and therefore permits
     * walking onto the monster; 08F6/49B1 owns the ensuing transition. */
    {
        static const int dx[] = {0, 0, 1, 0, -1};
        static const int dy[] = {0, -1, 0, 1, 0};
        static const MRArrowKey keys[] = {
            0, MR_ARROW_UP, MR_ARROW_RIGHT, MR_ARROW_DOWN, MR_ARROW_LEFT
        };
        int monster_index = 0, monster_x = 0, monster_y = 0;
        int start_x = 0, start_y = 0;
        MRArrowKey occupied_key = 0;
        for (int slot = 1; slot <= MR_MONSTER_SLOTS_PER_LEVEL &&
                           !occupied_key; ++slot) {
            int index = mr_monster_index(1, slot);
            if (session.world.monster_health[index] <= 0) continue;
            int packed = session.world.monster_positions[index];
            int target_x = mr_monster_position_x(packed);
            int target_y = mr_monster_position_y(packed);
            for (int direction = MR_DIRECTION_NORTH;
                 direction <= MR_DIRECTION_WEST; ++direction) {
                int candidate_x = target_x - dx[direction];
                int candidate_y = target_y - dy[direction];
                int moved_x = candidate_x, moved_y = candidate_y;
                if (candidate_x < 1 || candidate_x > 20 ||
                    candidate_y < 1 || candidate_y > 19)
                    continue;
                if (mr_world_try_move(
                        1,
                        (float)session.save.character.
                            dungeon_generation_seed,
                        (MRDirection)direction,
                        &moved_x, &moved_y) != MR_MOVE_OK ||
                    moved_x != target_x || moved_y != target_y)
                    continue;
                monster_index = index;
                monster_x = target_x;
                monster_y = target_y;
                start_x = candidate_x;
                start_y = candidate_y;
                occupied_key = keys[direction];
                break;
            }
        }
        if (!occupied_key) {
            failed = 1;
        } else {
            MRSession occupied_session = session;
            occupied_session.save.character.player_x = (float)start_x;
            occupied_session.save.character.player_y = (float)start_y;
            occupied_session.input.movement_mode = MR_MOVEMENT_CARDINAL;
            occupied_session.combat_global_index = 0;
            occupied_session.primary_tracking_global_index = 0;
            occupied_session.secondary_tracking_global_index = 0;
            failed |= mr_session_arrow(&occupied_session, occupied_key) !=
                          MR_SESSION_STEP_MOVED ||
                      occupied_session.phase != MR_SESSION_EXPLORING ||
                      occupied_session.combat_global_index != 0 ||
                      occupied_session.save.character.player_x != monster_x ||
                      occupied_session.save.character.player_y != monster_y ||
                      mr_session_monster_at(&occupied_session, monster_x,
                                            monster_y) != monster_index;

            /* The same occupied destination is rejected once combat has
             * selected depth one, leaving both coordinates untouched for
             * 33EA's notice. */
            occupied_session = session;
            occupied_session.save.character.player_x = (float)start_x;
            occupied_session.save.character.player_y = (float)start_y;
            occupied_session.input.movement_mode = MR_MOVEMENT_CARDINAL;
            occupied_session.phase = MR_SESSION_COMBAT;
            occupied_session.combat_global_index = monster_index == 1 ? 2 : 1;
            failed |= mr_session_arrow(&occupied_session, occupied_key) !=
                          MR_SESSION_STEP_MONSTER ||
                      occupied_session.phase != MR_SESSION_COMBAT ||
                      occupied_session.save.character.player_x != start_x ||
                      occupied_session.save.character.player_y != start_y;
        }
    }
    session.save.character.player_x = 10;
    session.save.character.player_y = 10;
    session.phase = MR_SESSION_EXPLORING;
    session.combat_global_index = 0;
    session.input.movement_mode = MR_MOVEMENT_RELATIVE;

    /* 06D2-0841 consumes one seven-way advice RND on every positive dungeon
     * cycle.  These QB seeds select buckets one through five directly and
     * isolate each recovered predicate. */
    MRSession advice_session;
    MRSessionCycleResult advice_cycle;
    memset(&advice_session, 0, sizeof(advice_session));
    advice_session.phase = MR_SESSION_EXPLORING;
    advice_session.save.character.dungeon_level = 10;
    advice_session.save.character.player_level = 1;
    advice_session.save.character.health_max = 100;
    advice_session.save.character.health_current = 100;
    advice_session.save.character.experience =
        mr_experience_threshold(1) + 1.0;
    advice_session.rng.state = 45; /* bucket 1 */
    mr_session_begin_command_cycle(&advice_session, &advice_cycle);
    failed |= advice_cycle.ambient_advice != MR_ADVICE_STAY_AT_INN ||
              advice_session.rng.state != 0x48923CU;
    advice_session.save.character.experience = 0;
    advice_session.save.character.health_current = 24;
    advice_session.rng.state = 32; /* bucket 2 */
    mr_session_begin_command_cycle(&advice_session, &advice_cycle);
    failed |= advice_cycle.ambient_advice != MR_ADVICE_GET_CURE;
    advice_session.save.character.health_current = 100;
    advice_session.rng.state = 19; /* bucket 3 */
    mr_session_begin_command_cycle(&advice_session, &advice_cycle);
    failed |= advice_cycle.ambient_advice != MR_ADVICE_DUNGEON_TOO_DEEP;
    advice_session.save.character.dungeon_level = 1;
    advice_session.save.character.carried_treasure = 1;
    advice_session.rng.state = 5; /* bucket 4 */
    mr_session_begin_command_cycle(&advice_session, &advice_cycle);
    failed |= advice_cycle.ambient_advice != MR_ADVICE_CASH_IN_TREASURE;
    advice_session.rng.state = 0; /* bucket 5 */
    mr_session_begin_command_cycle(&advice_session, &advice_cycle);
    failed |= advice_cycle.ambient_advice != MR_ADVICE_NONE ||
              advice_session.rng.state != 0xC39EC3U;
    advice_session.phase = MR_SESSION_TOWN;
    advice_session.save.character.dungeon_level = 0;
    advice_session.rng.state = 5;
    mr_session_begin_command_cycle(&advice_session, &advice_cycle);
    failed |= advice_cycle.ambient_advice != MR_ADVICE_NONE ||
              advice_session.rng.state != 5;

    /* BF60 normalizes the empty-INKEY$ loop to 326 polls/second. At a
     * normalized calibration of one, 7EEC's minimum interval is eight.
     * Seed 0x31 makes the first QuickBASIC roll land on bucket one, proving
     * both the single-RND boundary and 7001's increment-before-parity order. */
    failed |= mr_session_idle_monster_interval(0, 0, 1.0) != 8;
    failed |= mr_session_idle_monster_interval(20, 100, 10.0) != 122;
    session.rng.state = 0x31U;
    MRMonsterAdvanceResult idle_advance;
    failed |= !mr_session_idle_monster_poll(&session, 1.0, &idle_advance) ||
              idle_advance.global_index != 2 ||
              idle_advance.code != MR_MONSTER_ADVANCE_NONE ||
              session.idle_monster_global_cursor != 2 ||
              session.rng.state != 0x3DA230U;

    /* The next selected odd record enters 70A1. Mark it dead temporarily so
     * the test isolates cursor/parity behavior without changing the seeded
     * world table used by the later movement and combat assertions. */
    int idle_health = session.world.monster_health[3];
    session.world.monster_health[3] = 0;
    session.rng.state = 0x31U;
    failed |= !mr_session_idle_monster_poll(&session, 1.0, &idle_advance) ||
              idle_advance.global_index != 3 ||
              session.idle_monster_global_cursor != 3 ||
              session.rng.state != 0x3DA230U;
    session.world.monster_health[3] = idle_health;

    /* 7EEC is not exploration-only. Town still consumes its gate RND before
     * 7001 returns for level zero, and the combat INKEY$ loop invokes the
     * same helper without replacing the active encounter. */
    MRSessionPhase saved_idle_phase = session.phase;
    double saved_idle_level = session.save.character.dungeon_level;
    int saved_idle_cursor = session.idle_monster_global_cursor;
    int saved_combat_index = session.combat_global_index;
    session.phase = MR_SESSION_TOWN;
    session.save.character.dungeon_level = 0;
    session.rng.state = 0;
    (void)mr_session_idle_monster_poll(&session, 1.0, &idle_advance);
    failed |= session.rng.state != 0xC39EC3U ||
              session.phase != MR_SESSION_TOWN;
    session.phase = MR_SESSION_COMBAT;
    session.save.character.dungeon_level = saved_idle_level;
    session.idle_monster_global_cursor = mr_monster_index(
        mr_session_int(saved_idle_level), 1);
    session.combat_global_index = 1;
    session.rng.state = 0x31U;
    failed |= !mr_session_idle_monster_poll(&session, 1.0, &idle_advance) ||
              idle_advance.global_index != 2 ||
              session.phase != MR_SESSION_COMBAT ||
              session.combat_global_index != 1;
    session.phase = saved_idle_phase;
    session.save.character.dungeon_level = saved_idle_level;
    session.idle_monster_global_cursor = saved_idle_cursor;
    session.combat_global_index = saved_combat_index;

    /* 70A1 movement: the fixed QuickBASIC seed consumes one weight-gate RND
     * and one direction-gate RND. With a primary two cells north and facing
     * north, the original facing-axis rule picks east. */
    session.save.character.player_x = 8;
    session.save.character.player_y = 10;
    session.save.character.carried_weight = 1000;
    session.save.character.dungeon_generation_seed = 3;
    session.input.facing = MR_DIRECTION_NORTH;
    session.primary_tracking_global_index = 1;
    session.secondary_tracking_global_index = 0;
    session.active_monster_pursuit_target = 0;
    session.combat_global_index = 0;
    session.rng.state = 0x050000U;
    MRMonsterAdvanceResult advance;
    failed |= mr_session_advance_active_monster(&session, 1, 12, &advance) !=
              MR_MONSTER_ADVANCE_MOVED;
    failed |= advance.old_x != 8 || advance.old_y != 12 ||
              advance.new_x != 9 || advance.new_y != 12 ||
              advance.direction != MR_DIRECTION_EAST ||
              session.world.monster_positions[1] !=
                  mr_monster_pack_position(9, 12) ||
              session.rng.state != 0x888E7AU;

    /* Restore the seeded table entry so the following combat assertions do
     * not depend on the movement test's native mutation. */
    session.occupancy[12][9] = 0;
    session.world.monster_positions[1] = 392;
    session.occupancy[12][8] = 1;

    /* 0C0B delegates movement to 70A1 but does not itself enter combat.
     * With the player one cell east, this is the same deterministic pursuit
     * used above and must leave the outer 08F6/49B1 occupancy check in charge
     * of the later controller transition. */
    session.save.character.player_x = 9;
    session.save.character.player_y = 12;
    session.player_x_at_last_monster_update = 8;
    session.player_y_at_last_monster_update = 12;
    session.primary_tracking_global_index = 1;
    session.secondary_tracking_global_index = 0;
    session.active_monster_pursuit_target = 0;
    session.combat_global_index = 0;
    session.phase = MR_SESSION_EXPLORING;
    session.rng.state = 0x050000U;
    MRMonsterAdvanceResult tracked[2];
    failed |= mr_session_update_active_monsters(&session, tracked) != 1 ||
              tracked[0].code != MR_MONSTER_ADVANCE_REACHED_PLAYER ||
              session.phase != MR_SESSION_EXPLORING ||
              session.combat_global_index != 0 ||
              mr_session_monster_at(&session, 9, 12) != 1;

    session.occupancy[12][9] = 0;
    session.world.monster_positions[1] = 392;
    session.occupancy[12][8] = 1;
    session.save.character.player_x = 8;
    session.save.character.player_y = 10;
    session.active_monster_pursuit_target = -7;
    failed |= !mr_session_begin_combat(&session, 1) ||
              session.active_monster_pursuit_target != 100;
    failed |= session.combat_floor_slot != 1 ||
              session.combat_health != 4 ||
              session.combat_profile.type != mr_monster_type(1, 1, 4);
    failed |= mr_session_arrow(&session, MR_ARROW_RIGHT) !=
                  MR_SESSION_STEP_TURNED ||
              session.phase != MR_SESSION_COMBAT;

    /* 871F routes arrows through ordinary movement while fighting. Find a
     * deterministic open neighboring cell, then exercise 8E76's first
     * three-way escape gate. The movement itself consumes no RNG; this seed
     * makes the first separation RND produce INT(RND*3)==1. */
    session.input.movement_mode = MR_MOVEMENT_CARDINAL;
    session.primary_tracking_global_index = 2;
    session.secondary_tracking_global_index = 0;
    MRArrowKey escape_key = 0;
    for (int direction = MR_DIRECTION_NORTH;
         direction <= MR_DIRECTION_WEST; ++direction) {
        int test_x = mr_session_int(session.save.character.player_x);
        int test_y = mr_session_int(session.save.character.player_y);
        MRMoveResult move = mr_world_try_move(
            mr_session_int(session.save.character.dungeon_level),
            (float)session.save.character.dungeon_generation_seed,
            (MRDirection)direction, &test_x, &test_y);
        if (move != MR_MOVE_OK || mr_session_monster_at(
                &session, test_x, test_y))
            continue;
        escape_key = direction == MR_DIRECTION_NORTH ? MR_ARROW_UP :
                     direction == MR_DIRECTION_EAST ? MR_ARROW_RIGHT :
                     direction == MR_DIRECTION_SOUTH ? MR_ARROW_DOWN :
                                                       MR_ARROW_LEFT;
        break;
    }
    if (!escape_key) {
        failed = 1;
    } else {
        session.rng.state = 0x123456U;
        MRSessionStepResult escape = mr_session_arrow(&session, escape_key);
        failed |= (escape != MR_SESSION_STEP_MOVED &&
                   escape != MR_SESSION_STEP_CHUTE) ||
                  (escape == MR_SESSION_STEP_MOVED &&
                   session.phase != MR_SESSION_COMBAT) ||
                  session.rng.state != 0x123456U;
        if (escape == MR_SESSION_STEP_MOVED) {
            MRCombatSeparationResult separation;
            failed |= !mr_session_resolve_combat_separation(
                           &session, &separation) ||
                      separation.code != MR_COMBAT_SEPARATION_ESCAPED ||
                      session.phase == MR_SESSION_COMBAT ||
                      session.rng.state != 0x71D9C1U;
        }
    }
    session.input.movement_mode = MR_MOVEMENT_RELATIVE;

    /* 8F2F short-circuits before the invisibility RND when armor*Agility beats
     * the pursuit score. Starting at 050000h, exactly three RND calls leave
     * the original stream at 945B55h; consuming the optional fourth call
     * would incorrectly leave it at 4A20C4h and shift every later outcome. */
    {
        MRSession armor_escape = session;
        armor_escape.phase = MR_SESSION_EXPLORING;
        armor_escape.save.character.dungeon_level = 1;
        armor_escape.save.character.player_x = 8;
        armor_escape.save.character.player_y = 10;
        /* The scripted pursuit score is 19. Armor 2 * Agility 10 escapes,
         * while the deliberately low Laziness value would not; this pins the
         * one-based DS:1FA6 -> native [4] mapping. */
        armor_escape.save.character.equipped_armor = 2;
        armor_escape.save.character.attributes[4] = 10;
        armor_escape.save.character.attributes[5] = 1;
        armor_escape.save.character.active_effects[
            MR_EFFECT_INVISIBILITY] = 1;
        armor_escape.world.monster_positions[1] =
            mr_monster_pack_position(8, 12);
        armor_escape.world.monster_health[1] = 4;
        mr_session_rebuild_occupancy(&armor_escape);
        failed |= !mr_session_begin_combat(&armor_escape, 1);
        armor_escape.rng.state = 0x050000U;
        MRCombatSeparationResult armor_result;
        failed |= !mr_session_resolve_combat_separation(
                       &armor_escape, &armor_result) ||
                  armor_result.code != MR_COMBAT_SEPARATION_ESCAPED ||
                  armor_escape.phase == MR_SESSION_COMBAT ||
                  armor_escape.rng.state != 0x945B55U;
    }

    /* Seed 050000 traverses the full non-sticky pursuit branch: the first
     * gate does not escape, armor*Agility is zero, the combat-depth 70A1 call
     * consumes its eager weight RND and enters the player's cell, and the
     * following INT(RND*2)==1 suppresses the immediate strike. */
    session.save.character.equipped_armor = 0;
    session.save.character.attributes[4] = 1;
    session.save.character.active_effects[MR_EFFECT_INVISIBILITY] = 0;
    session.phase = MR_SESSION_EXPLORING;
    mr_session_rebuild_occupancy(&session);
    failed |= !mr_session_begin_combat(&session, 1);
    session.rng.state = 0x050000U;
    MRCombatSeparationResult pursued;
    failed |= !mr_session_resolve_combat_separation(&session, &pursued) ||
              pursued.code != MR_COMBAT_SEPARATION_REENGAGED ||
              pursued.advance.code != MR_MONSTER_ADVANCE_REACHED_PLAYER ||
              session.phase != MR_SESSION_COMBAT ||
              mr_session_monster_at(
                  &session,
                  mr_session_int(session.save.character.player_x),
                  mr_session_int(session.save.character.player_y)) != 1 ||
              session.rng.state != 0x0396A9U;
    mr_session_leave_combat(&session);

    /* A3C2 clears the defeated cell before the replacement-coordinate loop.
     * Seed 0229 makes the first candidate land on the same (8,10) square;
     * the replacement must therefore remain present in both the table and
     * occupancy grid.  Clearing the old cell after installation erased it. */
    {
        MRSession same_cell = session;
        memset(same_cell.occupancy, 0, sizeof(same_cell.occupancy));
        same_cell.save.character.dungeon_level = 1;
        same_cell.combat_global_index = 1;
        same_cell.combat_health = 1;
        same_cell.world.monster_positions[1] =
            (uint16_t)mr_monster_pack_position(8, 10);
        same_cell.world.monster_health[1] = 0;
        same_cell.occupancy[10][8] = 1;
        same_cell.rng.state = 0x000229U;
        failed |= !mr_session_respawn_defeated_monster(&same_cell) ||
                  same_cell.world.monster_positions[1] !=
                      mr_monster_pack_position(8, 10) ||
                  same_cell.world.monster_health[1] <= 0 ||
                  mr_session_monster_at(&same_cell, 8, 10) != 1 ||
                  same_cell.rng.state != 0x902D02U;
    }

    /* Exercise the exact defeated-monster ordering without depending on a
     * particular weapon roll. With this seed, slot 1's post-respawn gate is
     * INT(RND*5)==1, so the odd slot skips the entire reward pipeline. */
    failed |= !mr_session_begin_combat(&session, 1);
    int old_position = session.world.monster_positions[1];
    session.world.monster_health[1] = 0;
    session.rng.state = 0x050000U;
    failed |= !mr_session_respawn_defeated_monster(&session);
    session.phase = MR_SESSION_DEFEAT_NOTICE;
    failed |= !mr_session_acknowledge_defeat(&session);
    failed |= session.phase != MR_SESSION_EXPLORING ||
              session.world.monster_health[1] <= 0 ||
              session.world.monster_positions[1] == old_position ||
              mr_session_monster_at(
                  &session, mr_monster_position_x(
                      session.world.monster_positions[1]),
                  mr_monster_position_y(
                  session.world.monster_positions[1])) != 1;

    /* An even slot always enters the reward pipeline, although its eager
     * gate RND is still consumed. Coins are one all-or-nothing T/L choice. */
    failed |= !mr_session_begin_combat(&session, 2);
    old_position = session.world.monster_positions[2];
    session.world.monster_health[2] = 0;
    session.rng.state = 0x050000U;
    failed |= !mr_session_respawn_defeated_monster(&session);
    session.phase = MR_SESSION_DEFEAT_NOTICE;
    failed |= !mr_session_acknowledge_defeat(&session);
    failed |= session.phase != MR_SESSION_REWARDS ||
              session.world.monster_positions[2] == old_position;
    if (session.coin_loot_pending)
        failed |= mr_session_resolve_coin_loot(&session, 0) !=
                  MR_COIN_LOOT_DECLINED;
    failed |= !session.coin_loot_resolved ||
              session.post_combat_rewards_generated;
    failed |= !mr_session_generate_post_combat_rewards_staged(
                  &session, NULL, NULL) ||
              !session.post_combat_rewards_generated;
    failed |= !mr_session_finish_rewards(&session) ||
              session.phase != MR_SESSION_EXPLORING;

    /* Preparation spell transitions are session transitions, not merely
     * arithmetic on the save record: the native layer must swap the current
     * monster table and reveal the arrival cell just as 4C6B/4CC3 do. */
    session.save.character.spell_points = 100;
    session.save.character.dungeon_level = 1;
    session.phase = MR_SESSION_EXPLORING;
    MRPreparationCastResult preparation;
    failed |= !mr_session_cast_preparation_spell(
        &session, MR_PREP_DESCEND, &preparation);
    failed |= !preparation.cast || preparation.spell_points_spent != 3 ||
              session.save.character.dungeon_level != 2 ||
              session.phase != MR_SESSION_EXPLORING ||
              session.shared_menu_value != 6.0f;

    /* 4C28 clamps Descend at the original floor-70 ceiling, after the spell
     * has already spent its three points. */
    session.save.character.dungeon_level = MR_DUNGEON_LEVELS;
    session.save.character.spell_points = 100;
    failed |= !mr_session_cast_preparation_spell(
        &session, MR_PREP_DESCEND, &preparation);
    failed |= session.save.character.dungeon_level != MR_DUNGEON_LEVELS ||
              preparation.spell_points_spent != 3 ||
              session.phase != MR_SESSION_EXPLORING;

    /* Teleport writes .5, (18,17), then immediately calls the same 1CF5
     * town-entry recomputation used by ladders and town-warp spells. */
    session.save.character.attributes[0] = 16;
    session.save.character.attributes[3] = 20;
    session.save.character.attributes[4] = 25;
    session.save.character.health_growth_factor = 99;
    session.save.character.combat_attack_factor = 99;
    session.save.character.agility_defense_factor = 99;
    session.save.character.dungeon_level = 12;
    session.save.character.carried_items[MR_ITEM_TELEPORT_SCROLL] = 1;
    MRItemUseResult preparation_item;
    failed |= !mr_session_use_preparation_item(
        &session, MR_PREP_ITEM_TELEPORT_SCROLL, &preparation_item);
    failed |= !preparation_item.town_transition ||
              session.save.character.dungeon_level != 0 ||
              session.save.character.player_x != 18 ||
              session.save.character.player_y != 17 ||
              session.save.character.health_growth_factor != 14 ||
              session.save.character.combat_attack_factor != 3.5 ||
              session.save.character.agility_defense_factor != 13 ||
              session.shared_menu_value != 1.0f ||
              session.phase != MR_SESSION_TOWN;

    /* Wand counters at B6CC/B6CE are global across encounters. White does
     * not grant the monster a response, and its ten-turn lock remains when
     * the next encounter begins. */
    session.save.character.dungeon_level = 1;
    session.phase = MR_SESSION_EXPLORING;
    mr_session_rebuild_occupancy(&session);
    failed |= !mr_session_begin_combat(&session, 1);
    session.save.character.persistent_values[
        MR_PERSISTENT_WHITE_WAND_CHARGES] = 1;
    MRSessionItemResult item_result;
    failed |= !mr_session_use_wand(&session, MR_WAND_WHITE, &item_result);
    failed |= !item_result.item.used || item_result.monster_attacked ||
              session.monster_attack_state.cannot_strike_counter != 10 ||
              session.shared_menu_value != 4.0f ||
              session.phase != MR_SESSION_COMBAT;
    session.save.character.persistent_values[
        MR_PERSISTENT_WHITE_PILLS] = 1;
    MRItemUseResult pill_result;
    failed |= !mr_session_use_pill(
                  &session, MR_PILL_WHITE, &pill_result) ||
              !pill_result.used || session.shared_menu_value != 1.0f;
    mr_session_leave_combat(&session);
    failed |= !mr_session_begin_combat(&session, 2) ||
              session.monster_attack_state.cannot_strike_counter != 10;

    /* The combat input cycle turns an inactive shielding TIMER into the
     * original B706 baseline of 16. This superficially odd value comes from
     * 8655-8660 and is intentionally not normalized away. */
    MRTimedEffectResult expiry;
    mr_session_expire_combat_effects(&session, 100.0, &expiry);
    failed |= session.monster_attack_state.shield_defense_bonus != 16.0;
    mr_session_leave_combat(&session);

    /* The outer 0627/0636 redraw assigns B4C2=1. Disease still advances,
     * but health rings do not heal at this boundary. */
    session.phase = MR_SESSION_EXPLORING;
    session.save.character.health_current = 5;
    session.save.character.health_max = 10;
    session.save.character.magic_item_values[MR_MAGIC_HEALTH_RINGS] = 2;
    session.save.character.persistent_values[MR_PERSISTENT_DISEASE] = 99;
    for (int index = 0; index < MR_SAVE_ATTRIBUTES; ++index)
        session.save.character.attributes[index] = 5;
    MRSessionCycleResult cycle;
    mr_session_begin_command_cycle(&session, &cycle);
    double drained_sum = 0;
    for (int index = 0; index < MR_SAVE_ATTRIBUTES; ++index)
        drained_sum += session.save.character.attributes[index];
    failed |= cycle.health_regenerated != 0 ||
              session.save.character.health_current != 5 ||
              !cycle.disease_triggered || cycle.disease_attribute < 0 ||
              cycle.disease_attribute >= MR_SAVE_ATTRIBUTES ||
              drained_sum != 29 ||
              session.save.character.persistent_values[
                  MR_PERSISTENT_DISEASE] != 100;

    /* Relative rotations also set B4C2=1 before 3FFC. A coordinate-edge
     * movement falls through 33CB without that assignment, so it receives
     * the ring heal even though the player cannot leave the grid. */
    session.save.character.health_current = 5;
    session.save.character.persistent_values[MR_PERSISTENT_DISEASE] = 99;
    for (int index = 0; index < MR_SAVE_ATTRIBUTES; ++index)
        session.save.character.attributes[index] = 5;
    session.input.movement_mode = MR_MOVEMENT_RELATIVE;
    session.input.facing = MR_DIRECTION_NORTH;
    MRSessionCycleResult arrow_cycle;
    failed |= mr_session_arrow_with_cycle(
                  &session, MR_ARROW_RIGHT, &arrow_cycle) !=
                  MR_SESSION_STEP_TURNED ||
              arrow_cycle.health_regenerated != 0 ||
              !arrow_cycle.disease_triggered ||
              session.save.character.health_current != 5;

    session.save.character.health_current = 5;
    session.save.character.persistent_values[MR_PERSISTENT_DISEASE] = 99;
    for (int index = 0; index < MR_SAVE_ATTRIBUTES; ++index)
        session.save.character.attributes[index] = 5;
    session.save.character.player_x = 10;
    session.save.character.player_y = 1;
    session.input.movement_mode = MR_MOVEMENT_CARDINAL;
    failed |= mr_session_arrow_with_cycle(
                  &session, MR_ARROW_UP, &arrow_cycle) !=
                  MR_SESSION_STEP_BOUNDARY ||
              arrow_cycle.health_regenerated != 2 ||
              !arrow_cycle.disease_triggered ||
              session.save.character.health_current != 7;

    /* DUNSMALL 803A+ is a distinct combat loop and has no call to the
     * command-cycle status routine at 3FFC.  Guard the exact state that a
     * stray outer-loop call used to corrupt: HP, disease, and RNG. */
    session.phase = MR_SESSION_COMBAT;
    session.save.character.health_current = 5;
    session.save.character.health_max = 10;
    session.save.character.magic_item_values[MR_MAGIC_HEALTH_RINGS] = 2;
    session.save.character.persistent_values[MR_PERSISTENT_DISEASE] = 99;
    session.rng.state = 0x123456U;
    mr_session_begin_command_cycle(&session, &cycle);
    failed |= cycle.health_regenerated != 0 || cycle.disease_triggered ||
              cycle.ambient_advice != MR_ADVICE_NONE ||
              session.save.character.health_current != 5 ||
              session.save.character.persistent_values[
                  MR_PERSISTENT_DISEASE] != 99 ||
              session.rng.state != 0x123456U;

    /* Before 49B1 jumps into combat, the original performs one final 3FFC
     * status pass.  It includes rings/disease but not the separate advice
     * roll at 06D2. */
    session.save.character.health_current = 5;
    session.save.character.magic_item_values[MR_MAGIC_HEALTH_RINGS] = 2;
    session.save.character.persistent_values[MR_PERSISTENT_DISEASE] = 99;
    for (int index = 0; index < MR_SAVE_ATTRIBUTES; ++index)
        session.save.character.attributes[index] = 5;
    session.rng.state = 0x123456U;
    MRRng combat_entry_rng = session.rng;
    (void)mr_rng_next(&combat_entry_rng);
    mr_session_begin_combat_entry_status_cycle(&session, &cycle);
    drained_sum = 0;
    for (int index = 0; index < MR_SAVE_ATTRIBUTES; ++index)
        drained_sum += session.save.character.attributes[index];
    failed |= cycle.health_regenerated != 2 || !cycle.disease_triggered ||
              cycle.ambient_advice != MR_ADVICE_NONE ||
              session.save.character.health_current != 7 ||
              session.save.character.persistent_values[
                  MR_PERSISTENT_DISEASE] != 100 ||
              drained_sum != 29 ||
              session.rng.state != combat_entry_rng.state;

    /* A deliberate step onto an exploration monster has already executed
     * that same 3FFC boundary before 49B1 enters 803A. The host combat
     * handoff must consume the marker without ticking either effect again. */
    session.combat_entry_redraw_already_applied = 1;
    session.save.character.health_current = 5;
    session.save.character.persistent_values[MR_PERSISTENT_DISEASE] = 99;
    session.rng.state = 0x123456U;
    mr_session_begin_combat_entry_status_cycle(&session, &cycle);
    failed |= cycle.health_regenerated != 0 || cycle.disease_triggered ||
              session.save.character.health_current != 5 ||
              session.save.character.persistent_values[
                  MR_PERSISTENT_DISEASE] != 99 ||
              session.rng.state != 0x123456U ||
              session.combat_entry_redraw_already_applied;

    /* Rise preserves the monster, consumes four points, leaves combat, and
     * enters town when it reaches floor zero. */
    session.save.character.dungeon_level = 1;
    session.save.character.spell_points = 100;
    session.save.character.attributes[0] = 16;
    session.save.character.attributes[3] = 20;
    session.save.character.attributes[4] = 25;
    session.save.character.health_growth_factor = 99;
    session.save.character.combat_attack_factor = 99;
    session.save.character.agility_defense_factor = 99;
    session.save.character.active_effects[MR_EFFECT_BATTLE_STRENGTH] = 15;
    session.phase = MR_SESSION_EXPLORING;
    mr_session_rebuild_occupancy(&session);
    int rise_monster_health = session.world.monster_health[1];
    failed |= !mr_session_begin_combat(&session, 1);
    MRSessionBattleSpellResult spell_result;
    failed |= !mr_session_cast_battle_spell(
        &session, MR_BATTLE_RISE, &spell_result);
    failed |= !spell_result.spell.cast ||
              spell_result.spell.spell_points_spent != 4 ||
              session.save.character.dungeon_level != 0 ||
              session.phase != MR_SESSION_TOWN ||
              session.world.monster_health[1] != rise_monster_health ||
              session.save.character.health_growth_factor != 14 ||
              session.save.character.combat_attack_factor != 3.5 ||
              session.save.character.agility_defense_factor != 13 ||
              session.shared_menu_value != 8.0f ||
              session.save.character.active_effects[
                  MR_EFFECT_BATTLE_STRENGTH] != 15;

    /* GO AWAY still respawns and rewards the monster, then waits for Return;
     * only the YOU KILLED IT text is suppressed by B726. */
    session.save.character.dungeon_level = 1;
    session.save.character.player_level = 100;
    session.save.character.spell_points = 100;
    session.phase = MR_SESSION_EXPLORING;
    mr_session_rebuild_occupancy(&session);
    failed |= !mr_session_begin_combat(&session, 1);
    session.rng.state = 0x050000U;
    failed |= !mr_session_cast_battle_spell(
        &session, MR_BATTLE_GO_AWAY, &spell_result);
    failed |= !spell_result.spell.rewarded_defeat ||
              !spell_result.spell.suppress_kill_message ||
              session.phase != MR_SESSION_DEFEAT_NOTICE ||
              !session.suppress_defeat_message ||
              session.world.monster_health[1] <= 0;

    /* The level-70 fountain uses the persistent x/y slots as its command
     * gate. Drinking performs the full restart and exposes the original
     * pre-derived-stat SAVE boundary. */
    session.save.character = (MRCharacter){0};
    session.save.character.player_class = MR_CLASS_WIZARD;
    session.save.character.dungeon_level = 70;
    session.save.character.player_x = 10;
    session.save.character.player_y = 10;
    session.save.character.dungeon_generation_seed = 1;
    session.save.character.carried_treasure = 125;
    session.save.character.attributes[0] = 12;
    session.save.character.attributes[1] = 10;
    session.save.character.attributes[2] = 10;
    session.save.character.attributes[3] = 20;
    session.save.character.attributes[4] = 20;
    session.save.character.attributes[5] = 10;
    session.save.character.combat_attack_factor = 44;
    session.save.character.agility_defense_factor = 55;
    session.save.character.persistent_values[MR_PERSISTENT_FOUNTAIN_X] = 10;
    session.save.character.persistent_values[MR_PERSISTENT_FOUNTAIN_Y] = 10;
    session.phase = MR_SESSION_EXPLORING;
    session.rng.state = 0x050000U;
    MRFountainResult fountain;
    failed |= !mr_session_is_at_fountain(&session) ||
              !mr_session_drink_fountain(&session, &fountain) ||
              session.phase != MR_SESSION_TOWN ||
              session.save.character.dungeon_level != 0 ||
              session.save.character.pocket_money != 125 ||
              fountain.next_fountain_x != 12 ||
              fountain.next_fountain_y != 10 ||
              !session.pending_save_snapshot_valid;
    MRSave boundary;
    failed |= !mr_session_take_save_snapshot(&session, &boundary) ||
              session.pending_save_snapshot_valid ||
              boundary.character.combat_attack_factor != 44 ||
              boundary.character.agility_defense_factor != 55;

    /* 9E92 persists a class-five level drain before the deep resource set's
     * following Strength decrement.  The headless session exposes that exact
     * boundary without writing into the supplied original save directory. */
    session.save.character = (MRCharacter){0};
    session.save.character.player_level = 5;
    session.save.character.experience = 100;
    session.save.character.health_max = 200;
    session.save.character.health_current = 200;
    session.save.character.health_growth_factor = 3;
    session.save.character.attributes[0] = 12;
    session.save.character.attributes[4] = 100;
    session.phase = MR_SESSION_COMBAT;
    /* DS:52FC is session-global, not a field reset with the encounter.  Seed
     * only the session copy here; mr_session_monster_turn must bridge it into
     * the formula state before 9CF3 consumes it. */
    session.shared_menu_value = 100;
    session.monster_attack_state =
        (MRMonsterAttackState){.shared_roll = 0};
    mr_monster_combat_profile(2, 19, 10, &session.combat_profile);
    session.rng.state = 0x050000U;
    MRMonsterTurnResult drain_turn;
    failed |= !mr_session_monster_turn(&session, &drain_turn) ||
              !drain_turn.strikes[0].level_drained ||
              !drain_turn.strikes[0].strength_drained ||
              !session.pending_save_snapshot_valid ||
              session.save.character.attributes[0] != 11 ||
              session.pending_save_snapshot.character.attributes[0] != 12 ||
              fabs(session.pending_save_snapshot.character.experience - 70.0)
                  > 1e-9 ||
              session.pending_save_snapshot.character.player_level != 4 ||
              session.shared_menu_value !=
                  session.monster_attack_state.shared_roll;
    session.pending_save_snapshot_valid = 0;

    /* Static trace A0A4-A22A: seed 0x1D selects reincarnation.  A surviving
     * death resumes the same DUNSMALL session in town instead of returning
     * to BEGIN or persisting a dead combat record. */
    session.phase = MR_SESSION_DEAD;
    session.rng.state = 0x00001DU;
    session.save.character.health_current = -5;
    session.save.character.health_max = 30;
    session.save.character.player_level = 9;
    session.save.character.experience = 400;
    session.save.character.pending_experience = 20;
    failed |= mr_session_resolve_player_death(&session) !=
                  MR_DEATH_REINCARNATED ||
              session.phase != MR_SESSION_TOWN ||
              session.save.character.health_current != 30 ||
              session.save.character.player_level != 0 ||
              session.save.character.experience != 0 ||
              session.save.character.pending_experience != 0 ||
              session.save.character.dungeon_level != 0 ||
              session.save.character.player_x != 14 ||
              session.save.character.player_y != 12 ||
              session.combat_global_index != 0;
    if (failed) {
        mr_session_error(error, error_size,
                         "DUNSMALL native session-state mismatch");
        return 0;
    }
    return 1;
}
