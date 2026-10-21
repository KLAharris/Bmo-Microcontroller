
#include "bmo_menu.h"
#include "bmo_p3_music_adapter.h"

#include "bmo_state.h"
#include "bmo_face.h"
#include "bmo_input.h"
#include "bmo_game.h"
#include <stdint.h>

/* Timing configuration (milliseconds) */
#define BMO_REACTION_DURATION_MS    3000U
#define BMO_IDLE_SLEEP_TIMEOUT_MS   300000U
#define BMO_LISTENING_TIMEOUT_MS    10000U
#define BMO_PROCESSING_TIMEOUT_MS   5000U

/* Current state and timing */
static bmo_state_t current_state;
static uint32_t current_time_ms;
static uint32_t state_entered_ms;
static uint8_t timer_initialized;

/* Actions performed when entering a state */
static void bmo_state_on_enter(bmo_state_t state)
{
    switch (state)
    {
        case BMO_STATE_SLEEP:
            face_show(FACE_SLEEPY);
            face_set_active(1);
            /* TODO: Integrate power management */
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
            /* TODO: Integrate actual LCD menu */
            break;

        case BMO_STATE_APP:
            face_set_active(0);

            /* Start the selected game or music app */
            if (bmo_menu_get_category() == BMO_MENU_MUSIC)
            {
                bmo_p3_music_start();
            }
            else if (bmo_menu_get_category() == BMO_MENU_GAMES)
            {
                bmo_game_start(bmo_input_get_selected_game());
            }
            break;

        case BMO_STATE_LISTENING:
            face_set_active(1);
            face_show(FACE_IDLE);

            /* TODO P5: Start microphone recording */
            break;

        case BMO_STATE_PROCESSING:
            face_set_active(1);
            face_show(FACE_IDLE);

            /* TODO P5: Process recorded voice command */
            break;

        default:
            break;
    }
}

/* Actions performed when leaving a state */
static void bmo_state_on_exit(bmo_state_t state)
{
    switch (state)
    {
        case BMO_STATE_SLEEP:
            /* TODO: Restore system after wake */
            break;

        case BMO_STATE_MENU:
            /* TODO: Close actual LCD menu */
            break;

        case BMO_STATE_APP:
            /* Stop the active app before leaving APP */
            if (bmo_menu_get_category() == BMO_MENU_MUSIC)
            {
                bmo_p3_music_stop();
            }
            else
            {
                bmo_game_stop();
            }
            break;

        case BMO_STATE_LISTENING:
            /* TODO P5: Stop microphone recording */
            break;

        case BMO_STATE_PROCESSING:
            /* TODO P5: Cancel pending voice processing */
            break;

        default:
            break;
    }
}

/* Change state with exit and entry actions */
static void bmo_state_transition(bmo_state_t next_state)
{
    if (current_state == next_state)
    {
        return;
    }

    bmo_state_on_exit(current_state);

    current_state = next_state;
    state_entered_ms = current_time_ms;

    bmo_state_on_enter(current_state);
}

/* Initialize the state machine */
void bmo_state_init(void)
{
    current_state = BMO_STATE_IDLE;
    current_time_ms = 0U;
    state_entered_ms = 0U;
    timer_initialized = 0U;

    /* Initialize the Game Lifecycle Manager */
    bmo_game_init();
}

/* Return the current state */
bmo_state_t bmo_state_get(void)
{
    return current_state;
}

/* Handle incoming events */
void bmo_state_handle_event(bmo_event_t event)
{
    switch (current_state)
    {
        case BMO_STATE_SLEEP:
            if (event == BMO_EVENT_WAKE)
            {
                bmo_state_transition(BMO_STATE_IDLE);
            }
            break;

        case BMO_STATE_IDLE:
            if (event == BMO_EVENT_MENU_REQUEST)
            {
                bmo_state_transition(BMO_STATE_MENU);
            }
            else if (event == BMO_EVENT_SHAKE)
            {
                bmo_state_transition(BMO_STATE_REACTING);
            }
            else if (event == BMO_EVENT_SLEEP_REQUEST)
            {
                bmo_state_transition(BMO_STATE_SLEEP);
            }
            else if (event == BMO_EVENT_TALK_PRESSED)
            {
                bmo_state_transition(BMO_STATE_LISTENING);
            }
            break;

        case BMO_STATE_MENU:
            if (event == BMO_EVENT_SELECT)
            {
                bmo_state_transition(BMO_STATE_APP);
            }
            else if (event == BMO_EVENT_BACK)
            {
                bmo_state_transition(BMO_STATE_IDLE);
            }
            else if (event == BMO_EVENT_TALK_PRESSED)
            {
                bmo_state_transition(BMO_STATE_LISTENING);
            }
            break;

        case BMO_STATE_APP:
            if (event == BMO_EVENT_SELECT &&
                bmo_menu_get_category() == BMO_MENU_MUSIC)
            {
                bmo_p3_music_handle_enter();
            }
            else if (event == BMO_EVENT_BACK)
            {
                bmo_state_transition(BMO_STATE_IDLE);
            }

            /* Ignore SHAKE and TALK while an app is running */
            break;

        case BMO_STATE_REACTING:
            if (event == BMO_EVENT_TIMEOUT)
            {
                bmo_state_transition(BMO_STATE_IDLE);
            }
            else if (event == BMO_EVENT_TALK_PRESSED)
            {
                bmo_state_transition(BMO_STATE_LISTENING);
            }
            break;

        case BMO_STATE_LISTENING:
            if (event == BMO_EVENT_TALK_RELEASED)
            {
                bmo_state_transition(BMO_STATE_PROCESSING);
            }
            else if (event == BMO_EVENT_BACK ||
                     event == BMO_EVENT_TIMEOUT ||
                     event == BMO_EVENT_VOICE_ERROR)
            {
                bmo_state_transition(BMO_STATE_IDLE);
            }
            break;

        case BMO_STATE_PROCESSING:
            if (event == BMO_EVENT_VOICE_GAME ||
                event == BMO_EVENT_VOICE_MUSIC)
            {
                /*
                 * Voice command recognized.
                 * TODO: Select the correct menu category
                 * once the menu API is integrated.
                 */
                bmo_state_transition(BMO_STATE_MENU);
            }
            else if (event == BMO_EVENT_VOICE_UNKNOWN ||
                     event == BMO_EVENT_VOICE_ERROR ||
                     event == BMO_EVENT_TIMEOUT ||
                     event == BMO_EVENT_BACK)
            {
                bmo_state_transition(BMO_STATE_IDLE);
            }
            break;

        default:
            break;
    }
}

/* Update timers without blocking other tasks */
void bmo_state_tick(uint32_t now_ms)
{
    current_time_ms = now_ms;

    if (!timer_initialized)
    {
        state_entered_ms = now_ms;
        timer_initialized = 1U;
    }

    uint32_t elapsed_ms = now_ms - state_entered_ms;

    if (current_state == BMO_STATE_REACTING &&
        elapsed_ms >= BMO_REACTION_DURATION_MS)
    {
        bmo_state_handle_event(BMO_EVENT_TIMEOUT);
    }
    else if (current_state == BMO_STATE_IDLE &&
             elapsed_ms >= BMO_IDLE_SLEEP_TIMEOUT_MS)
    {
        bmo_state_handle_event(BMO_EVENT_SLEEP_REQUEST);
    }
    else if (current_state == BMO_STATE_LISTENING &&
             elapsed_ms >= BMO_LISTENING_TIMEOUT_MS)
    {
        bmo_state_handle_event(BMO_EVENT_TIMEOUT);
    }
    else if (current_state == BMO_STATE_PROCESSING &&
             elapsed_ms >= BMO_PROCESSING_TIMEOUT_MS)
    {
        bmo_state_handle_event(BMO_EVENT_TIMEOUT);
    }

    /* Update the active app without blocking */
    if (current_state == BMO_STATE_APP)
    {
        if (bmo_menu_get_category() == BMO_MENU_MUSIC)
        {
            bmo_p3_music_update(now_ms);
        }
        else
        {
            bmo_game_update(now_ms);
        }
    }
}
