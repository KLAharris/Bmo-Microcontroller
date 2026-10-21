#include "bmo_launcher.h"
#include "bmo_home.h"
#include "bmo_gfx_interface.h"
#include "bmo_ui_scale.h"
#include "bmo_draw_util.h"
#include <stddef.h>

extern BmoApp bmo_game_snake;
extern BmoApp bmo_game_simon;
extern BmoApp bmo_app_timer;
extern BmoApp bmo_app_music;

typedef enum { MODE_HOME, MODE_SELECT, MODE_APP, MODE_HOLD } Mode;

#define GAME_OVER_HOLD 1.8f     /* seconds a finished game's last screen stays up */

#define UIN(v) (UI(v) < 1 ? 1 : UI(v))

/* ---- game picker (800x480 design values, scaled) ---- */
#define MENU_ITEM_COUNT 2
#define CARD_W   340
#define CARD_H   270
#define CARD_GAP 40
#define CARD_Y   100

typedef struct { const char *name; const char *subtitle; int accent[3]; } MenuItem;
static const MenuItem s_items[MENU_ITEM_COUNT] = {
    { "SNAKE", "EAT AND GROW",       {60, 220, 100} },
    { "SIMON", "REPEAT THE COLORS",  {255, 200, 40} },
};
static BmoApp *s_menu_apps[MENU_ITEM_COUNT];
static int s_menu_selection = 0;

#define MENU_BG      RGB565(10, 12, 24)
#define TEXT_MUTED   RGB565(120, 125, 150)

static uint16_t col(int r, int g, int b, int dim) {
    if (dim) { r = r * 38 / 100; g = g * 38 / 100; b = b * 38 / 100; }
    return RGB565(r, g, b);
}

/* 7x5 pixel-art snake with a head and an apple. cell = pixel size of one square. */
static void draw_snake_icon(int x, int y, int cell, int dim) {
    static const char *map[5] = { "gggggHr", "g......", "gggggg.", ".....g.", "gggggg." };
    int gap = 1;
    int sz = cell - 2 * gap;
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 7; c++) {
            char ch = map[r][c];
            int px = x + c * cell + gap, py = y + r * cell + gap;
            if (ch == 'g') {
                if ((r + c) & 1) gfx_fill_rect(px, py, sz, sz, col(30, 165, 65, dim));
                else             gfx_fill_rect(px, py, sz, sz, col(40, 200, 80, dim));
            } else if (ch == 'H') {
                gfx_fill_rect(px, py, sz, sz, col(130, 255, 150, dim));
                gfx_fill_rect(px + UI(12), py + UI(4), UIN(6), UIN(6), col(255, 255, 255, dim)); /* eye */
                gfx_fill_rect(px + UI(15), py + UI(6), UIN(3), UIN(3), col(0, 0, 0, dim));       /* pupil */
            } else if (ch == 'r') {
                gfx_fill_rect(px + UI(2), py + UI(4), sz - 2 * UI(2), sz - UI(6), col(230, 55, 55, dim));   /* apple */
                gfx_fill_rect(px + UI(5), py + UI(7), UIN(4), UIN(4), col(255, 170, 170, dim));             /* shine */
                gfx_fill_rect(px + sz / 2 - 1, py + UI(1), UIN(4), UIN(3), col(70, 200, 70, dim));          /* leaf */
            }
        }
    }
}

/* The same plus-shaped colour layout the game uses, colours matching the game. */
static void draw_simon_icon(int x, int y, int cell, int dim) {
    static const struct { int gx, gy; int rgb[3]; } arms[4] = {
        {1, 0, {255, 200, 40}},
        {1, 2, {50, 120, 255}},
        {0, 1, {240, 60, 60}},
        {2, 1, {60, 210, 90}},
    };
    int gap = UIN(2);
    gfx_fill_rect(x + cell + cell / 4, y + cell + cell / 4, cell / 2, cell / 2, RGB565(40, 44, 70)); /* hub */
    for (int i = 0; i < 4; i++) {
        int ax = x + arms[i].gx * cell, ay = y + arms[i].gy * cell;
        gfx_fill_rect(ax + gap, ay + gap, cell - 2 * gap, cell - 2 * gap, col(arms[i].rgb[0], arms[i].rgb[1], arms[i].rgb[2], dim));
    }
}

static int menu_card_x(int i) { return UIX((UI_BASE_W - (2 * CARD_W + CARD_GAP)) / 2 + i * (CARD_W + CARD_GAP)); }

/* Draws one whole card; it fully overwrites its own rectangle, so it is
   safe to call again when the selection changes. */
static void draw_menu_card(int i, int selected) {
    int x = menu_card_x(i), y = UIY(CARD_Y);
    int cw = UI(CARD_W), ch = UI(CARD_H);
    int bt = selected ? UIN(4) : UIN(2);
    uint16_t border = selected ? RGB565(s_items[i].accent[0], s_items[i].accent[1], s_items[i].accent[2])
                               : RGB565(50, 54, 80);
    uint16_t bg = selected ? RGB565(26, 30, 62) : RGB565(16, 18, 34);
    gfx_fill_rect(x, y, cw, ch, border);
    gfx_fill_rect(x + bt, y + bt, cw - 2 * bt, ch - 2 * bt, bg);

    int dim = !selected;
    int icon_y = y + UI(36);
    if (i == 0) draw_snake_icon(x + (cw - 7 * UI(24)) / 2, icon_y, UI(24), dim);
    else        draw_simon_icon(x + (cw - 3 * UI(40)) / 2, icon_y, UI(40), dim);

    draw_text_scaled_centered(x + cw / 2, y + UI(176), s_items[i].name, selected ? COLOR_WHITE : TEXT_MUTED, UI_TS(4));
    draw_text_scaled_centered(x + cw / 2, y + UI(224), UI_NARROW && i == 1 ? "REPEAT COLORS" : s_items[i].subtitle,
                              selected ? RGB565(190, 196, 220) : RGB565(80, 84, 108), 2);
}

static void draw_menu_full(void) {
    gfx_fill_rect(0, 0, GFX_WIDTH, GFX_HEIGHT, MENU_BG);
    draw_text_scaled_centered(GFX_WIDTH / 2, UIY(36), "SELECT GAME", COLOR_WHITE, UI_TS(4));
    for (int i = 0; i < MENU_ITEM_COUNT; i++) draw_menu_card(i, i == s_menu_selection);
    draw_text_scaled_centered(GFX_WIDTH / 2, UIY(414), UI_HINT("LEFT/RIGHT: CHOOSE   ENTER: PLAY", "L/R: CHOOSE   ENTER: PLAY"), TEXT_MUTED, 2);
}

static void menu_move_selection(int new_selection) {
    if (new_selection == s_menu_selection) return;
    int old = s_menu_selection;
    s_menu_selection = new_selection;
    draw_menu_card(old, 0);
    draw_menu_card(s_menu_selection, 1);
}

/* ---- state machine ---- */
static Mode    s_mode = MODE_HOME;
static Mode    s_return_mode = MODE_HOME;   /* where leaving the current app goes */
static BmoApp *s_current = NULL;
static float   s_hold_timer = 0.0f;
static int     s_leave = 0;

static int     s_direct = 0;                /* desktop test: one app, restarted when it ends */
static BmoApp *s_direct_app = NULL;

static int is_game(const BmoApp *a) { return a == &bmo_game_snake || a == &bmo_game_simon; }

static void show_screen(Mode m) {
    s_mode = m;
    if (m == MODE_SELECT) draw_menu_full();
    else                  home_draw_full();
}

static void start_app(BmoApp *app, Mode return_to) {
    s_current = app;
    s_return_mode = return_to;
    s_mode = MODE_APP;
    app->init();
}

static void leave_app(void) {
    s_current->cleanup();
    s_current = NULL;
    if (s_direct) { start_app(s_direct_app, MODE_HOME); return; }   /* restart the same app */
    show_screen(s_return_mode);
}

void launcher_open(void) {
    s_menu_apps[0] = &bmo_game_snake;
    s_menu_apps[1] = &bmo_game_simon;
    s_direct = 0;
    s_leave = 0;
    s_current = NULL;
    show_screen(MODE_HOME);
}

void launcher_open_app(BmoApp *app) {
    s_menu_apps[0] = &bmo_game_snake;
    s_menu_apps[1] = &bmo_game_simon;
    s_direct = 1;
    s_direct_app = app;
    s_leave = 0;
    start_app(app, MODE_HOME);
}

void launcher_event(BmoEvent ev) {
    switch (s_mode) {
        case MODE_HOME: {
            if (ev == BMO_EVENT_BACK) { s_leave = 1; break; }
            HomeAction a = home_handle_event(ev);
            if (a == HOME_ACTION_GAMES)      show_screen(MODE_SELECT);
            else if (a == HOME_ACTION_TIMER) start_app(&bmo_app_timer, MODE_HOME);
            else if (a == HOME_ACTION_MUSIC) start_app(&bmo_app_music, MODE_HOME);
            break;
        }
        case MODE_SELECT:
            if (ev == BMO_EVENT_LEFT)        menu_move_selection(0);
            else if (ev == BMO_EVENT_RIGHT)  menu_move_selection(1);
            else if (ev == BMO_EVENT_CENTER) start_app(s_menu_apps[s_menu_selection], MODE_SELECT);
            else if (ev == BMO_EVENT_BACK)   show_screen(MODE_HOME);
            break;
        case MODE_APP:
            /* games can always be quit with BACK; the timer and music apps get BACK
               themselves so they can step back one screen first */
            if (ev == BMO_EVENT_BACK && is_game(s_current)) leave_app();
            else s_current->handle_input(ev);
            break;
        case MODE_HOLD:
            if (ev == BMO_EVENT_CENTER || ev == BMO_EVENT_BACK) leave_app();
            break;
    }
}

void launcher_update(float dt) {
    if (s_mode == MODE_HOME) {
        home_update();                      /* clock / battery / sound icon */
    } else if (s_mode == MODE_APP) {
        s_current->update(dt);
        s_current->draw();
        if (s_current->is_finished()) {
            if (is_game(s_current)) { s_mode = MODE_HOLD; s_hold_timer = 0.0f; }   /* show the final screen */
            else leave_app();                                                     /* timer / music: straight back */
        }
    } else if (s_mode == MODE_HOLD) {
        s_hold_timer += dt;
        if (s_hold_timer >= GAME_OVER_HOLD) leave_app();
    }
}

int launcher_at_home(void) { return s_mode == MODE_HOME; }

int launcher_leave_requested(void) { return s_leave; }

void launcher_shutdown(void) {
    if (s_current) { s_current->cleanup(); s_current = NULL; }
    s_leave = 0;
}
