
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

/* Available games */
typedef enum
{
    BMO_GAME_SNAKE = 0,
    BMO_GAME_SIMON
} bmo_game_t;

/* Callback for directional input in APP state */
typedef void (*bmo_input_direction_callback_t)(
    bmo_input_direction_t direction
);

/* Initialize the input handler */
void bmo_input_init(void);

/* Register a callback for the active game */
void bmo_input_set_direction_callback(
    bmo_input_direction_callback_t callback
);

/* Handle D-Pad input according to the current state */
void bmo_input_handle_direction(
    bmo_input_direction_t direction
);

/* Get the currently selected game */
bmo_game_t bmo_input_get_selected_game(void);

#endif /* BMO_INPUT_H */
