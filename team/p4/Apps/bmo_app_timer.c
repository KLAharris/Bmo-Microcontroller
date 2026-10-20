#include "bmo_app_interface.h"
#include "bmo_gfx_interface.h"
#include "bmo_draw_util.h"
#include "bmo_ui_scale.h"
#include "bmo_system_interface.h"
#include <stdio.h>

/* Timer app: choose "TIMER" (counts up from zero, like a stopwatch) or
   "COUNTDOWN" (set hh:mm:ss, counts down to zero). Times are shown as big
   seven-segment digits and only changed digits are redrawn. */

/* All sizes are designed for 800x480 and scaled to the real screen (UI()). */
#define DIGIT_W    UI(66)
#define DIGIT_H    UI(120)
#define DIGIT_T    UI(12)
#define COLON_W    UI(22)
#define DIGIT_GAP  UI(12)
#define TIME_Y     UIY(140)
#define MAX_SECONDS 359999UL /* 99:59:59 */

#define COL_BG      COLOR_BLACK
#define COL_GHOST   RGB565(22, 26, 44)
#define COL_TEXT    RGB565(200, 205, 225)
#define COL_MUTED   RGB565(120, 125, 150)
#define COL_ACCENT  RGB565(90, 200, 255)
#define COL_PAUSED  RGB565(255, 200, 40)
#define COL_DONE    RGB565(240, 60, 60)

typedef enum { SCREEN_MODE_SELECT, SCREEN_STOPWATCH, SCREEN_COUNTDOWN } Screen;
typedef enum { RUN_IDLE, RUN_RUNNING, RUN_PAUSED, RUN_DONE } RunState;

static Screen        s_screen;
static int           s_mode_sel;      /* 0 = timer, 1 = countdown */
static int           s_finished;
static RunState      s_run;
static unsigned long s_start_ms;      /* sys_millis() when the current run began */
static unsigned long s_elapsed_ms;    /* stopwatch: time banked before the current run */
static unsigned long s_left_ms;       /* countdown: time left at the start of the current run */
static int           s_cd_set[3];     /* countdown setting: hours, minutes, seconds */
static int           s_cd_field;      /* which of the three is being edited */
static int           s_shown[6];      /* digits currently on screen (-1 = unknown) */
static uint16_t      s_shown_color;
static unsigned long s_blink_ms;
static int           s_blink_on;

/* ---------- layout helpers ---------- */
static int pair_w(void)   { return 2 * (DIGIT_W + DIGIT_GAP) + COLON_W + DIGIT_GAP; }
static int time_x0(void)  { return (GFX_WIDTH - (2 * pair_w() + 2 * (DIGIT_W + DIGIT_GAP) - DIGIT_GAP)) / 2; }
static int digit_x(int i) { return time_x0() + (i / 2) * pair_w() + (i % 2) * (DIGIT_W + DIGIT_GAP); }
static int colon_x(int k) { return time_x0() + k * pair_w() + 2 * (DIGIT_W + DIGIT_GAP); }

static void draw_colons(void) {
    for (int k = 0; k < 2; k++)
        draw_7seg_colon(colon_x(k), TIME_Y, COLON_W, DIGIT_H, DIGIT_T, COL_MUTED);
}

static void invalidate_digits(void) {
    for (int i = 0; i < 6; i++) s_shown[i] = -1;
}

/* Redraw only the digits whose value (or colour) changed */
static void draw_time_digits(unsigned long total_secs, uint16_t color) {
    if (total_secs > MAX_SECONDS) total_secs = MAX_SECONDS;
    int h = (int)(total_secs / 3600), m = (int)((total_secs / 60) % 60), s = (int)(total_secs % 60);
    int v[6] = { h / 10, h % 10, m / 10, m % 10, s / 10, s % 10 };
    for (int i = 0; i < 6; i++) {
        if (s_shown[i] != v[i] || s_shown_color != color) {
            gfx_fill_rect(digit_x(i), TIME_Y, DIGIT_W, DIGIT_H, COL_BG);
            draw_7seg_digit(digit_x(i), TIME_Y, DIGIT_W, DIGIT_H, DIGIT_T, v[i], color, COL_GHOST);
            s_shown[i] = v[i];
        }
    }
    s_shown_color = color;
}

/* Clear a full-width band, then draw centred text in it. Band and text
   position are given in 800x480 design units; the band is never made
   shorter than the (unscaled) text needs. */
static void draw_band_text(int band_y, int band_h, int text_y, int text_scale,
                           const char *s, uint16_t color) {
    int ts = UI_TS(text_scale);
    int by = UIY(band_y), ty = UIY(text_y);
    int bh = UI(band_h);
    int need = 2 * (ty - by) + UI_TEXT_H(ts);
    if (bh < need) bh = need;
    gfx_fill_rect(0, by, GFX_WIDTH, bh, COL_BG);
    draw_text_scaled_centered(GFX_WIDTH / 2, ty, s, color, ts);
}
static void draw_title(const char *s) { draw_band_text(16, 40, 24, 3, s, COL_TEXT); }
static void draw_label(const char *s, uint16_t color) { draw_band_text(292, 30, 296, 3, s, color); }
static void draw_hint(const char *s) { draw_band_text(440, 22, 444, 2, s, COL_MUTED); }

/* ---------- mode select screen ---------- */
#define MCARD_W   UI(340)
#define MCARD_H   UI(220)
#define MCARD_GAP UI(40)
#define MCARD_Y   UIY(110)

static void draw_mode_card(int i, int sel) {
    static const char *names[2] = { "TIMER", "COUNTDOWN" };
    static const char *subs[2]  = { UI_HINT("COUNT UP FROM ZERO", "COUNT UP"),
                                    UI_HINT("COUNT DOWN TO ZERO", "COUNT DOWN") };
    int x = (GFX_WIDTH - (2 * MCARD_W + MCARD_GAP)) / 2 + i * (MCARD_W + MCARD_GAP);
    int y = MCARD_Y;
    int bt = sel ? 4 : 2;
    uint16_t border = sel ? COL_ACCENT : RGB565(50, 54, 80);
    uint16_t bg     = sel ? RGB565(26, 30, 62) : RGB565(16, 18, 34);
    gfx_fill_rect(x, y, MCARD_W, MCARD_H, border);
    gfx_fill_rect(x + bt, y + bt, MCARD_W - 2 * bt, MCARD_H - 2 * bt, bg);

    /* small clock preview: 00:00 for the timer, 05:00 for the countdown */
    const int mw = UI(28), mh = UI(52), mt = UI(6), mc = UI(12), mg = UI(6);
    int px = x + (MCARD_W - (4 * mw + mc + 4 * mg)) / 2, py = y + UI(34);
    uint16_t on  = sel ? COL_ACCENT : draw_scaled_color(90, 200, 255, 35);
    uint16_t off = sel ? RGB565(34, 40, 76) : RGB565(24, 28, 50);
    int d[4] = { 0, i == 1 ? 5 : 0, 0, 0 };
    draw_7seg_digit(px,                          py, mw, mh, mt, d[0], on, off);
    draw_7seg_digit(px + (mw + mg),              py, mw, mh, mt, d[1], on, off);
    draw_7seg_colon(px + 2 * (mw + mg),          py, mc, mh, mt, on);
    draw_7seg_digit(px + 2 * (mw + mg) + mc + mg, py, mw, mh, mt, d[2], on, off);
    draw_7seg_digit(px + 3 * (mw + mg) + mc + mg, py, mw, mh, mt, d[3], on, off);

    draw_text_scaled_centered(x + MCARD_W / 2, y + UI(130), names[i], sel ? COLOR_WHITE : COL_MUTED, UI_TS(3));
    draw_text_scaled_centered(x + MCARD_W / 2, y + UI(168), subs[i],
                              sel ? RGB565(190, 196, 220) : RGB565(80, 84, 108), UI_TS(2));
}

static void enter_mode_select(void) {
    s_screen = SCREEN_MODE_SELECT;
    gfx_fill_rect(0, 0, GFX_WIDTH, GFX_HEIGHT, COL_BG);
    draw_text_scaled_centered(GFX_WIDTH / 2, UIY(36), "CHOOSE MODE", COL_TEXT, UI_TS(3));
    draw_mode_card(0, s_mode_sel == 0);
    draw_mode_card(1, s_mode_sel == 1);
    draw_hint(UI_HINT("LEFT/RIGHT: CHOOSE   ENTER: OPEN   BACK: HOME", "L/R CHOOSE  ENTER OPEN"));
}

/* ---------- stopwatch ("TIMER") ---------- */
static unsigned long stopwatch_elapsed(void) {
    return s_run == RUN_RUNNING ? s_elapsed_ms + (sys_millis() - s_start_ms) : s_elapsed_ms;
}
static uint16_t stopwatch_color(void) {
    return s_run == RUN_RUNNING ? COL_ACCENT : (s_run == RUN_PAUSED ? COL_PAUSED : COL_TEXT);
}
static void stopwatch_refresh_digits(void) {
    draw_time_digits(stopwatch_elapsed() / 1000UL, stopwatch_color());
}
static void stopwatch_labels(void) {
    const char *l = s_run == RUN_RUNNING ? "RUNNING" : (s_run == RUN_PAUSED ? "PAUSED" : "READY");
    draw_label(l, stopwatch_color());
    draw_hint(UI_HINT("ENTER: START/PAUSE   DOWN: RESET   BACK: BACK", "ENTER GO  DOWN RESET"));
}
static void enter_stopwatch(void) {
    s_screen = SCREEN_STOPWATCH;
    s_run = RUN_IDLE;
    s_elapsed_ms = 0;
    gfx_fill_rect(0, 0, GFX_WIDTH, GFX_HEIGHT, COL_BG);
    draw_title("TIMER");
    draw_colons();
    invalidate_digits();
    stopwatch_refresh_digits();
    stopwatch_labels();
}
static void stopwatch_input(BmoEvent ev) {
    switch (ev) {
        case BMO_EVENT_CENTER:
            if (s_run == RUN_RUNNING) {
                s_elapsed_ms = stopwatch_elapsed();
                s_run = RUN_PAUSED;
            } else {
                s_start_ms = sys_millis();
                s_run = RUN_RUNNING;
            }
            break;
        case BMO_EVENT_DOWN:
            s_elapsed_ms = 0;
            s_run = RUN_IDLE;
            break;
        case BMO_EVENT_BACK:
            enter_mode_select();
            return;
        default:
            return;
    }
    stopwatch_refresh_digits();
    stopwatch_labels();
}

/* ---------- countdown ---------- */
static unsigned long countdown_set_secs(void) {
    return (unsigned long)s_cd_set[0] * 3600UL + (unsigned long)s_cd_set[1] * 60UL + (unsigned long)s_cd_set[2];
}
static unsigned long countdown_left_ms(void) {
    if (s_run == RUN_RUNNING) {
        unsigned long used = sys_millis() - s_start_ms;
        return used >= s_left_ms ? 0 : s_left_ms - used;
    }
    return s_left_ms;
}
static uint16_t countdown_color(void) {
    switch (s_run) {
        case RUN_RUNNING: return COL_ACCENT;
        case RUN_PAUSED:  return COL_PAUSED;
        case RUN_DONE:    return COL_DONE;
        default:          return COL_TEXT;
    }
}
static void countdown_refresh_digits(void) {
    unsigned long secs = (s_run == RUN_IDLE) ? countdown_set_secs() : (countdown_left_ms() + 999UL) / 1000UL;
    draw_time_digits(secs, countdown_color());
}
/* underline under the hours / minutes / seconds pair being edited */
static void countdown_underlines(void) {
    for (int f = 0; f < 3; f++) {
        int x = digit_x(2 * f);
        int w = digit_x(2 * f + 1) + DIGIT_W - x;
        gfx_fill_rect(x, TIME_Y + DIGIT_H + UI(12), w, UI(6), (s_run == RUN_IDLE && f == s_cd_field) ? COL_ACCENT : COL_BG);
    }
}
static void countdown_labels(void) {
    switch (s_run) {
        case RUN_IDLE:
            draw_label("SET TIME", COL_TEXT);
            draw_hint(UI_HINT("LEFT/RIGHT: FIELD   UP/DOWN: CHANGE   ENTER: START", "L/R FIELD  U/D SET"));
            break;
        case RUN_RUNNING:
            draw_label("RUNNING", COL_ACCENT);
            draw_hint(UI_HINT("ENTER: PAUSE   DOWN: CANCEL   BACK: BACK", "ENTER PAUSE  DOWN STOP"));
            break;
        case RUN_PAUSED:
            draw_label("PAUSED", COL_PAUSED);
            draw_hint(UI_HINT("ENTER: RESUME   DOWN: CANCEL   BACK: BACK", "ENTER GO  DOWN STOP"));
            break;
        case RUN_DONE:
            draw_label("TIME UP!", COL_DONE);
            draw_hint(UI_HINT("ENTER: AGAIN   BACK: BACK", "ENTER AGAIN"));
            break;
    }
    countdown_underlines();
}
static void enter_countdown(void) {
    s_screen = SCREEN_COUNTDOWN;
    s_run = RUN_IDLE;
    s_cd_field = 0;
    gfx_fill_rect(0, 0, GFX_WIDTH, GFX_HEIGHT, COL_BG);
    draw_title("COUNTDOWN");
    draw_colons();
    invalidate_digits();
    countdown_refresh_digits();
    countdown_labels();
}
static void countdown_change_field(int delta) {
    int max = (s_cd_field == 0) ? 99 : 59;
    int v = s_cd_set[s_cd_field] + delta;
    if (v < 0) v = max;
    if (v > max) v = 0;
    s_cd_set[s_cd_field] = v;
}
static void countdown_input(BmoEvent ev) {
    if (ev == BMO_EVENT_BACK) { enter_mode_select(); return; }
    switch (s_run) {
        case RUN_IDLE:
            if (ev == BMO_EVENT_LEFT)       s_cd_field = (s_cd_field + 2) % 3;
            else if (ev == BMO_EVENT_RIGHT) s_cd_field = (s_cd_field + 1) % 3;
            else if (ev == BMO_EVENT_UP)    countdown_change_field(+1);
            else if (ev == BMO_EVENT_DOWN)  countdown_change_field(-1);
            else if (ev == BMO_EVENT_CENTER && countdown_set_secs() > 0) {
                s_left_ms = countdown_set_secs() * 1000UL;
                s_start_ms = sys_millis();
                s_run = RUN_RUNNING;
            } else return;
            break;
        case RUN_RUNNING:
            if (ev == BMO_EVENT_CENTER) { s_left_ms = countdown_left_ms(); s_run = RUN_PAUSED; }
            else if (ev == BMO_EVENT_DOWN) s_run = RUN_IDLE;   /* cancel: keep the setting */
            else return;
            break;
        case RUN_PAUSED:
            if (ev == BMO_EVENT_CENTER) { s_start_ms = sys_millis(); s_run = RUN_RUNNING; }
            else if (ev == BMO_EVENT_DOWN) s_run = RUN_IDLE;
            else return;
            break;
        case RUN_DONE:
            if (ev == BMO_EVENT_CENTER || ev == BMO_EVENT_DOWN) s_run = RUN_IDLE;
            else return;
            break;
    }
    countdown_refresh_digits();
    countdown_labels();
}

/* ---------- BmoApp interface ---------- */
static void timer_init(void) {
    s_finished = 0;
    s_mode_sel = 0;
    s_cd_set[0] = 0; s_cd_set[1] = 1; s_cd_set[2] = 0;   /* countdown starts at 00:01:00 */
    s_run = RUN_IDLE;
    invalidate_digits();
    s_shown_color = 0;
    enter_mode_select();
}

static void timer_handle_input(BmoEvent ev) {
    switch (s_screen) {
        case SCREEN_MODE_SELECT:
            if (ev == BMO_EVENT_LEFT && s_mode_sel != 0) {
                s_mode_sel = 0; draw_mode_card(0, 1); draw_mode_card(1, 0);
            } else if (ev == BMO_EVENT_RIGHT && s_mode_sel != 1) {
                s_mode_sel = 1; draw_mode_card(0, 0); draw_mode_card(1, 1);
            } else if (ev == BMO_EVENT_CENTER) {
                if (s_mode_sel == 0) enter_stopwatch(); else enter_countdown();
            } else if (ev == BMO_EVENT_BACK) {
                s_finished = 1;   /* leave the app, back to the home screen */
            }
            break;
        case SCREEN_STOPWATCH: stopwatch_input(ev); break;
        case SCREEN_COUNTDOWN: countdown_input(ev); break;
    }
}

static void timer_update(float dt) {
    (void)dt; /* the timer reads sys_millis() itself, so a slow frame never loses time */
    if (s_screen == SCREEN_STOPWATCH) {
        if (s_run == RUN_RUNNING) stopwatch_refresh_digits();
    } else if (s_screen == SCREEN_COUNTDOWN) {
        if (s_run == RUN_RUNNING) {
            if (countdown_left_ms() == 0) {
                s_left_ms = 0;
                s_run = RUN_DONE;
                s_blink_ms = sys_millis();
                s_blink_on = 1;
                countdown_refresh_digits();
                countdown_labels();
            } else {
                countdown_refresh_digits();
            }
        } else if (s_run == RUN_DONE) {
            unsigned long now = sys_millis();
            if (now - s_blink_ms >= 500UL) {   /* blink the "TIME UP!" message */
                s_blink_ms = now;
                s_blink_on = !s_blink_on;
                draw_label("TIME UP!", s_blink_on ? COL_DONE : COL_TEXT);
            }
        }
    }
}

static void timer_draw(void) { /* drawing happens incrementally in update()/handle_input() */ }
static void timer_cleanup(void) { }
static int timer_is_finished(void) { return s_finished; }

BmoApp bmo_app_timer = {
    .name = "Timer",
    .init = timer_init,
    .update = timer_update,
    .draw = timer_draw,
    .handle_input = timer_handle_input,
    .cleanup = timer_cleanup,
    .is_finished = timer_is_finished
};
