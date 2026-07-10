#include "mw_input.h"
#include <string.h>

void input_init(Input *inp) {
    memset(inp, 0, sizeof(*inp));
}

static void input_push(Input *inp, int key) {
    int next = (inp->tail + 1) % KEY_QUEUE_SIZE;
    if (next == inp->head) return; /* queue full, drop key */
    inp->keys[inp->tail] = key;
    inp->tail = next;
}

void input_pump(Input *inp) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            inp->quit_requested = 1;
            break;
        case SDL_KEYDOWN: {
            if (ev.key.repeat) break;
            int dos_key = input_sdl_to_dos(ev.key.keysym.sym, (SDL_Keymod)ev.key.keysym.mod);
            if (dos_key > 0) {
                input_push(inp, dos_key);
            } else if (dos_key < 0) {
                /* Extended key: push 0 then scancode */
                input_push(inp, 0);
                input_push(inp, -dos_key);
            }
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
    inp->head = (inp->head + 1) % KEY_QUEUE_SIZE;
    return key;
}

int input_poll_quit(Input *inp) {
    return inp->quit_requested;
}

/* Map SDL keys to what the original DOS game expects.
 * Returns: >0 for ASCII, <0 for extended scancode (caller pushes 0 then -ret),
 *          0 for keys we don't map. */
int input_sdl_to_dos(SDL_Keycode sym, SDL_Keymod mod) {
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

    /* Numpad */
    case SDLK_KP_ENTER:  return 0x0D;

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
