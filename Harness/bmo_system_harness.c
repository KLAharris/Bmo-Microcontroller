/* Desktop stand-ins for the system and music interfaces. */
#include "bmo_system_interface.h"
#include "bmo_music_interface.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <time.h>

static int s_muted = 0;
static int s_brightness = 100;

void sys_get_time(int *hour, int *minute, int *second) {
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    if (lt) { *hour = lt->tm_hour; *minute = lt->tm_min; *second = lt->tm_sec; }
    else    { *hour = 0; *minute = 0; *second = 0; }
}

/* Fake battery: starts at 76% and loses 1% every 10 seconds, then "recharges".
   Lets you see the colour change at low levels without waiting for real hardware. */
int sys_get_battery_percent(void) {
    return 76 - (int)((SDL_GetTicks() / 10000u) % 71u);
}

int  sys_get_brightness(void) { return s_brightness; }
void sys_set_brightness(int percent) {
    if (percent < 10) percent = 10;
    if (percent > 100) percent = 100;
    s_brightness = percent;
}

int  sys_is_muted(void)         { return s_muted; }
void sys_set_muted(int muted)   { s_muted = muted ? 1 : 0; }
unsigned long sys_millis(void)  { return (unsigned long)SDL_GetTicks(); }

/* ---- music: placeholder list + print on selection (real playback = P5) ---- */
static const char *TRACKS[] = { "HAPPY DAY", "LOFI CHILL", "SLEEPY MOON", "ADVENTURE", "DANCE PARTY", "RAINY MIND" };

int music_track_count(void) { return (int)(sizeof(TRACKS) / sizeof(TRACKS[0])); }

const char *music_track_name(int index) {
    if (index < 0 || index >= music_track_count()) return "";
    return TRACKS[index];
}

void music_play(int index) {
    printf("[music] now playing: %s%s\n", music_track_name(index), s_muted ? " (sound is muted)" : "");
    fflush(stdout);
}

void music_stop(void) {
    printf("[music] stopped\n");
    fflush(stdout);
}
