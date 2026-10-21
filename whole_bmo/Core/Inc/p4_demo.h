#ifndef P4_DEMO_H
#define P4_DEMO_H

#include <stdint.h>

/* P4 board test hook: lets P2's face hand the screen to Snake, Simon, the
 * Timer or the Music app and take it back, driven by keys typed in a serial
 * terminal (ST-LINK virtual COM port, 115200 baud). No physical buttons needed.
 *
 *   p4_demo_init()      call once, after face_init()
 *   p4_demo_tick(ms)    call every main-loop pass, right after face_tick()
 *
 * See team/p4/README.md for the three lines to add to main.c.
 */
void p4_demo_init(void);
void p4_demo_tick(uint32_t now_ms);

/* print a line to the serial terminal (also used by Board/bmo_system_board.c) */
void p4_log(const char *s);

/* 1 while a game runs: main.c pauses the wake word so the game gets the CPU */
int  p4_game_running(void);

#endif
