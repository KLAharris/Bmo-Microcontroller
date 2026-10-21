#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

/* One layer for every input source (UART keyboard, real GPIO buttons).
 * Menus and games only read these events and never touch pins.
 *
 * Reset is NOT an event: the silver button is wired between NRST and GND
 * (a hardware reset), so no pin and no code are involved. */
typedef enum {
    INPUT_NONE = 0,
    INPUT_LEFT,    /* yellow D-pad */
    INPUT_RIGHT,
    INPUT_UP,
    INPUT_DOWN,
    INPUT_ENTER,   /* red   : confirm */
    INPUT_HOME,    /* HOME  : jump to the menu page from anywhere; on the menu it stays (not a reset) */
    INPUT_TALK,    /* green : push-to-talk */
    INPUT_BACK     /* blue  : go back one step */
} input_event_t;

void          input_init(void);
input_event_t input_poll(void);        /* call every loop pass; one event per call, or INPUT_NONE */
int           input_is_down(input_event_t btn);  /* 1 while a real button is held (GPIO only) */

#endif
