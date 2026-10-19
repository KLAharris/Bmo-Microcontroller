#include "bmo_app_interface.h"
#include "bmo_gfx_interface.h"
#include "bmo_draw_util.h"
#include "bmo_music_interface.h"
#include <stdio.h>

/* Music app: a list of up to 6 tracks. Enter "plays" the highlighted one by
   calling music_play() (the real playback belongs to the audio owner) and
   shows NOW PLAYING on screen. */

#define MAX_TRACKS 6
#define ROW_X      80
#define ROW_W      640
#define ROW_H      52
#define ROW_PITCH  58
#define ROW_Y0     64

#define COL_BG      COLOR_BLACK
#define COL_ACCENT  RGB565(230, 90, 200)
#define COL_MUTED   RGB565(120, 125, 150)
#define COL_PLAYING RGB565(60, 220, 100)

static int s_count;
static int s_sel;
static int s_playing;   /* -1 = nothing chosen yet */
static int s_finished;

static void draw_row(int i) {
    int x = ROW_X, y = ROW_Y0 + i * ROW_PITCH;
    int sel = (i == s_sel);
    int bt = sel ? 3 : 2;
    uint16_t border = sel ? COL_ACCENT : RGB565(50, 54, 80);
    uint16_t bg     = sel ? RGB565(26, 30, 62) : RGB565(16, 18, 34);
    gfx_fill_rect(x, y, ROW_W, ROW_H, border);
    gfx_fill_rect(x + bt, y + bt, ROW_W - 2 * bt, ROW_H - 2 * bt, bg);

    /* number badge */
    gfx_fill_rect(x + 14, y + 10, 32, 32, sel ? COL_ACCENT : draw_scaled_color(230, 90, 200, 30));
    char num[16];
    snprintf(num, sizeof(num), "%d", i + 1);
    draw_text_scaled_centered(x + 30, y + 16, num, COLOR_WHITE, 3);

    draw_text_scaled(x + 64, y + 16, music_track_name(i), sel ? COLOR_WHITE : COL_MUTED, 3);

    if (i == s_playing) {   /* little equaliser bars on the track that is playing */
        int bx = x + ROW_W - 58, base = y + 40;
        gfx_fill_rect(bx,      base - 12, 8, 12, COL_PLAYING);
        gfx_fill_rect(bx + 12, base - 24, 8, 24, COL_PLAYING);
        gfx_fill_rect(bx + 24, base - 18, 8, 18, COL_PLAYING);
    }
}

static void draw_now_playing(void) {
    gfx_fill_rect(0, 414, GFX_WIDTH, 24, COL_BG);
    if (s_playing >= 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "NOW PLAYING: %s", music_track_name(s_playing));
        draw_text_scaled_centered(GFX_WIDTH / 2, 418, buf, COL_PLAYING, 2);
    } else {
        draw_text_scaled_centered(GFX_WIDTH / 2, 418, "PRESS ENTER TO PLAY", COL_MUTED, 2);
    }
}

void music_app_init(void) {
    s_count = music_track_count();
    if (s_count > MAX_TRACKS) s_count = MAX_TRACKS;
    s_sel = 0;
    s_playing = -1;
    s_finished = 0;

    gfx_fill_rect(0, 0, GFX_WIDTH, GFX_HEIGHT, COL_BG);
    draw_text_scaled_centered(GFX_WIDTH / 2, 16, "MUSIC", COLOR_WHITE, 4);
    for (int i = 0; i < s_count; i++) draw_row(i);
    draw_now_playing();
    draw_text_scaled_centered(GFX_WIDTH / 2, 452, "UP/DOWN: CHOOSE   ENTER: PLAY   BACK: EXIT", COL_MUTED, 2);
}

void music_app_handle_input(BmoEvent ev) {
    int old = s_sel;
    switch (ev) {
        case BMO_EVENT_UP:
            if (s_count > 0) s_sel = (s_sel + s_count - 1) % s_count;
            break;
        case BMO_EVENT_DOWN:
            if (s_count > 0) s_sel = (s_sel + 1) % s_count;
            break;
        case BMO_EVENT_CENTER:
            if (s_count > 0) {
                int prev = s_playing;
                s_playing = s_sel;
                music_play(s_sel);
                if (prev >= 0 && prev != s_sel) draw_row(prev);
                draw_row(s_sel);
                draw_now_playing();
            }
            return;
        case BMO_EVENT_BACK:
            s_finished = 1;
            return;
        default:
            return;
    }
    if (old != s_sel) { draw_row(old); draw_row(s_sel); }
}

void music_app_update(float dt) { (void)dt; }
void music_app_draw(void) { }
void music_app_cleanup(void) { }
int  music_app_is_finished(void) { return s_finished; }

BmoApp bmo_app_music = {
    .name = "Music",
    .init = music_app_init,
    .update = music_app_update,
    .draw = music_app_draw,
    .handle_input = music_app_handle_input,
    .cleanup = music_app_cleanup,
    .is_finished = music_app_is_finished
};
