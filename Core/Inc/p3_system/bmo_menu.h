
#ifndef BMO_MENU_H
#define BMO_MENU_H

#include <stdint.h>

/* Main menu categories */
typedef enum
{
    BMO_MENU_GAMES = 0,
    BMO_MENU_TIMER,
    BMO_MENU_MUSIC,
    BMO_MENU_COUNT
} bmo_menu_category_t;

/* Menu screens */
typedef enum
{
    BMO_MENU_SCREEN_MAIN = 0,
    BMO_MENU_SCREEN_GAMES,
    BMO_MENU_SCREEN_TIMER,
    BMO_MENU_SCREEN_MUSIC
} bmo_menu_screen_t;

/* Games submenu */
typedef enum
{
    BMO_MENU_GAME_SNAKE = 0,
    BMO_MENU_GAME_SIMON,
    BMO_MENU_GAME_COUNT
} bmo_menu_game_t;

/* Timer submenu */
typedef enum
{
    BMO_MENU_TIMER_STOPWATCH = 0,
    BMO_MENU_TIMER_COUNTDOWN,
    BMO_MENU_TIMER_COUNT
} bmo_menu_timer_t;

/* Initialize menu data */
void bmo_menu_init(void);

/* Get current menu screen */
bmo_menu_screen_t bmo_menu_get_screen(void);

/* Get selected main menu category */
bmo_menu_category_t bmo_menu_get_category(void);

/* Get selected game */
bmo_menu_game_t bmo_menu_get_game(void);

/* Get selected timer mode */
bmo_menu_timer_t bmo_menu_get_timer(void);

/* Set selected main menu category */
void bmo_menu_set_category(bmo_menu_category_t category);

/* Set selected game */
void bmo_menu_set_game(bmo_menu_game_t game);

/* Set selected timer mode */
void bmo_menu_set_timer(bmo_menu_timer_t timer);

/* Set current menu screen */
void bmo_menu_set_screen(bmo_menu_screen_t screen);

/* Open submenu of selected category */
void bmo_menu_open_selected_category(void);

/* Reset current screen to main menu */
void bmo_menu_show_main(void);

#endif /* BMO_MENU_H */
