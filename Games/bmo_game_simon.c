#include "bmo_app_interface.h"
#include "bmo_gfx_interface.h"
#include "bmo_draw_util.h"
#include <stdlib.h>
#include <stdio.h>

/* Simon: watch the colour sequence, repeat it with the arrow keys
   (up/down/left/right = the four coloured blocks).
   Each level adds one step. No audio — purely visual. */

#define MAX_SEQUENCE    64
#define STEP_ON_TIME    0.5f  /* how long an arrow stays lit during playback */
#define STEP_GAP_TIME   0.25f /* dark gap between playback steps */
#define ECHO_TIME       0.2f  /* how long the player's own press stays lit */
#define LEVEL_COOLDOWN  0.8f  /* short pause before each level starts (change freely) */
#define IDLE_TIMEOUT    7.0f  /* no input this long while listening -> back to menu */
#define BAR_W           200   /* cooldown progress bar */
#define BAR_H           8
#define BAR_Y_OFFSET    84    /* bar position below the top of the middle cell */

typedef enum { PAD_UP = 0, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_COUNT } Pad;

typedef enum {
    SIMON_STATE_COOLDOWN,
    SIMON_STATE_PLAYBACK_ON,
    SIMON_STATE_PLAYBACK_GAP,
    SIMON_STATE_LISTENING,
    SIMON_STATE_GAMEOVER,
    SIMON_STATE_TIMEOUT      /* player went idle; app reports finished */
} SimonState;

static const int      s_pad_rgb[PAD_COUNT][3] = { {255, 200, 40}, {50, 120, 255}, {240, 60, 60}, {60, 210, 90} };

static int        s_sequence[MAX_SEQUENCE];
static int        s_sequence_len;
static int        s_playback_index;
static int        s_input_index;
static SimonState s_state;
static float      s_timer;
static int        s_score;      /* levels cleared */
static int        s_echo_pad;   /* -1 = none currently flashing from player input */
static float      s_echo_timer;
static float      s_idle_timer; /* seconds since the player last pressed anything */
static int        s_bar_px;     /* current width of the cooldown bar */

/* On-screen D-pad layout: a plus/cross shape, one arm per direction,
   matching the physical direction (up = top of screen, etc.). */
static void pad_rect(Pad p, int *x, int *y, int *w, int *h) {
    int col_w = GFX_WIDTH / 3;
    int row_h = GFX_HEIGHT / 3;
    switch (p) {
        case PAD_UP:    *x = col_w;     *y = 0;         break;
        case PAD_DOWN:  *x = col_w;     *y = 2 * row_h; break;
        case PAD_LEFT:  *x = 0;         *y = row_h;     break;
        case PAD_RIGHT: *x = 2 * col_w; *y = row_h;     break;
        default:        *x = 0;         *y = 0;         break;
    }
    *w = col_w;
    *h = row_h;
}

/* The empty middle of the cross: used for status messages */
static void clear_center(void) {
    gfx_fill_rect(GFX_WIDTH / 3, GFX_HEIGHT / 3, GFX_WIDTH / 3, GFX_HEIGHT / 3, COLOR_BLACK);
}

/* lit = full colour; idle = dim colour, so the whole layout is always
   visible and only the active block "glows". Colours only, no symbols. */
static void draw_pad(Pad p, int lit) {
    int x, y, w, h;
    pad_rect(p, &x, &y, &w, &h);
    int r = s_pad_rgb[p][0], g = s_pad_rgb[p][1], b = s_pad_rgb[p][2];
    uint16_t fill = lit ? RGB565(r, g, b) : draw_scaled_color(r, g, b, 18);
    gfx_fill_rect(x + 4, y + 4, w - 8, h - 8, fill);
}

static void draw_round_label(void) {
    char buf[32];
    snprintf(buf, sizeof(buf), "ROUND: %d", s_sequence_len);
    gfx_fill_rect(0, GFX_HEIGHT - 24, 160, 24, COLOR_BLACK);
    gfx_text(8, GFX_HEIGHT - 20, buf, COLOR_WHITE);
}

static void simon_add_step(void) {
    if (s_sequence_len < MAX_SEQUENCE) {
        s_sequence[s_sequence_len] = rand() % PAD_COUNT;
        s_sequence_len++;
    }
}

static void enter_playback_on(void) {
    s_state = SIMON_STATE_PLAYBACK_ON;
    s_timer = 0.0f;
    draw_pad((Pad)s_sequence[s_playback_index], 1);
}

static void enter_playback_gap(void) {
    s_state = SIMON_STATE_PLAYBACK_GAP;
    s_timer = 0.0f;
    draw_pad((Pad)s_sequence[s_playback_index], 0);
}

/* Cooldown between levels: input is ignored, the middle of the screen shows
   a message and a bar that shrinks over LEVEL_COOLDOWN seconds. */
static void enter_cooldown(void) {
    int cx = GFX_WIDTH / 2;
    int top = GFX_HEIGHT / 3;
    char buf[24];

    s_state = SIMON_STATE_COOLDOWN;
    s_timer = 0.0f;
    clear_center();
    draw_round_label();

    snprintf(buf, sizeof(buf), "ROUND %d", s_sequence_len);
    draw_text_centered(cx, top + 56, buf, COLOR_YELLOW);

    s_bar_px = BAR_W;
    gfx_fill_rect(cx - BAR_W / 2, top + BAR_Y_OFFSET, BAR_W, BAR_H, RGB565(90, 200, 255));
}

static void show_game_over(void) {
    int cx = GFX_WIDTH / 2;
    int top = GFX_HEIGHT / 3;
    char buf[24];
    clear_center();
    draw_text_centered(cx, top + 50, "GAME OVER", COLOR_RED);
    snprintf(buf, sizeof(buf), "SCORE: %d", s_score);
    draw_text_centered(cx, top + 82, buf, COLOR_WHITE);
}

static void show_timeout(void) {
    int cx = GFX_WIDTH / 2;
    int top = GFX_HEIGHT / 3;
    char buf[24];
    clear_center();
    draw_text_centered(cx, top + 50, "TIME UP", COLOR_YELLOW);
    snprintf(buf, sizeof(buf), "SCORE: %d", s_score);
    draw_text_centered(cx, top + 82, buf, COLOR_WHITE);
}

void simon_init(void) {
    s_sequence_len = 0;
    s_score = 0;
    s_echo_pad = -1;
    s_echo_timer = 0.0f;
    s_idle_timer = 0.0f;

    gfx_fill_rect(0, 0, GFX_WIDTH, GFX_HEIGHT, COLOR_BLACK); /* one-time full clear */
    for (int p = 0; p < PAD_COUNT; p++) draw_pad((Pad)p, 0);

    simon_add_step();          /* level 1 = one step */
    s_playback_index = 0;
    enter_cooldown();          /* short info pause before the first playback */
}

void simon_handle_input(BmoEvent event) {
    /* Ignore input during cooldown and playback — only LISTENING accepts it. */
    if (s_state != SIMON_STATE_LISTENING) return;

    s_idle_timer = 0.0f; /* any button press counts as activity */

    Pad pressed;
    switch (event) {
        case BMO_EVENT_UP:    pressed = PAD_UP;    break;
        case BMO_EVENT_DOWN:  pressed = PAD_DOWN;  break;
        case BMO_EVENT_LEFT:  pressed = PAD_LEFT;  break;
        case BMO_EVENT_RIGHT: pressed = PAD_RIGHT; break;
        default: return;
    }

    /* immediate visual feedback; update() turns it back off after ECHO_TIME */
    draw_pad(pressed, 1);
    s_echo_pad = pressed;
    s_echo_timer = 0.0f;

    if ((int)pressed != s_sequence[s_input_index]) {
        s_state = SIMON_STATE_GAMEOVER;
        show_game_over();
        return;
    }

    s_input_index++;
    if (s_input_index == s_sequence_len) {
        /* whole sequence repeated correctly -> clear level, cooldown, then
           the next level plays one step longer */
        s_score++;
        simon_add_step();
        s_playback_index = 0;
        enter_cooldown();
    }
}

void simon_update(float dt) {
    if (s_state == SIMON_STATE_GAMEOVER || s_state == SIMON_STATE_TIMEOUT) return;

    /* the player's own echo flash, independent of state */
    if (s_echo_pad >= 0) {
        s_echo_timer += dt;
        if (s_echo_timer >= ECHO_TIME) {
            draw_pad((Pad)s_echo_pad, 0);
            s_echo_pad = -1;
        }
    }

    s_timer += dt;
    switch (s_state) {
        case SIMON_STATE_COOLDOWN: {
            int remaining = (int)(BAR_W * (1.0f - s_timer / LEVEL_COOLDOWN));
            if (remaining < 0) remaining = 0;
            if (remaining < s_bar_px) {
                /* incremental: only erase the slice that just expired */
                int bar_x = GFX_WIDTH / 2 - BAR_W / 2;
                int bar_y = GFX_HEIGHT / 3 + BAR_Y_OFFSET;
                gfx_fill_rect(bar_x + remaining, bar_y, s_bar_px - remaining, BAR_H, COLOR_BLACK);
                s_bar_px = remaining;
            }
            if (s_timer >= LEVEL_COOLDOWN) {
                clear_center();
                s_playback_index = 0;
                enter_playback_on();
            }
            break;
        }
        case SIMON_STATE_PLAYBACK_ON:
            if (s_timer >= STEP_ON_TIME) enter_playback_gap();
            break;
        case SIMON_STATE_PLAYBACK_GAP:
            if (s_timer >= STEP_GAP_TIME) {
                s_playback_index++;
                if (s_playback_index >= s_sequence_len) {
                    s_state = SIMON_STATE_LISTENING;
                    s_input_index = 0;
                    s_idle_timer = 0.0f; /* inactivity clock starts once the colours finish */
                } else {
                    enter_playback_on();
                }
            }
            break;
        case SIMON_STATE_LISTENING:
            s_idle_timer += dt;
            if (s_idle_timer >= IDLE_TIMEOUT) {
                s_state = SIMON_STATE_TIMEOUT;
                show_timeout();
            }
            break;
        default:
            break;
    }
}

void simon_draw(void) {
    /* Intentionally empty — drawing happens incrementally in update()/handle_input(). */
}

void simon_cleanup(void) {
}

int simon_is_finished(void) {
    return s_state == SIMON_STATE_GAMEOVER || s_state == SIMON_STATE_TIMEOUT;
}

BmoApp bmo_game_simon = {
    .name = "Simon",
    .init = simon_init,
    .update = simon_update,
    .draw = simon_draw,
    .handle_input = simon_handle_input,
    .cleanup = simon_cleanup,
    .is_finished = simon_is_finished
};
