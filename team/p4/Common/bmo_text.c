/* bmo_text() - the one text call games and apps use.
 * Built only on gfx_fill_rect(), so it is identical on the desktop harness
 * and on the board (no dependency on P2's font or on SDL). */
#include "bmo_gfx_interface.h"
#include "bmo_draw_util.h"

void bmo_text(int x, int y, const char *str, uint16_t color) {
    draw_text_scaled(x, y, str, color, 2);
}
