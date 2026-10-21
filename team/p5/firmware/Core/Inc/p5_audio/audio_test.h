#ifndef AUDIO_TEST_H
#define AUDIO_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

void audio_test_tone_start(void);  /* 441 Hz tone on the speaker until stopped */
void audio_test_tone_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_TEST_H */
