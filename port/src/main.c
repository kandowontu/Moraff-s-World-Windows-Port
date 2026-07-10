#include "mw_game.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static int has_game_data(const char *dir) {
    char test[300];
    snprintf(test, sizeof(test), "%s/DUNG.BIN", dir);
    if (file_exists(test)) return 1;
    snprintf(test, sizeof(test), "%s/dung.bin", dir);
    return file_exists(test);
}

static void find_data_dir(char *out, int out_sz, const char *argv0) {
    /* 1) Check CWD */
    if (getcwd(out, out_sz) && has_game_data(out)) return;

    /* 2) Check exe directory and ancestors (handles port/build/ layout) */
    char exe_dir[260];
    const char *base = SDL_GetBasePath();
    if (base) {
        strncpy(exe_dir, base, sizeof(exe_dir) - 1);
        exe_dir[sizeof(exe_dir) - 1] = '\0';
        SDL_free((void *)base);

        /* Strip trailing separator */
        int len = (int)strlen(exe_dir);
        if (len > 0 && (exe_dir[len-1] == '/' || exe_dir[len-1] == '\\'))
            exe_dir[--len] = '\0';

        /* Check exe dir itself */
        if (has_game_data(exe_dir)) {
            strncpy(out, exe_dir, out_sz);
            return;
        }

        /* Walk up directory tree (up to 3 levels for port/build/) */
        for (int i = 0; i < 3; i++) {
            char *sep = strrchr(exe_dir, '\\');
            if (!sep) sep = strrchr(exe_dir, '/');
            if (!sep) break;
            *sep = '\0';
            if (has_game_data(exe_dir)) {
                strncpy(out, exe_dir, out_sz);
                return;
            }
        }
    }

    /* 3) Fallback to CWD */
    if (!getcwd(out, out_sz)) strcpy(out, ".");
}

int main(int argc, char *argv[]) {
    /* Need SDL init early for SDL_GetBasePath */
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

    char data_dir[260];

    if (argc > 1) {
        strncpy(data_dir, argv[1], sizeof(data_dir) - 1);
        data_dir[sizeof(data_dir) - 1] = '\0';
    } else {
        find_data_dir(data_dir, sizeof(data_dir), argv[0]);
    }

    printf("Moraff's World - Native Port\n");
    printf("Data directory: %s\n", data_dir);

    if (!has_game_data(data_dir)) {
        fprintf(stderr, "ERROR: Game data not found in '%s'\n", data_dir);
        fprintf(stderr, "Place the executable in the game directory or pass the path as an argument.\n");
        SDL_Quit();
        return 1;
    }

    Game game;
    if (game_init(&game, data_dir) < 0) {
        fprintf(stderr, "Failed to initialize game\n");
        SDL_Quit();
        return 1;
    }

    game_run(&game);
    game_shutdown(&game);

    return 0;
}
