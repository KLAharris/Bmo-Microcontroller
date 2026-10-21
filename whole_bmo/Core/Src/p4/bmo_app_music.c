#include "bmo_app_interface.h"
#include "bmo_gfx_interface.h"
#include "bmo_draw_util.h"
#include "bmo_ui_scale.h"
#include "bmo_music_interface.h"
#include <stdio.h>

/* Music app: a list of up to 6 tracks. Enter on a track plays it (via
   music_play(); the real playback belongs to the audio owner). Enter on the
   track that is already playing stops it (music_stop()). Choosing another
   track switches to it. Leaving the app stops the music. */

#define MAX_TRACKS 6
/* Sizes are designed for 800x480 and scaled to the real screen (UI()). */
#define ROW_X      UIX(80)
#define ROW_W      UI(640)
#define ROW_H      UI(52)
#define ROW_PITCH  UI(58)
#define ROW_Y0     UIY(64)

#define COL_BG      COLOR_BLACK
#define COL_ACCENT  RGB565(230, 90, 200)
#define COL_MUTED   RGB565(120, 125, 150)
#define COL_PLAYING RGB565(60, 220, 100)

static int s_count;
static int s_sel;
static int s_playing;   /* -1 = nothing chosen yet */
static int s_finished;
static int s_stopped;   /* 1 = the user stopped a track (shows STOPPED) */

static void draw_row(int i) {
    int x = ROW_X, y = ROW_Y0 + i * ROW_PITCH;
    int sel = (i == s_sel);
    int bt = sel ? 3 : 2;
    uint16_t border = sel ? COL_ACCENT : RGB565(50, 54, 80);
    uint16_t bg     = sel ? RGB565(26, 30, 62) : RGB565(16, 18, 34);
    gfx_fill_rect(x, y, ROW_W, ROW_H, border);
    gfx_fill_rect(x + bt, y + bt, ROW_W - 2 * bt, ROW_H - 2 * bt, bg);

    /* number badge */
    int badge = UI(32), ts = UI_TS(3);
    int text_y = y + (ROW_H - UI_TEXT_H(ts) + 1) / 2;
    gfx_fill_rect(x + UI(14), y + (ROW_H - badge) / 2, badge, badge,
                  sel ? COL_ACCENT : draw_scaled_color(230, 90, 200, 30));
    char num[16];
    snprintf(num, sizeof(num), "%d", i + 1);
    draw_text_scaled_centered(x + UI(14) + badge / 2, text_y, num, COLOR_WHITE, ts);

    draw_text_scaled(x + UI(64), text_y, music_track_name(i), sel ? COLOR_WHITE : COL_MUTED, ts);

    if (i == s_playing) {   /* little equaliser bars on the track that is playing */
        int bx = x + ROW_W - UI(58), base = y + UI(40);
        int bw = UI(8), step = UI(12);
        gfx_fill_rect(bx,            base - UI(12), bw, UI(12), COL_PLAYING);
        gfx_fill_rect(bx + step,     base - UI(24), bw, UI(24), COL_PLAYING);
        gfx_fill_rect(bx + 2 * step, base - UI(18), bw, UI(18), COL_PLAYING);
    }
}

static void draw_now_playing(void) {
    int ts = UI_TS(2);
    int by = UIY(414), ty = UIY(418);
    int bh = UI(24);
    int need = 2 * (ty - by) + UI_TEXT_H(ts);
    if (bh < need) bh = need;
    gfx_fill_rect(0, by, GFX_WIDTH, bh, COL_BG);
    if (s_playing >= 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "NOW PLAYING: %s", music_track_name(s_playing));
        draw_text_scaled_centered(GFX_WIDTH / 2, ty, buf, COL_PLAYING, ts);
    } else if (s_stopped) {
        draw_text_scaled_centered(GFX_WIDTH / 2, ty, "STOPPED", COL_MUTED, ts);
    } else {
        draw_text_scaled_centered(GFX_WIDTH / 2, ty, "PRESS ENTER TO PLAY", COL_MUTED, ts);
    }
}

static void music_app_init(void) {
    s_count = music_track_count();
    if (s_count > MAX_TRACKS) s_count = MAX_TRACKS;
    s_sel = 0;
    s_playing = -1;
    s_finished = 0;
    s_stopped = 0;

    gfx_fill_rect(0, 0, GFX_WIDTH, GFX_HEIGHT, COL_BG);
    draw_text_scaled_centered(GFX_WIDTH / 2, UIY(16), "MUSIC", COLOR_WHITE, UI_TS(4));
    for (int i = 0; i < s_count; i++) draw_row(i);
    draw_now_playing();
    draw_text_scaled_centered(GFX_WIDTH / 2, UIY(452),
                              UI_HINT("UP/DOWN: CHOOSE   ENTER: PLAY/STOP   BACK: EXIT", "U/D CHOOSE  ENTER PLAY/STOP"),
                              COL_MUTED, UI_TS(2));
}

static void music_app_handle_input(BmoEvent ev) {
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
                if (prev == s_sel) {            /* Enter on the playing track = stop */
                    music_stop();
                    s_playing = -1;
                    s_stopped = 1;
                } else {                        /* play (or switch to) the highlighted track */
                    s_playing = s_sel;
                    s_stopped = 0;
                    music_play(s_sel);
                }
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

static void music_app_update(float dt) { (void)dt; }
static void music_app_draw(void) { }
static void music_app_cleanup(void) {
    music_stop();   /* leaving the app stops the music */
}
static int music_app_is_finished(void) { return s_finished; }

BmoApp bmo_app_music = {
    .name = "Music",
    .init = music_app_init,
    .update = music_app_update,
    .draw = music_app_draw,
    .handle_input = music_app_handle_input,
    .cleanup = music_app_cleanup,
    .is_finished = music_app_is_finished
};
