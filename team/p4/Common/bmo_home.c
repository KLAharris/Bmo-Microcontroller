#include "bmo_home.h"
#include "bmo_gfx_interface.h"
#include "bmo_ui_scale.h"
#include "bmo_draw_util.h"
#include "bmo_system_interface.h"
#include <stdio.h>

/* Every number below is an 800x480 design value. UI(v) scales a length, UIX/UIY
   scale a position and keep the layout centred; UIN(v) never goes below 1 px
   (for borders). At 800x480 all of them return their input unchanged. */
#define UIN(v) (UI(v) < 1 ? 1 : UI(v))

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

#define CLOCK_RING_W 116

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

/* ---------------- layout helpers ---------------- */
static int bar_bottom(void) { return UIY(BAR_H); }                 /* first row under the bar */

/* the bar background reaches the top edge, so no letterbox gap shows above it */
static void bar_fill(int x, int w) { gfx_fill_rect(x, 0, w, bar_bottom(), BAR_BG); }

static int icon_cy(void) { return UIY(21); }                       /* vertical centre of bar icons */

/* ---------------- status bar ---------------- */
static void draw_focus_ring(int x, int w) {
    int t = UIN(2);
    int y0 = UIY(2), h = UI(38);
    gfx_fill_rect(x, y0, w, h, COLOR_WHITE);
    gfx_fill_rect(x + t, y0 + t, w - 2 * t, h - 2 * t, BAR_BG);
}

static void draw_battery(int pct) {
    bar_fill(0, UIX(140));
    gfx_fill_rect(UIX(16), UIY(11), UI(44), UI(20), ICON_COLOR);        /* outline */
    gfx_fill_rect(UIX(18), UIY(13), UI(40), UI(16), BAR_BG);            /* inside  */
    gfx_fill_rect(UIX(60), UIY(16), UIN(4), UI(10), ICON_COLOR);        /* terminal nub */
    uint16_t c = pct <= 20 ? RGB565(230, 60, 60) : (pct <= 40 ? RGB565(255, 200, 40) : RGB565(60, 210, 90));
    int w = UI(36) * pct / 100;
    if (w > 0) gfx_fill_rect(UIX(20), UIY(15), w, UI(12), c);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    draw_text_scaled(UIX(72), icon_cy() - 7, buf, COLOR_WHITE, 2);
}

/* The clock doubles as the Timer button: when focused it gets a ring. */
static void draw_clock(int hour, int minute, int focused) {
    int ring_w = UI(CLOCK_RING_W) < 64 ? 64 : UI(CLOCK_RING_W);
    int ts = UI_TS(3);
    gfx_fill_rect(GFX_WIDTH / 2 - UI(80), 0, UI(160), bar_bottom(), BAR_BG);   /* whole slot, so an old ring is wiped */
    if (focused) draw_focus_ring(GFX_WIDTH / 2 - ring_w / 2, ring_w);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    draw_text_scaled_centered(GFX_WIDTH / 2, icon_cy() - (7 * ts) / 2, buf, COLOR_WHITE, ts);
}

/* sun icon = brightness */
static void draw_brightness_icon(int focused) {
    const int cx = UIX(690), cy = icon_cy();
    int a = UIN(3);
    bar_fill(UIX(660), UI(56));
    if (focused) draw_focus_ring(UIX(668), UI(44));
    draw_fill_circle(cx, cy, UI(6), ICON_COLOR);
    gfx_fill_rect(cx - a / 2, cy - UI(14), a, UI(5), ICON_COLOR);      /* rays: up, down, left, right */
    gfx_fill_rect(cx - a / 2, cy + UI(10), a, UI(5), ICON_COLOR);
    gfx_fill_rect(cx - UI(14), cy - a / 2, UI(5), a, ICON_COLOR);
    gfx_fill_rect(cx + UI(10), cy - a / 2, UI(5), a, ICON_COLOR);
    if (UI(10) >= 8) {                                                  /* rays: diagonals (large screens only) */
        gfx_fill_rect(cx - UI(10), cy - UI(10), a, a, ICON_COLOR);
        gfx_fill_rect(cx + UI(8),  cy - UI(10), a, a, ICON_COLOR);
        gfx_fill_rect(cx - UI(10), cy + UI(8),  a, a, ICON_COLOR);
        gfx_fill_rect(cx + UI(8),  cy + UI(8),  a, a, ICON_COLOR);
    }
}

/* speaker icon = sound */
static void draw_sound(int muted, int focused) {
    const int ix = UIX(740), iy = icon_cy() - UI(14);   /* top-left of the speaker */
    int w = GFX_WIDTH - UIX(718);
    bar_fill(UIX(718), w);
    if (focused) draw_focus_ring(ix - UI(14), UI(62));
    gfx_fill_rect(ix,            iy + UI(9),  UI(8), UI(10), ICON_COLOR);   /* speaker body */
    gfx_fill_rect(ix + UI(8),    iy + UI(6),  UI(4), UI(16), ICON_COLOR);   /* cone, stepped */
    gfx_fill_rect(ix + UI(12),   iy + UI(3),  UI(4), UI(22), ICON_COLOR);
    gfx_fill_rect(ix + UI(16),   iy,          UI(4), UI(28), ICON_COLOR);
    if (!muted) {                                                          /* sound waves */
        gfx_fill_rect(ix + UI(25), iy + UI(8), UIN(3), UI(12), ICON_COLOR);
        gfx_fill_rect(ix + UI(31), iy + UI(4), UIN(3), UI(20), ICON_COLOR);
    } else {                                                               /* red X = muted */
        int n = UI(14), t = UIN(3);
        for (int i = 0; i < n; i++) {
            gfx_fill_rect(ix + UI(25) + i, iy + UI(7) + i, t, t, RGB565(240, 60, 60));
            gfx_fill_rect(ix + UI(25) + i, iy + UI(20) - i, t, t, RGB565(240, 60, 60));
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
/* Right-aligned under the bar. On a narrow screen it keeps a minimum width so
   "BRIGHTNESS" and the percentage never touch. */
static int panel_w(void) { int w = UI(290); return (UI_NARROW && w < 232) ? 232 : w; }
static int panel_x(void) { return GFX_WIDTH - UI_OX - UI(10) - panel_w(); }
static int panel_y(void) { return bar_bottom() + UI(6); }
static int panel_h(void) { return UI(58) < 40 ? 40 : UI(58); }

static void draw_brightness_panel(void) {
    int b = sys_get_brightness();
    int px = panel_x(), py = panel_y(), pw = panel_w(), ph = panel_h();
    int pad = UI(14) < 8 ? 8 : UI(14);
    int bt = UIN(2);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", b);
    gfx_fill_rect(px, py, pw, ph, COLOR_WHITE);
    gfx_fill_rect(px + bt, py + bt, pw - 2 * bt, ph - 2 * bt, RGB565(26, 30, 62));
    draw_text_scaled(px + pad, py + UI(10), "BRIGHTNESS", COLOR_WHITE, 2);
    draw_text_scaled(px + pw - pad - draw_text_scaled_width(buf, 2), py + UI(10), buf, SLIDER_COL, 2);
    int sy = py + ph - UI(22), sh = UIN(10);
    gfx_fill_rect(px + pad, sy, pw - 2 * pad, sh, RGB565(50, 54, 80));                        /* track */
    gfx_fill_rect(px + pad, sy, (pw - 2 * pad) * b / 100, sh, SLIDER_COL);                    /* fill  */
}

static void close_brightness_panel(void) {
    gfx_fill_rect(panel_x() - 2, panel_y() - 2, panel_w() + 4, panel_h() + 4, SCREEN_BG);
}

/* ---------------- card icons (all drawn with rectangles) ---------------- */
static void draw_games_icon(int cx, int cy, int dim) {      /* game controller */
    uint16_t body = draw_scaled_color(120, 130, 190, dim ? 38 : 100);
    uint16_t dark = draw_scaled_color(30, 34, 60, dim ? 60 : 100);
    gfx_fill_rect(cx - UI(70), cy - UI(34), UI(140), UI(62), body);       /* main body   */
    gfx_fill_rect(cx - UI(70), cy + UI(12), UI(46), UI(38), body);        /* left grip   */
    gfx_fill_rect(cx + UI(24), cy + UI(12), UI(46), UI(38), body);        /* right grip  */
    gfx_fill_rect(cx - UI(54), cy - UI(11), UI(32), UI(10), dark);        /* d-pad: horizontal */
    gfx_fill_rect(cx - UI(43), cy - UI(22), UI(10), UI(32), dark);        /* d-pad: vertical   */
    gfx_fill_rect(cx + UI(26), cy - UI(22), UI(16), UI(16), draw_scaled_color(240, 60, 60, dim ? 38 : 100));   /* buttons */
    gfx_fill_rect(cx + UI(46), cy - UI(8),  UI(16), UI(16), draw_scaled_color(255, 200, 40, dim ? 38 : 100));
}

static void draw_music_icon(int cx, int cy, int dim) {      /* two beamed notes */
    uint16_t c = draw_scaled_color(230, 90, 200, dim ? 38 : 100);
    draw_fill_circle(cx - UI(32), cy + UI(38), UI(15), c);                /* note heads */
    draw_fill_circle(cx + UI(36), cy + UI(38), UI(15), c);
    gfx_fill_rect(cx - UI(25), cy - UI(46), UIN(7), UI(84), c);           /* stems */
    gfx_fill_rect(cx + UI(43), cy - UI(46), UIN(7), UI(84), c);
    gfx_fill_rect(cx - UI(25), cy - UI(50), UI(75), UI(14), c);           /* beam  */
}

/* ---------------- cards ---------------- */
static int card_x(int i) { return UIX((UI_BASE_W - (CARD_COUNT * CARD_W + (CARD_COUNT - 1) * CARD_GAP)) / 2 + i * (CARD_W + CARD_GAP)); }

static void draw_card(int i, int selected) {
    int x = card_x(i), y = UIY(CARD_Y);
    int cw = UI(CARD_W), ch = UI(CARD_H);
    int bt = selected ? UIN(4) : UIN(2);
    uint16_t border = selected ? RGB565(s_cards[i].accent[0], s_cards[i].accent[1], s_cards[i].accent[2])
                               : RGB565(50, 54, 80);
    uint16_t bg = selected ? RGB565(26, 30, 62) : RGB565(16, 18, 34);
    gfx_fill_rect(x, y, cw, ch, border);
    gfx_fill_rect(x + bt, y + bt, cw - 2 * bt, ch - 2 * bt, bg);

    int cx = x + cw / 2, cy = y + UI(80);
    int dim = !selected;
    if (i == 0) draw_games_icon(cx, cy, dim);
    else        draw_music_icon(cx, cy, dim);

    int ts = UI_TS(4);
    draw_text_scaled_centered(cx, y + UI(166), s_cards[i].name, selected ? COLOR_WHITE : TEXT_MUTED, ts);
    draw_text_scaled_centered(cx, y + UI(208), s_cards[i].subtitle,
                              selected ? RGB565(190, 196, 220) : RGB565(80, 84, 108), 2);
}

/* the hint line at the bottom changes with what is focused */
static void draw_hint(void) {
    const char *t;
    if (s_adjusting)                 t = UI_HINT("LEFT/RIGHT: ADJUST   ENTER: DONE", "L/R: ADJUST   ENTER: DONE");
    else if (s_focus == FOCUS_CLOCK) t = UI_HINT("ENTER: OPEN TIMER   LEFT/RIGHT: ICON   DOWN: BACK", "ENTER: TIMER   L/R: ICON");
    else if (s_focus != FOCUS_CARDS) t = UI_HINT("LEFT/RIGHT: ICON   ENTER: SELECT   DOWN: BACK", "ENTER: SELECT   L/R: ICON");
    else                             t = UI_HINT("LEFT/RIGHT: CHOOSE   ENTER: OPEN   UP: TIMER/SETTINGS", "ENTER: OPEN   UP: CLOCK");
    int hy = UIY(410);
    gfx_fill_rect(0, hy - UI(6), GFX_WIDTH, UI(24) < 18 ? 18 : UI(24), SCREEN_BG);
    draw_text_scaled_centered(GFX_WIDTH / 2, hy, t, TEXT_MUTED, 2);
}

void home_draw_full(void) {
    s_adjusting = 0;
    gfx_fill_rect(0, 0, GFX_WIDTH, GFX_HEIGHT, SCREEN_BG);
    bar_fill(0, GFX_WIDTH);
    gfx_fill_rect(0, bar_bottom(), GFX_WIDTH, UIN(2), BAR_LINE);

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
