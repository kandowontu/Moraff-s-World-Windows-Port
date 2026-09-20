#ifndef MR_DATA_H
#define MR_DATA_H

#include <stddef.h>

#define MR_ATTRIBUTE_COUNT 6
#define MR_SPELL_LEVELS 12
#define MR_MONSTER_ROWS 44
#define MR_HELP_CHAPTERS 8
#define MR_MAGIC_ITEMS 13
#define MR_COMBAT_MESSAGES 15

typedef struct MRSpellEntry {
    char preparation_name[32];
    char battle_name[32];
    char preparation_description[192];
    char battle_description[192];
} MRSpellEntry;

typedef struct MRMagicItem {
    char name[48];
    char description[192];
} MRMagicItem;

typedef struct MRData {
    MRSpellEntry spells[MR_SPELL_LEVELS];
    char attribute_labels[MR_ATTRIBUTE_COUNT][32];
    MRMagicItem magic_items[MR_MAGIC_ITEMS];
    char inventory_labels[9][48];
    char treasure_names[9][48];
    char combat_messages[MR_COMBAT_MESSAGES][64];
    char monster_names[MR_MONSTER_ROWS][40];
    char *help[MR_HELP_CHAPTERS];
    size_t help_size[MR_HELP_CHAPTERS];
} MRData;

/* Load the original sequential data files from a relative Revenge directory.
 * The native port never embeds or redistributes their copyrighted contents. */
int mr_data_load(MRData *data, const char *directory,
                 char *error, size_t error_size);
void mr_data_free(MRData *data);
int mr_data_self_test(const char *directory, char *error, size_t error_size);

#endif
