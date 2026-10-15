
#include <stdint.h>

#include "bmo_state.h"
#include "bmo_face.h"
#include "fake_lcd.h"
#include "lcd_test.h"

/* Initialize P2 Face Renderer and P3 State Machine */
void simulator_init(void)
{
    /* Initialize Fake LCD */
    lcd_init();

    /* Set screen to landscape mode: 480 x 320 */
    lcd_set_landscape();

    /* Initialize Face Renderer */
    face_init(12345);

    /* Initialize P3 State Machine */
    bmo_state_init();
}

/* Send an event to the real P3 State Machine */
void simulator_send_event(int event)
{
    bmo_state_handle_event((bmo_event_t)event);
}

/* Read the current P3 state */
int simulator_get_state(void)
{
    return (int)bmo_state_get();
}

/* Read the current P2 face */
int simulator_get_face(void)
{
    return (int)face_current();
}

/* Advance state timers and face animation */
void simulator_tick(uint32_t now_ms)
{
    /* Update P3 State Machine timers */
    bmo_state_tick(now_ms);

    /* Update P2 Face Renderer */
    face_tick(now_ms);
}

/* Read the simulated LCD pixel buffer */
const uint16_t *simulator_get_screen(void)
{
    return lcd_screen;
}
