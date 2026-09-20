#ifndef MR_GAME_H
#define MR_GAME_H

#include <stddef.h>
#include <stdint.h>

#include "mw_game.h"

/* Native entry point for the disassembly-driven Revenge port.  `directory`
 * contains the user's local Advanced 3.3 data and writable native saves. */
int mr_game_run(Game *host, const char *directory);

/* Headless coverage of BEGIN's menu/roster transition and the exact 320x200
 * composition surface.  This intentionally requires no DOSBox/emulator. */
int mr_game_self_test(const char *directory, char *error,
                      size_t error_size);

#endif
