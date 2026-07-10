#ifndef MW_INPUT_H
#define MW_INPUT_H

#include "mw_types.h"
#include <SDL.h>

/* Key queue — replaces DOS getch/kbhit.
 * The original game is fully synchronous: it calls getch() and blocks.
 * We buffer SDL key events into a ring buffer and drain from it. */

#define KEY_QUEUE_SIZE 64

typedef struct {
    int keys[KEY_QUEUE_SIZE];
    int head;
    int tail;
    int quit_requested;
} Input;

void input_init(Input *inp);
void input_pump(Input *inp);          /* call each frame — drains SDL events */
int  input_kbhit(Input *inp);         /* returns nonzero if key available */
int  input_getch(Input *inp);         /* blocks until key available, returns ASCII/scancode */
int  input_poll_quit(Input *inp);     /* returns 1 if window close requested */

/* Map SDL keysym to the DOS-compatible value the game expects.
 * Most keys map to ASCII. Arrow keys map to 0x00 + scancode (two-byte sequence). */
int  input_sdl_to_dos(SDL_Keycode sym, SDL_Keymod mod);

#endif /* MW_INPUT_H */
