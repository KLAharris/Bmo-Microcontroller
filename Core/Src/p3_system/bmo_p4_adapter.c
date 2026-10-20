#include "bmo_p4_adapter.h"
#include "bmo_game.h"
#include "bmo_app_interface.h"
#include "bmo_event.h"

#include <stdint.h>

extern BmoApp bmo_game_snake;
extern BmoApp bmo_game_simon;

static uint32_t snake_last_ms;
static uint32_t simon_last_ms;

static void snake_start(void)
{
    snake_last_ms = 0U;
    if (bmo_game_snake.init != 0)
        bmo_game_snake.init();
}

static void snake_stop(void)
{
    if (bmo_game_snake.cleanup != 0)
        bmo_game_snake.cleanup();
}

static void snake_update(uint32_t now_ms)
{
    float dt = 0.0f;

    if (snake_last_ms != 0U)
        dt = (float)(now_ms - snake_last_ms) / 1000.0f;

    snake_last_ms = now_ms;

    if (bmo_game_snake.update != 0)
        bmo_game_snake.update(dt);

    if (bmo_game_snake.draw != 0)
        bmo_game_snake.draw();
}

static void simon_start(void)
{
    simon_last_ms = 0U;
    if (bmo_game_simon.init != 0)
        bmo_game_simon.init();
}

static void simon_stop(void)
{
    if (bmo_game_simon.cleanup != 0)
        bmo_game_simon.cleanup();
}

static void simon_update(uint32_t now_ms)
{
    float dt = 0.0f;

    if (simon_last_ms != 0U)
        dt = (float)(now_ms - simon_last_ms) / 1000.0f;

    simon_last_ms = now_ms;

    if (bmo_game_simon.update != 0)
        bmo_game_simon.update(dt);

    if (bmo_game_simon.draw != 0)
        bmo_game_simon.draw();
}

static BmoEvent direction_to_event(bmo_input_direction_t direction)
{
    switch (direction)
    {
        case BMO_INPUT_UP:    return BMO_EVENT_UP;
        case BMO_INPUT_DOWN:  return BMO_EVENT_DOWN;
        case BMO_INPUT_LEFT:  return BMO_EVENT_LEFT;
        case BMO_INPUT_RIGHT: return BMO_EVENT_RIGHT;
        default:              return BMO_EVENT_NONE;
    }
}

static void snake_direction(bmo_input_direction_t direction)
{
    BmoEvent event = direction_to_event(direction);

    if (event != BMO_EVENT_NONE && bmo_game_snake.handle_input != 0)
        bmo_game_snake.handle_input(event);
}

static void simon_direction(bmo_input_direction_t direction)
{
    BmoEvent event = direction_to_event(direction);

    if (event != BMO_EVENT_NONE && bmo_game_simon.handle_input != 0)
        bmo_game_simon.handle_input(event);
}

void bmo_p4_adapter_register(void)
{
    bmo_game_register(
        BMO_GAME_SNAKE,
        snake_start,
        snake_stop,
        snake_update
    );

    bmo_game_register_direction(
        BMO_GAME_SNAKE,
        snake_direction
    );

    bmo_game_register(
        BMO_GAME_SIMON,
        simon_start,
        simon_stop,
        simon_update
    );

    bmo_game_register_direction(
        BMO_GAME_SIMON,
        simon_direction
    );
}
