#include "mr_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MR_LINE_MAX 512

static void mr_error(char *error, size_t error_size, const char *text,
                     const char *path) {
    if (!error || error_size == 0) return;
    if (path) snprintf(error, error_size, "%s: %s", text, path);
    else snprintf(error, error_size, "%s", text);
}

static void mr_path(char *out, size_t out_size, const char *directory,
                    const char *name) {
    size_t length = strlen(directory);
    const char *separator = length && (directory[length - 1] == '/' ||
                                      directory[length - 1] == '\\') ? "" : "/";
    snprintf(out, out_size, "%s%s%s", directory, separator, name);
}

static void mr_trim_record(char *line) {
    size_t length = strlen(line);
    while (length && (line[length - 1] == '\r' || line[length - 1] == '\n' ||
                      (unsigned char)line[length - 1] == 0x1A)) {
        line[--length] = '\0';
    }
    if (length >= 2 && line[0] == '"' && line[length - 1] == '"') {
        memmove(line, line + 1, length - 2);
        line[length - 2] = '\0';
    }
}

static int mr_read_record(FILE *file, char *out, size_t out_size) {
    while (fgets(out, (int)out_size, file)) {
        mr_trim_record(out);
        if (out[0]) return 1;
    }
    return 0;
}

static int mr_copy_record(FILE *file, char *out, size_t out_size,
                          const char *path, char *error, size_t error_size) {
    char line[MR_LINE_MAX];
    if (!mr_read_record(file, line, sizeof(line))) {
        mr_error(error, error_size, "Truncated original data file", path);
        return 0;
    }
    snprintf(out, out_size, "%s", line);
    return 1;
}

static int mr_load_spells(MRData *data, const char *directory,
                          char *error, size_t error_size) {
    char path[512];
    mr_path(path, sizeof(path), directory, "F1.COM");
    FILE *file = fopen(path, "rb");
    if (!file) {
        mr_error(error, error_size, "Missing original spell table", path);
        return 0;
    }
    for (int attribute = 0; attribute < MR_ATTRIBUTE_COUNT; attribute++) {
        if (!mr_copy_record(file, data->attribute_labels[attribute],
                            sizeof(data->attribute_labels[attribute]), path,
                            error, error_size)) {
            fclose(file);
            return 0;
        }
        for (int within_attribute = 0; within_attribute < 2;
             within_attribute++) {
            int level = attribute * 2 + within_attribute;
            if (!mr_copy_record(file,
                                data->spells[level].preparation_description,
                                sizeof(data->spells[level].preparation_description),
                                path, error, error_size) ||
                !mr_copy_record(file, data->spells[level].battle_description,
                                sizeof(data->spells[level].battle_description),
                                path, error, error_size) ||
                !mr_copy_record(file, data->spells[level].preparation_name,
                                sizeof(data->spells[level].preparation_name), path,
                                error, error_size) ||
                !mr_copy_record(file, data->spells[level].battle_name,
                                sizeof(data->spells[level].battle_name), path,
                                error, error_size)) {
                fclose(file);
                return 0;
            }
        }
    }
    fclose(file);
    return 1;
}

static int mr_skip_records(FILE *file, int count, const char *path,
                           char *error, size_t error_size) {
    char line[MR_LINE_MAX];
    for (int index = 0; index < count; index++) {
        if (!mr_read_record(file, line, sizeof(line))) {
            mr_error(error, error_size, "Truncated original magic table", path);
            return 0;
        }
    }
    return 1;
}

static int mr_load_magic(MRData *data, const char *directory,
                         char *error, size_t error_size) {
    char path[512];
    mr_path(path, sizeof(path), directory, "F2.COM");
    FILE *file = fopen(path, "rb");
    if (!file) {
        mr_error(error, error_size, "Missing original magic-item table", path);
        return 0;
    }

    /* F2 starts with six dimension pairs, seven coordinate pairs, and five
     * adapter geometry rows.
     * DUNSMALL:BCF9 consumes them before the visible item text. */
    if (!mr_skip_records(file, 18, path, error, error_size)) {
        fclose(file);
        return 0;
    }
    for (int index = 0; index < 9; index++) {
        if (!mr_copy_record(file, data->inventory_labels[index],
                            sizeof(data->inventory_labels[index]), path,
                            error, error_size)) {
            fclose(file);
            return 0;
        }
    }
    for (int index = 0; index < 9; index++) {
        if (!mr_copy_record(file, data->treasure_names[index],
                            sizeof(data->treasure_names[index]), path,
                            error, error_size)) {
            fclose(file);
            return 0;
        }
    }
    for (int index = 0; index < MR_MAGIC_ITEMS; index++) {
        char description[MR_LINE_MAX];
        if (!mr_copy_record(file, description, sizeof(description), path,
                            error, error_size)) {
            fclose(file);
            return 0;
        }
        char *colon = strchr(description, ':');
        if (colon) {
            size_t name_length = (size_t)(colon - description);
            if (name_length >= sizeof(data->magic_items[index].name))
                name_length = sizeof(data->magic_items[index].name) - 1;
            memcpy(data->magic_items[index].name, description, name_length);
            data->magic_items[index].name[name_length] = '\0';
            colon++;
            while (*colon == ' ') colon++;
            snprintf(data->magic_items[index].description,
                     sizeof(data->magic_items[index].description), "%s", colon);
        } else {
            snprintf(data->magic_items[index].name,
                     sizeof(data->magic_items[index].name), "ITEM %d", index + 1);
            snprintf(data->magic_items[index].description,
                     sizeof(data->magic_items[index].description), "%s", description);
        }
    }
    for (int index = 0; index < MR_COMBAT_MESSAGES; index++) {
        if (!mr_copy_record(file, data->combat_messages[index],
                            sizeof(data->combat_messages[index]), path,
                            error, error_size)) {
            fclose(file);
            return 0;
        }
    }
    fclose(file);
    return 1;
}

static int mr_load_monster_file(MRData *data, const char *directory,
                                const char *name, int first,
                                char *error, size_t error_size) {
    char path[512];
    mr_path(path, sizeof(path), directory, name);
    FILE *file = fopen(path, "rb");
    if (!file) {
        mr_error(error, error_size, "Missing original monster table", path);
        return 0;
    }
    for (int index = 0; index < 22; index++) {
        if (!mr_copy_record(file, data->monster_names[first + index],
                            sizeof(data->monster_names[first + index]), path,
                            error, error_size)) {
            fclose(file);
            return 0;
        }
    }
    fclose(file);
    return 1;
}

static int mr_load_help(MRData *data, const char *directory,
                        char *error, size_t error_size) {
    for (int chapter = 0; chapter < MR_HELP_CHAPTERS; chapter++) {
        char name[16], path[512];
        snprintf(name, sizeof(name), "H%d.OVL", chapter + 1);
        mr_path(path, sizeof(path), directory, name);
        FILE *file = fopen(path, "rb");
        if (!file) {
            mr_error(error, error_size, "Missing original help chapter", path);
            return 0;
        }
        if (fseek(file, 0, SEEK_END) != 0) {
            fclose(file);
            mr_error(error, error_size, "Cannot size original help chapter", path);
            return 0;
        }
        long size = ftell(file);
        rewind(file);
        if (size < 0) {
            fclose(file);
            mr_error(error, error_size, "Cannot size original help chapter", path);
            return 0;
        }
        char *text = (char *)malloc((size_t)size + 1);
        if (!text) {
            fclose(file);
            mr_error(error, error_size, "Out of memory loading help", NULL);
            return 0;
        }
        size_t read_size = fread(text, 1, (size_t)size, file);
        fclose(file);
        while (read_size && (unsigned char)text[read_size - 1] == 0x1A)
            read_size--;
        text[read_size] = '\0';
        data->help[chapter] = text;
        data->help_size[chapter] = read_size;
    }
    return 1;
}

int mr_data_load(MRData *data, const char *directory,
                 char *error, size_t error_size) {
    if (!data || !directory) {
        mr_error(error, error_size, "Invalid Revenge data request", NULL);
        return 0;
    }
    memset(data, 0, sizeof(*data));
    if (!mr_load_spells(data, directory, error, error_size) ||
        !mr_load_magic(data, directory, error, error_size) ||
        !mr_load_monster_file(data, directory, "F6.COM", 0,
                              error, error_size) ||
        !mr_load_monster_file(data, directory, "F7.COM", 22,
                              error, error_size) ||
        !mr_load_help(data, directory, error, error_size)) {
        mr_data_free(data);
        return 0;
    }
    return 1;
}

void mr_data_free(MRData *data) {
    if (!data) return;
    for (int index = 0; index < MR_HELP_CHAPTERS; index++) {
        free(data->help[index]);
        data->help[index] = NULL;
        data->help_size[index] = 0;
    }
}

int mr_data_self_test(const char *directory, char *error, size_t error_size) {
    MRData data;
    if (!mr_data_load(&data, directory, error, error_size)) return 0;
    const char *failure = NULL;
    if (strcmp(data.spells[0].preparation_name, "CURE") != 0)
        failure = "F1 preparation-spell order";
    else if (strcmp(data.spells[11].battle_name, "GOD?") != 0)
        failure = "F1 battle-spell order";
    else if (strcmp(data.magic_items[0].name, "Teleport Scroll") != 0)
        failure = "F2 first magic item";
    else if (strcmp(data.magic_items[12].name, "Holy Hand Grenade") != 0)
        failure = "F2 final magic item";
    else if (strcmp(data.monster_names[0], "SKELETON") != 0)
        failure = "F6 monster order";
    else if (strcmp(data.monster_names[43], "VAMPIRE") != 0)
        failure = "F7 monster order";
    else if (data.help_size[0] <= 100 || data.help_size[7] <= 100)
        failure = "help chapter lengths";
    mr_data_free(&data);
    if (failure) {
        if (error && error_size)
            snprintf(error, error_size,
                     "Original Revenge tables do not match Advanced 3.3 layout "
                     "(%s): %s", failure, directory);
        return 0;
    }
    return 1;
}
