
#include "bmo_state.h"
#include "bmo_face.h"
#include <stdint.h>

// Timing configuration (milliseconds)
#define BMO_REACTION_DURATION_MS   3000U
#define BMO_IDLE_SLEEP_TIMEOUT_MS  300000U

// Current state and timing
static bmo_state_t current_state;
static uint32_t current_time_ms;
static uint32_t state_entered_ms;
static uint8_t timer_initialized;

// Actions performed when entering a state
static void bmo_state_on_enter(bmo_state_t state)
{
    switch (state)
    {
        case BMO_STATE_SLEEP:
            face_show(FACE_SLEEPY);
            face_set_active(1);
            // TODO: Integrate power management
            break;

        case BMO_STATE_IDLE:
            face_show(FACE_IDLE);
            face_set_active(1);
            break;

        case BMO_STATE_REACTING:
            face_show(FACE_SURPRISED);
            face_set_active(1);
            break;

        case BMO_STATE_MENU:
            face_set_active(0);
            // TODO: Activate menu module
            break;

        case BMO_STATE_APP:
            face_set_active(0);
            // TODO: Initialize selected game
            break;

        default:
            break;
    }
}

// Actions performed when leaving a state
static void bmo_state_on_exit(bmo_state_t state)
{
    switch (state)
    {
        case BMO_STATE_SLEEP:
            // TODO: Restore system after wake
            break;

        case BMO_STATE_IDLE:
            // TODO: Prepare to leave idle screen
            break;

        case BMO_STATE_REACTING:
            // TODO: Finish reaction
            break;

        case BMO_STATE_MENU:
            // TODO: Close menu screen
            break;

        case BMO_STATE_APP:
            // TODO: Pause or exit current game
            break;

        default:
            break;
    }
}

// Change state with exit and entry actions
static void bmo_state_transition(bmo_state_t next_state)
{
    if (current_state == next_state) {
        return;
    }

    // Exit current state
    bmo_state_on_exit(current_state);

    // Update state and reset state timer
    current_state = next_state;
    state_entered_ms = current_time_ms;

    // Enter new state
    bmo_state_on_enter(current_state);
}

// Initialize the state machine
void bmo_state_init(void)
{
    current_state = BMO_STATE_IDLE;
    current_time_ms = 0U;
    state_entered_ms = 0U;
    timer_initialized = 0U;
}

// Return the current state
bmo_state_t bmo_state_get(void)
{
    return current_state;
}

// Handle incoming events
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

            // Ignore SHAKE while playing a game.
            // Remain in APP until the game exits.
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

// Update timers without blocking other tasks
void bmo_state_tick(uint32_t now_ms)
{
    current_time_ms = now_ms;

    // Initialize timer on the first tick
    if (!timer_initialized) {
        state_entered_ms = now_ms;
        timer_initialized = 1U;
    }

    uint32_t elapsed_ms = now_ms - state_entered_ms;

    // Return to IDLE after reacting for 3 seconds
    if (current_state == BMO_STATE_REACTING &&
        elapsed_ms >= BMO_REACTION_DURATION_MS)
    {
        bmo_state_handle_event(BMO_EVENT_TIMEOUT);
    }

    // Enter SLEEP after 5 minutes in IDLE
    else if (current_state == BMO_STATE_IDLE &&
             elapsed_ms >= BMO_IDLE_SLEEP_TIMEOUT_MS)
    {
        bmo_state_handle_event(BMO_EVENT_SLEEP_REQUEST);
    }
}
