#ifndef BMO_LAUNCHER_H
#define BMO_LAUNCHER_H

#include "bmo_event.h"
#include "bmo_app_interface.h"

/* The home screen, the Snake/Simon picker and the running app, as one small
   state machine. Same code on the desktop harness and on the board.
   (P3's real state machine will replace this; the apps do not change.)

       HOME --Games--> SELECT --> game        HOME --clock--> Timer
       HOME --Music--> Music                  BACK on HOME  = "leave" (see below) */

void launcher_open(void);                  /* show the home screen */
void launcher_open_app(BmoApp *app);       /* desktop test: run one app directly, restart it when it ends */
void launcher_event(BmoEvent ev);
void launcher_update(float dt);            /* every frame: clock, running app, game-over pause */
int  launcher_leave_requested(void);       /* 1 after BACK on the home screen (the board then returns to the face) */
void launcher_shutdown(void);              /* clean up a running app (leaving for the face, or quitting) */

#endif
