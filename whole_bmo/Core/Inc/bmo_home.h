#ifndef BMO_HOME_H
#define BMO_HOME_H

#include "bmo_event.h"

/* Home screen: a status bar (battery, clock, brightness, sound) over two cards,
   Games and Music. The clock in the middle of the bar is the way into the Timer.
   Laid out for 800x480 and scaled to the real screen (bmo_ui_scale.h).
   Used by the desktop harness and by the board build (bmo_launcher.c). */

typedef enum { HOME_ACTION_NONE, HOME_ACTION_GAMES, HOME_ACTION_TIMER, HOME_ACTION_MUSIC } HomeAction;

void       home_draw_full(void);
HomeAction home_handle_event(BmoEvent ev);
void       home_update(void);   /* call every frame: refreshes time/battery/sound if they changed */

#endif
