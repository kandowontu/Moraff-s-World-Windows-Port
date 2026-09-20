#include "mr_world.h"
#include "mr_math.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static uint16_t mr_u16le(const uint8_t *bytes) {
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static void mr_put_u16le(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value & 0xff);
    bytes[1] = (uint8_t)(value >> 8);
}

static double mr_decode_mbf_single(const uint8_t bytes[4]) {
    unsigned exponent = bytes[3];
    if (!exponent) return 0.0;
    unsigned mantissa = (unsigned)bytes[0] | ((unsigned)bytes[1] << 8) |
                        ((unsigned)(bytes[2] & 0x7F) << 16);
    double value = (1.0 + mantissa / 8388608.0) *
                   ldexp(1.0, (int)exponent - 129);
    return bytes[2] & 0x80 ? -value : value;
}

static void mr_error(char *error, size_t error_size, const char *message,
                     const char *path) {
    if (!error || !error_size) return;
    if (path) snprintf(error, error_size, "%s: %s", message, path);
    else snprintf(error, error_size, "%s", message);
}

static void mr_path(char *out, size_t out_size, const char *directory,
                    const char *name) {
    size_t length = strlen(directory);
    const char *separator = length && (directory[length - 1] == '/' ||
                                      directory[length - 1] == '\\') ? "" : "/";
    snprintf(out, out_size, "%s%s%s", directory, separator, name);
}

static int mr_load_bsave(const char *directory, const char *name,
                         uint16_t expected_offset, uint16_t expected_bytes,
                         MRBSaveHeader *header, uint8_t *payload,
                         char *error, size_t error_size) {
    char path[512];
    mr_path(path, sizeof(path), directory, name);
    FILE *file = fopen(path, "rb");
    if (!file) {
        mr_error(error, error_size, "Missing original Revenge BSAVE resource",
                 path);
        return 0;
    }

    uint8_t bytes[7];
    int ok = fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    if (!ok || bytes[0] != 0xFD) {
        fclose(file);
        mr_error(error, error_size, "Invalid Revenge BSAVE header", path);
        return 0;
    }
    header->segment = mr_u16le(bytes + 1);
    header->offset = mr_u16le(bytes + 3);
    header->payload_bytes = mr_u16le(bytes + 5);
    if (header->offset != expected_offset ||
        header->payload_bytes != expected_bytes) {
        fclose(file);
        mr_error(error, error_size, "Unexpected Revenge BSAVE layout", path);
        return 0;
    }
    if (fread(payload, 1, expected_bytes, file) != expected_bytes) {
        fclose(file);
        mr_error(error, error_size, "Truncated Revenge BSAVE payload", path);
        return 0;
    }
    int final = fgetc(file);
    header->dos_eof = final == 0x1A;
    if (header->dos_eof) final = fgetc(file);
    fclose(file);
    if (final != EOF) {
        mr_error(error, error_size, "Unexpected data after Revenge BSAVE", path);
        return 0;
    }
    return 1;
}

int mr_world_load(MRWorldData *world, const char *directory,
                  char *error, size_t error_size) {
    if (!world || !directory) {
        mr_error(error, error_size, "Invalid Revenge world-data request", NULL);
        return 0;
    }
    memset(world, 0, sizeof(*world));
    if (!mr_load_bsave(directory, "1.NUM", 0x2242,
                       MR_MONSTER_TABLE_PAYLOAD_BYTES,
                       &world->monster_positions_header,
                       world->monster_positions_raw, error, error_size) ||
        !mr_load_bsave(directory, "2.NUM", 0x3824,
                       MR_MONSTER_TABLE_PAYLOAD_BYTES,
                       &world->monster_health_header,
                       world->monster_health_raw, error, error_size) ||
        !mr_load_bsave(directory, "7.NUM", 0x1856,
                       MR_SPECIAL_CELL_PAYLOAD_BYTES,
                       &world->special_cells_header,
                       world->special_cells_raw, error, error_size))
        return 0;

    for (int index = 0; index < MR_MONSTER_TABLE_INDEX_COUNT - 1; index++) {
        world->monster_positions[index] =
            mr_u16le(world->monster_positions_raw + index * 2);
        world->monster_health[index] =
            (int16_t)mr_u16le(world->monster_health_raw + index * 2);
    }
    world->monster_positions[MR_MONSTER_TABLE_INDEX_COUNT - 1] =
        world->monster_positions_raw[MR_MONSTER_TABLE_PAYLOAD_BYTES - 1];
    world->monster_health[MR_MONSTER_TABLE_INDEX_COUNT - 1] =
        world->monster_health_raw[MR_MONSTER_TABLE_PAYLOAD_BYTES - 1];

    for (int level = 0; level < MR_SPECIAL_CELL_LEVELS; level++) {
        for (int row = 0; row < MR_DUNGEON_ROWS; row++) {
            int index = level * MR_DUNGEON_ROWS + row;
            double value = mr_decode_mbf_single(
                world->special_cells_raw + index * 4);
            if (!isfinite(value) || value < 0.0 || value > 1048575.0 ||
                value != floor(value)) {
                mr_error(error, error_size,
                         "Invalid special-cell mask in original 7.NUM", NULL);
                return 0;
            }
            world->special_cells[level][row] = (uint32_t)value;
        }
    }
    memcpy(world->special_cells_tail,
           world->special_cells_raw +
               MR_SPECIAL_CELL_LEVELS * MR_DUNGEON_ROWS * 4,
           sizeof(world->special_cells_tail));
    return 1;
}

static int mr_write_monster_table(const char *directory, const char *name,
                                  const MRBSaveHeader *header,
                                  const uint16_t *values, char *error,
                                  size_t error_size) {
    char path[512];
    uint8_t bytes[7] = {0xFD, 0, 0, 0, 0, 0, 0};
    uint8_t payload[MR_MONSTER_TABLE_PAYLOAD_BYTES];
    mr_path(path, sizeof(path), directory, name);
    mr_put_u16le(bytes + 1, header->segment);
    mr_put_u16le(bytes + 3, header->offset);
    mr_put_u16le(bytes + 5, MR_MONSTER_TABLE_PAYLOAD_BYTES);
    for (int index = 0; index < MR_MONSTER_TABLE_INDEX_COUNT - 1; index++)
        mr_put_u16le(payload + index * 2, values[index]);
    /* The original files end halfway through BASIC element 2800. BLOAD
     * supplies a zero high byte, and BSAVE writes only this source range. */
    payload[MR_MONSTER_TABLE_PAYLOAD_BYTES - 1] =
        (uint8_t)(values[MR_MONSTER_TABLE_INDEX_COUNT - 1] & 0xff);

    FILE *file = fopen(path, "wb");
    if (!file) {
        mr_error(error, error_size, "Cannot create Revenge monster table",
                 path);
        return 0;
    }
    int ok = fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes) &&
             fwrite(payload, 1, sizeof(payload), file) == sizeof(payload) &&
             fputc(0x1A, file) != EOF;
    if (fclose(file) != 0) ok = 0;
    if (!ok)
        mr_error(error, error_size, "Cannot write Revenge monster table",
                 path);
    return ok;
}

int mr_world_write_monsters(const MRWorldData *world, const char *directory,
                            char *error, size_t error_size) {
    if (!world || !directory) {
        mr_error(error, error_size,
                 "Invalid Revenge monster-table save request", NULL);
        return 0;
    }
    uint16_t health[MR_MONSTER_TABLE_INDEX_COUNT];
    for (int index = 0; index < MR_MONSTER_TABLE_INDEX_COUNT; index++)
        health[index] = (uint16_t)world->monster_health[index];
    return mr_write_monster_table(directory, "1.NUM",
                                  &world->monster_positions_header,
                                  world->monster_positions, error,
                                  error_size) &&
           mr_write_monster_table(directory, "2.NUM",
                                  &world->monster_health_header, health,
                                  error, error_size);
}

int mr_world_special_cell_bit(const MRWorldData *world,
                              int level, int x, int y) {
    if (!world || level < 0 || level >= MR_SPECIAL_CELL_LEVELS ||
        x < 1 || x > 20 || y < 0 || y >= MR_DUNGEON_ROWS)
        return 0;
    return (int)((world->special_cells[level][y] >> (20 - x)) & 1U);
}

int mr_world_procedural_wall_value(int dungeon_level,
                                   float dungeon_generation_seed,
                                   float factor_a, float factor_b,
                                   float factor_c) {
    if (dungeon_generation_seed == 0.0f) return 9;
    /* DUNSMALL:548B-54C7. Keep each assignment in single precision: the
     * original executes the corresponding $FADA/$FDVC/$FMUC operations on
     * MBF singles before SIN, ABS, and INT. Movement accepts values 0..7 and
     * treats 8..9 as a wall. */
    float value = (float)dungeon_level + 2.0f;
    value = value / dungeon_generation_seed;
    value = value * factor_c;
    value = value * factor_b;
    value = value * factor_a;
    value = value + 10.0f;
    value = mr_qb3_sin(value);
    value = value * 10.0f;
    value = fabsf(value);
    return (int)floorf(value);
}

int mr_world_wall_value_for_step(int dungeon_level,
                                 float dungeon_generation_seed,
                                 int x, int y, MRDirection direction) {
    /* DUNSMALL's playable rectangle is x=1..20, y=1..19.  The underlying
     * BSAVE tables have a row 20 because their BASIC arrays are dimensioned
     * 0..20, but the south-movement guard at 32E5 compares against 19 and
     * the full map closes that same row at 51AB-51F5.  Treating the spare
     * array row as playable puts the marker at scanline 198 and lets the
     * player walk through the map's visibly solid southern border. */
    if (x < 1 || x > 20 || y < 1 || y > 19) return 9;
    float factor_a, factor_b, factor_c;
    switch (direction) {
        case MR_DIRECTION_NORTH:
            if (y <= 1) return 9;
            factor_a = (float)y;
            factor_b = (float)x;
            factor_c = 1.0f;
            break;
        case MR_DIRECTION_EAST:
            if (x >= 20) return 9;
            factor_a = (float)y;
            factor_b = (float)(x + 1);
            factor_c = 2.0f;
            break;
        case MR_DIRECTION_SOUTH:
            if (y >= 19) return 9;
            factor_a = (float)(y + 1);
            factor_b = (float)x;
            factor_c = 1.0f;
            break;
        case MR_DIRECTION_WEST:
            if (x <= 1) return 9;
            factor_a = (float)y;
            factor_b = (float)x;
            factor_c = 2.0f;
            break;
        default:
            return 9;
    }
    return mr_world_procedural_wall_value(
        dungeon_level, dungeon_generation_seed,
        factor_a, factor_b, factor_c);
}

int mr_world_vertical_feature_hash(int dungeon_level, int x, int y,
                                   int level_search_offset) {
    if (dungeon_level < 0 || dungeon_level > 70 || x < 1 || x > 20 ||
        y < 0 || y > 20 || level_search_offset < 0 ||
        dungeon_level + level_search_offset > 70)
        return MR_VERTICAL_NONE;

    /* DUNSMALL:5793-580D. $FEXC is exponentiation. The compiler stores the
     * intermediate after every MBF-single operation; explicit float
     * assignments retain the original operation order here. $FSUG computes
     * the saved quotient minus INT(quotient), producing the fractional part
     * used for the modulo-300 hash. */
    float x_term = mr_qb3_pow((float)(x + 7), 1.3f);
    float y_term = mr_qb3_pow((float)(y + 6), 1.2f);
    float value = x_term * y_term;
    float level_term = mr_qb3_pow(
        (float)(level_search_offset + dungeon_level + 1), 1.1f);
    value = value * level_term;
    float quotient = value / 300.0f;
    float fraction = quotient - floorf(quotient);
    value = fraction * 300.0f;
    value = value + -3.0f;
    return (int)floorf(value);
}

static int mr_normalize_vertical_feature(int value) {
    /* DUNSMALL:5649-567B performs these as two distinct tests, rather than a
     * general remainder operation. Its callers constrain the input to 1..9. */
    if (value > 3) value -= 3;
    if (value > 3) value -= 3;
    return value;
}

int mr_world_resolve_vertical_feature(const MRWorldData *world,
                                      int dungeon_level, int x, int y) {
    if (!mr_world_special_cell_bit(world, dungeon_level, x, y))
        return MR_VERTICAL_NONE;

    /* DUNSMALL:552B-5648. A feature generated on this level is an upward
     * ladder and is negated by $FUMA. When no direct feature exists, the
     * routine searches up to three deeper levels for an upward ladder whose
     * length reaches this cell, thereby reconstructing its positive/downward
     * endpoint without storing a second table. */
    int value = mr_world_vertical_feature_hash(dungeon_level, x, y, 0);
    if (value < 0) return MR_VERTICAL_FALSE_FLOOR;
    if (value == 0)
        return dungeon_level == 70 ? MR_VERTICAL_NONE : MR_VERTICAL_CHUTE;
    if (value <= 9) return -mr_normalize_vertical_feature(value);

    for (int offset = 1; offset <= 3; offset++) {
        if (dungeon_level + offset > 70) return value;
        value = mr_world_vertical_feature_hash(
            dungeon_level, x, y, offset);
        if (value < 1 || value > 9) continue;
        value = mr_normalize_vertical_feature(value);
        /* DUNSMALL 55DD-561C has a separate level-zero arm.  A candidate
         * whose upward span overshoots the surface stores the compiler's
         * canonical zero and returns immediately.  That zero deliberately
         * draws as a chute circle in town, but fall_down_chute returns on
         * level zero and the D-command accepts only positive values.  These
         * inert circles are an original (if confusing) part of the map. */
        if (dungeon_level == 0 && value > offset)
            return MR_VERTICAL_CHUTE;
        if (offset == value) return value;
    }
    return MR_VERTICAL_NONE;
}

MRMoveResult mr_world_try_move(int dungeon_level,
                               float dungeon_generation_seed,
                               MRDirection direction, int *x, int *y) {
    if (!x || !y || *x < 1 || *x > 20 || *y < 1 || *y > 19)
        return MR_MOVE_BOUNDARY;

    int next_x = *x;
    int next_y = *y;
    switch (direction) {
        case MR_DIRECTION_NORTH:
            if (*y <= 1) return MR_MOVE_BOUNDARY;
            next_y--;
            break;
        case MR_DIRECTION_EAST:
            if (*x >= 20) return MR_MOVE_BOUNDARY;
            next_x++;
            break;
        case MR_DIRECTION_SOUTH:
            if (*y >= 19) return MR_MOVE_BOUNDARY;
            next_y++;
            break;
        case MR_DIRECTION_WEST:
            if (*x <= 1) return MR_MOVE_BOUNDARY;
            next_x--;
            break;
        default:
            return MR_MOVE_BOUNDARY;
    }
    if (mr_world_wall_value_for_step(dungeon_level,
            dungeon_generation_seed, *x, *y, direction) > 7)
        return MR_MOVE_WALL;
    *x = next_x;
    *y = next_y;
    return MR_MOVE_OK;
}

int mr_world_self_test(const char *directory,
                       char *error, size_t error_size) {
    MRWorldData world;
    if (!mr_world_load(&world, directory, error, error_size)) return 0;

    const char *failure = NULL;
    if (!world.monster_positions_header.dos_eof ||
        !world.monster_health_header.dos_eof ||
        !world.special_cells_header.dos_eof)
        failure = "DOS EOF markers";
    else if (world.monster_positions[0] != 0 ||
             world.monster_positions[1] != 392 ||
             world.monster_positions[MR_MONSTER_TABLE_INDEX_COUNT - 1] != 176)
        failure = "1.NUM monster-position table";
    else if (world.monster_health[0] != 0 ||
             world.monster_health[1] != 4 ||
             world.monster_health[MR_MONSTER_TABLE_INDEX_COUNT - 1] != 71)
        failure = "2.NUM monster-health table";
    else if (world.special_cells[0][2] != 524288U ||
             world.special_cells[70][19] != 8192U ||
             !mr_world_special_cell_bit(&world, 0, 1, 2) ||
             mr_world_special_cell_bit(&world, 0, 2, 2) ||
             !mr_world_special_cell_bit(&world, 70, 7, 19))
        failure = "7.NUM special-cell bit order";
    else if (mr_world_procedural_wall_value(0, 1.0f, 10.0f, 10.0f,
                                             1.0f) != 4 ||
             mr_world_procedural_wall_value(1, 1.0f, 10.0f, 10.0f,
                                             1.0f) != 8 ||
             mr_world_procedural_wall_value(70, 3.0f, 20.0f, 20.0f,
                                              2.0f) != 7 ||
             /* This original-runtime boundary is the regression that host
              * sinf gets wrong: BRUN30's polynomial returns door/passable 7,
              * while common libm implementations round it to solid wall 8. */
             mr_world_procedural_wall_value(39, 5.0f, 19.0f, 6.0f,
                                              2.0f) != 7)
        failure = "procedural SIN wall formula";
    else if (mr_world_vertical_feature_hash(0, 1, 2, 0) != 178 ||
             mr_world_vertical_feature_hash(70, 7, 19, 0) != 8 ||
             /* This marked 7.NUM cell is the recovered $FEXC regression:
              * host powf rounds its hash to 30; BRUN30 returns 31. */
             mr_world_vertical_feature_hash(70, 1, 11, 0) != 31 ||
             mr_world_resolve_vertical_feature(&world, 2, 1, 2) != -3 ||
             mr_world_resolve_vertical_feature(&world, 1, 14, 1) != 1 ||
             mr_world_resolve_vertical_feature(&world, 1, 3, 6) != 2 ||
             mr_world_resolve_vertical_feature(&world, 1, 13, 14) != 3 ||
             mr_world_resolve_vertical_feature(&world, 19, 9, 13) !=
                 MR_VERTICAL_NONE ||
             /* Floor zero contains both usable reciprocal down ladders and
              * inert overshooting endpoints.  The latter retain numeric
              * zero and the original circular map mark. */
             mr_world_resolve_vertical_feature(&world, 0, 1, 2) !=
                 MR_VERTICAL_CHUTE ||
             mr_world_resolve_vertical_feature(&world, 0, 15, 5) != 2 ||
             mr_world_resolve_vertical_feature(&world, 0, 2, 2) !=
                 MR_VERTICAL_NONE)
        failure = "vertical feature hash and cross-floor resolver";
    else {
        int x = 10;
        int y = 10;
        if (mr_world_try_move(0, 1.0f, MR_DIRECTION_NORTH, &x, &y) !=
                MR_MOVE_OK || x != 10 || y != 9 ||
            mr_world_try_move(0, 1.0f, MR_DIRECTION_SOUTH, &x, &y) !=
                MR_MOVE_OK || x != 10 || y != 10 ||
            mr_world_try_move(0, 1.0f, MR_DIRECTION_WEST, &x, &y) !=
                MR_MOVE_WALL || x != 10 || y != 10)
            failure = "procedural cardinal movement";
    }
    if (!failure) {
        /* DUNSMALL 32E5-3313: 19 is the last playable row.  The map's
         * bottom border and movement bounds must never disagree again. */
        int x = 10;
        int y = 19;
        if (mr_world_wall_value_for_step(1, 3.0f, x, y,
                                         MR_DIRECTION_SOUTH) != 9 ||
            mr_world_try_move(1, 3.0f, MR_DIRECTION_SOUTH, &x, &y) !=
                MR_MOVE_BOUNDARY || x != 10 || y != 19) {
            failure = "southern playable-map boundary";
        }
    }
    if (!failure) {
        for (size_t index = 0; index < sizeof(world.special_cells_tail); index++) {
            if (world.special_cells_tail[index]) {
                failure = "7.NUM trailing workspace";
                break;
            }
        }
    }
    if (failure) {
        if (error && error_size)
            snprintf(error, error_size,
                     "Original Revenge world resources do not match Advanced "
                     "3.3 layout (%s): %s", failure, directory);
        return 0;
    }
    return 1;
}
