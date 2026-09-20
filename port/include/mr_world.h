#ifndef MR_WORLD_H
#define MR_WORLD_H

#include <stddef.h>
#include <stdint.h>

#define MR_DUNGEON_LEVELS 70
#define MR_SPECIAL_CELL_LEVELS 71
#define MR_DUNGEON_ROWS 21
#define MR_MONSTER_SLOTS_PER_LEVEL 40
#define MR_MONSTER_TABLE_INDEX_COUNT \
    (MR_DUNGEON_LEVELS * MR_MONSTER_SLOTS_PER_LEVEL + 1)
#define MR_MONSTER_TABLE_PAYLOAD_BYTES 5601
#define MR_SPECIAL_CELL_PAYLOAD_BYTES 6045
#define MR_SPECIAL_CELL_TRAILING_BYTES 81

typedef struct MRBSaveHeader {
    uint16_t segment;
    uint16_t offset;
    uint16_t payload_bytes;
    int dos_eof;
} MRBSaveHeader;

typedef struct MRWorldData {
    MRBSaveHeader monster_positions_header;
    MRBSaveHeader monster_health_header;
    MRBSaveHeader special_cells_header;

    /* DUNSMALL indexes both integer tables from 1 through 70*40. The files
     * contain 2,800 complete little-endian words plus the low byte of index
     * 2,800. The original BLOAD leaves its high byte zero in DGROUP, so the
     * final native element is reconstructed with a zero high byte. */
    uint16_t monster_positions[MR_MONSTER_TABLE_INDEX_COUNT];
    int16_t monster_health[MR_MONSTER_TABLE_INDEX_COUNT];
    uint8_t monster_positions_raw[MR_MONSTER_TABLE_PAYLOAD_BYTES];
    uint8_t monster_health_raw[MR_MONSTER_TABLE_PAYLOAD_BYTES];

    /* 7.NUM is 71*21 MBF single-precision sparse special-cell masks followed
     * by 81 zero workspace bytes. Ordinary maze walls are instead produced
     * by DUNSMALL's deterministic procedural wall function. */
    uint32_t special_cells[MR_SPECIAL_CELL_LEVELS][MR_DUNGEON_ROWS];
    uint8_t special_cells_raw[MR_SPECIAL_CELL_PAYLOAD_BYTES];
    uint8_t special_cells_tail[MR_SPECIAL_CELL_TRAILING_BYTES];
} MRWorldData;

typedef enum MRDirection {
    MR_DIRECTION_NORTH = 1,
    MR_DIRECTION_EAST = 2,
    MR_DIRECTION_SOUTH = 3,
    MR_DIRECTION_WEST = 4
} MRDirection;

typedef enum MRMoveResult {
    MR_MOVE_WALL = 0,
    MR_MOVE_OK = 1,
    MR_MOVE_BOUNDARY = 2
} MRMoveResult;

/* Exact DS:B4C6 values produced by DUNSMALL's vertical-feature resolver.
 * Negative values are ladders going up by 1..3 levels; positive 1..3 values
 * are ladders going down. Zero is an untriggered chute on positive floors.
 * Floor-zero reciprocal endpoints are exposed as positive down ladders so
 * the player can stand on their filled-square map marks and press D. Surface
 * overshoots retain the original numeric zero/circular mark but are inert.
 * Value 25 is the false-floor sentinel and 50 means no vertical feature. */
#define MR_VERTICAL_CHUTE 0
#define MR_VERTICAL_FALSE_FLOOR 25
#define MR_VERTICAL_NONE 50

int mr_world_load(MRWorldData *world, const char *directory,
                  char *error, size_t error_size);
/* Persist the two mutable global monster tables using their original BSAVE
 * headers/layout.  The caller supplies an isolated native-save directory;
 * the original installation is treated as a read-only seed. */
int mr_world_write_monsters(const MRWorldData *world, const char *directory,
                            char *error, size_t error_size);
int mr_world_special_cell_bit(const MRWorldData *world,
                              int level, int x, int y);
int mr_world_procedural_wall_value(int dungeon_level,
                                   float dungeon_generation_seed,
                                   float factor_a, float factor_b,
                                   float factor_c);
/* DUNSMALL 5AE1-6104 queries this same expression for the center, left, and
 * right edges of every projected corridor cell. Values 8..9 are blocking;
 * values 0..7 remain traversable but are still significant to rendering. */
int mr_world_wall_value_for_step(int dungeon_level,
                                 float dungeon_generation_seed,
                                 int x, int y, MRDirection direction);
int mr_world_vertical_feature_hash(int dungeon_level, int x, int y,
                                   int level_search_offset);
int mr_world_resolve_vertical_feature(const MRWorldData *world,
                                      int dungeon_level, int x, int y);
MRMoveResult mr_world_try_move(int dungeon_level,
                               float dungeon_generation_seed,
                               MRDirection direction, int *x, int *y);
int mr_world_self_test(const char *directory,
                       char *error, size_t error_size);

#endif
