#ifndef BMO_APP_INTERFACE_H
#define BMO_APP_INTERFACE_H

#include "bmo_event.h"

/* The contract between P3 (state machine) and every app/game (P4).
 * P3 calls these; it never reaches into a game's .c file directly.
 * This is a STARTING PROPOSAL — get it agreed with P3 exactly, then
 * freeze it. Everyone includes this one file, nobody re-types it. */

typedef struct {
    const char *name;                     /* shown in a menu, e.g. "Snake" */
    void (*init)(void);                   /* called once when launched */
    void (*update)(float dt_seconds);     /* called every tick */
    void (*draw)(void);                   /* called every tick, after update */
    void (*handle_input)(BmoEvent event); /* called when a button event arrives */
    void (*cleanup)(void);                /* called once when the app exits */
    int  (*is_finished)(void);            /* 1 = ready to hand control back to P3 */
} BmoApp;

#endif
