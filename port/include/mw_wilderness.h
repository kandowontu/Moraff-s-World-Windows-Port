#ifndef MW_WILDERNESS_H
#define MW_WILDERNESS_H

#include "mw_types.h"

struct Game;

/* Returns nonzero when an outdoor dungeon entrance was used. */
int wilderness_run(struct Game *game, Character *player);
/* Opens a non-persistent wilderness sandbox and restores the dungeon state. */
int wilderness_test_run(struct Game *game, Character *player);
int wilderness_self_test(void);
void wilderness_draw_test(struct Game *game, Character *player);

#endif
