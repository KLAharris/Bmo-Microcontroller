/* bmo_system_board.c - placeholder board versions of the system and music
 * interfaces, so the Timer and Music apps run on the board today.
 *
 * Replace each piece when its owner delivers the real one:
 *   clock            -> P3 (RTC)            battery -> P6 (ADC on PA3)
 *   mute / music     -> P5 (audio)          brightness -> P1 (backlight PWM)
 * Delete this file (or just the part that moved) when that happens; the
 * games and apps do not change.
 */
#include "main.h"            /* HAL_GetTick() */
#include "bmo_system_interface.h"
#include "bmo_music_interface.h"
#include "p4_demo.h"         /* p4_log() */

static int s_muted = 0;
static int s_brightness = 100;

unsigned long sys_millis(void) { return (unsigned long)HAL_GetTick(); }

/* Fake clock: starts at 12:00:00 at power-up. */
void sys_get_time(int *hour, int *minute, int *second)
{
    unsigned long t = HAL_GetTick() / 1000UL + 12UL * 3600UL;
    *hour   = (int)((t / 3600UL) % 24UL);
    *minute = (int)((t / 60UL) % 60UL);
    *second = (int)(t % 60UL);
}

int  sys_get_battery_percent(void) { return 100; }

int  sys_is_muted(void)            { return s_muted; }
void sys_set_muted(int muted)      { s_muted = muted ? 1 : 0; }

int  sys_get_brightness(void)      { return s_brightness; }
void sys_set_brightness(int percent)
{
    if (percent < 10)  percent = 10;
    if (percent > 100) percent = 100;
    s_brightness = percent;          /* TODO P1: drive the backlight PWM (TIM3_CH1 / PB4) */
}

/* ---- music: a track list and log lines only; real playback is P5's ---- */
static const char *TRACKS[] = { "HAPPY DAY", "LOFI CHILL", "SLEEPY MOON", "ADVENTURE", "DANCE PARTY", "RAINY MIND" };

int music_track_count(void) { return (int)(sizeof(TRACKS) / sizeof(TRACKS[0])); }

const char *music_track_name(int index)
{
    if (index < 0 || index >= music_track_count()) return "";
    return TRACKS[index];
}

void music_play(int index)
{
    p4_log("[music] now playing: ");
    p4_log(music_track_name(index));
    p4_log("\r\n");
}

void music_stop(void) { p4_log("[music] stopped\r\n"); }
