#include "bmo_menu.h"
#include "bmo_p3_music_adapter.h"

#include "bmo_input.h"
#include "bmo_state.h"
#include "bmo_game.h"

/* Callback for the active game */
static bmo_input_direction_callback_t direction_callback = 0;

/* Currently selected game in MENU */
static bmo_game_t selected_game = BMO_GAME_SNAKE;

void bmo_input_init(void)
{
    direction_callback = 0;
    selected_game = BMO_GAME_SNAKE;
}

void bmo_input_set_direction_callback(
    bmo_input_direction_callback_t callback
)
{
    direction_callback = callback;
}

bmo_game_t bmo_input_get_selected_game(void)
{
    return selected_game;
}

void bmo_input_handle_direction(
    bmo_input_direction_t direction
)
{
    /* Reject invalid directions */
    if ((uint32_t)direction > (uint32_t)BMO_INPUT_RIGHT)
    {
        return;
    }

    /* MENU: LEFT / RIGHT switch between two games */
    if (bmo_state_get() == BMO_STATE_MENU)
    {
        if (direction == BMO_INPUT_LEFT ||
            direction == BMO_INPUT_RIGHT)
        {
            if (selected_game == BMO_GAME_SNAKE)
            {
                selected_game = BMO_GAME_SIMON;
            }
            else
            {
                selected_game = BMO_GAME_SNAKE;
            }
        }

        return;
    }

    /* Ignore directional input outside APP */
    if (bmo_state_get() != BMO_STATE_APP)
    {
        return;
    }

    /* APP: forward direction to the active game */
    if (bmo_menu_get_category() == BMO_MENU_MUSIC)
    {
        bmo_p3_music_handle_direction(direction);
    }
    else
    {
        bmo_game_handle_direction(direction);
    }

    /* Preserve the existing simulator callback */
    if (direction_callback != 0)
    {
        direction_callback(direction);
    }
}

