#include "bmo_p3_music_adapter.h"
#include "bmo_app_interface.h"

extern BmoApp bmo_app_music;

static uint8_t music_active = 0U;
static uint32_t last_update_ms = 0U;

void bmo_p3_music_start(void)
{
    if (music_active)
    {
        return;
    }

    music_active = 1U;
    last_update_ms = 0U;

    if (bmo_app_music.init)
    {
        bmo_app_music.init();
    }
}

void bmo_p3_music_stop(void)
{
    if (!music_active)
    {
        return;
    }

    if (bmo_app_music.cleanup)
    {
        bmo_app_music.cleanup();
    }

    music_active = 0U;
    last_update_ms = 0U;
}

void bmo_p3_music_update(uint32_t now_ms)
{
    if (!music_active)
    {
        return;
    }

    float dt = 0.0f;

    if (last_update_ms != 0U)
    {
        dt = (float)(now_ms - last_update_ms) / 1000.0f;
    }

    last_update_ms = now_ms;

    if (bmo_app_music.update)
    {
        bmo_app_music.update(dt);
    }

    if (bmo_app_music.draw)
    {
        bmo_app_music.draw();
    }
}

void bmo_p3_music_handle_direction(bmo_input_direction_t direction)
{
    if (!music_active || !bmo_app_music.handle_input)
    {
        return;
    }

    switch (direction)
    {
        case BMO_INPUT_UP:
            bmo_app_music.handle_input(BMO_EVENT_UP);
            break;

        case BMO_INPUT_DOWN:
            bmo_app_music.handle_input(BMO_EVENT_DOWN);
            break;

        default:
            break;
    }
}

void bmo_p3_music_handle_enter(void)
{
    if (!music_active || !bmo_app_music.handle_input)
    {
        return;
    }

    bmo_app_music.handle_input(BMO_EVENT_CENTER);
}
