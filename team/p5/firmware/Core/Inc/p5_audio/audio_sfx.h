#ifndef AUDIO_SFX_H
#define AUDIO_SFX_H

#ifdef __cplusplus
extern "C" {
#endif

#define SFX_WAKE  0   /* "bee-boop!" rising chirp: BMO heard "hey BMO"   */
#define SFX_OK    1   /* short "bip": command understood (games / music) */
#define SFX_BEEP  2   /* plain 0.3 s beep, for testing the speaker        */

void audio_sfx_play(int sfx);  /* starts the sound and returns at once (DMA plays it) */
void audio_sfx_poll(void);     /* call every pass of the main loop                    */
int  audio_busy(void);         /* 1 while a sound is playing                          */

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_SFX_H */
