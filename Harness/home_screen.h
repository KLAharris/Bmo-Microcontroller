#ifndef HOME_SCREEN_H
#define HOME_SCREEN_H

#include "bmo_event.h"

/* Home screen (harness-testing version; the real one is P3's state machine):
   a status bar (battery, clock, brightness, sound) over two cards: Games and Music.
   The clock in the middle of the bar is also the way into the Timer app. */

typedef enum { HOME_ACTION_NONE, HOME_ACTION_GAMES, HOME_ACTION_TIMER, HOME_ACTION_MUSIC } HomeAction;

void       home_draw_full(void);
HomeAction home_handle_event(BmoEvent ev);
void       home_update(void);   /* call every frame: refreshes time/battery/sound if they changed */

#endif
