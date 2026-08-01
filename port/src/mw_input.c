#include "mw_input.h"
#include <string.h>

/* MW_PLATFORM_REPLACEMENT: SDL events replace WORLD check_key (0x26AF2),
 * func_26B38 and the DOS keyboard/INT 33h mouse layer. DOS two-byte extended-key
 * semantics are preserved because func_0F6E5 command dispatch depends on
 * them. SDL click/hover hit maps replace the original INT 33h hot-spot layer;
 * there is no original two-player branch. */

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

static int input_queue_free(const Input *inp) {
    int used = (inp->tail - inp->head + KEY_QUEUE_SIZE) % KEY_QUEUE_SIZE;
    return KEY_QUEUE_SIZE - 1 - used;
}

/* Enqueue one complete DOS key atomically.  An extended key must never leave
   a lone zero prefix in a nearly-full queue: the next getch() would otherwise
   block waiting for a scan byte or reinterpret a later ASCII key as one. */
static int input_push_dos_key(Input *inp, int dos_key, SDL_Keymod mods) {
    int needed = dos_key < 0 ? 2 : 1;
    if (!dos_key || input_queue_free(inp) < needed) return 0;
    if (dos_key < 0) {
        input_push(inp, 0, -1, -1, mods);
        input_push(inp, -dos_key, -1, -1, mods);
    } else {
        input_push(inp, dos_key, -1, -1, mods);
    }
    return 1;
}

enum {
    /* WORLD has no software repeat timer for F: it consumes the PC keyboard
       BIOS typematic stream through _kbhit/_getch.  These are the standard
       power-on defaults used by DOS: 500 ms delay and 10.9 characters/sec. */
    DOS_TYPEMATIC_DELAY_MS = 500,
    DOS_TYPEMATIC_PERIOD_MS = 92
};

static int original_repeatable_key(int dos_key) {
    /* Native commands begin above the byte range and deliberately do not
       auto-repeat. All original ASCII and extended BIOS keys do. */
    return dos_key != 0 && dos_key < INPUT_MOUSE_CLICK;
}

static void input_emit_typematic_repeat(Input *inp) {
    u32 now;
    if (!inp->repeat_held) return;
    now = SDL_GetTicks();
    if (!SDL_TICKS_PASSED(now, inp->repeat_next)) return;
    SDL_Keymod mods = SDL_GetModState();
    int dos_key = input_sdl_to_dos(inp->repeat_sym, mods);
    if (!original_repeatable_key(dos_key)) {
        inp->repeat_held = 0;
        inp->fight_repeating = 0;
        return;
    }
    if (inp->repeat_sym == SDLK_f &&
        !(mods & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)))
        inp->fight_repeating = 1;
    input_push_dos_key(inp, dos_key, mods);
    /* Do not flood the queue after a modal or paused window. */
    inp->repeat_next = now + DOS_TYPEMATIC_PERIOD_MS;
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
            /* Ignore host-generated repeat completely. WORLD received every
               held key from the BIOS typematic stream at one common cadence;
               accepting SDL repeat here would make movement platform-specific
               and would double-repeat alongside the native synthesizer. */
            if (ev.key.repeat) break;
            int dos_key = input_sdl_to_dos(ev.key.keysym.sym, mods);
            input_push_dos_key(inp, dos_key, mods);
            if (original_repeatable_key(dos_key)) {
                inp->repeat_held = 1;
                inp->repeat_sym = ev.key.keysym.sym;
                inp->repeat_next = SDL_GetTicks() + DOS_TYPEMATIC_DELAY_MS;
                inp->fight_repeating = 0;
            }
            break;
        }
        case SDL_KEYUP:
            if (inp->repeat_held && ev.key.keysym.sym == inp->repeat_sym) {
                inp->repeat_held = 0;
                inp->fight_repeating = 0;
            }
            break;
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
    input_emit_typematic_repeat(inp);
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
    const int keypad_numeric =
        !!(mod & KMOD_NUM) ^ !!(mod & KMOD_SHIFT);
    /* Native-only diagnostic shortcuts use otherwise unused function-key
     * chords, leaving every original WORLD key byte unchanged. */
    if (sym == SDLK_F12 && (mod & KMOD_CTRL) &&
        (mod & KMOD_SHIFT) && (mod & KMOD_ALT))
        return INPUT_MAX_CHARACTER;
    if (sym == SDLK_F2 && (mod & KMOD_CTRL)) return INPUT_BATTLE_SIMULATOR;
    if (sym == SDLK_F3 && (mod & KMOD_CTRL)) return INPUT_RANDOMIZE_FLOOR;
    if (sym == SDLK_F4 && (mod & KMOD_CTRL)) return INPUT_QUEST_BOSS_WARP;
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
    case SDLK_F11:       return -0x85;
    case SDLK_F12:       return -0x86;

    /* DOS BIOS returns ASCII keypad digits when numeric mode is active and a
       zero+scan pair otherwise. Holding Shift temporarily reverses Num Lock.
       WORLD uses that distinction in the wilderness: digits move 3 samples,
       navigation scans move 1. It never reads Shift state directly. */
    case SDLK_KP_ENTER:  return 0x0D;
    case SDLK_KP_0:      return keypad_numeric ? '0' : -0x52;
    case SDLK_KP_1:      return keypad_numeric ? '1' : -0x4F;
    case SDLK_KP_2:      return keypad_numeric ? '2' : -0x50;
    case SDLK_KP_3:      return keypad_numeric ? '3' : -0x51;
    case SDLK_KP_4:      return keypad_numeric ? '4' : -0x4B;
    case SDLK_KP_5:      return keypad_numeric ? '5' : -0x4C;
    case SDLK_KP_6:      return keypad_numeric ? '6' : -0x4D;
    case SDLK_KP_7:      return keypad_numeric ? '7' : -0x47;
    case SDLK_KP_8:      return keypad_numeric ? '8' : -0x48;
    case SDLK_KP_9:      return keypad_numeric ? '9' : -0x49;
    case SDLK_KP_PERIOD: return keypad_numeric ? '.' : -0x53;
    case SDLK_KP_DIVIDE: return '/';
    case SDLK_KP_MULTIPLY:return '*';
    case SDLK_KP_MINUS:  return '-';
    case SDLK_KP_PLUS:   return '+';
    case SDLK_KP_EQUALS: return '=';

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

int input_self_test(void) {
    int failures = 0;
    Input inp;
    static const SDL_Keycode keypad_key[10] = {
        SDLK_KP_0, SDLK_KP_1, SDLK_KP_2, SDLK_KP_3, SDLK_KP_4,
        SDLK_KP_5, SDLK_KP_6, SDLK_KP_7, SDLK_KP_8, SDLK_KP_9
    };
    static const int keypad_scan[10] = {
        0x52, 0x4F, 0x50, 0x51, 0x4B,
        0x4C, 0x4D, 0x47, 0x48, 0x49
    };

    if (DOS_TYPEMATIC_DELAY_MS != 500 || DOS_TYPEMATIC_PERIOD_MS != 92)
        failures++;
    for (int i = 0; i < 10; i++) {
        if (input_sdl_to_dos(keypad_key[i], KMOD_NONE) != -keypad_scan[i] ||
            input_sdl_to_dos(keypad_key[i], KMOD_NUM) != '0' + i ||
            input_sdl_to_dos(keypad_key[i], KMOD_SHIFT) != '0' + i ||
            input_sdl_to_dos(keypad_key[i], KMOD_NUM | KMOD_SHIFT) !=
                -keypad_scan[i])
            failures++;
    }
    if (input_sdl_to_dos(SDLK_KP_PERIOD, KMOD_NONE) != -0x53 ||
        input_sdl_to_dos(SDLK_KP_PERIOD, KMOD_NUM) != '.' ||
        input_sdl_to_dos(SDLK_KP_PLUS, KMOD_NONE) != '+' ||
        input_sdl_to_dos(SDLK_F11, KMOD_NONE) != -0x85 ||
        input_sdl_to_dos(SDLK_F12, KMOD_NONE) != -0x86)
        failures++;

    memset(&inp, 0, sizeof(inp));
    inp.repeat_held = 1;
    inp.repeat_sym = SDLK_f;
    inp.repeat_next = 0;
    input_emit_typematic_repeat(&inp);
    if (inp.head == inp.tail || input_getch(&inp) != 'f' ||
        !inp.fight_repeating)
        failures++;

    memset(&inp, 0, sizeof(inp));
    inp.repeat_held = 1;
    inp.repeat_sym = SDLK_DOWN;
    inp.repeat_next = 0;
    input_emit_typematic_repeat(&inp);
    if (input_getch(&inp) != 0 || input_getch(&inp) != 0x50)
        failures++;

    memset(&inp, 0, sizeof(inp));
    inp.tail = KEY_QUEUE_SIZE - 2; /* one usable slot remains */
    if (input_push_dos_key(&inp, -0x50, KMOD_NONE) ||
        inp.tail != KEY_QUEUE_SIZE - 2)
        failures++;
    return failures;
}
