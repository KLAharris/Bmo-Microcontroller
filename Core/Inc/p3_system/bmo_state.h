
#ifndef BMO_STATE_H
#define BMO_STATE_H

// Main states of BMO
typedef enum {
    BMO_STATE_SLEEP,
    BMO_STATE_IDLE,
    BMO_STATE_REACTING,
    BMO_STATE_MENU,
    BMO_STATE_APP
} bmo_state_t;

// Prototype events (subject to team API agreement)
typedef enum {
    BMO_EVENT_WAKE,
    BMO_EVENT_SLEEP_REQUEST,
    BMO_EVENT_MENU_REQUEST,
    BMO_EVENT_SELECT,
    BMO_EVENT_BACK,
    BMO_EVENT_SHAKE,
    BMO_EVENT_TIMEOUT
} bmo_event_t;

// Initialize state machine
void bmo_state_init(void);

// Process an incoming event from the main loop
void bmo_state_handle_event(bmo_event_t event);

// Get current state
bmo_state_t bmo_state_get(void);

#endif