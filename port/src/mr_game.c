#include "mr_game.h"

#include "mr_assets.h"
#include "mr_bios_font.h"
#include "mr_character.h"
#include "mr_data.h"
#include "mr_frontend.h"
#include "mr_render.h"
#include "mr_session.h"
#include "mr_town.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

typedef struct MRNativeScreen {
    uint8_t pixels[MR_RENDER_WIDTH * MR_RENDER_HEIGHT];
} MRNativeScreen;

typedef struct MRGameRuntime {
    Game *host;
    const char *resource_directory;
    const char *directory;
    char save_directory[512];
    MRNativeScreen screen;
    MRRenderLayout layout;
    MRViewportCache viewport_cache;
    MRData data;
    MRAssets assets;
    MRRoster roster;
    PaletteEntry saved_palette[256];
    int selected;
    int color_enabled;
    int text_color_cycle;
    int dungeon_text_color_cycle;
    int data_loaded;
    int assets_loaded;
} MRGameRuntime;

enum {
    MR_CGA_BLACK = 0,
    MR_CGA_CYAN = 1,
    MR_CGA_MAGENTA = 2,
    MR_CGA_WHITE = 3,
    /* Text-mode colors must not collide with SCREEN 1's remapped logical
     * colors 1..3.  Direct BIOS-glyph output uses this private palette bank
     * for the original 16 CGA text attributes. */
    MR_TEXT_COLOR_BASE = 16
};

static const uint8_t mr_cga_text_palette[16][3] = {
    {  0,   0,   0}, {  0,   0, 170}, {  0, 170,   0},
    {  0, 170, 170}, {170,   0,   0}, {170,   0, 170},
    {170,  85,   0}, {170, 170, 170}, { 85,  85,  85},
    { 85,  85, 255}, { 85, 255,  85}, { 85, 255, 255},
    {255,  85,  85}, {255,  85, 255}, {255, 255,  85},
    {255, 255, 255}
};

/* Literal QuickBASIC PLAY scores selected by DUNSMALL 05A0-05CB. They are
 * sent through the native MML parser only when this Revenge session has
 * sound enabled. */
static const char mr_music_temple[] =
    "T135O3L8CEFGL4<<C<G>>>L8CEFGL4<CL8<G>C>CEFL4GECEDL8G.L16AL8GFEDL4C.L8CL4EL8GGGF4L8<<G+L4F>>L8EFL4GECDL2C";
static const char mr_music_death[] =
    "T90O1MNL4F.FL8FL4F.G+L8GL4GL8FL4FL8EL4F.";
static const char mr_music_sleep[] =
    "T250O3MNL4E.G8>ED2C<E.G8>C<B2.F.>C8FE2CDC<AG2.E.G8>ED2C<E.G8>C<B2.F.>C8FE2CD<AB>C2.";
static const char mr_music_loading_review[] =
    "T135MNO3L4CD+F.L8D+DCD.D+16C4L4G>C";

/* NCD 0560-07E8 sends these records to QuickBASIC's printer stream. The
 * native build preserves the exact historical form in a local spool file. */
static const char mr_ncd_order_form[] =
    "\r\n"
    "Price:  US............ 10 US Dollars\r\n"
    "        Canada.........13 Canadian Dollars\r\n"
    "        Great Britain...8 British Pounds\r\n"
    "        Australia......11 Australian Dollars\r\n"
    "        Japan........1463 Japanese Yen\r\n"
    "\r\n"
    "Price includes all tax and shipping charges even to foreign countries!\r\n"
    "\r\n"
    "Please send  me the best game  available for IBM compatibles.  At this price,  I\r\n"
    "     realize that I am getting  the bargain of the century,  and I agree to tell\r\n"
    "     all of my  friends about it.  I understand  that I can  continue to play my\r\n"
    "     current favorite characters when I receive my advanced version of  Moraff's\r\n"
    "     Revenge.\r\n"
    "\r\n"
    "____ A check is included and is made out to `MORAFF'.\r\n"
    "\r\n"
    "____ Cash is included.\r\n"
    "\r\n"
    "My address is:      ______________________________________\r\n"
    "\r\n"
    "                    ______________________________________\r\n"
    "\r\n"
    "                    ______________________________________\r\n"
    "\r\n"
    "My phone number is: ______________________________________\r\n"
    "\r\n"
    "Send to:     Moraff's Revenge                  or call *** 1-800-842-6858 ***\r\n"
    "             815-A Brazos, #317\r\n"
    "             Austin, TX 78701-2509\r\n"
    "\r\n"
    "Please indicate disk size:       5 1/4 (Normal)      3 1/2 (PS/2, Laptops, Etc,)\r\n"
    "\r\n"
    "Please describe your computer's configuration (optional):\r\n"
    "\r\n"
    "     Computer:___________________________________________\r\n"
    "\r\n"
    "     Monitor:____________________________________________\r\n"
    "\r\n"
    "     Disk Drives:________________________________________\r\n"
    "\r\n"
    "     Memory:_____________________________________________\r\n"
    "\r\n"
    "     Printer:____________________________________________\r\n"
    "\r\n"
    "     Modem:______________________________________________\r\n"
    "\r\n"
    "Where you got this from:_________________________________\r\n"
    "\r\n"
    "Comments (greatly appreciated):_____________________________________________\r\n"
    "\r\n"
    "     _______________________________________________________________________\r\n"
    "\r\n\r\n";

static void mr_game_error(char *error, size_t error_size,
                          const char *message) {
    if (error && error_size) snprintf(error, error_size, "%s", message);
}

static uint8_t mr_dungeon_text_color(const MRGameRuntime *game, int index);
static void mr_session_redraw_viewports(MRGameRuntime *game,
                                        MRSession *session);

static void mr_game_join(char *out, size_t out_size, const char *directory,
                         const char *name) {
    size_t length = strlen(directory);
    const char *separator = length &&
        (directory[length - 1] == '/' || directory[length - 1] == '\\')
        ? "" : "/";
    snprintf(out, out_size, "%s%s%s", directory, separator, name);
}

static int mr_game_file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

/* C's rename() replaces an existing destination on POSIX, but the Microsoft
 * CRT implementation fails instead.  BEGIN updates NAME every time it leaves
 * the main menu, so use the native replace operation on Windows while keeping
 * the same-directory atomic rename used by the other platforms. */
static int mr_game_replace_file(const char *temporary,
                                const char *destination) {
#if defined(_WIN32)
    return MoveFileExA(temporary, destination,
                       MOVEFILE_REPLACE_EXISTING |
                       MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(temporary, destination) == 0;
#endif
}

static int mr_game_replace_file_self_test(char *error,
                                          size_t error_size) {
#if defined(_WIN32)
    char temp_directory[MAX_PATH + 1];
    char destination[MAX_PATH + 1];
    char temporary[MAX_PATH + 8];
    DWORD directory_length = GetTempPathA((DWORD)sizeof(temp_directory),
                                          temp_directory);
    if (!directory_length || directory_length >= sizeof(temp_directory) ||
        !GetTempFileNameA(temp_directory, "MRG", 0, destination)) {
        mr_game_error(error, error_size,
                      "Cannot create Revenge replacement self-test file");
        return 0;
    }
    int written = snprintf(temporary, sizeof(temporary), "%s.tmp",
                           destination);
    FILE *file = NULL;
    int ok = written >= 0 && written < (int)sizeof(temporary);
    if (ok) file = fopen(destination, "wb");
    if (!file) ok = 0;
    if (file) {
        int wrote = fputs("old", file) >= 0;
        if (fclose(file) != 0) wrote = 0;
        if (!wrote) ok = 0;
    }
    file = ok ? fopen(temporary, "wb") : NULL;
    if (!file) ok = 0;
    if (file) {
        int wrote = fputs("new", file) >= 0;
        if (fclose(file) != 0) wrote = 0;
        if (!wrote) ok = 0;
    }
    if (ok && !mr_game_replace_file(temporary, destination)) ok = 0;
    char contents[4] = {0};
    file = ok ? fopen(destination, "rb") : NULL;
    if (!file) ok = 0;
    if (file) {
        int read_ok = fread(contents, 1, 3, file) == 3;
        if (fclose(file) != 0) read_ok = 0;
        if (!read_ok || strcmp(contents, "new") != 0)
            ok = 0;
    }
    remove(temporary);
    remove(destination);
    if (!ok)
        mr_game_error(error, error_size,
                      "Windows existing-file replacement mismatch");
    return ok;
#else
    (void)error;
    (void)error_size;
    return 1;
#endif
}

/* BEGIN 008D-00AC reads the first sequential numeric record from NAME.
 * Advanced 3.3 ships this as "1\r\n\x1a"; an unconfigured copy contains
 * 10, which is the first-run setup sentinel. */
static int mr_name_parse_first_numeric(const uint8_t *bytes, size_t size,
                                       int *value) {
    if (!bytes || !value) return 0;
    size_t offset = 0;
    while (offset < size && bytes[offset] != 0x1a &&
           (bytes[offset] == ' ' || bytes[offset] == '\t' ||
            bytes[offset] == '\r' || bytes[offset] == '\n'))
        offset++;
    if (offset >= size || bytes[offset] == 0x1a) return 0;
    char record[64];
    size_t length = 0;
    while (offset < size && bytes[offset] != 0x1a &&
           bytes[offset] != '\r' && bytes[offset] != '\n' &&
           length + 1 < sizeof(record))
        record[length++] = (char)bytes[offset++];
    record[length] = 0;
    char *end = NULL;
    errno = 0;
    double parsed = strtod(record, &end);
    while (end && (*end == ' ' || *end == '\t')) end++;
    if (errno || !end || end == record || *end || !isfinite(parsed) ||
        parsed < -32768.0 || parsed > 32767.0)
        return 0;
    int integer = (int)parsed;
    if ((double)integer != parsed) return 0;
    *value = integer;
    return 1;
}

static int mr_game_read_name_state(MRGameRuntime *game, int *value,
                                   char *error, size_t error_size) {
    char path[512];
    mr_game_join(path, sizeof(path), game->directory, "NAME");
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (error && error_size)
            snprintf(error, error_size,
                     "Cannot read Revenge setup state: %s", path);
        return 0;
    }
    uint8_t bytes[64];
    size_t size = fread(bytes, 1, sizeof(bytes), file);
    int ok = !ferror(file) && mr_name_parse_first_numeric(bytes, size, value);
    fclose(file);
    if (!ok && error && error_size)
        snprintf(error, error_size,
                 "Malformed Revenge setup state: %s", path);
    return ok;
}

static int mr_game_write_name_state(MRGameRuntime *game, int value,
                                    char *error, size_t error_size) {
    char path[512], temporary[544];
    mr_game_join(path, sizeof(path), game->directory, "NAME");
    int written = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (written < 0 || written >= (int)sizeof(temporary)) {
        mr_game_error(error, error_size,
                      "Revenge setup-state path is too long");
        return 0;
    }
    remove(temporary);
    FILE *file = fopen(temporary, "wb");
    if (!file) {
        if (error && error_size)
            snprintf(error, error_size,
                     "Cannot write Revenge setup state: %s", path);
        return 0;
    }
    int ok = fprintf(file, "%d\r\n", value) >= 0 &&
             fputc(0x1a, file) != EOF;
    if (fclose(file) != 0) ok = 0;
    if (ok && !mr_game_replace_file(temporary, path)) ok = 0;
    if (!ok) remove(temporary);
    if (!ok && error && error_size)
        snprintf(error, error_size,
                 "Cannot write Revenge setup state: %s", path);
    return ok;
}

static int mr_game_create_directory(const char *path) {
#if defined(_WIN32)
    if (_mkdir(path) == 0 || errno == EEXIST) return 1;
#else
    if (mkdir(path, 0777) == 0 || errno == EEXIST) return 1;
#endif
    return 0;
}

static int mr_game_copy_file(const char *source, const char *destination,
                             int required, char *error,
                             size_t error_size) {
    if (mr_game_file_exists(destination)) return 1;
    FILE *input = fopen(source, "rb");
    if (!input) {
        if (!required && errno == ENOENT) return 1;
        if (error && error_size)
            snprintf(error, error_size, "Cannot read Revenge seed file: %s",
                     source);
        return 0;
    }
    char temporary[544];
    int written = snprintf(temporary, sizeof(temporary), "%s.tmp",
                           destination);
    if (written < 0 || written >= (int)sizeof(temporary)) {
        fclose(input);
        mr_game_error(error, error_size,
                      "Native Revenge temporary path is too long");
        return 0;
    }
    remove(temporary);
    FILE *output = fopen(temporary, "wb");
    if (!output) {
        fclose(input);
        if (error && error_size)
            snprintf(error, error_size,
                     "Cannot create native Revenge save file: %s",
                     destination);
        return 0;
    }
    uint8_t buffer[8192];
    int ok = 1;
    for (;;) {
        size_t count = fread(buffer, 1, sizeof(buffer), input);
        if (count && fwrite(buffer, 1, count, output) != count) {
            ok = 0;
            break;
        }
        if (count < sizeof(buffer)) {
            if (ferror(input)) ok = 0;
            break;
        }
    }
    if (fclose(input) != 0) ok = 0;
    if (fclose(output) != 0) ok = 0;
    if (ok && !mr_game_replace_file(temporary, destination)) ok = 0;
    if (!ok) remove(temporary);
    if (!ok && error && error_size)
        snprintf(error, error_size,
                 "Cannot seed native Revenge save file: %s", destination);
    return ok;
}

static int mr_game_write_seed_marker(const char *path, char *error,
                                     size_t error_size) {
    char temporary[544];
    int written = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (written < 0 || written >= (int)sizeof(temporary)) {
        mr_game_error(error, error_size,
                      "Native Revenge marker path is too long");
        return 0;
    }
    remove(temporary);
    FILE *file = fopen(temporary, "wb");
    if (!file) {
        if (error && error_size)
            snprintf(error, error_size,
                     "Cannot finish native Revenge save import: %s", path);
        return 0;
    }
    int ok = fputs("Moraff's Revenge native save import v1\n", file) >= 0;
    if (fclose(file) != 0) ok = 0;
    if (ok && !mr_game_replace_file(temporary, path)) ok = 0;
    if (!ok) remove(temporary);
    if (!ok && error && error_size)
        snprintf(error, error_size,
                 "Cannot finish native Revenge save import: %s", path);
    return ok;
}

static int mr_game_seed_save_directory(MRGameRuntime *game,
                                       const char *resource_directory,
                                       char *error, size_t error_size) {
    if (!game || !resource_directory || !resource_directory[0]) return 0;
    int written = snprintf(game->save_directory,
                           sizeof(game->save_directory), "%s-native",
                           resource_directory);
    if (written < 0 || written >= (int)sizeof(game->save_directory)) {
        mr_game_error(error, error_size,
                      "Native Revenge save-directory path is too long");
        return 0;
    }
    if (!mr_game_create_directory(game->save_directory)) {
        if (error && error_size)
            snprintf(error, error_size,
                     "Cannot create native Revenge save directory: %s",
                     game->save_directory);
        return 0;
    }
    char seed_marker[512];
    mr_game_join(seed_marker, sizeof(seed_marker), game->save_directory,
                 ".seed-complete");
    int first_seed = !mr_game_file_exists(seed_marker);

    /* BEGIN mutates NAME, while DUNSMALL mutates the roster, hall, numbered
     * character pairs, and the global 1.NUM/2.NUM monster tables. Keep the
     * user's original files as immutable import resources and seed one
     * relative native working copy. 7.NUM is read-only, but mr_world_load
     * consumes it from the same directory as those monster tables. */
    static const char *const required[] = {
        "NAME", "F5.COM", "F9.EXE", "1.NUM", "2.NUM", "7.NUM"
    };
    for (int index = 0;
         index < (int)(sizeof(required) / sizeof(required[0])); index++) {
        char source[512], destination[512];
        mr_game_join(source, sizeof(source), resource_directory,
                     required[index]);
        mr_game_join(destination, sizeof(destination), game->save_directory,
                     required[index]);
        if (!mr_game_copy_file(source, destination, 1, error, error_size))
            return 0;
    }

    /* Copy only original numbered pairs which exist, and only while seeding
     * the roster for the first time. Later native deletion and compaction
     * must not be undone on the next launch. */
    for (int slot = 1; first_seed && slot <= MR_ROSTER_CAPACITY; slot++) {
        char name[32], source[512], destination[512];
        snprintf(name, sizeof(name), "%d.EXE", slot);
        mr_game_join(source, sizeof(source), resource_directory, name);
        mr_game_join(destination, sizeof(destination), game->save_directory,
                     name);
        if (!mr_game_copy_file(source, destination, 0, error, error_size))
            return 0;
        snprintf(name, sizeof(name), "%d.BIN", slot);
        mr_game_join(source, sizeof(source), resource_directory, name);
        mr_game_join(destination, sizeof(destination), game->save_directory,
                     name);
        if (!mr_game_copy_file(source, destination, 0, error, error_size))
            return 0;
    }
    if (first_seed &&
        !mr_game_write_seed_marker(seed_marker, error, error_size))
        return 0;
    game->directory = game->save_directory;
    return 1;
}

static void mr_screen_clear(MRNativeScreen *screen, uint8_t color) {
    memset(screen->pixels, color, sizeof(screen->pixels));
}

static void mr_screen_pixel(MRNativeScreen *screen, int x, int y,
                            uint8_t color) {
    if (x < 0 || x >= MR_RENDER_WIDTH || y < 0 || y >= MR_RENDER_HEIGHT)
        return;
    screen->pixels[y * MR_RENDER_WIDTH + x] = color & 3;
}

static void mr_screen_fill(MRNativeScreen *screen, int x, int y, int width,
                           int height, uint8_t color) {
    int x1 = x + width, y1 = y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > MR_RENDER_WIDTH) x1 = MR_RENDER_WIDTH;
    if (y1 > MR_RENDER_HEIGHT) y1 = MR_RENDER_HEIGHT;
    for (int py = y; py < y1; py++)
        memset(screen->pixels + py * MR_RENDER_WIDTH + x, color & 3,
               (size_t)(x1 - x));
}

static void mr_screen_hline(MRNativeScreen *screen, int x0, int x1, int y,
                            uint8_t color) {
    if (x0 > x1) { int swap = x0; x0 = x1; x1 = swap; }
    mr_screen_fill(screen, x0, y, x1 - x0 + 1, 1, color);
}

static void mr_screen_vline(MRNativeScreen *screen, int x, int y0, int y1,
                            uint8_t color) {
    if (y0 > y1) { int swap = y0; y0 = y1; y1 = swap; }
    for (int y = y0; y <= y1; y++) mr_screen_pixel(screen, x, y, color);
}

static void mr_screen_line(MRNativeScreen *screen, int x0, int y0,
                           int x1, int y1, uint8_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        mr_screen_pixel(screen, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int twice = error * 2;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

static void mr_screen_box(MRNativeScreen *screen, int x, int y, int width,
                          int height, uint8_t color) {
    mr_screen_hline(screen, x, x + width - 1, y, color);
    mr_screen_hline(screen, x, x + width - 1, y + height - 1, color);
    mr_screen_vline(screen, x, y, y + height - 1, color);
    mr_screen_vline(screen, x + width - 1, y, y + height - 1, color);
}

static int mr_host_x(int x) {
    return x * LOGICAL_W / MR_RENDER_WIDTH;
}

static int mr_host_y(int y) {
    return y * LOGICAL_H / MR_RENDER_HEIGHT;
}

static uint8_t mr_basic_text_color(int color) {
    if (color < 0) color = 0;
    if (color > 15) color = 15;
    return (uint8_t)(MR_TEXT_COLOR_BASE + color);
}

static void mr_game_set_revenge_palette(MRGameRuntime *game) {
    Video *video = &game->host->video;
    video_set_palette(video, MR_CGA_BLACK, 0, 0, 0);
    video_set_palette(video, MR_CGA_CYAN, 0, 170, 170);
    video_set_palette(video, MR_CGA_MAGENTA, 170, 0, 170);
    video_set_palette(video, MR_CGA_WHITE, 255, 255, 255);
    for (int color = 0; color < 16; color++)
        video_set_palette(video, MR_TEXT_COLOR_BASE + color,
                          mr_cga_text_palette[color][0],
                          mr_cga_text_palette[color][1],
                          mr_cga_text_palette[color][2]);
}

/* BRUN30 8DAE-8E19 is the SCREEN 1 COLOR implementation reached by
 * DUNSMALL's apply_color_palette at 300C. The first argument selects the CGA
 * background register (low four bits, with bit 3 also enabling palette
 * intensity); the second selects palette zero/one by its low bit. */
static void mr_screen_one_palette_indices(int background, int palette,
                                          int indices[4]) {
    background &= 15;
    palette &= 1;
    int bright = background & 8;
    indices[0] = background;
    if (palette) {
        indices[1] = 3 + bright; /* cyan */
        indices[2] = 5 + bright; /* magenta */
        indices[3] = 7 + bright; /* light gray */
    } else {
        indices[1] = 2 + bright; /* green */
        indices[2] = 4 + bright; /* red */
        indices[3] = 6 + bright; /* brown */
    }
}

static void mr_game_apply_screen_one_palette(MRGameRuntime *game,
                                             const MRSession *session) {
    if (!game || !session || !game->color_enabled) return;
    int indices[4];
    mr_screen_one_palette_indices(session->effective_background_color,
                                  session->effective_palette_selector,
                                  indices);
    for (int logical = 0; logical < 4; ++logical) {
        int cga = indices[logical];
        video_set_palette(&game->host->video, logical,
                          mr_cga_text_palette[cga][0],
                          mr_cga_text_palette[cga][1],
                          mr_cga_text_palette[cga][2]);
    }
}

static void mr_screen_compose(MRGameRuntime *game) {
    Video *video = &game->host->video;
    for (int y = 0; y < LOGICAL_H; y++) {
        int source_y = y * MR_RENDER_HEIGHT / LOGICAL_H;
        for (int x = 0; x < LOGICAL_W; x++) {
            int source_x = x * MR_RENDER_WIDTH / LOGICAL_W;
            video->pixels[y * LOGICAL_W + x] =
                game->screen.pixels[source_y * MR_RENDER_WIDTH + source_x];
        }
    }
    video->dirty = 1;
}

static void mr_screen_compose_source_rect(MRGameRuntime *game,
                                          int source_left, int source_top,
                                          int source_right,
                                          int source_bottom) {
    /* Incremental DUNSMALL GET/PUT redraws alter only their viewport. Text
     * printed through BIOS output is not part of the SCREEN 1 backing array,
     * so recomposing the complete native frame here would incorrectly erase
     * queued notices and fixed labels. Copy only host pixels whose nearest
     * source sample lies inside the updated original rectangle. */
    if (!game) return;
    if (source_left < 0) source_left = 0;
    if (source_top < 0) source_top = 0;
    if (source_right >= MR_RENDER_WIDTH) source_right = MR_RENDER_WIDTH - 1;
    if (source_bottom >= MR_RENDER_HEIGHT)
        source_bottom = MR_RENDER_HEIGHT - 1;
    if (source_left > source_right || source_top > source_bottom) return;

    Video *video = &game->host->video;
    for (int y = 0; y < LOGICAL_H; y++) {
        int source_y = y * MR_RENDER_HEIGHT / LOGICAL_H;
        if (source_y < source_top || source_y > source_bottom) continue;
        for (int x = 0; x < LOGICAL_W; x++) {
            int source_x = x * MR_RENDER_WIDTH / LOGICAL_W;
            if (source_x < source_left || source_x > source_right) continue;
            video->pixels[y * LOGICAL_W + x] =
                game->screen.pixels[source_y * MR_RENDER_WIDTH + source_x];
        }
    }
    video->dirty = 1;
}

/* BRUN30's active character-output vector at DS:03B0+6 resolves to 5F0E.
 * Its printable branch reaches 272A, which issues BIOS INT 10h/AH=09h with
 * CX=1, BH=the current page, and BL=the current color.  Draw the corresponding
 * IBM BIOS 8x8 glyph directly; Revenge never uses Moraff's World's .FNT data. */
static void mr_screen_bios_char(Video *video, int x, int y, int width,
                                int height, uint8_t character,
                                uint8_t color) {
    if (!video || width <= 0 || height <= 0) return;
    const uint8_t *glyph = mr_bios_font_glyph(character);
    for (int target_y = 0; target_y < height; ++target_y) {
        int pixel_y = y + target_y;
        if (pixel_y < 0 || pixel_y >= LOGICAL_H) continue;
        int source_y = target_y * 8 / height;
        for (int target_x = 0; target_x < width; ++target_x) {
            int pixel_x = x + target_x;
            if (pixel_x < 0 || pixel_x >= LOGICAL_W) continue;
            int source_x = target_x * 8 / width;
            if (glyph[source_y] & (uint8_t)(1U << source_x))
                video->pixels[pixel_y * LOGICAL_W + pixel_x] = color;
        }
    }
    video->dirty = 1;
}

static void mr_screen_text(MRGameRuntime *game, int column, int row,
                           const char *text, uint8_t color) {
    if (!text || row < 0 || row >= MR_RENDER_TEXT_ROWS) return;
    Video *video = &game->host->video;
    int length = (int)strlen(text);
    for (int index = 0; index < length; index++) {
        int cell = column + index;
        if (cell < 0) continue;
        if (cell >= MR_RENDER_TEXT_COLUMNS) break;
        int target_w = mr_host_x((cell + 1) * 8) - mr_host_x(cell * 8);
        int target_h = mr_host_y((row + 1) * 8) - mr_host_y(row * 8);
        mr_screen_bios_char(video, mr_host_x(cell * 8), mr_host_y(row * 8),
                            target_w, target_h, (uint8_t)text[index], color);
    }
}

static int mr_screen_text_matches_zero_background(const MRGameRuntime *game,
                                                  int column, int row,
                                                  const char *text,
                                                  uint8_t color) {
    /* Test oracle for exact BIOS-text placement.  It deliberately rebuilds
     * the expected scaled cell from the ROM glyph rather than calling the
     * drawing routine under test. */
    if (!game || !game->host || !text) return 0;
    const Video *video = &game->host->video;
    for (int index = 0; text[index]; ++index) {
        int cell = column + index;
        if (cell < 0 || cell >= MR_RENDER_TEXT_COLUMNS || row < 0 ||
            row >= MR_RENDER_TEXT_ROWS)
            return 0;
        int x0 = mr_host_x(cell * 8);
        int x1 = mr_host_x((cell + 1) * 8);
        int y0 = mr_host_y(row * 8);
        int y1 = mr_host_y((row + 1) * 8);
        const uint8_t *glyph = mr_bios_font_glyph((uint8_t)text[index]);
        for (int y = y0; y < y1; ++y) {
            int source_y = (y - y0) * 8 / (y1 - y0);
            for (int x = x0; x < x1; ++x) {
                int source_x = (x - x0) * 8 / (x1 - x0);
                uint8_t expected =
                    (glyph[source_y] & (uint8_t)(1U << source_x))
                        ? color : 0;
                if (video->pixels[y * LOGICAL_W + x] != expected) return 0;
            }
        }
    }
    return 1;
}

static void mr_screen_text_attribute(MRGameRuntime *game, int column,
                                     int row, const char *text,
                                     uint8_t foreground,
                                     uint8_t background) {
    if (!game || !text || row < 0 || row >= MR_RENDER_TEXT_ROWS) return;
    Video *video = &game->host->video;
    for (int index = 0; text[index]; index++) {
        int cell = column + index;
        if (cell < 0) continue;
        if (cell >= MR_RENDER_TEXT_COLUMNS) break;
        int x0 = mr_host_x(cell * 8);
        int x1 = mr_host_x((cell + 1) * 8);
        int y0 = mr_host_y(row * 8);
        int y1 = mr_host_y((row + 1) * 8);
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                video->pixels[y * LOGICAL_W + x] = background;
        mr_screen_bios_char(video, x0, y0, x1 - x0, y1 - y0,
                            (uint8_t)text[index], foreground);
    }
    video->dirty = 1;
}

static int mr_screen_wrapped(MRGameRuntime *game, int column, int row,
                             int width, const char *text, uint8_t color) {
    if (!text || width < 1) return row;
    const char *cursor = text;
    char line[96];
    while (*cursor && row < MR_RENDER_TEXT_ROWS) {
        while (*cursor == ' ') cursor++;
        int length = 0;
        int last_space = -1;
        while (cursor[length] && cursor[length] != '\r' &&
               cursor[length] != '\n' && length < width) {
            if (cursor[length] == ' ') last_space = length;
            length++;
        }
        int consumed = length;
        if (cursor[length] && cursor[length] != '\r' &&
            cursor[length] != '\n' && last_space > 0) {
            length = last_space;
            consumed = last_space + 1;
        }
        if (length >= (int)sizeof(line)) length = (int)sizeof(line) - 1;
        memcpy(line, cursor, (size_t)length);
        line[length] = 0;
        mr_screen_text(game, column, row++, line, color);
        cursor += consumed;
        if (*cursor == '\r') cursor++;
        if (*cursor == '\n') cursor++;
    }
    return row;
}

/* QuickBASIC SCREEN 1 PRINT wraps at the physical 40-column boundary; it
 * does not reflow on words.  Use this for recovered literal PRINT streams,
 * keeping mr_screen_wrapped only for host-authored prose. */
static int mr_screen_fixed_wrap(MRGameRuntime *game, int column, int row,
                                int width, const char *text, uint8_t color) {
    if (!text || width < 1) return row;
    const char *cursor = text;
    int current_column = column;
    char line[MR_RENDER_TEXT_COLUMNS + 1];
    while (*cursor && row < MR_RENDER_TEXT_ROWS) {
        if (*cursor == '\r' || *cursor == '\n') {
            if (*cursor++ == '\r' && *cursor == '\n') cursor++;
            row++;
            current_column = 0;
            continue;
        }
        int available = width - current_column;
        if (available < 1) {
            row++;
            current_column = 0;
            continue;
        }
        int length = 0;
        while (cursor[length] && cursor[length] != '\r' &&
               cursor[length] != '\n' && length < available)
            length++;
        memcpy(line, cursor, (size_t)length);
        line[length] = 0;
        mr_screen_text(game, current_column, row, line, color);
        cursor += length;
        current_column += length;
        if (current_column >= width) {
            row++;
            current_column = 0;
        }
    }
    return row + (current_column > 0 ? 1 : 0);
}

/* DUNSMALL's help routine switches to the 80-column SCREEN 2 text grid at
 * C33B. Keep that coordinate system separate from SCREEN 1 while retaining
 * the same BIOS 8x8 glyph generator. */
static void mr_screen_text_80(MRGameRuntime *game, int column, int row,
                              const char *text, uint8_t color) {
    if (!text || row < 0 || row >= MR_RENDER_TEXT_ROWS) return;
    Video *video = &game->host->video;
    int length = (int)strlen(text);
    for (int index = 0; index < length; index++) {
        int cell = column + index;
        if (cell < 0) continue;
        if (cell >= 80) break;
        int target_w = mr_host_x((cell + 1) * 4) - mr_host_x(cell * 4);
        int target_h = mr_host_y((row + 1) * 8) - mr_host_y(row * 8);
        mr_screen_bios_char(video, mr_host_x(cell * 4), mr_host_y(row * 8),
                            target_w, target_h, (uint8_t)text[index], color);
    }
}

static void mr_screen_text_80_attribute(MRGameRuntime *game, int column,
                                        int row, const char *text,
                                        uint8_t foreground,
                                        uint8_t background,
                                        int foreground_visible) {
    if (!game || !text || row < 0 || row >= MR_RENDER_TEXT_ROWS) return;
    Video *video = &game->host->video;
    for (int index = 0; text[index]; index++) {
        int cell = column + index;
        if (cell < 0) continue;
        if (cell >= 80) break;
        int x0 = mr_host_x(cell * 4);
        int x1 = mr_host_x((cell + 1) * 4);
        int y0 = mr_host_y(row * 8);
        int y1 = mr_host_y((row + 1) * 8);
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                video->pixels[y * LOGICAL_W + x] = background;
        if (foreground_visible)
            mr_screen_bios_char(video, x0, y0, x1 - x0, y1 - y0,
                                (uint8_t)text[index], foreground);
    }
    video->dirty = 1;
}

static void mr_screen_present(MRGameRuntime *game) {
    mr_screen_compose(game);
    video_present(&game->host->video);
}

static void mr_screen_message(MRGameRuntime *game, const char *line1,
                              const char *line2) {
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    if (line1) mr_screen_text(game, 0, 1, line1, MR_CGA_WHITE);
    if (line2) mr_screen_text(game, 0, 3, line2, MR_CGA_CYAN);
    mr_screen_text(game, 0, 23, "HIT ANY KEY TO CONTINUE", MR_CGA_MAGENTA);
    video_present(&game->host->video);
    input_wait_any_key(&game->host->input);
}

static void mr_screen_wait_milliseconds(MRGameRuntime *game,
                                        uint32_t milliseconds) {
    uint32_t duration = input_scaled_milliseconds(&game->host->input,
                                                  milliseconds);
    uint32_t start = SDL_GetTicks();
    while (!input_poll_quit(&game->host->input) &&
           SDL_GetTicks() - start < duration) {
        input_pump(&game->host->input);
        SDL_Delay(10);
    }
}

static void mr_original_keyboard_drain(MRGameRuntime *game) {
    /* DUNSMALL 2FCB-2FF6: eighteen nonblocking INKEY$ polls. */
    input_drain_pending_polls(&game->host->input,
                              MR_KEYBOARD_DRAIN_POLLS);
}

static void mr_screen_timed_message(MRGameRuntime *game, const char *line,
                                    uint32_t milliseconds) {
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    mr_screen_text(game, 0, 1, line, MR_CGA_WHITE);
    video_present(&game->host->video);
    mr_screen_wait_milliseconds(game, milliseconds);
}

static void mr_play_session_music(MRGameRuntime *game,
                                  const MRSession *session,
                                  const char *mml) {
    if (!game || !session || session->sound_disabled || !mml) return;
    mw_audio_play_qbasic_mml(&game->host->audio, mml);
    /* Every successful music selector reaches 05CB-05EE: PLAY MBX queues the
     * background score, clears the shared string, then performs the fixed
     * eighteen-INKEY$ drain. The former native wrapper omitted 05EE, letting
     * held menu/fight input leak across loading, inns, temple, and death. */
    mr_original_keyboard_drain(game);
}

/* BEGIN 09AE-0A09: color mode advances a 0..6 counter and applies BASIC
 * text attribute 8+counter. Monochrome mode leaves the default attribute 7
 * untouched. */
static uint8_t mr_begin_advance_text_color(MRGameRuntime *game) {
    if (!game->color_enabled) return mr_basic_text_color(7);
    game->text_color_cycle++;
    if (game->text_color_cycle > 6) game->text_color_cycle = 0;
    return mr_basic_text_color(8 + game->text_color_cycle);
}

static uint8_t mr_begin_current_text_color(const MRGameRuntime *game) {
    if (!game || !game->color_enabled) return mr_basic_text_color(7);
    return mr_basic_text_color(8 + game->text_color_cycle);
}

static int mr_begin_wait_upper_key(MRGameRuntime *game) {
    int key = input_wait_any_key(&game->host->input);
    /* BEGIN 0793-07A6 subtracts 20h from every byte greater than 60h; it
     * does not perform the usual upper-bound check for 'z'. */
    if (key > 0x60 && key <= 0xff) key -= 0x20;
    return key;
}

static void mr_begin_wait_read_delay(MRGameRuntime *game) {
    /* BEGIN 02A4-02EB waits 15 TIMER seconds. INKEY$ values are consumed,
     * but only the single-byte Ctrl-X value (18h at DS:243E) bypasses it. */
    uint32_t duration = input_scaled_milliseconds(&game->host->input, 15000);
    uint32_t start = SDL_GetTicks();
    while (!input_poll_quit(&game->host->input) &&
           SDL_GetTicks() - start < duration) {
        if (input_kbhit(&game->host->input)) {
            int key = input_wait_any_key(&game->host->input);
            if (key == 0x18) break;
        }
        SDL_Delay(10);
    }
}

static void mr_begin_first_run_setup(MRGameRuntime *game) {
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    mr_screen_text_80(game, 0, 0, "Color (Y or N)?",
                      mr_basic_text_color(7));
    video_present(&game->host->video);
    for (;;) {
        int key = mr_begin_wait_upper_key(game);
        if (key == 'Y') {
            game->color_enabled = 1;
            break;
        }
        if (key == 'N') {
            game->color_enabled = 0;
            break;
        }
        if (input_poll_quit(&game->host->input)) return;
    }

    /* BEGIN 010B-017C: 80-column copyright page. LOCATE uses one-based
     * coordinates, hence the zero-based rows below. begin_print_hit_any_key
     * falls through to the common blocking key routine at 0778. */
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    uint8_t color = mr_begin_advance_text_color(game);
    mr_screen_text_80(game, 0, 0,
        "                     MORAFF'S REVENGE ADVANCED VERSION 3.3", color);
    color = mr_begin_advance_text_color(game);
    mr_screen_text_80(game, 0, 9,
        "              Copyright 1989 by Steve Moraff.  All rights reserved.",
        color);
    color = mr_begin_advance_text_color(game);
    mr_screen_text_80(game, 0, 11,
        "       Brun30.exe copyright 1982-1987 Microsoft Corp. All rights reserved.",
        color);
    color = mr_begin_advance_text_color(game);
    mr_screen_text_80(game, 0, 19,
        " IBM is a registered trademark of International Business Machines Corporation.",
        color);
    color = mr_begin_advance_text_color(game);
    mr_screen_text_80(game, 34, 24, "HIT ANY KEY", color);
    video_present(&game->host->video);
    (void)mr_begin_wait_upper_key(game);
    if (input_poll_quit(&game->host->input)) return;

    /* BEGIN 017F-0305 resets the color cycle and switches to 40 columns.
     * The blank PRINT records deliberately leave two rows between sections. */
    game->text_color_cycle = 0;
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    color = mr_begin_advance_text_color(game);
    static const char *const role_page[] = {
        "   This is a  role  playing  game.  That",
        "means you will  actually be a fighter or",
        "a wizard.  You will  venture into a deep",
        "dungeon  in  search  of   adventure  and",
        "treasure."
    };
    static const char *const experience_page[] = {
        "   Each time you kill a monster you will",
        "obtain experience.  When you have enough",
        "experience, you will get more power. The",
        "more experience you get, the more power-",
        "ful you become."
    };
    static const char *const help_page[] = {
        "   There are a lot of  instructions  for",
        "this game.  If the instructions were not",
        "needed they would not be there. The help",
        "files contain the most  important infor-",
        "mation. If you read them all, success is",
        "not far away."
    };
    for (int row = 0; row < 5; row++)
        mr_screen_text(game, 0, row, role_page[row], color);
    color = mr_begin_advance_text_color(game);
    for (int row = 0; row < 5; row++)
        mr_screen_text(game, 0, 7 + row, experience_page[row], color);
    color = mr_begin_advance_text_color(game);
    for (int row = 0; row < 6; row++)
        mr_screen_text(game, 0, 14 + row, help_page[row], color);
    color = mr_begin_advance_text_color(game);
    mr_screen_text(game, 11, 24, "Please read this...", color);
    video_present(&game->host->video);
    mr_begin_wait_read_delay(game);
    mr_screen_compose(game);
    for (int row = 0; row < 5; row++)
        mr_screen_text(game, 0, row, role_page[row],
                       game->color_enabled
                           ? mr_basic_text_color(9)
                           : mr_basic_text_color(7));
    for (int row = 0; row < 5; row++)
        mr_screen_text(game, 0, 7 + row, experience_page[row],
                       game->color_enabled
                           ? mr_basic_text_color(10)
                           : mr_basic_text_color(7));
    for (int row = 0; row < 6; row++)
        mr_screen_text(game, 0, 14 + row, help_page[row],
                       game->color_enabled
                           ? mr_basic_text_color(11)
                           : mr_basic_text_color(7));
    mr_screen_text(game, 11, 24, "   HIT ANY KEY     ", color);
    video_present(&game->host->video);
    (void)mr_begin_wait_upper_key(game);
}

static void mr_begin_draw_menu(MRGameRuntime *game) {
    /* BEGIN 0308-038E, in the exact 40-column PRINT/LOCATE layout. Choice
     * four is the sole integration adaptation: DOS becomes Moraff's World. */
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    uint8_t color = mr_begin_advance_text_color(game);
    mr_screen_text(game, 12, 0, "MENU", color); /* PRINT TAB(13); */
    color = mr_begin_advance_text_color(game);
    mr_screen_text(game, 0, 1, "1...CREATE NEW CHARACTER", color);
    color = mr_begin_advance_text_color(game);
    mr_screen_text(game, 0, 2, "2...PLAY GAME", color);
    color = mr_begin_advance_text_color(game);
    mr_screen_text(game, 0, 3, "3...VISIT HALL OF FAME", color);
    color = mr_begin_advance_text_color(game);
    mr_screen_text(game, 0, 4,
                   "4...SAVE AND RETURN TO MORAFF'S WORLD", color);
    color = mr_begin_advance_text_color(game);
    mr_screen_text(game, 0, 5,
                   "5...ORDER MORAFF'S REVENGE ADVANCED VER.", color);
    mr_screen_text(game, 0, 9,
                   "NOTE: CTRL-X SKIPS THE PREVIOUS SCREEN", color);
    video_present(&game->host->video);
}

static uint8_t mr_ncd_native_color(const MRGameRuntime *game,
                                   int original_color) {
    /* NCD is 40-column SCREEN 0 text, so its original attributes 9..14 are
     * representable directly; they are not SCREEN 1 logical colors. */
    if (!game || !game->color_enabled) return mr_basic_text_color(7);
    if (original_color < 0 || original_color > 15)
        return mr_basic_text_color(7);
    return mr_basic_text_color(original_color);
}

static uint8_t mr_ncd_advance_color(const MRGameRuntime *game,
                                    int *next_color) {
    int applied = next_color ? *next_color : 9;
    if (next_color) {
        (*next_color)++;
        if (*next_color == 15) *next_color = 9;
    }
    return mr_ncd_native_color(game, applied);
}

static int mr_ncd_write_order_form(MRGameRuntime *game) {
    char path[512];
    mr_game_join(path, sizeof(path), game->directory,
                 "REVENGE_ORDER_FORM.TXT");
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    int ok = fwrite(mr_ncd_order_form, 1, strlen(mr_ncd_order_form), file) ==
             strlen(mr_ncd_order_form);
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static void mr_ncd_show_ordering_information(MRGameRuntime *game) {
    /* BEGIN 0A37-0A67 writes color_flag+109 before chaining NCD. NCD
     * 0080-00C5 subtracts 109, restores NAME, and jumps directly to 02AE.
     * Therefore the Advanced 3.3 menu path begins on this exact 40-column
     * screen; the earlier 80-column sales pitch is a direct-NCD shareware
     * path and is not reachable from BEGIN option five. */
    int next_color = 9;
    uint8_t color;
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);

    color = mr_ncd_advance_color(game, &next_color);       /* 02C3 */
    mr_screen_text(game, 0, 0, "To order  this  exciting  computer game,", color);
    mr_screen_text(game, 0, 1, "   send  $10 (US),  $13  Canadian,  or 8", color);
    mr_screen_text(game, 0, 2, "   British pounds cash or check to:", color);

    color = mr_ncd_advance_color(game, &next_color);       /* 02F8 */
    mr_screen_text(game, 0, 4, "     Moraff's Revenge", color);
    mr_screen_text(game, 0, 5, "     815-A Brazos, #317", color);
    mr_screen_text(game, 0, 6, "     Austin, TX 78701-2509", color);

    /* 032D-035D emits one logical line with explicit COLOR 10/14/10
     * changes between its three PRINT-with-semicolon fragments. */
    mr_screen_text(game, 0, 8, "Or call  ****  ",
                   mr_ncd_native_color(game, 10));
    mr_screen_text(game, 15, 8, "1-800-842-6858  ",
                   mr_ncd_native_color(game, 14));
    mr_screen_text(game, 31, 8, "****",
                   mr_ncd_native_color(game, 10));

    (void)mr_ncd_advance_color(game, &next_color);         /* 036F */
    color = mr_ncd_advance_color(game, &next_color);       /* 0374 */
    mr_screen_text(game, 0, 10, "   Note that  you  may  continue to  use", color);
    mr_screen_text(game, 0, 11, "   your current favorite characters with", color);
    mr_screen_text(game, 0, 12, "   the advanced version of the game.", color);

    color = mr_ncd_advance_color(game, &next_color);       /* 03A9 */
    mr_screen_text(game, 0, 14, "All orders  will be shipped the ", color);
    mr_screen_text(game, 32, 14, "SAME DAY",
                   mr_ncd_native_color(game, 14));
    mr_screen_text(game, 0, 15, "   ",
                   mr_ncd_native_color(game, 14));
    mr_screen_text(game, 3, 15, "they are received.",
                   mr_ncd_native_color(game, 13));

    (void)mr_ncd_advance_color(game, &next_color);         /* 03F6 */
    color = mr_ncd_native_color(game, 11);                 /* 03FB */
    mr_screen_text(game, 0, 17, "If you  have a  printer,  turn it on and", color);
    mr_screen_text(game, 0, 18, "   then hit `P'.", color);

    color = mr_ncd_advance_color(game, &next_color);       /* 0425 */
    mr_screen_text(game, 0, 20, "All orders include a useful ", color);
    mr_screen_text(game, 28, 20, "FREE ",
                   mr_ncd_native_color(game, 14));
    mr_screen_text(game, 33, 20, "gift.",
                   mr_ncd_native_color(game, 9));

    color = mr_ncd_advance_color(game, &next_color);       /* 045A */
    mr_screen_text(game, 0, 23, "Hit any  other key to return to the main", color);
    mr_screen_text(game, 0, 24, "   menu.", color);
    video_present(&game->host->video);

    int key = input_wait_any_key(&game->host->input);
    if (key == 'P' || key == 'p') {
        if (!mr_ncd_write_order_form(game))
            mr_screen_message(game, "PRINTER OUTPUT FAILED",
                              "CANNOT WRITE REVENGE_ORDER_FORM.TXT");
    }
}

static int mr_read_dos_key(Input *input, int *scan) {
    int first = input_getch(input);
    if (scan) *scan = 0;
    if (first == 0 && scan) *scan = input_getch(input);
    else if (first == 0) (void)input_getch(input);
    return first;
}

static int mr_read_original_nonempty_key(MRGameRuntime *game,
                                         MRSession *session) {
    /* DUNSMALL 2F71-2F84 is more than a blocking raw INKEY$ read: after a
     * nonempty key it unconditionally calls 2F43, clamping all six character
     * attributes to at least one. Several town, pause, guild, spell-result,
     * and loot paths depend on that otherwise surprising side effect. */
    int key = input_wait_any_key(&game->host->input);
    if (session) mr_clamp_character_attributes(&session->save.character);
    return key;
}

enum {
    MR_DOS_KEY_IDLE_MONSTER_REACHED_PLAYER = -1,
    MR_MODAL_CONTACT_KEEP_SAMPLE = 0,
    MR_MODAL_CONTACT_SUBSTITUTE_BLANK = 1,
    MR_MODAL_CONTACT_READ_FRESH_KEY = 2,
    MR_MAIN_INPUT_NO_IDLE_POLL = 0,
    MR_MAIN_INPUT_SAMPLE_THEN_POLL = 1,
    MR_MAIN_INPUT_POLL_THEN_SAMPLE = 2
};

static int mr_modal_contact_input_policy(int occupant,
                                         int combat_initialized,
                                         int sampled_key) {
    if (occupant <= 0) return MR_MODAL_CONTACT_KEEP_SAMPLE;
    if (combat_initialized == 1) return MR_MODAL_CONTACT_READ_FRESH_KEY;
    return sampled_key < 0 ? MR_MODAL_CONTACT_SUBSTITUTE_BLANK
                           : MR_MODAL_CONTACT_KEEP_SAMPLE;
}

static int mr_main_input_poll_order(MRSessionPhase phase,
                                    int dungeon_level) {
    if (phase == MR_SESSION_COMBAT) return MR_MAIN_INPUT_POLL_THEN_SAMPLE;
    if (phase == MR_SESSION_EXPLORING && dungeon_level != 0)
        return MR_MAIN_INPUT_SAMPLE_THEN_POLL;
    return MR_MAIN_INPUT_NO_IDLE_POLL;
}

static int mr_read_dos_key_with_idle_monsters(MRGameRuntime *game,
                                               MRSession *session,
                                               int *scan) {
    /* DUNSMALL BF60-BFB9 calibrates a tight one-second loop and divides its
     * count by 326. 7EEC then multiplies its random interval by that factor.
     * Running exactly 326 normalized polls per real second with calibration
     * one is the algebraically equivalent native clock: it preserves one
     * QuickBASIC RND consumption per original empty-INKEY$ poll without
     * making monster speed depend on the host CPU. */
    const double polls_per_second = 326.0;
    const uint64_t frequency = SDL_GetPerformanceFrequency();
    uint64_t previous = SDL_GetPerformanceCounter();
    /* Both live dungeon loops execute 7EEC once on their first pass even
     * when a key is already buffered. Exploration samples INKEY$ first
     * (087F, then 08F3); combat reverses that order (86E2, then 86E5). */
    double pending_polls = 1.0;

    /* DUNSMALL has two live INKEY$ loops: exploration at 087F-092F and
     * combat at 85BA-86FB. Both call 7EEC once per dungeon pass, including
     * the pass which returns a queued key. Town skips that helper, so retain
     * a blocking read there. */
    int input_order = session
        ? mr_main_input_poll_order(
              session->phase,
              (int)floor(session->save.character.dungeon_level))
        : MR_MAIN_INPUT_NO_IDLE_POLL;
    if (input_order == MR_MAIN_INPUT_NO_IDLE_POLL)
        return mr_read_dos_key(&game->host->input, scan);
    if (scan) *scan = 0;
    for (;;) {
        if (input_poll_quit(&game->host->input)) return 0x1B;

        uint64_t now = SDL_GetPerformanceCounter();
        pending_polls += (double)(now - previous) * polls_per_second /
                         (double)frequency;
        previous = now;
        while (pending_polls >= 1.0) {
            MRMonsterAdvanceResult advance;
            int pending_key = -1;
            int pending_scan = 0;
            if (input_order == MR_MAIN_INPUT_SAMPLE_THEN_POLL &&
                input_kbhit(&game->host->input))
                pending_key = mr_read_dos_key(&game->host->input,
                                               &pending_scan);
            pending_polls -= 1.0;
            if (mr_session_idle_monster_poll(session, 1.0, &advance)) {
                if (advance.code == MR_MONSTER_ADVANCE_REACHED_PLAYER &&
                    session->phase == MR_SESSION_EXPLORING) {
                    /* 08F6-091E, outside 7EEC itself, notices the occupied
                     * player cell and transfers to 803A. */
                    (void)mr_session_begin_combat(
                        session, advance.global_index);
                    return MR_DOS_KEY_IDLE_MONSTER_REACHED_PLAYER;
                }
                if (advance.code == MR_MONSTER_ADVANCE_MOVED) {
                    /* DUNSMALL 7749-78D4 updates the affected cached view
                     * immediately after 70A1 moves a visible monster, even
                     * while INKEY$ remains empty.  The former bridge changed
                     * occupancy but retained a pane containing the sprite at
                     * its old position.  Redrawing all four small panes is a
                     * final-pixel-equivalent native replacement for those
                     * direction-specific GET/PUT branches, and deliberately
                     * leaves the map and queued top text untouched. */
                    mr_session_redraw_viewports(game, session);
                }
            }
            if (input_order == MR_MAIN_INPUT_POLL_THEN_SAMPLE &&
                input_kbhit(&game->host->input))
                return mr_read_dos_key(&game->host->input, scan);
            if (pending_key >= 0) {
                if (scan) *scan = pending_scan;
                return pending_key;
            }
        }
        SDL_Delay(1);
    }
}

/* Direct controller translation of DUNSMALL 7DC9-7E8C. Unlike the ordinary
 * blocking helper at 2F71, this path samples INKEY$, runs 7EEC once even when
 * a key was already waiting, and keeps polling while the player's cell is
 * empty. It is shared by spell-level, spell-choice, wand, pill, battle-item,
 * drop-treasure, defeat-return, and coin-loot prompts. */
static int mr_read_modal_key_with_idle_monsters_ex(
    MRGameRuntime *game, MRSession *session, int *occupancy_interrupted) {
    const double polls_per_second = 326.0;
    const uint64_t frequency = SDL_GetPerformanceFrequency();
    uint64_t previous = SDL_GetPerformanceCounter();
    double pending_polls = 1.0; /* 7DC9 performs its first pass immediately. */
    int pending_key = -1;

    if (!game || !session) return 0x1B;
    if (occupancy_interrupted) *occupancy_interrupted = 0;
    for (;;) {
        if (pending_key < 0 && input_kbhit(&game->host->input)) {
            int scan = 0;
            pending_key = mr_read_dos_key(&game->host->input, &scan);
            (void)scan;
        }
        if (input_poll_quit(&game->host->input)) return 0x1B;

        uint64_t now = SDL_GetPerformanceCounter();
        pending_polls += (double)(now - previous) * polls_per_second /
                         (double)frequency;
        previous = now;
        while (pending_polls >= 1.0) {
            MRMonsterAdvanceResult advance;
            pending_polls -= 1.0;
            if (mr_session_idle_monster_poll(session, 1.0, &advance) &&
                advance.code == MR_MONSTER_ADVANCE_MOVED)
                mr_session_redraw_viewports(game, session);

            int player_x = (int)floor(session->save.character.player_x);
            int player_y = (int)floor(session->save.character.player_y);
            int occupant = mr_session_monster_at(session, player_x, player_y);
            if (occupant > 0) {
                /* Once B60E has been set by combat initialization, 7E41
                 * tail-calls raw 2F71 instead of setting B578.  The
                 * INKEY$ sample taken at 7DCF is not reused on this path:
                 * even when it was nonempty, the tail call waits for a new
                 * raw key and then applies 2F43's attribute clamp. */
                int contact_policy = mr_modal_contact_input_policy(
                    occupant, session->combat_initialized_flag, pending_key);
                if (contact_policy == MR_MODAL_CONTACT_READ_FRESH_KEY) {
                    return mr_read_original_nonempty_key(game, session);
                }
                /* Before the first 844E combat initialization, 7E4A-7E8B
                 * substitutes a single blank only when the sampled key was
                 * empty, sets B578, and returns without changing ownership.
                 * Only the 1918 drop prompt consumes B578; other callers
                 * continue with the sampled key/blank and the outer loop
                 * observes the occupied cell afterward. */
                if (occupancy_interrupted) *occupancy_interrupted = 1;
                return contact_policy == MR_MODAL_CONTACT_SUBSTITUTE_BLANK
                    ? ' ' : pending_key;
            }
            if (pending_key >= 0) return pending_key;
        }
        SDL_Delay(1);
    }
}

static int mr_read_modal_key_with_idle_monsters(MRGameRuntime *game,
                                                MRSession *session) {
    return mr_read_modal_key_with_idle_monsters_ex(game, session, NULL);
}

static void mr_begin_roster_display_name(const char *name,
                                         char display[39]) {
    size_t length = name ? strlen(name) : 0;
    if (length <= 35) {
        snprintf(display, 39, "%s", name ? name : "");
        return;
    }
    memcpy(display, name, 35);
    memcpy(display + 35, "...", 4);
}

static void mr_begin_draw_roster(MRGameRuntime *game, int blink_visible) {
    /* BEGIN 042D-0574. This is an 80-column selector with all ten names on
     * screen; the selected row uses BASIC's foreground+16 blink attribute
     * and, in color mode, background 12. */
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    uint8_t current = mr_begin_current_text_color(game);
    uint8_t name_color = mr_basic_text_color(
        game->color_enabled ? 10 : 7);
    mr_screen_text_80(game, 0, 0, "Choose a character:", current);
    for (int index = 0; index < game->roster.count; index++) {
        char display[39];
        mr_begin_roster_display_name(game->roster.names[index], display);
        mr_screen_text_80(game, 0, 2 + index, display, name_color);
    }
    mr_screen_text_80(game, 39, 2, "Use arrow keys to", current);
    mr_screen_text_80(game, 41, 3, "highlight character.", current);
    mr_screen_text_80(game, 39, 5, "Hit return to make", current);
    mr_screen_text_80(game, 41, 6, "selection.", current);
    mr_screen_text_80(game, 39, 8, "Hit `Esc' to restart.", current);
    if (game->selected >= 0 && game->selected < game->roster.count) {
        char display[39];
        mr_begin_roster_display_name(game->roster.names[game->selected],
                                     display);
        mr_screen_text_80_attribute(
            game, 0, 2 + game->selected, display, name_color,
            mr_basic_text_color(game->color_enabled ? 12 : 0),
            blink_visible);
    }
    video_present(&game->host->video);
}

static int mr_begin_read_roster_key(MRGameRuntime *game, int *scan) {
    /* COLOR foreground+16 sets the IBM text blink bit. CGA toggles that bit
     * every 16 vertical retraces; reproduce the adapter behavior while the
     * original INKEY$ loop waits. */
    int blink_visible = 1;
    uint32_t changed = SDL_GetTicks();
    mr_begin_draw_roster(game, blink_visible);
    for (;;) {
        if (input_kbhit(&game->host->input))
            return mr_read_dos_key(&game->host->input, scan);
        if (input_poll_quit(&game->host->input)) return 0x1b;
        uint32_t now = SDL_GetTicks();
        if (now - changed >= 267) {
            blink_visible = !blink_visible;
            changed = now;
            mr_begin_draw_roster(game, blink_visible);
        }
        SDL_Delay(5);
    }
}

static int mr_begin_select_character(MRGameRuntime *game) {
    if (game->roster.count <= 0) {
        /* BEGIN 041B-042A prints this one recovered line and waits; there is
         * no native explanatory second line. */
        mr_screen_clear(&game->screen, MR_CGA_BLACK);
        mr_screen_compose(game);
        mr_screen_text_80(game, 0, 0,
                          "There are no characters on this disk.",
                          mr_begin_current_text_color(game));
        video_present(&game->host->video);
        (void)mr_begin_wait_upper_key(game);
        return -1;
    }
    if (game->selected < 0 || game->selected >= game->roster.count)
        game->selected = 0;
    for (;;) {
        int scan = 0;
        int first = mr_begin_read_roster_key(game, &scan);
        if (input_poll_quit(&game->host->input)) return -2;
        MRRosterAction action = mr_begin_decode_roster_key(first, scan);
        if (action == MR_ROSTER_ACTION_PREVIOUS)
            game->selected = mr_roster_wrap_selection(&game->roster,
                                                       game->selected, -1);
        else if (action == MR_ROSTER_ACTION_NEXT)
            game->selected = mr_roster_wrap_selection(&game->roster,
                                                       game->selected, 1);
        else if (action == MR_ROSTER_ACTION_SELECT) return game->selected;
        else if (action == MR_ROSTER_ACTION_RESTART) return -1;
    }
}

static void mr_begin_draw_hall(MRGameRuntime *game) {
    char path[512], error[512] = {0};
    MRHallOfFame hall;
    mr_game_join(path, sizeof(path), game->directory, "F9.EXE");
    if (!mr_hall_load(&hall, path, error, sizeof(error))) {
        mr_screen_message(game, "CANNOT LOAD HALL OF FAME", error);
        return;
    }
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    /* BEGIN 090F-09AB uses the 80-column grid. It advances BEGIN's retained
     * 0..6 color cycle for the heading and each complete row, then reapplies
     * the final row color to every five-character level prefix. */
    uint8_t color = game->color_enabled
        ? mr_begin_advance_text_color(game)
        : mr_basic_text_color(7);
    mr_screen_text_80(
        game, 0, 0,
        "LEVEL CLASS     NAME                    MONEY    KILLED BY           ON LEVEL",
        color);
    mr_screen_text_80(
        game, 0, 1,
        "----- -----     ----                    -----    ---------           --------",
        color);
    for (int row = 0; row < MR_HALL_ROWS; row++) {
        color = game->color_enabled
            ? mr_begin_advance_text_color(game)
            : mr_basic_text_color(7);
        mr_screen_text_80(game, 0, 2 + row, hall.rows[row], color);
    }
    color = mr_begin_current_text_color(game);
    for (int row = 0; row < MR_HALL_ROWS; row++) {
        char level[6];
        memcpy(level, hall.rows[row], 5);
        level[5] = 0;
        mr_screen_text_80(game, 0, 2 + row, level, color);
    }
    mr_screen_text_80(game, 34, 24, "HIT ANY KEY", color);
    video_present(&game->host->video);
    (void)mr_begin_wait_upper_key(game);
}

static uint8_t mr_f8_advance_color(const MRGameRuntime *game, int *cycle) {
    if (!game || !game->color_enabled) return mr_basic_text_color(7);
    if (!cycle) return mr_basic_text_color(10);
    *cycle += 1;
    if (*cycle > 5) *cycle = 0;
    return mr_basic_text_color(9 + *cycle);
}

static void mr_f8_draw_hall(MRGameRuntime *game,
                            const MRHallOfFame *hall) {
    if (!game || !hall) return;
    /* F8:0498-056F has its own color counter and is not BEGIN's choice-three
     * presenter.  It advances 0..5 before the heading and every row, yielding
    * 10,11,12,13,14,9 in color mode, then advances once more and overlays
     * all ten five-character prefixes in that one color. */
    int cycle = 0;
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    uint8_t color = mr_f8_advance_color(game, &cycle);
    mr_screen_text_80(
        game, 0, 0,
        "LEVEL CLASS     NAME                    MONEY    KILLED BY           ON LEVEL",
        color);
    mr_screen_text_80(
        game, 0, 1,
        "----- -----     ----                    -----    ---------           --------",
        color);
    for (int row = 0; row < MR_HALL_ROWS; row++) {
        color = mr_f8_advance_color(game, &cycle);
        mr_screen_text_80(game, 0, 2 + row, hall->rows[row], color);
    }
    color = mr_f8_advance_color(game, &cycle);
    for (int row = 0; row < MR_HALL_ROWS; row++) {
        char prefix[6];
        memcpy(prefix, hall->rows[row], 5);
        prefix[5] = 0;
        mr_screen_text_80(game, 0, 2 + row, prefix, color);
    }
    mr_screen_text_80(game, 34, 24, "HIT ANY KEY", color);
    video_present(&game->host->video);
    (void)input_wait_any_key(&game->host->input);
}

static void mr_character_text_stream(MRGameRuntime *game, int column, int row,
                                     const char *text, uint8_t color) {
    if (!game || !text) return;
    while (*text && row < MR_RENDER_TEXT_ROWS) {
        char line[MR_RENDER_TEXT_COLUMNS + 1];
        int count = 0;
        while (*text && column + count < MR_RENDER_TEXT_COLUMNS)
            line[count++] = *text++;
        line[count] = 0;
        mr_screen_text(game, column, row, line, color);
        column = 0;
        row++;
    }
}

static void mr_character_normalize_name(char *name) {
    if (!name) return;
    for (int index = 0; name[index]; index++)
        if ((unsigned char)name[index] > 92)
            name[index] = (char)((unsigned char)name[index] - 32);
}

static int mr_name_input(MRGameRuntime *game,
                         char name[MR_ROSTER_NAME_BYTES], uint8_t color) {
    int length = 0;
    name[0] = 0;
    for (;;) {
        static const char clear_line[] =
            "                                        ";
        char display[MR_ROSTER_NAME_BYTES + 24];
        /* CHCHAR has remained in WIDTH 40 since 091E.  At 0ED3 it clears two
         * rows with SPACE$(80), then performs LINE INPUT at BASIC row 20,
         * column 1.  Redraw just that input field so editing does not erase
         * the accepted roll, class, or knife notice above it. */
        for (int row = 19; row <= 21; row++)
            mr_screen_text_attribute(game, 0, row, clear_line,
                                     mr_basic_text_color(0),
                                     mr_basic_text_color(0));
        snprintf(display, sizeof(display), "Name of character: %s", name);
        mr_character_text_stream(game, 0, 19, display, color);
        video_present(&game->host->video);
        int scan = 0;
        int key = mr_read_dos_key(&game->host->input, &scan);
        (void)scan;
        if (input_poll_quit(&game->host->input)) return 0;
        if (key == '\r' && length > 0) {
            /* 0F36-0FA5 subtracts 32 from every byte greater than 92.  This
             * is slightly broader than an ASCII a-z conversion and is kept
             * deliberately. */
            mr_character_normalize_name(name);
            return 1;
        }
        if (key == '\b' && length > 0) name[--length] = 0;
        else if (key >= 32 && key <= 255 &&
                 length < MR_ROSTER_NAME_BYTES - 1) {
            name[length++] = (char)key;
            name[length] = 0;
        }
    }
}

static int mr_character_creation_wait(MRGameRuntime *game) {
    input_wait_any_key(&game->host->input);
    return !input_poll_quit(&game->host->input);
}

static uint8_t mr_character_advance_text_color(const MRGameRuntime *game,
                                               int *cycle) {
    if (!game || !game->color_enabled) return mr_basic_text_color(15);
    (*cycle)++;
    if (*cycle > 6) *cycle = 0;
    return mr_basic_text_color(9 + *cycle);
}

static int mr_character_creation_explanation(MRGameRuntime *game) {
    /* CHCHAR 0736-0877.  PRINT wraps these source literals naturally on the
     * 80-column SCREEN 2 grid; spell the recovered rows out explicitly so
     * host font metrics cannot change those boundaries. */
    static const char *const first_page[] = {
        "     These are the characteristics  that the character  you play will have.  The",
        "higher the characteristic,  the more benefit you will  receive.",
        "",
        "Strength:       Increases chance of hitting and damage done to monsters.",
        "",
        "Intelligence:   Determines number of spell points player receives.",
        "",
        "Wisdom:         Also effects spell points.",
        "",
        "Health:         Determines health points.",
        "",
        "Agility:        Can effect the likelihood of dodging attacks,  and may allow you",
        "                   to take multiple strikes  at monsters.  Also  effects ability",
        "                   to run away from monsters.",
        "",
        "Laziness:       Wastes some of the limited  points that  could be  part of other",
        "                   important  characteristics and serves no purpose  whatsoever.",
        "                   The smaller the laziness the better.",
        "",
        "Health Points:  The amount of damage that you can take before dying. This number",
        "                   goes up (usually) each time you gain an experience level."
    };
    static const char *const second_page[] = {
        "     You may choose  whether to keep a character  or roll a new one.  You should",
        "roll many  before you keep one.  If this is  your first  time playing,  then you",
        "probably should hold out for a character with a high strength  (22 or more), and",
        "lots of health points (23 or more).",
        "",
        "     You will be given a choice between playing a fighter or a wizard.  At first",
        "you should play a fighter.  Fighters are much more  powerful than wizards at the",
        "beginning of the game.  Later,  you will  probably  want to try  playing wizards",
        "because they have much greater potential than fighters."
    };
    int cycle = 0;
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    static const int first_section_ends[] = {2, 4, 6, 8, 10, 14, 18, 20};
    int row = 0;
    for (int section = 0; section < 8; section++) {
        uint8_t color = mr_character_advance_text_color(game, &cycle);
        while (row <= first_section_ends[section]) {
            mr_screen_text_80(game, 0, row, first_page[row], color);
            row++;
        }
    }
    mr_screen_text_80(game, 27, 24, "MORE   (HIT ANY KEY)",
                      mr_basic_text_color(15));
    video_present(&game->host->video);
    if (!mr_character_creation_wait(game)) return 0;

    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    row = 0;
    static const int second_section_ends[] = {4, 8};
    for (int section = 0; section < 2; section++) {
        uint8_t color = mr_character_advance_text_color(game, &cycle);
        while (row <= second_section_ends[section]) {
            mr_screen_text_80(game, 0, row, second_page[row], color);
            row++;
        }
    }
    mr_screen_text_80(game, 0, 24, "Hit any key when ready...",
                      mr_basic_text_color(15));
    video_present(&game->host->video);
    return mr_character_creation_wait(game);
}

static int mr_character_creation_select_race(MRGameRuntime *game) {
    static const char *const names[] = {"Human", "Dwarf", "Elf", "Hobbit"};
    static const int columns[] = {6, 13, 20, 25};
    int race = MR_RACE_HUMAN;
    uint8_t normal = mr_basic_text_color(game->color_enabled ? 9 : 7);
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    mr_screen_text(game, 0, 24, "HIT RETURN TO MAKE SELECTION",
                   mr_basic_text_color(15));
    mr_screen_text(game, 0, 0, "RACE:", normal);
    for (;;) {
        for (int index = 0; index < 4; index++)
            mr_screen_text_attribute(
                game, columns[index], 0, names[index],
                race == index + 1 ? mr_basic_text_color(0) : normal,
                race == index + 1 ? normal : mr_basic_text_color(0));
        video_present(&game->host->video);
        int scan = 0;
        int key = mr_read_dos_key(&game->host->input, &scan);
        if (input_poll_quit(&game->host->input)) return 0;
        /* CHCHAR 0A18-0A8C recognizes only extended Right (M) and Left (K),
         * wraps both ends, and accepts Return. */
        if (key == 0 && scan == MR_ARROW_RIGHT)
            race = race == MR_RACE_HOBBIT ? MR_RACE_HUMAN : race + 1;
        else if (key == 0 && scan == MR_ARROW_LEFT)
            race = race == MR_RACE_HUMAN ? MR_RACE_HOBBIT : race - 1;
        else if (key == '\r')
            return race;
    }
}

static void mr_character_creation_draw_roll(MRGameRuntime *game,
                                             const MRCharacterRoll *roll) {
    static const char *const formats[MR_SAVE_ATTRIBUTES] = {
        "Strength:    %3d ", "Intelligence:%3d ", "Wisdom:      %3d ",
        "Health:      %3d ", "Agility:     %3d ", "Laziness:    %3d "
    };
    static const char clear_line[] =
        "                                        ";
    char line[48];
    uint8_t color = mr_basic_text_color(roll->display_color);
    /* 0A8F clears only the old bottom prompt and returns to BASIC row 3.
     * Leave the selected RACE line intact, but clear the fixed roll region so
     * a reroll cannot leave stale digits or class text. */
    for (int row = 2; row <= 14; row++)
        mr_screen_text_attribute(game, 0, row, clear_line,
                                 mr_basic_text_color(0),
                                 mr_basic_text_color(0));
    for (int index = 0; index < MR_SAVE_ATTRIBUTES; index++) {
        snprintf(line, sizeof(line), formats[index], roll->attributes[index]);
        mr_screen_text(game, 0, 2 + index, line, color);
    }
    snprintf(line, sizeof(line), "TOTAL:       %3d", roll->total_attributes);
    mr_screen_text(game, 0, 9, line, color);
    snprintf(line, sizeof(line), "Health points:%d  ", roll->health_max);
    mr_screen_text(game, 0, 10, line, color);
    mr_screen_text(game, 0, 12, "Do you want it (Y, N, OR ESC)?", color);
    video_present(&game->host->video);
}

static void mr_character_creation_draw_class_prompt(MRGameRuntime *game,
                                                     uint8_t color) {
    mr_screen_text(game, 0, 13, "Choose a character class:", color);
    mr_screen_text(game, 0, 14, "1=Fighter 2=Wizard", color);
    video_present(&game->host->video);
}

static float mr_qbasic_timer_seconds(void) {
#if defined(_WIN32)
    SYSTEMTIME local_time;
    GetLocalTime(&local_time);
    return local_time.wHour * 3600.0f + local_time.wMinute * 60.0f +
           local_time.wSecond + local_time.wMilliseconds / 1000.0f;
#else
    struct timespec now;
    if (timespec_get(&now, TIME_UTC) != TIME_UTC) return 0.0f;
    time_t seconds = now.tv_sec;
    struct tm local_time;
    if (!localtime_r(&seconds, &local_time)) return 0.0f;
    return local_time.tm_hour * 3600.0f + local_time.tm_min * 60.0f +
           local_time.tm_sec + (float)now.tv_nsec / 1000000000.0f;
#endif
}

static void mr_character_roster_capacity_message(MRGameRuntime *game,
                                                  int last_character,
                                                  uint8_t color) {
    /* The startup count-ten branch is still WIDTH 80.  The count-nine branch
     * at 0FCE occurs after LINE INPUT in WIDTH 40 and PRINTs from the following
     * row without clearing the accepted roll.  Both fall through 1676's
     * LOCATE 25,15 / HIT ANY KEY wait. */
    if (last_character) {
        mr_character_text_stream(
            game, 0, 20,
            "This is the last character that will fit   on this disk.",
            color);
        mr_screen_text(game, 14, 24, "HIT ANY KEY", color);
    } else {
        mr_screen_clear(&game->screen, MR_CGA_BLACK);
        mr_screen_compose(game);
        mr_screen_text_80(game, 0, 0,
                          "There is no more room on this disk.", color);
        mr_screen_text_80(game, 14, 24, "HIT ANY KEY", color);
    }
    video_present(&game->host->video);
    input_wait_any_key(&game->host->input);
}

static int mr_begin_create_character(MRGameRuntime *game) {
    /* character_roster_find_name (1560-15CA) returns the number of existing
     * F5.COM names.  The original refuses creation at exactly ten before it
     * displays the explanation or consumes a roll. */
    if (game->roster.count >= MR_ROSTER_CAPACITY) {
        mr_character_roster_capacity_message(
            game, 0, mr_basic_text_color(7));
        return 0;
    }
    MRRng rng;
    mr_rng_seed_default(&rng);
    /* CHCHAR 0564-056A is RANDOMIZE TIMER through BRUN30 $TMR/$RZ1, not a
     * seed from process uptime. */
    mr_rng_seed_timer(&rng, mr_qbasic_timer_seconds());
    if (!mr_character_creation_explanation(game)) return 0;
    int race = mr_character_creation_select_race(game);
    if (!race) return 0;
    int player_class = 0;

    MRCharacterRoll roll;
    for (;;) {
        if (!mr_character_roll(&rng, race, game->color_enabled, &roll))
            return 0;
        mr_character_creation_draw_roll(game, &roll);
        for (;;) {
            int key = input_wait_any_key(&game->host->input);
            if (input_poll_quit(&game->host->input)) return 0;
            if (key == 0x1B) return 0;
            if (key == 'n' || key == 'N') break;
            if (key == 'y' || key == 'Y') {
                player_class = -1;
                break;
            }
            /* 0E61 loops at the same prompt. Invalid input does not consume
             * a fresh random character. */
        }
        if (player_class == -1) break;
    }

    uint8_t creation_color = mr_basic_text_color(roll.display_color);
    mr_character_creation_draw_class_prompt(game, creation_color);
    for (;;) {
        int key = input_wait_any_key(&game->host->input);
        if (input_poll_quit(&game->host->input)) return 0;
        if (key == '1' || key == '2') { player_class = key - '0'; break; }
    }
    mr_screen_text(game, 0, 15, "Your weapon is a knife.", creation_color);
    video_present(&game->host->video);

    char name[MR_ROSTER_NAME_BYTES];
    char error[512] = {0};
    int index = -1;
    int appended = 0;
    for (;;) {
        if (!mr_name_input(game, name, creation_color)) return 0;
        /* 0FBD-0FDC warns after name entry when the new character would fill
         * slot ten.  It does so even if this name is about to replace an
         * existing character, so preserve that seemingly odd ordering. */
        if (game->roster.count == MR_ROSTER_CAPACITY - 1)
            mr_character_roster_capacity_message(game, 1, creation_color);
        index = mr_roster_find(&game->roster, name);
        if (index < 0) {
            if (!mr_roster_append(&game->roster, name, error,
                                  sizeof(error))) {
                mr_screen_message(game, "CHARACTER WAS NOT CREATED", error);
                return 0;
            }
            index = game->roster.count - 1;
            appended = 1;
            break;
        }
        static const char clear_line[] =
            "                                        ";
        for (int row = 20; row <= 22; row++)
            mr_screen_text_attribute(game, 0, row, clear_line,
                                     mr_basic_text_color(0),
                                     mr_basic_text_color(0));
        mr_character_text_stream(
            game, 0, 20,
            "This name is already in use. Do you want   to replace it (Y or N)?",
            creation_color);
        video_present(&game->host->video);
        for (;;) {
            int key = input_wait_any_key(&game->host->input);
            if (input_poll_quit(&game->host->input)) return 0;
            if (key == 'y' || key == 'Y') break;
            if (key == 'n' || key == 'N') {
                index = -1;
                break;
            }
        }
        if (index >= 0) break;
    }

    MRSave save;
    memset(&save, 0, sizeof(save));
    save.bsave_offset = 0x9B06;
    if (!mr_character_accept_roll(&roll, player_class, &save.character))
        return 0;
    char text_path[512], binary_path[512], roster_path[512];
    mr_begin_character_paths(game->directory, index, text_path,
                             sizeof(text_path), binary_path,
                             sizeof(binary_path));
    mr_game_join(roster_path, sizeof(roster_path), game->directory, "F5.COM");
    if (!mr_save_write(&save, text_path, binary_path, error, sizeof(error)) ||
        !mr_roster_write(&game->roster, roster_path, error, sizeof(error))) {
        if (appended) game->roster.count--;
        mr_screen_message(game, "CHARACTER WAS NOT CREATED", error);
        return 0;
    }
    game->selected = index;
    /* CHCHAR 155A chains straight back to BEGIN after the paired save and
     * F5.COM write; there is no extra "character created" acknowledgement. */
    return 1;
}

static void mr_draw_bitmap_at_clipped(MRGameRuntime *game,
                                      const MRBitmapView *bitmap,
                                      int left, int top,
                                      int clip_left, int clip_top,
                                      int clip_right, int clip_bottom) {
    if (!game || !bitmap) return;
    for (int y = 0; y < bitmap->height; y++) {
        int screen_y = top + y;
        if (screen_y < clip_top || screen_y > clip_bottom) continue;
        for (int x = 0; x < bitmap->width; x++) {
            int screen_x = left + x;
            if (screen_x < clip_left || screen_x > clip_right) continue;
            uint8_t source = mr_bitmap_pixel(bitmap, x, y);
            if (source) {
                size_t offset = (size_t)screen_y * MR_RENDER_WIDTH +
                                (size_t)screen_x;
                /* Unqualified QuickBASIC PUT uses XOR. A zero source pixel
                 * therefore leaves the wall raster untouched. */
                game->screen.pixels[offset] =
                    (game->screen.pixels[offset] ^ source) & 3U;
            }
        }
    }
}

static void mr_draw_projected_view(MRGameRuntime *game, MRSession *session,
                                   int pass) {
    MRDirection direction = (MRDirection)mr_render_direction_for_pass(
        session->input.facing, pass);
    MRRenderView view;
    if (!mr_render_trace_view(&session->save.character, session->occupancy,
                              direction, &view))
        return;
    int ox = game->layout.viewport_origins[pass].x;
    int oy = game->layout.viewport_origins[pass].y;
    mr_render_draw_first_person(
        &game->layout, &view,
        (int)floor(session->save.character.dungeon_level),
        game->screen.pixels, MR_RENDER_WIDTH, ox, oy,
        game->color_enabled);

    /* DUNSMALL 69BC-6B5A layers the projected monster PUTs after the complete
     * LINE/PAINT wall pass. 6CC5 does not acquire an AI target from draw
     * order. Its B66A depth scratch is reset to zero at 59F1 and has no
     * reachable nonzero assignment, so every projected monster other than
     * B502 clears B506. Retain that original quirk without inventing a new
     * primary target here. */
    for (int depth = 0; depth < view.depth_count; depth++) {
        MRRenderDepth *row = &view.depth[depth];
        if (row->monster_global_index > 0) {
            if (row->monster_global_index !=
                    session->primary_tracking_global_index)
                session->secondary_tracking_global_index = 0;
            if (game->assets_loaded) {
                int level = (int)session->save.character.dungeon_level;
                int health = session->world.monster_health[
                    row->monster_global_index];
                int type = mr_monster_type(row->monster_global_index, level,
                                           health);
                const MRMonsterAssetSet *set =
                    level >= MR_DEEP_MONSTER_RESOURCE_FLOOR
                    ? &game->assets.deep : &game->assets.shallow;
                MRBitmapView bitmap;
                int use_large, variant, sprite_x, sprite_y;
                int basic_depth = depth + 1;
                if (mr_render_projected_sprite_placement(
                        basic_depth, &use_large, &variant,
                        &sprite_x, &sprite_y) &&
                    (use_large
                         ? mr_assets_large_bitmap(set, type, variant, &bitmap)
                         : mr_assets_compact_bitmap(set, type, variant,
                                                    &bitmap)))
                    mr_draw_bitmap_at_clipped(
                        game, &bitmap, ox + sprite_x, oy + sprite_y,
                        ox, oy, ox + 52, oy + 53);
            }
        }
    }
}

static void mr_session_redraw_viewports(MRGameRuntime *game,
                                        MRSession *session) {
    if (!game || !session ||
        (session->phase != MR_SESSION_EXPLORING &&
         session->phase != MR_SESSION_COMBAT))
        return;
    for (int pass = 0; pass < 4; pass++)
        mr_draw_projected_view(game, session, pass);
    /* The original replaces the affected GET/PUT cache as part of its
     * movement redraw.  Refresh the native cache from the same completed
     * pane pixels so a later command-cycle restore cannot resurrect the old
     * monster sprite. */
    mr_render_viewport_cache_capture(
        &game->viewport_cache, &game->layout, game->screen.pixels,
        MR_RENDER_WIDTH,
        (int)floor(session->save.character.dungeon_level),
        (int)floor(session->save.character.player_x),
        (int)floor(session->save.character.player_y),
        session->input.facing);

    /* 7749-78D4 updates only one cached small viewport at a time. The native
     * bridge redraws all four for equivalent final pane pixels, but it must
     * still publish those changed SCREEN 1 rectangles. A full compose would
     * erase BIOS text that is deliberately outside the graphics backing
     * store. The combat-sized player-cell monster occupies the central gap,
     * not these four pane rectangles; XORing it again here erased it. */
    for (int pass = 0; pass < 4; pass++) {
        int origin_x = game->layout.viewport_origins[pass].x;
        int origin_y = game->layout.viewport_origins[pass].y;
        int height = pass == 1 || pass == 3 ? 55 : 54;
        mr_screen_compose_source_rect(
            game, origin_x, origin_y,
            origin_x + MR_RENDER_VIEWPORT_CACHE_WIDTH - 1,
            origin_y + height - 1);
    }
    video_present(&game->host->video);
}

static const char *mr_monster_data_name(const MRGameRuntime *game,
                                        int resource_set, int type) {
    if (!game || !game->data_loaded || type < 1 || type > 22)
        return "MONSTER";
    int first = resource_set == 2 ? 22 : 0;
    int index = first + type - 1;
    if (index < 0 || index >= MR_MONSTER_ROWS ||
        !game->data.monster_names[index][0])
        return "MONSTER";
    return game->data.monster_names[index];
}

static int mr_original_combat_contact_dispatches_player_command(
    float contact_scratch) {
    /* 8783 loads literal one as the left operand and DS:B51C as the right.
     * JNE reaches the player-command dispatcher; equality alone reaches the
     * monster turn. The enclosing 803A cycle writes zero to B51C at 8707. */
    return contact_scratch != 1.0f;
}

static float mr_cached_viewport_rotation(const MRViewportCache *cache,
                                         MRDirection facing) {
    if (!cache || !cache->valid) return 0.0f;
    int rotation = (int)cache->facing - (int)facing;
    while (rotation < 0) rotation += 4;
    while (rotation >= 4) rotation -= 4;
    return (float)rotation;
}

static int mr_session_used_false_floor_notice(const MRSession *session,
                                              int level, int x, int y,
                                              int feature) {
    /* 064D-06CC converts the otherwise unmarked landing cell of the most
     * recently used false floor to display value one while the saved landing
     * coordinates match.  The two ORed comparisons at 0698-06BB are
     * (level == last_level) and (level - 1 == last_level); the latter uses
     * BRUN30:B374's MBF addition with -1.  Floor 70 is explicitly excluded. */
    return session && feature > 3 && level > 0 &&
           level < MR_DUNGEON_LEVELS &&
           x == session->last_chute_landing_x &&
           y == session->last_chute_landing_y &&
           (level == session->last_chute_landing_level ||
            level - 1 == session->last_chute_landing_level);
}

static void mr_session_draw_vertical_notice(MRGameRuntime *game,
                                            const MRSession *session,
                                            int level, int x, int y) {
    if (!game || !session || level < 0 || level > MR_DUNGEON_LEVELS ||
        x < 1 || x > 20 || y < 1 || y > 19)
        return;
    int feature = mr_world_resolve_vertical_feature(
        &session->world, level, x, y);
    if (mr_session_used_false_floor_notice(session, level, x, y, feature)) {
        /* show_false_floor_notice 567C-56CB. */
        mr_screen_text(game, 21, 24, "   False floor.   ", MR_CGA_WHITE);
        mr_screen_text(game, 28, 14, "D-GO", MR_CGA_WHITE);
        mr_screen_text(game, 28, 15, "DOWN", MR_CGA_WHITE);
        return;
    }

    /* show_ladder_notice 56CC-5792 returns when a monster occupies the
     * player's cell. Values below four include the original inert level-zero
     * circles, which are described as going down even though D rejects them. */
    if (feature >= 4 || mr_session_monster_at(session, x, y) > 0) return;
    mr_screen_text(game, 21, 24, " Ladder going ", MR_CGA_WHITE);
    if (feature < 0) {
        mr_screen_text(game, 35, 24, "up.  ", MR_CGA_WHITE);
        mr_screen_text(game, 28, 14, "U-GO", MR_CGA_WHITE);
        mr_screen_text(game, 28, 15, " UP ", MR_CGA_WHITE);
    } else {
        mr_screen_text(game, 35, 24, "down.", MR_CGA_WHITE);
        mr_screen_text(game, 28, 14, "D-GO", MR_CGA_WHITE);
        mr_screen_text(game, 28, 15, "DOWN", MR_CGA_WHITE);
    }
}

static void mr_session_draw_view_direction_labels(MRGameRuntime *game) {
    /* draw_view_direction_labels 4BC6-4C27. */
    mr_screen_text(game, 27, 6, "FRONT", MR_CGA_WHITE);
    mr_screen_text(game, 21, 19, "LEFT", MR_CGA_WHITE);
    mr_screen_text(game, 35, 19, "RIGHT", MR_CGA_WHITE);
    mr_screen_text(game, 28, 24, "BACK", MR_CGA_WHITE);
}

static void mr_session_draw(MRGameRuntime *game, MRSession *session,
                            const char *message) {
    const MRCharacter *player = &session->save.character;
    mr_game_apply_screen_one_palette(game, session);
    mr_screen_clear(&game->screen, MR_CGA_BLACK);

    /* Exact static translation of DUNSMALL 4CC3-53ED, including procedural
     * wall segments, doorway gaps, vertical features and the cropped BIOS
     * facing arrow. */
    int level = (int)floor(player->dungeon_level);
    if (level >= 0 && level < MR_EXPLORED_LEVELS)
        mr_render_draw_dungeon_map(&session->save, &session->world,
                                   game->screen.pixels, MR_RENDER_WIDTH,
                                   game->color_enabled, 1,
                                   session->input.facing);

    int player_x = (int)floor(player->player_x);
    int player_y = (int)floor(player->player_y);
    int restored_cached_panes = 0;
    if (session->phase != MR_SESSION_COMBAT) {
        restored_cached_panes = mr_render_viewport_cache_restore(
            &game->viewport_cache, &game->layout, game->screen.pixels,
            MR_RENDER_WIDTH, level, player_x, player_y,
            session->input.facing);
        if (restored_cached_panes) {
            /* DUNSMALL 580F-5834 computes the cached-pane rotation through
             * DS:52FC before any of the four PUTs. 52FC is also reused by
             * combat and the coin-loot weight check, so restoring only the
             * pixels left observably stale state in the native port. */
            session->shared_menu_value = mr_cached_viewport_rotation(
                &game->viewport_cache, session->input.facing);
        }
    }
    if (!restored_cached_panes) {
        for (int pass = 0; pass < 4; pass++)
            mr_draw_projected_view(game, session, pass);
        /* DUNSMALL 6DE7-6EC9 GETs these exact four rectangles only after a
         * fresh ray render. The combat-sized player-cell monster is layered
         * later and is deliberately absent from the cached backgrounds. */
        mr_render_viewport_cache_capture(
            &game->viewport_cache, &game->layout, game->screen.pixels,
            MR_RENDER_WIDTH, level, player_x, player_y,
            session->input.facing);
    }

    if (session->phase == MR_SESSION_COMBAT && game->assets_loaded) {
        const MRMonsterAssetSet *set = level >= MR_DEEP_MONSTER_RESOURCE_FLOOR
            ? &game->assets.deep : &game->assets.shallow;
        MRBitmapView bitmap;
        if (mr_assets_large_bitmap(set, session->combat_profile.type,
                                   MR_COMBAT_MONSTER_SIZE_VARIANT,
                                   &bitmap))
            mr_draw_bitmap_at_clipped(game, &bitmap, 225, 112,
                                      0, 0,
                                      MR_RENDER_WIDTH - 1,
                                      MR_RENDER_HEIGHT - 1);
    }

    mr_screen_compose(game);
    char line[80];

    /* DUNSMALL 4CDF-4D43 and 4BC6-4C24.  These are the complete fixed
     * dungeon labels in SCREEN 1's 40x25 text grid.  The original does not
     * print a level/coordinate/HP/SP HUD or an added command legend here. */
    mr_screen_text(game, 34, 4, "SPELLS", MR_CGA_WHITE);
    mr_screen_text(game, 34, 5, "CAST", MR_CGA_WHITE);
    mr_session_draw_view_direction_labels(game);
    if (level == 0)
        mr_screen_text(game, 0, 4, "YOU'RE IN TOWN", MR_CGA_WHITE);
    if (player->player_level < 2.0f)
        mr_screen_text(game, 27, 17, "H=HELP", MR_CGA_WHITE);
    mr_session_draw_vertical_notice(game, session, level, player_x, player_y);

    if (session->phase == MR_SESSION_COMBAT) {
        /* show_attacking_monster 7FC8-7FF7 and combat_loop 851D-85B1.
         * BASIC's positive-number PRINT format contributes the spaces around
         * the level.  Combat results reuse the first queued-message row and
         * therefore replace, rather than accompany, the attack announcement. */
        if (message && message[0]) {
            mr_screen_text(game, 0, 0, message, MR_CGA_WHITE);
            if (strcmp(message, "YOU DO NOT HAVE THAT") == 0)
                mr_screen_text(game, 0, 1, "   WEAPON!!   ",
                               MR_CGA_WHITE);
        } else {
            int type = session->combat_profile.type;
            const char *monster_name = mr_monster_data_name(
                game, session->combat_profile.resource_set, type);
            snprintf(line, sizeof(line), "A LEVEL %d %s IS ATTACKING!",
                     session->combat_profile.level, monster_name);
            mr_screen_text(game, 0, 0, line, MR_CGA_WHITE);
        }

        mr_screen_text(game, 0, 2, "YOUR HEALTH POINTS:", MR_CGA_WHITE);
        /* TAB(21) positions at zero-based column 20; QuickBASIC's numeric
         * PRINT then emits a sign blank before positive values and one
         * trailing blank. */
        snprintf(line, sizeof(line), " %d ",
                 (int)player->health_current);
        mr_screen_text(game, 20, 2, line, MR_CGA_WHITE);
        mr_screen_text(game, 0, 3, "ITS HEALTH POINTS:", MR_CGA_WHITE);
        snprintf(line, sizeof(line), " %d ",
                 (int)session->combat_health);
        mr_screen_text(game, 20, 3, line, MR_CGA_WHITE);

        mr_screen_text(game, 0, 23, "EXP. VALUE:", MR_CGA_WHITE);
        snprintf(line, sizeof(line), " %d ",
                 (int)session->combat_profile.experience_reward);
        mr_screen_text(game, 2, 24, line, MR_CGA_WHITE);

        /* show_agility_effect/show_shielding_effect/show_breathe_fire_command
         * at 7F7D-7FC7.  The native session uses the same TIMER-second basis
         * as its potion dispatcher, so these comparisons retain that state. */
        float timer_seconds = mr_qbasic_timer_seconds();
        if (player->persistent_values[MR_PERSISTENT_AGILITY_EXPIRY] >
            timer_seconds)
            mr_screen_text(game, 19, 4, "YOU FEEL VERY AGILE. ",
                           MR_CGA_WHITE);
        if (player->persistent_values[MR_PERSISTENT_SHIELDING_EXPIRY] >
            timer_seconds)
            mr_screen_text(game, 19, 5, "YOUR BODY GLOWS.     ",
                           MR_CGA_WHITE);
        if (player->persistent_values[MR_PERSISTENT_BREATHE_FIRE_EXPIRY] >
            timer_seconds)
            mr_screen_text(game, 19, 6, "B-BREATH FIRE ",
                           MR_CGA_WHITE);
    } else {
        /* The command cycle at 06FE-0724 clears and repositions to BASIC
         * LOCATE 2,1 before printing one of the optional advice strings. */
        if (message && message[0])
            mr_screen_text(game, 0, 1, message, MR_CGA_WHITE);
    }
    video_present(&game->host->video);
}

static void mr_session_draw_combat_result(MRGameRuntime *game,
                                          MRSession *session,
                                          const char *message) {
    /* The state layer eagerly enters A4CA's defeat phase so rewards can be
     * deterministic. The original, however, leaves the combat picture up
     * through the 8E12/95A6 result hold. Temporarily expose that already-drawn
     * combat state without changing the persisted transition. */
    MRSessionPhase phase = session->phase;
    if (phase == MR_SESSION_DEFEAT_NOTICE)
        session->phase = MR_SESSION_COMBAT;
    mr_session_draw(game, session, message);
    session->phase = phase;
}

static int mr_player_owns_combat_weapon(const MRCharacter *player,
                                        MRPlayerAttackCode attack) {
    if (!player) return 0;
    if (attack == MR_PLAYER_ATTACK_FIST) return 1;
    if (attack == MR_PLAYER_ATTACK_KNIFE)
        return (int)player->persistent_values[
            MR_PERSISTENT_KNIFE_OWNED] == 1;
    if (attack == MR_PLAYER_ATTACK_SWORD)
        return (int)player->persistent_values[
            MR_PERSISTENT_SWORD_OWNED] == 1;
    if (attack == MR_PLAYER_ATTACK_MACE)
        return (int)player->persistent_values[
            MR_PERSISTENT_MACE_OWNED] == 1;
    return attack == MR_PLAYER_ATTACK_BREATHE_FIRE;
}

static void mr_present_missing_combat_weapon(MRGameRuntime *game,
                                             MRSession *session) {
    /* DUNSMALL 89B4-89D8 prints these two exact records and returns to the
     * combat dispatcher without consuming a turn. */
    mr_session_draw_combat_result(game, session, "YOU DO NOT HAVE THAT");
    mr_screen_text(game, 0, 1, "   WEAPON!!   ", MR_CGA_WHITE);
    video_present(&game->host->video);
}

static void mr_present_chute_notice(MRGameRuntime *game) {
    /* fall_down_chute 346D-3488 uses COLOR 1, LOCATE 1,1 and prints the
     * 22-byte notice followed by SPACE$(18), exactly filling SCREEN 1's
     * forty-column first row.  There is no TIMER or key wait: B308 saves
     * immediately afterward, before the dungeon level is incremented.  The
     * host framebuffer still contains the pre-move dungeon here, preserving
     * that original ordering without emulating DOS disk latency. */
    mr_screen_text_attribute(
        game, 0, 0, "YOU FELL DOWN A CHUTE!                  ",
        mr_basic_text_color(1), mr_basic_text_color(0));
    video_present(&game->host->video);
}

static int mr_monster_strike_has_extended_notice(
    const MRMonsterStrikeResult *strike) {
    /* B730 is set by the SQUASH, health-drain, strength-drain and
     * disease/agility-drain branches at 9DBB, 9EE3, 9F14 and 9F40. */
    return strike && (strike->squash_message || strike->health_drained ||
                      strike->strength_drained || strike->agility_drained ||
                      strike->disease_applied);
}

static void mr_present_monster_turn(MRGameRuntime *game, MRSession *session,
                                    const MRMonsterTurnResult *turn) {
    if (!turn || turn->strike_count <= 0) return;
    for (int index = 0; index < turn->strike_count; ++index) {
        const MRMonsterStrikeResult *strike = &turn->strikes[index];
        char message[80];
        /* 9A39-A00E writes into fixed rows of the existing combat frame; it
         * never repurposes the row-one attacking-monster announcement as a
         * generic message area. Rebuilding the unchanged background first
         * is the native equivalent of the original selective overwrites. */
        mr_session_draw_combat_result(game, session, NULL);
        if (strike->could_not_strike) {
            mr_screen_text(game, 0, 6, "IT CAN'T STRIKE        ",
                           MR_CGA_WHITE);
        } else {
            if (strike->stuck_message)
                mr_screen_text(game, 0, 4, "IT'S STUCK TO YOU!",
                               MR_CGA_WHITE);
            if (strike->missed) {
                mr_screen_text(game, 0, 6, "IT MISSED               ",
                               MR_CGA_WHITE);
            } else {
                int damage_row = 6;
                if (strike->squash_message) {
                    mr_screen_text(game, 0, 6, "SQUASH!!",
                                   MR_CGA_WHITE);
                    damage_row = 7;
                }
                snprintf(message, sizeof(message), "IT DID %d POINTS  ",
                         strike->damage);
                mr_screen_text(game, 0, damage_row, message, MR_CGA_WHITE);
            }
        }

        int row = 19;
        if (strike->level_drained) {
            if (game->data_loaded && strike->drain_phrase_index >= 0 &&
                strike->drain_phrase_index < MR_COMBAT_MESSAGES)
                mr_screen_text(game, 0, row++, game->data.combat_messages[
                                   strike->drain_phrase_index],
                               MR_CGA_MAGENTA);
            mr_screen_text(game, 0, row++, "LEVEL DRAINED!", MR_CGA_MAGENTA);
        }
        if (strike->health_drained)
            mr_screen_text(game, 0, row++, "YOU FEEL UNHEALTHY!",
                           MR_CGA_MAGENTA);
        if (strike->strength_drained)
            mr_screen_text(game, 0, row++, "STRENGTH DRAINED!",
                           MR_CGA_MAGENTA);
        if (strike->disease_applied)
            mr_screen_text(game, 0, row++, "YOU FEEL SICK!",
                           MR_CGA_MAGENTA);
        if (strike->agility_drained)
            mr_screen_text(game, 0, row++, "AGILITY IS DRAINED!",
                           MR_CGA_MAGENTA);
        video_present(&game->host->video);

        if (mr_monster_strike_has_extended_notice(strike)) {
            /* 9F8E waits two seconds, then performs the fixed debounce
             * before an optional agility-based repeat strike. */
            mr_screen_wait_milliseconds(game, 2000);
            mr_original_keyboard_drain(game);
        }
    }
    /* 9FEC-A00E holds the final monster result for one TIMER second. */
    mr_screen_wait_milliseconds(game, 1000);
}

static int mr_session_flush_pending_saves(MRGameRuntime *game,
                                          int roster_index,
                                          MRSession *session,
                                          char *error,
                                          size_t error_size) {
    char text_path[512], binary_path[512];
    mr_begin_character_paths(game->directory, roster_index, text_path,
                             sizeof(text_path), binary_path,
                             sizeof(binary_path));
    MRSave snapshot;
    while (mr_session_take_save_snapshot(session, &snapshot)) {
        /* save_character B308 begins with 2FCB for every original save
         * boundary (quit, chute, fountain, pause, and level drain). */
        mr_original_keyboard_drain(game);
        if (!mr_save_write(&snapshot, text_path, binary_path, error,
                           error_size)) return 0;
    }
    return 1;
}

static int mr_session_flush_or_report(MRGameRuntime *game,
                                      int roster_index,
                                      MRSession *session) {
    char error[512] = {0};
    if (mr_session_flush_pending_saves(game, roster_index, session,
                                       error, sizeof(error)))
        return 1;
    mr_screen_message(game, "CANNOT SAVE CHARACTER", error);
    return 0;
}

static int mr_session_persist(MRGameRuntime *game, int roster_index,
                              MRSession *session) {
    char text_path[512], binary_path[512], error[512] = {0};
    mr_begin_character_paths(game->directory, roster_index, text_path,
                             sizeof(text_path), binary_path,
                             sizeof(binary_path));
    if (!mr_session_flush_pending_saves(game, roster_index, session,
                                        error, sizeof(error)))
        return 0;
    /* The normal Q-to-character-menu path reaches B308 at 0D91 even when no
     * earlier snapshot remains pending. */
    mr_original_keyboard_drain(game);
    return mr_save_write(&session->save, text_path, binary_path, error,
                         sizeof(error)) &&
           mr_world_write_monsters(&session->world, game->directory, error,
                                   sizeof(error));
}

static const char *mr_game_spell_name(const MRGameRuntime *game,
                                      MRSpellbookMaskType type, int index) {
    if (index < 0 || index >= MR_PREP_SPELL_COUNT) return "";
    if (game->data_loaded) {
        return type == MR_SPELLBOOK_PREPARATION
            ? game->data.spells[index].preparation_name
            : game->data.spells[index].battle_name;
    }
    return type == MR_SPELLBOOK_PREPARATION
        ? mr_preparation_spell_name(index) : mr_battle_spell_name(index);
}

/* Direct UI/control-flow transcription of DUNSMALL 35AC-366F,
 * 90D3-9187 and the shared C5D0-C7A4 selector. The executable first asks
 * for a level, rejects levels beyond six or beyond current spell points,
 * then displays the two learned names represented by bits one and two. */
static int mr_session_select_spell(MRGameRuntime *game, MRSession *session,
                                   MRSpellbookMaskType type) {
    /* DUNSMALL 355D-3590 routes the preparation-spell command through the
     * fixed eighteen-poll keyboard drain before entering 35AC. Battle
     * spells enter through 90D3 and do not have this boundary. */
    if (type == MR_SPELLBOOK_PREPARATION)
        mr_original_keyboard_drain(game);
    const int base_row = type == MR_SPELLBOOK_PREPARATION ? 0 : 9;
    mr_session_draw(game, session, NULL);
    mr_screen_text(game, 0, base_row,
                   type == MR_SPELLBOOK_PREPARATION
                       ? "WHAT LEVEL SPELL (1-6)?"
                       : "WHAT LEVEL (1-6)?",
                   MR_CGA_WHITE);
    mr_screen_text(game, 0, base_row + 1, "ESC-CAST NO SPELL",
                   MR_CGA_WHITE);
    video_present(&game->host->video);

    int key = mr_read_modal_key_with_idle_monsters(game, session);
    if (key == MR_DOS_KEY_IDLE_MONSTER_REACHED_PLAYER || key == 0x1B ||
        input_poll_quit(&game->host->input))
        return -1;
    int level = key >= '0' && key <= '9' ? key - '0' : 0;
    if (type == MR_SPELLBOOK_BATTLE && (level < 1 || level > 6))
        return -1;
    if (type == MR_SPELLBOOK_PREPARATION && level < 1) return -1;
    if ((type == MR_SPELLBOOK_PREPARATION && level > 6) ||
        session->save.character.spell_points < level) {
        /* Preparation 362F prints at the cursor following the two prompt
         * lines. Battle 9143 explicitly LOCATEs row 12, column 1. */
        mr_screen_text(game, 0, base_row + 2,
                       "NOT ENOUGH SPELL POINTS!!", MR_CGA_WHITE);
        video_present(&game->host->video);
        /* Both preparation 363B and battle 915B selectors hold this rejection
         * for the shared two-second TIMER interval. */
        mr_screen_wait_milliseconds(game, 2000);
        return -1;
    }

    int first_index = mr_spell_dispatch_index(level, 1);
    int second_index = mr_spell_dispatch_index(level, 2);
    const char *first = mr_spellbook_knows_choice(
        &session->save.character, type, level, 1)
        ? mr_game_spell_name(game, type, first_index) : "";
    const char *second = mr_spellbook_knows_choice(
        &session->save.character, type, level, 2)
        ? mr_game_spell_name(game, type, second_index) : "";

    for (;;) {
        char line[41];
        mr_session_draw(game, session, NULL);
        snprintf(line, sizeof(line), "LEVEL %d- SELECT ONE:", level);
        mr_screen_text(game, 0, base_row, line, MR_CGA_WHITE);
        snprintf(line, sizeof(line), "1) %s", first);
        mr_screen_text(game, 0, base_row + 1, line, MR_CGA_WHITE);
        snprintf(line, sizeof(line), "2) %s", second);
        mr_screen_text(game, 0, base_row + 2, line, MR_CGA_WHITE);
        mr_screen_text(game, 0, base_row + 3, "3) CAST NO SPELL",
                       MR_CGA_WHITE);
        video_present(&game->host->video);
        key = mr_read_modal_key_with_idle_monsters(game, session);
        if (key == MR_DOS_KEY_IDLE_MONSTER_REACHED_PLAYER)
            return -1;
        /* C707 and C721-C79E leave the final choice in DS:52FC, the same
         * single later reused by monster damage.  Escape and an unavailable
         * learned-spell slot both normalize that shared value to three. */
        if (key == 0x1B || key == '3' ||
            input_poll_quit(&game->host->input)) {
            session->shared_menu_value = 3.0f;
            return -1;
        }
        if (key == '1') {
            session->shared_menu_value = first[0] ? 1.0f : 3.0f;
            return first[0] ? first_index : -1;
        }
        if (key == '2') {
            session->shared_menu_value = second[0] ? 2.0f : 3.0f;
            return second[0] ? second_index : -1;
        }
    }
}

static const char *mr_preparation_cast_message(
    const MRSession *session, int spell,
    const MRPreparationCastResult *result, char *buffer,
    size_t buffer_size) {
    (void)session;
    (void)buffer;
    (void)buffer_size;
    if (result->insufficient_spell_points)
        return "NOT ENOUGH SPELL POINTS!!";
    if (result->duplicate_effect) return "";
    switch ((MRPreparationSpell)spell) {
        case MR_PREP_MOCCIOLO:
            return result->random_outcome == 5 ? "UH OH..." : "";
        default: return "";
    }
}

static void mr_show_player_statistics(MRGameRuntime *game,
                                      MRSession *session,
                                      int roster_index);

static void mr_present_preparation_spell_cast(
    MRGameRuntime *game, MRSession *session, int roster_index,
    MRPreparationSpell spell, const MRPreparationCastResult *result) {
    if (!game || !session || !result || result->insufficient_spell_points ||
        result->duplicate_effect)
        return;

    char line[80];
    switch (spell) {
        case MR_PREP_CURE:
        case MR_PREP_STRENGTH:
        case MR_PREP_SPEED:
            /* 36CD and 3776 tail-jump to show_player_statistics. */
            mr_show_player_statistics(game, session, roster_index);
            break;
        case MR_PREP_SENSE_LEVEL:
            /* 36D0-36F3 prints on the current display and calls the raw
             * INKEY$ acknowledgement helper, with no invented prompt. */
            mr_session_draw(game, session, NULL);
            snprintf(line, sizeof(line), "YOU ARE ON LEVEL %d",
                     (int)session->save.character.dungeon_level);
            mr_screen_text(game, 0, 3, line, MR_CGA_MAGENTA);
            video_present(&game->host->video);
            (void)mr_read_original_nonempty_key(game, session);
            break;
        case MR_PREP_SENSE_LOCATION:
            /* 3779-37C9 uses two exact lines and the four-second TIMER
             * helper. */
            mr_session_draw(game, session, NULL);
            snprintf(line, sizeof(line), "X=%d Y=%d",
                     (int)session->save.character.player_x,
                     (int)session->save.character.player_y);
            mr_screen_text(game, 0, 3, line, MR_CGA_MAGENTA);
            snprintf(line, sizeof(line), "AND YOU ARE ON LEVEL %d",
                     (int)session->save.character.dungeon_level);
            mr_screen_text(game, 0, 4, line, MR_CGA_MAGENTA);
            video_present(&game->host->video);
            mr_screen_wait_milliseconds(game, 4000);
            break;
        case MR_PREP_FEATHER:
            if (!result->feather_fell_through_to_ascend)
                mr_show_player_statistics(game, session, roster_index);
            else if (result->dungeon_transition)
                mr_session_draw(game, session, "POOF");
            break;
        case MR_PREP_ASCEND:
            if (result->dungeon_transition)
                mr_session_draw(game, session, "POOF");
            break;
        case MR_PREP_MOCCIOLO:
            if (result->random_outcome == 1) {
                mr_session_draw(game, session, "WOW!");
                mr_screen_wait_milliseconds(game, 4000);
            } else if (result->random_outcome == 6) {
                mr_session_draw(game, session, "Oh my God!");
                mr_screen_wait_milliseconds(game, 4000);
            }
            break;
        default:
            /* Descend, Change Level, Invisibility, Heal and Mocciolo
             * outcomes two through four redraw/return without result text. */
            break;
    }
}

static const char *mr_battle_cast_message(
    MRBattleSpell spell, const MRSessionBattleSpellResult *result, char *buffer,
    size_t buffer_size) {
    if (result->spell.insufficient_spell_points)
        return "NOT ENOUGH SPELL POINTS!!";
    if (result->spell.damage > 0) {
        snprintf(buffer, buffer_size, "YOU DO %d POINTS",
                 result->spell.damage);
        return buffer;
    }
    if (result->spell.no_effect) return "NO EFFECT";
    if (spell == MR_BATTLE_GO_AWAY && result->spell.rewarded_defeat)
        return "IT'S GONE";
    if (spell == MR_BATTLE_RISE) return "POOF";
    if (spell == MR_BATTLE_GOD && result->spell.random_outcome == 5)
        return "UH OH...";
    return "";
}

static int mr_battle_cast_has_two_second_hold(
    MRBattleSpell spell, const MRBattleCastResult *result) {
    /* 9529, 9555 and 9569 converge on the 2F1A TIMER helper. Successful
     * GO AWAY/AUTO KILL and the RISE/God town-warp branches bypass it. */
    if (!result || result->insufficient_spell_points) return 0;
    if (result->damage > 0 || result->no_effect) return 1;
    if (spell == MR_BATTLE_SPEED || spell == MR_BATTLE_STRENGTH ||
        spell == MR_BATTLE_HEAL)
        return 1;
    return spell == MR_BATTLE_GOD && result->random_outcome == 5;
}

static void mr_present_battle_spell_cast(
    MRGameRuntime *game, MRSession *session, MRBattleSpell spell,
    const MRSessionBattleSpellResult *result) {
    char line[80];
    if (spell == MR_BATTLE_GAS && result->spell.damage > 0) {
        /* 9202 prints this before falling into the ordinary damage path. */
        snprintf(line, sizeof(line), "YOU DO %d POINTS",
                 result->spell.damage);
        mr_session_draw_combat_result(
            game, session, "IT FALLS ASLEEP AND YOU KILL IT");
        mr_screen_text(game, 0, 4,
                       line, MR_CGA_MAGENTA);
        video_present(&game->host->video);
    } else {
        const char *message = mr_battle_cast_message(
            spell, result, line, sizeof(line));
        mr_session_draw_combat_result(game, session,
                                      message && message[0] ? message : NULL);
    }
    if (mr_battle_cast_has_two_second_hold(spell, &result->spell))
        mr_screen_wait_milliseconds(game, 2000);
    if (result->monster_attacked)
        mr_present_monster_turn(game, session, &result->monster);
}

static int mr_character_has_status(const MRCharacter *player, int status) {
    return (((int)player->status_flags) & status) != 0;
}

static void mr_format_qb_color_inventory_line(char *line,
                                               size_t line_size,
                                               int choice,
                                               const char *color,
                                               double count) {
    if (!line || line_size == 0) return;
    /* QuickBASIC PRINT emits one leading and one trailing blank around a
     * positive numeric value. The source then prints one explicit trailing
     * blank after the count. */
    if (count != 0.0)
        snprintf(line, line_size, " %d %s %.0f  ", choice, color, count);
    else
        snprintf(line, line_size, " %d --------------- ", choice);
}

enum {
    MR_ITEM_SELECTION_LEAVE = -1,
    MR_ITEM_SELECTION_RETRY = -2
};

static int mr_item_selection_from_key(
    int key, const int available[MR_PREP_ITEM_COUNT],
    int information_mode, int combat) {
    /* DUNSMALL 1449-16E3 and 968A-98E6 have two distinct state machines.
     * Ordinary use reads one raw key and returns to the main/combat loop on
     * any invalid, out-of-range, or unavailable selection. Only Wizard
     * Guild information mode repeats invalid choices and exposes L. Prep
     * information compares uppercase L only; battle information explicitly
     * compares both L and l. */
    if (information_mode &&
        (key == 'L' || (combat && key == 'l')))
        return MR_ITEM_SELECTION_LEAVE;
    if (key >= '1' && key <= '6') {
        int item = key - '1';
        if (available[item]) return item;
    }
    return information_mode
        ? MR_ITEM_SELECTION_RETRY : MR_ITEM_SELECTION_LEAVE;
}

static int mr_session_select_item(MRGameRuntime *game, MRSession *session,
                                  int combat, int information_mode) {
    const MRCharacter *player = &session->save.character;
    int available[MR_PREP_ITEM_COUNT] = {0};
    if (combat) {
        for (int index = 0; index < 5; index++) {
            available[index] =
                player->carried_items[MR_ITEM_SPEED_POTION + index] > 0.0f;
        }
        available[5] = player->magic_item_values[
            MR_MAGIC_HOLY_GRENADES] > 0.0f;
    } else {
        for (int index = 0; index < 4; index++) {
            available[index] = player->carried_items[index] > 0.0f;
        }
        available[4] = mr_character_has_status(
            player, MR_STATUS_BAG_OF_HOLDING);
        available[5] = mr_character_has_status(
            player, MR_STATUS_FLOOR_SLOSHER);
    }

    static const char *const prep_lines[MR_PREP_ITEM_COUNT] = {
        "1) TELEPORT SCROLL   ",
        "2) SCROLL OF SEEING  ",
        "3) SCROLL OF HEALING ",
        "4) SPELL POINT SCROLL",
        "5) BAG OF HOLDING    ",
        "6) FLOOR SLOSHER     "
    };
    static const char *const battle_lines[MR_BATTLE_ITEM_COUNT] = {
        "1) POTION OF SPEED      ",
        "2) POTION OF FIRE       ",
        "3) POTION OF SHIELDING  ",
        "4) POTION OF HEALTH     ",
        "5) POTION OF RELOCATION ",
        "6) HOLY HAND GRENADE    "
    };

    for (;;) {
        int base_row = combat ? 9 : 0;
        if (information_mode) {
            /* The Wizard Guild sets DS:B55C=1, clears SCREEN 1, and then
             * enters the ordinary item selector.  Do not redraw the dungeon
             * behind this information-only use of the shared routine. */
            mr_screen_clear(&game->screen, MR_CGA_BLACK);
            mr_screen_compose(game);
        } else {
            mr_session_draw(game, session, NULL);
        }
        mr_screen_text(game, 0, base_row,
                       combat ? "WHICH ITEM:   " : "WHICH ITEM?",
                       MR_CGA_WHITE);
        if (combat)
            mr_screen_text(game, 0, base_row + 1, "                ",
                           MR_CGA_WHITE);
        for (int index = 0; index < 6; index++) {
            char unavailable[24];
            const char *line;
            if (available[index]) {
                line = combat ? battle_lines[index] : prep_lines[index];
            } else {
                snprintf(unavailable, sizeof(unavailable),
                         combat ? "%d)------" : "%d) ------------------",
                         index + 1);
                line = unavailable;
            }
            mr_screen_text(game, 0, base_row + (combat ? 2 : 1) + index,
                           line, MR_CGA_WHITE);
        }
        if (information_mode)
            mr_screen_text(game, 0, base_row + (combat ? 8 : 7),
                           "L = LEAVE", MR_CGA_WHITE);
        video_present(&game->host->video);
        int key = (combat || information_mode)
            ? mr_read_modal_key_with_idle_monsters(game, session)
            : mr_read_original_nonempty_key(game, session);
        if (key == MR_DOS_KEY_IDLE_MONSTER_REACHED_PLAYER) return -1;
        /* 14A0-14E8 and 96D9-9726 parse the one-character response through
         * VAL into DS:52FC before checking availability.  A nonnumeric key,
         * including information-mode L, therefore leaves zero there. */
        session->shared_menu_value =
            key >= '0' && key <= '9' ? (float)(key - '0') : 0.0f;
        if (input_poll_quit(&game->host->input))
            return -1;
        int selection = mr_item_selection_from_key(
            key, available, information_mode, combat);
        if (selection != MR_ITEM_SELECTION_RETRY) return selection;
    }
}

static int mr_session_select_wand(MRGameRuntime *game, MRSession *session) {
    const MRCharacter *player = &session->save.character;
    /* Both callers LOCATE row 6, column 1 before entering 7AA1. */
    const int base_row = 5;
    mr_session_draw(game, session, NULL);
    mr_screen_text(game, 0, base_row, "WHICH WAND:             ",
                   MR_CGA_WHITE);
    for (int index = 0; index < MR_WAND_COUNT; index++) {
        char line[40];
        double count = player->persistent_values[
            MR_PERSISTENT_PURPLE_WAND_CHARGES + index];
        /* QuickBASIC PRINT places a leading and trailing space around every
         * positive number. 7AC0-7B1B has no ')' punctuation. */
        mr_format_qb_color_inventory_line(
            line, sizeof(line), index + 1, mr_wand_name(index), count);
        mr_screen_text(game, 0, base_row + 1 + index, line, MR_CGA_WHITE);
    }
    video_present(&game->host->video);
    int key = mr_read_modal_key_with_idle_monsters(game, session);
    if (key == MR_DOS_KEY_IDLE_MONSTER_REACHED_PLAYER)
        return -1;
    /* 7B2D-7B39 stores VAL(INKEY$) in DS:52FC before every range, Escape,
     * and charge-count return. */
    session->shared_menu_value =
        key >= '0' && key <= '9' ? (float)(key - '0') : 0.0f;
    if (input_poll_quit(&game->host->input) || key < '1' || key > '9')
        return -1;
    int wand = key - '1';
    /* 7B7F-7B8A returns immediately for a selected charge count below one;
     * it does not keep an empty menu open. */
    if (player->persistent_values[
            MR_PERSISTENT_PURPLE_WAND_CHARGES + wand] < 1.0f)
        return -1;
    return wand;
}

static int mr_session_select_pill(MRGameRuntime *game, MRSession *session) {
    const MRCharacter *player = &session->save.character;
    /* 7C49 itself performs LOCATE 10,1. */
    const int base_row = 9;
    mr_session_draw(game, session, NULL);
    mr_screen_text(game, 0, base_row,
                   "PICK A COLOR PILL:              ", MR_CGA_WHITE);
    for (int index = 0; index < MR_PILL_COUNT; index++) {
        char line[40];
        double count = player->persistent_values[
            MR_PERSISTENT_BLUE_PILLS + index];
        mr_format_qb_color_inventory_line(
            line, sizeof(line), index + 1, mr_pill_name(index), count);
        mr_screen_text(game, 0, base_row + 1 + index, line, MR_CGA_WHITE);
    }
    video_present(&game->host->video);
    int key = mr_read_modal_key_with_idle_monsters(game, session);
    if (key == MR_DOS_KEY_IDLE_MONSTER_REACHED_PLAYER)
        return -1;
    /* 7CD6-7CE2 uses the same shared numeric slot as choose_wand. */
    session->shared_menu_value =
        key >= '0' && key <= '9' ? (float)(key - '0') : 0.0f;
    if (input_poll_quit(&game->host->input) || key < '1' || key > '6')
        return -1;
    int pill = key - '1';
    if (player->persistent_values[
            MR_PERSISTENT_BLUE_PILLS + pill] < 1.0f)
        return -1;
    return pill;
}

static const char *mr_item_result_message(const MRItemUseResult *result,
                                          char *buffer,
                                          size_t buffer_size) {
    /* Item selectors suppress unavailable records before dispatch.  The
     * original effect routines only emit the three results below; successful
     * utility effects otherwise return silently. */
    if (result->unavailable) return "";
    if (result->floor_slosher_too_deep)
        return "DOESN'T WORK THIS DEEP";
    if (result->damage > 0) {
        snprintf(buffer, buffer_size, "YOU DO %d POINTS",
                 result->damage);
        return buffer;
    }
    if (result->no_effect) return "NO EFFECT";
    return "";
}

static const char *mr_character_race_name(const MRCharacter *player) {
    static const char *const names[] = {
        "", "HUMAN", "DWARF", "ELF", "HOBBIT"
    };
    int race = player ? (int)player->persistent_values[MR_PERSISTENT_RACE] : 0;
    return race >= MR_RACE_HUMAN && race <= MR_RACE_HOBBIT
        ? names[race] : "HUMAN";
}

static const char *mr_character_armor_name(const MRCharacter *player) {
    /* DUNSMALL 0249-0287 builds the descriptor table subsequently indexed
     * by equipped_armor at 1AF4-1B02. */
    static const char *const names[] = {
        "robes.    ", "leather armor. ", "chain armor. ",
        "plate armor. ", "field plate armor. "
    };
    int armor = player ? (int)player->equipped_armor : 0;
    if (armor < 0 || armor >= (int)(sizeof(names) / sizeof(names[0])))
        armor = 0;
    return names[armor];
}

static void mr_format_qb_using_integer_field(char *out, size_t out_size,
                                             const char *format,
                                             double value) {
    if (!out || out_size == 0) return;
    if (!format) format = "";
    const char *field = strchr(format, '#');
    if (!field) {
        snprintf(out, out_size, "%s", format);
        return;
    }
    const char *end = field;
    while (*end == '#') end++;
    size_t prefix_length = (size_t)(field - format);
    size_t width = (size_t)(end - field);
    char number[64];
    snprintf(number, sizeof(number), "%.0f", value);
    size_t number_length = strlen(number);
    size_t cursor = 0;
    size_t copy = prefix_length < out_size - 1
        ? prefix_length : out_size - 1;
    memcpy(out, format, copy);
    cursor = copy;
    if (number_length <= width) {
        size_t padding = width - number_length;
        while (padding-- && cursor + 1 < out_size) out[cursor++] = ' ';
    } else if (cursor + 1 < out_size) {
        /* QuickBASIC PRINT USING prefixes an overflowing numeric field with
         * `%` instead of silently widening a run of `#` characters. */
        out[cursor++] = '%';
    }
    for (size_t index = 0; index < number_length && cursor + 1 < out_size;
         index++)
        out[cursor++] = number[index];
    /* Literal text after the consumed field is emitted until another format
     * field begins.  DUNSMALL's attribute records end after one field. */
    while (*end && *end != '#' && cursor + 1 < out_size)
        out[cursor++] = *end++;
    out[cursor] = '\0';
}

static void mr_format_qb_leading_stat(char *out, size_t out_size,
                                      double value, const char *label) {
    char format[96];
    /* DUNSMALL 1B91-1C1E concatenates the 13-`#` numeric field and one
     * literal space before each label, then PRINT USING supplies one value.
     * Any trailing `#` characters in the source label merely begin an
     * unfilled second field and are therefore not displayed. */
    snprintf(format, sizeof(format), "############# %s", label ? label : "");
    mr_format_qb_using_integer_field(out, out_size, format, value);
}

/* Direct translation of show_magic_item_inventory, DUNSMALL 3B16-3D82.
 * The executable uses two full SCREEN 1 pages and waits for one key after
 * each page. Wands suppress zero counts; pills deliberately do not. */
static void mr_show_magic_item_inventory(MRGameRuntime *game,
                                         MRSession *session) {
    MRMagicInventorySnapshot inventory;
    mr_magic_inventory_snapshot(&session->save.character, &inventory);
    char line[96];
    int row = 1;

    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    mr_screen_text(game, 0, 0, "YOU HAVE THE FOLLOWING MAGIC ITEMS:",
                   MR_CGA_WHITE);
    for (size_t index = 0; index < inventory.first_page_count; index++) {
        const MRMagicInventoryEntry *entry = &inventory.first_page[index];
        switch (entry->kind) {
            case MR_INVENTORY_HEALTH_RINGS:
                snprintf(line, sizeof(line), " %.0f RINGS OF HEALTH ",
                         entry->amount);
                break;
            case MR_INVENTORY_BAG_COINS:
                snprintf(line, sizeof(line),
                         "YOUR BAG OF HOLDING CONTAINS  %.0f ", entry->amount);
                break;
            case MR_INVENTORY_MAGIC_SWORD:
                snprintf(line, sizeof(line), "A + %.0f MAGIC SWORD ",
                         entry->amount);
                break;
            case MR_INVENTORY_MAGIC_MACE:
                snprintf(line, sizeof(line), "A + %.0f MAGIC MACE ",
                         entry->amount);
                break;
            case MR_INVENTORY_MAGIC_RING:
                snprintf(line, sizeof(line), "A + %.0f MAGIC RING ",
                         entry->amount);
                break;
            case MR_INVENTORY_MAGIC_ARMOR:
                snprintf(line, sizeof(line), "+ %.0f MAGIC ARMOR ",
                         entry->amount);
                break;
            case MR_INVENTORY_FLOOR_SLOSHER:
                snprintf(line, sizeof(line), "A FLOOR SLOSHER ");
                break;
            case MR_INVENTORY_HOLY_GRENADES:
                snprintf(line, sizeof(line), " %.0f HOLY HAND GRENADES",
                         entry->amount);
                break;
            case MR_INVENTORY_CARRIED_ITEM: {
                const char *name = entry->source_index >= 0 &&
                    entry->source_index < MR_MAGIC_ITEMS && game->data_loaded
                    ? game->data.magic_items[entry->source_index].name
                    : "ITEM";
                snprintf(line, sizeof(line), " %s %.0f", name, entry->amount);
                break;
            }
            default:
                line[0] = 0;
                break;
        }
        mr_screen_text(game, 0, row++, line, MR_CGA_WHITE);
        if (entry->kind == MR_INVENTORY_BAG_COINS)
            mr_screen_text(game, 0, row++, "      COINS ", MR_CGA_WHITE);
    }
    /* wait_for_any_key (2F3C/C5B0) places the shared mixed-case prompt at
     * LOCATE 25, 10 when the help-reader offset is inactive. */
    mr_screen_text(game, 9, 24, "Hit any key", MR_CGA_WHITE);
    video_present(&game->host->video);
    (void)mr_read_original_nonempty_key(game, session);
    if (input_poll_quit(&game->host->input)) return;

    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    mr_screen_text(game, 0, 0, "YOU ALSO HAVE: ", MR_CGA_WHITE);
    row = 1;
    for (int wand = 0; wand < MR_WAND_COUNT; wand++) {
        if (inventory.wand_charges[wand] <= 0.0) continue;
        snprintf(line, sizeof(line), " %.0f %s WAND CHARGES",
                 inventory.wand_charges[wand], mr_wand_name(wand));
        mr_screen_text(game, 0, row++, line, MR_CGA_WHITE);
    }
    for (int pill = 0; pill < MR_PILL_COUNT; pill++) {
        snprintf(line, sizeof(line), " %.0f %s PILLS",
                 inventory.pill_counts[pill], mr_pill_name(pill));
        mr_screen_text(game, 0, row++, line, MR_CGA_WHITE);
    }
    mr_screen_text(game, 9, 24, "Hit any key", MR_CGA_WHITE);
    video_present(&game->host->video);
    (void)mr_read_original_nonempty_key(game, session);
    /* 3D73 is the fixed post-page debounce before the dungeon redraw. */
    mr_original_keyboard_drain(game);
}

/* Direct screen-content translation of show_player_statistics,
 * DUNSMALL 19F7-1C75.  The original labels the combined race/class line
 * "Class:"; that historical mislabel is intentionally preserved. */
static void mr_show_player_statistics(MRGameRuntime *game,
                                      MRSession *session,
                                      int roster_index) {
    /* show_player_statistics begins with the shared 18-poll debounce at
     * DUNSMALL 19F7. */
    mr_original_keyboard_drain(game);
    const MRCharacter *player = &session->save.character;
    char line[96];
    char name[41];
    const char *source_name = roster_index >= 0 &&
                              roster_index < game->roster.count
        ? game->roster.names[roster_index] : "";
    if (strlen(source_name) > 40) {
        memcpy(name, source_name, 37);
        memcpy(name + 37, "...", 4);
    } else {
        snprintf(name, sizeof(name), "%s", source_name);
    }

    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    snprintf(line, sizeof(line), "Player Statistics For %s", name);
    mr_screen_text(game, 0, 0, line, MR_CGA_WHITE);
    snprintf(line, sizeof(line), "Class: %s",
             mr_character_race_name(player));
    mr_screen_text(game, 0, 1, line, MR_CGA_WHITE);
    mr_screen_text(game, 0, 2,
                   (int)player->player_class == MR_CLASS_FIGHTER
                       ? " FIGHTER" : " WIZARD",
                   MR_CGA_WHITE);

    static const char *const fallback_attributes[MR_SAVE_ATTRIBUTES] = {
        "Strength:    ### ", "Intelligence:### ", "Wisdom:      ### ",
        "Health:      ### ", "Agility:     ### ", "Laziness:    ### "
    };
    for (int index = 0; index < MR_SAVE_ATTRIBUTES; index++) {
        const char *format = game->data_loaded &&
                             game->data.attribute_labels[index][0]
            ? game->data.attribute_labels[index]
            : fallback_attributes[index];
        /* 1AA4-1AC4 uses each F1.COM attribute label as a PRINT USING
         * format.  In particular the sixth original label is Laziness, not
         * the host-authored Luck substitute used by the first port. */
        mr_format_qb_using_integer_field(
            line, sizeof(line), format, player->attributes[index]);
        mr_screen_text(game, 0, 4 + index, line, MR_CGA_WHITE);
    }

    snprintf(line, sizeof(line), "You are wearing %s",
             mr_character_armor_name(player));
    mr_screen_text(game, 0, 11, line, MR_CGA_WHITE);
    mr_screen_text(game, 0, 12, "Weapons owned:", MR_CGA_WHITE);
    int row = 13;
    if ((int)player->persistent_values[MR_PERSISTENT_KNIFE_OWNED] == 1)
        mr_screen_text(game, 0, row++, "KNIFE ", MR_CGA_WHITE);
    if ((int)player->persistent_values[MR_PERSISTENT_SWORD_OWNED] == 1)
        mr_screen_text(game, 0, row++, "SWORD ", MR_CGA_WHITE);
    if ((int)player->persistent_values[MR_PERSISTENT_MACE_OWNED] == 1)
        mr_screen_text(game, 0, row++, "MACE", MR_CGA_WHITE);

    snprintf(line, sizeof(line), "Health points:  %.0f of %.0f ",
             player->health_current, player->health_max);
    mr_screen_text(game, 0, row++, line, MR_CGA_WHITE);
    mr_format_qb_leading_stat(line, sizeof(line), player->spell_points,
                              "Spell points");
    mr_screen_text(game, 0, row++, line, MR_CGA_WHITE);
    mr_format_qb_leading_stat(line, sizeof(line), player->player_level,
                              "Player level");
    mr_screen_text(game, 0, row++, line, MR_CGA_WHITE);
    mr_format_qb_leading_stat(line, sizeof(line), player->carried_weight,
                              "Player weight");
    mr_screen_text(game, 0, row++, line, MR_CGA_WHITE);
    mr_format_qb_leading_stat(line, sizeof(line), player->pocket_money,
                              "Pocket money");
    mr_screen_text(game, 0, row++, line, MR_CGA_WHITE);
    mr_format_qb_leading_stat(line, sizeof(line), player->experience,
                              "Experience");
    mr_screen_text(game, 0, row++, line, MR_CGA_WHITE);
    mr_format_qb_leading_stat(line, sizeof(line), player->bank_money,
                              "Money in bank");
    mr_screen_text(game, 0, row++, line, MR_CGA_WHITE);

    if (player->persistent_values[MR_PERSISTENT_DISEASE] > 0.0f) {
        mr_screen_text(game, 0, row++,
                       "You are diseased.  Get a cure disease at",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "   the temple (400 JP).",
                       MR_CGA_WHITE);
    }
    mr_screen_text(game, 9, 24, "Hit any key", MR_CGA_WHITE);
    video_present(&game->host->video);
    (void)mr_read_original_nonempty_key(game, session);
}

/* offer_drop_all_carried_treasure, DUNSMALL 1918-19F4. Invalid input repeats
 * the complete prompt. A confirmed drop immediately opens statistics. */
static void mr_offer_drop_all_carried_treasure(MRGameRuntime *game,
                                               MRSession *session,
                                               int roster_index) {
    if (session->save.character.carried_treasure == 0.0f) {
        mr_screen_timed_message(game, "You have no treasure.", 4000);
        return;
    }
    for (;;) {
        mr_session_draw(game, session, NULL);
        mr_screen_text(game, 0, 0,
                       "Do you want to drop all of your coins? ",
                       MR_CGA_WHITE);
        video_present(&game->host->video);
        int occupancy_interrupted = 0;
        int key = mr_read_modal_key_with_idle_monsters_ex(
            game, session, &occupancy_interrupted);
        if (occupancy_interrupted) return;
        if (input_poll_quit(&game->host->input)) return;
        if (key == 'y' || key == 'Y') {
            mr_drop_all_carried_treasure(&session->save.character, 1);
            mr_show_player_statistics(game, session, roster_index);
            return;
        }
        if (key == 'n' || key == 'N') return;
    }
}

static const char *mr_help_next_line(const char *cursor, const char *end,
                                     char *line, size_t line_size) {
    if (!cursor || cursor >= end) return NULL;
    size_t length = 0;
    while (cursor < end && *cursor != '\r' && *cursor != '\n' &&
           (unsigned char)*cursor != 0x1A) {
        if (length + 1 < line_size) line[length++] = *cursor;
        cursor++;
    }
    line[length] = 0;
    if (cursor < end && *cursor == '\r') cursor++;
    if (cursor < end && *cursor == '\n') cursor++;
    return cursor;
}

static uint8_t mr_help_advance_text_color(MRGameRuntime *game) {
    /* DUNSMALL B9D6 advances B780 through 0..6 before indexing 19E6. In
     * color mode startup fills the entries with 9..15; monochrome fills
     * every entry with 15. A leading '~' is therefore not a fixed cyan line. */
    game->dungeon_text_color_cycle++;
    if (game->dungeon_text_color_cycle > 6)
        game->dungeon_text_color_cycle = 0;
    return mr_dungeon_text_color(game, game->dungeon_text_color_cycle);
}

static void mr_help_clear_page(MRGameRuntime *game) {
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
}

static void mr_help_expand_tabs(const char *source, char *expanded,
                                size_t expanded_size) {
    size_t output = 0;
    int column = 0;
    if (!expanded_size) return;
    while (source && *source && output + 1 < expanded_size) {
        if (*source == '\t') {
            int spaces = 8 - (column & 7);
            while (spaces-- > 0 && output + 1 < expanded_size) {
                expanded[output++] = ' ';
                column++;
            }
        } else {
            expanded[output++] = *source;
            column++;
        }
        source++;
    }
    expanded[output] = 0;
}

static void mr_help_repaint_first_column(MRGameRuntime *game,
                                         const char first_cells[25],
                                         int rows, uint8_t color) {
    for (int row = 0; row < rows && row < 25; row++) {
        if (first_cells[row]) {
            char cell[2] = {first_cells[row], 0};
            mr_screen_text_80(game, 0, row, cell, color);
        }
    }
}

/* DUNSMALL C332-C5AF opens H1..H8 as SCREEN 0 / WIDTH 80 sequential
 * files. H1 is the index: keys 1..7 select H2..H8; another key returns.
 * Chapters other than H1 pause and clear after record 25, then finish with
 * the shared centered `Hit any key' prompt before chaining back to H1.
 * C47B also repaints the first command character in H1/H8, plus the embedded
 * # and Esc labels, in the source's fixed highlight color. */
static void mr_show_help(MRGameRuntime *game, MRSession *session,
                         int initial_chapter) {
    if (!game->data_loaded) {
        mr_screen_message(game, "HELP FILES ARE NOT AVAILABLE.", NULL);
        return;
    }
    int chapter = initial_chapter;
    uint8_t current_color = mr_basic_text_color(7);
    while (chapter >= 1 && chapter <= MR_HELP_CHAPTERS &&
           !input_poll_quit(&game->host->input)) {
        mr_help_clear_page(game);
        const char *cursor = game->data.help[chapter - 1];
        const char *end = cursor + game->data.help_size[chapter - 1];
        char line[192];
        char expanded[256];
        char first_cells[25] = {0};
        int page_record = 0;
        while (cursor && cursor < end) {
            const char *next = mr_help_next_line(cursor, end, line,
                                                 sizeof(line));
            if (!next) break;
            cursor = next;
            const char *visible = line;
            if (visible[0] == '~') {
                visible++;
                current_color = mr_help_advance_text_color(game);
            }
            mr_help_expand_tabs(visible, expanded, sizeof(expanded));
            if (page_record < 25)
                first_cells[page_record] = expanded[0];
            mr_screen_text_80(game, 0, page_record, expanded, current_color);
            page_record++;

            /* C3AC-C411 special-cases record 25 only when the active file
             * is not H1. The record itself already contains the MORE text;
             * there is no invented extra prompt at this boundary. */
            if (page_record == 25 && chapter != 1) {
                video_present(&game->host->video);
                (void)mr_read_original_nonempty_key(game, session);
                if (input_poll_quit(&game->host->input)) return;
                mr_help_clear_page(game);
                memset(first_cells, 0, sizeof(first_cells));
                page_record = 0;
            }
        }

        if (chapter == 1 || chapter == 8) {
            uint8_t highlight = mr_basic_text_color(
                game->color_enabled ? 15 : 7);
            if (chapter == 1) {
                mr_screen_text_80(game, 2, 12, "#", highlight);
                mr_screen_text_80(game, 0, 16, "Esc", highlight);
                mr_help_repaint_first_column(game, first_cells, 16,
                                             highlight);
            } else {
                /* H8 has twenty records. Its final PRINT leaves the cursor
                 * on row 21; LOCATE ,13 places this exit label there. */
                mr_screen_text_80(game, 12, page_record, "Esc", highlight);
                mr_help_repaint_first_column(game, first_cells, 13,
                                             highlight);
            }
        }

        if (chapter == 1) {
            video_present(&game->host->video);
            int key = mr_read_original_nonempty_key(game, session);
            if (input_poll_quit(&game->host->input)) return;
            if (key >= '1' && key <= '7') chapter = key - '0' + 1;
            else break;
        } else {
            /* C45C advances the help color once more before C5B0 prints the
             * shared prompt at BASIC row 25, column 35. */
            current_color = mr_help_advance_text_color(game);
            mr_screen_text_80(game, 34, 24, "Hit any key", current_color);
            video_present(&game->host->video);
            (void)mr_read_original_nonempty_key(game, session);
            if (input_poll_quit(&game->host->input)) return;
            chapter = 1;
        }
    }
}

/* DUNSMALL 7FFB-8039. In the DOS original Q saves and terminates the
 * process. The embedded native port maps that terminal action to its safe
 * character-selection boundary so it never tears down Moraff's World. */
static int mr_pause_game(MRGameRuntime *game, MRSession *session) {
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    mr_screen_text(game, 14, 9, "PAUSE... (Q FOR DOS)", MR_CGA_WHITE);
    video_present(&game->host->video);
    int key = mr_read_original_nonempty_key(game, session);
    /* 8019 calls raw read_nonempty_key and 801C compares only the literal
     * uppercase Q. The 2F87 normalizer belongs to the dispatcher that
     * entered this routine and is not applied to the pause response. */
    return key == 'Q';
}

static int mr_prompt_action_delay(MRGameRuntime *game, int current) {
    char digits[16] = {0};
    int length = 0;
    static const char clear_line[] =
        "                                        ";
    (void)current;

    /* The E command does not enter a modal full-screen form. 0F0E calls the
     * command-overlay message path and prints at LOCATE 1,1 over the current
     * dungeon frame. Clear only that four-row notice area once. */
    for (int row = 0; row <= 3; row++)
        mr_screen_text_attribute(game, 0, row, clear_line,
                                 mr_basic_text_color(0),
                                 mr_basic_text_color(0));
    mr_screen_text(game, 0, 0,
                   "Try delays between 0 (Default) and 3000.",
                   MR_CGA_WHITE);
    mr_screen_text(game, 0, 1, "Enter delay and hit return:",
                   MR_CGA_WHITE);
    for (;;) {
        /* read_bank_transfer_amount records this exact cursor after the
         * second PRINT. Its 15-character field can wrap two cells onto row
         * three (zero-based row two), just as SCREEN 1 PRINT does. */
        mr_screen_fixed_wrap(game, 27, 1, 40, "               ",
                             MR_CGA_WHITE);
        mr_screen_fixed_wrap(game, 27, 1, 40, digits, MR_CGA_WHITE);
        video_present(&game->host->video);
        int key = input_wait_any_key(&game->host->input);
        if (input_poll_quit(&game->host->input)) return current;
        if (key == '\r') {
            if (!length) return 0;
            unsigned long long value = strtoull(digits, NULL, 10);
            return value > 3000 ? 3000 : (int)value;
        }
        if (key == '\b' && length > 0) digits[--length] = 0;
        else if (key >= '0' && key <= '9' && length < 15) {
            digits[length++] = (char)key;
            digits[length] = 0;
        }
    }
}

static int mr_current_cell_is_revealed(const MRSession *session) {
    if (!session) return 0;
    int level = (int)floor(session->save.character.dungeon_level);
    int x = (int)floor(session->save.character.player_x);
    int y = (int)floor(session->save.character.player_y);
    if (level < 0 || level >= MR_EXPLORED_LEVELS ||
        x < 1 || x > 20 || y < 1 || y > 19)
        return 0;
    return (session->save.explored[level][y] & (1U << (20 - x))) != 0;
}

static int mr_should_apply_configured_delay(const MRSession *session) {
    /* draw_status_and_map 4100-416B skips the delay on an unrevealed cell or
     * after more than three consecutive arrow strings. */
    return session && session->phase != MR_SESSION_COMBAT &&
           mr_current_cell_is_revealed(session) &&
           session->consecutive_arrow_commands <= 3;
}

static void mr_record_status_lookahead_for_delay(MRSession *session,
                                                 int key, int scan) {
    if (!session || session->phase == MR_SESSION_COMBAT) return;
    if (key == 0 && (scan == MR_ARROW_UP || scan == MR_ARROW_RIGHT ||
                     scan == MR_ARROW_DOWN || scan == MR_ARROW_LEFT))
        session->consecutive_arrow_commands++;
    else
        session->consecutive_arrow_commands = 0;
}

static void mr_consume_status_input_lookahead(MRGameRuntime *game,
                                              MRSession *session) {
    if (!game || !session) return;
    int x = (int)floor(session->save.character.player_x);
    int y = (int)floor(session->save.character.player_y);
    /* 41AF-425F reaches INKEY$ only for an empty player cell, exploration
     * view depth (not combat depth one), the ordinary redraw state, and a
     * revealed current map bit. An empty/non-arrow sample resets B5FA; a
     * queued arrow string is consumed, increments it, and returns before
     * the normal 4260 refresh. The later 087F dispatcher therefore receives
     * the next BIOS typematic event, not this lookahead. */
    if ((session->phase != MR_SESSION_EXPLORING &&
         session->phase != MR_SESSION_TOWN) ||
        mr_session_monster_at(session, x, y) != 0 ||
        !mr_current_cell_is_revealed(session)) {
        session->consecutive_arrow_commands = 0;
        return;
    }
    int key = -1, scan = 0;
    if (input_kbhit(&game->host->input))
        key = mr_read_dos_key(&game->host->input, &scan);
    mr_record_status_lookahead_for_delay(session, key, scan);
}

static int mr_configured_delay_iteration_count(const MRSession *session) {
    if (!mr_should_apply_configured_delay(session)) return 0;
    return (session->configured_action_delay / 6) *
           session->consecutive_arrow_commands;
}

static void mr_apply_configured_delay(const MRSession *session) {
    /* 4127-4169 is an empty floating-point-counted loop, not a TIMER wait.
     * 3F:87 divides the configured value by six, QuickBASIC INT floors it,
     * and 3F:91 multiplies by B5FA. Preserve that odd increasing 1x/2x/3x
     * arrow-lookahead delay before the >3 bypass instead of applying the raw
     * configured value once per dispatched command. */
    if (!mr_should_apply_configured_delay(session)) return;
    volatile int sink = 0;
    int count = mr_configured_delay_iteration_count(session);
    for (int index = 0; index < count; index++) sink += index & 1;
    (void)sink;
}

static const char *mr_ambient_advice_text(MRAmbientAdvice advice) {
    switch (advice) {
        case MR_ADVICE_STAY_AT_INN: return "You should stay at an Inn.";
        case MR_ADVICE_GET_CURE: return "You could use a cure!";
        case MR_ADVICE_DUNGEON_TOO_DEEP:
            return "I don't think you'll survive down here.";
        case MR_ADVICE_CASH_IN_TREASURE:
            return "Go to bank to cash in treasure";
        default: return NULL;
    }
}

static int mr_town_choice(MRGameRuntime *game, MRSession *session,
                          const char *valid) {
    for (;;) {
        int key = mr_read_original_nonempty_key(game, session);
        if (input_poll_quit(&game->host->input)) return 0;
        /* DUNSMALL 2F71 is the raw nonempty-INKEY$ helper used by every
         * caller of this wrapper.  Unlike the exploration dispatcher at
         * 0CD2 and combat dispatcher at 8704, it does not call the 2F87
         * uppercase helper. These prompts therefore accept the displayed
         * uppercase choices only; 2F43's attribute clamp has already run. */
        if (key && strchr(valid, key)) return key;
    }
}

static uint8_t mr_dungeon_text_color(const MRGameRuntime *game, int index) {
    /* DUNSMALL 011E-01CC initializes 19E6(0..6). Monochrome stores 15 in
     * every slot; color mode stores 9 through 15. */
    if (index < 0) index = 0;
    if (index > 6) index = 6;
    return mr_basic_text_color(game && game->color_enabled ? 9 + index : 15);
}

static uint8_t mr_select_random_dungeon_text_color(MRGameRuntime *game,
                                                    MRSession *session) {
    /* B9B3 consumes RND, selects INT(RND*6), and indexes 19E6. */
    int index = session
        ? (int)floorf(mr_rng_next(&session->rng) * 6.0f) : 0;
    if (index < 0) index = 0;
    if (index > 5) index = 5;
    return mr_dungeon_text_color(game, index);
}

static int mr_prompt_session_sound(MRGameRuntime *game, MRSession *session) {
    /* 0514-0545 prompts on every DUNSMALL invocation. 0514 first calls
     * B9B3, so choosing the prompt color consumes one gameplay RND value.
     * The original starts with sound disabled and changes that state only
     * for a Y response. */
    uint8_t color = mr_select_random_dungeon_text_color(game, session);
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    mr_screen_text(game, 0, 0, "Sound (Y or N)?", color);
    video_present(&game->host->video);
    return mr_town_choice(game, session, "YN") == 'Y';
}

static int mr_decode_review_line(const char *source, char *text,
                                 size_t text_size, int *color_index) {
    if (!source || !text || !text_size) return 0;
    const char *cursor = source;
    int index = -1;
    if (*cursor >= '1' && *cursor <= '7') index = *cursor++ - '1';
    size_t out = 0;
    while (*cursor && *cursor != '\r' && *cursor != '\n' &&
           out + 1 < text_size) {
        /* BA7C-BAE2 replaces the file's vertical-bar marker with a comma. */
        text[out++] = *cursor == '|' ? ',' : *cursor;
        cursor++;
    }
    text[out] = 0;
    if (color_index) *color_index = index;
    return 1;
}

static void mr_show_loading_review(MRGameRuntime *game, MRSession *session) {
    /* BA15 calls B9B3 for the default text color before BA1E-BA4F consumes
     * another RND to choose REVIEW.1..6. Both values belong to the same
     * QuickBASIC stream used by later dungeon, combat, and loot logic. */
    uint8_t default_color =
        mr_select_random_dungeon_text_color(game, session);
    int review = (int)floorf(mr_rng_next(&session->rng) * 6.0f) + 1;
    char name[24], path[512];
    snprintf(name, sizeof(name), "REVIEW.%d", review);
    mr_game_join(path, sizeof(path), game->resource_directory, name);

    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    FILE *file = fopen(path, "rb");
    int row = 0;
    if (file) {
        char source[192], text[192];
        while (row < 24 && fgets(source, sizeof(source), file)) {
            int color_index;
            if (mr_decode_review_line(source, text, sizeof(text),
                                      &color_index)) {
                uint8_t color = color_index >= 0
                    ? mr_dungeon_text_color(game, color_index)
                    : default_color;
                mr_screen_text_80(game, 0, row++, text, color);
            }
        }
        fclose(file);
    }
    /* BB9E-BBC8 closes the review file, places this loading notice at row
     * 25, and starts its own PLAY score. The acknowledgement is not on this
     * SCREEN 0 page; it is printed after the character load and SCREEN 1
     * transition at 057A-0591. */
    mr_screen_text_80(game, 0, 24, "Please read this...", default_color);
    video_present(&game->host->video);
    mr_play_session_music(game, session, mr_music_loading_review);
}

static void mr_town_draw_money(MRGameRuntime *game,
                               const MRCharacter *player, int row) {
    char line[64];
    snprintf(line, sizeof(line),
             "Jewel pieces with character: %9.0f", player->pocket_money);
    mr_screen_text(game, 0, row, line, MR_CGA_WHITE);
}

static int mr_town_prompt_amount(MRGameRuntime *game,
                                 MRSession *session,
                                 const char *first, const char *second,
                                 int start_row,
                                 double *amount) {
    char digits[24] = {0};
    int length = 0;
    int input_row;
    int input_column;

    /* read_bank_transfer_amount (21F3-22F4) reads at the current PRINT
     * cursor. Withdrawal is one fixed 40-column wrapping literal; deposit
     * prints its first sentence with a newline and leaves the cursor after
     * the indented second sentence. */
    if (second) {
        mr_screen_text(game, 0, start_row, first, MR_CGA_WHITE);
        mr_screen_text(game, 0, start_row + 1, second, MR_CGA_WHITE);
        input_row = start_row + 1;
        input_column = (int)strlen(second) % 40;
    } else {
        size_t prompt_length = strlen(first);
        mr_screen_fixed_wrap(game, 0, start_row, 40, first, MR_CGA_WHITE);
        input_row = start_row + (int)(prompt_length / 40);
        input_column = (int)(prompt_length % 40);
    }
    for (;;) {
        mr_screen_text(game, input_column, input_row,
                       "               ", MR_CGA_WHITE);
        mr_screen_text(game, input_column, input_row, digits, MR_CGA_WHITE);
        video_present(&game->host->video);
        /* read_bank_transfer_amount polls through raw helper 2F71 for every
         * character, including rejected keys.  Besides waiting for a
         * nonempty key, that helper clamps all six attributes to one. */
        int key = mr_read_original_nonempty_key(game, session);
        if (input_poll_quit(&game->host->input)) return 0;
        if (key == '\r') {
            *amount = length ? atof(digits) : 0.0;
            return 1;
        }
        if (key == '\b' && length > 0) digits[--length] = 0;
        else if (key >= '0' && key <= '9' && length < 15) {
            digits[length++] = (char)key;
            digits[length] = 0;
        }
    }
}

static void mr_visit_inn(MRGameRuntime *game, MRSession *session,
                         MRInnType inn) {
    /* render_inn_stay_prompt (1DB7-1DEE) prints one concatenated 40-column
     * string.  Preserve the resulting SCREEN 1 wrap, including its padding. */
    static const char *const first_lines[] = {
        "You are at the Flea Bag Inn.  A room    ",
        "You are at the Yuppydom Inn.  A suite   ",
        "You are at the Kings Inn.  A grand suite"
    };
    static const char *const second_lines[] = {
        "   will cost 10 jewel pieces.",
        "   will cost 200 jewel pieces.",
        "   will cost 6000 jewel pieces."
    };
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    mr_screen_text(game, 0, 0, first_lines[inn], MR_CGA_WHITE);
    mr_screen_text(game, 0, 1, second_lines[inn], MR_CGA_WHITE);
    mr_screen_text(game, 0, 3, "Do you want to stay (Y or N)?",
                   MR_CGA_WHITE);
    video_present(&game->host->video);
    if (mr_town_choice(game, session, "YN") != 'Y') return;

    MRInnOutcome outcome = mr_inn_stay(&session->save.character, inn,
                                        &session->rng);
    if (outcome.result == MR_TOWN_INSUFFICIENT_FUNDS) {
        mr_screen_text(game, 0, 4,
                       "A gaurd throws you out because you",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, 5, "   don't have enough money.",
                       MR_CGA_WHITE);
        video_present(&game->host->video);
        mr_screen_wait_milliseconds(game, 4000);
        return;
    }

    /* sleep_at_inn calls the background PLAY routine. With sound disabled,
     * 05B8 substitutes wait_four_seconds. Kings then makes one additional
     * explicit wait_four_seconds call. */
    int row = 4;
    if (inn == MR_INN_KINGS) {
        mr_screen_text(game, 0, row++,
                       "A hotel staff cleric heals all of your",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "   wounds.", MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "You are sleeping...", MR_CGA_WHITE);
        video_present(&game->host->video);
        if (session->sound_disabled)
            mr_screen_wait_milliseconds(game, 4000);
        else
            mr_play_session_music(game, session, mr_music_sleep);
        mr_screen_wait_milliseconds(game, 4000);
    } else {
        mr_screen_text(game, 0, row++, "You are sleeping...", MR_CGA_WHITE);
        video_present(&game->host->video);
        if (session->sound_disabled)
            mr_screen_wait_milliseconds(game, 4000);
        else
            mr_play_session_music(game, session, mr_music_sleep);
    }
    if (outcome.robbed) {
        mr_screen_text(game, 0, row++, "I think that you were robbed.",
                       MR_CGA_WHITE);
        video_present(&game->host->video);
        mr_screen_wait_milliseconds(game, 4000);
    }
    if (outcome.became_diseased) {
        mr_screen_text(game, 0, row++,
                       "You feel very sick.  You throw up on    ",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, row++,
                       "   the bed.  I think you should see a   ",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "   doctor.   ", MR_CGA_WHITE);
        video_present(&game->host->video);
        mr_screen_wait_milliseconds(game, 8000);
    }
}

static void mr_visit_bank(MRGameRuntime *game, MRSession *session) {
    MRCharacter *player = &session->save.character;
    mr_bank_exchange_carried_treasure(player);
    int initial_page = 1;
    for (;;) {
        char line[96];
        mr_screen_clear(&game->screen, MR_CGA_BLACK);
        mr_screen_compose(game);
        int row = 0;
        if (initial_page &&
            player->persistent_values[MR_PERSISTENT_OWNS_TOWN] == 0.0f) {
            const char *owner = game->selected >= 0 &&
                                game->selected < game->roster.count
                ? game->roster.names[game->selected] : "";
            snprintf(line, sizeof(line), "A sign says: Bank for sale, %s",
                     owner);
            row = mr_screen_fixed_wrap(game, 0, row, 40, line,
                                       MR_CGA_WHITE);
            mr_screen_text(game, 0, row++,
                           "   5,000,000 JP.  Heh heh heh.", MR_CGA_WHITE);
        }
        if (initial_page) {
            mr_screen_text(game, 0, row++,
                           "You are in the bank. Your treasure has",
                           MR_CGA_WHITE);
            mr_screen_text(game, 0, row++,
                           "   been exchanged for jewelry.", MR_CGA_WHITE);
        }
        snprintf(line, sizeof(line), "Jewel pieces in the bank:   %9.0f",
                 player->bank_money);
        mr_screen_text(game, 0, row++, line, MR_CGA_WHITE);
        snprintf(line, sizeof(line), "Jewel pieces in your pocket:%9.0f",
                 player->pocket_money);
        mr_screen_text(game, 0, row++, line, MR_CGA_WHITE);
        mr_screen_text(game, 0, row++,
                       "Hit `D' to deposit jewelry, `W' to",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, row++,
                       "   withdraw jewelry, and `L' to leave.",
                       MR_CGA_WHITE);
        video_present(&game->host->video);
        int choice = mr_town_choice(game, session, "DWL");
        if (!choice || choice == 'L') return;
        double amount = 0.0;
        if (choice == 'W') {
            if (!mr_town_prompt_amount(
                    game, session,
                    "Type the amount  of the  withdrawal  and   hit return:",
                    NULL, row, &amount))
                return;
            mr_bank_transfer(player, amount, 1);
        } else {
            if (!mr_town_prompt_amount(
                    game, session,
                    "Type the amount that you wish to deposit",
                    "   and hit return:", row, &amount))
                return;
            mr_bank_transfer(player, amount, 0);
        }
        /* Every completed transfer executes CLS then jumps directly to
         * 23AB, omitting the ownership sign and exchange narrative. */
        initial_page = 0;
    }
}

static void mr_visit_temple(MRGameRuntime *game, MRSession *session) {
    MRCharacter *player = &session->save.character;
    int another = 0;
    int initial_page = 1;
    const char *notice = NULL;
    for (;;) {
        char line[64];
        mr_screen_clear(&game->screen, MR_CGA_BLACK);
        mr_screen_compose(game);
        int row = 0;
        if (initial_page) {
            /* DUNSMALL 252B-256D prints this five-line introduction only on
             * entry.  Successful purchases clear the screen and return at
             * 2570, below this block. */
            mr_screen_text(game, 0, row++,
                           "A man in robes says, `Welcome to the",
                           MR_CGA_WHITE);
            mr_screen_text(game, 0, row++, "   temple.", MR_CGA_WHITE);
            /* 2543 selects the background-mode temple score after these two
             * PRINTs and before the remaining introduction. Present that
             * intermediate state before queueing the asynchronous MML. */
            video_present(&game->host->video);
            mr_play_session_music(game, session, mr_music_temple);
            mr_screen_text(game, 0, row++,
                           "  Do you wish to purchase", MR_CGA_WHITE);
            mr_screen_text(game, 0, row++,
                           "   a spell?'  You can hear many coins",
                           MR_CGA_WHITE);
            mr_screen_text(game, 0, row++,
                           "   jingling in his robes.", MR_CGA_WHITE);
            initial_page = 0;
        } else if (notice) {
            mr_screen_text(game, 0, row++, notice, MR_CGA_WHITE);
            notice = NULL;
        }

        /* 2570 deliberately emits one blank line before the balances. */
        row++;
        mr_town_draw_money(game, player, row++);
        snprintf(line, sizeof(line), "Your health points:  %.0f of %.0f ",
                 player->health_current, player->health_max);
        mr_screen_text(game, 0, row++, line, MR_CGA_WHITE);
        mr_screen_text(game, 0, row++,
                       another ? "Another spell?" : "Which spell?",
                       MR_CGA_WHITE);
        another = 1;
        row++;
        mr_screen_text(game, 0, row++, "1) Cure wounds: 75 JP",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "2) Heal all wounds: 1000 JP",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "3) Cure disease: 400 JP",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "4) Remove poison: 20000 JP",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "5) Gain level: 500000 JP",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "L = Leave", MR_CGA_WHITE);
        video_present(&game->host->video);
        int choice = mr_town_choice(game, session, "12345L");
        if (!choice || choice == 'L') return;
        MRTownResult result = mr_temple_purchase(
            player, (MRTempleService)(choice - '0'), &session->rng);
        if (result == MR_TOWN_INSUFFICIENT_FUNDS) {
            /* 27CD-27F7 prefixes the cleric sentence with the shared exact
             * insufficient-funds string, appends it to the standing menu,
             * and holds both lines four seconds. */
            mr_screen_text(game, 0, row++,
                           "You do not have enough money. The good",
                           MR_CGA_WHITE);
            mr_screen_text(game, 0, row, "   cleric throws you out.",
                           MR_CGA_WHITE);
            video_present(&game->host->video);
            mr_screen_wait_milliseconds(game, 4000);
            return;
        }
        notice = "You feel very good.";
        if (result == MR_TOWN_NO_EFFECT)
            notice = "You don't feel any different.";
        else if (choice == '2') notice = "You feel perfect.";
        else if (choice == '3')
            notice = "You don't feel sick anymore.";
        else if (choice == '4') notice = "The poison is gone.";
        else if (choice == '5')
            notice = "You feel EXTREMELY good.";
        another = 1;
    }
}

static void mr_visit_store(MRGameRuntime *game, MRSession *session) {
    MRCharacter *player = &session->save.character;
    int first_menu = 1;
    for (;;) {
        /* The dispatcher already performs the initial 281E drain.  Every
         * successful purchase and recoverable error jumps back through 281E,
         * so repeat visits to this menu receive the same fixed 18-poll
         * debounce rather than accepting the purchase key a second time. */
        if (!first_menu) mr_original_keyboard_drain(game);
        first_menu = 0;
        mr_screen_clear(&game->screen, MR_CGA_BLACK);
        mr_screen_compose(game);
        int row = 0;
        char line[96];
        mr_screen_text(game, 0, row++, "You are in the store.",
                       MR_CGA_WHITE);
        /* 2833-283C calls the inventory-only tail of player statistics. */
        snprintf(line, sizeof(line), "You are wearing %s",
                 mr_character_armor_name(player));
        mr_screen_text(game, 0, row++, line, MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "Weapons owned:", MR_CGA_WHITE);
        if ((int)player->persistent_values[MR_PERSISTENT_KNIFE_OWNED] == 1)
            mr_screen_text(game, 0, row++, "KNIFE ", MR_CGA_WHITE);
        if ((int)player->persistent_values[MR_PERSISTENT_SWORD_OWNED] == 1)
            mr_screen_text(game, 0, row++, "SWORD ", MR_CGA_WHITE);
        if ((int)player->persistent_values[MR_PERSISTENT_MACE_OWNED] == 1)
            mr_screen_text(game, 0, row++, "MACE", MR_CGA_WHITE);
        row++;
        mr_town_draw_money(game, player, row++);
        mr_screen_text(game, 0, row++, "   Which would you like to buy?",
                       MR_CGA_WHITE);
        row++;
        mr_screen_text(game, 0, row++, "1) Knife:  10 JP", MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "2) Mace:  200 JP", MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "3) Sword:  200 JP", MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "4) Leather armor: 200 JP",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "5) Chain armor: 500 JP",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "6) Plate armor: 3000 JP",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "7) Field plate armor: 10000 JP",
                       MR_CGA_WHITE);
        if (mr_store_town_offer_visible(player))
            mr_screen_text(game, 0, row++, "8) The Town: 1000000 JP",
                           MR_CGA_WHITE);
        mr_screen_text(game, 0, row, "L = Leave", MR_CGA_WHITE);
        video_present(&game->host->video);
        int choice = mr_town_choice(game, session,
            mr_store_town_offer_visible(player) ? "12345678L" : "1234567L");
        if (!choice || choice == 'L') return;
        MRTownResult result = mr_store_purchase(
            player, (MRStoreItem)(choice - '0'));
        if (result == MR_TOWN_OK) {
            if (choice == '8') {
                /* Preserve the registered executable's C8D0 typo exactly. */
                mr_screen_fixed_wrap(
                    game, 0, row + 1, 40,
                    "I'm also selling the Brooklyn bridge,      want to it, too?",
                    MR_CGA_WHITE);
                mr_screen_text(game, 9, 24, "Hit any key",
                               MR_CGA_WHITE);
                video_present(&game->host->video);
                (void)mr_read_original_nonempty_key(game, session);
                return;
            }
            continue;
        }
        if (result == MR_TOWN_CLASS_FORBIDDEN) {
            mr_screen_text(game, 0, row + 1,
                           "You can't use that because you are a",
                           MR_CGA_WHITE);
            mr_screen_text(game, 0, row + 2, "   magic user.",
                           MR_CGA_WHITE);
            video_present(&game->host->video);
            mr_screen_wait_milliseconds(game, 4000);
        } else {
            const char *text = "You do not have enough money.";
            if (result == MR_TOWN_ALREADY_OWNED)
                text = "You already have that weapon.";
            else if (result == MR_TOWN_NO_UPGRADE)
                text = "You don't need that anymore.";
            mr_screen_text(game, 0, row + 1, text, MR_CGA_WHITE);
            video_present(&game->host->video);
            mr_screen_wait_milliseconds(game, 4000);
        }
    }
}

static void mr_guild_show_item_description(MRGameRuntime *game,
                                           MRSession *session,
                                           const MRMagicItem *item) {
    char record[512];
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    /* F2.COM stores the name, colon and explanation as one fixed-width
     * record. DUNSMALL 2CC2/2D29 prints that record directly after CLS. */
    snprintf(record, sizeof(record), "%s: %s", item->name,
             item->description);
    mr_screen_fixed_wrap(game, 0, 0, 40, record, MR_CGA_WHITE);
    video_present(&game->host->video);
    (void)mr_read_original_nonempty_key(game, session);
}

static void mr_guild_append_spell_descriptions(MRGameRuntime *game,
                                               MRSession *session,
                                               int row,
                                               const char *heading,
                                               const char *first,
                                               const char *second) {
    mr_screen_text(game, 0, row++, heading, MR_CGA_WHITE);
    row = mr_screen_fixed_wrap(game, 0, row, 40, first, MR_CGA_WHITE);
    mr_screen_fixed_wrap(game, 0, row, 40, second, MR_CGA_WHITE);
    mr_screen_text(game, 9, 24, "Hit any key", MR_CGA_WHITE);
    video_present(&game->host->video);
    (void)mr_read_original_nonempty_key(game, session);
}

static void mr_guild_append_insufficient_funds(MRGameRuntime *game,
                                               int row) {
    /* 2EF3-2F14 appends the shared money error to the current guild page and
     * holds it for the recovered four-second TIMER interval. */
    mr_screen_text(game, 0, row++,
                   "You do not have enough money. You find",
                   MR_CGA_WHITE);
    mr_screen_text(game, 0, row,
                   "   yourself floating out of the guild...",
                   MR_CGA_WHITE);
    video_present(&game->host->video);
    mr_screen_wait_milliseconds(game, 4000);
}

static void mr_visit_wizard_guild(MRGameRuntime *game, MRSession *session) {
    MRCharacter *player = &session->save.character;
    int first_menu = 1;
    for (;;) {
        /* DUNSMALL 2BB8-2BC1 is the guild's common re-entry label: CLS,
         * fixed keyboard drain, then redraw.  The outer town dispatcher has
         * already supplied the first drain, so reproduce it on later passes
         * only. */
        if (!first_menu) mr_original_keyboard_drain(game);
        first_menu = 0;
        mr_screen_clear(&game->screen, MR_CGA_BLACK);
        mr_screen_compose(game);
        mr_screen_text(game, 0, 0, "You are in the wizard's guild.",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, 2,
                       "You may find out what the various spells",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, 3, "   and magic items do.", MR_CGA_WHITE);
        mr_screen_text(game, 0, 5, "1-SPELLS", MR_CGA_WHITE);
        mr_screen_text(game, 0, 6, "2-MAGIC ITEMS", MR_CGA_WHITE);
        mr_screen_text(game, 0, 7, "L-LEAVE WIZARD'S GUILD",
                        MR_CGA_WHITE);
        video_present(&game->host->video);
        int choice = mr_town_choice(game, session, "12L");
        if (!choice || choice == 'L') return;

        if (choice == '1') {
            /* 2D54 continues printing below the standing guild menu. */
            mr_screen_text(game, 0, 8,
                           "Type the spell level (1-6): ", MR_CGA_WHITE);
            video_present(&game->host->video);
            int level_key = mr_town_choice(game, session, "123456L");
            if (!level_key || level_key == 'L') return;
            int level = level_key - '0';
            char typed[2] = {(char)level_key, 0};
            mr_screen_text(game, 30, 8, typed, MR_CGA_WHITE);
            int price = mr_wizard_guild_spell_information_price(level);
            char line[64];
            snprintf(line, sizeof(line), "That will cost you %d JP.", price);
            mr_screen_text(game, 0, 9, line, MR_CGA_WHITE);
            if (mr_wizard_guild_charge_spell_information(
                    player, level, 0) == MR_TOWN_INSUFFICIENT_FUNDS) {
                mr_guild_append_insufficient_funds(game, 10);
                return;
            }
            mr_screen_text(game, 0, 10,
                           "P=Prep spells (used while not fighting)",
                           MR_CGA_WHITE);
            mr_screen_text(game, 0, 11, "B=Battle spells   L=Leave",
                           MR_CGA_WHITE);
            video_present(&game->host->video);
            int type = mr_town_choice(game, session, "PBL");
            if (!type || type == 'L') return;
            MRTownResult result = mr_wizard_guild_charge_spell_information(
                player, level, 1);
            if (result == MR_TOWN_INSUFFICIENT_FUNDS) {
                mr_guild_append_insufficient_funds(game, 12);
                return;
            }
            int index = (level - 1) * 2;
            const char *first = type == 'P'
                ? game->data.spells[index].preparation_description
                : game->data.spells[index].battle_description;
            const char *second = type == 'P'
                ? game->data.spells[index + 1].preparation_description
                : game->data.spells[index + 1].battle_description;
            mr_guild_append_spell_descriptions(game, session, 12,
                type == 'P' ? "PREP SPELLS" : "BATTLE SPELLS",
                first, second);
        } else {
            /* 2C35 likewise appends the price and type chooser below the
             * original three-option guild page. */
            mr_screen_text(game, 0, 8, "This will cost you 800 JP.",
                           MR_CGA_WHITE);
            if (mr_wizard_guild_charge_information(player, 0) ==
                    MR_TOWN_INSUFFICIENT_FUNDS) {
                mr_guild_append_insufficient_funds(game, 9);
                return;
            }
            mr_screen_text(game, 0, 9,
                           "P=Prep items (used while not fighting)",
                           MR_CGA_WHITE);
            mr_screen_text(game, 0, 10, "B=Battle items   L=Leave",
                           MR_CGA_WHITE);
            video_present(&game->host->video);
            int type = mr_town_choice(game, session, "PBL");
            if (!type || type == 'L') return;

            int selected = mr_session_select_item(
                game, session, type == 'B', 1);
            /* L from the information selector returns to 2BB8, not to the
             * town dispatcher. */
            if (selected < 0) continue;
            MRTownResult result = mr_wizard_guild_charge_information(
                player, 1);
            if (result == MR_TOWN_INSUFFICIENT_FUNDS) {
                mr_guild_append_insufficient_funds(game, 0);
                return;
            }
            int data_index = selected + (type == 'P' ? 0 : 7);
            mr_guild_show_item_description(
                game, session, &game->data.magic_items[data_index]);
        }
    }
}

static void mr_visit_town_location(MRGameRuntime *game, MRSession *session,
                                   MRTownLocation location) {
    /* The seven rope destinations enter through drains at 19F7/1FCD, 22F7,
     * 2522, 281E and 2BBE. One shared native boundary is equivalent because
     * only one destination can occupy the player's town coordinate. */
    mr_original_keyboard_drain(game);
    switch (location) {
        case MR_TOWN_LOCATION_FLEA_BAG_INN:
            mr_visit_inn(game, session, MR_INN_FLEA_BAG); break;
        case MR_TOWN_LOCATION_YUPPYDOM_INN:
            mr_visit_inn(game, session, MR_INN_YUPPYDOM); break;
        case MR_TOWN_LOCATION_KINGS_INN:
            mr_visit_inn(game, session, MR_INN_KINGS); break;
        case MR_TOWN_LOCATION_BANK:
            mr_visit_bank(game, session); break;
        case MR_TOWN_LOCATION_TEMPLE:
            mr_visit_temple(game, session); break;
        case MR_TOWN_LOCATION_STORE:
            mr_visit_store(game, session); break;
        case MR_TOWN_LOCATION_WIZARD_GUILD:
            mr_visit_wizard_guild(game, session); break;
        default:
            break;
    }
}

static const char *mr_reward_armor_name(int armor) {
    /* 0249-0287 builds this exact lowercase table; B233-B262 prints the
     * selected element immediately after "You find ". */
    static const char *const names[] = {
        "robes.    ", "leather armor. ", "chain armor. ",
        "plate armor. ", "field plate armor. "
    };
    return armor >= 0 && armor < (int)(sizeof(names) / sizeof(names[0]))
        ? names[armor] : names[0];
}

static const char *mr_reward_attribute_name(int attribute) {
    /* B03E-B068 stores the five four-byte string descriptors in the order
     * strength, learning, wizdom, health, agility. B080 indexes that table
     * with the one-based reward roll. Luck is not in the book table. */
    static const char *const names[] = {
        "", "strength.", "learning.", "wizdom.", "health.", "agility."
    };
    return attribute >= 1 && attribute <= 5 ? names[attribute] : "";
}

static void mr_reward_clear(MRGameRuntime *game) {
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
}

static void mr_reward_coin_frame(MRGameRuntime *game, MRSession *session) {
    /* A890-A899 does not leave the coin prompt on an empty screen. After
     * CLS it restores the four cached dungeon panes through 580F and draws
     * the combat-sized monster through 6BAF. The map and fixed labels remain
     * erased. */
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    if (!session) {
        mr_screen_compose(game);
        return;
    }
    const MRCharacter *player = &session->save.character;
    int level = (int)floor(player->dungeon_level);
    int player_x = (int)floor(player->player_x);
    int player_y = (int)floor(player->player_y);
    int restored_cached_panes = mr_render_viewport_cache_restore(
            &game->viewport_cache, &game->layout, game->screen.pixels,
            MR_RENDER_WIDTH, level, player_x, player_y,
            session->input.facing);
    if (restored_cached_panes) {
        /* A890 calls 580F before A956 performs the source bug's
         * DS:52FC-based carry check. Preserve the rotation write as well as
         * the reconstructed pixels. */
        session->shared_menu_value = mr_cached_viewport_rotation(
            &game->viewport_cache, session->input.facing);
    } else {
        /* The original always has a valid GET cache here. Keep a deterministic
         * native fallback for imported sessions that reach rewards before a
         * frame was cached. */
        for (int pass = 0; pass < 4; ++pass)
            mr_draw_projected_view(game, session, pass);
    }
    if (game->assets_loaded) {
        const MRMonsterAssetSet *set =
            session->combat_profile.resource_set == 2
                ? &game->assets.deep : &game->assets.shallow;
        MRBitmapView bitmap;
        if (mr_assets_large_bitmap(set, session->combat_profile.type,
                                   MR_COMBAT_MONSTER_SIZE_VARIANT,
                                   &bitmap))
            mr_draw_bitmap_at_clipped(game, &bitmap, 225, 112, 0, 0,
                                      MR_RENDER_WIDTH - 1,
                                      MR_RENDER_HEIGHT - 1);
    }
    mr_screen_compose(game);
}

static void mr_reward_present(MRGameRuntime *game) {
    video_present(&game->host->video);
}

static void mr_reward_wait_any_key(MRGameRuntime *game, MRSession *session) {
    /* 2F3C -> C5B0 prints the literal initialized at 020B, reads one
     * nonempty INKEY$ value, and restores SCREEN 1. C5B0 always executes
     * LOCATE 25,10; callers do not choose a prompt position. */
    mr_screen_text(game, 9, 24, "Hit any key", MR_CGA_WHITE);
    mr_reward_present(game);
    (void)mr_read_original_nonempty_key(game, session);
}

static void mr_present_spellbook_reward(MRGameRuntime *game,
                                        MRSession *session,
                                        const MRPostCombatRewards *reward) {
    if (!reward || !reward->spellbook_found) return;
    char line[48];
    mr_reward_clear(game);
    snprintf(line, sizeof(line), "YOU FIND A LEVEL  %d  SPELLBOOK     ",
             reward->spellbook_level);
    mr_screen_text(game, 0, 0, line, MR_CGA_WHITE);
    mr_screen_text(game, 0, 2,
        "   If you have have enough spell points,", MR_CGA_WHITE);
    mr_screen_text(game, 0, 3,
        "you can  see  which  spells you  have by", MR_CGA_WHITE);
    mr_screen_text(game, 0, 4,
        "hitting `C'.  You can find out what they", MR_CGA_WHITE);
    mr_screen_text(game, 0, 5,
        "do at the Wizard's Guild.", MR_CGA_WHITE);
    mr_reward_wait_any_key(game, session);
}

static void mr_present_weapon_reward(MRGameRuntime *game,
                                     MRSession *session,
                                     const MRPostCombatRewards *reward) {
    if (!reward || reward->weapon_kind == MR_EXTRA_REWARD_NONE) return;
    char line[64];
    switch (reward->weapon_kind) {
        case MR_EXTRA_REWARD_ARMOR:
            snprintf(line, sizeof(line), "You find %s",
                     mr_reward_armor_name(reward->weapon_value));
            break;
        case MR_EXTRA_REWARD_SWORD:
            snprintf(line, sizeof(line), "You find a sword.");
            break;
        case MR_EXTRA_REWARD_MACE:
            snprintf(line, sizeof(line), "You find a mace.");
            break;
        default:
            return;
    }
    mr_reward_clear(game);
    mr_screen_text(game, 0, 0, line, MR_CGA_WHITE);
    mr_reward_wait_any_key(game, session);
}

static void mr_present_wand_reward(MRGameRuntime *game,
                                   MRSession *session,
                                   const MRPostCombatRewards *reward) {
    if (!reward || !reward->wand_found) return;
    const char *color = mr_wand_name(reward->wand_color - 1);
    char line[64];
    snprintf(line, sizeof(line), "You have found a %s wand!",
             color ? color : "");
    mr_reward_clear(game);
    mr_screen_text(game, 0, 0, line, MR_CGA_WHITE);
    mr_reward_wait_any_key(game, session);
}

static void mr_present_pill_reward(MRGameRuntime *game,
                                   MRSession *session,
                                   const MRPostCombatRewards *reward) {
    if (!reward || !reward->pill_found) return;
    const char *color = mr_pill_name(reward->pill_color - 1);
    char line[64];
    snprintf(line, sizeof(line), "You have found a %s pill!",
             color ? color : "");
    mr_reward_clear(game);
    mr_screen_text(game, 0, 0, line, MR_CGA_WHITE);
    mr_reward_wait_any_key(game, session);
}

static void mr_reward_generic_line(const MRData *data,
                                   const MRPostCombatRewards *reward,
                                   char *line, size_t line_size) {
    if (!line || !line_size) return;
    line[0] = 0;
    if (!reward) return;
    switch (reward->generic_kind) {
        case MR_EXTRA_REWARD_HEALTH_RING:
            snprintf(line, line_size, " A RING OF HEALTH"); break;
        case MR_EXTRA_REWARD_BAG_OF_HOLDING:
            snprintf(line, line_size, " A BAG OF HOLDING"); break;
        case MR_EXTRA_REWARD_MAGIC_SWORD:
            /* Positive QuickBASIC numeric PRINT contributes one leading and
             * one trailing blank between the initialized strings. */
            snprintf(line, line_size, " A + %d SWORD",
                     reward->generic_enchantment); break;
        case MR_EXTRA_REWARD_MAGIC_MACE:
            snprintf(line, line_size, " A + %d MACE",
                     reward->generic_enchantment); break;
        case MR_EXTRA_REWARD_MAGIC_RING:
            snprintf(line, line_size, " + %d RING",
                     reward->generic_enchantment); break;
        case MR_EXTRA_REWARD_MAGIC_ARMOR:
            snprintf(line, line_size, " + %d FIELD PLATE ARMOR",
                     reward->generic_enchantment); break;
        case MR_EXTRA_REWARD_HOLY_HAND_GRENADE:
            snprintf(line, line_size, " A HOLY HAND GRENADE!"); break;
        case MR_EXTRA_REWARD_FLOOR_SLOSHER:
            snprintf(line, line_size, " A FLOOR SLOSHER"); break;
        case MR_EXTRA_REWARD_CARRIED_ITEM:
            if (data && reward->generic_value >= 1 &&
                reward->generic_value <= 9)
                snprintf(line, line_size, "%s",
                         data->treasure_names[reward->generic_value - 1]);
            else
                snprintf(line, line_size, "NOTHING");
            break;
        case MR_EXTRA_REWARD_ATTRIBUTE_BOOK:
            snprintf(line, line_size, "You have found a book of %s",
                     mr_reward_attribute_name(reward->generic_value));
            break;
        case MR_EXTRA_REWARD_NONE:
        default:
            /* ADD1, AE5D, AEC6, AF3F and AFC1 all converge here for
             * ineligible, weaker, or already-owned rewards. */
            snprintf(line, line_size, "NOTHING"); break;
    }
}

static void mr_present_generic_reward(MRGameRuntime *game,
                                      MRSession *session,
                                      const MRPostCombatRewards *reward) {
    if (!reward || !reward->generic_drop_triggered) return;
    char line[96];

    /* AC87-ACA2: the teaser has its own cleared screen and an unconditional
     * four-TIMER-second hold before the item table is selected/displayed. */
    mr_original_keyboard_drain(game);
    mr_reward_clear(game);
    mr_screen_text(game, 0, 0, "YOU FIND... ", MR_CGA_WHITE);
    mr_reward_present(game);
    mr_screen_wait_milliseconds(game, 4000);

    mr_reward_generic_line(&game->data, reward, line, sizeof(line));
    if (reward->generic_kind == MR_EXTRA_REWARD_ATTRIBUTE_BOOK) {
        /* B06B explicitly LOCATEs the book at BASIC row 20. */
        mr_screen_text(game, 0, 19, line, MR_CGA_WHITE);
        mr_screen_text(game, 0, 20, "   Press any key to read it.",
                       MR_CGA_WHITE);
        mr_reward_present(game);
        (void)mr_read_original_nonempty_key(game, session);
        mr_screen_text(game, 0, 21, "You feel very good.", MR_CGA_WHITE);
    } else
        /* The generic teaser PRINT leaves the cursor on BASIC row 2. */
        mr_screen_text(game, 0, 1, line, MR_CGA_WHITE);
    /* B0DA always drains before the final 2F3C acknowledgement. */
    mr_original_keyboard_drain(game);
    mr_reward_wait_any_key(game, session);
}

typedef struct MRRewardPresentationContext {
    MRGameRuntime *game;
    MRSession *session;
} MRRewardPresentationContext;

static void mr_present_post_combat_reward_stage(
    MRPostCombatRewardStage stage, const MRPostCombatRewards *reward,
    void *context) {
    MRRewardPresentationContext *presentation =
        (MRRewardPresentationContext *)context;
    if (!presentation) return;
    switch (stage) {
        case MR_POST_COMBAT_REWARD_SPELLBOOK:
            mr_present_spellbook_reward(presentation->game,
                                        presentation->session, reward);
            break;
        case MR_POST_COMBAT_REWARD_WEAPON:
            mr_present_weapon_reward(presentation->game,
                                     presentation->session, reward);
            break;
        case MR_POST_COMBAT_REWARD_WAND:
            mr_present_wand_reward(presentation->game,
                                   presentation->session, reward);
            break;
        case MR_POST_COMBAT_REWARD_PILL:
            mr_present_pill_reward(presentation->game,
                                   presentation->session, reward);
            break;
        case MR_POST_COMBAT_REWARD_GENERIC:
            mr_present_generic_reward(presentation->game,
                                      presentation->session, reward);
            break;
    }
}

static void mr_present_coin_rows(MRGameRuntime *game, MRSession *session,
                                 const MRCoinLoot *coins, int *next_row) {
    static const char *const names[] = {
        "COPPER", "SILVER", "IVORY", "GOLD", "PLATINUM", "JEWELS"
    };
    const int values[] = {
        coins->copper, coins->silver, coins->ivory, coins->gold,
        coins->platinum, coins->jewels
    };
    int row = 1;
    mr_reward_coin_frame(game, session);
    mr_screen_text(game, 0, 0, "YOU HAVE FOUND:", MR_CGA_WHITE);
    for (int index = 0; index < 6; index++) {
        if (values[index] <= 0) continue;
        char number[24];
        mr_screen_text(game, 0, row, names[index], MR_CGA_WHITE);
        /* A8B9-A953 uses PRINT label, value. The comma advances to the next
         * fourteen-column print zone and positive numeric PRINT supplies a
         * leading and trailing blank. */
        snprintf(number, sizeof(number), " %d ", values[index]);
        mr_screen_text(game, 14, row++, number, MR_CGA_WHITE);
    }
    if (next_row) *next_row = row;
}

static int mr_session_reward_flow(MRGameRuntime *game, MRSession *session) {
    mr_session_draw_combat_result(game, session, NULL);
    /* A4D8 and A4F9 place these over the retained combat picture. */
    if (!session->suppress_defeat_message)
        mr_screen_text(game, 23, 15, "YOU KILLED IT!!", MR_CGA_WHITE);
    mr_screen_text(game, 25, 16, "HIT RETURN", MR_CGA_WHITE);
    video_present(&game->host->video);
    /* A4F9-A520 drains eighteen INKEY$ polls and then accepts ASCII 13
     * only.  An arbitrary-key acknowledgement changes the reward RNG
     * boundary and lets held combat input leak into the coin prompt. */
    mr_original_keyboard_drain(game);
    for (;;) {
        int key = mr_read_modal_key_with_idle_monsters(game, session);
        if (key == MR_DOS_KEY_IDLE_MONSTER_REACHED_PLAYER)
            return 1;
        if (key == 13) break;
        if (input_poll_quit(&game->host->input)) return 0;
    }
    if (!mr_session_acknowledge_defeat(session)) return 0;
    if (session->phase != MR_SESSION_REWARDS) return 1;
    if (session->coin_loot_pending) {
        int row;
        mr_present_coin_rows(game, session, &session->pending_coins, &row);
        if (session->save.character.carried_weight +
                session->shared_menu_value >= 350.0f) {
            mr_screen_text(game, 0, row,
                           "IT'S TOO HEAVY FOR YOU TO CARRY", MR_CGA_WHITE);
            mr_reward_present(game);
            /* A97C-A97F drains eighteen polls and then reads one nonempty
             * key. It never offers T/L in this branch. */
            mr_original_keyboard_drain(game);
            if (mr_read_modal_key_with_idle_monsters(game, session) ==
                MR_DOS_KEY_IDLE_MONSTER_REACHED_PLAYER)
                return 1;
            mr_session_resolve_coin_loot(session, 0);
        } else {
            mr_screen_text(game, 0, row,
                           "T=TAKE COINS  L=LEAVE COINS", MR_CGA_WHITE);
            mr_reward_present(game);
            /* A997 performs the same fixed debounce before the T/L read. */
            mr_original_keyboard_drain(game);
            for (;;) {
                int key = mr_read_modal_key_with_idle_monsters(game, session);
                if (key == MR_DOS_KEY_IDLE_MONSTER_REACHED_PLAYER)
                    return 1;
                if (key == 't' || key == 'T') {
                    mr_session_resolve_coin_loot(session, 1); break;
                }
                if (key == 'l' || key == 'L') {
                    mr_session_resolve_coin_loot(session, 0); break;
                }
            }
        }
    }
    MRRewardPresentationContext presentation = {game, session};
    if (!mr_session_generate_post_combat_rewards_staged(
            session, mr_present_post_combat_reward_stage, &presentation))
        return 0;
    return mr_session_finish_rewards(session);
}

static void mr_death_monster_name(const MRGameRuntime *game,
                                  const MRSession *session,
                                  char name[40]) {
    const char *selected = session
        ? mr_monster_data_name(game, session->combat_profile.resource_set,
                               session->combat_profile.type)
        : "MONSTER";
    snprintf(name, 40, "%s", selected);
}

static int mr_remove_dead_character(MRGameRuntime *game, int roster_index,
                                    char *error, size_t error_size) {
    if (!game || roster_index < 0 || roster_index >= game->roster.count) {
        mr_game_error(error, error_size,
                      "Invalid dead-character roster position");
        return 0;
    }
    int old_count = game->roster.count;
    MRRoster rewritten = game->roster;
    if (!mr_roster_remove(&rewritten, roster_index)) {
        mr_game_error(error, error_size, "Cannot remove dead character");
        return 0;
    }

    /* DUNSMALL C02D-C101 rewrites F5.COM without the selected name before
     * A249 kills the selected N.EXE/N.BIN pair and shifts every later pair
     * down by one slot. */
    char roster_path[512];
    mr_game_join(roster_path, sizeof(roster_path), game->directory, "F5.COM");
    if (!mr_roster_write(&rewritten, roster_path, error, error_size))
        return 0;

    char target_text[512], target_binary[512];
    mr_begin_character_paths(game->directory, roster_index, target_text,
                             sizeof(target_text), target_binary,
                             sizeof(target_binary));
    if (remove(target_text) != 0 || remove(target_binary) != 0) {
        if (error && error_size)
            snprintf(error, error_size,
                     "Cannot delete dead Revenge character files: %s",
                     strerror(errno));
        return 0;
    }
    for (int source_index = roster_index + 1;
         source_index < old_count; ++source_index) {
        char source_text[512], source_binary[512];
        mr_begin_character_paths(game->directory, source_index,
                                 source_text, sizeof(source_text),
                                 source_binary, sizeof(source_binary));
        mr_begin_character_paths(game->directory, source_index - 1,
                                 target_text, sizeof(target_text),
                                 target_binary, sizeof(target_binary));
        if (rename(source_text, target_text) != 0 ||
            rename(source_binary, target_binary) != 0) {
            if (error && error_size)
                snprintf(error, error_size,
                         "Cannot compact Revenge character files: %s",
                         strerror(errno));
            return 0;
        }
    }
    game->roster = rewritten;
    if (game->roster.count <= 0) game->selected = 0;
    else if (game->selected >= game->roster.count)
        game->selected = game->roster.count - 1;
    return 1;
}

static int mr_record_death_in_hall(MRGameRuntime *game,
                                   const MRHallSummary *summary,
                                   char *error, size_t error_size) {
    char path[512], row[MR_HALL_ROW_COLUMNS + 1];
    MRHallOfFame hall;
    mr_game_join(path, sizeof(path), game->directory, "F9.EXE");
    if (!mr_hall_load(&hall, path, error, error_size)) return 0;
    mr_hall_format_row(summary, row);
    mr_hall_insert(&hall, row);
    /* F8 main draws and waits before it BSAVEs the possibly updated table. */
    mr_f8_draw_hall(game, &hall);
    return mr_hall_write(&hall, path, error, error_size);
}

/* Return one when DUNSMALL resumes this character in town, zero when its
 * permanent-death branch has removed the numbered save and handed control
 * through F8 back to BEGIN. */
static int mr_present_player_death(MRGameRuntime *game, MRSession *session,
                                   int roster_index) {
    char character_name[MR_ROSTER_NAME_BYTES];
    char killer[40];
    snprintf(character_name, sizeof(character_name), "%s",
             game->roster.names[roster_index]);
    mr_death_monster_name(game, session, killer);
    MRHallSummary summary = {
        (int)floor(session->save.character.player_level),
        (int)floor(session->save.character.player_class),
        character_name,
        session->save.character.pocket_money,
        session->save.character.bank_money,
        (int)floor(session->save.character.dungeon_level),
        killer
    };
    if (summary.player_level < 0) summary.player_level = 0;

    mr_play_session_music(game, session, mr_music_death);
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    mr_screen_text(game, 0, 0, "YOU'RE DEAD HA HA HA...", MR_CGA_WHITE);

    MRDeathOutcome outcome = mr_session_resolve_player_death(session);
    /* Every branch continues from the row immediately after the row-one
     * death announcement. B5C8 emits one empty line before Better luck. */
    int row = 1;
    if (outcome == MR_DEATH_REINCARNATED) {
        mr_screen_text(game, 0, row++, "You've been reincarnated.",
                       MR_CGA_WHITE);
    } else if (outcome == MR_DEATH_RAISED ||
               outcome == MR_DEATH_RAISE_FAILED) {
        mr_screen_text(game, 0, row++,
                       "  Someone carries you out and tries to",
                       MR_CGA_WHITE);
        mr_screen_text(game, 0, row++, "raise you from the dead.",
                       MR_CGA_WHITE);
        if (outcome == MR_DEATH_RAISE_FAILED)
            mr_screen_text(game, 0, row++, "  The raise doesn't work.",
                           MR_CGA_WHITE);
    }

    if (outcome == MR_DEATH_REINCARNATED || outcome == MR_DEATH_RAISED) {
        mr_screen_text(game, 0, 23, "Hit any key", MR_CGA_MAGENTA);
        video_present(&game->host->video);
        (void)mr_read_original_nonempty_key(game, session);
        /* A209/A156 reach 2F3C first.  Its raw-key tail clamps all six
         * attributes; only then do A20C/A159 call 1CF5.  This ordering matters
         * when a successful raise subtracts Health from one to zero. */
        mr_recompute_town_entry_factors(&session->save.character);
        return 1;
    }

    char error[512] = {0};
    int removed = mr_remove_dead_character(game, roster_index, error,
                                           sizeof(error));
    if (removed && !mr_world_write_monsters(&session->world,
                                             game->directory, error,
                                             sizeof(error)))
        removed = 0;
    mr_screen_text(game, 0, row + 1, "   Better luck next time!",
                   MR_CGA_WHITE);
    mr_screen_text(game, 0, 23, "Hit any key", MR_CGA_MAGENTA);
    video_present(&game->host->video);
    /* A23A adds the fixed eighteen-poll drain only on permanent removal. */
    mr_original_keyboard_drain(game);
    (void)mr_read_original_nonempty_key(game, session);

    if (!removed) {
        mr_screen_message(game, "CANNOT REMOVE DEAD CHARACTER", error);
        return 0;
    }
    if (!mr_record_death_in_hall(game, &summary, error, sizeof(error))) {
        mr_screen_message(game, "CANNOT UPDATE HALL OF FAME", error);
        return 0;
    }
    return 0;
}

static int mr_session_run(MRGameRuntime *game, int roster_index) {
    char text_path[512], binary_path[512], error[512] = {0};
    mr_begin_character_paths(game->directory, roster_index, text_path,
                             sizeof(text_path), binary_path,
                             sizeof(binary_path));
    MRSession session;
    MRRng startup_rng;
    /* DUNSMALL 01F9-0202 executes RANDOMIZE TIMER before any RND call.
     * BRUN30's $RZ1 overwrites only state bytes one through three and
     * preserves byte zero.  The runtime has already initialized the stream
     * to its default 050000h state, so the native stack object must receive
     * that state before applying the TIMER mix.  Leaving it uninitialized
     * made the preserved byte (and therefore every later review, fountain,
     * movement, combat, and loot roll) depend on arbitrary stack contents. */
    mr_rng_seed_default(&startup_rng);
    mr_rng_seed_timer(&startup_rng, mr_qbasic_timer_seconds());
    if (!mr_session_load(&session, game->directory, text_path, binary_path,
                         startup_rng.state, error, sizeof(error))) {
        mr_screen_message(game, "CANNOT LOAD CHARACTER", error);
        return 0;
    }
    /* The four GET buffers belong to one DUNSMALL process/session. Never let
     * a same-coordinate character reuse the preceding character's panes. */
    game->viewport_cache.valid = 0;
    game->dungeon_text_color_cycle = 0;
    session.sound_disabled = mr_prompt_session_sound(game, &session) ? 0 : 1;
    mr_show_loading_review(game, &session);
    /* B674's B98F-B9B2 epilogue occurs only after BA10 has consumed the
     * default review color and review-file selection rolls. Keep that exact
     * RNG boundary even though the native decoder loaded the records earlier. */
    mr_session_apply_original_load_epilogue(&session);
    /* 056B loads the character while the review is visible. The native
     * session is already decoded, so continue with the exact post-load
     * SCREEN 1 acknowledgement rather than waiting on the review page. */
    mr_screen_clear(&game->screen, MR_CGA_BLACK);
    mr_screen_compose(game);
    mr_screen_text(game, 0, 0, "HIT ANY KEY        ", MR_CGA_WHITE);
    video_present(&game->host->video);
    (void)mr_read_original_nonempty_key(game, &session);
    /* 059A is the final boundary before initialize_floor_and_redraw. */
    mr_original_keyboard_drain(game);
    const char *message = NULL;
    int combat_input_debounced = 0;
    int monster_blocks_way_notice = 0;
    while (!input_poll_quit(&game->host->input) &&
           session.phase != MR_SESSION_RETURN_TO_ROSTER) {
        /* 08F6/49B1 observe occupancy outside 7EEC. A preparation selector
         * reached before the first encounter may return a sampled key (or a
         * substituted blank) with B578 set but without entering combat; on
         * the next outer pass the current cell owns the transition. */
        if (session.phase == MR_SESSION_EXPLORING) {
            int player_x = (int)floor(session.save.character.player_x);
            int player_y = (int)floor(session.save.character.player_y);
            int occupant = mr_session_monster_at(
                &session, player_x, player_y);
            if (occupant > 0)
                (void)mr_session_begin_combat(&session, occupant);
        }
        if (session.phase == MR_SESSION_DEAD) {
            if (!mr_present_player_death(game, &session, roster_index))
                return 1;
            message = NULL;
            combat_input_debounced = 0;
            continue;
        }
        if (session.phase == MR_SESSION_DEFEAT_NOTICE) {
            if (!mr_session_reward_flow(game, &session)) break;
            message = NULL;
            continue;
        }
        if (session.phase == MR_SESSION_COMBAT) {
            if (!combat_input_debounced) {
                MRSessionCycleResult entry_cycle;
                mr_session_begin_combat_entry_status_cycle(
                    &session, &entry_cycle);
                if (entry_cycle.disease_triggered) {
                    /* The source warning is emitted by 3FFC before the map
                     * renderer transfers ownership to 803A. Temporarily
                     * render the pre-combat pane composition for that hold;
                     * do not run the separate ambient-advice roll. */
                    MRSessionPhase combat_phase = session.phase;
                    session.phase = session.save.character.dungeon_level == 0.0f
                        ? MR_SESSION_TOWN : MR_SESSION_EXPLORING;
                    mr_session_draw(
                        game, &session,
                        "You feel sick. You need a cure disease. ");
                    mr_screen_wait_milliseconds(game, 4000);
                    session.phase = combat_phase;
                }
            }
            /* DUNSMALL calls 7F43 at combat setup (845A) and again before
             * presenting the active combat commands (8517). It both clears
             * expired TIMER-backed effects and exposes the still-active
             * ones. The state routine existed previously but was never
             * called by the live bridge, leaving agility/shield effects
             * permanent. */
            MRTimedEffectResult expired_effects;
            mr_session_expire_combat_effects(
                &session, mr_qbasic_timer_seconds(), &expired_effects);
            if (!combat_input_debounced) {
                /* Combat entry calls the fixed keyboard drain at 8077 before
                 * accepting the first command. */
                mr_original_keyboard_drain(game);
                combat_input_debounced = 1;
            }
        } else {
            combat_input_debounced = 0;
        }
        MRSessionCycleResult cycle;
        memset(&cycle, 0, sizeof(cycle));
        cycle.disease_attribute = -1;
        if (session.phase != MR_SESSION_COMBAT) {
            /* DUNSMALL's exploration/status cycle at 3FFC is not part of
             * the separate combat loop at 803A+.  In particular, combat
             * prompts must not advance disease, regenerate health rings,
             * or consume the ambient-advice RND. */
            mr_session_begin_command_cycle(&session, &cycle);
        }
        if (cycle.disease_triggered) {
            /* 40D6-40EB holds this exact warning for four TIMER seconds. */
            mr_session_draw(
                game, &session,
                "You feel sick. You need a cure disease. ");
            mr_screen_wait_milliseconds(game, 4000);
            message = NULL;
        }
        MRTownLocation town_location = MR_TOWN_LOCATION_NONE;
        if (session.phase == MR_SESSION_TOWN) {
            town_location = mr_town_location_at(
                (int)floor(session.save.character.player_x),
                (int)floor(session.save.character.player_y));
        }
        int at_fountain = mr_session_is_at_fountain(&session);
        const char *draw_message = message;
        if (!draw_message)
            draw_message = mr_ambient_advice_text(cycle.ambient_advice);
        if (!draw_message && town_location != MR_TOWN_LOCATION_NONE)
            draw_message = "There's a rope above. Hit U to climb it.";
        mr_session_draw(game, &session, draw_message);
        if (town_location != MR_TOWN_LOCATION_NONE) {
            /* show_rope_prompt_and_climb 12EE-1323 prints a second visual
             * cue after the top-left sentence: LOCATE 16,29, PRINT "ROPE".
             * This sits in the lower-right pane, independently of the S/B/T/
             * I/W glyph already captured into the map cell. */
            mr_screen_text(game, 28, 15, "ROPE", MR_CGA_WHITE);
            video_present(&game->host->video);
        }
        if (monster_blocks_way_notice) {
            /* 33EA-3408: COLOR 6, LOCATE 6,22, PRINT the exact notice. */
            mr_screen_text(game, 21, 5, "MONSTER BLOCKS WAY",
                           mr_basic_text_color(6));
            video_present(&game->host->video);
        }
        /* 4100-416B runs before the fountain and lookahead branches. */
        mr_apply_configured_delay(&session);
        if (at_fountain) {
            /* handle_fountain_of_youth, 3D83-3DAD, debounces held input and
             * overlays these two lines on every command cycle at its cell.
             * The preceding draw_status_and_map fountain arm at 41A3 also
             * consumes one INKEY$ and jumps through 4260, resetting B5FA,
             * before 3D83 performs its separate eighteen-poll drain. */
            input_drain_pending_polls(&game->host->input, 1);
            session.consecutive_arrow_commands = 0;
            mr_original_keyboard_drain(game);
            mr_screen_text(
                game, 0, 3,
                "You have found the fountain of youth.",
                MR_CGA_WHITE);
            mr_screen_text(game, 0, 4, "   Hit `D' to drink.",
                           MR_CGA_WHITE);
            video_present(&game->host->video);
        } else {
            mr_consume_status_input_lookahead(game, &session);
        }
        message = NULL;
        monster_blocks_way_notice = 0;
        int scan = 0;
        int key = mr_read_dos_key_with_idle_monsters(game, &session, &scan);
        if (key == MR_DOS_KEY_IDLE_MONSTER_REACHED_PLAYER) {
            /* 08F6-091E checks the player's occupancy immediately after the
             * empty-key update and enters combat before accepting a key. */
            combat_input_debounced = 0;
            continue;
        }
        if (session.phase == MR_SESSION_COMBAT) {
            /* 871F calls the normal movement routine before dispatching H,
             * S, M, K, P, F, C, I, T, W or B.  A successful step can break
             * contact; turns and blocked steps leave combat active. */
            if (key == 0 && (scan == MR_ARROW_UP || scan == MR_ARROW_DOWN ||
                             scan == MR_ARROW_LEFT ||
                             scan == MR_ARROW_RIGHT)) {
                MRSessionCycleResult movement_cycle;
                MRSessionStepResult step =
                    mr_session_arrow_with_cycle(
                        &session, (MRArrowKey)scan, &movement_cycle);
                if (movement_cycle.disease_triggered) {
                    /* Every non-blocked combat arrow reaches 3FFC from the
                     * ordinary movement routine before 871F resumes combat.
                     * Its disease warning is held at that exact boundary. */
                    mr_session_draw(
                        game, &session,
                        "You feel sick. You need a cure disease. ");
                    mr_screen_wait_milliseconds(game, 4000);
                }
                if (step == MR_SESSION_STEP_MONSTER)
                    monster_blocks_way_notice = 1;
                else if (step == MR_SESSION_STEP_CHUTE)
                    mr_present_chute_notice(game);
                else if (step == MR_SESSION_STEP_MOVED &&
                         session.phase == MR_SESSION_COMBAT) {
                    MRCombatSeparationResult separation;
                    if (mr_session_resolve_combat_separation(
                            &session, &separation) &&
                        separation.code ==
                            MR_COMBAT_SEPARATION_REENGAGED_AND_ATTACKED)
                        mr_present_monster_turn(game, &session,
                                                &separation.monster);
                }
                if (!mr_session_flush_or_report(game, roster_index,
                                                &session))
                    return 0;
                continue;
            }
            /* 8752-8791 compares the active monster's coordinates with the
             * player's after the arrow dispatcher. Equal coordinates then
             * compare the reset DS:B51C scratch against one. B51C is zero
             * at 8707, so the ordinary path falls through to the player's
             * H/S/M/K/P/F/C/I/T/W/B command dispatcher. Equality at 878C is
             * the exceptional B51C==1 path; treating simple contact as that
             * branch discards every attack key and lets the monster attack
             * forever. */
            MRSessionCombatResult result;
            MRPlayerAttackCode attack = 0;
            MRCombatCommand combat_command =
                mr_input_decode_combat_command(key);
            if (combat_command == MR_COMBAT_COMMAND_SWORD)
                attack = MR_PLAYER_ATTACK_SWORD;
            else if (combat_command == MR_COMBAT_COMMAND_MACE)
                attack = MR_PLAYER_ATTACK_MACE;
            else if (combat_command == MR_COMBAT_COMMAND_KNIFE)
                attack = MR_PLAYER_ATTACK_KNIFE;
            else if (combat_command == MR_COMBAT_COMMAND_FISTS)
                attack = MR_PLAYER_ATTACK_FIST;
            else if (combat_command == MR_COMBAT_COMMAND_BREATHE_FIRE)
                attack = session.save.character.persistent_values[
                             MR_PERSISTENT_BREATHE_FIRE_EXPIRY] >
                             mr_qbasic_timer_seconds()
                    ? MR_PLAYER_ATTACK_BREATHE_FIRE : 0;
            if (attack && !mr_player_owns_combat_weapon(
                              &session.save.character, attack)) {
                mr_present_missing_combat_weapon(game, &session);
                /* Preserve the rejection while the next combat command is
                 * awaited; the original loop does not repaint it away before
                 * reading another INKEY$ value. */
                message = "YOU DO NOT HAVE THAT";
            } else if (attack && mr_session_player_attack_only(
                              &session, attack, &result)) {
                char damage_line[80];
                const char *phrase = "";
                if (game->data_loaded &&
                    result.player.combat_phrase_index >= 0 &&
                    result.player.combat_phrase_index < MR_COMBAT_MESSAGES)
                    phrase = game->data.combat_messages[
                        result.player.combat_phrase_index];
                /* 8D00-8D35 clears exactly BASIC rows 11 and 10 across the
                 * first 25 columns. Miss/hit prose is then printed on row 11
                 * and the numeric hit report is explicitly LOCATEd to row
                 * 10. It does not replace the row-one attack announcement. */
                mr_session_draw_combat_result(game, &session, NULL);
                mr_screen_text(game, 0, 9,
                               "                         ", MR_CGA_WHITE);
                mr_screen_text(game, 0, 10,
                               "                         ", MR_CGA_WHITE);
                if (phrase[0])
                    mr_screen_text(game, 0, 10, phrase, MR_CGA_WHITE);
                if (result.player.damage > 0) {
                    snprintf(damage_line, sizeof(damage_line),
                             result.player.damage == 1
                                 ? "YOU DID 1 POINT."
                                 : "YOU DID %d POINTS.",
                             result.player.damage);
                    mr_screen_text(game, 0, 9, damage_line,
                                   MR_CGA_MAGENTA);
                }
                video_present(&game->host->video);
                /* 8E12-8E34 holds every weapon result, including a zero
                 * damage result, for one TIMER second. */
                mr_screen_wait_milliseconds(game, 1000);
                if (session.phase == MR_SESSION_COMBAT &&
                    result.player.monster_takes_turn) {
                    MRMonsterTurnResult monster;
                    if (mr_session_resolve_monster_turn(&session, &monster))
                        mr_present_monster_turn(game, &session, &monster);
                }
                message = NULL;
            } else if (combat_command == MR_COMBAT_COMMAND_CAST) {
                int spell = mr_session_select_spell(
                    game, &session, MR_SPELLBOOK_BATTLE);
                if (spell >= 0) {
                    MRSessionBattleSpellResult spell_result;
                    if (mr_session_cast_battle_spell(
                            &session, (MRBattleSpell)spell, &spell_result)) {
                        mr_present_battle_spell_cast(
                            game, &session, (MRBattleSpell)spell,
                            &spell_result);
                        message = NULL;
                    }
                }
            } else if (combat_command == MR_COMBAT_COMMAND_USE_ITEM) {
                int item = mr_session_select_item(game, &session, 1, 0);
                if (item >= 0) {
                    MRSessionItemResult item_result;
                    if (mr_session_use_battle_item(
                            &session, (MRBattleItem)item,
                            mr_qbasic_timer_seconds(), &item_result)) {
                        /* 98FD-9A2C returns directly to the combat prompt for
                         * ordinary potions.  The three timed effects repaint
                         * their fixed notices; Health prints the initialized
                         * mixed-case `You feel very good.' at BASIC row 18.
                         * The grenade prints its explosion on that same row
                         * before entering the ordinary reward pipeline. */
                        message = NULL;
                        if (item == MR_BATTLE_ITEM_SPEED_POTION ||
                            item == MR_BATTLE_ITEM_FIRE_POTION ||
                            item == MR_BATTLE_ITEM_SHIELDING_POTION) {
                            mr_session_draw_combat_result(game, &session, NULL);
                        } else if (item == MR_BATTLE_ITEM_HEALTH_POTION) {
                            mr_session_draw_combat_result(game, &session, NULL);
                            mr_screen_text(game, 0, 17,
                                           "You feel very good.",
                                           MR_CGA_WHITE);
                            video_present(&game->host->video);
                        } else if (item ==
                                   MR_BATTLE_ITEM_HOLY_HAND_GRENADE) {
                            mr_session_draw_combat_result(game, &session, NULL);
                            mr_screen_text(game, 0, 17,
                                           "THERE'S AN EXPLOSION",
                                           MR_CGA_WHITE);
                            video_present(&game->host->video);
                        }
                    }
                }
            } else if (combat_command == MR_COMBAT_COMMAND_USE_WAND) {
                int wand = mr_session_select_wand(game, &session);
                if (wand >= 0) {
                    MRSessionItemResult item_result;
                    if (mr_session_use_wand(&session, (MRWand)wand,
                                            &item_result)) {
                        static char item_message[80];
                        const char *wand_message = mr_item_result_message(
                            &item_result.item, item_message,
                            sizeof(item_message));
                        if (wand == MR_WAND_YELLOW ||
                            wand == MR_WAND_GREEN || wand == MR_WAND_RED) {
                            /* 8926-896A enters the ordinary Strength,
                             * Lightning, or Explosion spell branch, including
                             * its two-second result hold and monster response. */
                            mr_session_draw_combat_result(
                                game, &session,
                                wand_message && wand_message[0]
                                    ? wand_message : NULL);
                            mr_screen_wait_milliseconds(game, 2000);
                            if (item_result.monster_attacked)
                                mr_present_monster_turn(
                                    game, &session, &item_result.monster);
                            message = NULL;
                        } else message = NULL;
                    }
                }
            } else if (combat_command == MR_COMBAT_COMMAND_TAKE_PILL) {
                int pill = mr_session_select_pill(game, &session);
                if (pill >= 0) {
                    MRItemUseResult item_result;
                    if (mr_session_use_pill(&session, (MRPill)pill,
                                            &item_result)) {
                        /* 7D34-7DC8 mutates, clamps and returns without a
                         * success banner. */
                        message = NULL;
                    }
                }
            } else if (combat_command == MR_COMBAT_COMMAND_HELP) {
                mr_show_help(game, &session, 8);
            } else if (combat_command == MR_COMBAT_COMMAND_PAUSE) {
                if (mr_pause_game(game, &session))
                    session.phase = MR_SESSION_RETURN_TO_ROSTER;
            }
            /* B308 is an immediate disk boundary, not deferred autosave.
             * This flushes the 9E92 level-drain snapshot (including monster
             * responses entered through spells, items, and wands) before the
             * next command is accepted. */
            if (!mr_session_flush_or_report(game, roster_index, &session))
                return 0;
            continue;
        }
        if (key == 0 && (scan == MR_ARROW_UP || scan == MR_ARROW_DOWN ||
                         scan == MR_ARROW_LEFT || scan == MR_ARROW_RIGHT)) {
            MRSessionCycleResult movement_cycle;
            MRSessionStepResult result =
                mr_session_arrow_with_cycle(
                    &session, (MRArrowKey)scan, &movement_cycle);
            if (movement_cycle.disease_triggered) {
                /* 30D9-33E9 calls 3FFC before 09E8 advances a tracked
                 * monster or accepts another key. */
                mr_session_draw(
                    game, &session,
                    "You feel sick. You need a cure disease. ");
                mr_screen_wait_milliseconds(game, 4000);
            }
            if (result == MR_SESSION_STEP_MONSTER)
                monster_blocks_way_notice = 1;
            else if (result == MR_SESSION_STEP_CHUTE)
                mr_present_chute_notice(game);
            if (!mr_session_flush_or_report(game, roster_index, &session))
                return 0;
            continue;
        }
        int upper = mr_input_original_uppercase(key);
        MRMainCommand command = mr_input_decode_main_command(upper);
        if (command == MR_COMMAND_QUIT_TO_CHARACTER_MENU) break;
        if (command == MR_COMMAND_VIEW_STATISTICS) {
            mr_show_player_statistics(game, &session, roster_index);
        } else if (command == MR_COMMAND_MAGIC_ITEM_INVENTORY) {
            mr_show_magic_item_inventory(game, &session);
        } else if (command == MR_COMMAND_DROP_CARRIED_TREASURE) {
            mr_offer_drop_all_carried_treasure(game, &session, roster_index);
        } else if (command == MR_COMMAND_HELP) {
            mr_show_help(game, &session, 1);
        } else if (command == MR_COMMAND_PAUSE) {
            if (mr_pause_game(game, &session))
                session.phase = MR_SESSION_RETURN_TO_ROSTER;
        } else if (command == MR_COMMAND_SET_DELAY) {
            session.configured_action_delay = mr_prompt_action_delay(
                game, session.configured_action_delay);
        } else if (command == MR_COMMAND_TOGGLE_SOUND) {
            session.sound_disabled = (session.sound_disabled + 1) % 2;
            /* 1077-10BA prints the state, holds it for exactly two TIMER
             * seconds, then erases the nine-character field. */
            mr_session_draw(game, &session,
                session.sound_disabled ? "SOUND OFF" : "SOUND ON");
            mr_screen_wait_milliseconds(game, 2000);
            message = NULL;
        } else if (command == MR_COMMAND_NEXT_PRIMARY_COLOR) {
            session.primary_color++;
            /* 1003-1027 applies the incremented value before wrapping it. */
            session.effective_background_color = session.primary_color;
            session.effective_palette_selector = session.secondary_color;
            if (session.primary_color > 16) session.primary_color = 0;
        } else if (command == MR_COMMAND_NEXT_SECONDARY_COLOR) {
            session.secondary_color++;
            /* 1038-1049 likewise applies value four before storing two. */
            session.effective_background_color = session.primary_color;
            session.effective_palette_selector = session.secondary_color;
            if (session.secondary_color >= 4) session.secondary_color = 2;
        } else if (command == MR_COMMAND_CAST_PREPARATION_SPELL) {
            int spell = mr_session_select_spell(
                game, &session, MR_SPELLBOOK_PREPARATION);
            if (spell >= 0) {
                MRPreparationCastResult result;
                if (mr_session_cast_preparation_spell(
                        &session, (MRPreparationSpell)spell, &result)) {
                    static char spell_message[80];
                    message = mr_preparation_cast_message(
                        &session, spell, &result, spell_message,
                        sizeof(spell_message));
                    mr_present_preparation_spell_cast(
                        game, &session, roster_index,
                        (MRPreparationSpell)spell, &result);
                    if (!message || !message[0]) message = NULL;
                }
            }
        } else if (command == MR_COMMAND_USE_PREPARATION_ITEM) {
            int item = mr_session_select_item(game, &session, 0, 0);
            if (item >= 0) {
                MRItemUseResult item_result;
                if (mr_session_use_preparation_item(
                        &session, (MRPreparationItem)item, &item_result)) {
                    message = item_result.floor_slosher_too_deep
                        ? "DOESN'T WORK THIS DEEP" : NULL;
                    if (item == MR_PREP_ITEM_SEEING_SCROLL &&
                        item_result.used) {
                        /* 17A2 calls the full dungeon redraw immediately. */
                        mr_session_draw(game, &session, NULL);
                    } else if ((item == MR_PREP_ITEM_HEALING_SCROLL ||
                                item == MR_PREP_ITEM_SPELL_POINT_SCROLL) &&
                               item_result.used) {
                        /* 17C0 and 17F2 tail-call show_player_statistics. */
                        mr_show_player_statistics(
                            game, &session, roster_index);
                        message = NULL;
                    }
                    if (item == MR_PREP_ITEM_FLOOR_SLOSHER &&
                        item_result.used &&
                        !item_result.floor_slosher_too_deep) {
                        /* 18AF-18D8 clears the display, announces the fall,
                         * waits for one raw key, then reveals the new floor. */
                        mr_screen_clear(&game->screen, MR_CGA_BLACK);
                        mr_screen_compose(game);
                        mr_screen_text(
                            game, 0, 0,
                            "YOU ARE SLIPPING THROUGH THE FLOOR.",
                            MR_CGA_WHITE);
                        mr_screen_text(game, 9, 24, "Hit any key",
                                       MR_CGA_WHITE);
                        video_present(&game->host->video);
                        (void)mr_read_original_nonempty_key(game, &session);
                        message = NULL;
                    }
                }
            }
        } else if (command == MR_COMMAND_USE_WAND) {
            int wand = mr_session_select_wand(game, &session);
            if (wand >= 0) {
                MRSessionItemResult item_result;
                if (mr_session_use_wand(&session, (MRWand)wand,
                                        &item_result)) {
                    if ((wand == MR_WAND_YELLOW ||
                         wand == MR_WAND_GREEN || wand == MR_WAND_RED) &&
                        item_result.item.no_effect) {
                        /* Main-loop choices six through eight converge on
                         * NO EFFECT + wait_two_seconds at 0FCC-0FD8. */
                        mr_session_draw(game, &session, "NO EFFECT");
                        mr_screen_wait_milliseconds(game, 2000);
                        message = NULL;
                    } else message = NULL;
                }
            }
        } else if (command == MR_COMMAND_TAKE_PILL) {
            int pill = mr_session_select_pill(game, &session);
            if (pill >= 0) {
                MRItemUseResult item_result;
                if (mr_session_use_pill(&session, (MRPill)pill,
                                        &item_result)) {
                    message = NULL;
                }
            }
        } else if (command == MR_COMMAND_ASCEND_OR_USE_ROPE) {
            if (session.phase == MR_SESSION_TOWN &&
                town_location != MR_TOWN_LOCATION_NONE) {
                mr_visit_town_location(game, &session, town_location);
                message = NULL;
            } else {
                /* 0DE0-0E12 silently changes floor only when the signed
                 * ladder feature matches; invalid U is also silent. */
                (void)mr_session_traverse(&session, 1);
                message = NULL;
            }
        } else if (command == MR_COMMAND_DESCEND_OR_DRINK) {
            if (mr_session_is_at_fountain(&session)) {
                MRFountainResult result;
                /* 3DAE-3DC3 presents the reaction and waits four seconds
                 * before any restart state is changed. */
                mr_screen_clear(&game->screen, MR_CGA_BLACK);
                mr_screen_compose(game);
                mr_screen_text(game, 0, 0, "YOU FEEL STRANGE...",
                               MR_CGA_WHITE);
                video_present(&game->host->video);
                mr_screen_wait_milliseconds(game, 4000);
                if (mr_session_drink_fountain(&session, &result))
                    message = NULL;
                if (!mr_session_flush_or_report(game, roster_index,
                                                &session))
                    return 0;
            } else {
                /* 0E13-0E45 has the same silent behavior for D. */
                (void)mr_session_traverse(&session, 0);
                message = NULL;
            }
        } else if (key == 0x1B) {
            mr_input_toggle_movement_mode(&session.input);
            /* 10BE toggles the mode flag without printing a status line. */
            message = NULL;
        }
    }
    mr_session_persist(game, roster_index, &session);
    return 1;
}

int mr_game_run(Game *host, const char *directory) {
    if (!host || !directory) return 0;
    MRGameRuntime game;
    memset(&game, 0, sizeof(game));
    game.host = host;
    game.resource_directory = directory;
    memcpy(game.saved_palette, host->video.palette, sizeof(game.saved_palette));
    mr_game_set_revenge_palette(&game);

    char path[512], error[512] = {0};
    if (!mr_game_seed_save_directory(&game, directory, error,
                                     sizeof(error))) {
        mr_screen_message(&game, "CANNOT INITIALIZE REVENGE SAVES", error);
        memcpy(host->video.palette, game.saved_palette,
               sizeof(game.saved_palette));
        host->video.dirty = 1;
        return 0;
    }
    int setup_state = 0;
    if (!mr_game_read_name_state(&game, &setup_state, error,
                                 sizeof(error))) {
        mr_screen_message(&game, "CANNOT READ REVENGE SETUP STATE", error);
        memcpy(host->video.palette, game.saved_palette,
               sizeof(game.saved_palette));
        host->video.dirty = 1;
        return 0;
    }
    game.color_enabled = setup_state != 0;
    mr_game_join(path, sizeof(path), game.directory, "F5.COM");
    if (!mr_render_layout_load(&game.layout, game.resource_directory, error,
                               sizeof(error)) ||
        !mr_roster_load(&game.roster, path, error, sizeof(error))) {
        mr_screen_message(&game, "MORAFF'S REVENGE FILES ARE MISSING", error);
        memcpy(host->video.palette, game.saved_palette,
               sizeof(game.saved_palette));
        host->video.dirty = 1;
        return 0;
    }
    game.data_loaded = mr_data_load(&game.data, game.resource_directory,
                                    error, sizeof(error));
    if (!game.data_loaded) {
        mr_screen_message(&game, "MORAFF'S REVENGE FILES ARE MISSING", error);
        memcpy(host->video.palette, game.saved_palette,
               sizeof(game.saved_palette));
        host->video.dirty = 1;
        return 0;
    }
    game.assets_loaded = mr_assets_load(&game.assets,
                                        game.resource_directory, error,
                                        sizeof(error));
    if (!game.assets_loaded) {
        mr_data_free(&game.data);
        mr_screen_message(&game, "MORAFF'S REVENGE FILES ARE MISSING", error);
        memcpy(host->video.palette, game.saved_palette,
               sizeof(game.saved_palette));
        host->video.dirty = 1;
        return 0;
    }
    for (int review = 1; review <= 6; review++) {
        char name[16];
        snprintf(name, sizeof(name), "REVIEW.%d", review);
        mr_game_join(path, sizeof(path), game.resource_directory, name);
        if (!mr_game_file_exists(path)) {
            snprintf(error, sizeof(error),
                     "Missing original Revenge review resource: %s", path);
            mr_data_free(&game.data);
            mr_screen_message(&game,
                              "MORAFF'S REVENGE FILES ARE MISSING", error);
            memcpy(host->video.palette, game.saved_palette,
                   sizeof(game.saved_palette));
            host->video.dirty = 1;
            return 0;
        }
    }

    if (setup_state == 10)
        mr_begin_first_run_setup(&game);

    int running = 1;
    while (running && !input_poll_quit(&host->input)) {
        mr_begin_draw_menu(&game);
        MRBeginMenuOption option = 0;
        while (!option && !input_poll_quit(&host->input))
            option = mr_begin_decode_menu_key(
                mr_begin_wait_upper_key(&game));
        if (!option) break;
        /* BEGIN writes the current color flag to NAME before each CHAIN or
         * exit arm. Native dispatch remains in-process but preserves the
         * same on-disk state transition. */
        if (!mr_game_write_name_state(&game, game.color_enabled,
                                      error, sizeof(error))) {
            mr_screen_message(&game, "CANNOT SAVE REVENGE SETUP STATE",
                              error);
            break;
        }
        if (option == MR_BEGIN_CREATE_CHARACTER)
            mr_begin_create_character(&game);
        else if (option == MR_BEGIN_PLAY_GAME) {
            int selected = mr_begin_select_character(&game);
            if (selected >= 0) mr_session_run(&game, selected);
            else if (selected == -2) running = 0;
        } else if (option == MR_BEGIN_HALL_OF_FAME)
            mr_begin_draw_hall(&game);
        else if (option == MR_BEGIN_RETURN_TO_WORLD)
            running = 0;
        else if (option == MR_BEGIN_ORDER_ADVANCED)
            mr_ncd_show_ordering_information(&game);
    }
    if (game.data_loaded) mr_data_free(&game.data);
    memcpy(host->video.palette, game.saved_palette, sizeof(game.saved_palette));
    host->video.dirty = 1;
    input_drain_pending(&host->input);
    return 1;
}

int mr_game_self_test(const char *directory, char *error,
                      size_t error_size) {
    if (!mr_game_replace_file_self_test(error, error_size)) return 0;
    if (!mr_bios_font_self_test(error, error_size)) return 0;
    if (!mw_audio_qbasic_mml_self_test()) {
        mr_game_error(error, error_size,
                      "QuickBASIC PLAY/MML transcription mismatch");
        return 0;
    }
    if (MR_RENDER_WIDTH != 320 || MR_RENDER_HEIGHT != 200) {
        mr_game_error(error, error_size,
                      "Revenge native screen is not SCREEN 1 sized");
        return 0;
    }
    {
        Game *host = (Game *)calloc(1, sizeof(*host));
        MRGameRuntime *runtime =
            (MRGameRuntime *)calloc(1, sizeof(*runtime));
        if (!host || !runtime) {
            free(runtime);
            free(host);
            mr_game_error(error, error_size,
                          "Cannot allocate Revenge partial-redraw fixture");
            return 0;
        }
        runtime->host = host;
        memset(runtime->screen.pixels, 1, sizeof(runtime->screen.pixels));
        memset(host->video.pixels, 9, sizeof(host->video.pixels));
        mr_screen_compose_source_rect(runtime, 10, 20, 10, 20);
        int inside_x = 32, inside_y = 77;
        int outside_x = 31, outside_y = 77;
        int partial_failed =
            host->video.pixels[inside_y * LOGICAL_W + inside_x] != 1 ||
            host->video.pixels[outside_y * LOGICAL_W + outside_x] != 9;
        free(runtime);
        free(host);
        if (partial_failed) {
            mr_game_error(error, error_size,
                          "Revenge incremental viewport compose mismatch");
            return 0;
        }
    }
    {
        MRViewportCache cache;
        memset(&cache, 0, sizeof(cache));
        cache.valid = 1;
        cache.facing = MR_DIRECTION_WEST;
        if (mr_cached_viewport_rotation(&cache, MR_DIRECTION_WEST) != 0.0f ||
            mr_cached_viewport_rotation(&cache, MR_DIRECTION_NORTH) != 3.0f ||
            mr_cached_viewport_rotation(&cache, MR_DIRECTION_EAST) != 2.0f ||
            mr_cached_viewport_rotation(&cache, MR_DIRECTION_SOUTH) != 1.0f) {
            mr_game_error(error, error_size,
                          "Revenge cached-view shared rotation mismatch");
            return 0;
        }
    }
    if (!mr_original_combat_contact_dispatches_player_command(0.0f) ||
        mr_original_combat_contact_dispatches_player_command(1.0f)) {
        mr_game_error(error, error_size,
                      "Revenge combat overlap branch direction mismatch");
        return 0;
    }
    {
        MRSession notice_session;
        memset(&notice_session, 0, sizeof(notice_session));
        notice_session.last_chute_landing_x = 8;
        notice_session.last_chute_landing_y = 9;
        notice_session.last_chute_landing_level = 12;
        if (!mr_session_used_false_floor_notice(
                &notice_session, 12, 8, 9, MR_VERTICAL_NONE) ||
            !mr_session_used_false_floor_notice(
                &notice_session, 13, 8, 9, MR_VERTICAL_NONE) ||
            mr_session_used_false_floor_notice(
                &notice_session, 14, 8, 9, MR_VERTICAL_NONE) ||
            mr_session_used_false_floor_notice(
                &notice_session, 12, 7, 9, MR_VERTICAL_NONE) ||
            mr_session_used_false_floor_notice(
                &notice_session, 70, 8, 9, MR_VERTICAL_NONE) ||
            mr_session_used_false_floor_notice(
                &notice_session, 12, 8, 9, MR_VERTICAL_CHUTE)) {
            mr_game_error(error, error_size,
                          "Revenge used-false-floor notice gate mismatch");
            return 0;
        }

        Game *notice_host = (Game *)calloc(1, sizeof(*notice_host));
        MRGameRuntime *notice_runtime =
            (MRGameRuntime *)calloc(1, sizeof(*notice_runtime));
        if (!notice_host || !notice_runtime) {
            free(notice_runtime);
            free(notice_host);
            mr_game_error(error, error_size,
                          "Cannot allocate Revenge vertical-notice fixture");
            return 0;
        }
        notice_runtime->host = notice_host;
        mr_session_draw_view_direction_labels(notice_runtime);
        int notice_failed =
            !mr_screen_text_matches_zero_background(
                notice_runtime, 27, 6, "FRONT", MR_CGA_WHITE) ||
            !mr_screen_text_matches_zero_background(
                notice_runtime, 21, 19, "LEFT", MR_CGA_WHITE) ||
            !mr_screen_text_matches_zero_background(
                notice_runtime, 35, 19, "RIGHT", MR_CGA_WHITE) ||
            !mr_screen_text_matches_zero_background(
                notice_runtime, 28, 24, "BACK", MR_CGA_WHITE);
        memset(notice_host->video.pixels, 0,
               sizeof(notice_host->video.pixels));
        mr_session_draw_vertical_notice(notice_runtime, &notice_session,
                                        12, 8, 9);
        notice_failed |=
            !mr_screen_text_matches_zero_background(
                notice_runtime, 21, 24, "   False floor.   ", MR_CGA_WHITE) ||
            !mr_screen_text_matches_zero_background(
                notice_runtime, 28, 14, "D-GO", MR_CGA_WHITE) ||
            !mr_screen_text_matches_zero_background(
                notice_runtime, 28, 15, "DOWN", MR_CGA_WHITE);

        memset(notice_host->video.pixels, 0,
               sizeof(notice_host->video.pixels));
        memset(&notice_session, 0, sizeof(notice_session));
        notice_session.world.special_cells[2][2] = 1U << 19;
        mr_session_draw_vertical_notice(notice_runtime, &notice_session,
                                        2, 1, 2);
        notice_failed |=
            !mr_screen_text_matches_zero_background(
                notice_runtime, 21, 24, " Ladder going ", MR_CGA_WHITE) ||
            !mr_screen_text_matches_zero_background(
                notice_runtime, 35, 24, "up.  ", MR_CGA_WHITE) ||
            !mr_screen_text_matches_zero_background(
                notice_runtime, 28, 14, "U-GO", MR_CGA_WHITE) ||
            !mr_screen_text_matches_zero_background(
                notice_runtime, 28, 15, " UP ", MR_CGA_WHITE);

        memset(notice_host->video.pixels, 0,
               sizeof(notice_host->video.pixels));
        if (!mr_world_load(&notice_session.world, directory, error,
                           error_size)) {
            free(notice_runtime);
            free(notice_host);
            return 0;
        }
        mr_session_draw_vertical_notice(notice_runtime, &notice_session,
                                        1, 14, 1);
        notice_failed |=
            !mr_screen_text_matches_zero_background(
                notice_runtime, 21, 24, " Ladder going ", MR_CGA_WHITE) ||
            !mr_screen_text_matches_zero_background(
                notice_runtime, 35, 24, "down.", MR_CGA_WHITE) ||
            !mr_screen_text_matches_zero_background(
                notice_runtime, 28, 14, "D-GO", MR_CGA_WHITE) ||
            !mr_screen_text_matches_zero_background(
                notice_runtime, 28, 15, "DOWN", MR_CGA_WHITE);
        free(notice_runtime);
        free(notice_host);
        if (notice_failed) {
            mr_game_error(error, error_size,
                          "Revenge vertical-feature notice raster mismatch");
            return 0;
        }
    }
    if (mr_modal_contact_input_policy(0, 1, 'F') !=
            MR_MODAL_CONTACT_KEEP_SAMPLE ||
        mr_modal_contact_input_policy(1, 0, 'F') !=
            MR_MODAL_CONTACT_KEEP_SAMPLE ||
        mr_modal_contact_input_policy(1, 0, -1) !=
            MR_MODAL_CONTACT_SUBSTITUTE_BLANK ||
        mr_modal_contact_input_policy(1, 1, 'F') !=
            MR_MODAL_CONTACT_READ_FRESH_KEY ||
        mr_modal_contact_input_policy(1, 1, -1) !=
            MR_MODAL_CONTACT_READ_FRESH_KEY) {
        mr_game_error(error, error_size,
                      "Revenge modal combat-contact input mismatch");
        return 0;
    }
    if (mr_main_input_poll_order(MR_SESSION_TOWN, 0) !=
            MR_MAIN_INPUT_NO_IDLE_POLL ||
        mr_main_input_poll_order(MR_SESSION_EXPLORING, 0) !=
            MR_MAIN_INPUT_NO_IDLE_POLL ||
        mr_main_input_poll_order(MR_SESSION_EXPLORING, 1) !=
            MR_MAIN_INPUT_SAMPLE_THEN_POLL ||
        mr_main_input_poll_order(MR_SESSION_COMBAT, 1) !=
            MR_MAIN_INPUT_POLL_THEN_SAMPLE) {
        mr_game_error(error, error_size,
                      "Revenge main input/monster-poll ordering mismatch");
        return 0;
    }
    {
        MRSession delay_session;
        memset(&delay_session, 0, sizeof(delay_session));
        delay_session.phase = MR_SESSION_EXPLORING;
        delay_session.save.character.dungeon_level = 1;
        delay_session.save.character.player_x = 10;
        delay_session.save.character.player_y = 10;
        if (mr_should_apply_configured_delay(&delay_session)) {
            mr_game_error(error, error_size,
                          "Revenge delay ran on an unrevealed cell");
            return 0;
        }
        delay_session.save.explored[1][10] = 1U << 10;
        delay_session.configured_action_delay = 3000;
        for (int arrow = 0; arrow < 4; arrow++) {
            if (!mr_should_apply_configured_delay(&delay_session)) {
                mr_game_error(error, error_size,
                              "Revenge initial arrow-delay gate mismatch");
                return 0;
            }
            mr_record_status_lookahead_for_delay(
                &delay_session, 0, MR_ARROW_UP);
            if (arrow < 3 &&
                mr_configured_delay_iteration_count(&delay_session) !=
                    500 * (arrow + 1)) {
                mr_game_error(error, error_size,
                              "Revenge configured-delay loop bound mismatch");
                return 0;
            }
        }
        if (mr_should_apply_configured_delay(&delay_session)) {
            mr_game_error(error, error_size,
                          "Revenge held-arrow acceleration mismatch");
            return 0;
        }
        mr_record_status_lookahead_for_delay(&delay_session, 'V', 0);
        if (delay_session.consecutive_arrow_commands != 0 ||
            !mr_should_apply_configured_delay(&delay_session)) {
            mr_game_error(error, error_size,
                          "Revenge non-arrow delay reset mismatch");
            return 0;
        }
        delay_session.phase = MR_SESSION_COMBAT;
        if (mr_should_apply_configured_delay(&delay_session)) {
            mr_game_error(error, error_size,
                          "Revenge combat delay gate mismatch");
            return 0;
        }
    }
    {
        MRCharacter player;
        memset(&player, 0, sizeof(player));
        player.persistent_values[MR_PERSISTENT_KNIFE_OWNED] = 1;
        if (!mr_player_owns_combat_weapon(&player,
                                          MR_PLAYER_ATTACK_KNIFE) ||
            !mr_player_owns_combat_weapon(&player,
                                          MR_PLAYER_ATTACK_FIST) ||
            mr_player_owns_combat_weapon(&player,
                                          MR_PLAYER_ATTACK_SWORD) ||
            mr_player_owns_combat_weapon(&player,
                                          MR_PLAYER_ATTACK_MACE)) {
            mr_game_error(error, error_size,
                          "Revenge combat weapon ownership mismatch");
            return 0;
        }
        player.persistent_values[MR_PERSISTENT_SWORD_OWNED] = 1;
        player.persistent_values[MR_PERSISTENT_MACE_OWNED] = 1;
        if (!mr_player_owns_combat_weapon(&player,
                                          MR_PLAYER_ATTACK_SWORD) ||
            !mr_player_owns_combat_weapon(&player,
                                          MR_PLAYER_ATTACK_MACE)) {
            mr_game_error(error, error_size,
                          "Revenge owned combat weapon mismatch");
            return 0;
        }
    }
    {
        int palette[4];
        mr_screen_one_palette_indices(0, 2, palette);
        if (palette[0] != 0 || palette[1] != 2 || palette[2] != 4 ||
            palette[3] != 6) {
            mr_game_error(error, error_size,
                          "Revenge initial SCREEN 1 palette mismatch");
            return 0;
        }
        mr_screen_one_palette_indices(11, 1, palette);
        if (palette[0] != 11 || palette[1] != 11 || palette[2] != 13 ||
            palette[3] != 15) {
            mr_game_error(error, error_size,
                          "Revenge intense SCREEN 1 palette mismatch");
            return 0;
        }
        mr_screen_one_palette_indices(16, 2, palette);
        if (palette[0] != 0 || palette[1] != 2 || palette[2] != 4 ||
            palette[3] != 6) {
            mr_game_error(error, error_size,
                          "Revenge SCREEN 1 COLOR masking mismatch");
            return 0;
        }
    }
    {
        char line[40];
        mr_format_qb_color_inventory_line(
            line, sizeof(line), 1, "PURPLE", 5.0);
        if (strcmp(line, " 1 PURPLE 5  ") != 0) {
            mr_game_error(error, error_size,
                          "Revenge wand PRINT spacing mismatch");
            return 0;
        }
        mr_format_qb_color_inventory_line(
            line, sizeof(line), 6, "WHITE", 0.0);
        if (strcmp(line, " 6 --------------- ") != 0) {
            mr_game_error(error, error_size,
                          "Revenge empty pill/wand line mismatch");
            return 0;
        }
    }
    {
        char line[96];
        mr_format_qb_using_integer_field(
            line, sizeof(line), "Strength:    ### ", 7.0);
        if (strcmp(line, "Strength:      7 ") != 0) {
            mr_game_error(error, error_size,
                          "Revenge attribute PRINT USING mismatch");
            return 0;
        }
        mr_format_qb_using_integer_field(
            line, sizeof(line), "Laziness:    ### ", 148.0);
        if (strcmp(line, "Laziness:    148 ") != 0) {
            mr_game_error(error, error_size,
                          "Revenge Laziness label/field mismatch");
            return 0;
        }
        mr_format_qb_using_integer_field(
            line, sizeof(line), "Health:      ### ", 1234.0);
        if (strcmp(line, "Health:      %1234 ") != 0) {
            mr_game_error(error, error_size,
                          "Revenge PRINT USING overflow mismatch");
            return 0;
        }
        mr_format_qb_leading_stat(
            line, sizeof(line), 42.0, "Spell points");
        if (strcmp(line, "           42 Spell points") != 0) {
            mr_game_error(error, error_size,
                          "Revenge leading numeric stat mismatch");
            return 0;
        }
    }
    {
        int available[MR_PREP_ITEM_COUNT] = {1, 0, 0, 0, 0, 0};
        if (mr_item_selection_from_key('1', available, 0, 0) != 0 ||
            mr_item_selection_from_key('2', available, 0, 0) !=
                MR_ITEM_SELECTION_LEAVE ||
            mr_item_selection_from_key('X', available, 0, 1) !=
                MR_ITEM_SELECTION_LEAVE ||
            mr_item_selection_from_key('2', available, 1, 0) !=
                MR_ITEM_SELECTION_RETRY ||
            mr_item_selection_from_key('l', available, 1, 0) !=
                MR_ITEM_SELECTION_RETRY ||
            mr_item_selection_from_key('L', available, 1, 0) !=
                MR_ITEM_SELECTION_LEAVE ||
            mr_item_selection_from_key('l', available, 1, 1) !=
                MR_ITEM_SELECTION_LEAVE) {
            mr_game_error(error, error_size,
                          "Revenge item-selector return rules mismatch");
            return 0;
        }
    }
    if (!strstr(mr_ncd_order_form,
                "Price:  US............ 10 US Dollars") ||
        !strstr(mr_ncd_order_form,
                "Comments (greatly appreciated):")) {
        mr_game_error(error, error_size,
                      "NCD printable order-form transcription mismatch");
        return 0;
    }
    {
        static const uint8_t setup[] = "  10\r\n\x1a";
        static const uint8_t enabled[] = "1\r\n\x1a";
        static const uint8_t invalid[] = "1.5\r\n\x1a";
        int value = -1;
        if (!mr_name_parse_first_numeric(setup, sizeof(setup) - 1, &value) ||
            value != 10 ||
            !mr_name_parse_first_numeric(enabled, sizeof(enabled) - 1,
                                         &value) ||
            value != 1 ||
            mr_name_parse_first_numeric(invalid, sizeof(invalid) - 1,
                                        &value)) {
            mr_game_error(error, error_size,
                          "BEGIN NAME setup-state parsing mismatch");
            return 0;
        }
        MRGameRuntime color_game;
        memset(&color_game, 0, sizeof(color_game));
        color_game.color_enabled = 1;
        if (mr_begin_advance_text_color(&color_game) !=
                mr_basic_text_color(9) ||
            mr_begin_advance_text_color(&color_game) !=
                mr_basic_text_color(10)) {
            mr_game_error(error, error_size,
                          "BEGIN text-color cycle mismatch");
            return 0;
        }
        color_game.text_color_cycle = 6;
        if (mr_begin_advance_text_color(&color_game) !=
                mr_basic_text_color(8)) {
            mr_game_error(error, error_size,
                          "BEGIN text-color wrap mismatch");
            return 0;
        }
        color_game.color_enabled = 0;
        if (mr_begin_advance_text_color(&color_game) !=
                mr_basic_text_color(7)) {
            mr_game_error(error, error_size,
                          "BEGIN monochrome text-color mismatch");
            return 0;
        }
        MRGameRuntime ncd_game;
        memset(&ncd_game, 0, sizeof(ncd_game));
        ncd_game.color_enabled = 1;
        int ncd_next = 9;
        for (int expected = 9; expected <= 14; ++expected) {
            if (mr_ncd_advance_color(&ncd_game, &ncd_next) !=
                    mr_basic_text_color(expected)) {
                mr_game_error(error, error_size,
                              "NCD text-color sequence mismatch");
                return 0;
            }
        }
        if (ncd_next != 9 ||
            mr_ncd_advance_color(&ncd_game, &ncd_next) !=
                mr_basic_text_color(9)) {
            mr_game_error(error, error_size,
                          "NCD text-color wrap mismatch");
            return 0;
        }
        ncd_game.color_enabled = 0;
        if (mr_ncd_advance_color(&ncd_game, &ncd_next) !=
                mr_basic_text_color(7)) {
            mr_game_error(error, error_size,
                          "NCD monochrome text-color mismatch");
            return 0;
        }
        char roster_name[39];
        mr_begin_roster_display_name(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789LONG", roster_name);
        if (strlen(roster_name) != 38 ||
            strcmp(roster_name + 35, "...") != 0) {
            mr_game_error(error, error_size,
                          "BEGIN 35-character roster truncation mismatch");
            return 0;
        }
        MRGameRuntime character_game;
        int character_cycle = 0;
        memset(&character_game, 0, sizeof(character_game));
        character_game.color_enabled = 1;
        if (mr_character_advance_text_color(&character_game,
                                            &character_cycle) !=
                mr_basic_text_color(10)) {
            mr_game_error(error, error_size,
                          "CHCHAR first text-color cycle mismatch");
            return 0;
        }
        character_cycle = 6;
        if (mr_character_advance_text_color(&character_game,
                                            &character_cycle) !=
                mr_basic_text_color(9)) {
            mr_game_error(error, error_size,
                          "CHCHAR text-color wrap mismatch");
            return 0;
        }
        character_game.color_enabled = 0;
        if (mr_character_advance_text_color(&character_game,
                                            &character_cycle) !=
                mr_basic_text_color(15)) {
            mr_game_error(error, error_size,
                          "CHCHAR monochrome text-color mismatch");
            return 0;
        }
        char normalized[] = "az{|}~[";
        mr_character_normalize_name(normalized);
        static const unsigned char expected_name[] = {
            'A', 'Z', '[', '\\', ']', '^', '[', 0
        };
        if (memcmp(normalized, expected_name, sizeof(expected_name)) != 0) {
            mr_game_error(error, error_size,
                          "CHCHAR name-normalization mismatch");
            return 0;
        }
    }
    MRNativeScreen screen;
    mr_screen_clear(&screen, 0);
    mr_screen_box(&screen, 214, 57, 53, 54, 1);
    if (screen.pixels[57 * MR_RENDER_WIDTH + 214] != 1 ||
        screen.pixels[110 * MR_RENDER_WIDTH + 266] != 1 ||
        screen.pixels[100 * MR_RENDER_WIDTH + 240] != 0) {
        mr_game_error(error, error_size,
                      "Revenge 320x200 composition primitive mismatch");
        return 0;
    }
    {
        MRData data;
        MRGameRuntime monster_game;
        MRPostCombatRewards reward;
        char line[96];
        memset(&data, 0, sizeof(data));
        memset(&monster_game, 0, sizeof(monster_game));
        memset(&reward, 0, sizeof(reward));
        snprintf(data.inventory_labels[3], sizeof(data.inventory_labels[3]),
                 "INVENTORY LABEL MUST NOT PRINT");
        snprintf(data.treasure_names[3], sizeof(data.treasure_names[3]),
                 "TEST TREASURE NAME");

        reward.generic_kind = MR_EXTRA_REWARD_MAGIC_SWORD;
        reward.generic_enchantment = 7;
        mr_reward_generic_line(&data, &reward, line, sizeof(line));
        if (strcmp(line, " A + 7 SWORD") != 0) {
            mr_game_error(error, error_size,
                          "Revenge magic-reward text mismatch");
            return 0;
        }
        reward.generic_kind = MR_EXTRA_REWARD_CARRIED_ITEM;
        reward.generic_value = 4;
        mr_reward_generic_line(&data, &reward, line, sizeof(line));
        if (strcmp(line, "TEST TREASURE NAME") != 0) {
            mr_game_error(error, error_size,
                          "Revenge carried-item reward text mismatch");
            return 0;
        }
        reward.generic_kind = MR_EXTRA_REWARD_ATTRIBUTE_BOOK;
        reward.generic_value = 4;
        mr_reward_generic_line(&data, &reward, line, sizeof(line));
        if (strcmp(line, "You have found a book of health.") != 0) {
            mr_game_error(error, error_size,
                          "Revenge attribute-book reward text mismatch");
            return 0;
        }
        reward.generic_kind = MR_EXTRA_REWARD_NONE;
        mr_reward_generic_line(&data, &reward, line, sizeof(line));
        if (strcmp(line, "NOTHING") != 0) {
            mr_game_error(error, error_size,
                          "Revenge empty-reward text mismatch");
            return 0;
        }
        snprintf(data.monster_names[0], sizeof(data.monster_names[0]),
                 "SHALLOW ONE");
        snprintf(data.monster_names[22], sizeof(data.monster_names[22]),
                 "DEEP ONE");
        monster_game.data = data;
        monster_game.data_loaded = 1;
        if (strcmp(mr_monster_data_name(&monster_game, 1, 1),
                   "SHALLOW ONE") != 0 ||
            strcmp(mr_monster_data_name(&monster_game, 2, 1),
                   "DEEP ONE") != 0) {
            mr_game_error(error, error_size,
                          "Revenge monster resource-set name mismatch");
            return 0;
        }
    }
    {
        char line[96];
        int color_index = 0;
        if (!mr_decode_review_line("4Now loading:\r\n", line,
                                   sizeof(line), &color_index) ||
            color_index != 3 || strcmp(line, "Now loading:") != 0 ||
            !mr_decode_review_line("you are enjoying| our\n", line,
                                   sizeof(line), &color_index) ||
            color_index != -1 ||
            strcmp(line, "you are enjoying, our") != 0) {
            mr_game_error(error, error_size,
                          "Revenge REVIEW text decoder mismatch");
            return 0;
        }
        MRGameRuntime dungeon_game;
        MRSession color_session;
        MRRng expected_rng;
        memset(&dungeon_game, 0, sizeof(dungeon_game));
        memset(&color_session, 0, sizeof(color_session));
        dungeon_game.color_enabled = 1;
        color_session.rng.state = 0x050000u;
        expected_rng = color_session.rng;
        int expected_index =
            (int)floor(mr_rng_next(&expected_rng) * 6.0);
        uint8_t selected = mr_select_random_dungeon_text_color(
            &dungeon_game, &color_session);
        if (selected != mr_basic_text_color(9 + expected_index) ||
            color_session.rng.state != expected_rng.state ||
            mr_dungeon_text_color(&dungeon_game, 6) !=
                mr_basic_text_color(15)) {
            mr_game_error(error, error_size,
                          "Revenge random text-color/RND boundary mismatch");
            return 0;
        }
        dungeon_game.color_enabled = 0;
        if (mr_dungeon_text_color(&dungeon_game, 0) !=
                mr_basic_text_color(15) ||
            mr_dungeon_text_color(&dungeon_game, 6) !=
                mr_basic_text_color(15)) {
            mr_game_error(error, error_size,
                          "Revenge monochrome text-color table mismatch");
            return 0;
        }
    }
    {
        char expanded[64];
        MRGameRuntime help_game;
        mr_help_expand_tabs("\t\t       fighting", expanded,
                            sizeof(expanded));
        if (strlen(expanded) != 31 ||
            strncmp(expanded, "                       ", 23) != 0 ||
            strcmp(expanded + 23, "fighting") != 0) {
            mr_game_error(error, error_size,
                          "Revenge help-file tab expansion mismatch");
            return 0;
        }
        memset(&help_game, 0, sizeof(help_game));
        help_game.color_enabled = 0;
        if (mr_help_advance_text_color(&help_game) !=
                mr_basic_text_color(15)) {
            mr_game_error(error, error_size,
                          "Revenge monochrome help-color cycle mismatch");
            return 0;
        }
        help_game.color_enabled = 1;
        help_game.dungeon_text_color_cycle = 0;
        if (mr_help_advance_text_color(&help_game) !=
                mr_basic_text_color(10)) {
            mr_game_error(error, error_size,
                          "Revenge color help-line attribute mismatch");
            return 0;
        }
    }
    {
        MRSessionBattleSpellResult result;
        char line[80];
        memset(&result, 0, sizeof(result));
        result.spell.damage = 37;
        if (strcmp(mr_battle_cast_message(MR_BATTLE_MAGIC_BOLT, &result,
                                          line, sizeof(line)),
                   "YOU DO 37 POINTS") != 0 ||
            !mr_battle_cast_has_two_second_hold(MR_BATTLE_MAGIC_BOLT,
                                                 &result.spell)) {
            mr_game_error(error, error_size,
                          "Revenge battle-spell damage staging mismatch");
            return 0;
        }
        memset(&result, 0, sizeof(result));
        result.spell.rewarded_defeat = 1;
        if (strcmp(mr_battle_cast_message(MR_BATTLE_GO_AWAY, &result,
                                          line, sizeof(line)),
                   "IT'S GONE") != 0 ||
            mr_battle_cast_has_two_second_hold(MR_BATTLE_GO_AWAY,
                                                &result.spell)) {
            mr_game_error(error, error_size,
                          "Revenge GO AWAY presentation mismatch");
            return 0;
        }
        memset(&result, 0, sizeof(result));
        result.spell.random_outcome = 5;
        if (strcmp(mr_battle_cast_message(MR_BATTLE_GOD, &result,
                                          line, sizeof(line)),
                   "UH OH...") != 0 ||
            !mr_battle_cast_has_two_second_hold(MR_BATTLE_GOD,
                                                 &result.spell)) {
            mr_game_error(error, error_size,
                          "Revenge God spell presentation mismatch");
            return 0;
        }
    }
    {
        MRSession session;
        MRPreparationCastResult result;
        MRItemUseResult item;
        char line[80];
        memset(&session, 0, sizeof(session));
        memset(&result, 0, sizeof(result));
        result.random_outcome = 5;
        if (strcmp(mr_preparation_cast_message(
                       &session, MR_PREP_MOCCIOLO, &result,
                       line, sizeof(line)), "UH OH...") != 0) {
            mr_game_error(error, error_size,
                          "Revenge Mocciolo presentation mismatch");
            return 0;
        }
        result.random_outcome = 6;
        if (mr_preparation_cast_message(
                &session, MR_PREP_MOCCIOLO, &result,
                line, sizeof(line))[0] != '\0') {
            mr_game_error(error, error_size,
                          "Revenge timed Mocciolo staging mismatch");
            return 0;
        }
        for (int index = 0; index < MR_SAVE_ATTRIBUTES; ++index)
            session.save.character.attributes[index] = index - 2.0;
        mr_clamp_character_attributes(&session.save.character);
        if (session.save.character.attributes[0] != 1.0 ||
            session.save.character.attributes[5] != 3.0) {
            mr_game_error(error, error_size,
                          "Revenge acknowledged attribute clamp mismatch");
            return 0;
        }
        memset(&item, 0, sizeof(item));
        item.floor_slosher_too_deep = 1;
        if (strcmp(mr_item_result_message(&item, line, sizeof(line)),
                   "DOESN'T WORK THIS DEEP") != 0) {
            mr_game_error(error, error_size,
                          "Revenge floor-slosher result mismatch");
            return 0;
        }
        memset(&item, 0, sizeof(item));
        item.used = 1;
        item.map_revealed = 1;
        if (mr_item_result_message(&item, line, sizeof(line))[0] != '\0') {
            mr_game_error(error, error_size,
                          "Revenge silent utility-item result mismatch");
            return 0;
        }
        int available[MR_PREP_ITEM_COUNT] = {0};
        if (mr_item_selection_from_key(0x1B, available, 0, 0) !=
                MR_ITEM_SELECTION_LEAVE ||
            mr_item_selection_from_key('l', available, 0, 0) !=
                MR_ITEM_SELECTION_LEAVE ||
            mr_item_selection_from_key('1', available, 0, 0) !=
                MR_ITEM_SELECTION_LEAVE ||
            mr_item_selection_from_key('l', available, 1, 0) !=
                MR_ITEM_SELECTION_RETRY ||
            mr_item_selection_from_key('L', available, 1, 0) !=
                MR_ITEM_SELECTION_LEAVE ||
            mr_item_selection_from_key('l', available, 1, 1) !=
                MR_ITEM_SELECTION_LEAVE) {
            mr_game_error(error, error_size,
                          "Revenge item-menu mode/case mismatch");
            return 0;
        }
        available[3] = 1;
        if (mr_item_selection_from_key('4', available, 0, 0) != 3) {
            mr_game_error(error, error_size,
                          "Revenge item-menu selection mismatch");
            return 0;
        }
    }
    char roster_path[512], text_path[512], binary_path[512];
    mr_game_join(roster_path, sizeof(roster_path), directory, "F5.COM");
    MRRoster roster;
    if (!mr_roster_load(&roster, roster_path, error, error_size)) return 0;
    mr_begin_character_paths(directory, 0, text_path, sizeof(text_path),
                             binary_path, sizeof(binary_path));
    MRSession session;
    if (!mr_session_load(&session, directory, text_path, binary_path,
                         0x050000u, error, error_size)) return 0;
    if (roster.count < 1 || session.phase != MR_SESSION_TOWN ||
        mr_begin_decode_menu_key('2') != MR_BEGIN_PLAY_GAME) {
        mr_game_error(error, error_size,
                      "BEGIN-to-DUNSMALL native transition mismatch");
        return 0;
    }
    return 1;
}
