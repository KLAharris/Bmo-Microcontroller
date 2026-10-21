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
#include "tim.h"             /* htim3 (TIM3_CH1 = backlight PWM on PB4) */
#include "bmo_system_interface.h"
#include "bmo_music_interface.h"
#include "p4_demo.h"         /* p4_log() */
#include "p5_audio/audio_sfx.h"   /* P5: audio_say(), audio_stop(), audio_set_volume() */
#include <stdio.h>

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
void sys_set_muted(int muted)
{
    s_muted = muted ? 1 : 0;
    audio_set_volume(s_muted ? 0 : 100);
}

int  sys_get_brightness(void)      { return s_brightness; }

/* Backlight PWM: TIM3_CH1 on PB4. Duty = percent of the timer period. */
void sys_set_brightness(int percent)
{
    static int started = 0;
    if (percent < 10)  percent = 10;
    if (percent > 100) percent = 100;
    s_brightness = percent;

    if (!started) {                       /* first call starts the PWM output */
        HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
        started = 1;
    }
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim3) + 1u;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, (uint32_t)percent * period / 100u);
}

/* ---- music: track n on screen = TRACK(n+1).WAV on the SD card (P5 plays it) ---- */
static const char *TRACKS[] = { "COME ALONG WITH ME", "BEN 10", "THITIKORN", "BLANK SPACE", "DITTO", "OTONOKE" };

int music_track_count(void) { return (int)(sizeof(TRACKS) / sizeof(TRACKS[0])); }

const char *music_track_name(int index)
{
    if (index < 0 || index >= music_track_count()) return "";
    return TRACKS[index];
}

void music_play(int index)
{
    char name[24];
    snprintf(name, sizeof name, "TRACK%d.WAV", index + 1);
    audio_say(name);
    p4_log("[music] now playing: ");
    p4_log(music_track_name(index));
    p4_log("\r\n");
}

void music_stop(void) { audio_stop(); p4_log("[music] stopped\r\n"); }
