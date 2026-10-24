
#include "bmo_menu.h"

/* Current menu screen */
static bmo_menu_screen_t current_screen;

/* Selected main menu category */
static bmo_menu_category_t selected_category;

/* Selected game */
static bmo_menu_game_t selected_game;

/* Selected timer mode */
static bmo_menu_timer_t selected_timer;

/* Initialize menu data */
void bmo_menu_init(void)
{
    current_screen = BMO_MENU_SCREEN_MAIN;
    selected_category = BMO_MENU_GAMES;
    selected_game = BMO_MENU_GAME_SNAKE;
    selected_timer = BMO_MENU_TIMER_STOPWATCH;
}

/* Get current menu screen */
bmo_menu_screen_t bmo_menu_get_screen(void)
{
    return current_screen;
}

/* Get selected main menu category */
bmo_menu_category_t bmo_menu_get_category(void)
{
    return selected_category;
}

/* Get selected game */
bmo_menu_game_t bmo_menu_get_game(void)
{
    return selected_game;
}

/* Get selected timer mode */
bmo_menu_timer_t bmo_menu_get_timer(void)
{
    return selected_timer;
}

/* Set selected main menu category */
void bmo_menu_set_category(bmo_menu_category_t category)
{
    if (category < BMO_MENU_COUNT)
    {
        selected_category = category;
    }
}

/* Set selected game */
void bmo_menu_set_game(bmo_menu_game_t game)
{
    if (game < BMO_MENU_GAME_COUNT)
    {
        selected_game = game;
    }
}

/* Set selected timer mode */
void bmo_menu_set_timer(bmo_menu_timer_t timer)
{
    if (timer < BMO_MENU_TIMER_COUNT)
    {
        selected_timer = timer;
    }
}

/* Set current menu screen */
void bmo_menu_set_screen(bmo_menu_screen_t screen)
{
    if (screen <= BMO_MENU_SCREEN_MUSIC)
    {
        current_screen = screen;
    }
}

/* Open submenu of selected category */
void bmo_menu_open_selected_category(void)
{
    switch (selected_category)
    {
        case BMO_MENU_GAMES:
            current_screen = BMO_MENU_SCREEN_GAMES;
            break;

        case BMO_MENU_TIMER:
            current_screen = BMO_MENU_SCREEN_TIMER;
            break;

        case BMO_MENU_MUSIC:
            current_screen = BMO_MENU_SCREEN_MUSIC;
            break;

        default:
            break;
    }
}

/* Return to main menu screen */
void bmo_menu_show_main(void)
{
    current_screen = BMO_MENU_SCREEN_MAIN;
}
