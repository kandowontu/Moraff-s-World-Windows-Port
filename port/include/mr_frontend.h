#ifndef MR_FRONTEND_H
#define MR_FRONTEND_H

#include <stddef.h>
#include <stdint.h>

/* CHCHAR 037A-038B and 0FBD-1000 compare the number of names read from
 * F5.COM against the initialized constants 9 and 10.  Ten is the original
 * hard roster limit; the messages call it available "disk" space. */
#define MR_ROSTER_CAPACITY 10
/* QuickBASIC dynamic strings and LINE INPUT accept up to 255 bytes. The
 * roster display truncates only when drawing (BEGIN 04D5-0524); the stored
 * F5.COM value itself is not an 80-column field. */
#define MR_ROSTER_NAME_BYTES 256
#define MR_HALL_ROWS 10
#define MR_HALL_ROW_COLUMNS 80
#define MR_HALL_PAYLOAD_BYTES 1781

typedef struct MRRoster {
    char names[MR_ROSTER_CAPACITY][MR_ROSTER_NAME_BYTES];
    int count;
} MRRoster;

typedef struct MRHallOfFame {
    uint16_t bsave_segment;
    uint16_t bsave_offset;
    uint8_t raw[MR_HALL_PAYLOAD_BYTES];
    char rows[MR_HALL_ROWS][MR_HALL_ROW_COLUMNS + 1];
    int dos_eof;
} MRHallOfFame;

typedef struct MRHallSummary {
    int player_level;
    int player_class;
    const char *name;
    double pocket_money;
    double bank_money;
    int dungeon_level;
    const char *killer;
} MRHallSummary;

/* BEGIN.EXE 0308-051D.  Keep the original numeric values because the
 * compiled ON GOTO table dispatches directly from the entered number. */
typedef enum MRBeginMenuOption {
    MR_BEGIN_CREATE_CHARACTER = 1,
    MR_BEGIN_PLAY_GAME = 2,
    MR_BEGIN_HALL_OF_FAME = 3,
    MR_BEGIN_RETURN_TO_WORLD = 4,
    MR_BEGIN_ORDER_ADVANCED = 5
} MRBeginMenuOption;

typedef enum MRRosterAction {
    MR_ROSTER_ACTION_NONE = 0,
    MR_ROSTER_ACTION_PREVIOUS,
    MR_ROSTER_ACTION_NEXT,
    MR_ROSTER_ACTION_SELECT,
    MR_ROSTER_ACTION_RESTART
} MRRosterAction;

int mr_roster_load(MRRoster *roster, const char *path,
                   char *error, size_t error_size);
int mr_roster_write(const MRRoster *roster, const char *path,
                    char *error, size_t error_size);
int mr_roster_append(MRRoster *roster, const char *name,
                     char *error, size_t error_size);
int mr_roster_remove(MRRoster *roster, int index);
int mr_roster_wrap_selection(const MRRoster *roster, int selected,
                             int delta);
int mr_roster_find(const MRRoster *roster, const char *name);

MRBeginMenuOption mr_begin_decode_menu_key(int first_byte);
MRRosterAction mr_begin_decode_roster_key(int first_byte,
                                          int extended_scan_code);
void mr_begin_character_paths(const char *directory, int roster_index,
                              char *text_path, size_t text_path_size,
                              char *binary_path, size_t binary_path_size);

int mr_hall_load(MRHallOfFame *hall, const char *path,
                 char *error, size_t error_size);
int mr_hall_write(const MRHallOfFame *hall, const char *path,
                  char *error, size_t error_size);
void mr_hall_format_row(const MRHallSummary *summary,
                        char row[MR_HALL_ROW_COLUMNS + 1]);
void mr_hall_insert(MRHallOfFame *hall,
                    const char row[MR_HALL_ROW_COLUMNS + 1]);
void mr_hall_sort(MRHallOfFame *hall);

int mr_frontend_self_test(const char *resource_directory,
                          char *error, size_t error_size);

#endif
