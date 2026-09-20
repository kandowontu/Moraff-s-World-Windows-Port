#include "mw_audio.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct Tone {
    int hz;
    int ms;
    int volume;
} Tone;

typedef struct QBasicMmlState {
    int tempo;
    int octave;
    int default_length;
    int articulation; /* 8=normal 7/8, 4=legato 4/4, 3=staccato 3/4. */
} QBasicMmlState;

static void queue_phrase(MwAudio *audio, const Tone *tones, int count) {
    if (!audio || !audio->available || !audio->enabled || count <= 0) return;

    int total = 0;
    for (int i = 0; i < count; i++)
        total += audio->spec.freq * tones[i].ms / 1000;
    if (total <= 0) return;

    Sint16 *samples = (Sint16 *)malloc((size_t)total * sizeof(*samples));
    if (!samples) return;
    int at = 0;
    double phase = 0.0;
    for (int t = 0; t < count; t++) {
        int n = audio->spec.freq * tones[t].ms / 1000;
        double step = tones[t].hz > 0 ?
                      (double)tones[t].hz / (double)audio->spec.freq : 0.0;
        for (int i = 0; i < n; i++) {
            int edge = audio->spec.freq / 250; /* short click-free envelope */
            int envelope = 256;
            if (i < edge) envelope = i * 256 / (edge ? edge : 1);
            if (n - i < edge) envelope = (n - i) * 256 / (edge ? edge : 1);
            int value = tones[t].hz > 0 ?
                        ((phase < 0.5 ? 1 : -1) * tones[t].volume * envelope / 256) : 0;
            samples[at++] = (Sint16)value;
            phase += step;
            phase -= floor(phase);
        }
    }

    /* Do not allow repeated movement cues to build a seconds-long backlog. */
    if (SDL_GetQueuedAudioSize(audio->device) >
        (Uint32)(audio->spec.freq * sizeof(Sint16) / 2))
        SDL_ClearQueuedAudio(audio->device);
    SDL_QueueAudio(audio->device, samples, (Uint32)total * sizeof(*samples));
    SDL_PauseAudioDevice(audio->device, 0);
    free(samples);
}

static int mml_number(const char **cursor, int fallback) {
    int value = 0, digits = 0;
    while (isdigit((unsigned char)**cursor)) {
        value = value * 10 + (**cursor - '0');
        (*cursor)++;
        digits++;
    }
    return digits ? value : fallback;
}

static int mml_duration_ms(QBasicMmlState *state, const char **cursor) {
    int denominator = mml_number(cursor, state->default_length);
    if (denominator < 1) denominator = state->default_length;
    int duration = 240000 / state->tempo / denominator;
    if (**cursor == '.') {
        duration += duration / 2;
        (*cursor)++;
    }
    return duration > 0 ? duration : 1;
}

static int mml_append(Tone *tones, int capacity, int *count,
                      int hz, int milliseconds) {
    if (milliseconds <= 0) return 1;
    if (*count >= capacity) return 0;
    tones[*count].hz = hz;
    tones[*count].ms = milliseconds;
    tones[*count].volume = hz ? 2600 : 0;
    (*count)++;
    return 1;
}

static int qbasic_mml_parse(const char *mml, Tone *tones, int capacity,
                            int *tone_count, int *total_ms) {
    QBasicMmlState state = {120, 4, 4, 8};
    const char *cursor = mml;
    int count = 0, total = 0;
    if (!mml || !tones || capacity < 1) return 0;
    while (*cursor) {
        int command = toupper((unsigned char)*cursor++);
        if (isspace(command) || command == ',') continue;
        if (command == 'T') {
            state.tempo = mml_number(&cursor, state.tempo);
            if (state.tempo < 32) state.tempo = 32;
            if (state.tempo > 255) state.tempo = 255;
            continue;
        }
        if (command == 'O') {
            state.octave = mml_number(&cursor, state.octave);
            if (state.octave < 0) state.octave = 0;
            if (state.octave > 6) state.octave = 6;
            continue;
        }
        if (command == 'L') {
            state.default_length = mml_number(&cursor,
                                              state.default_length);
            if (state.default_length < 1) state.default_length = 4;
            continue;
        }
        if (command == 'M') {
            int mode = toupper((unsigned char)*cursor);
            if (mode) cursor++;
            if (mode == 'L') state.articulation = 4;
            else if (mode == 'S') state.articulation = 3;
            else if (mode == 'N') state.articulation = 8;
            /* MB/MF select background/foreground scheduling. Native audio is
             * already queued asynchronously, so neither changes notes. */
            continue;
        }
        if (command == '<') {
            if (state.octave > 0) state.octave--;
            continue;
        }
        if (command == '>') {
            if (state.octave < 6) state.octave++;
            continue;
        }
        if (command == 'P' || command == 'R') {
            int duration = mml_duration_ms(&state, &cursor);
            if (!mml_append(tones, capacity, &count, 0, duration)) return 0;
            total += duration;
            continue;
        }
        int semitone;
        switch (command) {
            case 'C': semitone = 0; break;
            case 'D': semitone = 2; break;
            case 'E': semitone = 4; break;
            case 'F': semitone = 5; break;
            case 'G': semitone = 7; break;
            case 'A': semitone = 9; break;
            case 'B': semitone = 11; break;
            default:
                /* X is BRUN30's string-expression command and is removed by
                 * the native caller, which passes the resolved sequence. */
                continue;
        }
        if (*cursor == '+' || *cursor == '#') {
            semitone++;
            cursor++;
        } else if (*cursor == '-') {
            semitone--;
            cursor++;
        }
        int duration = mml_duration_ms(&state, &cursor);
        int sounding = duration;
        if (state.articulation == 8) sounding = duration * 7 / 8;
        else if (state.articulation == 3) sounding = duration * 3 / 4;
        int midi = (state.octave + 1) * 12 + semitone;
        int hz = (int)lround(440.0 * pow(2.0, (midi - 69) / 12.0));
        if (!mml_append(tones, capacity, &count, hz, sounding)) return 0;
        if (duration > sounding &&
            !mml_append(tones, capacity, &count, 0, duration - sounding))
            return 0;
        total += duration;
    }
    if (tone_count) *tone_count = count;
    if (total_ms) *total_ms = total;
    return count > 0;
}

int mw_audio_init(MwAudio *audio) {
    if (!audio) return -1;
    memset(audio, 0, sizeof(*audio));
    audio->enabled = 1;

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = 22050;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    audio->device = SDL_OpenAudioDevice(NULL, 0, &want, &audio->spec,
                                        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!audio->device) return -1;
    audio->available = 1;
    return 0;
}

void mw_audio_shutdown(MwAudio *audio) {
    if (!audio) return;
    if (audio->device) SDL_CloseAudioDevice(audio->device);
    memset(audio, 0, sizeof(*audio));
}

void mw_audio_set_enabled(MwAudio *audio, int enabled) {
    if (!audio) return;
    audio->enabled = enabled != 0;
    if (!audio->enabled && audio->device) SDL_ClearQueuedAudio(audio->device);
}

void mw_audio_play(MwAudio *audio, MwSoundEffect effect) {
    static const Tone phrases[][4] = {
        {{880,24,2800},{0,10,0}},                                      /* UI */
        {{110,18,1700},{82,16,1300}},                                  /* step */
        {{92,70,3000},{72,90,2600}},                                   /* blocked */
        {{196,32,2500},{262,45,2500}},                                 /* door */
        {{330,45,2700},{440,45,2700},{587,75,2700}},                    /* ladder */
        {{330,45,3200},{220,55,3200},{147,80,3000}},                    /* fall */
        {{740,25,3400},{185,50,3000}},                                 /* attack */
        {{123,70,3600},{92,75,3200}},                                  /* hurt */
        {{330,35,2800},{494,35,2800},{740,75,3000}},                    /* magic */
        {{988,25,2500},{1319,45,2500}},                                /* coin */
        {{392,55,3000},{523,55,3000},{659,55,3000},{784,120,3200}},     /* victory */
        {{70,140,3200},{0,20,0}}                                       /* error */
    };
    static const int lengths[] = {2,2,2,2,3,3,2,2,3,2,4,2};
    if ((unsigned)effect >= sizeof(lengths) / sizeof(lengths[0])) return;
    queue_phrase(audio, phrases[effect], lengths[effect]);
}

void mw_audio_play_qbasic_mml(MwAudio *audio, const char *mml) {
    Tone tones[256];
    int count = 0;
    if (!qbasic_mml_parse(mml, tones,
                          (int)(sizeof(tones) / sizeof(tones[0])),
                          &count, NULL))
        return;
    queue_phrase(audio, tones, count);
}

int mw_audio_qbasic_mml_self_test(void) {
    Tone tones[16];
    int count = 0, total = 0;
    if (!qbasic_mml_parse("T120O4MNL4C.D8", tones, 16,
                          &count, &total))
        return 0;
    /* A dotted quarter is 750 ms and an eighth is 250 ms. Normal mode emits
     * a sounding segment and its 1/8 articulation rest for each note. */
    return count == 4 && total == 1000 &&
           tones[0].hz == 262 && tones[0].ms == 656 &&
           tones[1].hz == 0 && tones[1].ms == 94;
}
