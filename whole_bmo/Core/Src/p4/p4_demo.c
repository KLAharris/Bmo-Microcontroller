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
 * Real buttons (when input.h is in the project): yellow D-pad = w a s d, red = e,
 * blue = BACK (b); on the face, red or blue opens the home screen.
 * The HOME button jumps to the menu page from anywhere (on the menu it stays).
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
#include "bmo_system_interface.h"   /* sys_set_brightness() */
#include "p4_demo.h"

/* P1's button layer. If input.h is in the project the real buttons work too;
 * if not, nothing changes and only the serial keys are used. */
#if defined(__has_include)
#  if __has_include("input.h")
#    include "input.h"
#    define P4_HAVE_BUTTONS 1
#  endif
#endif
#ifndef P4_HAVE_BUTTONS
#  define P4_HAVE_BUTTONS 0
#endif

/* Not a real key (serial bytes are 0..255): the HOME button asks for the menu page. */
#define KEY_HOME_MENU 0x101

extern BmoApp bmo_game_snake;
extern BmoApp bmo_game_simon;
extern BmoApp bmo_app_timer;
extern BmoApp bmo_app_music;

/* Sleep mode: after this long with no key, no button and nothing running, the face falls asleep
 * (FACE_SLEEPY). Any key or button wakes it (that first press only wakes, it does nothing else).
 * For a quick test build with -DSLEEP_AFTER_MS=10000u (10 s). */
#ifndef SLEEP_AFTER_MS
#define SLEEP_AFTER_MS  300000u   /* 5 minutes */
#endif

#define HOLD_MS     1800u    /* a finished game's last screen stays up this long */
#define DT_MAX_S    0.25f    /* never feed an app a huge step after a slow pass */

typedef enum { P4_FACE, P4_APP, P4_HOLD, P4_LAUNCHER } P4State;

static P4State  s_state = P4_FACE;
static BmoApp  *s_app = NULL;
static int      s_is_game = 0;
static uint32_t s_last_ms = 0;
static uint32_t s_hold_start = 0;
static uint32_t s_last_input = 0;   /* last key/button press, or last time an app was on screen */
static int      s_talk = 0;         /* green TALK button pressed, not yet taken by main.c */

void p4_log(const char *s)
{
    HAL_UART_Transmit(&huart3, (uint8_t *)s, (uint16_t)strlen(s), 100);
}

#if P4_HAVE_BUTTONS
/* A real button press becomes the same character a key would send, so the buttons and the
 * serial keyboard drive exactly the same code below.  TALK (green) is for P5's voice: ignored here.
 *   yellow = w a s d     red  = e (on the face: opens the home screen)
 *   blue   = BACK = b (on the face: opens the home screen)
 *   HOME   = jump to the menu page (handled by go_to_menu below) */
static int button_getc(void)
{
    switch (input_poll()) {
        case INPUT_UP:    return 'w';
        case INPUT_DOWN:  return 's';
        case INPUT_LEFT:  return 'a';
        case INPUT_RIGHT: return 'd';
        case INPUT_ENTER: return (s_state == P4_FACE) ? 'm' : 'e';
        case INPUT_BACK:  return (s_state == P4_FACE) ? 'm' : 'b';
        case INPUT_HOME:  return KEY_HOME_MENU;
        case INPUT_TALK:  s_talk = 1; s_last_input = HAL_GetTick(); return -1;   /* push-to-talk: for main.c */
        default:          return -1;
    }
}
#endif

/* 1 once per press of the green TALK button (push-to-talk); main.c asks for it every loop pass. */
int p4_talk_pressed(void)
{
    int t = s_talk;
    s_talk = 0;
    return t;
}

/* 1 while Snake or Simon is running (or showing GAME OVER): main.c pauses the wake word. */
int p4_game_running(void) { return (s_state == P4_APP && s_is_game) || s_state == P4_HOLD; }

/* One received character (serial key or button), or -1 if none. Never blocks. */
static int uart_getc(void)
{
    if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_ORE)) __HAL_UART_CLEAR_OREFLAG(&huart3);
    if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_RXNE)) return (int)(huart3.Instance->RDR & 0xFFu);
#if P4_HAVE_BUTTONS
    return button_getc();
#else
    return -1;
#endif
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

/* HOME button: go to the menu page from wherever we are, and stay there if we already are. */
static void go_to_menu(void)
{
    switch (s_state) {
        case P4_FACE:
            start_launcher();
            break;
        case P4_LAUNCHER:
            if (!launcher_at_home()) {          /* game picker or a running app: back to the menu page */
                launcher_shutdown();
                launcher_open();
                p4_log("[p4] home screen\r\n");
            }                                   /* already on the menu page: nothing changes */
            break;
        default:                                /* an app started with keys 1..4 (running or game-over pause) */
            s_app->cleanup();
            s_app = NULL;
            start_launcher();
            break;
    }
}

void p4_demo_init(void)
{
    s_state = P4_FACE;
    s_app = NULL;
#if P4_HAVE_BUTTONS
    input_init();
#endif
    sys_set_brightness(100);   /* starts the backlight PWM at full brightness */
    s_last_input = HAL_GetTick();
    print_help();
}

void p4_demo_tick(uint32_t now_ms)
{
    for (int n = 0; n < 8; n++) {       /* drain a few keys per pass */
        int c = uart_getc();
        if (c < 0) break;
        s_last_input = now_ms;
        if (s_state == P4_FACE && face_current() == FACE_SLEEPY) {   /* asleep: this press only wakes BMO */
            face_show(FACE_IDLE);
            p4_log("[p4] woke up\r\n");
            continue;
        }
        if (c == KEY_HOME_MENU) { go_to_menu(); continue; }

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

    /* sleep timer: runs only while the face is on screen */
    if (s_state != P4_FACE) {
        s_last_input = now_ms;                      /* a game or the menu counts as activity */
    } else if (face_current() != FACE_SLEEPY && (now_ms - s_last_input) >= SLEEP_AFTER_MS) {
        face_show(FACE_SLEEPY);
        p4_log("[p4] sleeping\r\n");
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
