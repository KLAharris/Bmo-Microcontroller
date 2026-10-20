#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include "bmo_gfx_interface.h"
#include "bmo_app_interface.h"
#include "bmo_event.h"
#include "bmo_draw_util.h"
#include "home_screen.h"
#include "bmo_system_interface.h"

extern SDL_Renderer *g_renderer;
extern SDL_Texture  *g_canvas;

extern BmoApp bmo_game_snake;
extern BmoApp bmo_game_simon;
extern BmoApp bmo_app_timer;
extern BmoApp bmo_app_music;

/* Screens:  HOME (status bar + Games/Music cards; the clock opens the Timer)
 *           SELECT (the Snake/Simon picker)  ->  APP (a game, the timer, music)
 *           HOLD (a finished game's last screen, shown briefly)             */
typedef enum { MODE_HOME, MODE_SELECT, MODE_APP, MODE_HOLD } Mode;

#define GAME_OVER_HOLD 1.8f /* seconds a finished game's last screen stays up */

/* ---- Game select screen (unchanged) ---- */
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

static void text_centered(int cx, int y, const char *s, uint16_t color, int scale) {
    draw_text_scaled_centered(cx, y, s, color, scale);
}

/* 7x5 pixel-art snake with a head and an apple. cell = pixel size of one square. */
static void draw_snake_icon(int x, int y, int cell, int dim) {
    static const char *map[5] = { "gggggHr", "g......", "gggggg.", ".....g.", "gggggg." };
    int sz = cell - 2;
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 7; c++) {
            char ch = map[r][c];
            int px = x + c * cell + 1, py = y + r * cell + 1;
            if (ch == 'g') {
                if ((r + c) & 1) gfx_fill_rect(px, py, sz, sz, col(30, 165, 65, dim));
                else             gfx_fill_rect(px, py, sz, sz, col(40, 200, 80, dim));
            } else if (ch == 'H') {
                gfx_fill_rect(px, py, sz, sz, col(130, 255, 150, dim));
                gfx_fill_rect(px + 12, py + 4, 6, 6, col(255, 255, 255, dim)); /* eye */
                gfx_fill_rect(px + 15, py + 6, 3, 3, col(0, 0, 0, dim));       /* pupil */
            } else if (ch == 'r') {
                gfx_fill_rect(px + 2, py + 4, sz - 4, sz - 6, col(230, 55, 55, dim));   /* apple */
                gfx_fill_rect(px + 5, py + 7, 4, 4, col(255, 170, 170, dim));           /* shine */
                gfx_fill_rect(px + sz / 2 - 1, py + 1, 4, 3, col(70, 200, 70, dim));    /* leaf */
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
    gfx_fill_rect(x + cell + cell / 4, y + cell + cell / 4, cell / 2, cell / 2, RGB565(40, 44, 70)); /* hub */
    for (int i = 0; i < 4; i++) {
        int ax = x + arms[i].gx * cell, ay = y + arms[i].gy * cell;
        gfx_fill_rect(ax + 2, ay + 2, cell - 4, cell - 4, col(arms[i].rgb[0], arms[i].rgb[1], arms[i].rgb[2], dim));
    }
}

static int card_x(int i) { return (GFX_WIDTH - (2 * CARD_W + CARD_GAP)) / 2 + i * (CARD_W + CARD_GAP); }

/* Draws one whole card; it fully overwrites its own rectangle, so it is
   safe to call again when the selection changes. */
static void draw_card(int i, int selected) {
    int x = card_x(i), y = CARD_Y;
    int bt = selected ? 4 : 2;
    uint16_t border = selected ? RGB565(s_items[i].accent[0], s_items[i].accent[1], s_items[i].accent[2])
                               : RGB565(50, 54, 80);
    uint16_t bg = selected ? RGB565(26, 30, 62) : RGB565(16, 18, 34);
    gfx_fill_rect(x, y, CARD_W, CARD_H, border);
    gfx_fill_rect(x + bt, y + bt, CARD_W - 2 * bt, CARD_H - 2 * bt, bg);

    int dim = !selected;
    int icon_y = y + 36;
    if (i == 0) draw_snake_icon(x + (CARD_W - 7 * 24) / 2, icon_y, 24, dim);
    else        draw_simon_icon(x + (CARD_W - 3 * 40) / 2, icon_y, 40, dim);

    text_centered(x + CARD_W / 2, y + 176, s_items[i].name,
                  selected ? COLOR_WHITE : TEXT_MUTED, 4);
    text_centered(x + CARD_W / 2, y + 224, s_items[i].subtitle,
                  selected ? RGB565(190, 196, 220) : RGB565(80, 84, 108), 2);
}

static void draw_menu_full(void) {
    gfx_fill_rect(0, 0, GFX_WIDTH, GFX_HEIGHT, MENU_BG);
    text_centered(GFX_WIDTH / 2, 36, "SELECT GAME", COLOR_WHITE, 4);
    for (int i = 0; i < MENU_ITEM_COUNT; i++) draw_card(i, i == s_menu_selection);
    text_centered(GFX_WIDTH / 2, 414, "LEFT/RIGHT: CHOOSE   ENTER: PLAY", TEXT_MUTED, 2);
}

static void menu_move_selection(int new_selection) {
    if (new_selection == s_menu_selection) return;
    int old = s_menu_selection;
    s_menu_selection = new_selection;
    draw_card(old, 0);
    draw_card(s_menu_selection, 1);
}


static Mode    s_mode = MODE_HOME;
static Mode    s_return_mode = MODE_HOME;   /* where leaving the current app goes */
static BmoApp *s_current = NULL;
static float   s_hold_timer = 0.0f;

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
    show_screen(s_return_mode);
}

static void dispatch_event(BmoEvent ev) {
    switch (s_mode) {
        case MODE_HOME: {
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
            /* games can always be quit from the desktop with Backspace; the timer and
               music apps get BACK themselves so they can step back one screen first */
            if (ev == BMO_EVENT_BACK && is_game(s_current)) leave_app();
            else s_current->handle_input(ev);
            break;
        case MODE_HOLD:
            if (ev == BMO_EVENT_CENTER || ev == BMO_EVENT_BACK) leave_app();
            break;
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("BMO Harness",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        GFX_WIDTH, GFX_HEIGHT, 0);
    g_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    g_canvas = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET, GFX_WIDTH, GFX_HEIGHT);

    s_menu_apps[0] = &bmo_game_snake;
    s_menu_apps[1] = &bmo_game_simon;

    /* The render target stays on the persistent canvas for the whole loop
       (including key handling); it only points at the window for the
       copy+present at the end of each frame. */
    SDL_SetRenderTarget(g_renderer, g_canvas);
    show_screen(MODE_HOME);

    printf("Arrow keys move, ENTER selects, BACKSPACE goes back, ESC quits.\n");

    Uint32 last_ticks = SDL_GetTicks();
    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                BmoEvent ev = BMO_EVENT_NONE;
                switch (e.key.keysym.sym) {
                    case SDLK_LEFT:      ev = BMO_EVENT_LEFT;   break;
                    case SDLK_RIGHT:     ev = BMO_EVENT_RIGHT;  break;
                    case SDLK_UP:        ev = BMO_EVENT_UP;     break;
                    case SDLK_DOWN:      ev = BMO_EVENT_DOWN;   break;
                    case SDLK_RETURN:
                    case SDLK_SPACE:     ev = BMO_EVENT_CENTER; break;
                    case SDLK_BACKSPACE: ev = BMO_EVENT_BACK;   break;
                    case SDLK_ESCAPE:    running = 0;           break;
                    default: break;
                }
                if (ev != BMO_EVENT_NONE) dispatch_event(ev);
            }
        }

        Uint32 now = SDL_GetTicks();
        float dt = (now - last_ticks) / 1000.0f;
        last_ticks = now;
        if (dt > 0.25f) dt = 0.25f;

        if (s_mode == MODE_HOME) {
            home_update();                      /* clock / battery / sound icon */
        } else if (s_mode == MODE_APP) {
            s_current->update(dt);
            s_current->draw();
            if (s_current->is_finished()) {
                if (is_game(s_current)) { s_mode = MODE_HOLD; s_hold_timer = 0.0f; }  /* show the final screen */
                else leave_app();                                                    /* timer / music: straight back */
            }
        } else if (s_mode == MODE_HOLD) {
            s_hold_timer += dt;
            if (s_hold_timer >= GAME_OVER_HOLD) leave_app();
        }

        SDL_SetRenderTarget(g_renderer, NULL);
        SDL_RenderCopy(g_renderer, g_canvas, NULL, NULL);
        {   /* desktop stand-in for the backlight: darken the finished frame */
            int b = sys_get_brightness();
            if (b < 100) {
                SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, (Uint8)((100 - b) * 255 / 100));
                SDL_RenderFillRect(g_renderer, NULL);
                SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
            }
        }
        SDL_RenderPresent(g_renderer);
        SDL_SetRenderTarget(g_renderer, g_canvas);

        SDL_Delay(10);
    }

    if (s_current) s_current->cleanup();
    SDL_DestroyTexture(g_canvas);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
