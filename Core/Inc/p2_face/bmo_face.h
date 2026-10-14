/* bmo_face.h — P2 face renderer API (what P3 calls).
 *
 * Call from the main loop only, never from an interrupt.
 * Depends only on face_gfx.h, so it runs the same on the SPI stub,
 * the SDL2 desktop stub, and P1's real driver later.
 */
#ifndef BMO_FACE_H
#define BMO_FACE_H

#include <stdint.h>

/* Expression parts for the old face_set() API: one value each, and every
 * combination shows FACE_IDLE. New code uses face_show(face_id_t). */
typedef enum { FACE_EYES_NORMAL = 0, FACE_EYES_COUNT } face_eyes_t;
typedef enum { FACE_MOUTH_SMILE = 0, FACE_MOUTH_COUNT } face_mouth_t;
typedef enum { FACE_OVERLAY_NONE = 0, FACE_OVERLAY_COUNT } face_overlay_t;

/* Whole faces from Figma (CLAUDE.md §15), in bmo_faces.h table order.
 * Every face is alive: breathing, gaze, blinks and its own signature motion
 * (CLAUDE.md §18; tuning table at the top of bmo_face.c). */
typedef enum {
    FACE_IDLE,          /* Rosto-01 default     */
    FACE_HAPPY,         /* Rosto-02             */
    FACE_SAD,           /* Rosto-03             */
    FACE_CONTENT,       /* Rosto-04             */
    FACE_SHY,           /* Rosto-05             */
    FACE_SCARED,        /* Rosto-07             */
    FACE_ANGRY,         /* Rosto-10             */
    FACE_SURPRISED,     /* Rosto-13             */
    FACE_PLAYFUL,       /* Rosto-19  > 3 <      */
    FACE_LOVE,          /* Rosto-21             */
    FACE_DIZZY,         /* Rosto-24             */
    FACE_KNOCKED_OUT,   /* Rosto-25  X_X        */
    FACE_SLEEPY,        /* Rosto-29  - _ -      */
    FACE_COUNT
} face_id_t;

void face_init(uint32_t seed);        /* draw default face; seed the randomness      */
void face_tick(uint32_t now_ms);      /* call every loop pass; returns fast          */

void face_set(face_eyes_t eyes, face_mouth_t mouth, face_overlay_t overlay);
                                      /* old API: today every combination = FACE_IDLE */
void face_show(face_id_t face);       /* switch whole face: eyes close, face swaps,  */
                                      /* new eyes open (takes ~0.1-0.3 s of ticks).  */
                                      /* Only changed pixels are sent.               */
face_id_t   face_current(void);         /* the face shown, or being changed to       */
int         face_changing(void);        /* 1 while a face_show() change is running   */
const char *face_name(face_id_t face);  /* e.g. "02 Happy"; "?" if out of range      */

void face_blink(void);                /* force a blink now (e.g. on wake word)       */
void face_set_gaze(int dx, int dy);   /* look offset in px (P3/IMU); 0,0 = resume     */

/* Optional, call once after face_init(): a free-running cycle counter
 * (e.g. DWT->CYCCNT) so face_tick() watches its real time and never runs
 * past ~25 ms. Without it face_tick() trusts its timing model. */
void     face_set_clock(uint32_t (*cycles)(void), uint32_t cycles_per_us);
uint32_t face_frames(void);           /* +1 each time face_tick() runs a frame        */

void face_set_active(int on);         /* 0 = menu/game owns the screen, face draws   */
                                      /* nothing. 1 = face owns it, full redraw.     */

#endif
