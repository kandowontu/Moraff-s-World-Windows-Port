#ifndef MW_TRAINER_H
#define MW_TRAINER_H

#include "mw_game.h"

/* Native port of the resident TRAINER.ASM character editor. */
void trainer_run(Game *g, Character *player);
void trainer_draw_grid_test(Game *g, Character *player, int set,
                            int row, int column);
int  trainer_self_test(void);

#endif /* MW_TRAINER_H */
