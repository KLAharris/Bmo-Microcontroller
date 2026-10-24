#include "bmo_music_interface.h"

/*
 * Temporary UI-only music backend.
 * Replace with P5 audio implementation when available.
 * No actual audio playback.
 */

int music_track_count(void)
{
    return 3;
}

const char *music_track_name(int index)
{
    static const char *tracks[] = {
        "Track 1",
        "Track 2",
        "Track 3"
    };

    if (index < 0 || index >= 3)
    {
        return "Unknown";
    }

    return tracks[index];
}

void music_play(int index)
{
    (void)index;
    /* UI testing only — no audio output. */
}

void music_stop(void)
{
    /* UI testing only — no audio output. */
}
