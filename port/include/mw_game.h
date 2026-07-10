#ifndef MW_GAME_H
#define MW_GAME_H

#include "mw_types.h"
#include "mw_video.h"
#include "mw_input.h"

/* Dungeon map constants */
#define MAP_W       80
#define MAP_H       80
#define MAP_STRIDE  80

/* Wall bit encoding (2 bits per direction in cell byte) */
#define WALL_N_MASK  0x03
#define WALL_E_MASK  0x0C
#define WALL_S_MASK  0x30
#define WALL_W_MASK  0xC0

/* Global game state — replaces the DS-relative globals from the original */
typedef struct {
    /* Video */
    Video video;
    int   video_mode;           /* original mode index (we use 6 = 640x480) */
    int   screen_w;
    int   screen_h;

    /* Input */
    Input input;

    /* Players */
    int        num_players;     /* 1 or 2 */
    int        cur_player;      /* 0 or 1 */
    int        player_slot[2];  /* save slot index for each player */
    Character  chars[MAX_PLAYERS];
    int        char_exists[MAX_PLAYERS];

    /* Dungeon state */
    int   cur_floor;
    int   cur_x, cur_y;
    int   facing_dir;           /* 0=N, 1=E, 2=S, 3=W */
    u8   *dungeon_data;         /* loaded from dung.bin */
    int   dungeon_data_size;
    u8   *worldmap_data;        /* loaded from worldmap.bin */

    /* Graphics assets */
    u8   *world_pic_data[256];  /* image table from world.pic */
    int   world_pic_sizes[256];
    int   world_pic_count;
    u8   *wall_pic_data[64];    /* wall tile images from wall.pic */
    int   wall_pic_sizes[64];
    int   wall_pic_count;

    /* Monster data */
    u8   *monster_data;         /* from h.bin */

    /* File paths */
    char  game_dir[260];        /* directory containing game data files */

    /* RNG */
    u32   rand_state;
} Game;

/* Core lifecycle */
int  game_init(Game *g, const char *data_dir);
void game_shutdown(Game *g);
void game_run(Game *g);

/* Character I/O */
int  game_load_character(Game *g, int slot);
int  game_save_character(Game *g, int slot);

/* Asset loading */
int  game_load_pics(Game *g);
int  game_load_dungeon(Game *g);
int  game_load_monsters(Game *g);

/* Dungeon map access */
u8   map_get_cell(Game *g, int x, int y);
int  map_is_wall(Game *g, int x, int y);
int  map_has_wall_n(u8 cell);
int  map_has_wall_e(u8 cell);
int  map_has_wall_s(u8 cell);
int  map_has_wall_w(u8 cell);

/* RNG — matches original LCG */
int  game_rand(Game *g);
void game_srand(Game *g, u32 seed);

/* File path helper */
void game_make_path(Game *g, char *out, int out_sz, const char *filename);

/* Drawing helpers (internal to mw_game.c) */
void draw_minimap(Game *g, int mx, int my, int mw, int mh);
void draw_3d_view(Game *g, int vx, int vy, int vw, int vh);

#endif /* MW_GAME_H */
