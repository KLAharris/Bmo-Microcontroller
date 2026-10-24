
#include <stdint.h>

#include "bmo_state.h"
#include "bmo_input.h"
#include "bmo_face.h"
#include "fake_lcd.h"
#include "lcd_test.h"
#include "bmo_game.h"
#include "bmo_menu.h"

/* Store the last direction received by the simulator callback */
static int last_game_direction = -1;
static uint32_t game_direction_count = 0;

/* Mock game-specific direction counters */
static uint32_t snake_direction_count = 0;
static uint32_t simon_direction_count = 0;

/* Temporary simulator callback for testing D-Pad */
static void simulator_game_direction_callback(
    bmo_input_direction_t direction
)
{
    last_game_direction = (int)direction;
    game_direction_count++;
}

/* Mock Snake callback */
static void mock_snake_direction(
    bmo_input_direction_t direction
)
{
    (void)direction;
    snake_direction_count++;
}

/* Mock Simon callback */
static void mock_simon_direction(
    bmo_input_direction_t direction
)
{
    (void)direction;
    simon_direction_count++;
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

    /* Initialize P3 State Machine and Game Manager */
    bmo_state_init();

    /* Initialize P3 Input Handler */
    bmo_input_init();

    /* Initialize Main Menu data */
    bmo_menu_init();

    /* Register mock game-specific callbacks */
    bmo_game_register_direction(
        BMO_GAME_SNAKE,
        mock_snake_direction
    );

    bmo_game_register_direction(
        BMO_GAME_SIMON,
        mock_simon_direction
    );

    /* Register simulator direction callback */
    bmo_input_set_direction_callback(
        simulator_game_direction_callback
    );

    /* Reset D-Pad test results */
    last_game_direction = -1;
    game_direction_count = 0;

    /* Reset mock game counters */
    snake_direction_count = 0;
    simon_direction_count = 0;
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

/* Read the last direction received by simulator callback */
int simulator_get_last_direction(void)
{
    return last_game_direction;
}

/* Count directions forwarded to simulator callback */
uint32_t simulator_get_direction_count(void)
{
    return game_direction_count;
}

/* Count directions received by Snake mock callback */
uint32_t simulator_get_snake_direction_count(void)
{
    return snake_direction_count;
}

/* Count directions received by Simon mock callback */
uint32_t simulator_get_simon_direction_count(void)
{
    return simon_direction_count;
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

/* ===== Main Menu Simulator Interface ===== */

/* Read current menu screen */
int simulator_menu_get_screen(void)
{
    return (int)bmo_menu_get_screen();
}

/* Read selected main menu category */
int simulator_menu_get_category(void)
{
    return (int)bmo_menu_get_category();
}

/* Read selected game in new menu module */
int simulator_menu_get_game(void)
{
    return (int)bmo_menu_get_game();
}

/* Read selected timer mode */
int simulator_menu_get_timer(void)
{
    return (int)bmo_menu_get_timer();
}

/* Set selected main menu category */
void simulator_menu_set_category(int category)
{
    if (category >= BMO_MENU_GAMES &&
        category < BMO_MENU_COUNT)
    {
        bmo_menu_set_category(
            (bmo_menu_category_t)category
        );
    }
}

/* Set selected game */
void simulator_menu_set_game(int game)
{
    if (game >= BMO_MENU_GAME_SNAKE &&
        game < BMO_MENU_GAME_COUNT)
    {
        bmo_menu_set_game(
            (bmo_menu_game_t)game
        );
    }
}

/* Set selected timer mode */
void simulator_menu_set_timer(int timer)
{
    if (timer >= BMO_MENU_TIMER_STOPWATCH &&
        timer < BMO_MENU_TIMER_COUNT)
    {
        bmo_menu_set_timer(
            (bmo_menu_timer_t)timer
        );
    }
}

/* Open selected main menu category */
void simulator_menu_open_category(void)
{
    bmo_menu_open_selected_category();
}

/* Return menu data to main screen */
void simulator_menu_show_main(void)
{
    bmo_menu_show_main();
}
