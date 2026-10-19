/* p4_demo.c - hand the screen between P2's face and P4's games/apps.
 *
 *   FACE  --(key 1..4)-->  APP running  --(game over / BACK / q)-->  FACE
 *   FACE  --(key m)------>  HOME SCREEN (Games / Music / Timer) --(BACK / q)-->  FACE
 *
 * While an app runs, face_set_active(0) tells P2's face to stop drawing, so
 * only one thing ever writes to the screen. When the app ends,
 * face_set_active(1) makes the face repaint the whole screen itself, so no
 * game pixels are left behind.
 *
 * Keys (typed in a serial terminal, 115200 8N1, no echo needed):
 *   on the face:  m home screen   1 Snake   2 Simon   3 Timer   4 Music   h help
 *   in the home screen or an app:  w a s d = up/left/down/right    e or Enter = centre/select
 *                 b = back                        q = quit the app now
 */
#include "main.h"
#include "usart.h"           /* huart3 = ST-LINK virtual COM port */
#include <stdlib.h>
#include <string.h>

#include "bmo_face.h"        /* face_set_active() */
#include "bmo_app_interface.h"
#include "bmo_event.h"
#include "bmo_launcher.h"
#include "p4_demo.h"

extern BmoApp bmo_game_snake;
extern BmoApp bmo_game_simon;
extern BmoApp bmo_app_timer;
extern BmoApp bmo_app_music;

#define HOLD_MS     1800u    /* a finished game's last screen stays up this long */
#define DT_MAX_S    0.25f    /* never feed an app a huge step after a slow pass */

typedef enum { P4_FACE, P4_APP, P4_HOLD, P4_LAUNCHER } P4State;

static P4State  s_state = P4_FACE;
static BmoApp  *s_app = NULL;
static int      s_is_game = 0;
static uint32_t s_last_ms = 0;
static uint32_t s_hold_start = 0;

void p4_log(const char *s)
{
    HAL_UART_Transmit(&huart3, (uint8_t *)s, (uint16_t)strlen(s), 100);
}

/* One received character, or -1 if none. Never blocks. */
static int uart_getc(void)
{
    if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_ORE)) __HAL_UART_CLEAR_OREFLAG(&huart3);
    if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_RXNE)) return (int)(huart3.Instance->RDR & 0xFFu);
    return -1;
}

static BmoEvent key_to_event(int c)
{
    switch (c) {
        case 'w': case 'W': return BMO_EVENT_UP;
        case 's': case 'S': return BMO_EVENT_DOWN;
        case 'a': case 'A': return BMO_EVENT_LEFT;
        case 'd': case 'D': return BMO_EVENT_RIGHT;
        case 'e': case 'E': case ' ': case '\r': case '\n': return BMO_EVENT_CENTER;
        case 'b': case 'B': case 0x08: case 0x7F:           return BMO_EVENT_BACK;
        default:                                            return BMO_EVENT_NONE;
    }
}

static void print_help(void)
{
    p4_log("\r\n[p4] on the face:  m home screen   1 Snake   2 Simon   3 Timer   4 Music   h help\r\n"
           "[p4] in the home screen or an app:  w a s d move   e/Enter select   b back   q quit\r\n");
}

static void start_app(BmoApp *app, const char *name)
{
    srand(HAL_GetTick());
    face_set_active(0);                 /* the app owns the screen now */
    s_app = app;
    s_is_game = (app == &bmo_game_snake || app == &bmo_game_simon);
    app->init();                        /* each app clears the screen itself */
    s_last_ms = HAL_GetTick();
    s_state = P4_APP;
    p4_log("[p4] started "); p4_log(name); p4_log("\r\n");
}

static void stop_app(void)
{
    s_app->cleanup();
    s_app = NULL;
    face_set_active(1);                 /* the face repaints the whole screen */
    s_state = P4_FACE;
    p4_log("[p4] back to the face\r\n");
}

static void start_launcher(void)
{
    srand(HAL_GetTick());
    face_set_active(0);                 /* the home screen owns the screen now */
    launcher_open();                    /* draws the home screen */
    s_last_ms = HAL_GetTick();
    s_state = P4_LAUNCHER;
    p4_log("[p4] home screen\r\n");
}

static void stop_launcher(void)
{
    launcher_shutdown();                /* clean up an app that is still running */
    face_set_active(1);                 /* the face repaints the whole screen */
    s_state = P4_FACE;
    p4_log("[p4] back to the face\r\n");
}

void p4_demo_init(void)
{
    s_state = P4_FACE;
    s_app = NULL;
    print_help();
}

void p4_demo_tick(uint32_t now_ms)
{
    for (int n = 0; n < 8; n++) {       /* drain a few keys per pass */
        int c = uart_getc();
        if (c < 0) break;

        if (s_state == P4_FACE) {
            switch (c) {
                case '1': start_app(&bmo_game_snake, "Snake"); break;
                case '2': start_app(&bmo_game_simon, "Simon"); break;
                case '3': start_app(&bmo_app_timer,  "Timer"); break;
                case '4': start_app(&bmo_app_music,  "Music"); break;
                case 'm': case 'M':    start_launcher();       break;
                case 'h': case 'H': case '?': print_help();    break;
                default: break;
            }
        } else if (s_state == P4_APP) {
            BmoEvent ev = key_to_event(c);
            if (c == 'q' || c == 'Q') { stop_app(); break; }
            if (ev == BMO_EVENT_BACK && s_is_game) { stop_app(); break; }   /* games: BACK leaves */
            if (ev != BMO_EVENT_NONE) s_app->handle_input(ev);              /* timer/music get BACK */
        } else if (s_state == P4_LAUNCHER) {
            if (c == 'q' || c == 'Q') { stop_launcher(); break; }
            BmoEvent ev = key_to_event(c);
            if (ev != BMO_EVENT_NONE) launcher_event(ev);
            if (launcher_leave_requested()) { stop_launcher(); break; }      /* BACK on the home screen */
        } else {                        /* P4_HOLD: any select/back ends the pause early */
            BmoEvent ev = key_to_event(c);
            if (ev == BMO_EVENT_CENTER || ev == BMO_EVENT_BACK || c == 'q') { stop_app(); break; }
        }
    }

    if (s_state == P4_APP) {
        float dt = (float)(now_ms - s_last_ms) / 1000.0f;
        s_last_ms = now_ms;
        if (dt > DT_MAX_S) dt = DT_MAX_S;

        s_app->update(dt);
        s_app->draw();
        if (s_app->is_finished()) {
            if (s_is_game) { s_state = P4_HOLD; s_hold_start = now_ms; }   /* show GAME OVER briefly */
            else           stop_app();                                     /* timer/music: straight back */
        }
    } else if (s_state == P4_HOLD) {
        if (now_ms - s_hold_start >= HOLD_MS) stop_app();
    } else if (s_state == P4_LAUNCHER) {
        float dt = (float)(now_ms - s_last_ms) / 1000.0f;
        s_last_ms = now_ms;
        if (dt > DT_MAX_S) dt = DT_MAX_S;
        launcher_update(dt);
    }
}
