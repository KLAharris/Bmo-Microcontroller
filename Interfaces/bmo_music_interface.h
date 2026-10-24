#ifndef BMO_MUSIC_INTERFACE_H
#define BMO_MUSIC_INTERFACE_H

/* The music app only shows a list and asks for a track to be played.
 * The actual playback belongs to the audio owner (P5). */

int         music_track_count(void);
const char *music_track_name(int index);
void        music_play(int index);
void        music_stop(void);       /* stop whatever is playing (safe to call when nothing is) */

#endif
