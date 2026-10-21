/* gfx_sdl.c — desktop implementation of face_gfx.h (plan Step 2).
 *
 * Same contract as gfx_spi.c on the board, but draws into a laptop window.
 * The A4 blend is plain software: slow, does not matter on a laptop.
 * bmo_face.c compiles against this unchanged.
 */
#include <SDL.h>
#include "face_gfx.h"

#define SCALE 2                                   /* window = 960 x 640 */

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;
static uint16_t      fb[GFX_H][GFX_W];            /* RGB565, readable: no region needed */
static int           cx0, cy0, cx1 = GFX_W, cy1 = GFX_H;   /* clip: open box or whole screen */
static int           in_box;

/* Every box sent (gfx_end, or a fill with no box open), for the D overlay. */
#define SENT_MAX 512
static SDL_Rect      sent[SENT_MAX];
static uint32_t      sent_ms[SENT_MAX];
static int           n_sent;

static void record(int x, int y, int w, int h)
{
    if (n_sent == SENT_MAX) n_sent = 0;
    sent[n_sent] = (SDL_Rect){ x, y, w, h };
    sent_ms[n_sent++] = SDL_GetTicks();
}

static int inside(int x, int y) { return x >= cx0 && x < cx1 && y >= cy0 && y < cy1; }

static uint16_t mix(uint16_t fg, uint16_t bg, int a)
{
    int r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * (15 - a) + 7) / 15;
    int g = (((fg >> 5)  & 0x3F) * a + ((bg >> 5)  & 0x3F) * (15 - a) + 7) / 15;
    int b = (( fg        & 0x1F) * a + ( bg        & 0x1F) * (15 - a) + 7) / 15;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void gfx_init(void)
{
    SDL_Init(SDL_INIT_VIDEO);
    win = SDL_CreateWindow("BMO face (desktop stub)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           GFX_W * SCALE, GFX_H * SCALE, 0);
    ren = SDL_CreateRenderer(win, -1, 0);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, GFX_W, GFX_H);
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (!in_box) record(x, y, w, h);
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            if (inside(i, j)) fb[j][i] = color;
}

void gfx_blend_sprite(const uint8_t *a4, int w, int h, int x, int y, uint16_t color)
{
    if (!in_box) record(x, y, w, h);
    int sy0 = cy0 - y > 0 ? cy0 - y : 0, sy1 = cy1 - y < h ? cy1 - y : h;   /* clipped rows/cols only */
    int sx0 = cx0 - x > 0 ? cx0 - x : 0, sx1 = cx1 - x < w ? cx1 - x : w;
    for (int sy = sy0; sy < sy1; sy++)
        for (int sx = sx0; sx < sx1; sx++) {
            int px = x + sx, py = y + sy;
            if (!inside(px, py)) continue;
            int p = sy * w + sx;
            int a = (p & 1) ? (a4[p >> 1] & 0x0F) : (a4[p >> 1] >> 4);   /* high nibble = left */
            if (a) fb[py][px] = (a == 15) ? color : mix(color, fb[py][px], a);
        }
}

/* The desktop framebuffer is readable, so regions need no buffer. They still
 * clip, like gfx_spi.c, so strips drawn here look exactly as on the board. */
void gfx_begin(int x, int y, int w, int h)
{
    cx0 = x < 0 ? 0 : x;                 cy0 = y < 0 ? 0 : y;
    cx1 = x + w > GFX_W ? GFX_W : x + w; cy1 = y + h > GFX_H ? GFX_H : y + h;
    in_box = 1;
}

void gfx_end(void)
{
    if (in_box) record(cx0, cy0, cx1 - cx0, cy1 - cy0);
    cx0 = cy0 = 0; cx1 = GFX_W; cy1 = GFX_H;
    in_box = 0;
}

/* Desktop only: window title (face name). */
void gfx_sdl_set_title(const char *title)
{
    SDL_SetWindowTitle(win, title);
}

/* Desktop only: show the framebuffer. Called by desktop_main.c each loop.
 * outline_ms > 0: outline in red every box sent in the last outline_ms. */
void gfx_sdl_present(uint32_t outline_ms)
{
    SDL_UpdateTexture(tex, NULL, fb, GFX_W * 2);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    if (outline_ms) {
        uint32_t now = SDL_GetTicks();
        SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
        for (int i = 0; i < SENT_MAX; i++) {
            if (!sent[i].w || now - sent_ms[i] > outline_ms) continue;
            SDL_Rect r = { sent[i].x * SCALE, sent[i].y * SCALE, sent[i].w * SCALE, sent[i].h * SCALE };
            SDL_RenderDrawRect(ren, &r);
        }
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    }
    SDL_RenderPresent(ren);
}

/* Desktop only: save the current frame (used by --shot). */
int gfx_sdl_save_bmp(const char *path)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(fb, GFX_W, GFX_H, 16, GFX_W * 2,
                                                        SDL_PIXELFORMAT_RGB565);
    int r = SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
    return r;
}
