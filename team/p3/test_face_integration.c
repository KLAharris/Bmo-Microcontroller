
#include <stdio.h>
#include <assert.h>
#include <stdint.h>

#include "bmo_state.h"
#include "bmo_face.h"
#include "lcd_test.h"
#include "fake_lcd.h"

static uint32_t test_time_ms = 0;

// Simulate the passage of time for P2 Face Renderer
static void run_face_ticks(uint32_t duration_ms)
{
    for (uint32_t i = 0; i < duration_ms; i++) {
        face_tick(test_time_ms++);
    }
}

// Verify that Face Renderer does not draw in MENU or APP
static void check_no_face_drawing(const char *mode)
{
    lcd_stats_reset();

    run_face_ticks(2000);

    assert(lcd_stats.px == 0);

    printf("[PASS] %s: Face Renderer draws 0 pixels\n", mode);
}

// Verify that Face Renderer resumes drawing after returning to IDLE
// IMPORTANT: Reset lcd_stats BEFORE sending the BACK event.
static void check_face_resumed(const char *source)
{
    assert(bmo_state_get() == BMO_STATE_IDLE);
    assert(face_current() == FACE_IDLE);
    assert(lcd_stats.px > 0);

    printf("[PASS] %s: Face Renderer resumed drawing (%ld pixels)\n",
           source, lcd_stats.px);
}

int main(void)
{
    printf("=== BMO P3 + P2 Integration Test ===\n");

    // Initialize Fake LCD in landscape mode (480 x 320)
    lcd_init();
    lcd_set_landscape();

    // Initialize P2 Face Renderer and P3 State Machine
    face_init(12345);
    bmo_state_init();

    // Test 1: Initial state
    assert(bmo_state_get() == BMO_STATE_IDLE);
    printf("[PASS] Initial state: IDLE\n");

    // Test 2: IDLE -> REACTING
    bmo_state_handle_event(BMO_EVENT_SHAKE);

    assert(bmo_state_get() == BMO_STATE_REACTING);
    assert(face_current() == FACE_SURPRISED);
    printf("[PASS] REACTING selects SURPRISED face\n");

    // Test 3: REACTING -> IDLE
    bmo_state_handle_event(BMO_EVENT_TIMEOUT);

    assert(bmo_state_get() == BMO_STATE_IDLE);
    assert(face_current() == FACE_IDLE);
    printf("[PASS] IDLE selects IDLE face\n");

    // Test 4: IDLE -> SLEEP
    bmo_state_handle_event(BMO_EVENT_SLEEP_REQUEST);

    assert(bmo_state_get() == BMO_STATE_SLEEP);
    assert(face_current() == FACE_SLEEPY);
    printf("[PASS] SLEEP selects SLEEPY face\n");

    // Test 5: SLEEP -> IDLE
    bmo_state_handle_event(BMO_EVENT_WAKE);

    assert(bmo_state_get() == BMO_STATE_IDLE);
    assert(face_current() == FACE_IDLE);
    printf("[PASS] WAKE restores IDLE face\n");

    // Test 6: IDLE -> MENU
    bmo_state_handle_event(BMO_EVENT_MENU_REQUEST);

    assert(bmo_state_get() == BMO_STATE_MENU);
    printf("[PASS] IDLE enters MENU\n");

    // Test 7: MENU stops face drawing
    check_no_face_drawing("MENU");

    // Test 8: MENU -> APP
    bmo_state_handle_event(BMO_EVENT_SELECT);

    assert(bmo_state_get() == BMO_STATE_APP);
    printf("[PASS] MENU enters APP\n");

    // Test 9: APP keeps face drawing disabled
    check_no_face_drawing("APP");

    // Test 10: APP -> IDLE resumes face drawing
    lcd_stats_reset();

    bmo_state_handle_event(BMO_EVENT_BACK);

    check_face_resumed("APP -> IDLE");
    printf("[PASS] APP returns to IDLE face\n");

    // Test 11: IDLE -> MENU again
    bmo_state_handle_event(BMO_EVENT_MENU_REQUEST);

    assert(bmo_state_get() == BMO_STATE_MENU);
    printf("[PASS] IDLE enters MENU again\n");

    // Test 12: MENU still stops face drawing
    check_no_face_drawing("MENU (second entry)");

    // Test 13: MENU -> IDLE resumes face drawing
    lcd_stats_reset();

    bmo_state_handle_event(BMO_EVENT_BACK);

    check_face_resumed("MENU -> IDLE");
    printf("[PASS] MENU returns to IDLE face\n");

    printf("\nAll integration checks passed!\n");

    return 0;
}