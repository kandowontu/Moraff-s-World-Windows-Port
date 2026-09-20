#include "mr_frontend.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t mr_frontend_u16le(const uint8_t *bytes) {
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static void mr_frontend_put_u16le(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value & 0xff);
    bytes[1] = (uint8_t)(value >> 8);
}

static void mr_frontend_error(char *error, size_t error_size,
                              const char *message, const char *path) {
    if (!error || !error_size) return;
    if (path) snprintf(error, error_size, "%s: %s", message, path);
    else snprintf(error, error_size, "%s", message);
}

static void mr_frontend_join(char *out, size_t out_size,
                             const char *directory, const char *name) {
    size_t length = strlen(directory);
    const char *separator = length &&
        (directory[length - 1] == '/' || directory[length - 1] == '\\')
        ? "" : "/";
    snprintf(out, out_size, "%s%s%s", directory, separator, name);
}

static int mr_read_sequential_string(FILE *file, char *out,
                                     size_t out_size) {
    int c;
    do {
        c = fgetc(file);
        if (c == EOF || c == 0x1A) return 0;
    } while (c == '\r' || c == '\n' || c == ' ' || c == '\t');
    if (c != '"') return -1;
    size_t length = 0;
    while ((c = fgetc(file)) != EOF && c != 0x1A && c != '"') {
        if (length + 1 < out_size) out[length++] = (char)c;
    }
    if (c != '"') return -1;
    out[length] = 0;
    while ((c = fgetc(file)) != EOF && c != 0x1A && c != '\n') {
        if (c != '\r' && c != ' ' && c != '\t') return -1;
    }
    return 1;
}

int mr_roster_load(MRRoster *roster, const char *path,
                   char *error, size_t error_size) {
    if (!roster || !path) {
        mr_frontend_error(error, error_size, "Invalid Revenge roster request",
                          NULL);
        return 0;
    }
    memset(roster, 0, sizeof(*roster));
    FILE *file = fopen(path, "rb");
    if (!file) {
        mr_frontend_error(error, error_size, "Cannot open Revenge roster",
                          path);
        return 0;
    }
    int terminated = 0;
    for (;;) {
        char value[MR_ROSTER_NAME_BYTES];
        int read = mr_read_sequential_string(file, value, sizeof(value));
        if (read == 0) break;
        if (read < 0) {
            fclose(file);
            mr_frontend_error(error, error_size,
                              "Malformed Revenge roster record", path);
            return 0;
        }
        if (strcmp(value, "END") == 0) {
            terminated = 1;
            break;
        }
        if (roster->count >= MR_ROSTER_CAPACITY) {
            fclose(file);
            mr_frontend_error(error, error_size,
                              "Revenge roster exceeds native capacity", path);
            return 0;
        }
        snprintf(roster->names[roster->count], MR_ROSTER_NAME_BYTES, "%s",
                 value);
        roster->count++;
    }
    fclose(file);
    if (!terminated) {
        mr_frontend_error(error, error_size,
                          "Revenge roster is missing END sentinel", path);
        return 0;
    }
    return 1;
}

int mr_roster_write(const MRRoster *roster, const char *path,
                    char *error, size_t error_size) {
    if (!roster || !path || roster->count < 0 ||
        roster->count > MR_ROSTER_CAPACITY) {
        mr_frontend_error(error, error_size,
                          "Invalid Revenge roster save request", NULL);
        return 0;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        mr_frontend_error(error, error_size, "Cannot create Revenge roster",
                          path);
        return 0;
    }
    int ok = 1;
    for (int index = 0; index < roster->count && ok; index++)
        ok = fprintf(file, "\"%s\"\r\n", roster->names[index]) >= 0;
    if (ok) ok = fputs("\"END\"\r\n", file) >= 0;
    if (ok) ok = fputc(0x1A, file) != EOF;
    if (fclose(file) != 0) ok = 0;
    if (!ok)
        mr_frontend_error(error, error_size, "Cannot write Revenge roster",
                          path);
    return ok;
}

int mr_roster_append(MRRoster *roster, const char *name,
                     char *error, size_t error_size) {
    if (!roster || !name || !name[0] || strchr(name, '"') ||
        strchr(name, '\r') || strchr(name, '\n')) {
        mr_frontend_error(error, error_size,
                          "Invalid Revenge character name", NULL);
        return 0;
    }
    if (roster->count >= MR_ROSTER_CAPACITY) {
        mr_frontend_error(error, error_size, "Revenge roster is full", NULL);
        return 0;
    }
    if (mr_roster_find(roster, name) >= 0) {
        mr_frontend_error(error, error_size,
                          "That Revenge character already exists", NULL);
        return 0;
    }
    size_t length = strlen(name);
    if (length >= MR_ROSTER_NAME_BYTES) {
        mr_frontend_error(error, error_size,
                          "Revenge character name is too long", NULL);
        return 0;
    }
    snprintf(roster->names[roster->count], MR_ROSTER_NAME_BYTES, "%s", name);
    roster->count++;
    return 1;
}

int mr_roster_remove(MRRoster *roster, int index) {
    if (!roster || index < 0 || index >= roster->count) return 0;
    if (index + 1 < roster->count) {
        memmove(roster->names[index], roster->names[index + 1],
                (size_t)(roster->count - index - 1) *
                    sizeof(roster->names[0]));
    }
    roster->count--;
    memset(roster->names[roster->count], 0, sizeof(roster->names[0]));
    return 1;
}

MRBeginMenuOption mr_begin_decode_menu_key(int first_byte) {
    return first_byte >= '1' && first_byte <= '5'
        ? (MRBeginMenuOption)(first_byte - '0') : 0;
}

MRRosterAction mr_begin_decode_roster_key(int first_byte,
                                          int extended_scan_code) {
    /* BEGIN 0778-07A9 uppercases ordinary keys. Its roster arm accepts the
     * QuickBASIC two-byte Up/Down forms, Return, and Escape. */
    if (first_byte == 0) {
        if (extended_scan_code == 0x48) return MR_ROSTER_ACTION_PREVIOUS;
        if (extended_scan_code == 0x50) return MR_ROSTER_ACTION_NEXT;
        return MR_ROSTER_ACTION_NONE;
    }
    if (first_byte == '\r') return MR_ROSTER_ACTION_SELECT;
    if (first_byte == 0x1B) return MR_ROSTER_ACTION_RESTART;
    return MR_ROSTER_ACTION_NONE;
}

void mr_begin_character_paths(const char *directory, int roster_index,
                              char *text_path, size_t text_path_size,
                              char *binary_path, size_t binary_path_size) {
    char name[32];
    if (roster_index < 0) roster_index = 0;
    snprintf(name, sizeof(name), "%d.EXE", roster_index + 1);
    if (text_path && text_path_size)
        mr_frontend_join(text_path, text_path_size, directory, name);
    snprintf(name, sizeof(name), "%d.BIN", roster_index + 1);
    if (binary_path && binary_path_size)
        mr_frontend_join(binary_path, binary_path_size, directory, name);
}

int mr_roster_wrap_selection(const MRRoster *roster, int selected,
                             int delta) {
    if (!roster || roster->count <= 0) return -1;
    selected %= roster->count;
    if (selected < 0) selected += roster->count;
    selected = (selected + delta) % roster->count;
    if (selected < 0) selected += roster->count;
    return selected;
}

int mr_roster_find(const MRRoster *roster, const char *name) {
    if (!roster || !name) return -1;
    for (int index = 0; index < roster->count; index++)
        if (strcmp(roster->names[index], name) == 0) return index;
    return -1;
}

int mr_hall_load(MRHallOfFame *hall, const char *path,
                 char *error, size_t error_size) {
    if (!hall || !path) {
        mr_frontend_error(error, error_size,
                          "Invalid Revenge hall-of-fame request", NULL);
        return 0;
    }
    memset(hall, 0, sizeof(*hall));
    FILE *file = fopen(path, "rb");
    if (!file) {
        mr_frontend_error(error, error_size,
                          "Cannot open Revenge hall of fame", path);
        return 0;
    }
    uint8_t header[7];
    int ok = fread(header, 1, sizeof(header), file) == sizeof(header) &&
             header[0] == 0xFD &&
             mr_frontend_u16le(header + 5) == MR_HALL_PAYLOAD_BYTES &&
             fread(hall->raw, 1, sizeof(hall->raw), file) ==
                 sizeof(hall->raw);
    if (ok) {
        hall->bsave_segment = mr_frontend_u16le(header + 1);
        hall->bsave_offset = mr_frontend_u16le(header + 3);
        int final = fgetc(file);
        hall->dos_eof = final == 0x1A;
        if (hall->dos_eof) final = fgetc(file);
        ok = final == EOF;
    }
    fclose(file);
    if (!ok) {
        mr_frontend_error(error, error_size,
                          "Invalid Revenge hall-of-fame BSAVE", path);
        return 0;
    }
    /* F8:01CC supplies DS:1856 explicitly to QuickBASIC BLOAD, just as
     * DUNSMALL supplies DS:9B06 for character sidecars.  The segment and
     * offset stored in a BSAVE header are therefore informational here, not
     * a validity condition.  Preserve them for subsequent writes. */
    /* F8:01E6-022F: records 1..10, characters 1..80, stride 81 words. */
    for (int row = 0; row < MR_HALL_ROWS; row++) {
        for (int column = 0; column < MR_HALL_ROW_COLUMNS; column++) {
            int word = (row + 1) * 81 + column + 1;
            int offset = word * 2;
            hall->rows[row][column] = offset < MR_HALL_PAYLOAD_BYTES
                ? (char)hall->raw[offset] : 0;
        }
        hall->rows[row][MR_HALL_ROW_COLUMNS] = 0;
    }
    return 1;
}

int mr_hall_write(const MRHallOfFame *hall, const char *path,
                  char *error, size_t error_size) {
    if (!hall || !path) {
        mr_frontend_error(error, error_size,
                          "Invalid Revenge hall-of-fame save request", NULL);
        return 0;
    }
    uint8_t raw[MR_HALL_PAYLOAD_BYTES];
    uint8_t header[7] = {0xFD, 0, 0, 0, 0, 0, 0};
    memcpy(raw, hall->raw, sizeof(raw));
    for (int row = 0; row < MR_HALL_ROWS; row++) {
        for (int column = 0; column < MR_HALL_ROW_COLUMNS; column++) {
            int word = (row + 1) * 81 + column + 1;
            int offset = word * 2;
            if (offset >= MR_HALL_PAYLOAD_BYTES) continue;
            raw[offset] = (uint8_t)hall->rows[row][column];
            if (offset + 1 < MR_HALL_PAYLOAD_BYTES) raw[offset + 1] = 0;
        }
    }
    mr_frontend_put_u16le(header + 1, hall->bsave_segment);
    mr_frontend_put_u16le(header + 3,
                          hall->bsave_offset ? hall->bsave_offset : 0x1856);
    mr_frontend_put_u16le(header + 5, MR_HALL_PAYLOAD_BYTES);
    FILE *file = fopen(path, "wb");
    if (!file) {
        mr_frontend_error(error, error_size,
                          "Cannot create Revenge hall of fame", path);
        return 0;
    }
    int ok = fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
             fwrite(raw, 1, sizeof(raw), file) == sizeof(raw) &&
             fputc(0x1A, file) != EOF;
    if (fclose(file) != 0) ok = 0;
    if (!ok)
        mr_frontend_error(error, error_size,
                          "Cannot write Revenge hall of fame", path);
    return ok;
}

static void mr_hall_copy_field(char *row, int start, int width,
                               const char *value) {
    int length = value ? (int)strlen(value) : 0;
    if (length > width) length = width;
    memcpy(row + start, value, (size_t)length);
}

static void mr_hall_right_number(char *row, int start, int width,
                                 long long value) {
    char number[64];
    snprintf(number, sizeof(number), "%lld", value);
    int length = (int)strlen(number);
    if (length > width) {
        memcpy(row + start, number + length - width, (size_t)width);
        return;
    }
    memcpy(row + start + width - length, number, (size_t)length);
}

void mr_hall_format_row(const MRHallSummary *summary,
                        char row[MR_HALL_ROW_COLUMNS + 1]) {
    if (!row) return;
    memset(row, ' ', MR_HALL_ROW_COLUMNS);
    row[MR_HALL_ROW_COLUMNS] = 0;
    if (!summary) return;
    mr_hall_right_number(row, 0, 3, summary->player_level);
    mr_hall_copy_field(row, 6, 10,
                       summary->player_class == 1 ? "FIGHTER   "
                                                  : "WIZARD    ");
    char name[21] = {0};
    if (summary->name && strlen(summary->name) > 20) {
        memcpy(name, summary->name, 17);
        memcpy(name + 17, "...", 3);
    } else if (summary->name) {
        snprintf(name, sizeof(name), "%s", summary->name);
    }
    mr_hall_copy_field(row, 16, 20, name);
    mr_hall_right_number(row, 40, 5,
                         (long long)(summary->pocket_money +
                                     summary->bank_money));
    const char *killer = summary->killer && summary->killer[0]
        ? summary->killer : "STILL ALIVE";
    mr_hall_copy_field(row, 49, 20, killer);
    mr_hall_right_number(row, 69, 8, summary->dungeon_level);
}

void mr_hall_sort(MRHallOfFame *hall) {
    if (!hall) return;
    /* F8:00E9-01CB compares the complete fixed-length QuickBASIC strings,
     * not parsed level numbers.  That distinction controls tie ordering and
     * the treatment of the NUL-filled unused F9.EXE rows. */
    int changed;
    do {
        changed = 0;
        for (int row = 0; row + 1 < MR_HALL_ROWS; row++) {
            if (memcmp(hall->rows[row], hall->rows[row + 1],
                       MR_HALL_ROW_COLUMNS) < 0) {
                char temporary[MR_HALL_ROW_COLUMNS + 1];
                memcpy(temporary, hall->rows[row], sizeof(temporary));
                memcpy(hall->rows[row], hall->rows[row + 1],
                       sizeof(temporary));
                memcpy(hall->rows[row + 1], temporary,
                       sizeof(temporary));
                changed = 1;
            }
        }
    } while (changed);
}

void mr_hall_insert(MRHallOfFame *hall,
                    const char row[MR_HALL_ROW_COLUMNS + 1]) {
    if (!hall || !row) return;
    /* F8:0051-0067 rejects a handoff row which sorts below table entry ten.
     * The former native path always replaced entry ten, so a low-ranking
     * death could erase a legitimate hall record. */
    if (memcmp(row, hall->rows[MR_HALL_ROWS - 1],
               MR_HALL_ROW_COLUMNS) < 0)
        return;
    memcpy(hall->rows[MR_HALL_ROWS - 1], row,
           MR_HALL_ROW_COLUMNS + 1);
    mr_hall_sort(hall);
}

int mr_frontend_self_test(const char *resource_directory,
                          char *error, size_t error_size) {
    char roster_path[512], hall_path[512];
    mr_frontend_join(roster_path, sizeof(roster_path), resource_directory,
                     "F5.COM");
    mr_frontend_join(hall_path, sizeof(hall_path), resource_directory,
                     "F9.EXE");
    MRRoster roster;
    MRHallOfFame hall;
    if (!mr_roster_load(&roster, roster_path, error, error_size) ||
        !mr_hall_load(&hall, hall_path, error, error_size))
        return 0;
    /* F8's explicit BLOAD destination ignores the BSAVE address words.
     * Exercise a synthetic form so valid tables from another DOS memory
     * layout can never regress to a false header rejection. */
    char hall_variant_path[512];
    mr_frontend_join(hall_variant_path, sizeof(hall_variant_path),
                     resource_directory, "_mr_f9_header_variant.tmp");
    MRHallOfFame hall_variant = hall;
    hall_variant.bsave_segment = 0x2A31;
    hall_variant.bsave_offset = 0x4020;
    MRHallOfFame hall_variant_read;
    int hall_header_failed =
        !mr_hall_write(&hall_variant, hall_variant_path, error, error_size) ||
        !mr_hall_load(&hall_variant_read, hall_variant_path,
                      error, error_size) ||
        hall_variant_read.bsave_segment != 0x2A31 ||
        hall_variant_read.bsave_offset != 0x4020 ||
        memcmp(hall_variant_read.rows, hall_variant.rows,
               sizeof(hall_variant.rows)) != 0;
    remove(hall_variant_path);
    int roster_failed = roster.count != 2 ||
                        strcmp(roster.names[0], "THE FIRST CHARACTER") != 0 ||
                        strcmp(roster.names[1], "K") != 0 ||
                        mr_roster_find(&roster, "K") != 1 ||
                        mr_roster_wrap_selection(&roster, 0, -1) != 1 ||
                        mr_roster_wrap_selection(&roster, 1, 1) != 0;
    int load_failed = strncmp(
        hall.rows[0], "  0   FIGHTER   THE FIRST CHARACTER", 35) != 0;
    MRHallSummary summary = {123, 2, "A CHARACTER NAME THAT IS TOO LONG",
                             40, 2, 70, ""};
    char row[MR_HALL_ROW_COLUMNS + 1];
    mr_hall_format_row(&summary, row);
    int format_failed =
        strncmp(row, "123   WIZARD    A CHARACTER NAME ...", 36) != 0 ||
        strncmp(row + 40, "   42", 5) != 0 ||
        strncmp(row + 49, "STILL ALIVE", 11) != 0 ||
        strncmp(row + 69, "      70", 8) != 0;
    /* F8:045C keeps the rightmost characters when a numeric string is wider
     * than the caller's fixed field. Pin that non-obvious overflow behavior,
     * not just the ordinary padding case above. */
    MRHallSummary wide_summary = {12345, 1, "WIDE", 1234567, 0,
                                  123456789, ""};
    char wide_row[MR_HALL_ROW_COLUMNS + 1];
    mr_hall_format_row(&wide_summary, wide_row);
    format_failed |= strncmp(wide_row, "345", 3) != 0 ||
                     strncmp(wide_row + 40, "34567", 5) != 0 ||
                     strncmp(wide_row + 69, "23456789", 8) != 0;
    char text_path[512], binary_path[512];
    mr_begin_character_paths(resource_directory, 1, text_path,
                             sizeof(text_path), binary_path,
                             sizeof(binary_path));
    int dispatch_failed =
        mr_begin_decode_menu_key('1') != MR_BEGIN_CREATE_CHARACTER ||
        mr_begin_decode_menu_key('5') != MR_BEGIN_ORDER_ADVANCED ||
        mr_begin_decode_menu_key('0') != 0 ||
        mr_begin_decode_roster_key(0, 0x48) !=
            MR_ROSTER_ACTION_PREVIOUS ||
        mr_begin_decode_roster_key(0, 0x50) != MR_ROSTER_ACTION_NEXT ||
        mr_begin_decode_roster_key('\r', 0) != MR_ROSTER_ACTION_SELECT ||
        mr_begin_decode_roster_key(0x1B, 0) != MR_ROSTER_ACTION_RESTART ||
        !strstr(text_path, "2.EXE") || !strstr(binary_path, "2.BIN");
    MRRoster copy = roster;
    int append_failed = !mr_roster_append(&copy, "STATIC TEST", error,
                                           error_size) ||
                        copy.count != roster.count + 1 ||
                        mr_roster_append(&copy, "STATIC TEST", NULL, 0);
    MRRoster full;
    memset(&full, 0, sizeof(full));
    full.count = MR_ROSTER_CAPACITY;
    int capacity_failed = MR_ROSTER_CAPACITY != 10 ||
                          mr_roster_append(&full, "ONE TOO MANY", NULL, 0);
    MRRoster removal = roster;
    int remove_failed = !mr_roster_remove(&removal, 0) ||
                        removal.count != 1 ||
                        strcmp(removal.names[0], "K") != 0 ||
                        mr_roster_remove(&removal, 1) ||
                        !mr_roster_remove(&removal, 0) || removal.count != 0;
    MRHallOfFame ordering;
    memset(&ordering, 0, sizeof(ordering));
    for (int index = 0; index < MR_HALL_ROWS; index++) {
        memset(ordering.rows[index], ' ', MR_HALL_ROW_COLUMNS);
        ordering.rows[index][MR_HALL_ROW_COLUMNS] = 0;
        memcpy(ordering.rows[index], "200   M", 7);
    }
    memcpy(ordering.rows[0], "010   Z", 7);
    memcpy(ordering.rows[1], "010   A", 7);
    mr_hall_sort(&ordering);
    int sort_failed = memcmp(ordering.rows[MR_HALL_ROWS - 2],
                             "010   Z", 7) != 0 ||
                      memcmp(ordering.rows[MR_HALL_ROWS - 1],
                             "010   A", 7) != 0;
    MRHallOfFame rejected = ordering;
    char below[MR_HALL_ROW_COLUMNS + 1];
    memset(below, ' ', MR_HALL_ROW_COLUMNS);
    below[MR_HALL_ROW_COLUMNS] = 0;
    memcpy(below, "009", 3);
    mr_hall_insert(&rejected, below);
    int threshold_failed = memcmp(&rejected, &ordering,
                                  sizeof(ordering)) != 0;
    int failed = roster_failed || load_failed || hall_header_failed ||
                 format_failed ||
                 dispatch_failed || append_failed || capacity_failed ||
                 remove_failed || sort_failed || threshold_failed;
    if (failed) {
        if (error && error_size)
            snprintf(error, error_size,
                     "BEGIN/F8 native frontend mismatch (roster=%d load=%d "
                     "header=%d "
                     "format=%d dispatch=%d append=%d capacity=%d remove=%d "
                     "sort=%d threshold=%d "
                     "loaded='%.36s' "
                     "row='%.36s')", roster_failed, load_failed,
                     hall_header_failed, format_failed, dispatch_failed,
                     append_failed,
                     capacity_failed, remove_failed, sort_failed,
                     threshold_failed,
                     hall.rows[0], row);
        return 0;
    }
    return 1;
}
