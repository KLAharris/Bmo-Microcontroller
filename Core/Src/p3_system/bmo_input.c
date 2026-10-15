
#include "bmo_input.h"
#include "bmo_state.h"

/* Callback for the active game */
static bmo_input_direction_callback_t direction_callback = 0;

void bmo_input_init(void)
{
    direction_callback = 0;
}

void bmo_input_set_direction_callback(
    bmo_input_direction_callback_t callback
)
{
    direction_callback = callback;
}

void bmo_input_handle_direction(
    bmo_input_direction_t direction
)
{
    /* Reject invalid directions */
    if (direction > BMO_INPUT_RIGHT)
    {
        return;
    }

    /* Directional input is forwarded only in APP state */
    if (bmo_state_get() != BMO_STATE_APP)
    {
        return;
    }

    /* Forward the direction to the active game */
    if (direction_callback != 0)
    {
        direction_callback(direction);
    }
}
