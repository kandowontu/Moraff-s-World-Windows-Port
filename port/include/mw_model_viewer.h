#ifndef MW_MODEL_VIEWER_H
#define MW_MODEL_VIEWER_H

struct Game;

enum {
    MODEL_VIEWER_WORLD = 0,
    MODEL_VIEWER_WALL = 1,
    MODEL_VIEWER_FONT = 2,
    MODEL_VIEWER_SET_COUNT = 3
};

/* Native asset-inspection extension opened by Ctrl+F5. */
void model_viewer_run(struct Game *g);
void model_viewer_draw_test(struct Game *g, int set, int index,
                            float zoom, float angle_degrees);
int model_viewer_self_test(struct Game *g);

#endif /* MW_MODEL_VIEWER_H */
