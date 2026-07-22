#include "mw_audio.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct Tone {
    int hz;
    int ms;
    int volume;
} Tone;

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
