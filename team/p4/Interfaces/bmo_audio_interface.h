#ifndef BMO_AUDIO_INTERFACE_H
#define BMO_AUDIO_INTERFACE_H

/* Owned by P5. tone_id is just an int your team agrees on (or an enum) —
 * swap this for whatever P5 actually exposes. Games never touch a
 * buzzer/DAC/I2S peripheral directly. */
void audio_play_tone(int tone_id);

#endif
