
#include <stdint.h>

#include "bmo_state.h"
#include "bmo_input.h"
#include "bmo_face.h"
#include "fake_lcd.h"
#include "lcd_test.h"
#include "bmo_game.h"

/* Store the last direction received by the active game */
static int last_game_direction = -1;
static uint32_t game_direction_count = 0;

/* Temporary game callback for testing D-Pad */
static void simulator_game_direction_callback(
    bmo_input_direction_t direction
)
{
    last_game_direction = (int)direction;
    game_direction_count++;
}

/* Initialize P2 Face Renderer and P3 State Machine */
void simulator_init(void)
{
    /* Initialize Fake LCD */
    lcd_init();

    /* Set screen to landscape mode: 480 x 320 */
    lcd_set_landscape();

    /* Initialize Face Renderer */
    face_init(12345);

    /* Initialize P3 State Machine */
    bmo_state_init();

    /* Initialize P3 Input Handler */
    bmo_input_init();

    /* Register temporary game callback */
    bmo_input_set_direction_callback(
        simulator_game_direction_callback
    );

    /* Reset D-Pad test results */
    last_game_direction = -1;
    game_direction_count = 0;
}

/* Send an event to the real P3 State Machine */
void simulator_send_event(int event)
{
    bmo_state_handle_event((bmo_event_t)event);
}

/* Send a D-Pad direction to the real P3 Input Handler */
void simulator_send_direction(int direction)
{
    if (direction < BMO_INPUT_UP ||
        direction > BMO_INPUT_RIGHT)
    {
        return;
    }

    bmo_input_handle_direction(
        (bmo_input_direction_t)direction
    );
}

/* Read the last direction received by the game callback */
int simulator_get_last_direction(void)
{
    return last_game_direction;
}

/* Count directions actually forwarded to the game */
uint32_t simulator_get_direction_count(void)
{
    return game_direction_count;
}

/* Read the current P3 state */
int simulator_get_state(void)
{
    return (int)bmo_state_get();
}

/* Read the currently selected game in MENU */
int simulator_get_selected_game(void)
{
    return (int)bmo_input_get_selected_game();
}

/* Read the current P2 face */
int simulator_get_face(void)
{
    return (int)face_current();
}

/* Advance state timers and face animation */
void simulator_tick(uint32_t now_ms)
{
    /* Update P3 State Machine timers */
    bmo_state_tick(now_ms);

    /* Update P2 Face Renderer */
    face_tick(now_ms);
}

/* Read the simulated LCD pixel buffer */
const uint16_t *simulator_get_screen(void)
{
    return lcd_screen;
}

/* Check whether the Game Manager is running */
int simulator_is_game_running(void)
{
    return (int)bmo_game_is_running();
}

/* Read the active game from the Game Manager */
int simulator_get_active_game(void)
{
    return (int)bmo_game_get_active();
}
