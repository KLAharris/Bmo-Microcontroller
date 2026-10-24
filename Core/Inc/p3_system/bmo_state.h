
#ifndef BMO_STATE_H
#define BMO_STATE_H

#include <stdint.h>

/* Main states of BMO */
typedef enum
{
    BMO_STATE_SLEEP,
    BMO_STATE_IDLE,
    BMO_STATE_REACTING,
    BMO_STATE_MENU,
    BMO_STATE_APP,

    /* Audio / Voice states */
    BMO_STATE_LISTENING,
    BMO_STATE_PROCESSING
} bmo_state_t;

/* Events handled by the state machine */
typedef enum
{
    BMO_EVENT_WAKE,
    BMO_EVENT_SLEEP_REQUEST,
    BMO_EVENT_MENU_REQUEST,
    BMO_EVENT_SELECT,
    BMO_EVENT_BACK,
    BMO_EVENT_SHAKE,
    BMO_EVENT_TIMEOUT,

    /* Audio / Voice events */
    BMO_EVENT_TALK_PRESSED,
    BMO_EVENT_TALK_RELEASED,
    BMO_EVENT_VOICE_GAME,
    BMO_EVENT_VOICE_MUSIC,
    BMO_EVENT_VOICE_UNKNOWN,
    BMO_EVENT_VOICE_ERROR
} bmo_event_t;

/* Initialize the state machine */
void bmo_state_init(void);

/* Process an incoming event */
void bmo_state_handle_event(bmo_event_t event);

/* Get the current state */
bmo_state_t bmo_state_get(void);

/* Update state timers using milliseconds */
void bmo_state_tick(uint32_t now_ms);

#endif /* BMO_STATE_H */
