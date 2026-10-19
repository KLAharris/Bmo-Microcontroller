#ifndef BMO_SYSTEM_INTERFACE_H
#define BMO_SYSTEM_INTERFACE_H

/* Things the home screen / timer need from the rest of the system.
 * Implemented twice:
 *   - Harness/bmo_system_harness.c : PC clock + fake battery (desktop testing)
 *   - firmware: RTC for the time, ADC for the battery, audio code for mute
 *     (owners: P3 / P5 — agree the names with them). */

/* current wall-clock time, 24-hour */
void sys_get_time(int *hour, int *minute, int *second);

/* battery level 0..100 */
int  sys_get_battery_percent(void);

/* speaker mute state (1 = muted) */
int  sys_is_muted(void);
void sys_set_muted(int muted);

/* screen backlight brightness, 10..100 percent (never fully dark, so the
 * screen can't be switched off by accident). Firmware: usually a PWM duty on the
 * display's backlight pin or the SSD1963's PWM output - check the display
 * module's schematic with P1. */
int  sys_get_brightness(void);
void sys_set_brightness(int percent);

/* milliseconds since power-up (firmware: HAL_GetTick()). The timer uses this
 * instead of adding up frame times, so it stays accurate if a frame runs long. */
unsigned long sys_millis(void);

#endif
