
#ifndef BMO_INPUT_H
#define BMO_INPUT_H

#include <stdint.h>

/* D-Pad directions */
typedef enum
{
    BMO_INPUT_UP = 0,
    BMO_INPUT_DOWN,
    BMO_INPUT_LEFT,
    BMO_INPUT_RIGHT
} bmo_input_direction_t;

/* Callback for receiving directional input */
typedef void (*bmo_input_direction_callback_t)(
    bmo_input_direction_t direction
);

/* Initialize the input handler */
void bmo_input_init(void);

/* Register a callback for directional input */
void bmo_input_set_direction_callback(
    bmo_input_direction_callback_t callback
);

/* Dispatch a directional input */
void bmo_input_handle_direction(
    bmo_input_direction_t direction
);

#endif /* BMO_INPUT_H */
