#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include "bmo_gfx_interface.h"
#include "bmo_app_interface.h"
#include "bmo_event.h"
#include "bmo_draw_util.h"
#include "bmo_launcher.h"
#include "bmo_system_interface.h"

extern SDL_Renderer *g_renderer;
extern SDL_Texture  *g_canvas;

extern BmoApp bmo_game_snake;
extern BmoApp bmo_game_simon;
extern BmoApp bmo_app_timer;
extern BmoApp bmo_app_music;

/* Screen flow (home screen, game picker, running app) lives in Common/bmo_launcher.c,
 * the same code the board build uses. */

static BmoApp *find_app(const char *name) {
    if (!strcmp(name, "snake")) return &bmo_game_snake;
    if (!strcmp(name, "simon")) return &bmo_game_simon;
    if (!strcmp(name, "timer")) return &bmo_app_timer;
    if (!strcmp(name, "music")) return &bmo_app_music;
    return NULL;
}

int main(int argc, char *argv[]) {
    BmoApp *direct = NULL;
    if (argc > 1) {
        direct = find_app(argv[1]);
        if (!direct) {
            fprintf(stderr, "Unknown app '%s'. Use: snake, simon, timer or music.\n", argv[1]);
            return 1;
        }
    }

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

    /* The render target stays on the persistent canvas for the whole loop
       (including key handling); it only points at the window for the
       copy+present at the end of each frame. */
    SDL_SetRenderTarget(g_renderer, g_canvas);
    if (direct) {
        launcher_open_app(direct);
        printf("Screen %dx%d, running '%s' directly.\n", GFX_WIDTH, GFX_HEIGHT, argv[1]);
    } else {
        launcher_open();
    }

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
                if (ev != BMO_EVENT_NONE) launcher_event(ev);
            }
        }

        Uint32 now = SDL_GetTicks();
        float dt = (now - last_ticks) / 1000.0f;
        last_ticks = now;
        if (dt > 0.25f) dt = 0.25f;

        launcher_update(dt);

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

    launcher_shutdown();
    SDL_DestroyTexture(g_canvas);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
