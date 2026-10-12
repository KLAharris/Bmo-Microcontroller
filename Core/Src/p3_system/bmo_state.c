
#include "bmo_state.h"

// Current state of BMO
static bmo_state_t current_state;

// Initialize the state machine
void bmo_state_init(void)
{
    current_state = BMO_STATE_IDLE;
}

// Return the current state
bmo_state_t bmo_state_get(void)
{
    return current_state;
}

/* Change the current state */
static void bmo_state_transition(bmo_state_t next_state)
{
    // Do nothing if already in the requested state
    if (current_state == next_state) {
        return;
    }

    // Update the current state
    current_state = next_state;
}

/* Handle incoming events */
void bmo_state_handle_event(bmo_event_t event)
{
    switch (current_state)
    {
        case BMO_STATE_SLEEP:
            if (event == BMO_EVENT_WAKE) {
                bmo_state_transition(BMO_STATE_IDLE);
            }
            break;

        case BMO_STATE_IDLE:
            if (event == BMO_EVENT_MENU_REQUEST) {
                bmo_state_transition(BMO_STATE_MENU);
            }
            else if (event == BMO_EVENT_SHAKE) {
                bmo_state_transition(BMO_STATE_REACTING);
            }
            else if (event == BMO_EVENT_SLEEP_REQUEST) {
                bmo_state_transition(BMO_STATE_SLEEP);
            }
            break;

        case BMO_STATE_MENU:
            if (event == BMO_EVENT_SELECT) {
                bmo_state_transition(BMO_STATE_APP);
            }
            else if (event == BMO_EVENT_BACK) {
                bmo_state_transition(BMO_STATE_IDLE);
            }
            break;

        case BMO_STATE_APP:
            if (event == BMO_EVENT_BACK) {
                bmo_state_transition(BMO_STATE_IDLE);
            }
            else if (event == BMO_EVENT_SHAKE) {
                bmo_state_transition(BMO_STATE_REACTING);
            }
            break;

        case BMO_STATE_REACTING:
            if (event == BMO_EVENT_TIMEOUT) {
                bmo_state_transition(BMO_STATE_IDLE);
            }
            break;

        default:
            break;
    }
}