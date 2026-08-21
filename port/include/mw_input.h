#ifndef MW_INPUT_H
#define MW_INPUT_H

#include "mw_types.h"
#include <SDL.h>

/* Key queue — replaces DOS getch/kbhit.
 * The original game is fully synchronous: it calls getch() and blocks.
 * We buffer SDL key events into a ring buffer and drain from it. */

#define KEY_QUEUE_SIZE 64
#define INPUT_MOUSE_CLICK 0x100
#define INPUT_TRAINER     0x101  /* Ctrl+F12 */
#define INPUT_WILDERNESS_TEST 0x102  /* Ctrl+F11 */
#define INPUT_NOCLIP_TOGGLE 0x103  /* Ctrl+F10 */
#define INPUT_GOD_TOGGLE    0x104  /* Ctrl+F9 */
#define INPUT_TOWN_TELEPORT 0x105  /* Ctrl+F8 */
#define INPUT_OPEN_FLOOR_TOGGLE 0x106  /* Ctrl+F7 */
#define INPUT_DUNGEON_REROLL 0x107  /* Ctrl+F6 */
#define INPUT_MODEL_VIEWER   0x108  /* Ctrl+F5 */
#define INPUT_MOUSE_WHEEL_UP 0x109
#define INPUT_MOUSE_WHEEL_DOWN 0x10A
#define INPUT_MAX_CHARACTER 0x10B  /* Ctrl+Shift+Alt+F12 */
#define INPUT_QUEST_BOSS_WARP 0x10C  /* Ctrl+F4 */
#define INPUT_RANDOMIZE_FLOOR 0x10D  /* Ctrl+F3 */
#define INPUT_BATTLE_SIMULATOR 0x10E /* Ctrl+F2 */
#define INPUT_TURBO_TOGGLE 0x10F /* Ctrl+F1 */
#define INPUT_VIDEO_MODE 0x110 /* Alt+V */

typedef struct {
    int keys[KEY_QUEUE_SIZE];
    SDL_Keymod key_mods[KEY_QUEUE_SIZE];
    int event_x[KEY_QUEUE_SIZE];
    int event_y[KEY_QUEUE_SIZE];
    int head;
    int tail;
    int last_mouse_x;
    int last_mouse_y;
    int mouse_x;
    int mouse_y;
    unsigned mouse_motion_serial;
    SDL_Keymod last_key_mods;
    int quit_requested;
    /* WORLD reads the PC BIOS typematic stream rather than owning separate
       repeat rules for Fight, movement, menus, and text entry.  Keep one
       active physical key and synthesize the DOS default cadence for every
       original mapped key; native Ctrl+Fn diagnostics remain single-shot. */
    int repeat_held;
    SDL_Keycode repeat_sym;
    u32 repeat_next;
    int fight_repeating;
    /* Runtime timing multiplier. One hundred is the original DOS-compatible
       cadence; Turbo Mode changes this without changing turn mechanics. */
    int timing_percent;
} Input;

void input_init(Input *inp);
void input_pump(Input *inp);          /* call each frame — drains SDL events */
int  input_kbhit(Input *inp);         /* returns nonzero if key available */
int  input_getch(Input *inp);         /* blocks until key available, returns ASCII/scancode */
int  input_wait_any_key(Input *inp);  /* consumes a complete key, including extended scancode */
void input_drain_pending(Input *inp); /* consumes keys already buffered without waiting */
int  input_poll_quit(Input *inp);     /* returns 1 if window close requested */
void input_last_mouse_click(Input *inp, int *x, int *y);
void input_mouse_position(Input *inp, int *x, int *y, unsigned *serial);
SDL_Keymod input_last_key_modifiers(const Input *inp);
void input_set_timing_percent(Input *inp, int percent);
u32  input_scaled_milliseconds(const Input *inp, u32 milliseconds);
int  input_self_test(void);

/* Map SDL keysym to the DOS-compatible value the game expects.
 * Most keys map to ASCII. Arrow keys map to 0x00 + scancode (two-byte sequence). */
int  input_sdl_to_dos(SDL_Keycode sym, SDL_Keymod mod);

#endif /* MW_INPUT_H */
