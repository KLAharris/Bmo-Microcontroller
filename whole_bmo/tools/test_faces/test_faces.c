/* test_faces.c — the living faces on the laptop, with the real gfx_spi.c.
 *
 * Builds bmo_face.c (with -DFACE_TEST hooks) + gfx_spi.c + gfx_font.c +
 * fake_lcd.c (a RAM screen in place of the ILI9486) and checks:
 *   1. Clean draw of each face at rest -> out/face_Rosto-XX.ppm, compared
 *      with the Figma check images by compare_checks.py.
 *   2. All 13 x 13 = 169 face changes, each from rest, mid-blink, mid-glance
 *      and mid-motion (676 changes).
 *   3. 10 simulated minutes per face. Reports CPU load per face and checks that
 *      the face redraws at least once every 2 s and that no two groups' ink
 *      ever touches.
 *   4. face_set_gaze(), face_set_active(), face_show() to the same face.
 *   5. gfx_blend_sprite() with no box open draws a big sprite in strips.
 * After EVERY frame in 2-4: the screen equals a clean draw of every group at
 * its committed transform (face_test_render, a separate full-screen composer),
 * no whole-screen fill, no push bigger than the gfx box, and the frame's
 * board time (timing model in bmo_face.c) is under the 25 ms budget.
 * ./test_faces --anim writes only out/anim/Rosto-XX_NNN.ppm (first 6 s of each
 * face) for make_gifs.py (`make gifs`), which deletes them after use.
 *
 * Exit code 0 = all passed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "face_gfx.h"
#include "bmo_face.h"
#include "bmo_faces.h"              /* test only: big sprite for check 5 */
#include "fake_lcd.h"

void     face_test_render(uint16_t *scr);
long     face_test_overlap(void);
uint32_t face_test_tick_ns(void);
uint32_t face_test_spent_ns(void);
uint32_t face_test_spi_ns_px(void);
uint32_t face_test_push_ns(void);
uint32_t face_test_state_hash(void);

#define SEED       12345u
#define BUDGET_NS  25000000u        /* hard limit per face_tick() on the board */
#define SPI_NS_PX  ((long)face_test_spi_ns_px())   /* bmo_face.c timing model */
#define PUSH_NS    ((long)face_test_push_ns())

/* The face's clock in the test: its own model time x clock_factor.
 * 1.0 = the board behaves like the model; 1.8 = the board is 1.8x slower. */
static double clock_factor = 1.0;
static uint32_t fake_clock(void) { return (uint32_t)(face_test_spent_ns() * clock_factor / 1000.0); }
#define C_BG       0xCF38
#define MINUTES    10

static uint16_t ref[FACE_COUNT][480 * 320];
static uint16_t want[480 * 320];
static int      fails;

#define CHECK(cond, ...) do { if (!(cond)) { if (fails < 40) { printf("FAIL: " __VA_ARGS__); printf("\n"); } fails++; } } while (0)

/* ------------------------------------------------------------ frame checks */
typedef struct {
    long     frames, drawn;          /* frames run / frames that sent pixels */
    double   ns;                     /* model board time, sum              */
    uint32_t worst_ns;
    long     over;                   /* frames over budget                 */
    long     px, pushes;
    long     mismatch;
    uint32_t last_draw_t, max_gap;   /* ms between frames that sent pixels */
    long     overlap;                /* worst ink overlap seen             */
    uint32_t hashes;                 /* committed-state changes            */
} run_t;

static run_t    R;
static uint32_t t_now;
static const char *ctx = "";

static void run_reset(void)
{
    memset(&R, 0, sizeof R);
    R.last_draw_t = t_now;
}

static void frame_check(int overlap_too)
{
    static uint32_t last_hash;
    uint32_t model = face_test_tick_ns();
    if (!model) return;                               /* face_tick returned early */
    uint32_t ns = (uint32_t)(model * clock_factor);   /* what the board's clock would show */
    R.frames++;
    R.ns += ns;
    if (ns > R.worst_ns) R.worst_ns = ns;
    if (ns > BUDGET_NS) R.over++;

    long spi = lcd_stats.px * SPI_NS_PX + (long)lcd_stats.pushes * PUSH_NS;
    CHECK(spi <= (long)model + 1, "%s t=%u: SPI time %ld ns above the model's %u ns", ctx, t_now, spi, model);
    CHECK(lcd_stats.full_fills == 0, "%s t=%u: whole-screen fill", ctx, t_now);
    CHECK(lcd_stats.max_push_px <= GFX_BOX_MAX_PX, "%s t=%u: push of %d px > box", ctx, t_now, lcd_stats.max_push_px);
    if (lcd_stats.px) {
        R.drawn++;
        R.px += lcd_stats.px; R.pushes += lcd_stats.pushes;
        if (t_now - R.last_draw_t > R.max_gap) R.max_gap = t_now - R.last_draw_t;
        R.last_draw_t = t_now;
    }
    uint32_t h = face_test_state_hash();
    if (h != last_hash) { R.hashes++; last_hash = h; }

    face_test_render(want);
    if (memcmp(want, lcd_screen, sizeof want) != 0) {
        long d = 0;
        for (int i = 0; i < 480 * 320; i++) d += want[i] != lcd_screen[i];
        R.mismatch++;
        CHECK(0, "%s t=%u: %ld px differ from a clean draw", ctx, t_now, d);
    }
    if (overlap_too) {
        long o = face_test_overlap();
        if (o > R.overlap) R.overlap = o;
    }
    lcd_stats_reset();
}

static void run_for(uint32_t ms, int overlap_every)
{
    for (uint32_t end = t_now + ms; t_now < end; t_now++) {
        face_tick(t_now);
        frame_check(overlap_every && (R.frames % overlap_every) == 0);
    }
}

/* Show face f from a clean, fully drawn screen; first tick starts the clock. */
static void start_on(face_id_t f)
{
    face_init(SEED + f);
    face_set_clock(fake_clock, 1);
    face_set_active(0);
    face_show(f);
    face_set_active(1);                      /* full repaint of f */
    face_tick(t_now);
    lcd_stats_reset();
}

static void write_ppm(const char *path, const uint16_t *s)
{
    FILE *f = fopen(path, "wb");
    if (!f) { printf("FAIL: cannot write %s\n", path); fails++; return; }
    fprintf(f, "P6\n480 320\n255\n");
    for (int i = 0; i < 480 * 320; i++) {
        int r = (s[i] >> 11) & 0x1F, g = (s[i] >> 5) & 0x3F, b = s[i] & 0x1F;
        unsigned char px[3] = { (unsigned char)((r * 255 + 15) / 31),     /* = expand565() */
                                (unsigned char)((g * 255 + 31) / 63),     /*   in slice_faces.py */
                                (unsigned char)((b * 255 + 15) / 31) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

static double ms_of(double ns) { return ns / 1e6; }

/* ------------------------------------------------------------ 1. rest */
static void test_rest(void)
{
    for (int f = 0; f < FACE_COUNT; f++) {
        start_on((face_id_t)f);
        memcpy(ref[f], lcd_screen, sizeof ref[f]);
        face_test_render(want);
        CHECK(memcmp(want, lcd_screen, sizeof want) == 0, "%s: first draw differs from a clean draw", face_name(f));
        char path[64];
        snprintf(path, sizeof path, "out/face_Rosto-%.2s.ppm", face_name((face_id_t)f));
        write_ppm(path, ref[f]);
    }
}

/* ------------------------------------------------------------ 2. changes */
static const char *MODES[4] = { "rest", "mid-blink", "mid-glance", "mid-motion" };

static void test_changes(void)
{
    static long px[FACE_COUNT][FACE_COUNT];
    long worst_px = 0; int wa = 0, wb = 0; uint32_t worst_t = 0; int ta = 0, tb = 0;
    uint32_t worst_frame = 0; long overs = 0, changes = 0;
    char buf[96];

    for (int m = 0; m < 4; m++)
        for (int a = 0; a < FACE_COUNT; a++)
            for (int b = 0; b < FACE_COUNT; b++) {
                snprintf(buf, sizeof buf, "%s -> %s (%s)", face_name(a), face_name(b), MODES[m]);
                ctx = buf;
                start_on((face_id_t)a);
                run_reset();
                switch (m) {
                case 1: run_for(300, 0); face_blink(); run_for(40, 0); break;
                case 2: run_for(300, 0); face_set_gaze(9, 3); run_for(30, 0); break;
                case 3: run_for(2000 + (uint32_t)(a * 977 + b * 331) % 6000, 0); break;
                default: break;
                }
                run_reset();
                face_show((face_id_t)b);
                CHECK(face_current() == (face_id_t)b, "%s: face_current wrong", buf);
                uint32_t t0 = t_now;
                while (face_changing() && t_now - t0 < 3000) run_for(1, 0);
                CHECK(!face_changing(), "%s: change not finished after 3 s", buf);
                uint32_t took = t_now - t0;
                long sent = R.px;
                if (a == b) CHECK(sent == 0 && took == 0, "%s: change to itself sent %ld px", buf, sent);
                run_for(200, 0);                             /* new face lives on */
                CHECK(R.mismatch == 0, "%s: screen wrong", buf);
                if (m == 0) px[a][b] = sent;
                if (sent > worst_px) { worst_px = sent; wa = a; wb = b; }
                if (took > worst_t) { worst_t = took; ta = a; tb = b; }
                if (R.worst_ns > worst_frame) worst_frame = R.worst_ns;
                overs += R.over;
                changes++;
            }
    ctx = "";

    printf("Face changes: %ld (169 x rest / mid-blink / mid-glance / mid-motion)\n", changes);
    printf("Pixels sent per change from rest, in thousands (row = from, column = to):\n      ");
    for (int b = 0; b < FACE_COUNT; b++) printf("%5.2s", face_name(b));
    printf("\n");
    for (int a = 0; a < FACE_COUNT; a++) {
        printf("  %.2s  ", face_name(a));
        for (int b = 0; b < FACE_COUNT; b++) printf("%5.1f", px[a][b] / 1000.0);
        printf("\n");
    }
    printf("Most pixels: %s -> %s, %ld px (%.0f ms of SPI, spread over frames)\n",
           face_name(wa), face_name(wb), worst_px, worst_px * SPI_NS_PX / 1e6);
    printf("Longest change: %s -> %s, %u ms (eyes close, swap, open)\n", face_name(ta), face_name(tb), worst_t);
    printf("Worst frame during changes: %.1f ms, frames over budget: %ld\n\n", ms_of(worst_frame), overs);
    CHECK(overs == 0, "face changes: %ld frames over the 25 ms budget", overs);
}

/* ------------------------------------------------------------ 3. long runs */
static void anim_frames(face_id_t f)
{
    char dir[] = "out/anim", path[64];
    mkdir(dir, 0755);
    start_on(f);
    for (int k = 0; k < 120; k++) {                      /* 6 s, a frame every 50 ms */
        run_for(50, 0);
        snprintf(path, sizeof path, "out/anim/Rosto-%.2s_%03d.ppm", face_name(f), k);
        write_ppm(path, lcd_screen);
    }
}

static void test_long(void)
{
    printf("%d simulated minutes per face (model board time per frame, 25 ms budget):\n", MINUTES);
    printf("  face           CPU%%  worst ms  over  avg kpx/s  max gap ms  moves/s  overlap\n");
    for (int f = 0; f < FACE_COUNT; f++) {
        char buf[64];
        snprintf(buf, sizeof buf, "%s long run", face_name(f));
        ctx = buf;
        start_on((face_id_t)f);
        long rest_overlap = face_test_overlap();
        run_reset();
        run_for(MINUTES * 60000u, 5);
        double secs = MINUTES * 60.0;
        printf("  %-14s %5.1f  %8.1f  %4ld  %9.1f  %10u  %7.1f  %ld\n", face_name(f),
               R.ns / (secs * 1e9) * 100.0, ms_of(R.worst_ns), R.over, R.px / secs / 1000.0,
               R.max_gap, R.hashes / secs, R.overlap);
        CHECK(R.over == 0, "%s: %ld frames over budget", buf, R.over);
        CHECK(R.max_gap <= 2000, "%s: no pixels sent for %u ms", buf, R.max_gap);
        CHECK(R.overlap <= rest_overlap, "%s: groups' ink overlap (%ld px, %ld at rest)", buf, R.overlap, rest_overlap);
        CHECK(R.mismatch == 0, "%s: %ld wrong frames", buf, R.mismatch);
    }
    ctx = "";
    printf("\n");
}

/* ------------------------------------------------------------ 3b. slow board */
/* The closed loop must hold 25 ms even when the board is much slower than the
 * model: run every face and some changes with the clock 1.8x the model. */
static void test_slow(void)
{
    char buf[64];
    double worst = 0; long over = 0, wrong = 0;
    clock_factor = 1.8;
    for (int f = 0; f < FACE_COUNT; f++) {
        snprintf(buf, sizeof buf, "%s slow board", face_name(f));
        ctx = buf;
        start_on((face_id_t)f);
        run_reset();
        run_for(60000, 0);
        face_show((face_id_t)((f + 9) % FACE_COUNT));     /* a big change too */
        run_for(2000, 0);
        if (R.worst_ns > worst) worst = R.worst_ns;
        over += R.over; wrong += R.mismatch;
    }
    clock_factor = 1.0;
    ctx = "";
    printf("Board 1.8x slower than the model, 1 min per face + a change: worst tick %.1f ms, over 25 ms: %ld, wrong frames: %ld\n\n",
           worst / 1e6, over, wrong);
    CHECK(over == 0, "slow board: %ld ticks over 25 ms", over);
    CHECK(wrong == 0, "slow board: %ld wrong frames", wrong);
}

/* ------------------------------------------------------------ 4. API */
static void test_api(void)
{
    ctx = "api";
    start_on(FACE_HAPPY);
    run_reset();
    run_for(1000, 0);
    face_set_gaze(9, 0);                                 /* hold a look */
    run_for(3000, 0);
    face_set_gaze(0, 0);                                 /* resume the pattern */
    run_for(3000, 0);
    face_show(FACE_HAPPY);                               /* same face: nothing */
    CHECK(!face_changing(), "face_show(same) started a change");
    face_set_active(0);                                  /* menu owns the screen */
    face_set_clock(fake_clock, 1);
    lcd_stats_reset();
    for (uint32_t end = t_now + 2000; t_now < end; t_now++) face_tick(t_now);
    CHECK(lcd_stats.px == 0, "inactive face drew %ld px", lcd_stats.px);
    face_show(FACE_LOVE);                                /* changed while hidden */
    face_set_active(1);
    face_test_render(want);
    CHECK(memcmp(want, lcd_screen, sizeof want) == 0, "after face_set_active(1): screen wrong");
    CHECK(face_current() == FACE_LOVE && !face_changing(), "face_show while inactive not applied");
    lcd_stats_reset();
    run_reset();
    face_show(FACE_SAD); run_for(30, 0);
    face_show(FACE_SLEEPY);                              /* asked again mid-change */
    while (face_changing() && R.frames < 300) run_for(1, 0);
    CHECK(face_current() == FACE_SLEEPY && !face_changing(), "second face_show mid-change lost");
    run_for(500, 0);
    face_set(FACE_EYES_NORMAL, FACE_MOUTH_SMILE, FACE_OVERLAY_NONE);
    while (face_changing() && R.frames < 600) run_for(1, 0);
    CHECK(face_current() == FACE_IDLE, "face_set did not show the idle face");
    CHECK(R.mismatch == 0 && R.over == 0, "api: wrong frames or over budget");
    printf("API checks done (gaze lock/resume, inactive, show while hidden, show mid-change, face_set)\n\n");
    ctx = "";
}

/* ------------------------------------------------------------ 5. strips */
static uint16_t mix(uint16_t fg, uint16_t bg, int a)    /* copy of gfx_spi.c */
{
    int r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * (15 - a) + 7) / 15;
    int g = (((fg >> 5)  & 0x3F) * a + ((bg >> 5)  & 0x3F) * (15 - a) + 7) / 15;
    int b = (( fg        & 0x1F) * a + ( bg        & 0x1F) * (15 - a) + 7) / 15;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void test_strips(void)
{
    const face_part_t *p = &bmo_faces[BMO_ROSTO_21].parts[0];      /* 136 x 123 heart */

    face_set_active(0);
    gfx_fill_rect(0, 0, GFX_W, GFX_H, C_BG);
    for (int i = 0; i < 480 * 320; i++) want[i] = C_BG;
    for (int sy = 0; sy < p->h; sy++)
        for (int sx = 0; sx < p->w; sx++) {
            int k = sy * p->w + sx, a = (k & 1) ? (p->a4[k >> 1] & 0x0F) : (p->a4[k >> 1] >> 4);
            uint16_t *d = &want[(p->y + sy) * 480 + p->x + sx];
            if (a) *d = (a == 15) ? p->color : mix(p->color, *d, a);
        }

    lcd_stats_reset();
    gfx_blend_sprite(p->a4, p->w, p->h, p->x, p->y, p->color);
    long d = 0;
    for (int i = 0; i < 480 * 320; i++) d += want[i] != lcd_screen[i];
    printf("Big sprite, no box open: %d x %d in %d strips, %ld px differ\n\n", p->w, p->h, lcd_stats.pushes, d);
    CHECK(d == 0, "big sprite drawn wrong");
    CHECK(lcd_stats.pushes > 1, "big sprite not split into strips");
    CHECK(lcd_stats.max_push_px <= GFX_BOX_MAX_PX, "strip bigger than the box");
}

int main(int argc, char **argv)
{
    int quick = argc > 1 && strcmp(argv[1], "--quick") == 0;   /* skip the long runs */
    gfx_init();
    lcd_stats_reset();
    if (argc == 2 && strcmp(argv[1], "--anim") == 0) {          /* frames for make_gifs.py */
        for (int f = 0; f < FACE_COUNT; f++) anim_frames((face_id_t)f);
        return fails != 0;
    }
    if (argc == 4 && strcmp(argv[1], "--face") == 0) {          /* one face, N minutes */
        int f = atoi(argv[2]), min = atoi(argv[3]);
        if (getenv("CLOCK")) clock_factor = atof(getenv("CLOCK"));
        start_on((face_id_t)f);
        run_reset();
        run_for((uint32_t)min * 60000u, 5);
        extern struct { double comp, rows, box_px, blend_plain, blend_sampled; } face_test_ops;
        double sec = min * 60.0;
        printf("RATES %s px/s %.0f pushes/s %.0f comp/s %.0f rows/s %.0f boxpx/s %.0f plain/s %.0f sampled/s %.0f\n",
               face_name(f), R.px / sec, R.pushes / sec, face_test_ops.comp / sec, face_test_ops.rows / sec,
               face_test_ops.box_px / sec, face_test_ops.blend_plain / sec, face_test_ops.blend_sampled / sec);
        printf("%s %d min: CPU %.1f%%  worst %.1f ms  over %ld  kpx/s %.1f  gap %u  overlap %ld  wrong %ld\n",
               face_name(f), min, R.ns / (min * 60e9) * 100, R.worst_ns / 1e6, R.over,
               R.px / (min * 60.0) / 1000, R.max_gap, R.overlap, R.mismatch);
        return 0;
    }
    test_rest();
    test_changes();
    if (!quick) test_long();
    test_slow();
    test_api();
    test_strips();
    printf(fails ? "%d check(s) FAILED\n" : "All checks passed\n", fails);
    return fails != 0;
}
