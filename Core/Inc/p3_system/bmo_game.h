
#ifndef BMO_GAME_H
#define BMO_GAME_H

#include <stdint.h>
#include "bmo_input.h"

/*
 * Game Lifecycle Manager
 *
 * Handles starting, stopping, and updating
 * the selected game.
 *
 * Actual Snake and Simon implementations
 * will be connected later.
 */

/* Callback types for game integration */
typedef void (*bmo_game_start_callback_t)(void);
typedef void (*bmo_game_stop_callback_t)(void);
typedef void (*bmo_game_update_callback_t)(uint32_t now_ms);

/* Initialize game manager */
void bmo_game_init(void);

/* Register callbacks for a specific game */
void bmo_game_register(
    bmo_game_t game,
    bmo_game_start_callback_t start,
    bmo_game_stop_callback_t stop,
    bmo_game_update_callback_t update
);

/* Start the selected game */
void bmo_game_start(bmo_game_t game);

/* Stop the currently active game */
void bmo_game_stop(void);

/* Update the active game */
void bmo_game_update(uint32_t now_ms);

/* Check whether a game is running */
uint8_t bmo_game_is_running(void);

/* Get the active game */
bmo_game_t bmo_game_get_active(void);

#endif /* BMO_GAME_H */
