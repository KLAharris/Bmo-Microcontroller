
#include <stdio.h>
#include <assert.h>
#include "bmo_state.h"

int main(void)
{
    printf("=== BMO State Machine Tests ===\n");

    // Test 1: Initial state
    bmo_state_init();
    assert(bmo_state_get() == BMO_STATE_IDLE);
    printf("[PASS] Initial State: IDLE\n");

    // Test 2: IDLE -> MENU
    bmo_state_handle_event(BMO_EVENT_MENU_REQUEST);
    assert(bmo_state_get() == BMO_STATE_MENU);
    printf("[PASS] IDLE -> MENU\n");

    // Test 3: MENU -> APP
    bmo_state_handle_event(BMO_EVENT_SELECT);
    assert(bmo_state_get() == BMO_STATE_APP);
    printf("[PASS] MENU -> APP\n");

    // Test 4: APP -> REACTING
    bmo_state_handle_event(BMO_EVENT_SHAKE);
    assert(bmo_state_get() == BMO_STATE_REACTING);
    printf("[PASS] APP -> REACTING\n");

    // Test 5: REACTING -> IDLE
    bmo_state_handle_event(BMO_EVENT_TIMEOUT);
    assert(bmo_state_get() == BMO_STATE_IDLE);
    printf("[PASS] REACTING -> IDLE\n");

    // Test 6: MENU -> IDLE
    bmo_state_handle_event(BMO_EVENT_MENU_REQUEST);
    bmo_state_handle_event(BMO_EVENT_BACK);
    assert(bmo_state_get() == BMO_STATE_IDLE);
    printf("[PASS] MENU -> IDLE\n");

    // Test 7: APP -> IDLE
    bmo_state_handle_event(BMO_EVENT_MENU_REQUEST);
    bmo_state_handle_event(BMO_EVENT_SELECT);
    bmo_state_handle_event(BMO_EVENT_BACK);
    assert(bmo_state_get() == BMO_STATE_IDLE);
    printf("[PASS] APP -> IDLE\n");

    // Test 8: Ignore unsupported event
    bmo_state_handle_event(BMO_EVENT_SELECT);
    assert(bmo_state_get() == BMO_STATE_IDLE);
    printf("[PASS] Unsupported event ignored\n");

    // Test 9: IDLE -> SLEEP -> IDLE
    bmo_state_handle_event(BMO_EVENT_SLEEP_REQUEST);
    assert(bmo_state_get() == BMO_STATE_SLEEP);
    printf("[PASS] IDLE -> SLEEP\n");

    bmo_state_handle_event(BMO_EVENT_WAKE);
    assert(bmo_state_get() == BMO_STATE_IDLE);
    printf("[PASS] SLEEP -> IDLE\n");

    printf("\nAll 9 tests passed!\n");

    return 0;
}