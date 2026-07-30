#include "mw_integrity.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* MW_PLATFORM_REPLACEMENT: original func_08BDF copy-protection/startup check
 * is intentionally replaced by deterministic size, CRC-32, and SHA-256
 * validation of the two locally supplied original executables. */

typedef struct Sha256Context {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    size_t block_used;
} Sha256Context;

typedef struct RequiredExecutableVariant {
    const char *name;
    uint64_t size;
    uint32_t crc32;
    uint8_t digest[32];
} RequiredExecutableVariant;

/* Approved executable variants from original Moraff's World distributions.
   Requiring MW.EXE plus one known WORLD.EXE prevents the native port from
   being distributed or launched without the original game. */
static const RequiredExecutableVariant executable_variants[] = {
    {
        "MW.EXE", 12823, 0x30c074b7,
        {0x6e,0xa1,0xa4,0x30,0xae,0x34,0x18,0x53,
         0x99,0xcf,0x3c,0x19,0xac,0xfa,0xdd,0xec,
         0x8d,0xc5,0x2a,0x20,0xd5,0x9e,0xed,0x91,
         0x72,0xf2,0x66,0xc5,0xef,0x78,0x58,0xb9}
    },
    {
        "WORLD.EXE", 229480, 0x9aba4217,
        {0x04,0xad,0xd8,0xaa,0x22,0x94,0x78,0x96,
         0xa5,0xa5,0x3d,0x76,0x98,0xdb,0x92,0xce,
         0x33,0xf4,0xc9,0xaf,0xee,0x9d,0x23,0x83,
         0x1b,0x37,0xe4,0x04,0x16,0x09,0x23,0x65}
    },
    {
        "WORLD.EXE", 104316, 0x2fdc68f1,
        {0xdc,0x5d,0xc9,0x18,0x02,0x8a,0xa3,0x6f,
         0xfa,0x63,0x72,0x3b,0xd1,0x49,0xcf,0x3b,
         0xac,0x89,0xcc,0x70,0x5c,0x31,0xe3,0xf4,
         0x4a,0x77,0x9c,0x13,0xfb,0xc7,0xca,0x80}
    }
};

static const char *const required_executable_names[] = {
    "MW.EXE", "WORLD.EXE"
};

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t rotate_right(uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32 - bits));
}

static void sha256_transform(Sha256Context *ctx, const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        int p = i * 4;
        w[i] = ((uint32_t)block[p] << 24) |
               ((uint32_t)block[p + 1] << 16) |
               ((uint32_t)block[p + 2] << 8) |
               block[p + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotate_right(w[i - 15], 7) ^
                      rotate_right(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotate_right(w[i - 2], 17) ^
                      rotate_right(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx->state[0], b = ctx->state[1];
    uint32_t c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5];
    uint32_t g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choose = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + choose + sha256_k[i] + w[i];
        uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + majority;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(Sha256Context *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(Sha256Context *ctx, const uint8_t *data, size_t size) {
    ctx->bit_count += (uint64_t)size * 8;
    while (size) {
        size_t room = 64 - ctx->block_used;
        size_t take = size < room ? size : room;
        memcpy(ctx->block + ctx->block_used, data, take);
        ctx->block_used += take;
        data += take;
        size -= take;
        if (ctx->block_used == 64) {
            sha256_transform(ctx, ctx->block);
            ctx->block_used = 0;
        }
    }
}

static void sha256_finish(Sha256Context *ctx, uint8_t digest[32]) {
    uint64_t bits = ctx->bit_count;
    uint8_t pad = 0x80;
    sha256_update(ctx, &pad, 1);
    uint8_t zero = 0;
    while (ctx->block_used != 56) sha256_update(ctx, &zero, 1);
    uint8_t length[8];
    for (int i = 0; i < 8; i++)
        length[7 - i] = (uint8_t)(bits >> (i * 8));
    sha256_update(ctx, length, sizeof(length));
    for (int i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)ctx->state[i];
    }
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size) {
    while (size--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1));
    }
    return crc;
}

static int hash_file(const char *path, uint8_t digest[32], uint64_t *size,
                     uint32_t *crc32) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    Sha256Context ctx;
    sha256_init(&ctx);
    uint8_t buffer[8192];
    uint64_t total = 0;
    uint32_t crc = UINT32_MAX;
    for (;;) {
        size_t got = fread(buffer, 1, sizeof(buffer), file);
        if (got) {
            sha256_update(&ctx, buffer, got);
            crc = crc32_update(crc, buffer, got);
            total += got;
        }
        if (got < sizeof(buffer)) {
            if (ferror(file)) { fclose(file); return 0; }
            break;
        }
    }
    fclose(file);
    sha256_finish(&ctx, digest);
    if (size) *size = total;
    if (crc32) *crc32 = ~crc;
    return 1;
}

int integrity_verify_original_executables(const char *directory,
                                          char *error, size_t error_size) {
    if (error && error_size) error[0] = '\0';
    for (size_t i = 0; i < sizeof(required_executable_names) /
                            sizeof(required_executable_names[0]); i++) {
        const char *required_name = required_executable_names[i];
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", directory, required_name);
        uint8_t actual[32];
        uint64_t actual_size = 0;
        uint32_t actual_crc32 = 0;
        if (!hash_file(path, actual, &actual_size, &actual_crc32)) {
            if (error && error_size)
                snprintf(error, error_size,
                         "%s is missing. Copy the original executable beside moraffs_world.exe.",
                         required_name);
            return 0;
        }
        int matched = 0;
        for (size_t variant_index = 0;
             variant_index < sizeof(executable_variants) /
                             sizeof(executable_variants[0]);
             variant_index++) {
            const RequiredExecutableVariant *variant =
                &executable_variants[variant_index];
            if (strcmp(required_name, variant->name) == 0 &&
                actual_size == variant->size &&
                actual_crc32 == variant->crc32 &&
                memcmp(actual, variant->digest, sizeof(actual)) == 0) {
                matched = 1;
                break;
            }
        }
        if (!matched) {
            if (error && error_size)
                snprintf(error, error_size,
                         "%s is not an approved original executable (size %llu, CRC-32 %08X).",
                         required_name, (unsigned long long)actual_size,
                         (unsigned)actual_crc32);
            return 0;
        }
    }
    return 1;
}
