/* face_gfx.h — the shared drawing contract.
 *
 * P2 (face), P3 (menu) and P4 (games) draw ONLY through these functions.
 * Today gfx_spi.c implements them on the ILI9486 SPI panel.
 * Later P1's real driver (DMA2D + SPI-DMA) implements the same signatures,
 * and bmo_face.c does not change.
 */
#ifndef FACE_GFX_H
#define FACE_GFX_H

#include <stdint.h>

#define GFX_W 480                      /* landscape */
#define GFX_H 320

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* Power up the display in landscape. */
void gfx_init(void);

/* Fill a screen rectangle with one colour (background, eyelids). */
void gfx_fill_rect(int x, int y, int w, int h, uint16_t color);

/* Stamp an A4 stencil at (x, y) in `color`, blended over what is there.
 * a4: 4-bit alpha, 2 px per byte, high nibble = left pixel. w must be even. */
void gfx_blend_sprite(const uint8_t *a4, int w, int h, int x, int y, uint16_t color);

/* SPI extension — PROPOSED ADDITION, agree with P1.
 * An SPI panel cannot be read back, so "blend over what is there" needs a
 * RAM copy of the area. Draws between gfx_begin() and gfx_end() go into a
 * small RAM buffer; gfx_end() pushes the whole box in one transfer.
 * This is also what removes flicker: the screen only ever sees finished pixels. */
void gfx_begin(int x, int y, int w, int h);
void gfx_end(void);

/* Biggest box gfx_begin() accepts (w * h pixels). Draw larger areas in strips.
 * gfx_blend_sprite() with no box open strips big sprites by itself. */
#define GFX_BOX_MAX_PX (96 * 64)

/* Text (the team plan's gfx_text). 5x7 font in a 6x8 cell, times `scale`.
 * Lowercase shows as uppercase. `bg` fills each character cell.
 * Implemented once in gfx_font.c on top of the functions above. */
void gfx_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale);
int  gfx_text_width(const char *s, int scale);   /* pixels, for centring */

#endif
