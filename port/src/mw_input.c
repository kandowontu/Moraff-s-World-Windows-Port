#include "mw_input.h"
#include <string.h>

/* MW_PLATFORM_REPLACEMENT: SDL events replace WORLD check_key (0x26AF2),
 * func_26B38 and the DOS keyboard/INT 33h mouse layer. DOS two-byte extended-key
 * semantics are preserved because func_0F6E5 command dispatch depends on
 * them. The original INT 33h mouse hot-spot layer is only partly represented
 * by SDL click/hover handling; there is no original two-player branch. */

void input_init(Input *inp) {
    memset(inp, 0, sizeof(*inp));
}

static void input_push(Input *inp, int key, int x, int y, SDL_Keymod mods) {
    int next = (inp->tail + 1) % KEY_QUEUE_SIZE;
    if (next == inp->head) return; /* queue full, drop key */
    inp->keys[inp->tail] = key;
    inp->key_mods[inp->tail] = mods;
    inp->event_x[inp->tail] = x;
    inp->event_y[inp->tail] = y;
    inp->tail = next;
}

static int movement_keycode(SDL_Keycode sym) {
    switch (sym) {
    case SDLK_UP:
    case SDLK_DOWN:
    case SDLK_LEFT:
    case SDLK_RIGHT:
    case SDLK_HOME:
    case SDLK_END:
    case SDLK_PAGEUP:
    case SDLK_PAGEDOWN:
    case SDLK_KP_1:
    case SDLK_KP_2:
    case SDLK_KP_3:
    case SDLK_KP_4:
    case SDLK_KP_6:
    case SDLK_KP_7:
    case SDLK_KP_8:
    case SDLK_KP_9:
        return 1;
    default:
        return 0;
    }
}

static int extended_repeat_already_queued(const Input *inp, int scan,
                                          SDL_Keymod mods) {
    if (inp->head == inp->tail) return 0;
    int scan_index = (inp->tail + KEY_QUEUE_SIZE - 1) % KEY_QUEUE_SIZE;
    int zero_index = (scan_index + KEY_QUEUE_SIZE - 1) % KEY_QUEUE_SIZE;
    return scan_index != inp->head &&
           inp->keys[zero_index] == 0 &&
           inp->keys[scan_index] == scan &&
           inp->key_mods[scan_index] == mods;
}

void input_pump(Input *inp) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            inp->quit_requested = 1;
            break;
        case SDL_KEYDOWN: {
            SDL_Keymod mods = (SDL_Keymod)ev.key.keysym.mod;
            /* DOS keyboard repeat is what lets WORLD keep walking while a
             * cursor key is held.  SDL marks those later KEYDOWN events as
             * repeats; accept them only for movement so held menu commands
             * cannot fire repeatedly. */
            if (ev.key.repeat && !movement_keycode(ev.key.keysym.sym)) break;
            int dos_key = input_sdl_to_dos(ev.key.keysym.sym, mods);
            if (dos_key > 0) {
                input_push(inp, dos_key, -1, -1, mods);
            } else if (dos_key < 0) {
                if (ev.key.repeat &&
                    extended_repeat_already_queued(inp, -dos_key, mods))
                    break;
                /* Extended key: push 0 then scancode */
                input_push(inp, 0, -1, -1, mods);
                input_push(inp, -dos_key, -1, -1, mods);
            }
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
            inp->mouse_x = ev.button.x;
            inp->mouse_y = ev.button.y;
            if (ev.button.button == SDL_BUTTON_LEFT)
                input_push(inp, INPUT_MOUSE_CLICK, ev.button.x, ev.button.y,
                           KMOD_NONE);
            break;
        case SDL_MOUSEMOTION:
            inp->mouse_x = ev.motion.x;
            inp->mouse_y = ev.motion.y;
            inp->mouse_motion_serial++;
            break;
        case SDL_MOUSEWHEEL: {
            int amount = ev.wheel.y;
            if (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                amount = -amount;
            if (amount > 0)
                input_push(inp, INPUT_MOUSE_WHEEL_UP, inp->mouse_x,
                           inp->mouse_y, KMOD_NONE);
            else if (amount < 0)
                input_push(inp, INPUT_MOUSE_WHEEL_DOWN, inp->mouse_x,
                           inp->mouse_y, KMOD_NONE);
            break;
        }
        default:
            break;
        }
    }
}

int input_kbhit(Input *inp) {
    input_pump(inp);
    return inp->head != inp->tail;
}

int input_getch(Input *inp) {
    while (inp->head == inp->tail) {
        input_pump(inp);
        if (inp->quit_requested) return 0x1B; /* ESC on quit */
        SDL_Delay(10);
    }
    int key = inp->keys[inp->head];
    inp->last_key_mods = inp->key_mods[inp->head];
    if (key == INPUT_MOUSE_CLICK) {
        inp->last_mouse_x = inp->event_x[inp->head];
        inp->last_mouse_y = inp->event_y[inp->head];
    }
    inp->head = (inp->head + 1) % KEY_QUEUE_SIZE;
    return key;
}

int input_wait_any_key(Input *inp) {
    int key = input_getch(inp);

    /* DOS getch() represents arrows and function keys as two bytes: a zero
     * prefix followed by the scan code.  Modal "any key" screens must drain
     * both bytes or the scan code is mistaken for an ASCII command by the
     * next game loop (Down's 0x50, for example, is 'P' / Pockets). */
    if (key == 0)
        (void)input_getch(inp);
    return key;
}

int input_poll_quit(Input *inp) {
    return inp->quit_requested;
}

void input_last_mouse_click(Input *inp, int *x, int *y) {
    if (x) *x = inp->last_mouse_x;
    if (y) *y = inp->last_mouse_y;
}

void input_mouse_position(Input *inp, int *x, int *y, unsigned *serial) {
    if (x) *x = inp->mouse_x;
    if (y) *y = inp->mouse_y;
    if (serial) *serial = inp->mouse_motion_serial;
}

SDL_Keymod input_last_key_modifiers(const Input *inp) {
    return inp ? inp->last_key_mods : KMOD_NONE;
}

/* Map SDL keys to what the original DOS game expects.
 * Returns: >0 for ASCII, <0 for extended scancode (caller pushes 0 then -ret),
 *          0 for keys we don't map. */
int input_sdl_to_dos(SDL_Keycode sym, SDL_Keymod mod) {
    /* Native-only diagnostic shortcuts use otherwise unused function-key
     * chords, leaving every original WORLD key byte unchanged. */
    if (sym == SDLK_F12 && (mod & KMOD_CTRL) &&
        (mod & KMOD_SHIFT) && (mod & KMOD_ALT))
        return INPUT_MAX_CHARACTER;
    if (sym == SDLK_F5 && (mod & KMOD_CTRL)) return INPUT_MODEL_VIEWER;
    if (sym == SDLK_F6 && (mod & KMOD_CTRL)) return INPUT_DUNGEON_REROLL;
    if (sym == SDLK_F7 && (mod & KMOD_CTRL)) return INPUT_OPEN_FLOOR_TOGGLE;
    if (sym == SDLK_F8 && (mod & KMOD_CTRL)) return INPUT_TOWN_TELEPORT;
    if (sym == SDLK_F9 && (mod & KMOD_CTRL)) return INPUT_GOD_TOGGLE;
    if (sym == SDLK_F10 && (mod & KMOD_CTRL)) return INPUT_NOCLIP_TOGGLE;
    if (sym == SDLK_F11 && (mod & KMOD_CTRL)) return INPUT_WILDERNESS_TEST;
    if (sym == SDLK_F12 && (mod & KMOD_CTRL)) return INPUT_TRAINER;

    /* Standard ASCII range */
    if (sym >= SDLK_SPACE && sym <= SDLK_z) {
        int ch = (int)sym;
        if (mod & KMOD_SHIFT) {
            if (ch >= 'a' && ch <= 'z') ch -= 32; /* uppercase */
            /* Shifted number keys etc */
            else switch (ch) {
                case '1': ch = '!'; break;
                case '2': ch = '@'; break;
                case '3': ch = '#'; break;
                case '4': ch = '$'; break;
                case '5': ch = '%'; break;
                case '6': ch = '^'; break;
                case '7': ch = '&'; break;
                case '8': ch = '*'; break;
                case '9': ch = '('; break;
                case '0': ch = ')'; break;
                case '-': ch = '_'; break;
                case '=': ch = '+'; break;
                case '[': ch = '{'; break;
                case ']': ch = '}'; break;
                case ';': ch = ':'; break;
                case '\'': ch = '"'; break;
                case ',': ch = '<'; break;
                case '.': ch = '>'; break;
                case '/': ch = '?'; break;
            }
        }
        return ch;
    }

    switch (sym) {
    case SDLK_RETURN:    return 0x0D;
    case SDLK_ESCAPE:    return 0x1B;
    case SDLK_BACKSPACE: return 0x08;
    case SDLK_TAB:       return 0x09;

    /* Arrow keys → extended scancodes (DOS convention: getch returns 0, then scancode) */
    case SDLK_UP:        return -0x48;
    case SDLK_DOWN:      return -0x50;
    case SDLK_LEFT:      return -0x4B;
    case SDLK_RIGHT:     return -0x4D;

    /* Function keys */
    case SDLK_F1:        return -0x3B;
    case SDLK_F2:        return -0x3C;
    case SDLK_F3:        return -0x3D;
    case SDLK_F4:        return -0x3E;
    case SDLK_F5:        return -0x3F;
    case SDLK_F6:        return -0x40;
    case SDLK_F7:        return -0x41;
    case SDLK_F8:        return -0x42;
    case SDLK_F9:        return -0x43;
    case SDLK_F10:       return -0x44;

    /* Numpad.  SDL reports navigation-pad keys as KP digits on some
       keyboards when Num Lock is off, so retain their DOS navigation role. */
    case SDLK_KP_ENTER:  return 0x0D;
    case SDLK_KP_8:      return -0x48;
    case SDLK_KP_2:      return -0x50;
    case SDLK_KP_4:      return -0x4B;
    case SDLK_KP_6:      return -0x4D;
    case SDLK_KP_7:      return -0x47;
    case SDLK_KP_1:      return -0x4F;
    case SDLK_KP_9:      return -0x49;
    case SDLK_KP_3:      return -0x51;

    /* Home/End/PgUp/PgDn */
    case SDLK_HOME:      return -0x47;
    case SDLK_END:       return -0x4F;
    case SDLK_PAGEUP:    return -0x49;
    case SDLK_PAGEDOWN:  return -0x51;
    case SDLK_INSERT:    return -0x52;
    case SDLK_DELETE:    return -0x53;

    default: return 0;
    }
}
