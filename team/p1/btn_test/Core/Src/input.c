/* input.c — one input layer for BMO.
 *
 * Sources (both can be on at the same time):
 *   INPUT_USE_UART : laptop keyboard over the ST-LINK virtual COM port (USART3, PD8/PD9)
 *   INPUT_USE_GPIO : real buttons, pull-up, active low, 20 ms debounce
 *
 * Menus and games only call input_poll(). Nothing above this file knows which source fired.
 * No interrupts and no DMA are used, so no extra CubeMX peripheral setup is needed.
 *
 * Pin map (GPIO_Input, Pull-up, set the User Label in CubeMX):
 *   BTN_LEFT  PG0  (CN9 pin 29)   yellow
 *   BTN_RIGHT PG1  (CN9 pin 30)   yellow
 *   BTN_UP    PG2  (CN8 pin 14)   yellow
 *   BTN_DOWN  PG3  (CN8 pin 16)   yellow
 *   BTN_ENTER PE12 (CN10 pin 26)  red    : confirm
 *   BTN_HOME  PE14 (CN10 pin 28)  blue   : back to menu
 *   BTN_TALK  PE15 (CN10 pin 30)  green  : push-to-talk
 *   Silver reset button: wired NRST (CN8 pin 5) to GND, no pin and no code.
 */
#include "input.h"
#include "main.h"     /* BTN_xxx_Pin / BTN_xxx_GPIO_Port come from the CubeMX User Labels */
#include "usart.h"    /* huart3 */
#include <stdio.h>

#ifndef INPUT_USE_UART
#define INPUT_USE_UART 1
#endif
#ifndef INPUT_USE_GPIO
#define INPUT_USE_GPIO 1
#endif
#ifndef INPUT_DEBUG_PRINT
#define INPUT_DEBUG_PRINT 1   /* printf each event to the terminal (printf is already on USART3) */
#endif

#define BTN_ACTIVE_LEVEL GPIO_PIN_RESET   /* button to GND = pressed = 0 */
#define DEBOUNCE_MS      20u
#define QUEUE_SIZE       8u               /* must be a power of 2 */

/* ---------- tiny event queue ---------- */
static input_event_t q[QUEUE_SIZE];
static uint8_t q_head, q_tail;

static void push(input_event_t ev)
{
    uint8_t next = (uint8_t)((q_head + 1u) & (QUEUE_SIZE - 1u));
    if (next == q_tail) return;           /* full: drop the newest event */
    q[q_head] = ev;
    q_head = next;
#if INPUT_DEBUG_PRINT
    {
        static const char *const names[] =
            { "NONE", "LEFT", "RIGHT", "UP", "DOWN", "ENTER", "HOME", "TALK" };
        printf("[input] %s\r\n", names[ev]);
    }
#endif
}

/* ---------- UART keyboard backend ---------- */
#if INPUT_USE_UART
/* Keys: w a s d = up/left/down/right (arrow keys also work), Enter or Space = red/confirm,
 *       h = blue/home, p = green/push-to-talk.  (Esc alone is not used: it is the start of arrow sequences.) */
static void uart_scan(void)
{
    static uint8_t esc = 0;               /* 0 normal, 1 got ESC, 2 got ESC [ */

    while (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_RXNE)) {
        uint8_t c = (uint8_t)(huart3.Instance->RDR & 0xFFu);

        if (esc == 2) {
            esc = 0;
            if      (c == 'A') push(INPUT_UP);
            else if (c == 'B') push(INPUT_DOWN);
            else if (c == 'C') push(INPUT_RIGHT);
            else if (c == 'D') push(INPUT_LEFT);
            continue;
        }
        if (esc == 1) {
            if (c == '[') { esc = 2; continue; }
            esc = 0;                      /* not an arrow sequence: handle c normally below */
        }
        if (c == 0x1B) { esc = 1; continue; }

        switch (c) {
        case 'w': case 'W': push(INPUT_UP);    break;
        case 's': case 'S': push(INPUT_DOWN);  break;
        case 'a': case 'A': push(INPUT_LEFT);  break;
        case 'd': case 'D': push(INPUT_RIGHT); break;
        case '\r': case ' ': push(INPUT_ENTER); break;
        case 'h': case 'H': push(INPUT_HOME);  break;
        case 'p': case 'P': push(INPUT_TALK);  break;
        default: break;
        }
    }
    if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_ORE)) {
        __HAL_UART_CLEAR_OREFLAG(&huart3);   /* a long SPI draw can overrun; just clear it */
    }
}
#endif

/* ---------- real GPIO button backend ---------- */
#if INPUT_USE_GPIO
typedef struct {
    GPIO_TypeDef  *port;
    uint16_t       pin;
    input_event_t  ev;
    uint8_t        raw;       /* last raw reading (1 = pressed) */
    uint8_t        stable;    /* debounced state */
    uint32_t       t_change;  /* when raw last changed */
} btn_t;

static btn_t btns[] = {
    { BTN_LEFT_GPIO_Port,  BTN_LEFT_Pin,  INPUT_LEFT,  0, 0, 0 },
    { BTN_RIGHT_GPIO_Port, BTN_RIGHT_Pin, INPUT_RIGHT, 0, 0, 0 },
    { BTN_UP_GPIO_Port,    BTN_UP_Pin,    INPUT_UP,    0, 0, 0 },
    { BTN_DOWN_GPIO_Port,  BTN_DOWN_Pin,  INPUT_DOWN,  0, 0, 0 },
    { BTN_ENTER_GPIO_Port, BTN_ENTER_Pin, INPUT_ENTER, 0, 0, 0 },
    { BTN_HOME_GPIO_Port,  BTN_HOME_Pin,  INPUT_HOME,  0, 0, 0 },
    { BTN_TALK_GPIO_Port,  BTN_TALK_Pin,  INPUT_TALK,  0, 0, 0 },
};

static void gpio_scan(void)
{
    uint32_t now = HAL_GetTick();
    for (unsigned i = 0; i < sizeof btns / sizeof btns[0]; i++) {
        btn_t *b = &btns[i];
        uint8_t pressed = (HAL_GPIO_ReadPin(b->port, b->pin) == BTN_ACTIVE_LEVEL);

        if (pressed != b->raw) {                      /* reading changed: restart the timer */
            b->raw = pressed;
            b->t_change = now;
        } else if (pressed != b->stable && (now - b->t_change) >= DEBOUNCE_MS) {
            b->stable = pressed;                      /* held steady long enough */
            if (pressed) push(b->ev);                 /* event on press only, no auto-repeat */
        }
    }
}
#endif

/* ---------- public API ---------- */
int input_is_down(input_event_t btn)
{
#if INPUT_USE_GPIO
    for (unsigned i = 0; i < sizeof btns / sizeof btns[0]; i++)
        if (btns[i].ev == btn) return btns[i].stable;
#endif
    (void)btn;
    return 0;   /* the UART keyboard has no key-up, so it can only send TALK as a one-shot event */
}

void input_init(void)
{
    q_head = q_tail = 0;
}

input_event_t input_poll(void)
{
#if INPUT_USE_UART
    uart_scan();
#endif
#if INPUT_USE_GPIO
    gpio_scan();
#endif
    if (q_tail == q_head) return INPUT_NONE;
    input_event_t ev = q[q_tail];
    q_tail = (uint8_t)((q_tail + 1u) & (QUEUE_SIZE - 1u));
    return ev;
}
