#ifndef BMO_EVENT_H
#define BMO_EVENT_H

/* PLACEHOLDER — align this exactly with whatever P3 actually defines.
 * What matters most: everyone includes ONE shared copy of this file,
 * not their own re-typed version of it. */
typedef enum {
    BMO_EVENT_NONE = 0,
    BMO_EVENT_UP,
    BMO_EVENT_DOWN,
    BMO_EVENT_LEFT,
    BMO_EVENT_RIGHT,
    BMO_EVENT_CENTER,   /* 5th press, if your switch has one */
    BMO_EVENT_BACK      /* dedicated "exit app" button, if one exists */
} BmoEvent;

#endif
