#ifndef MR_ASSETS_H
#define MR_ASSETS_H

#include <stddef.h>
#include <stdint.h>

#define MR_ASSET_TYPE_ROWS 23
#define MR_LARGE_SPRITE_STRIDE_WORDS 125
#define MR_LARGE_SPRITE_SLOTS 32
#define MR_COMPACT_SPRITE_STRIDE_WORDS 45
#define MR_COMPACT_SPRITE_SLOTS 57
#define MR_LARGE_SPRITE_PAYLOAD 7999
#define MR_COMPACT_SPRITE_PAYLOAD 5129
/* DUNSMALL 6C62-6C7D assigns BASIC size selector one before every
 * player-cell/combat PUT.  Selector zero is reserved for the nearest
 * projected (one-cell-away) monster. */
#define MR_COMBAT_MONSTER_SIZE_VARIANT 1

typedef struct MRBitmapView {
    int storage_width_bits;
    int width;
    int height;
    int row_bytes;
    const uint8_t *bits;
    size_t available_bytes;
} MRBitmapView;

typedef struct MRMonsterAssetSet {
    int large_art_index[MR_ASSET_TYPE_ROWS];
    int compact_art_index[MR_ASSET_TYPE_ROWS];
    uint8_t large_raw[MR_LARGE_SPRITE_PAYLOAD];
    uint8_t compact_raw[MR_COMPACT_SPRITE_PAYLOAD];
} MRMonsterAssetSet;

typedef struct MRAssets {
    MRMonsterAssetSet shallow;
    MRMonsterAssetSet deep;
} MRAssets;

int mr_assets_load(MRAssets *assets, const char *directory,
                   char *error, size_t error_size);
/* The first resource maps each monster type to one of fifteen art groups;
 * every group has two fixed-stride SCREEN 1 GET images. */
int mr_assets_large_bitmap(const MRMonsterAssetSet *set, int monster_type,
                           int size_variant, MRBitmapView *bitmap);
/* The second resource maps to one of eighteen art groups, each with three
 * progressively smaller fixed-stride SCREEN 1 GET images. */
int mr_assets_compact_bitmap(const MRMonsterAssetSet *set, int monster_type,
                             int distance_variant, MRBitmapView *bitmap);
uint8_t mr_bitmap_pixel(const MRBitmapView *bitmap, int x, int y);
int mr_bitmap_test_pixel(const MRBitmapView *bitmap, int x, int y);

int mr_assets_self_test(const char *directory,
                        char *error, size_t error_size);

#endif
