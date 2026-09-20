#include "mr_assets.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct MRAssetBSave {
    uint16_t segment;
    uint16_t offset;
    uint16_t payload_bytes;
} MRAssetBSave;

static uint16_t mr_assets_u16le(const uint8_t *bytes) {
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static double mr_assets_mbf_single(const uint8_t *bytes) {
    unsigned exponent = bytes[3];
    if (!exponent) return 0.0;
    unsigned mantissa = (unsigned)bytes[0] | ((unsigned)bytes[1] << 8) |
                        ((unsigned)(bytes[2] & 0x7F) << 16);
    double value = (1.0 + mantissa / 8388608.0) *
                   ldexp(1.0, (int)exponent - 129);
    return bytes[2] & 0x80 ? -value : value;
}

static void mr_assets_error(char *error, size_t error_size,
                            const char *message, const char *path) {
    if (!error || !error_size) return;
    if (path) snprintf(error, error_size, "%s: %s", message, path);
    else snprintf(error, error_size, "%s", message);
}

static void mr_assets_path(char *out, size_t out_size, const char *directory,
                           const char *name) {
    size_t length = strlen(directory);
    const char *separator = length &&
        (directory[length - 1] == '/' || directory[length - 1] == '\\')
        ? "" : "/";
    snprintf(out, out_size, "%s%s%s", directory, separator, name);
}

static int mr_assets_load_bsave(const char *directory, const char *name,
                                uint16_t expected_offset,
                                uint16_t expected_payload, uint8_t *payload,
                                char *error, size_t error_size) {
    char path[512];
    uint8_t header[7];
    mr_assets_path(path, sizeof(path), directory, name);
    FILE *file = fopen(path, "rb");
    if (!file) {
        mr_assets_error(error, error_size,
                        "Missing original Revenge graphics resource", path);
        return 0;
    }
    int ok = fread(header, 1, sizeof(header), file) == sizeof(header) &&
             header[0] == 0xFD &&
             mr_assets_u16le(header + 3) == expected_offset &&
             mr_assets_u16le(header + 5) == expected_payload &&
             fread(payload, 1, expected_payload, file) == expected_payload;
    if (ok) {
        int final = fgetc(file);
        if (final == 0x1A) final = fgetc(file);
        ok = final == EOF;
    }
    fclose(file);
    if (!ok)
        mr_assets_error(error, error_size,
                        "Invalid Revenge graphics BSAVE layout", path);
    return ok;
}

static int mr_assets_decode_indices(const uint8_t *raw, int *indices,
                                    int maximum) {
    for (int index = 0; index < MR_ASSET_TYPE_ROWS; index++) {
        double value = mr_assets_mbf_single(raw + index * 4);
        int decoded = (int)floor(value);
        if (value != decoded || decoded < 0 || decoded > maximum) return 0;
        indices[index] = decoded;
    }
    return 1;
}

static int mr_assets_load_set(MRMonsterAssetSet *set, const char *directory,
                              const char *large_index_name,
                              const char *large_name,
                              const char *compact_index_name,
                              const char *compact_name,
                              uint16_t large_index_offset,
                              uint16_t large_sprite_offset,
                              uint16_t compact_index_offset,
                              uint16_t compact_sprite_offset, char *error,
                              size_t error_size) {
    uint8_t large_indices[MR_ASSET_TYPE_ROWS * 4];
    uint8_t compact_indices[MR_ASSET_TYPE_ROWS * 4];
    if (!mr_assets_load_bsave(directory, large_index_name,
                              large_index_offset,
                              sizeof(large_indices), large_indices, error,
                              error_size) ||
        !mr_assets_load_bsave(directory, large_name, large_sprite_offset,
                              MR_LARGE_SPRITE_PAYLOAD, set->large_raw,
                              error, error_size) ||
        !mr_assets_load_bsave(directory, compact_index_name,
                              compact_index_offset,
                              sizeof(compact_indices), compact_indices, error,
                              error_size) ||
        !mr_assets_load_bsave(directory, compact_name,
                              compact_sprite_offset,
                              MR_COMPACT_SPRITE_PAYLOAD, set->compact_raw,
                              error, error_size))
        return 0;
    if (!mr_assets_decode_indices(large_indices, set->large_art_index, 15) ||
        !mr_assets_decode_indices(compact_indices, set->compact_art_index,
                                  18)) {
        mr_assets_error(error, error_size,
                        "Invalid Revenge monster art-index table", NULL);
        return 0;
    }
    return 1;
}

int mr_assets_load(MRAssets *assets, const char *directory,
                   char *error, size_t error_size) {
    if (!assets || !directory) {
        mr_assets_error(error, error_size,
                        "Invalid Revenge asset request", NULL);
        return 0;
    }
    memset(assets, 0, sizeof(*assets));
    return mr_assets_load_set(&assets->shallow, directory,
                              "3.NUM", "4.NUM", "5.NUM", "6.NUM",
                              0x0A80, 0x2280, 0x1856, 0xAF30,
                              error, error_size) &&
           mr_assets_load_set(&assets->deep, directory,
                              "3A.NUM", "4A.NUM", "5A.NUM", "6A.NUM",
                              0x9040, 0xA950, 0x9DC0, 0xA760,
                              error, error_size);
}

static int mr_assets_bitmap(const uint8_t *raw, size_t raw_size,
                            int stride_words, int slot,
                            MRBitmapView *bitmap) {
    if (!raw || !bitmap || slot < 0) return 0;
    size_t offset = (size_t)slot * (size_t)stride_words * 2U;
    if (offset + 4 > raw_size) return 0;
    /* A QuickBASIC GET header stores horizontal size in bits. These images
     * were captured in SCREEN 1, so every visible pixel occupies two packed
     * CGA bits. */
    int width_bits = mr_assets_u16le(raw + offset);
    int height = mr_assets_u16le(raw + offset + 2);
    /* QuickBASIC GET left-justifies every scanline on a byte boundary.  The
     * fixed INTEGER-array slot may contain unused bytes after the complete
     * image, but individual rows are not word padded.  Treating them as
     * word-aligned shifted every row after the first in the original monster
     * art and produced the diagonal/block corruption visible in combat. */
    int row_bytes = (width_bits + 7) / 8;
    size_t bytes = (size_t)row_bytes * (size_t)height;
    if (width_bits <= 0 || (width_bits & 1) || height <= 0 ||
        offset + 4 + bytes > raw_size ||
        4 + bytes > (size_t)stride_words * 2U)
        return 0;
    bitmap->storage_width_bits = width_bits;
    bitmap->width = width_bits / 2;
    bitmap->height = height;
    bitmap->row_bytes = row_bytes;
    bitmap->bits = raw + offset + 4;
    bitmap->available_bytes = bytes;
    return 1;
}

int mr_assets_large_bitmap(const MRMonsterAssetSet *set, int monster_type,
                           int size_variant, MRBitmapView *bitmap) {
    if (!set || monster_type < 1 || monster_type >= MR_ASSET_TYPE_ROWS ||
        size_variant < 0 || size_variant > 1)
        return 0;
    int art = set->large_art_index[monster_type];
    if (art < 1 || art > 15) return 0;
    return mr_assets_bitmap(set->large_raw, sizeof(set->large_raw),
                            MR_LARGE_SPRITE_STRIDE_WORDS,
                            art * 2 + size_variant, bitmap);
}

int mr_assets_compact_bitmap(const MRMonsterAssetSet *set, int monster_type,
                             int distance_variant, MRBitmapView *bitmap) {
    if (!set || monster_type < 1 || monster_type >= MR_ASSET_TYPE_ROWS ||
        distance_variant < 0 || distance_variant > 2)
        return 0;
    int art = set->compact_art_index[monster_type];
    if (art < 1 || art > 18) return 0;
    return mr_assets_bitmap(set->compact_raw, sizeof(set->compact_raw),
                            MR_COMPACT_SPRITE_STRIDE_WORDS,
                            art * 3 + distance_variant, bitmap);
}

uint8_t mr_bitmap_pixel(const MRBitmapView *bitmap, int x, int y) {
    if (!bitmap || !bitmap->bits || x < 0 || y < 0 || x >= bitmap->width ||
        y >= bitmap->height)
        return 0;
    const uint8_t *row = bitmap->bits + (size_t)y * bitmap->row_bytes;
    return (uint8_t)((row[x >> 2] >> ((3 - (x & 3)) * 2)) & 3U);
}

int mr_bitmap_test_pixel(const MRBitmapView *bitmap, int x, int y) {
    return mr_bitmap_pixel(bitmap, x, y) != 0;
}

static uint32_t mr_assets_bitmap_pixel_hash(const MRBitmapView *bitmap) {
    /* FNV-1a over unpacked logical SCREEN 1 pixels. These original-resource
     * fixtures detect the byte-vs-word scanline mistake which can preserve
     * plausible dimensions and nonzero pixels while diagonally corrupting
     * every monster after its first row. */
    uint32_t hash = 2166136261U;
    if (!bitmap) return 0;
    for (int y = 0; y < bitmap->height; ++y) {
        for (int x = 0; x < bitmap->width; ++x) {
            hash ^= mr_bitmap_pixel(bitmap, x, y);
            hash *= 16777619U;
        }
    }
    return hash;
}

int mr_assets_self_test(const char *directory,
                        char *error, size_t error_size) {
    MRAssets assets;
    if (!mr_assets_load(&assets, directory, error, error_size)) return 0;
    int failed = MR_COMBAT_MONSTER_SIZE_VARIANT != 1 ||
                 assets.shallow.large_art_index[1] != 6 ||
                 assets.shallow.large_art_index[14] != 2 ||
                 assets.shallow.compact_art_index[1] != 6 ||
                 assets.shallow.compact_art_index[14] != 1 ||
                 assets.deep.large_art_index[1] != 10 ||
                 assets.deep.compact_art_index[1] != 12;
    MRBitmapView large, compact;
    failed |= !mr_assets_large_bitmap(&assets.shallow, 1, 0, &large) ||
              large.storage_width_bits != 72 || large.width != 36 ||
              large.height != 24 ||
              large.row_bytes != 9 ||
              mr_assets_bitmap_pixel_hash(&large) != 0x748D2458U;
    failed |= !mr_assets_large_bitmap(&assets.shallow, 1, 1, &large) ||
              large.storage_width_bits != 54 || large.width != 27 ||
              large.height != 19 ||
              large.row_bytes != 7 ||
              mr_assets_bitmap_pixel_hash(&large) != 0x7DEA913DU;
    /* 6C62 selects this exact second large record for the monster sharing
     * the player's cell; keep the combat call-site contract tied to the
     * original-resource fixture rather than only testing both slots. */
    failed |= !mr_assets_large_bitmap(
                  &assets.shallow, 1, MR_COMBAT_MONSTER_SIZE_VARIANT,
                  &large) ||
              mr_assets_bitmap_pixel_hash(&large) != 0x7DEA913DU;
    failed |= !mr_assets_compact_bitmap(&assets.shallow, 1, 0, &compact) ||
              compact.storage_width_bits != 40 || compact.width != 20 ||
              compact.height != 14 ||
              compact.row_bytes != 5 ||
              mr_assets_bitmap_pixel_hash(&compact) != 0x925FCA78U;
    failed |= !mr_assets_compact_bitmap(&assets.shallow, 1, 2, &compact) ||
              compact.storage_width_bits != 24 || compact.width != 12 ||
              compact.height != 9 ||
              compact.row_bytes != 3 ||
              mr_assets_bitmap_pixel_hash(&compact) != 0xCFC0E756U;
    failed |= !mr_assets_large_bitmap(&assets.deep, 1, 0, &large) ||
              mr_assets_bitmap_pixel_hash(&large) != 0x87341AE6U;
    failed |= !mr_assets_compact_bitmap(&assets.deep, 1, 2, &compact) ||
              mr_assets_bitmap_pixel_hash(&compact) != 0x9DB5AADCU;
    int set_pixels = 0, colored_pixels = 0;
    for (int y = 0; y < compact.height; y++)
        for (int x = 0; x < compact.width; x++)
            if (mr_bitmap_pixel(&compact, x, y)) {
                set_pixels++;
                colored_pixels += mr_bitmap_pixel(&compact, x, y) > 1;
            }
    failed |= set_pixels <= 0 || set_pixels >= compact.width * compact.height ||
              colored_pixels <= 0;
    if (failed) {
        mr_assets_error(error, error_size,
                        "DUNSMALL monster graphics-resource mismatch", NULL);
        return 0;
    }
    return 1;
}
