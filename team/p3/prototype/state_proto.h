#ifndef STATE_PROTO_H
#define STATE_PROTO_H

// BMO States
typedef enum {
    STATE_SLEEP,
    STATE_IDLE,
    STATE_REACTING,
    STATE_MENU,
    STATE_APP
} BMO_State;

// Mock Events
typedef enum {
    EVENT_WAKE,
    EVENT_SLEEP_REQUEST,
    EVENT_MENU_REQUEST,
    EVENT_SELECT,
    EVENT_BACK,
    EVENT_SHAKE,
    EVENT_TIMEOUT
} BMO_Event;

// State Machine Functions
void proto_state_init(void);
void proto_state_handle_event(BMO_Event event);
BMO_State proto_state_get(void);

#endif