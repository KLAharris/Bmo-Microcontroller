#ifndef AUDIO_SFX_H
#define AUDIO_SFX_H

/* BMO audio output (P5): sound effects made in code, and WAV files from the SD card.
 * One sound at a time. Everything returns at once; the DMA plays in the background.
 * Call audio_sfx_poll() every pass of the main loop. */

#ifdef __cplusplus
extern "C" {
#endif

#define SFX_WAKE  0   /* "bee-boop!" rising chirp: BMO heard "hey BMO"   */
#define SFX_OK    1   /* short "bip": command understood (games / music) */
#define SFX_BEEP  2   /* plain 0.3 s beep, for testing the speaker        */

void audio_sfx_play(int sfx);        /* play a sound effect (cuts off whatever is playing) */
void audio_sfx_poll(void);           /* call every loop: refills the buffer, stops at the end */
int  audio_busy(void);               /* 1 while anything is playing (or a WAV is queued)       */

/* WAV files on the SD card: 16-bit PCM, mono or stereo, 22050 Hz, short 8.3 names
 * such as "HI.WAV" or "TRACK1.WAV". Make them on a Mac with:
 *   afconvert -f WAVE -d LEI16@22050 -c 1 input.mp3 OUTPUT.WAV                         */
int  audio_say(const char *path);    /* play a file. If a sound effect is playing, it plays
                                        right after it. Returns 0 if OK, <0 on error.        */
void audio_stop(void);               /* stop everything now (also clears the queue)          */
void audio_set_volume(int percent);  /* 0..100 for WAV files (default 100)                   */

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_SFX_H */
