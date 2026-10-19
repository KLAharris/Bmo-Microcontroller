#ifndef BMO_UI_SCALE_H
#define BMO_UI_SCALE_H

#include "bmo_gfx_interface.h"

/* Layouts in this project were designed for the final 800x480 panel. These
 * helpers let the same numbers fit any screen (today's 480x320 test panel,
 * the final 800x480 one) without a second set of layouts.
 *
 *   UI(v)    a length designed for 800x480, scaled to this screen
 *   UIX(v)   an x position designed for 800x480, scaled and centred
 *   UIY(v)   a y position designed for 800x480, scaled and centred
 *   UI_TS(n) a text scale (1,2,3,4...) designed for 800x480, shrunk to fit
 *            but never below 2 (the 5x7 font is unreadable at 1)
 *
 * The whole design is scaled by ONE factor (the tighter of width and height)
 * and centred, so squares stay square. On 800x480 every helper returns its
 * input unchanged, so nothing moves there.
 */

#define UI_BASE_W 800
#define UI_BASE_H 480

#define UI_SCALE_NUM_ (((long)GFX_WIDTH * UI_BASE_H < (long)GFX_HEIGHT * UI_BASE_W) \
                         ? ((long)GFX_WIDTH * UI_BASE_H) : ((long)GFX_HEIGHT * UI_BASE_W))
#define UI_SCALE_DEN_ ((long)UI_BASE_W * UI_BASE_H)

#define UI(v)   ((int)((((long)(v)) * UI_SCALE_NUM_ + UI_SCALE_DEN_ / 2) / UI_SCALE_DEN_))

#define UI_OX   ((GFX_WIDTH  - UI(UI_BASE_W)) / 2)
#define UI_OY   ((GFX_HEIGHT - UI(UI_BASE_H)) / 2)
#define UIX(v)  (UI_OX + UI(v))
#define UIY(v)  (UI_OY + UI(v))

#define UI_TS(n) ((n) <= 1 ? 1 : (UI(n) < 2 ? 2 : UI(n)))

/* Narrow screens (the 480-wide test panel) get shorter hint lines. */
#define UI_NARROW (GFX_WIDTH < 640)
#define UI_HINT(long_text, short_text) (UI_NARROW ? (short_text) : (long_text))

/* Height in pixels of text drawn at text scale `s` */
#define UI_TEXT_H(s) (7 * (s))

#endif
