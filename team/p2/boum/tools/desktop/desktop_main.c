/* desktop_main.c — run BMO's face on the laptop.
 *
 * Keys:
 *   B            blink now                (face_blink)
 *   arrow keys   look left/right/up/down  (face_set_gaze)
 *   C            look centre, resume the gaze pattern (face_set_gaze(0,0))
 *   A            toggle active            (face_set_active: simulates menu/game)
 *   N / P        next / previous face     (face_show), name in the window title
 *   1 / 2        normal speed / quarter speed (to judge motion)
 *   D            outline the boxes sent to the screen (draw cost at a glance)
 *   S            save frame to shot.bmp
 *   Esc / close  quit
 *
 * Optional: ./bmo_face_desktop --shot out.bmp 3000
 *   runs 3000 ms without a window refresh loop, saves one frame, exits.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include "face_gfx.h"
#include "bmo_face.h"

void gfx_sdl_present(uint32_t outline_ms);
int  gfx_sdl_save_bmp(const char *path);
void gfx_sdl_set_title(const char *title);

static int quarter, outlines;

static void set_title(face_id_t f)
{
    char title[96];
    SDL_snprintf(title, sizeof title, "BMO face - %s (Rosto-%.2s)%s%s", face_name(f), face_name(f),
                 quarter ? "  [1/4 speed]" : "", outlines ? "  [boxes]" : "");
    gfx_sdl_set_title(title);
}

static void step_face(int dir)
{
    face_id_t f = (face_id_t)((face_current() + FACE_COUNT + dir) % FACE_COUNT);
    face_show(f);
    set_title(f);
}

int main(int argc, char **argv)
{
    gfx_init();
    face_init((uint32_t)SDL_GetPerformanceCounter());   /* fixed number here = repeatable run */
    set_title(face_current());

    if (argc == 4 && SDL_strcmp(argv[1], "--shot") == 0) {
        uint32_t end = SDL_GetTicks() + (uint32_t)atoi(argv[3]);
        while (SDL_GetTicks() < end) { face_tick(SDL_GetTicks()); SDL_Delay(1); }
        return gfx_sdl_save_bmp(argv[2]) == 0 ? 0 : 1;
    }

    /* The face runs on its own clock so it can be slowed down. */
    uint32_t real_last = SDL_GetTicks(), sim_ms = 0, frac = 0;
    int running = 1, active = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type != SDL_KEYDOWN) continue;
            switch (e.key.keysym.sym) {
            case SDLK_ESCAPE: running = 0; break;
            case SDLK_b:      face_blink(); break;
            case SDLK_LEFT:   face_set_gaze(-9, 0); break;
            case SDLK_RIGHT:  face_set_gaze( 9, 0); break;
            case SDLK_UP:     face_set_gaze( 0, -3); break;
            case SDLK_DOWN:   face_set_gaze( 0,  3); break;
            case SDLK_c:      face_set_gaze( 0,  0); break;
            case SDLK_a:      active = !active; face_set_active(active);
                              if (!active) gfx_fill_rect(0, 0, GFX_W, GFX_H, RGB565(40, 40, 40));
                              break;
            case SDLK_n:      step_face(+1); break;
            case SDLK_p:      step_face(-1); break;
            case SDLK_1:      quarter = 0; set_title(face_current()); break;
            case SDLK_2:      quarter = 1; set_title(face_current()); break;
            case SDLK_d:      outlines = !outlines; set_title(face_current()); break;
            case SDLK_s:      gfx_sdl_save_bmp("shot.bmp"); puts("saved shot.bmp"); break;
            default: break;
            }
        }
        uint32_t now = SDL_GetTicks(), dt = now - real_last;
        real_last = now;
        if (quarter) { frac += dt; sim_ms += frac / 4; frac %= 4; }
        else sim_ms += dt;
        face_tick(sim_ms);
        gfx_sdl_present(outlines ? (quarter ? 320u : 80u) : 0u);   /* about 4 face frames */
        SDL_Delay(5);
    }
    SDL_Quit();
    return 0;
}
