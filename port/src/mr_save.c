#include "mr_save.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MR_BIN_PAYLOAD_BYTES 6045
#define MR_BIN_GRID_BYTES (MR_EXPLORED_LEVELS * MR_EXPLORED_ROWS * 4)
#define MR_SAVE_LINE_MAX 512

static void mr_save_error(char *error, size_t error_size, const char *message,
                          const char *path) {
    if (!error || !error_size) return;
    if (path) snprintf(error, error_size, "%s: %s", message, path);
    else snprintf(error, error_size, "%s", message);
}

static uint16_t mr_u16le(const uint8_t *bytes) {
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static void mr_put_u16le(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value & 0xFF);
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

static unsigned mr_round_mantissa_nearest_even(double value) {
    double integral = floor(value);
    double fraction = value - integral;
    unsigned rounded = (unsigned)integral;
    if (fraction > 0.5 || (fraction == 0.5 && (rounded & 1U))) rounded++;
    return rounded;
}

static int mr_encode_mbf_single(double value, uint8_t bytes[4]) {
    memset(bytes, 0, 4);
    if (value == 0.0) return 1;
    if (!isfinite(value)) return 0;
    int negative = value < 0.0;
    double magnitude = fabs(value);
    int exponent2 = 0;
    double fraction = frexp(magnitude, &exponent2);
    int mbf_exponent = exponent2 + 128;
    if (mbf_exponent <= 0 || mbf_exponent > 255) return 0;
    double normalized = fraction * 2.0;
    unsigned mantissa = mr_round_mantissa_nearest_even(
        (normalized - 1.0) * 8388608.0);
    if (mantissa >= 8388608U) {
        mantissa = 0;
        if (++mbf_exponent > 255) return 0;
    }
    bytes[0] = (uint8_t)(mantissa & 0xFF);
    bytes[1] = (uint8_t)((mantissa >> 8) & 0xFF);
    bytes[2] = (uint8_t)((mantissa >> 16) & 0x7F);
    if (negative) bytes[2] |= 0x80;
    bytes[3] = (uint8_t)mbf_exponent;
    return 1;
}

static int mr_read_numeric_line(FILE *file, char *line, size_t line_size,
                                int *dos_eof) {
    while (fgets(line, (int)line_size, file)) {
        size_t length = strlen(line);
        while (length && (line[length - 1] == '\r' ||
                          line[length - 1] == '\n'))
            line[--length] = '\0';
        if (length == 1 && (unsigned char)line[0] == 0x1A) {
            if (dos_eof) *dos_eof = 1;
            continue;
        }
        if (length && (unsigned char)line[length - 1] == 0x1A) {
            line[--length] = '\0';
            if (dos_eof) *dos_eof = 1;
        }
        if (length) return 1;
    }
    return 0;
}

static int mr_parse_values(const char *line, double *values, int count) {
    /* QuickBASIC WRITE # uses D exponents for DOUBLE values. C's strtod
     * accepts E but not D, so normalize only that exponent marker before
     * parsing the otherwise numeric record. */
    char normalized[MR_SAVE_LINE_MAX];
    size_t length = strlen(line);
    if (length >= sizeof(normalized)) return 0;
    memcpy(normalized, line, length + 1);
    for (size_t index = 0; index < length; ++index) {
        if (normalized[index] == 'D') normalized[index] = 'E';
        else if (normalized[index] == 'd') normalized[index] = 'e';
    }
    const char *cursor = normalized;
    for (int index = 0; index < count; index++) {
        char *end = NULL;
        errno = 0;
        double value = strtod(cursor, &end);
        if (errno || end == cursor || !isfinite(value)) return 0;
        while (*end == ' ' || *end == '\t') end++;
        if (index + 1 < count) {
            if (*end != ',') return 0;
            cursor = end + 1;
        } else {
            if (*end != '\0') return 0;
        }
        values[index] = value;
    }
    return 1;
}

static int mr_read_record(FILE *file, int record, double *values, int count,
                          int *dos_eof, const char *path,
                          char *error, size_t error_size) {
    char line[MR_SAVE_LINE_MAX];
    if (!mr_read_numeric_line(file, line, sizeof(line), dos_eof)) {
        if (error && error_size)
            snprintf(error, error_size, "%s: missing record %d", path, record);
        return 0;
    }
    if (!mr_parse_values(line, values, count)) {
        if (error && error_size)
            snprintf(error, error_size, "%s: malformed record %d", path, record);
        return 0;
    }
    return 1;
}

static int mr_load_text(MRSave *save, const char *path,
                        char *error, size_t error_size) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        mr_save_error(error, error_size, "Cannot open Revenge character save", path);
        return 0;
    }
    MRCharacter *value = &save->stored;
    int record = 1;
    int ok = 1;
    double fields[10];
    for (int i = 0; ok && i < MR_SAVE_ATTRIBUTES; i++, record++) {
        ok = mr_read_record(file, record, fields, 1, &save->text_dos_eof,
                            path, error, error_size);
        if (ok) value->attributes[i] = (float)fields[0];
    }
    ok = ok && mr_read_record(file, record++, fields, 5,
                              &save->text_dos_eof, path, error, error_size);
    if (ok) {
        value->combat_attack_factor = (float)fields[0];
        value->health_growth_factor = (float)fields[1];
        value->agility_defense_factor = (float)fields[2];
        value->player_class = (float)fields[3];
        value->equipped_armor = (float)fields[4];
    }
    ok = ok && mr_read_record(file, record++, fields, 5,
                              &save->text_dos_eof, path, error, error_size);
    if (ok) {
        value->experience = fields[0];
        value->player_level = (float)fields[1];
        value->health_max = (float)fields[2];
        value->health_current = (float)fields[3];
        value->status_flags = (float)fields[4];
    }
    ok = ok && mr_read_record(file, record++, fields, 10,
                              &save->text_dos_eof, path, error, error_size);
    if (ok) {
        value->carried_weight = (float)fields[0];
        value->carried_treasure = (float)fields[1];
        value->pocket_money = fields[2];
        value->bank_money = fields[3];
        value->pending_experience = (float)fields[4];
        value->spell_points = (float)fields[5];
        value->player_x = (float)fields[6];
        value->player_y = (float)fields[7];
        value->dungeon_level = (float)fields[8];
        value->dungeon_generation_seed = (float)fields[9];
    }
    for (int i = 0; ok && i < MR_SAVE_TABLE_SIZE; i++, record++) {
        ok = mr_read_record(file, record, fields, 1, &save->text_dos_eof,
                            path, error, error_size);
        if (ok) value->active_effects[i] = (float)fields[0];
    }
    for (int i = 0; ok && i < MR_SAVE_TABLE_SIZE; i++, record++) {
        ok = mr_read_record(file, record, fields, 1, &save->text_dos_eof,
                            path, error, error_size);
        if (ok) value->magic_item_values[i] = (float)fields[0];
    }
    for (int i = 0; ok && i < MR_SAVE_TABLE_SIZE; i++, record++) {
        ok = mr_read_record(file, record, fields, 1, &save->text_dos_eof,
                            path, error, error_size);
        if (ok) value->carried_items[i] = (float)fields[0];
    }
    for (int i = 0; ok && i < MR_SAVE_SPELL_MASK_ROWS; i++, record++) {
        ok = mr_read_record(file, record, fields, 2, &save->text_dos_eof,
                            path, error, error_size);
        if (ok) {
            value->spellbook_masks[i][0] = (float)fields[0];
            value->spellbook_masks[i][1] = (float)fields[1];
        }
    }
    for (int i = 0; ok && i < MR_SAVE_PERSISTENT_VALUES; i++, record++) {
        ok = mr_read_record(file, record, fields, 1, &save->text_dos_eof,
                            path, error, error_size);
        if (ok) value->persistent_values[i] = (float)fields[0];
    }
    if (ok) {
        char extra[MR_SAVE_LINE_MAX];
        if (mr_read_numeric_line(file, extra, sizeof(extra), &save->text_dos_eof)) {
            mr_save_error(error, error_size,
                          "Revenge character save has more than 311 records", path);
            ok = 0;
        }
    }
    fclose(file);
    if (!ok || record != MR_SAVE_RECORDS + 1) return 0;

    save->character = save->stored;
    for (int i = 0; i < MR_SAVE_ATTRIBUTES; i++)
        save->character.attributes[i] =
            (save->stored.attributes[i] - 237.0f) / 3.0f;
    save->character.experience = save->stored.experience - 12316.0;
    save->character.player_level = save->stored.player_level - 476.0f;
    save->character.health_max = save->stored.health_max - 376.0f;
    save->character.health_current = save->stored.health_current - 176.0f;
    save->character.carried_weight = save->stored.carried_weight - 71.0f;
    save->character.carried_treasure = save->stored.carried_treasure - 4434.0f;
    save->character.pocket_money = save->stored.pocket_money - 223.0;
    return 1;
}

static int mr_load_binary(MRSave *save, const char *path,
                          char *error, size_t error_size) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        mr_save_error(error, error_size, "Cannot open Revenge BSAVE sidecar", path);
        return 0;
    }
    uint8_t header[7];
    uint8_t payload[MR_BIN_PAYLOAD_BYTES];
    int ok = fread(header, 1, sizeof(header), file) == sizeof(header);
    if (!ok || header[0] != 0xFD || mr_u16le(header + 5) != MR_BIN_PAYLOAD_BYTES) {
        fclose(file);
        mr_save_error(error, error_size, "Invalid Revenge BSAVE header", path);
        return 0;
    }
    if (fread(payload, 1, sizeof(payload), file) != sizeof(payload)) {
        fclose(file);
        mr_save_error(error, error_size, "Truncated Revenge BSAVE payload", path);
        return 0;
    }
    int final = fgetc(file);
    if (final == 0x1A) {
        save->binary_dos_eof = 1;
        final = fgetc(file);
    }
    if (final != EOF) {
        fclose(file);
        mr_save_error(error, error_size, "Unexpected data after Revenge BSAVE", path);
        return 0;
    }
    fclose(file);
    save->bsave_segment = mr_u16le(header + 1);
    save->bsave_offset = mr_u16le(header + 3);
    /* DUNSMALL B964-B97B supplies DS:9B06 explicitly to QuickBASIC's BLOAD.
     * That makes the segment/offset stored in the BSAVE header informational;
     * it is not the destination used by the original game. The supplied
     * character set proves this matters: valid sidecars use 9B06, 4020, and
     * 4050. Preserve those words for byte-exact writes, but validate the
     * payload itself instead of rejecting a valid original character. */
    for (int level = 0; level < MR_EXPLORED_LEVELS; level++) {
        for (int row = 0; row < MR_EXPLORED_ROWS; row++) {
            int index = level * MR_EXPLORED_ROWS + row;
            const uint8_t *source = payload + index * 4;
            memcpy(save->explored_raw[level][row], source, 4);
            double decoded = mr_decode_mbf_single(source);
            long rounded = lround(decoded);
            if (fabs(decoded - rounded) > 0.0001 || rounded < 0 ||
                rounded >= (1L << 21)) {
                mr_save_error(error, error_size,
                              "Invalid explored-cell bit mask in Revenge BSAVE", path);
                return 0;
            }
            save->explored[level][row] = (uint32_t)rounded;
        }
    }
    memcpy(save->binary_tail, payload + MR_BIN_GRID_BYTES,
           MR_BIN_TRAILING_BYTES);
    return 1;
}

int mr_save_load(MRSave *save, const char *text_path, const char *binary_path,
                 char *error, size_t error_size) {
    if (!save || !text_path || !binary_path) {
        mr_save_error(error, error_size, "Invalid Revenge save request", NULL);
        return 0;
    }
    memset(save, 0, sizeof(*save));
    return mr_load_text(save, text_path, error, error_size) &&
           mr_load_binary(save, binary_path, error, error_size);
}

static int mr_qb_numeric_text(double input, int is_double,
                              char *output, size_t output_size) {
    /* BRUN30 B235-B2C0, after AA2D/ABA1 have produced and rounded the decimal
     * digit run.  QuickBASIC uses seven significant digits for SINGLE and
     * sixteen for DOUBLE, removes trailing zeroes, chooses fixed notation only
     * when both decimal extents fit that precision, omits the zero before a
     * leading decimal point, and uses E (SINGLE) versus D (DOUBLE).  C's %g is
     * observably different at every one of those boundaries. */
    if (!output || output_size < 2 || !isfinite(input)) return 0;

    const int precision = is_double ? 16 : 7;
    const double value = is_double ? input : (double)(float)input;
    if (value == 0.0) {
        output[0] = '0';
        output[1] = '\0';
        return 1;
    }

    char scientific[96];
    if (snprintf(scientific, sizeof(scientific), "%.*e", precision - 1,
                 fabs(value)) < 0)
        return 0;
    const char *exponent_marker = strchr(scientific, 'e');
    if (!exponent_marker) exponent_marker = strchr(scientific, 'E');
    if (!exponent_marker) return 0;

    char digits[32];
    int digit_count = 0;
    for (const char *cursor = scientific;
         cursor < exponent_marker && digit_count < (int)sizeof(digits) - 1;
         ++cursor) {
        if (*cursor >= '0' && *cursor <= '9')
            digits[digit_count++] = *cursor;
    }
    while (digit_count > 1 && digits[digit_count - 1] == '0') digit_count--;
    digits[digit_count] = '\0';
    if (!digit_count) return 0;

    const int decimal_position = (int)strtol(exponent_marker + 1, NULL, 10) + 1;
    const int fractional_extent = decimal_position - digit_count;
    const int use_fixed = abs(fractional_extent) <= precision &&
                          decimal_position <= precision;
    size_t used = 0;
#define MR_QB_APPEND(character)                                                \
    do {                                                                       \
        if (used + 1 >= output_size) return 0;                                 \
        output[used++] = (character);                                          \
    } while (0)

    if (value < 0.0) MR_QB_APPEND('-');
    if (use_fixed) {
        if (decimal_position <= 0) {
            MR_QB_APPEND('.');
            for (int zero = decimal_position; zero < 0; ++zero)
                MR_QB_APPEND('0');
            for (int digit = 0; digit < digit_count; ++digit)
                MR_QB_APPEND(digits[digit]);
        } else {
            int digit = 0;
            for (; digit < digit_count && digit < decimal_position; ++digit)
                MR_QB_APPEND(digits[digit]);
            for (; digit < decimal_position; ++digit) MR_QB_APPEND('0');
            if (digit < digit_count) {
                MR_QB_APPEND('.');
                for (; digit < digit_count; ++digit)
                    MR_QB_APPEND(digits[digit]);
            }
        }
    } else {
        MR_QB_APPEND(digits[0]);
        if (digit_count > 1) {
            MR_QB_APPEND('.');
            for (int digit = 1; digit < digit_count; ++digit)
                MR_QB_APPEND(digits[digit]);
        }
        const int exponent = decimal_position - 1;
        MR_QB_APPEND(is_double ? 'D' : 'E');
        MR_QB_APPEND(exponent < 0 ? '-' : '+');
        const int magnitude = abs(exponent);
        if (magnitude >= 100) MR_QB_APPEND((char)('0' + magnitude / 100));
        MR_QB_APPEND((char)('0' + (magnitude / 10) % 10));
        MR_QB_APPEND((char)('0' + magnitude % 10));
    }
    output[used] = '\0';
#undef MR_QB_APPEND
    return 1;
}

static int mr_print_values(FILE *file, const double *values, int count,
                           unsigned double_mask) {
    for (int index = 0; index < count; index++) {
        if (index && fputc(',', file) == EOF) return 0;
        char numeric[96];
        if (!mr_qb_numeric_text(values[index],
                                (double_mask & (1U << index)) != 0,
                                numeric, sizeof(numeric)) ||
            fputs(numeric, file) < 0)
            return 0;
    }
    return fputs("\r\n", file) >= 0;
}

static int mr_write_text(const MRSave *save, const char *path,
                         char *error, size_t error_size) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        mr_save_error(error, error_size, "Cannot create Revenge character save", path);
        return 0;
    }
    MRCharacter value = save->character;
    for (int i = 0; i < MR_SAVE_ATTRIBUTES; i++)
        value.attributes[i] = value.attributes[i] * 3.0f + 237.0f;
    value.experience += 12316.0;
    value.player_level += 476.0f;
    value.health_max += 376.0f;
    value.health_current += 176.0f;
    value.carried_weight += 71.0f;
    value.carried_treasure += 4434.0f;
    value.pocket_money += 223.0;
    int ok = 1;
    double fields[10];
    for (int i = 0; ok && i < MR_SAVE_ATTRIBUTES; i++) {
        fields[0] = value.attributes[i];
        ok = mr_print_values(file, fields, 1, 0);
    }
    fields[0] = value.combat_attack_factor;
    fields[1] = value.health_growth_factor;
    fields[2] = value.agility_defense_factor;
    fields[3] = value.player_class;
    fields[4] = value.equipped_armor;
    ok = ok && mr_print_values(file, fields, 5, 0);
    fields[0] = value.experience;
    fields[1] = value.player_level;
    fields[2] = value.health_max;
    fields[3] = value.health_current;
    fields[4] = value.status_flags;
    ok = ok && mr_print_values(file, fields, 5, 1U << 0);
    fields[0] = value.carried_weight;
    fields[1] = value.carried_treasure;
    fields[2] = value.pocket_money;
    fields[3] = value.bank_money;
    fields[4] = value.pending_experience;
    fields[5] = value.spell_points;
    fields[6] = value.player_x;
    fields[7] = value.player_y;
    fields[8] = value.dungeon_level;
    fields[9] = value.dungeon_generation_seed;
    ok = ok && mr_print_values(file, fields, 10,
                               (1U << 2) | (1U << 3));
    for (int i = 0; ok && i < MR_SAVE_TABLE_SIZE; i++) {
        fields[0] = value.active_effects[i];
        ok = mr_print_values(file, fields, 1, 0);
    }
    for (int i = 0; ok && i < MR_SAVE_TABLE_SIZE; i++) {
        fields[0] = value.magic_item_values[i];
        ok = mr_print_values(file, fields, 1, 0);
    }
    for (int i = 0; ok && i < MR_SAVE_TABLE_SIZE; i++) {
        fields[0] = value.carried_items[i];
        ok = mr_print_values(file, fields, 1, 0);
    }
    for (int i = 0; ok && i < MR_SAVE_SPELL_MASK_ROWS; i++) {
        fields[0] = value.spellbook_masks[i][0];
        fields[1] = value.spellbook_masks[i][1];
        ok = mr_print_values(file, fields, 2, 0);
    }
    for (int i = 0; ok && i < MR_SAVE_PERSISTENT_VALUES; i++) {
        fields[0] = value.persistent_values[i];
        ok = mr_print_values(file, fields, 1, 0);
    }
    /* QuickBASIC's original sequential files always terminate with DOS EOF. */
    if (ok) ok = fputc(0x1A, file) != EOF;
    if (fclose(file) != 0) ok = 0;
    if (!ok) mr_save_error(error, error_size, "Cannot write Revenge character save", path);
    return ok;
}

static int mr_write_binary(const MRSave *save, const char *path,
                           char *error, size_t error_size) {
    uint8_t header[7] = {0xFD, 0, 0, 0, 0, 0, 0};
    uint8_t payload[MR_BIN_PAYLOAD_BYTES];
    mr_put_u16le(header + 1, save->bsave_segment);
    mr_put_u16le(header + 3, save->bsave_offset);
    mr_put_u16le(header + 5, MR_BIN_PAYLOAD_BYTES);
    for (int level = 0; level < MR_EXPLORED_LEVELS; level++) {
        for (int row = 0; row < MR_EXPLORED_ROWS; row++) {
            uint32_t mask = save->explored[level][row];
            uint8_t *destination =
                payload + (level * MR_EXPLORED_ROWS + row) * 4;
            const uint8_t *original = save->explored_raw[level][row];
            long original_value = lround(mr_decode_mbf_single(original));
            if (mask < (1U << 21) && original_value == (long)mask) {
                memcpy(destination, original, 4);
            } else if (mask >= (1U << 21) ||
                       !mr_encode_mbf_single((double)mask, destination)) {
                mr_save_error(error, error_size,
                              "Explored-cell mask cannot be encoded", path);
                return 0;
            }
        }
    }
    memcpy(payload + MR_BIN_GRID_BYTES, save->binary_tail,
           MR_BIN_TRAILING_BYTES);
    FILE *file = fopen(path, "wb");
    if (!file) {
        mr_save_error(error, error_size, "Cannot create Revenge BSAVE sidecar", path);
        return 0;
    }
    int ok = fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
             fwrite(payload, 1, sizeof(payload), file) == sizeof(payload) &&
             fputc(0x1A, file) != EOF;
    if (fclose(file) != 0) ok = 0;
    if (!ok) mr_save_error(error, error_size, "Cannot write Revenge BSAVE sidecar", path);
    return ok;
}

int mr_save_write(const MRSave *save, const char *text_path,
                  const char *binary_path, char *error, size_t error_size) {
    if (!save || !text_path || !binary_path) {
        mr_save_error(error, error_size, "Invalid Revenge save request", NULL);
        return 0;
    }
    return mr_write_text(save, text_path, error, error_size) &&
           mr_write_binary(save, binary_path, error, error_size);
}

static void mr_join_path(char *out, size_t out_size, const char *directory,
                         const char *name) {
    size_t length = strlen(directory);
    const char *separator = length && (directory[length - 1] == '/' ||
                                      directory[length - 1] == '\\') ? "" : "/";
    snprintf(out, out_size, "%s%s%s", directory, separator, name);
}

static int mr_files_equal(const char *left, const char *right) {
    FILE *a = fopen(left, "rb");
    FILE *b = fopen(right, "rb");
    if (!a || !b) {
        if (a) fclose(a);
        if (b) fclose(b);
        return 0;
    }
    int equal = 1;
    for (;;) {
        int ca = fgetc(a), cb = fgetc(b);
        if (ca != cb) { equal = 0; break; }
        if (ca == EOF) break;
    }
    fclose(a);
    fclose(b);
    return equal;
}

int mr_save_self_test(const char *directory, char *error, size_t error_size) {
    char text[512], binary[512];
    char numeric[96];
    uint8_t mbf_tie[4];
    if (!mr_encode_mbf_single(1.0 + 0.5 / 8388608.0, mbf_tie) ||
        mbf_tie[0] != 0 || mbf_tie[1] != 0 ||
        (mbf_tie[2] & 0x7f) != 0 || mbf_tie[3] != 129 ||
        !mr_encode_mbf_single(1.0 + 1.5 / 8388608.0, mbf_tie) ||
        mbf_tie[0] != 2 || mbf_tie[1] != 0 ||
        (mbf_tie[2] & 0x7f) != 0 || mbf_tie[3] != 129) {
        mr_save_error(error, error_size,
                      "QuickBASIC MBF tie-to-even encoding mismatch", NULL);
        return 0;
    }
    const struct {
        double value;
        int is_double;
        const char *expected;
    } numeric_cases[] = {
        {0.0, 0, "0"},
        {6.3, 0, "6.3"},
        {-0.25, 0, "-.25"},
        {1.0e-7, 0, ".0000001"},
        {1.0e-8, 0, "1E-08"},
        {1.0e7, 0, "1E+07"},
        {1.234567890123456e20, 1, "1.234567890123456D+20"},
        {1.0e100, 1, "1D+100"},
    };
    for (size_t index = 0;
         index < sizeof(numeric_cases) / sizeof(numeric_cases[0]); ++index) {
        if (!mr_qb_numeric_text(numeric_cases[index].value,
                                numeric_cases[index].is_double,
                                numeric, sizeof(numeric)) ||
            strcmp(numeric, numeric_cases[index].expected) != 0) {
            if (error && error_size)
                snprintf(error, error_size,
                         "QuickBASIC numeric writer case %u produced %s",
                         (unsigned)index, numeric);
            return 0;
        }
    }
    const char *out_text = "_mr_save_roundtrip_test.EXE";
    const char *out_binary = "_mr_save_roundtrip_test.BIN";
    const char *variant_binary = "_mr_save_variant_header_test.BIN";
    mr_join_path(text, sizeof(text), directory, "1.EXE");
    mr_join_path(binary, sizeof(binary), directory, "1.BIN");
    MRSave save;
    if (!mr_save_load(&save, text, binary, error, error_size)) return 0;
    if (save.character.player_level != 0.0 ||
        save.character.experience != 0.0 ||
        save.character.health_max != 22.0 ||
        save.character.health_current != 22.0 ||
        save.character.pocket_money != 16.0 ||
        save.character.attributes[0] != 20.0) {
        mr_save_error(error, error_size,
                      "Decoded Revenge save does not match static load transforms", text);
        return 0;
    }
    for (size_t i = 0; i < MR_BIN_TRAILING_BYTES; ++i) {
        if (save.binary_tail[i] != 0) {
            mr_save_error(error, error_size,
                          "Original Revenge BSAVE padding is not zero", binary);
            return 0;
        }
    }
    int ok = mr_save_write(&save, out_text, out_binary, error, error_size);
    int text_equal = ok && mr_files_equal(text, out_text);
    int binary_equal = ok && mr_files_equal(binary, out_binary);
    ok = ok && text_equal && binary_equal;
    if (!ok && error && error_size && !error[0]) {
        snprintf(error, error_size,
                 "Original Revenge save round-trip differs (%s%s)",
                 text_equal ? "" : "text",
                 binary_equal ? "" : (text_equal ? "binary" : "+binary"));
    }
    if (ok) {
        /* The supplied roster contains multiple legitimate BSAVE header
         * addresses. Exercise every sidecar that is present, not merely the
         * canonical-looking first slot. */
        for (int slot = 1; ok && slot <= 5; ++slot) {
            char slot_name[16], slot_binary[512], slot_output[64];
            snprintf(slot_name, sizeof(slot_name), "%d.BIN", slot);
            mr_join_path(slot_binary, sizeof(slot_binary), directory,
                         slot_name);
            FILE *probe = fopen(slot_binary, "rb");
            if (!probe) continue;
            fclose(probe);
            snprintf(slot_output, sizeof(slot_output),
                     "_mr_save_slot_%d_test.BIN", slot);
            MRSave slot_save;
            ok = mr_load_binary(&slot_save, slot_binary, error, error_size) &&
                 mr_write_binary(&slot_save, slot_output, error, error_size) &&
                 mr_files_equal(slot_binary, slot_output);
            remove(slot_output);
            if (!ok && error && error_size && !error[0])
                snprintf(error, error_size,
                         "Original Revenge slot %d BSAVE did not round-trip",
                         slot);
        }
    }
    if (ok) {
        /* BLOAD's explicit DS:9B06 destination ignores these header words.
         * Exercise one of the non-9B06 forms present in the supplied slots so
         * a future strict-header regression cannot hide behind slot 1. */
        uint16_t original_segment = save.bsave_segment;
        uint16_t original_offset = save.bsave_offset;
        save.bsave_segment = 0x1655;
        save.bsave_offset = 0x4020;
        MRSave variant;
        ok = mr_write_binary(&save, variant_binary, error, error_size) &&
             mr_load_binary(&variant, variant_binary, error, error_size) &&
             variant.bsave_segment == 0x1655 &&
             variant.bsave_offset == 0x4020 &&
             variant.explored[0][0] == save.explored[0][0] &&
             variant.explored[70][20] == save.explored[70][20];
        remove(variant_binary);
        save.bsave_segment = original_segment;
        save.bsave_offset = original_offset;
        if (!ok && error && error_size && !error[0])
            snprintf(error, error_size,
                     "Revenge explicit-destination BSAVE variant failed");
    }
    if (ok) {
        /* Values 37..200 are unused by DUNSMALL gameplay but belong to the
         * original sequential format. Prove that native persistence does not
         * discard future/reserved data merely because the executable never
         * selects it. */
        save.character.persistent_values[MR_PERSISTENT_RESERVED_TAIL_FIRST] =
            123.5;
        MRSave reloaded;
        ok = mr_save_write(&save, out_text, out_binary, error, error_size) &&
             mr_save_load(&reloaded, out_text, out_binary, error, error_size) &&
             reloaded.character.persistent_values[
                 MR_PERSISTENT_RESERVED_TAIL_FIRST] == 123.5;
        if (!ok && error && error_size && !error[0])
            snprintf(error, error_size,
                     "Reserved Revenge persistent value did not round-trip");
        remove(out_text);
        remove(out_binary);
    }
    if (ok) {
        /* WRITE # emits this DOUBLE in D notation. Prove that the loader
         * accepts the original marker rather than merely testing the text
         * formatter in isolation. */
        save.character.bank_money = 1.0e20;
        MRSave reloaded;
        ok = mr_save_write(&save, out_text, out_binary, error, error_size) &&
             mr_save_load(&reloaded, out_text, out_binary, error, error_size) &&
             reloaded.character.bank_money == 1.0e20;
        if (!ok && error && error_size && !error[0])
            snprintf(error, error_size,
                     "QuickBASIC D-exponent save did not round-trip");
        remove(out_text);
        remove(out_binary);
    }
    return ok;
}
