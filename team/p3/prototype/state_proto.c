
#include "state_proto.h"

// เก็บสถานะปัจจุบันของ BMO
static BMO_State current_state;

// เริ่มต้น State Machine
void proto_state_init(void)
{
    current_state = STATE_IDLE;
}

// อ่านสถานะปัจจุบัน
BMO_State proto_state_get(void)
{
    return current_state;
}

// รับ Event แล้วตัดสินใจเปลี่ยน State
void proto_state_handle_event(BMO_Event event)
{
    switch (current_state)
    {
        case STATE_SLEEP:
            if (event == EVENT_WAKE) {
                current_state = STATE_IDLE;
            }
            break;

        case STATE_IDLE:
            if (event == EVENT_MENU_REQUEST) {
                current_state = STATE_MENU;
            }
            else if (event == EVENT_SHAKE) {
                current_state = STATE_REACTING;
            }
            break;

        case STATE_MENU:
            if (event == EVENT_SELECT) {
                current_state = STATE_APP;
            }
            else if (event == EVENT_BACK) {
                current_state = STATE_IDLE;
            }
            break;

        case STATE_APP:
            if (event == EVENT_BACK) {
                current_state = STATE_IDLE;
            }
            else if (event == EVENT_SHAKE) {
                current_state = STATE_REACTING;
            }
            break;

        case STATE_REACTING:
            if (event == EVENT_TIMEOUT) {
                current_state = STATE_IDLE;
            }
            break;

        default:
            break;
    }
}