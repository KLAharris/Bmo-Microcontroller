#include "home_screen.h"
#include "bmo_gfx_interface.h"
#include "bmo_draw_util.h"
#include "bmo_system_interface.h"
#include <stdio.h>

#define BAR_H        42
#define BAR_BG       RGB565(18, 22, 42)
#define BAR_LINE     RGB565(50, 54, 80)
#define SCREEN_BG    RGB565(10, 12, 24)
#define TEXT_MUTED   RGB565(120, 125, 150)
#define ICON_COLOR   RGB565(200, 205, 225)
#define SLIDER_COL   RGB565(255, 200, 40)

/* two big app cards: Games and Music (the Timer is opened from the clock) */
#define CARD_COUNT 2
#define CARD_W     340
#define CARD_H     252
#define CARD_GAP   40
#define CARD_Y     116

#define BRIGHT_MIN  10
#define BRIGHT_STEP 10

/* the clock in the middle of the status bar */
#define CLOCK_RING_W 116

/* brightness slider panel, shown just under the status bar */
#define PANEL_X 500
#define PANEL_Y 48
#define PANEL_W 290
#define PANEL_H 58

typedef struct { const char *name; const char *subtitle; int accent[3]; } Card;
static const Card s_cards[CARD_COUNT] = {
    { "GAMES", "SNAKE, SIMON", {60, 220, 100} },
    { "MUSIC", "PICK A SONG",  {230, 90, 200} },
};

/* Focus order matches the left-to-right order in the status bar, so
   LEFT/RIGHT simply moves along it. */
typedef enum { FOCUS_CARDS, FOCUS_CLOCK, FOCUS_BRIGHT, FOCUS_SOUND } Focus;

static int   s_sel = 0;              /* highlighted card */
static Focus s_focus = FOCUS_CARDS;  /* the cards, or one of the three things in the bar */
static int   s_adjusting = 0;        /* 1 = the brightness slider is open */
/* what is currently drawn in the bar (-1 = not drawn yet) */
static int s_last_minute = -1, s_last_hour = -1, s_last_batt = -1, s_last_muted = -1, s_last_focus = -1;

/* ---------------- status bar ---------------- */
static void draw_focus_ring(int x, int w) {
    gfx_fill_rect(x, 2, w, 38, COLOR_WHITE);
    gfx_fill_rect(x + 2, 4, w - 4, 34, BAR_BG);
}

static void draw_battery(int pct) {
    gfx_fill_rect(8, 4, 128, 34, BAR_BG);
    gfx_fill_rect(16, 11, 44, 20, ICON_COLOR);          /* outline */
    gfx_fill_rect(18, 13, 40, 16, BAR_BG);              /* inside  */
    gfx_fill_rect(60, 16, 4, 10, ICON_COLOR);           /* terminal nub */
    uint16_t c = pct <= 20 ? RGB565(230, 60, 60) : (pct <= 40 ? RGB565(255, 200, 40) : RGB565(60, 210, 90));
    int w = 36 * pct / 100;
    if (w > 0) gfx_fill_rect(20, 15, w, 12, c);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    draw_text_scaled(72, 14, buf, COLOR_WHITE, 2);
}

/* The clock doubles as the Timer button: when focused it gets a ring. */
static void draw_clock(int hour, int minute, int focused) {
    gfx_fill_rect(GFX_WIDTH / 2 - 80, 0, 160, BAR_H, BAR_BG);   /* whole slot, so an old ring is wiped */
    if (focused) draw_focus_ring(GFX_WIDTH / 2 - CLOCK_RING_W / 2, CLOCK_RING_W);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    draw_text_scaled_centered(GFX_WIDTH / 2, 11, buf, COLOR_WHITE, 3);
}

/* sun icon = brightness */
static void draw_brightness_icon(int focused) {
    const int cx = 690, cy = 21;
    gfx_fill_rect(660, 0, 56, BAR_H, BAR_BG);
    if (focused) draw_focus_ring(668, 44);
    draw_fill_circle(cx, cy, 6, ICON_COLOR);
    gfx_fill_rect(cx - 1, cy - 14, 3, 5, ICON_COLOR);      /* rays: up, down, left, right */
    gfx_fill_rect(cx - 1, cy + 10, 3, 5, ICON_COLOR);
    gfx_fill_rect(cx - 14, cy - 1, 5, 3, ICON_COLOR);
    gfx_fill_rect(cx + 10, cy - 1, 5, 3, ICON_COLOR);
    gfx_fill_rect(cx - 10, cy - 10, 3, 3, ICON_COLOR);     /* rays: diagonals */
    gfx_fill_rect(cx + 8,  cy - 10, 3, 3, ICON_COLOR);
    gfx_fill_rect(cx - 10, cy + 8,  3, 3, ICON_COLOR);
    gfx_fill_rect(cx + 8,  cy + 8,  3, 3, ICON_COLOR);
}

/* speaker icon = sound */
static void draw_sound(int muted, int focused) {
    const int ix = 740, iy = 7;       /* top-left of the 28px-tall speaker */
    gfx_fill_rect(718, 0, 82, BAR_H, BAR_BG);
    if (focused) draw_focus_ring(ix - 14, 62);
    gfx_fill_rect(ix,      iy + 9,  8, 10, ICON_COLOR);   /* speaker body */
    gfx_fill_rect(ix + 8,  iy + 6,  4, 16, ICON_COLOR);   /* cone, stepped */
    gfx_fill_rect(ix + 12, iy + 3,  4, 22, ICON_COLOR);
    gfx_fill_rect(ix + 16, iy,      4, 28, ICON_COLOR);
    if (!muted) {                                          /* sound waves */
        gfx_fill_rect(ix + 25, iy + 8, 3, 12, ICON_COLOR);
        gfx_fill_rect(ix + 31, iy + 4, 3, 20, ICON_COLOR);
    } else {                                               /* red X = muted */
        for (int i = 0; i < 14; i++) {
            gfx_fill_rect(ix + 25 + i, iy + 7 + i, 3, 3, RGB565(240, 60, 60));
            gfx_fill_rect(ix + 25 + i, iy + 20 - i, 3, 3, RGB565(240, 60, 60));
        }
    }
}

void home_update(void) {
    int h, m, s;
    sys_get_time(&h, &m, &s);
    int focus_changed = ((int)s_focus != s_last_focus);

    if (m != s_last_minute || h != s_last_hour || focus_changed) {
        draw_clock(h, m, s_focus == FOCUS_CLOCK);
        s_last_minute = m;
        s_last_hour = h;
    }

    int pct = sys_get_battery_percent();
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (pct != s_last_batt) { draw_battery(pct); s_last_batt = pct; }

    int muted = sys_is_muted();
    if (focus_changed)                          draw_brightness_icon(s_focus == FOCUS_BRIGHT);
    if (focus_changed || muted != s_last_muted) draw_sound(muted, s_focus == FOCUS_SOUND);
    s_last_muted = muted;
    s_last_focus = (int)s_focus;
}

/* ---------------- brightness slider ---------------- */
static void draw_brightness_panel(void) {
    int b = sys_get_brightness();
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", b);
    gfx_fill_rect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, COLOR_WHITE);
    gfx_fill_rect(PANEL_X + 2, PANEL_Y + 2, PANEL_W - 4, PANEL_H - 4, RGB565(26, 30, 62));
    draw_text_scaled(PANEL_X + 14, PANEL_Y + 10, "BRIGHTNESS", COLOR_WHITE, 2);
    draw_text_scaled(PANEL_X + PANEL_W - 14 - draw_text_scaled_width(buf, 2), PANEL_Y + 10, buf, SLIDER_COL, 2);
    gfx_fill_rect(PANEL_X + 14, PANEL_Y + 36, PANEL_W - 28, 10, RGB565(50, 54, 80));       /* track */
    gfx_fill_rect(PANEL_X + 14, PANEL_Y + 36, (PANEL_W - 28) * b / 100, 10, SLIDER_COL);   /* fill  */
}

static void close_brightness_panel(void) {
    gfx_fill_rect(PANEL_X - 2, PANEL_Y - 2, PANEL_W + 4, PANEL_H + 4, SCREEN_BG);
}

/* ---------------- card icons (all drawn with rectangles) ---------------- */
static void draw_games_icon(int cx, int cy, int dim) {      /* game controller */
    uint16_t body = draw_scaled_color(120, 130, 190, dim ? 38 : 100);
    uint16_t dark = draw_scaled_color(30, 34, 60, dim ? 60 : 100);
    gfx_fill_rect(cx - 70, cy - 34, 140, 62, body);         /* main body   */
    gfx_fill_rect(cx - 70, cy + 12, 46, 38, body);          /* left grip   */
    gfx_fill_rect(cx + 24, cy + 12, 46, 38, body);          /* right grip  */
    gfx_fill_rect(cx - 54, cy - 11, 32, 10, dark);          /* d-pad: horizontal */
    gfx_fill_rect(cx - 43, cy - 22, 10, 32, dark);          /* d-pad: vertical   */
    gfx_fill_rect(cx + 26, cy - 22, 16, 16, draw_scaled_color(240, 60, 60, dim ? 38 : 100));   /* buttons */
    gfx_fill_rect(cx + 46, cy - 8, 16, 16, draw_scaled_color(255, 200, 40, dim ? 38 : 100));
}

static void draw_music_icon(int cx, int cy, int dim) {      /* two beamed notes */
    uint16_t c = draw_scaled_color(230, 90, 200, dim ? 38 : 100);
    draw_fill_circle(cx - 32, cy + 38, 15, c);              /* note heads */
    draw_fill_circle(cx + 36, cy + 38, 15, c);
    gfx_fill_rect(cx - 25, cy - 46, 7, 84, c);              /* stems */
    gfx_fill_rect(cx + 43, cy - 46, 7, 84, c);
    gfx_fill_rect(cx - 25, cy - 50, 75, 14, c);             /* beam  */
}

/* ---------------- cards ---------------- */
static int card_x(int i) { return (GFX_WIDTH - (CARD_COUNT * CARD_W + (CARD_COUNT - 1) * CARD_GAP)) / 2 + i * (CARD_W + CARD_GAP); }

static void draw_card(int i, int selected) {
    int x = card_x(i), y = CARD_Y;
    int bt = selected ? 4 : 2;
    uint16_t border = selected ? RGB565(s_cards[i].accent[0], s_cards[i].accent[1], s_cards[i].accent[2])
                               : RGB565(50, 54, 80);
    uint16_t bg = selected ? RGB565(26, 30, 62) : RGB565(16, 18, 34);
    gfx_fill_rect(x, y, CARD_W, CARD_H, border);
    gfx_fill_rect(x + bt, y + bt, CARD_W - 2 * bt, CARD_H - 2 * bt, bg);

    int cx = x + CARD_W / 2, cy = y + 80;
    int dim = !selected;
    if (i == 0) draw_games_icon(cx, cy, dim);
    else        draw_music_icon(cx, cy, dim);

    draw_text_scaled_centered(cx, y + 166, s_cards[i].name, selected ? COLOR_WHITE : TEXT_MUTED, 4);
    draw_text_scaled_centered(cx, y + 208, s_cards[i].subtitle,
                              selected ? RGB565(190, 196, 220) : RGB565(80, 84, 108), 2);
}

/* the hint line at the bottom changes with what is focused */
static void draw_hint(void) {
    const char *t;
    if (s_adjusting)                 t = "LEFT/RIGHT: ADJUST   ENTER: DONE";
    else if (s_focus == FOCUS_CLOCK) t = "ENTER: OPEN TIMER   LEFT/RIGHT: ICON   DOWN: BACK";
    else if (s_focus != FOCUS_CARDS) t = "LEFT/RIGHT: ICON   ENTER: SELECT   DOWN: BACK";
    else                             t = "LEFT/RIGHT: CHOOSE   ENTER: OPEN   UP: TIMER/SETTINGS";
    gfx_fill_rect(0, 404, GFX_WIDTH, 24, SCREEN_BG);
    draw_text_scaled_centered(GFX_WIDTH / 2, 410, t, TEXT_MUTED, 2);
}

void home_draw_full(void) {
    s_adjusting = 0;
    gfx_fill_rect(0, 0, GFX_WIDTH, GFX_HEIGHT, SCREEN_BG);
    gfx_fill_rect(0, 0, GFX_WIDTH, BAR_H, BAR_BG);
    gfx_fill_rect(0, BAR_H, GFX_WIDTH, 2, BAR_LINE);

    s_last_minute = s_last_hour = s_last_batt = s_last_muted = s_last_focus = -1;
    home_update();                                   /* paints battery, clock, icons */

    for (int i = 0; i < CARD_COUNT; i++) draw_card(i, i == s_sel && s_focus == FOCUS_CARDS);
    draw_hint();
}

HomeAction home_handle_event(BmoEvent ev) {
    /* 1) brightness slider open: LEFT/RIGHT change it, anything else closes it */
    if (s_adjusting) {
        if (ev == BMO_EVENT_LEFT || ev == BMO_EVENT_RIGHT) {
            int b = sys_get_brightness() + (ev == BMO_EVENT_RIGHT ? BRIGHT_STEP : -BRIGHT_STEP);
            if (b < BRIGHT_MIN) b = BRIGHT_MIN;
            if (b > 100) b = 100;
            sys_set_brightness(b);
            draw_brightness_panel();
        } else if (ev == BMO_EVENT_CENTER || ev == BMO_EVENT_BACK || ev == BMO_EVENT_UP || ev == BMO_EVENT_DOWN) {
            s_adjusting = 0;
            close_brightness_panel();
            draw_hint();
        }
        return HOME_ACTION_NONE;
    }

    /* 2) focus is on the clock, the brightness icon or the speaker icon */
    if (s_focus != FOCUS_CARDS) {
        switch (ev) {
            case BMO_EVENT_LEFT:  if (s_focus > FOCUS_CLOCK) s_focus = (Focus)(s_focus - 1); break;
            case BMO_EVENT_RIGHT: if (s_focus < FOCUS_SOUND) s_focus = (Focus)(s_focus + 1); break;
            case BMO_EVENT_DOWN:
                s_focus = FOCUS_CARDS;
                draw_card(s_sel, 1);
                break;
            case BMO_EVENT_CENTER:
                if (s_focus == FOCUS_CLOCK) {
                    return HOME_ACTION_TIMER;         /* the clock opens the Timer app */
                } else if (s_focus == FOCUS_SOUND) {
                    sys_set_muted(!sys_is_muted());
                } else {
                    s_adjusting = 1;
                    draw_brightness_panel();
                }
                break;
            default: break;
        }
        draw_hint();
        home_update();
        return HOME_ACTION_NONE;
    }

    /* 3) focus is on the cards */
    switch (ev) {
        case BMO_EVENT_LEFT:
            if (s_sel > 0) { int old = s_sel; s_sel--; draw_card(old, 0); draw_card(s_sel, 1); }
            break;
        case BMO_EVENT_RIGHT:
            if (s_sel < CARD_COUNT - 1) { int old = s_sel; s_sel++; draw_card(old, 0); draw_card(s_sel, 1); }
            break;
        case BMO_EVENT_UP:
            s_focus = FOCUS_CLOCK;                    /* straight up from the cards = the clock */
            draw_card(s_sel, 0);
            draw_hint();
            home_update();
            break;
        case BMO_EVENT_CENTER:
            return s_sel == 0 ? HOME_ACTION_GAMES : HOME_ACTION_MUSIC;
        default:
            break;
    }
    return HOME_ACTION_NONE;
}
