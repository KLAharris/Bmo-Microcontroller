#include "bmo_gfx_interface.h"
#include <SDL2/SDL.h>
#include <stddef.h>

/* Set by main_harness.c. All draw calls land on g_canvas, a persistent
   off-screen texture, so nothing is cleared between frames unless a game
   explicitly clears it — matching the real display. */
SDL_Renderer *g_renderer = NULL;
SDL_Texture  *g_canvas   = NULL;

static void rgb565_to_rgb888(uint16_t c, Uint8 *r, Uint8 *g, Uint8 *b) {
    *r = (Uint8)(((c >> 11) & 0x1F) * 255 / 31);
    *g = (Uint8)(((c >> 5)  & 0x3F) * 255 / 63);
    *b = (Uint8)((c         & 0x1F) * 255 / 31);
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t color) {
    Uint8 r, g, b;
    rgb565_to_rgb888(color, &r, &g, &b);
    SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderFillRect(g_renderer, &rect);
}
