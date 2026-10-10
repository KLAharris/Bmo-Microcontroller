
#include <stdio.h>
#include <assert.h>
#include "state_proto.h"

int main(void)
{
    printf("=== BMO State Machine Test ===\n");

    // Test 1: Initialize
    proto_state_init();
    assert(proto_state_get() == STATE_IDLE);
    printf("[PASS] Initial State: IDLE\n");

    // Test 2: IDLE -> MENU
    proto_state_handle_event(EVENT_MENU_REQUEST);
    assert(proto_state_get() == STATE_MENU);
    printf("[PASS] IDLE -> MENU\n");

    // Test 3: MENU -> APP
    proto_state_handle_event(EVENT_SELECT);
    assert(proto_state_get() == STATE_APP);
    printf("[PASS] MENU -> APP\n");

    // Test 4: APP -> REACTING
    proto_state_handle_event(EVENT_SHAKE);
    assert(proto_state_get() == STATE_REACTING);
    printf("[PASS] APP -> REACTING\n");

    // Test 5: REACTING -> IDLE
    proto_state_handle_event(EVENT_TIMEOUT);
    assert(proto_state_get() == STATE_IDLE);
    printf("[PASS] REACTING -> IDLE\n");

    // Test 6: MENU -> IDLE
    proto_state_handle_event(EVENT_MENU_REQUEST);
    proto_state_handle_event(EVENT_BACK);
    assert(proto_state_get() == STATE_IDLE);
    printf("[PASS] MENU -> IDLE\n");

    // Test 7: APP -> IDLE
    proto_state_handle_event(EVENT_MENU_REQUEST);
    proto_state_handle_event(EVENT_SELECT);
    proto_state_handle_event(EVENT_BACK);
    assert(proto_state_get() == STATE_IDLE);
    printf("[PASS] APP -> IDLE\n");

    // Test 8: Ignore unsupported event
    proto_state_handle_event(EVENT_SELECT);
    assert(proto_state_get() == STATE_IDLE);
    printf("[PASS] Unsupported event ignored\n");

    printf("\nAll 8 tests passed!\n");

    return 0;
}