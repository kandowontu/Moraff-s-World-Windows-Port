#ifndef MW_AUDIO_H
#define MW_AUDIO_H

#include <SDL.h>

/* The DOS program drives a PC-speaker style sound channel.  The native port
 * keeps the same deliberately terse one-voice character by queueing square
 * wave phrases rather than substituting modern sampled effects. */
typedef enum MwSoundEffect {
    MW_SFX_UI = 0,
    MW_SFX_STEP,
    MW_SFX_BLOCKED,
    MW_SFX_DOOR,
    MW_SFX_LADDER,
    MW_SFX_FALL,
    MW_SFX_ATTACK,
    MW_SFX_HURT,
    MW_SFX_MAGIC,
    MW_SFX_COIN,
    MW_SFX_VICTORY,
    MW_SFX_ERROR
} MwSoundEffect;

typedef struct MwAudio {
    SDL_AudioDeviceID device;
    SDL_AudioSpec spec;
    int available;
    int enabled;
} MwAudio;

int  mw_audio_init(MwAudio *audio);
void mw_audio_shutdown(MwAudio *audio);
void mw_audio_set_enabled(MwAudio *audio, int enabled);
void mw_audio_play(MwAudio *audio, MwSoundEffect effect);
/* QuickBASIC 3 PLAY/MML subset used by Moraff's Revenge's disassembled
 * 05A0-05EE music routine. Playback is queued asynchronously, matching the
 * original MB (music background) mode. */
void mw_audio_play_qbasic_mml(MwAudio *audio, const char *mml);
int  mw_audio_qbasic_mml_self_test(void);

#endif
