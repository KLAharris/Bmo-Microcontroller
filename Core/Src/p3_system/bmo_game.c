
#include "bmo_game.h"

/* Only two games are supported */
#define BMO_GAME_COUNT 2U

/* Callbacks registered by the game modules */
typedef struct
{
    bmo_game_start_callback_t start;
    bmo_game_stop_callback_t stop;
    bmo_game_update_callback_t update;
} bmo_game_callbacks_t;

/* Game callback registry */
static bmo_game_callbacks_t game_callbacks[BMO_GAME_COUNT];

/* Current game status */
static bmo_game_t active_game = BMO_GAME_SNAKE;
static uint8_t game_running = 0U;

/* Initialize the Game Lifecycle Manager */
void bmo_game_init(void)
{
    uint32_t i;

    for (i = 0U; i < BMO_GAME_COUNT; i++)
    {
        game_callbacks[i].start = 0;
        game_callbacks[i].stop = 0;
        game_callbacks[i].update = 0;
    }

    active_game = BMO_GAME_SNAKE;
    game_running = 0U;
}

/* Register callbacks for Snake or Simon */
void bmo_game_register(
    bmo_game_t game,
    bmo_game_start_callback_t start,
    bmo_game_stop_callback_t stop,
    bmo_game_update_callback_t update
)
{
    if ((uint32_t)game >= BMO_GAME_COUNT)
    {
        return;
    }

    game_callbacks[game].start = start;
    game_callbacks[game].stop = stop;
    game_callbacks[game].update = update;
}

/* Start the selected game */
void bmo_game_start(bmo_game_t game)
{
    if ((uint32_t)game >= BMO_GAME_COUNT)
    {
        return;
    }

    /* Stop any previously running game */
    if (game_running)
    {
        bmo_game_stop();
    }

    active_game = game;
    game_running = 1U;

    /* Call the actual game start function if registered */
    if (game_callbacks[game].start != 0)
    {
        game_callbacks[game].start();
    }
}

/* Stop the active game */
void bmo_game_stop(void)
{
    if (!game_running)
    {
        return;
    }

    /* Call the actual game stop function if registered */
    if (game_callbacks[active_game].stop != 0)
    {
        game_callbacks[active_game].stop();
    }

    game_running = 0U;
}

/* Update the active game */
void bmo_game_update(uint32_t now_ms)
{
    if (!game_running)
    {
        return;
    }

    /* Call the actual game update function if registered */
    if (game_callbacks[active_game].update != 0)
    {
        game_callbacks[active_game].update(now_ms);
    }
}

/* Return whether a game is running */
uint8_t bmo_game_is_running(void)
{
    return game_running;
}

/* Return the currently active game */
bmo_game_t bmo_game_get_active(void)
{
    return active_game;
}
