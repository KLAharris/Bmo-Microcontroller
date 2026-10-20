#include "bmo_app_interface.h"
#include "bmo_gfx_interface.h"
#include <stdlib.h>
#include <stdio.h>

#define CELL_SIZE       20
#define GRID_Y_OFFSET   40  /* reserve top strip for the score */
#define GRID_W          (GFX_WIDTH / CELL_SIZE)
#define GRID_H          ((GFX_HEIGHT - GRID_Y_OFFSET) / CELL_SIZE)
#define MAX_SNAKE_LEN   (GRID_W * GRID_H)
#define TICK_INTERVAL   0.15f /* seconds per move; lower = faster snake */

typedef struct { int x, y; } Point;
typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } Direction;

static Point     s_body[MAX_SNAKE_LEN];
static int       s_length;
static Direction s_dir;
static Direction s_pending_dir; /* input buffer — applied once per tick, never immediately */
static Point     s_food;
static float     s_accumulator;
static int       s_alive;
static int       s_score;

static void snake_place_food(void) {
    int ok = 0;
    while (!ok) {
        s_food.x = rand() % GRID_W;
        s_food.y = rand() % GRID_H;
        ok = 1;
        for (int i = 0; i < s_length; i++) {
            if (s_body[i].x == s_food.x && s_body[i].y == s_food.y) { ok = 0; break; }
        }
    }
}

static void draw_cell(Point p, uint16_t color) {
    gfx_fill_rect(p.x * CELL_SIZE, GRID_Y_OFFSET + p.y * CELL_SIZE, CELL_SIZE, CELL_SIZE, color);
}

static void draw_score(void) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Score: %d", s_score);
    gfx_fill_rect(0, 0, 200, GRID_Y_OFFSET, COLOR_BLACK); /* clear just the score strip */
    gfx_text(4, 4, buf, COLOR_WHITE);
}

void snake_init(void) {
    s_length = 3;
    s_body[0] = (Point){ GRID_W / 2,     GRID_H / 2 };
    s_body[1] = (Point){ GRID_W / 2 - 1, GRID_H / 2 };
    s_body[2] = (Point){ GRID_W / 2 - 2, GRID_H / 2 };
    s_dir = DIR_RIGHT;
    s_pending_dir = DIR_RIGHT;
    s_accumulator = 0.0f;
    s_alive = 1;
    s_score = 0;

    /* ONE full clear, at startup only — never again during play */
    gfx_fill_rect(0, 0, GFX_WIDTH, GFX_HEIGHT, COLOR_BLACK);
    for (int i = 0; i < s_length; i++) draw_cell(s_body[i], COLOR_GREEN);

    snake_place_food();
    draw_cell(s_food, COLOR_RED);
    draw_score();
}

void snake_handle_input(BmoEvent event) {
    /* BUFFER ONLY. Never touch s_dir here — that's how a fast double-press
       (e.g. right then instantly down) lets the snake reverse into its own
       neck before the render even catches up. */
    switch (event) {
        case BMO_EVENT_UP:    if (s_dir != DIR_DOWN)  s_pending_dir = DIR_UP;    break;
        case BMO_EVENT_DOWN:  if (s_dir != DIR_UP)    s_pending_dir = DIR_DOWN;  break;
        case BMO_EVENT_LEFT:  if (s_dir != DIR_RIGHT) s_pending_dir = DIR_LEFT;  break;
        case BMO_EVENT_RIGHT: if (s_dir != DIR_LEFT)  s_pending_dir = DIR_RIGHT; break;
        default: break;
    }
}

void snake_update(float dt) {
    if (!s_alive) return;

    s_accumulator += dt;
    /* Fixed-timestep loop: may run 0, 1, or several ticks this call, so a
       slow frame (something else briefly blocking) never silently drops
       a step — it just catches up instead of "teleporting." */
    while (s_accumulator >= TICK_INTERVAL) {
        s_accumulator -= TICK_INTERVAL;

        s_dir = s_pending_dir; /* apply the buffered direction exactly once per tick */

        Point new_head = s_body[0];
        switch (s_dir) {
            case DIR_UP:    new_head.y--; break;
            case DIR_DOWN:  new_head.y++; break;
            case DIR_LEFT:  new_head.x--; break;
            case DIR_RIGHT: new_head.x++; break;
        }

        /* Wall collision */
        if (new_head.x < 0 || new_head.x >= GRID_W || new_head.y < 0 || new_head.y >= GRID_H) {
            s_alive = 0;
            gfx_text(GFX_WIDTH / 2 - 40, GFX_HEIGHT / 2, "GAME OVER", COLOR_WHITE);
            return;
        }

        /* Self collision — checked BEFORE committing the move, so we never
           render a frame where the head already overlaps the body */
        for (int i = 0; i < s_length; i++) {
            if (s_body[i].x == new_head.x && s_body[i].y == new_head.y) {
                s_alive = 0;
                gfx_text(GFX_WIDTH / 2 - 40, GFX_HEIGHT / 2, "GAME OVER", COLOR_WHITE);
                return;
            }
        }

        int ate = (new_head.x == s_food.x && new_head.y == s_food.y);
        Point old_tail = s_body[s_length - 1];

        int new_length = ate ? s_length + 1 : s_length;
        if (new_length > MAX_SNAKE_LEN) new_length = MAX_SNAKE_LEN; /* safety clamp */
        for (int i = new_length - 1; i > 0; i--) s_body[i] = s_body[i - 1];
        s_body[0] = new_head;
        s_length = new_length;

        /* --- incremental draw: ONLY these specific cells, every move ---
           This is the rule that makes the game viable on real hardware.
           The desktop harness will look fine even if you cheat this and
           redraw everything — the real display will not. */
        draw_cell(new_head, COLOR_GREEN);           /* new head */
        draw_cell(s_body[1], COLOR_GREEN);           /* old head -> now body */
        if (!ate) draw_cell(old_tail, COLOR_BLACK);  /* clear vacated tail */

        if (ate) {
            s_score++;
            draw_score();
            snake_place_food();
            draw_cell(s_food, COLOR_RED);
        }
    }
}

void snake_draw(void) {
    /* Intentionally empty. All drawing already happened incrementally
       inside update(), exactly when a cell changed. There's nothing to
       redraw every frame — that's the point. */
}

void snake_cleanup(void) {
    /* nothing to free — no dynamic allocation used */
}

int snake_is_finished(void) {
    return !s_alive;
}

BmoApp bmo_game_snake = {
    .name = "Snake",
    .init = snake_init,
    .update = snake_update,
    .draw = snake_draw,
    .handle_input = snake_handle_input,
    .cleanup = snake_cleanup,
    .is_finished = snake_is_finished
};
