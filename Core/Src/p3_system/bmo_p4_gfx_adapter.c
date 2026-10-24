#include <stdint.h>
#include "p2_face/face_gfx.h"

/*
 * P4 graphics text adapter.
 *
 * P4: bmo_p4_gfx_text(x, y, str, color)
 * P2: gfx_text(x, y, str, fg, bg, scale)
 *
 * Scale 2 matches P4's expected 12 px character width.
 */
void bmo_p4_gfx_text(int x, int y, const char *str, uint16_t color)
{
    gfx_text(x, y, str, color, 0x0000, 2);
}
