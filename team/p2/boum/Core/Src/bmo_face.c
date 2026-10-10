/* bmo_face.c — BMO's 13 Figma faces, each one alive (CLAUDE.md §18).
 *
 * No new art: every face is the stencils in bmo_faces.h, moved, squashed,
 * rotated and recoloured at draw time.
 *
 *   Rig       Six part groups (eye_l, eye_r, brow_l, brow_r, mouth, extra).
 *             Each has an offset (whole pixels, 0.3 px hysteresis), a squash /
 *             stretch, a rotation, a tint, an eyelid and a "closed U" swap.
 *   Life      Breathing, a gaze pattern with parallax and saccades, blinks, and
 *             each face's signature motions. All of it is in FACES[] below.
 *   Renderer  Changed pixels only. For a group that moved, every row of its old
 *             and new box is composed twice in a row buffer (before / after);
 *             only the runs that differ are redrawn, through face_gfx.h.
 *   Valve     Closed loop on the real clock (face_set_clock, DWT on the board),
 *             helped by a timing model: no new group after SOFT_US, and a
 *             group in progress stops between rows before LIMIT_US and finishes
 *             next tick. A group that waits goes first next tick.
 *
 * Only calls face_gfx.h. The whole screen is redrawn only by face_init() and
 * face_set_active(1).
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")        /* pixel loops; the Debug build is -O0 */
#endif

#include <string.h>
#include "bmo_face.h"
#include "face_gfx.h"
#include "bmo_faces.h"            /* included here only: its tables are static */

/* ================================================================ tuning */
#define C_BG            0xCF38    /* #C9E4C3 mint */
#define C_INK           0x0000

#define FRAME_MS        20        /* animation step (50 fps max) */

#define BLINK_MIN_MS    2000      /* gap between blinks (x blink_gap % per face) */
#define BLINK_MAX_MS    6000
#define BLINK_HOLD_MIN  100       /* eyes fully shut */
#define BLINK_HOLD_MAX  150
#define DOUBLE_PCT      15        /* chance of a second blink right after */
#define DOUBLE_GAP_MIN  120
#define DOUBLE_GAP_MAX  220
#define SLOW_BLINK_MS   250       /* closed time of a slow blink */
#define SLOW_BLINK_PCT  30        /* gaze returns to the viewer that end in one */

#define VIEW_HOLD_MIN   1500      /* gaze pattern: look at the viewer ... */
#define VIEW_HOLD_MAX   4000
#define GLANCE_HOLD_MIN 800       /* ... glance, hold, small adjust, hold ... */
#define GLANCE_HOLD_MAX 2000
#define GLANCE_PX_MIN   6
#define GLANCE_PX_MAX   9
#define GLANCE_PX_Y     2
#define ADJUST_PX       2         /* small adjust: 1..2 px */
#define SHY_X           8         /* 05: looks away down to one side */
#define SHY_Y           4
#define SHY_AWAY_MIN    1000
#define SHY_AWAY_MAX    2000
#define SHY_PEEK_MS     500
#define DART_MIN_MS     500       /* 07: darting glances */
#define DART_MAX_MS     1500
#define DART_WIDEN      1.10f     /* eyes widen on each dart ... */
#define DART_WIDEN_MS   250       /* ... and relax over this time */
#define LOCK_PX_X       9         /* face_set_gaze() is clamped to this */
#define LOCK_PX_Y       4

#define SACC_JUMP_MS    50        /* saccade: fast jump past the target ... */
#define SACC_OVER_MIN   1         /* ... by 1-2 px ... */
#define SACC_OVER_MAX   2
#define SACC_SETTLE_MIN 80        /* ... then settle back */
#define SACC_SETTLE_MAX 120
#define SACC_SQUASH     0.90f     /* eye sy during the jump */

#define DRIFT_MIN_MS    1000      /* micro-drift on top of everything */
#define DRIFT_MAX_MS    3000
#define DRIFT_PX_X      2
#define DRIFT_PX_Y      1
#define DRIFT_K         0.06f

#define HYST_PX         0.3f      /* a value must pass a rounding edge by this much */
#define HEART_STEP      0.04f     /* 21: heart scale moves in steps (each step redraws) */
#define SCALE_MIN       0.80f     /* squash / stretch range of the rig */
#define SCALE_MAX       1.15f

/* Parallax: how far each group moves when the gaze moves (head turn). */
static const float PARALLAX[6] = { 1.0f, 1.0f, 1.0f, 1.0f, 0.45f, 0.6f };

/* ---------------------------------------------------------- timing model
 * Board numbers (216 MHz, SPI5 13.5 MHz, polling), calibrated 6 Oct from the
 * FACE_DEMO log (21, 24, 25): transfer x1.22, compute x1.85 of the first guess.
 * The model plans the work; the real clock (face_set_clock) has the last word. */
#define SPI_NS_PX       1446      /* 16 bits at 13.5 MHz + HAL per-byte overhead */
#define PUSH_NS         30500     /* window commands + HAL call overhead */
#define BOX_NS_PX       37        /* gfx_begin clear + mint fill, per box pixel */
#define BLEND_NS_PX     111       /* gfx_blend_sprite, per pixel of a part */
#define COMP_NS_PX      74        /* compose one part sample (scaled / turned) */
#define COMP_PLAIN_NS_PX 74       /* compose one sample of a whole-pixel part (not yet measured) */
#define ROW_NS          555       /* per part per row set-up */

/* ---------------------------------------------------------- valve (closed loop) */
#define SOFT_US         18000     /* no new group starts after this much of a tick */
#define LIMIT_US        24000     /* a group in progress stops (between rows) before this */
#define SAFETY          2         /* model estimates are doubled when deciding to stop */

/* ---------------------------------------------------------- renderer */
#define GAP_PX          12        /* changed runs closer than this are joined */
#define BOX_FLUSH_PX    2048      /* boxes stay small, so stopping mid-group is quick */
#define MAX_RUNS        6         /* changed runs kept per row (more are joined) */
#define PUSH_PX         (PUSH_NS / SPI_NS_PX)   /* a push costs as much as this many pixels */
#define ROWBUF          128       /* row buffer width (two on the stack) */
#define XP_MAX          12        /* drawn entries per face: parts + 2 lids */
#define SWAP_MAX        24        /* areas of a face change */

/* ================================================================ faces */
enum { G_EYE_L, G_EYE_R, G_BROW_L, G_BROW_R, G_MOUTH, G_EXTRA, NG };
#define M_EYE_L   (1 << G_EYE_L)
#define M_EYE_R   (1 << G_EYE_R)
#define M_EYES    (M_EYE_L | M_EYE_R)
#define M_BROWS   ((1 << G_BROW_L) | (1 << G_BROW_R))
#define M_MOUTH   (1 << G_MOUTH)
#define M_EXTRA   (1 << G_EXTRA)
#define M_ALL     0x3F

enum { BL_NONE, BL_U, BL_LINE, BL_RIM };      /* blink style (closed shape) */
enum { GZ_NORMAL, GZ_SHY, GZ_DART };          /* gaze pattern */

/* Motion types. Amounts a, b, c, d; px are in tenths (PX(1.5) = 15).
 *   TILT      a px: left eye+brow up, right down; dur = hold
 *   STRETCH   a = mouth sx %; dur = hold
 *   BOUNCE    a px up, dur ms up (and down); b = landing sy %, c ms; pct = double
 *   SQUINT    eyes become the closed U for dur ms
 *   SIGH      a px down: b ms sink, dur hold, c ms rise
 *   SWAY      a px side to side, period dur; eyes and brows lag b ms
 *   PULSE     extra colour fades a/16 toward mint and back, period dur
 *   TREMBLE   a px jitter at c Hz always on groups d (mask); bursts of b px,
 *             2c Hz, dur ms, on the whole face
 *   HUFF      shake a px 3x in dur ms; brows down b px; mouth sx c %
 *   POP       eyes scale to a %, then b %, then 100 %, dur ms per step
 *   SHIMMER   white highlights step a px, every dur ms
 *   WIGGLE    whole face side to side a px, b times in dur ms
 *   SQUEEZE   one eye sx a % for dur ms, eyes take turns
 *   BOB       mouth side to side a px, period dur
 *   HEARTBEAT eyes scale a % (lub) then b % (dub) every dur ms, in steps of
 *             HEART_STEP; colour +c/16 while beating (0 = off: a recolour
 *             redraws the whole heart); extras flick up d px on each beat
 *   SPIN      eyes turn once per dur ms in c steps per turn; a = +1/-1
 *             right eye direction; b px: turn centre below the box centre
 *   CIRCLE    whole face moves in a circle of radius a px, period dur
 *   WOBBLE    whole face side to side a px, period dur
 *   TWITCH    one eye jumps up a px for dur ms
 *   NOD       face sinks a px over dur ms, jerks back up in b ms
 *   ZZZ       "z" then "Z" float up from (a, b) by c px over dur ms
 *   PUFF      mouth sx up to a % and back, period dur, a quarter cycle after
 *             the breath (moves while the breath turns, so the face never stops)
 * gap_min/gap_max: random wait between runs; 0, 0 = always running. */
enum {
    MO_NONE, MO_TILT, MO_STRETCH, MO_BOUNCE, MO_SQUINT, MO_SIGH, MO_SWAY,
    MO_PULSE, MO_TREMBLE, MO_HUFF, MO_POP, MO_SHIMMER, MO_WIGGLE, MO_SQUEEZE,
    MO_BOB, MO_HEARTBEAT, MO_SPIN, MO_CIRCLE, MO_WOBBLE, MO_TWITCH, MO_NOD, MO_ZZZ,
    MO_PUFF,
};

typedef struct {
    uint8_t  type, pct;
    uint16_t gap_min, gap_max, dur;
    int16_t  a, b, c, d;
} motion_t;

typedef struct {
    uint16_t breath_ms;       /* breathing period                          */
    uint8_t  breath_px;       /* breathing amplitude, PX()                 */
    uint8_t  blink;           /* BL_...                                    */
    uint8_t  blink_gap;       /* % of BLINK_MIN/MAX_MS                     */
    uint8_t  lid_pct;         /* % of normal lid step time (200 = heavy)   */
    uint8_t  gaze;            /* GZ_...                                    */
    uint8_t  gaze_range;      /* % of the glance distance (collision limit) */
    uint8_t  gaze_hold;       /* % of the hold times (70 = busy eyes)      */
    int8_t   gaze_dy;         /* where "at the viewer" sits, px (03 looks low) */
    uint16_t step_ms;         /* 0 = move every frame; else stop-motion steps (24) */
    motion_t m[3];
} face_cfg_t;

#define PX(v) ((int16_t)((v) * 10))

/* One row per face, in face_id_t order. Tune here. */
static const face_cfg_t FACES[FACE_COUNT] = {
    /* 01 Idle: curious tilt, mouth stretch */
    { 4000, PX(1.5), BL_U, 100, 100, GZ_NORMAL, 100, 100, 0, 0, {
        { MO_TILT,      0, 6000, 12000, 1000, PX(2), 0, 0, 0 },
        { MO_STRETCH,   0, 5000, 11000,  500, 110, 0, 0, 0 } } },
    /* 02 Happy: bounce (30 % double), happy squint */
    { 3000, PX(1.5), BL_U, 100, 100, GZ_NORMAL, 100,  70, 0, 0, {
        { MO_BOUNCE,   30, 3000,  6000,  150, PX(5), 90, 80, 0 },
        { MO_SQUINT,    0, 4000,  9000,  600, 0, 0, 0, 0 } } },
    /* 03 Sad: sigh, looks low, slow heavy blinks */
    { 5000, PX(2),   BL_LINE, 130, 200, GZ_NORMAL, 60, 140, 4, 0, {
        { MO_SIGH,      0, 6000, 10000,  800, PX(3), 1000, 1500, 0 } } },
    /* 04 Content: gentle sway, eyes lag the mouth */
    { 5000, PX(2),   BL_NONE, 100, 100, GZ_NORMAL, 50, 140, 0, 0, {
        { MO_SWAY,      0,    0,     0, 4000, PX(3), 150, 0, 0 } } },
    /* 05 Shy: looks away and peeks; blush pulses */
    { 3500, PX(1),   BL_U, 80, 100, GZ_SHY, 100, 100, 0, 0, {
        { MO_PULSE,     0,    0,     0, 2000, 8, 0, 0, 0 } } },
    /* 07 Scared: tremble with bursts; darting glances (GZ_DART) */
    { 2000, PX(1),   BL_LINE, 60, 70, GZ_DART, 100, 100, 0, 0, {
        { MO_TREMBLE,   0, 3000,  5000,  400, PX(1), PX(2), 10, M_EYES | M_BROWS } } },
    /* 10 Angry: huff */
    { 3000, PX(1.5), BL_LINE, 120, 100, GZ_NORMAL, 70, 70, 0, 0, {
        { MO_HUFF,      0, 4000,  8000,  300, PX(2), PX(2), 90, 0 } } },
    /* 13 Surprised: eye pop, highlight shimmer */
    { 2500, PX(1),   BL_RIM, 150, 100, GZ_NORMAL, 80, 70, 0, 0, {
        { MO_POP,       0, 4000,  7000,  100, 110, 97, 0, 0 },
        { MO_SHIMMER,   0,    0,     0,  500, PX(1), 0, 0, 0 } } },
    /* 19 Playful: wiggle, > < squeeze, the 3 bobs */
    { 3000, PX(1.5), BL_NONE, 100, 100, GZ_NORMAL, 100, 70, 0, 0, {
        { MO_WIGGLE,    0, 3000,  5000,  600, PX(3), 3, 0, 0 },
        { MO_SQUEEZE,   0, 1500,  3500,  200, 85, 0, 0, 0 },
        { MO_BOB,       0,    0,     0, 1000, PX(2), 0, 0, 0 } } },
    /* 21 Love: lub-dub heartbeat (no brightness flush), calm dreamy gaze */
    { 4000, PX(1.5), BL_NONE, 100, 100, GZ_NORMAL, 40, 200, 0, 150, {
        { MO_HEARTBEAT, 0,    0,     0, 1200, 108, 105, 0, PX(1) } } },
    /* 24 Dizzy: spinning spirals, circling face; stop-motion, 4 steps a second */
    { 3500, PX(1.5), BL_NONE, 100, 100, GZ_NORMAL, 40, 140, 0, 250, {
        { MO_SPIN,      0,    0,     0, 3000, -1, PX(3), 12, 0 },
        { MO_CIRCLE,    0,    0,     0, 3000, PX(1.5), 0, 0, 0 } } },
    /* 25 Knocked out: wobble, twitch */
    { 6000, PX(1),   BL_NONE, 100, 100, GZ_NORMAL, 30, 200, 0, 0, {
        { MO_WOBBLE,    0,    0,     0, 4000, PX(2), 0, 0, 0 },
        { MO_TWITCH,   50, 8000, 15000,  120, PX(2), 0, 0, 0 } } },
    /* 29 Sleepy: nods off, z Z */
    { 6000, PX(2),   BL_NONE, 100, 100, GZ_NORMAL, 30, 200, 0, 0, {
        { MO_NOD,       0, 8000, 14000, 3000, PX(4), 150, 0, 0 },
        { MO_ZZZ,       0, 4000,  7000, 3000, 432, 96, 60, 0 },
        { MO_PUFF,      0,    0,     0, 6000, 106, 0, 0, 0 } } },
};

static const char *const FACE_NAMES[FACE_COUNT] = {
    "01 Idle", "02 Happy", "03 Sad", "04 Content", "05 Shy", "07 Scared", "10 Angry",
    "13 Surprised", "19 Playful", "21 Love", "24 Dizzy", "25 Knocked out", "29 Sleepy",
};

/* face_id_t indexes bmo_faces[] directly: keep both lists in the same order. */
#define SAME_ID(f, r) _Static_assert((int)(f) == (int)(r), #f " must equal " #r)
SAME_ID(FACE_COUNT, BMO_FACE_COUNT);
SAME_ID(FACE_IDLE, BMO_ROSTO_01);       SAME_ID(FACE_HAPPY, BMO_ROSTO_02);
SAME_ID(FACE_SAD, BMO_ROSTO_03);        SAME_ID(FACE_CONTENT, BMO_ROSTO_04);
SAME_ID(FACE_SHY, BMO_ROSTO_05);        SAME_ID(FACE_SCARED, BMO_ROSTO_07);
SAME_ID(FACE_ANGRY, BMO_ROSTO_10);      SAME_ID(FACE_SURPRISED, BMO_ROSTO_13);
SAME_ID(FACE_PLAYFUL, BMO_ROSTO_19);    SAME_ID(FACE_LOVE, BMO_ROSTO_21);
SAME_ID(FACE_DIZZY, BMO_ROSTO_24);      SAME_ID(FACE_KNOCKED_OUT, BMO_ROSTO_25);
SAME_ID(FACE_SLEEPY, BMO_ROSTO_29);
_Static_assert((int)PART_EYE_L == (int)G_EYE_L && (int)PART_EXTRA == (int)G_EXTRA, "part role = group");

/* ================================================================ blink steps */
typedef struct { uint8_t lid, var, sy, ms; } bstep_t;  /* lid %, U swap, sy %, ms (0 = hold) */

static const bstep_t SEQ_U[]    = { {40,0,100,25}, {75,0,100,25}, {0,1,100,0},  {75,0,100,35}, {40,0,100,35} };
static const bstep_t SEQ_LINE[] = { {0,0,55,25},   {0,0,28,25},   {0,0,12,0},   {0,0,28,35},   {0,0,55,35} };
static const bstep_t SEQ_RIM[]  = { {40,0,100,25}, {70,0,100,25}, {85,0,100,0}, {70,0,100,35}, {40,0,100,35} };
static const bstep_t SEQ_SQ[]   = { {0,0,65,30},   {0,0,30,0},    {0,0,65,30} };  /* face change, no-blink faces */
#define SEQ_LEN(s) ((int)(sizeof s / sizeof s[0]))

static const bstep_t *seq_of(int style, int *n, int *hold)
{
    switch (style) {
    case BL_U:    *n = SEQ_LEN(SEQ_U);    *hold = 2; return SEQ_U;
    case BL_LINE: *n = SEQ_LEN(SEQ_LINE); *hold = 2; return SEQ_LINE;
    case BL_RIM:  *n = SEQ_LEN(SEQ_RIM);  *hold = 2; return SEQ_RIM;
    default:      *n = SEQ_LEN(SEQ_SQ);   *hold = 1; return SEQ_SQ;
    }
}

/* ================================================================ types */
typedef struct { int16_t x, y, w, h; } rect_t;

/* What one group looks like on screen. */
typedef struct {
    int16_t  x, y;            /* offset, px                                 */
    uint16_t sx, sy;          /* Q8 squash / stretch (256 = 1.0)             */
    int16_t  ang;             /* rotation, 1/1024 turn                      */
    int8_t   tint;            /* <0 fades toward mint, >0 brighter, /16     */
    uint8_t  var;             /* 1 = closed U (Rosto-04 eye) instead of own */
    uint8_t  lid;             /* % of the eye covered by a mint lid          */
    int8_t   hx, hy;          /* extra shift of white parts (13 highlights) */
    uint8_t  pad;             /* keep 0: states are compared with memcmp    */
} gst_t;

typedef struct { uint8_t face, pad; gst_t g[NG]; } view_t;

typedef struct { int16_t x, y; uint8_t s; } ztext_t;  /* s = text scale, 0 = none */

/* One drawn entry: a part placed by its group's transform, or an eyelid. */
typedef struct {
    const uint8_t *a4;        /* NULL = solid fill (eyelid)                   */
    int16_t  x0, y0, x1, y1;  /* screen box holding every inked pixel         */
    uint16_t w, h, color;
    uint8_t  grp, plain;      /* plain: moved by whole pixels only            */
    int16_t  px, py;          /* plain: top-left on screen                    */
    int32_t  u0, v0, dudx, dvdx, dudy, dvdy;   /* Q16: screen -> stencil     */
} xpart_t;

typedef struct {              /* float targets, before snapping */
    float x, y, sx, sy, bsy, ang, tint, hx, hy;
    uint8_t var, lid;
} acc_t;

typedef struct { uint32_t next, start; uint8_t on, var, n; } mstate_t;

/* ================================================================ state */
static face_id_t cur_face, want_face;
static int       active, started;
static uint32_t  rng, last_frame, face_t0, tick_n;
static uint32_t  last_call;           /* now_ms of the previous face_tick() */

static view_t    cur;                 /* what is on the screen now        */
static view_t    tgt;                 /* where this tick wants it         */
static view_t    nxt;                 /* face change: new face, eyes shut */
static ztext_t   z_drawn, z_want;
static uint8_t   waiting[NG];         /* ticks a group has been deferred  */

enum { T_NONE, T_CLOSE, T_SWAP, T_OPEN };
static uint8_t   trans;
static int8_t    tr_i;
static uint32_t  tr_end;
static rect_t    sw[SWAP_MAX];
static uint8_t   sw_n, sw_k;
static int16_t   sw_y;

static int8_t    bl_i = -1;           /* blink step, -1 = eyes open */
static uint8_t   bl_double;
static uint16_t  bl_hold;
static uint32_t  bl_end, bl_next;

enum { GS_VIEW, GS_GLANCE, GS_ADJUST };
static uint8_t   gz_st, gz_locked;
static int8_t    gz_side;
static uint16_t  gz_settle;
static uint32_t  gz_until, gz_t0, gz_widen_t0, gz_slow_blink;
static float     gz_fx, gz_fy, gz_tx, gz_ty, gz_ox, gz_oy;
static float     dr_x, dr_y, dr_tx, dr_ty;
static uint32_t  dr_next;

static mstate_t  ms[3];

static uint8_t   inkr_face = 0xFF;    /* rotation: ink radius per part, cached */
static uint8_t   inkr[XP_MAX];

static uint32_t  tick_ns;             /* model cost of the last frame */
static uint32_t  spent_ns;            /* model cost so far in this frame */
static uint32_t  (*clk)(void);        /* real clock (face_set_clock), may be NULL */
static uint32_t  clk_per_us = 1, tick_c0;
static uint16_t  rows_this_tick;      /* a job always makes some progress */
static uint16_t  est_us[NG];          /* what each group cost last time (real clock) */
static uint32_t  life_last;           /* last life_update (step_ms faces) */

enum { J_NONE, J_GROUP, J_SWAP };     /* the job: cur -> nxt over sw[], row by row */
static uint8_t   job, job_g;
#ifdef FACE_TEST
struct { double comp, rows, box_px, blend_plain, blend_sampled; } face_test_ops;   /* calibration */
#endif
static uint32_t  face_salt;           /* random per face visit (tremble pattern) */

/* ================================================================ maths */
static uint32_t xorshift(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int rnd(int lo, int hi)                 /* inclusive */
{
    return lo + (int)(xorshift() % (uint32_t)(hi - lo + 1));
}

static int   iround(float v)          { return (int)(v + (v >= 0 ? 0.5f : -0.5f)); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int   imin(int a, int b)       { return a < b ? a : b; }
static int   imax(int a, int b)       { return a > b ? a : b; }
static int   ifloor(float v)          { int i = (int)v; return (float)i > v ? i - 1 : i; }

static float fsin_turn(float t)                /* sin(2 pi t), no libm */
{
    t -= (float)ifloor(t);                       /* 0..1 */
    float x = t < 0.5f ? t : t - 1.0f;           /* -0.5..0.5 */
    if (x > 0.25f) x = 0.5f - x; else if (x < -0.25f) x = -0.5f - x;
    float y = x * 6.2831853f, y2 = y * y;
    return y * (1 - y2 / 6 * (1 - y2 / 20 * (1 - y2 / 42)));
}
static float fcos_turn(float t) { return fsin_turn(t + 0.25f); }

static float ease(float x)                     /* smoothstep 0..1 */
{
    x = clampf(x, 0, 1);
    return x * x * (3 - 2 * x);
}

/* 0 -> 1 over `ramp` ms, hold, 1 -> 0 over the last `ramp` ms of `len`. */
static float env(uint32_t t, uint32_t len, uint32_t ramp)
{
    if (t < ramp) return ease((float)t / ramp);
    if (t + ramp > len) return ease((float)(len - t) / ramp);
    return 1;
}

static float lerp(float a, float b, float k) { return a + (b - a) * k; }

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}

static uint16_t mix(uint16_t fg, uint16_t bg, int a)   /* same as gfx_spi.c */
{
    int r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * (15 - a) + 7) / 15;
    int g = (((fg >> 5)  & 0x3F) * a + ((bg >> 5)  & 0x3F) * (15 - a) + 7) / 15;
    int b = (( fg        & 0x1F) * a + ( bg        & 0x1F) * (15 - a) + 7) / 15;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint16_t tinted(uint16_t c, int t)
{
    if (t == 0 || c == C_INK) return c;
    if (t < 0) return mix(C_BG, c, (-t * 15 + 8) / 16);        /* softer */
    int r = imin(31, ((c >> 11) & 0x1F) * (16 + t) / 16);      /* brighter */
    int g = imin(63, ((c >> 5) & 0x3F) * (16 + t) / 16);
    int b = imin(31, (c & 0x1F) * (16 + t) / 16);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static int isqrt(int v)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= v) r++;
    return r;
}

/* ================================================================ rects */
static int overlaps(const rect_t *a, const rect_t *b)
{
    return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

static void grow(rect_t *a, const rect_t *b)  /* a = bounding box of a and b */
{
    int x0 = imin(a->x, b->x), y0 = imin(a->y, b->y);
    int x1 = imax(a->x + a->w, b->x + b->w), y1 = imax(a->y + a->h, b->y + b->h);
    a->x = (int16_t)x0; a->y = (int16_t)y0; a->w = (int16_t)(x1 - x0); a->h = (int16_t)(y1 - y0);
}

/* Merge overlapping boxes until none overlap. */
static int merge_areas(rect_t *r, int n)
{
    for (int again = 1; again; ) {
        again = 0;
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++)
                if (overlaps(&r[i], &r[j])) {
                    grow(&r[i], &r[j]);
                    r[j--] = r[--n];
                    again = 1;
                }
    }
    return n;
}

static rect_t xp_rect(const xpart_t *p)
{
    return (rect_t){ p->x0, p->y0, (int16_t)(p->x1 - p->x0), (int16_t)(p->y1 - p->y0) };
}

/* ================================================================ placing parts */
static const face_part_t *closed_u(int g)     /* Rosto-04's eye for group g */
{
    const face_def_t *d = &bmo_faces[BMO_ROSTO_04];
    for (int i = 0; i < d->count; i++)
        if (d->parts[i].role == g) return &d->parts[i];
    return 0;
}

static const motion_t *find_motion(face_id_t f, int type)
{
    for (int i = 0; i < 3; i++)
        if (FACES[f].m[i].type == type) return &FACES[f].m[i];
    return 0;
}

/* Rest centre of each group: the point it scales and turns around. */
static void pivots(face_id_t f, float *pvx, float *pvy)
{
    const face_def_t *d = &bmo_faces[f];
    rect_t r[NG]; int have[NG] = { 0 };
    for (int i = 0; i < d->count; i++) {
        const face_part_t *p = &d->parts[i];
        rect_t pr = { p->x, p->y, (int16_t)p->w, (int16_t)p->h };
        if (have[p->role]) grow(&r[p->role], &pr); else { r[p->role] = pr; have[p->role] = 1; }
    }
    const motion_t *spin = find_motion(f, MO_SPIN);
    for (int g = 0; g < NG; g++) {
        pvx[g] = have[g] ? r[g].x + r[g].w * 0.5f : 0;
        pvy[g] = have[g] ? r[g].y + r[g].h * 0.5f : 0;
        if (spin && g <= G_EYE_R) pvy[g] += spin->b / 10.0f;
    }
}

/* Ink radius of part i around its group pivot (only needed when it turns). */
static int ink_radius(face_id_t f, int i, float pvx, float pvy)
{
    if (inkr_face != f) {
        memset(inkr, 0, sizeof inkr);
        inkr_face = (uint8_t)f;
    }
    if (inkr[i]) return inkr[i];
    const face_part_t *p = &bmo_faces[f].parts[i];
    float cx = pvx - p->x, cy = pvy - p->y;
    int best = 0;
    for (int v = 0; v < p->h; v++)
        for (int u = 0; u < p->w; u++) {
            int k = v * p->w + u;
            if (!((k & 1) ? (p->a4[k >> 1] & 0x0F) : (p->a4[k >> 1] >> 4))) continue;
            float dx = u + 0.5f - cx, dy = v + 0.5f - cy;
            int d2 = (int)(dx * dx + dy * dy);
            if (d2 > best) best = d2;
        }
    inkr[i] = (uint8_t)imin(255, isqrt(best) + 2);
    return inkr[i];
}

static int32_t q16(float v) { return (int32_t)(v * 65536.0f + (v >= 0 ? 0.5f : -0.5f)); }

static void place(xpart_t *e, const face_part_t *p, int g, const gst_t *s,
                  float pvx, float pvy, int radius)
{
    int white = (p->color == 0xFFFF && g <= G_EYE_R);
    int ox = s->x + (white ? s->hx : 0), oy = s->y + (white ? s->hy : 0);

    e->a4 = p->a4; e->w = p->w; e->h = p->h; e->grp = (uint8_t)g;
    e->color = tinted(p->color, s->tint);

    if (s->sx == 256 && s->sy == 256 && s->ang == 0) {      /* whole-pixel move */
        e->plain = 1;
        e->px = (int16_t)(p->x + ox); e->py = (int16_t)(p->y + oy);
        e->x0 = e->px; e->y0 = e->py; e->x1 = (int16_t)(e->px + p->w); e->y1 = (int16_t)(e->py + p->h);
        e->dudx = e->dvdy = 65536; e->dvdx = e->dudy = 0;
        e->u0 = -(int32_t)e->px * 65536 + 32768;
        e->v0 = -(int32_t)e->py * 65536 + 32768;
    } else {                                                /* scale / turn */
        float sx = s->sx / 256.0f, sy = s->sy / 256.0f, c = 1, sn = 0;
        if (s->ang) { c = fcos_turn(s->ang / 1024.0f); sn = fsin_turn(s->ang / 1024.0f); }
        float a = c / sx, b = sn / sx, cc = -sn / sy, d = c / sy;
        float qx = 0.5f - pvx - ox, qy = 0.5f - pvy - oy;
        e->plain = 0;
        e->dudx = q16(a); e->dudy = q16(b); e->dvdx = q16(cc); e->dvdy = q16(d);
        e->u0 = q16(pvx - p->x + a * qx + b * qy);
        e->v0 = q16(pvy - p->y + cc * qx + d * qy);
        if (s->ang) {
            float r = radius * (sx > sy ? sx : sy);
            e->x0 = (int16_t)ifloor(pvx + ox - r);  e->x1 = (int16_t)(ifloor(pvx + ox + r) + 2);
            e->y0 = (int16_t)ifloor(pvy + oy - r);  e->y1 = (int16_t)(ifloor(pvy + oy + r) + 2);
        } else {
            e->x0 = (int16_t)(ifloor(pvx + ox + (p->x - pvx) * sx) - 1);
            e->x1 = (int16_t)(ifloor(pvx + ox + (p->x + p->w - pvx) * sx) + 2);
            e->y0 = (int16_t)(ifloor(pvy + oy + (p->y - pvy) * sy) - 1);
            e->y1 = (int16_t)(ifloor(pvy + oy + (p->y + p->h - pvy) * sy) + 2);
        }
    }
    if (e->x0 < 0) e->x0 = 0;
    if (e->y0 < 0) e->y0 = 0;
    if (e->x1 > GFX_W) e->x1 = GFX_W;
    if (e->y1 > GFX_H) e->y1 = GFX_H;
    if (e->x1 < e->x0) e->x1 = e->x0;
    if (e->y1 < e->y0) e->y1 = e->y0;
}

/* Everything a view draws, back to front: parts in table order, each eye's
 * lid right after that eye's last part (so brows drawn later stay on top). */
static int build(const view_t *v, xpart_t *xp)
{
    const face_def_t *d = &bmo_faces[v->face];
    float pvx[NG], pvy[NG];
    int last[NG], n = 0, done_u[2] = { 0, 0 };

    pivots((face_id_t)v->face, pvx, pvy);
    for (int g = 0; g < NG; g++) last[g] = -1;
    for (int i = 0; i < d->count; i++) last[d->parts[i].role] = i;

    for (int i = 0; i < d->count; i++) {
        const face_part_t *p = &d->parts[i];
        int g = p->role;
        const gst_t *s = &v->g[g];
        if (g <= G_EYE_R && s->var) {                 /* closed U instead */
            if (!done_u[g]) {
                const face_part_t *u = closed_u(g);
                if (u) place(&xp[n++], u, g, s, pvx[g], pvy[g], 0);
                done_u[g] = 1;
            }
        } else {
            place(&xp[n++], p, g, s, pvx[g], pvy[g],
                  s->ang ? ink_radius((face_id_t)v->face, i, pvx[g], pvy[g]) : 0);
        }
        if (g <= G_EYE_R && i == last[g] && s->lid) {
            xpart_t *e = &xp[n];
            int x0 = GFX_W, y0 = GFX_H, x1 = 0, y1 = 0;
            for (int k = 0; k < n; k++)
                if (xp[k].grp == g && xp[k].a4) {
                    x0 = imin(x0, xp[k].x0); y0 = imin(y0, xp[k].y0);
                    x1 = imax(x1, xp[k].x1); y1 = imax(y1, xp[k].y1);
                }
            if (x1 <= x0) continue;
            memset(e, 0, sizeof *e);
            e->grp = (uint8_t)g; e->color = C_BG;
            e->x0 = (int16_t)x0; e->x1 = (int16_t)x1; e->y0 = (int16_t)y0;
            e->y1 = (int16_t)(y0 + (y1 - y0) * s->lid / 100);
            n++;
        }
    }
    return n;
}

/* ================================================================ composing */
/* Compose row y, x0 .. x0+w-1 of a view into out[]. Returns model ns. */
static uint32_t comp_row(const xpart_t *xp, int n, int y, int x0, int w, uint16_t *out)
{
    uint32_t ns = 0;
    for (int i = 0; i < w; i++) out[i] = C_BG;
    for (int k = 0; k < n; k++) {
        const xpart_t *p = &xp[k];
        if (y < p->y0 || y >= p->y1) continue;
        int a = imax(x0, p->x0), b = imin(x0 + w, p->x1);
        if (a >= b) continue;
        if (p->a4 && p->plain) {                  /* whole-pixel part: walk the stencil row */
            int iv = y - p->py;
            a = imax(a, p->px); b = imin(b, p->px + p->w);
            if (iv < 0 || iv >= p->h || a >= b) continue;
            ns += ROW_NS + (uint32_t)(b - a) * COMP_PLAIN_NS_PX;
#ifdef FACE_TEST
            face_test_ops.comp += b - a; face_test_ops.rows += 1;
#endif
            const uint8_t *s = p->a4;
            unsigned q = (unsigned)(iv * p->w + (a - p->px));
            uint16_t c = p->color, *o = out + (a - x0);
            for (int x = a; x < b; x++, q++, o++) {
                int al = (q & 1) ? (s[q >> 1] & 0x0F) : (s[q >> 1] >> 4);
                if (al) *o = (al == 15) ? c : mix(c, *o, al);
            }
            continue;
        }
        ns += ROW_NS + (uint32_t)(b - a) * COMP_NS_PX;
#ifdef FACE_TEST
        face_test_ops.comp += b - a; face_test_ops.rows += 1;
#endif
        uint16_t *o = out + (a - x0);
        if (!p->a4) { for (int x = a; x < b; x++) *o++ = p->color; continue; }
        int32_t u = p->u0 + p->dudx * a + p->dudy * y;
        int32_t v = p->v0 + p->dvdx * a + p->dvdy * y;
        const uint8_t *s = p->a4;
        unsigned pw = p->w, ph = p->h;
        uint16_t c = p->color;
        for (int x = a; x < b; x++, o++, u += p->dudx, v += p->dvdx) {
            unsigned iu = (unsigned)(u >> 16), iv = (unsigned)(v >> 16);
            if (iu >= pw || iv >= ph) continue;
            unsigned q = iv * pw + iu;
            int al = (q & 1) ? (s[q >> 1] & 0x0F) : (s[q >> 1] >> 4);
            if (al) *o = (al == 15) ? c : mix(c, *o, al);
        }
    }
    spent_ns += ns;
    return ns;
}

/* Sample w pixels of part p at row y from x into an A4 row (w even). */
static void sample_a4(const xpart_t *p, int x, int y, int w, uint8_t *row)
{
    int32_t u = p->u0 + p->dudx * x + p->dudy * y;
    int32_t v = p->v0 + p->dvdx * x + p->dvdy * y;
    unsigned pw = p->w, ph = p->h;
    memset(row, 0, (size_t)(w / 2));
    for (int i = 0; i < w; i++, u += p->dudx, v += p->dvdx) {
        unsigned iu = (unsigned)(u >> 16), iv = (unsigned)(v >> 16);
        if (iu >= pw || iv >= ph) continue;
        unsigned q = iv * pw + iu;
        int al = (q & 1) ? (p->a4[q >> 1] & 0x0F) : (p->a4[q >> 1] >> 4);
        row[i >> 1] |= (uint8_t)((i & 1) ? al : al << 4);
    }
}

/* Model cost of drawing box b of a view. */
static uint32_t box_cost(const xpart_t *xp, int n, int bx, int by, int bw, int bh)
{
    uint32_t px = (uint32_t)(bw * bh), ns = PUSH_NS + px * (SPI_NS_PX + BOX_NS_PX);
    for (int k = 0; k < n; k++) {
        const xpart_t *p = &xp[k];
        int w = imin(bx + bw, p->x1) - imax(bx, p->x0), h = imin(by + bh, p->y1) - imax(by, p->y0);
        if (w > 0 && h > 0) ns += (uint32_t)(w * h) * (p->plain ? BLEND_NS_PX : BLEND_NS_PX + COMP_NS_PX);
    }
    return ns;
}

/* Draw box b of a view through face_gfx.h, in one push. Returns model ns. */
static uint32_t draw_box(const xpart_t *xp, int n, int bx, int by, int bw, int bh)
{
    uint8_t row[ROWBUF / 2];

    gfx_begin(bx, by, bw, bh);
    gfx_fill_rect(bx, by, bw, bh, C_BG);
    for (int k = 0; k < n; k++) {
        const xpart_t *p = &xp[k];
        int x0 = imax(bx, p->x0), x1 = imin(bx + bw, p->x1);
        int y0 = imax(by, p->y0), y1 = imin(by + bh, p->y1);
        if (x0 >= x1 || y0 >= y1) continue;
        if (!p->a4)          { gfx_fill_rect(x0, y0, x1 - x0, y1 - y0, p->color); continue; }
        if (p->plain)        { gfx_blend_sprite(p->a4, p->w, p->h, p->px, p->py, p->color); continue; }
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x += ROWBUF) {
                int w = imin(ROWBUF, x1 - x), we = (w + 1) & ~1;   /* even; extra px is still sampled right */
                sample_a4(p, x, y, we, row);
                gfx_blend_sprite(row, we, 1, x, y, p->color);
            }
    }
    gfx_end();
#ifdef FACE_TEST
    face_test_ops.box_px += bw * bh;
    for (int k = 0; k < n; k++) {
        const xpart_t *p = &xp[k];
        int w = imin(bx + bw, p->x1) - imax(bx, p->x0), h = imin(by + bh, p->y1) - imax(by, p->y0);
        if (w > 0 && h > 0) { if (p->plain || !p->a4) face_test_ops.blend_plain += w * h; else face_test_ops.blend_sampled += w * h; }
    }
#endif
    uint32_t c = box_cost(xp, n, bx, by, bw, bh);
    spent_ns += c;
    return c;
}

/* Draw a whole area of a view, in strips that fit the gfx box. */
static uint32_t draw_area(const xpart_t *xp, int n, const rect_t *c)
{
    uint32_t ns = 0;
    int rows = GFX_BOX_MAX_PX / c->w;
    for (int y = c->y; y < c->y + c->h; y += rows)
        ns += draw_box(xp, n, c->x, y, c->w, imin(rows, c->y + c->h - y));
    return ns;
}

/* ================================================================ changed pixels */
typedef struct { int16_t x0, x1, y0, y1; } box_t;

typedef struct {
    const xpart_t *xb; int nb;   /* the view being drawn */
    box_t    ob[MAX_RUNS]; int nob;   /* boxes still growing down the rows */
} sender_t;

static void flush_box(sender_t *s, int j)
{
    box_t *b = &s->ob[j];
    int bw = b->x1 - b->x0, bh = b->y1 - b->y0;
    draw_box(s->xb, s->nb, b->x0, b->y0, bw, bh);
    s->ob[j] = s->ob[--s->nob];
}

static void flush_all(sender_t *s) { while (s->nob) flush_box(s, s->nob - 1); }

static uint32_t pending_ns(const sender_t *s)
{
    uint32_t ns = 0;
    for (int j = 0; j < s->nob; j++)
        ns += box_cost(s->xb, s->nb, s->ob[j].x0, s->ob[j].y0,
                       s->ob[j].x1 - s->ob[j].x0, s->ob[j].y1 - s->ob[j].y0);
    return ns;
}

/* Add row y's changed runs: grow a box from the row above or start one. */
static void box_row(sender_t *s, int y, const int16_t *sp, int n)
{
    int taken[MAX_RUNS] = { 0 };
    box_t add[MAX_RUNS]; int nadd = 0;

    for (int k = 0; k < n; k++) {
        int x0 = sp[2 * k], x1 = sp[2 * k + 1], best = -1, best_waste = PUSH_PX + 1;
        for (int j = 0; j < s->nob; j++) {        /* grow the box that wastes least ... */
            box_t *b = &s->ob[j];
            if (taken[j] || b->y1 != y) continue;
            int ux0 = imin(b->x0, x0), ux1 = imax(b->x1, x1), bw = b->x1 - b->x0, bh = b->y1 - b->y0;
            if ((ux1 - ux0) * (bh + 1) > BOX_FLUSH_PX) continue;
            int waste = (ux1 - ux0 - bw) * bh + (ux1 - ux0) - (x1 - x0);   /* widening hits every row */
            if (waste < best_waste) { best_waste = waste; best = j; }
        }                                          /* ... if that is cheaper than a new push */
        if (best >= 0) {
            box_t *b = &s->ob[best];
            b->x0 = (int16_t)imin(b->x0, x0); b->x1 = (int16_t)imax(b->x1, x1); b->y1 = (int16_t)(y + 1);
            taken[best] = 1;
        } else {
            add[nadd++] = (box_t){ (int16_t)x0, (int16_t)x1, (int16_t)y, (int16_t)(y + 1) };
        }
    }
    for (int j = s->nob - 1; j >= 0; j--)        /* boxes that did not continue */
        if (!taken[j]) {
            flush_box(s, j);
            if (j < s->nob) taken[j] = taken[s->nob];   /* flush moved the last box to j */
        }
    for (int k = 0; k < nadd; k++) s->ob[s->nob++] = add[k];
}

/* Runs of row y (x0..x1) where views a and b differ: at most MAX_RUNS,
 * close ones joined; when there are too many, the closest pair is joined. */
static int row_spans(const xpart_t *xa, int na, const xpart_t *xb, int nb,
                     int y, int x0, int x1, int16_t *sp, uint32_t *ns)
{
    uint16_t ba[ROWBUF], bb[ROWBUF];
    int n = 0;
    for (int cx = x0; cx < x1; cx += ROWBUF) {
        int w = imin(ROWBUF, x1 - cx);
        *ns += comp_row(xa, na, y, cx, w, ba);
        *ns += comp_row(xb, nb, y, cx, w, bb);
        for (int i = 0; i < w; i++) {
            if (ba[i] == bb[i]) continue;
            int x = cx + i;
            if (n && x - sp[2 * n - 1] <= GAP_PX) { sp[2 * n - 1] = (int16_t)(x + 1); continue; }
            if (n == MAX_RUNS) {                              /* join the closest pair */
                int best = n - 1, gap = x - sp[2 * n - 1];
                for (int k = 0; k + 1 < n; k++)
                    if (sp[2 * k + 2] - sp[2 * k + 1] < gap) { gap = sp[2 * k + 2] - sp[2 * k + 1]; best = k; }
                if (best == n - 1) { sp[2 * n - 1] = (int16_t)(x + 1); continue; }
                sp[2 * best + 1] = sp[2 * best + 3];
                for (int k = best + 1; k + 1 < n; k++) { sp[2 * k] = sp[2 * k + 2]; sp[2 * k + 1] = sp[2 * k + 3]; }
                n--;
            }
            sp[2 * n] = (int16_t)x; sp[2 * n + 1] = (int16_t)(x + 1); n++;
        }
    }
    return n;
}

/* Boxes a group covers in views a and b, merged. */
static int group_areas(const xpart_t *xa, int na, const xpart_t *xb, int nb, int g, rect_t *r)
{
    int n = 0;
    for (int k = 0; k < na; k++) if (xa[k].grp == g && xa[k].x1 > xa[k].x0 && xa[k].y1 > xa[k].y0) r[n++] = xp_rect(&xa[k]);
    for (int k = 0; k < nb; k++) if (xb[k].grp == g && xb[k].x1 > xb[k].x0 && xb[k].y1 > xb[k].y0) r[n++] = xp_rect(&xb[k]);
    return merge_areas(r, n);
}

static int gst_same(const gst_t *a, const gst_t *b) { return memcmp(a, b, sizeof *a) == 0; }

/* ================================================================ z Z (29) */
static void z_rect(const ztext_t *z, rect_t *r)
{
    *r = (rect_t){ z->x, z->y, (int16_t)(6 * z->s), (int16_t)(8 * z->s) };
}

/* Draw the new z, then clear what is left of the old one: no flicker. */
static uint32_t z_update(void)
{
    if (memcmp(&z_want, &z_drawn, sizeof z_want) == 0) return 0;
    uint32_t ns = 0;
    rect_t n, o;
    if (z_want.s) {
        z_rect(&z_want, &n);
        gfx_text(n.x, n.y, "z", C_INK, C_BG, z_want.s);
        ns += PUSH_NS + (uint32_t)(n.w * n.h) * (SPI_NS_PX + BOX_NS_PX);
    }
    if (z_drawn.s) {
        z_rect(&z_drawn, &o);
        if (!z_want.s) n = (rect_t){ 0, 0, 0, 0 };
        int ox1 = o.x + o.w, oy1 = o.y + o.h, nx1 = n.x + n.w, ny1 = n.y + n.h;
        rect_t cut[4] = {                           /* o minus n */
            { o.x, o.y, o.w, (int16_t)(imin(oy1, imax(o.y, n.y)) - o.y) },
            { o.x, (int16_t)imax(o.y, imin(oy1, ny1)), o.w, (int16_t)(oy1 - imax(o.y, imin(oy1, ny1))) },
            { o.x, (int16_t)imax(o.y, n.y), (int16_t)(imin(ox1, imax(o.x, n.x)) - o.x), (int16_t)(imin(oy1, ny1) - imax(o.y, n.y)) },
            { (int16_t)imax(o.x, imin(ox1, nx1)), (int16_t)imax(o.y, n.y), (int16_t)(ox1 - imax(o.x, imin(ox1, nx1))), (int16_t)(imin(oy1, ny1) - imax(o.y, n.y)) },
        };
        if (n.w == 0) { cut[0] = o; cut[1].h = cut[2].h = cut[3].h = 0; }
        for (int i = 0; i < 4; i++)
            if (cut[i].w > 0 && cut[i].h > 0) {
                gfx_fill_rect(cut[i].x, cut[i].y, cut[i].w, cut[i].h, C_BG);
                ns += PUSH_NS + (uint32_t)(cut[i].w * cut[i].h) * SPI_NS_PX;
            }
    }
    z_drawn = z_want;
    spent_ns += ns;
    return ns;
}

/* ================================================================ render */
/* Time spent in this face_tick() so far, in us: the real clock when one was
 * given (face_set_clock, DWT on the board), and never less than the model. */
static uint32_t elapsed_us(void)
{
    uint32_t model = spent_ns / 1000u;
    if (!clk) return model;
    uint32_t real = (clk() - tick_c0) / clk_per_us;
    return real > model ? real : model;
}

/* The job: bring the screen from `cur` to `nxt` over areas sw[], row by row.
 * Stops between rows when the tick would run past LIMIT_US and carries on next
 * tick; rows above sw_y of area sw_k already show `nxt`. Returns 1 when done. */
static int run_job(void)
{
    xpart_t xa[XP_MAX], xb[XP_MAX];
    int na = build(&cur, xa), nb = build(&nxt, xb);
    int16_t sp[2 * MAX_RUNS];

    while (sw_k < sw_n) {
        const rect_t *a = &sw[sw_k];
        sender_t s = { xb, nb, { { 0, 0, 0, 0 } }, 0 };
        uint32_t row_ns = (uint32_t)a->w * (SPI_NS_PX + BOX_NS_PX + 4 * COMP_NS_PX + BLEND_NS_PX)
                        + MAX_RUNS * PUSH_NS;                      /* worst case for one row */
        for (; sw_y < a->y + a->h; sw_y++) {
            if (rows_this_tick &&
                elapsed_us() + SAFETY * (pending_ns(&s) + row_ns) / 1000u > LIMIT_US) {
                flush_all(&s);
                return 0;
            }
            uint32_t c = 0;
            int n = row_spans(xa, na, xb, nb, sw_y, a->x, a->x + a->w, sp, &c);
            box_row(&s, sw_y, sp, n);
            rows_this_tick++;
        }
        flush_all(&s);
        if (++sw_k < sw_n) sw_y = sw[sw_k].y;
    }
    cur = nxt;
    job = J_NONE;
    return 1;
}

/* Set up a job that moves group g from where it is to where tgt wants it. */
static void job_group(int g)
{
    xpart_t xa[XP_MAX], xb[XP_MAX];
    rect_t  r[2 * XP_MAX];
    nxt = cur;
    nxt.g[g] = tgt.g[g];
    int na = build(&cur, xa), nb = build(&nxt, xb);
    int n = group_areas(xa, na, xb, nb, g, r);
    sw_n = (uint8_t)imin(n, SWAP_MAX);
    memcpy(sw, r, sizeof(rect_t) * sw_n);
    sw_k = 0;
    sw_y = sw_n ? sw[0].y : 0;
    job = J_GROUP; job_g = (uint8_t)g;
}

/* Bring the screen from `cur` towards `tgt`, group by group, within the tick. */
static void render_groups(void)
{
    int drew = 0, order[NG], n = 0;

    if (job != J_NONE) {                          /* a group left half-drawn last tick */
        if (!run_job()) return;
        est_us[job_g] = LIMIT_US;                 /* heavy: next time start it first */
        drew = 1;
    }
    for (int i = 0; i < NG; i++) {                /* longest-waiting first, ties take turns */
        int g = (i + (int)tick_n) % NG;
        if (!gst_same(&cur.g[g], &tgt.g[g])) order[n++] = g;
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (waiting[order[j]] > waiting[order[i]]) { int t = order[i]; order[i] = order[j]; order[j] = t; }

    for (int i = 0; i < n; i++) {
        int g = order[i];
        if (gst_same(&cur.g[g], &tgt.g[g])) continue;
        uint32_t e = elapsed_us();
        if (e >= SOFT_US || (drew && e + est_us[g] > LIMIT_US)) {
            if (waiting[g] < 255) waiting[g]++;   /* next tick, first */
            continue;
        }
        job_group(g);
        if (!run_job()) { waiting[g] = 0; return; }   /* out of time mid-group */
        uint32_t took = elapsed_us() - e;
        est_us[g] = (uint16_t)imin((int)took, 65535);
        waiting[g] = 0;
        drew = 1;
    }
}

static void redraw_all(void)
{
    xpart_t xp[XP_MAX];
    rect_t  r[XP_MAX];
    int n = build(&cur, xp), nr = 0;

    gfx_fill_rect(0, 0, GFX_W, GFX_H, C_BG);    /* whole screen: only on init/activate */
    for (int k = 0; k < n; k++)
        if (xp[k].x1 > xp[k].x0 && xp[k].y1 > xp[k].y0) r[nr++] = xp_rect(&xp[k]);
    nr = merge_areas(r, nr);
    for (int k = 0; k < nr; k++) draw_area(xp, n, &r[k]);
    memset(&z_drawn, 0, sizeof z_drawn);
    z_update();
}

/* ================================================================ life: blink */
static int face_blinks(face_id_t f) { return FACES[f].blink != BL_NONE; }

static uint32_t step_ms(const bstep_t *st, face_id_t f)
{
    return (uint32_t)st->ms * FACES[f].lid_pct / 100;
}

static void blink_schedule(uint32_t now)
{
    const face_cfg_t *fc = &FACES[cur_face];
    if (!bl_double && rnd(0, 99) < DOUBLE_PCT) {
        bl_double = 1;
        bl_next = now + (uint32_t)rnd(DOUBLE_GAP_MIN, DOUBLE_GAP_MAX);
    } else {
        bl_double = 0;
        bl_next = now + (uint32_t)rnd(BLINK_MIN_MS, BLINK_MAX_MS) * fc->blink_gap / 100;
    }
}

static void blink_start(uint32_t now, uint16_t hold)
{
    int n, h;
    if (bl_i >= 0 || !face_blinks(cur_face) || trans != T_NONE) return;
    const bstep_t *seq = seq_of(FACES[cur_face].blink, &n, &h);
    bl_i = 0;
    bl_hold = hold ? hold : (uint16_t)rnd(BLINK_HOLD_MIN, BLINK_HOLD_MAX);
    bl_end = now + step_ms(&seq[0], cur_face);
}

static void blink_update(uint32_t now, acc_t *a)
{
    int n, h;
    const bstep_t *seq = seq_of(FACES[cur_face].blink, &n, &h);
    if (!face_blinks(cur_face)) return;
    if (bl_i < 0) {
        if ((int32_t)(now - bl_next) >= 0) blink_start(now, 0);
        if (bl_i < 0) return;
    }
    while ((int32_t)(now - bl_end) >= 0) {
        if (++bl_i >= n) { bl_i = -1; blink_schedule(now); return; }
        bl_end = now + (bl_i == h ? bl_hold : step_ms(&seq[bl_i], cur_face));
    }
    for (int g = G_EYE_L; g <= G_EYE_R; g++) {
        a[g].lid = seq[bl_i].lid;
        a[g].var |= seq[bl_i].var;
        a[g].bsy = seq[bl_i].sy / 100.0f;
    }
}

/* ================================================================ life: gaze */
static void saccade_to(float x, float y, uint32_t now, float *gx, float *gy)
{
    gz_fx = *gx; gz_fy = *gy;
    gz_tx = x; gz_ty = y;
    float dx = x - gz_fx, dy = y - gz_fy, d2 = dx * dx + dy * dy;
    gz_ox = gz_oy = 0;
    if (d2 > 2.25f) {                           /* overshoot only on real jumps */
        float k = (float)rnd(SACC_OVER_MIN, SACC_OVER_MAX);
        float inv = 1.0f / (dx * dx > dy * dy ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy));
        gz_ox = dx * inv * k * 0.7f; gz_oy = dy * inv * k * 0.7f;
    }
    gz_t0 = now;
    gz_settle = (uint16_t)rnd(SACC_SETTLE_MIN, SACC_SETTLE_MAX);
}

/* Where the eyes are on the saccade curve now; *jump = still in the fast part. */
static void gaze_pos(uint32_t now, float *x, float *y, int *jump)
{
    uint32_t t = now - gz_t0;
    float ex = gz_tx + gz_ox, ey = gz_ty + gz_oy;
    *jump = 0;
    if (t < SACC_JUMP_MS) {
        float k = ease((float)t / SACC_JUMP_MS);
        *x = lerp(gz_fx, ex, k); *y = lerp(gz_fy, ey, k);
        *jump = (gz_ox != 0 || gz_oy != 0);
    } else if (t < SACC_JUMP_MS + gz_settle) {
        float k = ease((float)(t - SACC_JUMP_MS) / gz_settle);
        *x = lerp(ex, gz_tx, k); *y = lerp(ey, gz_ty, k);
    } else {
        *x = gz_tx; *y = gz_ty;
    }
}

static uint32_t hold_ms(int lo, int hi)
{
    return (uint32_t)rnd(lo, hi) * FACES[cur_face].gaze_hold / 100;
}

static void gaze_reset(uint32_t now)
{
    gz_st = GS_VIEW; gz_locked = 0; gz_side = 1;
    gz_fx = gz_fy = gz_tx = gz_ty = gz_ox = gz_oy = 0;
    gz_t0 = now - 1000; gz_settle = SACC_SETTLE_MIN;
    gz_until = now + hold_ms(VIEW_HOLD_MIN / 2, VIEW_HOLD_MAX / 2);
    gz_widen_t0 = now - 10000; gz_slow_blink = 0;
    dr_x = dr_y = dr_tx = dr_ty = 0;
    dr_next = now + (uint32_t)rnd(DRIFT_MIN_MS, DRIFT_MAX_MS);
    float dy = FACES[cur_face].gaze_dy;
    if (dy != 0) { float x = 0, y = 0; saccade_to(0, dy, now, &x, &y); }
}

static void gaze_update(uint32_t now, acc_t *a)
{
    const face_cfg_t *fc = &FACES[cur_face];
    float range = fc->gaze_range / 100.0f, vy = fc->gaze_dy;
    float gx, gy; int jump;
    gaze_pos(now, &gx, &gy, &jump);

    if (!gz_locked && (int32_t)(now - gz_until) >= 0) {
        if (fc->gaze == GZ_SHY) {
            if (gz_st == GS_GLANCE) {                    /* peek back at the viewer */
                gz_st = GS_VIEW; saccade_to(0, vy, now, &gx, &gy);
                gz_until = now + SHY_PEEK_MS;
            } else {                                     /* look away, down, to one side */
                if (rnd(0, 99) < 30) gz_side = (int8_t)-gz_side;
                gz_st = GS_GLANCE;
                saccade_to(gz_side * SHY_X * range, (SHY_Y + vy) * range, now, &gx, &gy);
                gz_until = now + hold_ms(SHY_AWAY_MIN, SHY_AWAY_MAX);
            }
        } else if (fc->gaze == GZ_DART) {
            float x = 0, y = vy;
            if (rnd(0, 99) >= 30) {
                x = rnd(-GLANCE_PX_MAX, GLANCE_PX_MAX) * range;
                y = vy + rnd(-LOCK_PX_Y, LOCK_PX_Y) * range;
            }
            saccade_to(x, y, now, &gx, &gy);
            gz_widen_t0 = now;
            gz_until = now + hold_ms(DART_MIN_MS, DART_MAX_MS);
        } else if (gz_st == GS_VIEW) {                   /* glance */
            int s = rnd(0, 1) ? 1 : -1;
            gz_st = GS_GLANCE;
            saccade_to(s * rnd(GLANCE_PX_MIN, GLANCE_PX_MAX) * range,
                       vy + rnd(-GLANCE_PX_Y, GLANCE_PX_Y) * range, now, &gx, &gy);
            gz_until = now + hold_ms(GLANCE_HOLD_MIN, GLANCE_HOLD_MAX);
        } else if (gz_st == GS_GLANCE) {                 /* small adjust */
            int dx = rnd(1, ADJUST_PX) * (rnd(0, 1) ? 1 : -1), dy = rnd(-1, 1);
            gz_st = GS_ADJUST;
            saccade_to(clampf(gz_tx + dx * range, -GLANCE_PX_MAX * range, GLANCE_PX_MAX * range),
                       gz_ty + dy * range, now, &gx, &gy);
            gz_until = now + hold_ms(GLANCE_HOLD_MIN, GLANCE_HOLD_MAX);
        } else {                                         /* back to the viewer */
            gz_st = GS_VIEW;
            saccade_to(0, vy, now, &gx, &gy);
            gz_until = now + hold_ms(VIEW_HOLD_MIN, VIEW_HOLD_MAX);
            if (rnd(0, 99) < SLOW_BLINK_PCT) gz_slow_blink = now + SACC_JUMP_MS + gz_settle;
        }
        gaze_pos(now, &gx, &gy, &jump);
    }
    if (gz_slow_blink && (int32_t)(now - gz_slow_blink) >= 0) {
        gz_slow_blink = 0;
        blink_start(now, SLOW_BLINK_MS);
    }

    /* slow drift on top */
    if ((int32_t)(now - dr_next) >= 0) {
        dr_tx = (float)rnd(-DRIFT_PX_X, DRIFT_PX_X);
        dr_ty = (float)rnd(-DRIFT_PX_Y, DRIFT_PX_Y);
        dr_next = now + (uint32_t)rnd(DRIFT_MIN_MS, DRIFT_MAX_MS);
    }
    dr_x += (dr_tx - dr_x) * DRIFT_K;
    dr_y += (dr_ty - dr_y) * DRIFT_K;
    gx += dr_x * range; gy += dr_y * range;

    for (int g = 0; g < NG; g++) { a[g].x += gx * PARALLAX[g]; a[g].y += gy * PARALLAX[g]; }
    for (int g = G_EYE_L; g <= G_EYE_R; g++) {
        if (jump) a[g].sy *= SACC_SQUASH;
        uint32_t wt = now - gz_widen_t0;
        if (wt < DART_WIDEN_MS) {
            float k = 1 + (DART_WIDEN - 1) * (1 - ease((float)wt / DART_WIDEN_MS));
            a[g].sx *= k; a[g].sy *= k;
        }
    }
}

/* ================================================================ life: motions */
static void add_xy(acc_t *a, int mask, float x, float y)
{
    for (int g = 0; g < NG; g++) if (mask & (1 << g)) { a[g].x += x; a[g].y += y; }
}

static void mul_s(acc_t *a, int mask, float sx, float sy)
{
    for (int g = 0; g < NG; g++) if (mask & (1 << g)) { a[g].sx *= sx; a[g].sy *= sy; }
}

static uint32_t run_len(const motion_t *m, const mstate_t *s)
{
    switch (m->type) {
    case MO_TILT:    return m->dur + 400u;
    case MO_STRETCH: return m->dur + 240u;
    case MO_BOUNCE:  return (2u * m->dur + (uint32_t)m->c) * (s->var ? 2u : 1u);
    case MO_SIGH:    return (uint32_t)m->b + m->dur + (uint32_t)m->c;
    case MO_HUFF:    return m->dur + 400u;
    case MO_POP:     return 3u * m->dur;
    case MO_NOD:     return m->dur + (uint32_t)m->b;
    default:         return m->dur;
    }
}

static void motion_eval(const motion_t *m, const mstate_t *s, int on, uint32_t t, uint32_t tf, acc_t *a)
{
    float A = m->a / 10.0f, per = (float)m->dur;
    switch (m->type) {
    case MO_TILT:
        if (on) {
            float f = A * env(t, run_len(m, s), 200);
            add_xy(a, M_EYE_L | (1 << G_BROW_L), 0, -f);
            add_xy(a, M_EYE_R | (1 << G_BROW_R), 0, f);
        }
        break;
    case MO_STRETCH:
        if (on) mul_s(a, M_MOUTH, 1 + (m->a / 100.0f - 1) * env(t, run_len(m, s), 120), 1);
        break;
    case MO_BOUNCE:
        if (on) {
            uint32_t hop = 2u * m->dur + (uint32_t)m->c, h = t % hop;
            if (h < m->dur)          add_xy(a, M_ALL, 0, -A * (1 - (1 - (float)h / per) * (1 - (float)h / per)));
            else if (h < 2u * m->dur) { float k = (float)(h - m->dur) / per; add_xy(a, M_ALL, 0, -A * (1 - k * k)); }
            else                     mul_s(a, M_ALL, 1, m->b / 100.0f);
        }
        break;
    case MO_SQUINT:
        if (on) { a[G_EYE_L].var = 1; a[G_EYE_R].var = 1; }
        break;
    case MO_SIGH:
        if (on) {
            float f;
            if (t < (uint32_t)m->b) f = ease((float)t / m->b);
            else if (t < (uint32_t)m->b + m->dur) f = 1;
            else f = 1 - ease((float)(t - m->b - m->dur) / m->c);
            add_xy(a, M_ALL, 0, A * f);
        }
        break;
    case MO_SWAY: {
        float lag = (float)m->b;
        add_xy(a, M_MOUTH | M_EXTRA, A * fsin_turn(tf / per), 0);
        add_xy(a, M_EYES | M_BROWS, A * fsin_turn((tf - lag) / per), 0);
        break;
    }
    case MO_PULSE:
        a[G_EXTRA].tint += -m->a * (0.5f + 0.5f * fsin_turn(tf / per));
        break;
    case MO_TREMBLE: {
        float amp = on ? m->b / 10.0f : A;
        uint32_t step = on ? 500u / (uint32_t)m->c : 1000u / (uint32_t)m->c;
        uint32_t h = hash32((tf / step) ^ (on ? 0x9E3779B9u : 0) ^ face_salt);
        add_xy(a, on ? M_ALL : m->d, amp * (float)((int)(h % 3) - 1), amp * (float)((int)((h >> 8) % 3) - 1));
        break;
    }
    case MO_HUFF:
        if (on) {
            if (t < m->dur) add_xy(a, M_ALL, A * fsin_turn(3.0f * t / per), 0);
            float f = env(t, run_len(m, s), 80);
            add_xy(a, M_BROWS, 0, m->b / 10.0f * f);
            mul_s(a, M_MOUTH, 1 - (1 - m->c / 100.0f) * f, 1);
        }
        break;
    case MO_POP:
        if (on) {
            float k = (float)(t % m->dur) / per, s1 = m->a / 100.0f, s2 = m->b / 100.0f, sc;
            if (t < m->dur)           sc = lerp(1, s1, ease(k));
            else if (t < 2u * m->dur) sc = lerp(s1, s2, ease(k));
            else                      sc = lerp(s2, 1, ease(k));
            mul_s(a, M_EYES, sc, sc);
        }
        break;
    case MO_SHIMMER: {
        static const int8_t P[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        int k = (int)((tf / m->dur) & 3);
        for (int g = G_EYE_L; g <= G_EYE_R; g++) { a[g].hx += P[k][0] * A; a[g].hy += P[k][1] * A; }
        break;
    }
    case MO_WIGGLE:
        if (on) add_xy(a, M_ALL, A * fsin_turn((float)m->b * t / per), 0);
        break;
    case MO_SQUEEZE:
        if (on) mul_s(a, (s->n & 1) ? M_EYE_R : M_EYE_L, 1 - (1 - m->a / 100.0f) * env(t, m->dur, 50), 1);
        break;
    case MO_BOB:
        add_xy(a, M_MOUTH, A * fsin_turn(tf / per), 0);
        break;
    case MO_HEARTBEAT: {
        uint32_t h = tf % m->dur;
        float s1 = m->a / 100.0f, s2 = m->b / 100.0f, sc = 1;
        if (h < 150)      sc = lerp(1, s1, ease(h / 150.0f));
        else if (h < 300) sc = lerp(s1, 1, ease((h - 150) / 150.0f));
        else if (h < 450) sc = lerp(1, s2, ease((h - 300) / 150.0f));
        else if (h < 600) sc = lerp(s2, 1, ease((h - 450) / 150.0f));
        sc = 1 + iround((sc - 1) / HEART_STEP) * HEART_STEP;
        mul_s(a, M_EYES, sc, sc);
        if (h < 600 && m->c) for (int g = G_EYE_L; g <= G_EYE_R; g++) a[g].tint += m->c;
        if (h < 120 || (h >= 300 && h < 420)) add_xy(a, M_EXTRA, 0, -m->d / 10.0f);
        break;
    }
    case MO_SPIN: {
        float turn = (float)(ifloor(tf / per * m->c)) / m->c;
        a[G_EYE_L].ang += turn;
        a[G_EYE_R].ang += turn * (m->a < 0 ? -1 : 1);
        break;
    }
    case MO_CIRCLE:
        add_xy(a, M_ALL, A * fcos_turn(tf / per), A * fsin_turn(tf / per));
        break;
    case MO_WOBBLE:
        add_xy(a, M_ALL, A * fsin_turn(tf / per), 0);
        break;
    case MO_TWITCH:
        if (on) add_xy(a, s->var ? M_EYE_R : M_EYE_L, 0, -A);
        break;
    case MO_NOD:
        if (on) {
            float f = t < m->dur ? (float)t / per : 1 - ease((float)(t - m->dur) / m->b);
            add_xy(a, M_ALL, 0, A * f);             /* sinks steadily, snaps up */
        }
        break;
    case MO_PUFF:
        mul_s(a, M_MOUTH, 1 + (m->a / 100.0f - 1) * (0.5f + 0.5f * fcos_turn(tf / per)), 1);
        break;
    case MO_ZZZ:
        if (on) {
            float k = (float)t / per;
            int big = k >= 0.5f;
            z_want.s = (uint8_t)(big ? 3 : 2);
            z_want.x = (int16_t)(m->a - (big ? 2 : 0));
            z_want.y = (int16_t)(m->b - iround(m->c * k) - (big ? 8 : 0));
        }
        break;
    default:
        break;
    }
}

static void motions_update(uint32_t now, uint32_t tf, acc_t *a)
{
    const face_cfg_t *fc = &FACES[cur_face];
    for (int i = 0; i < 3; i++) {
        const motion_t *m = &fc->m[i];
        mstate_t *s = &ms[i];
        uint32_t t = 0; int on = 0;
        if (m->type == MO_NONE) continue;
        if (m->gap_max) {
            if (!s->on && (int32_t)(now - s->next) >= 0) {
                s->on = 1; s->start = now; s->n++;
                s->var = (uint8_t)(rnd(0, 99) < m->pct);
            }
            if (s->on) {
                t = now - s->start;
                if (t >= run_len(m, s)) { s->on = 0; s->next = now + (uint32_t)rnd(m->gap_min, m->gap_max); }
                else on = 1;
            }
        }
        motion_eval(m, s, on, t, tf, a);
    }
}

/* ================================================================ life: per tick */
static int snap(float t, int cur_px)           /* whole pixel, 0.3 px hysteresis */
{
    float d = t - (float)cur_px;
    if (d < 0.5f + HYST_PX && d > -(0.5f + HYST_PX)) return cur_px;
    return iround(t);
}

static uint16_t q8_scale(float s) { return (uint16_t)(iround(s * 64.0f) * 4); }

static void acc_rest(acc_t *a)
{
    for (int g = 0; g < NG; g++) {
        memset(&a[g], 0, sizeof a[g]);
        a[g].sx = a[g].sy = a[g].bsy = 1;
    }
}

static void acc_to_view(const acc_t *a, view_t *v)
{
    v->face = (uint8_t)cur_face;
    for (int g = 0; g < NG; g++) {
        const gst_t *c = &cur.g[g];
        gst_t *o = &v->g[g];
        o->x = (int16_t)snap(a[g].x, c->x);
        o->y = (int16_t)snap(a[g].y, c->y);
        o->sx = q8_scale(clampf(a[g].sx, SCALE_MIN, SCALE_MAX));
        o->sy = q8_scale(clampf(a[g].sy, SCALE_MIN, SCALE_MAX) * a[g].bsy);
        o->ang = (int16_t)(iround(a[g].ang * 1024.0f) & 1023);
        o->tint = (int8_t)iround(clampf(a[g].tint, -16, 16));
        o->var = a[g].var;
        o->lid = a[g].lid;
        o->hx = (int8_t)iround(a[g].hx);
        o->hy = (int8_t)iround(a[g].hy);
    }
}

static void rest_view(view_t *v, face_id_t f)
{
    memset(v, 0, sizeof *v);
    v->face = (uint8_t)f;
    for (int g = 0; g < NG; g++) v->g[g].sx = v->g[g].sy = 256;
}

static void life_start(uint32_t now)           /* the face is open: start living */
{
    face_t0 = now;
    face_salt = xorshift();
    memset(ms, 0, sizeof ms);
    for (int i = 0; i < 3; i++) {
        const motion_t *m = &FACES[cur_face].m[i];
        if (m->gap_max) ms[i].next = now + (uint32_t)rnd(800, imax(800, m->gap_min));
    }
    bl_i = -1; bl_double = 0;
    bl_next = now + (uint32_t)rnd(BLINK_MIN_MS / 2, BLINK_MAX_MS / 2);
    life_last = now - FACES[cur_face].step_ms;
    gaze_reset(now);
}

static void life_update(uint32_t now)
{
    acc_t a[NG];
    const face_cfg_t *fc = &FACES[cur_face];
    uint32_t tf = now - face_t0;

    if (fc->step_ms && now - life_last < fc->step_ms) return;   /* stop-motion face: hold */
    life_last = now;
    acc_rest(a);
    memset(&z_want, 0, sizeof z_want);
    add_xy(a, M_ALL, 0, fc->breath_px / 10.0f * fsin_turn((float)tf / fc->breath_ms));
    gaze_update(now, a);
    blink_update(now, a);
    motions_update(now, tf, a);
    acc_to_view(a, &tgt);
}

/* ================================================================ face change */
static void eyes_step(view_t *v, const bstep_t *st)
{
    for (int g = G_EYE_L; g <= G_EYE_R; g++) {
        v->g[g].lid = st->lid;
        v->g[g].var = st->var;
        v->g[g].sx = 256;
        v->g[g].sy = (uint16_t)(st->sy * 256 / 100);
        v->g[g].ang = 0;
    }
}

static const bstep_t *trans_seq(face_id_t f, int *n, int *hold)
{
    return seq_of(FACES[f].blink, n, hold);
}

static void trans_start(uint32_t now)
{
    int n, h;
    const bstep_t *seq = trans_seq(cur_face, &n, &h);
    tr_i = (bl_i >= 0 && bl_i <= h) ? bl_i : 0;   /* already closing: carry on */
    bl_i = -1;
    tr_end = now + (tr_i == h ? 0 : step_ms(&seq[tr_i], cur_face));
    trans = T_CLOSE;
}

static void swap_prepare(void)
{
    xpart_t xa[XP_MAX], xb[XP_MAX];
    rect_t  r[2 * XP_MAX];
    int n, h, k = 0;
    const bstep_t *seq = trans_seq(want_face, &n, &h);

    rest_view(&nxt, want_face);
    eyes_step(&nxt, &seq[h]);
    int na = build(&cur, xa), nb = build(&nxt, xb);
    for (int i = 0; i < na; i++) if (xa[i].x1 > xa[i].x0 && xa[i].y1 > xa[i].y0) r[k++] = xp_rect(&xa[i]);
    for (int i = 0; i < nb; i++) if (xb[i].x1 > xb[i].x0 && xb[i].y1 > xb[i].y0) r[k++] = xp_rect(&xb[i]);
    k = merge_areas(r, k);
    sw_n = (uint8_t)imin(k, SWAP_MAX);
    memcpy(sw, r, sizeof(rect_t) * sw_n);
    sw_k = 0;
    sw_y = sw_n ? sw[0].y : 0;
    job = J_SWAP;
    trans = T_SWAP;
}

static void trans_update(uint32_t now)
{
    int n, h;
    memset(&z_want, 0, sizeof z_want);           /* z Z leaves with the old face */

    if (trans == T_CLOSE) {
        const bstep_t *seq = trans_seq(cur_face, &n, &h);
        while (tr_i < h && (int32_t)(now - tr_end) >= 0) {
            tr_i++;
            tr_end = now + step_ms(&seq[tr_i], cur_face);
        }
        tgt = cur;
        eyes_step(&tgt, &seq[tr_i]);
        if (tr_i == h && job == J_NONE && gst_same(&cur.g[G_EYE_L], &tgt.g[G_EYE_L])
            && gst_same(&cur.g[G_EYE_R], &tgt.g[G_EYE_R]) && z_drawn.s == 0) {
            if (want_face == cur_face) {              /* changed its mind: just open */
                trans = T_OPEN; tr_end = now;
            } else swap_prepare();
        }
    }
    if (trans == T_SWAP) {
        tgt = cur;
        if (job == J_NONE) {                          /* new face is on screen */
            cur = nxt; tgt = cur;
            cur_face = (face_id_t)nxt.face;
            memset(waiting, 0, sizeof waiting);
            trans = T_OPEN;
            trans_seq(cur_face, &n, &h);
            tr_i = (int8_t)h;
            tr_end = now;
        }
        return;
    }
    if (trans == T_OPEN) {
        const bstep_t *seq = trans_seq(cur_face, &n, &h);
        while (tr_i < n && (int32_t)(now - tr_end) >= 0) {
            if (++tr_i < n) tr_end = now + step_ms(&seq[tr_i], cur_face);
        }
        rest_view(&tgt, cur_face);
        if (tr_i < n) eyes_step(&tgt, &seq[tr_i]);
        if (tr_i >= n && gst_same(&cur.g[G_EYE_L], &tgt.g[G_EYE_L]) && gst_same(&cur.g[G_EYE_R], &tgt.g[G_EYE_R])) {
            trans = T_NONE;
            life_start(now);
            if (want_face != cur_face) trans_start(now);   /* another change was asked meanwhile */
        }
    }
}

/* ================================================================ API */
void face_init(uint32_t seed)
{
    rng = seed ? seed : 0x1234567u;            /* xorshift must not start at 0 */
    cur_face = want_face = FACE_IDLE;
    rest_view(&cur, cur_face);
    tgt = cur;
    trans = T_NONE; job = J_NONE;
    memset(waiting, 0, sizeof waiting);
    memset(est_us, 0, sizeof est_us);
    memset(&z_want, 0, sizeof z_want);
    active = 1; started = 0; tick_n = 0;
    last_frame = 0;
    redraw_all();
    life_start(0);
}

void face_tick(uint32_t now_ms)
{
    tick_ns = 0;
    if (!active) return;                       /* menu/game owns the screen */
    if (!started) {                            /* first call: align timers to real time */
        started = 1;
        last_frame = last_call = now_ms;
        life_start(now_ms);
        return;
    }
    if (now_ms == last_call) return;           /* same millisecond: nothing can be due */
    last_call = now_ms;
    if ((int32_t)(now_ms - last_frame) < FRAME_MS) return;
    last_frame = now_ms;
    tick_n++;
    spent_ns = 0;
    rows_this_tick = 0;
    if (clk) tick_c0 = clk();

    if (trans == T_NONE) life_update(now_ms);
    else                 trans_update(now_ms);

    if (trans == T_SWAP) run_job();
    else                 render_groups();
    if (elapsed_us() < LIMIT_US) z_update();
    tick_ns = spent_ns ? spent_ns : 1;
    if (trans == T_SWAP && job == J_NONE) trans_update(now_ms);   /* finished: open next tick */
}

void face_show(face_id_t face)
{
    if ((unsigned)face >= FACE_COUNT || face == want_face) return;
    want_face = face;
    if (!active || !started) {                 /* no animation: switch now, drawn on activation */
        cur_face = face;
        rest_view(&cur, face);
        tgt = cur;
        trans = T_NONE; job = J_NONE;
        if (active) redraw_all();
        life_start(last_frame);
        return;
    }
    if (trans == T_NONE) trans_start(last_frame);
}

void face_set(face_eyes_t eyes, face_mouth_t mouth, face_overlay_t overlay)
{
    /* One part each so far: the combination is the Idle face. */
    if (eyes >= FACE_EYES_COUNT || mouth >= FACE_MOUTH_COUNT || overlay >= FACE_OVERLAY_COUNT) return;
    face_show(FACE_IDLE);
}

face_id_t face_current(void) { return want_face; }

int face_changing(void) { return trans != T_NONE || want_face != cur_face; }

uint32_t face_frames(void) { return tick_n; }

void face_set_clock(uint32_t (*cycles)(void), uint32_t cycles_per_us)
{
    clk = cycles;
    clk_per_us = cycles_per_us ? cycles_per_us : 1;
}

const char *face_name(face_id_t face)
{
    return (unsigned)face < FACE_COUNT ? FACE_NAMES[face] : "?";
}

void face_set_active(int on)
{
    if (on && !active) {
        active = 1;
        if (trans != T_NONE || cur_face != want_face) {   /* finish any change at once */
            cur_face = want_face;
            rest_view(&cur, cur_face);
            trans = T_NONE;
        }
        job = J_NONE;                          /* half-drawn work: the repaint covers it */
        tgt = cur;
        redraw_all();                          /* screen was someone else's: repaint */
        life_start(last_frame);
    } else if (!on) {
        active = 0;
    }
}

void face_blink(void)
{
    blink_start(last_frame, 0);
}

void face_set_gaze(int dx, int dy)
{
    float x, y; int jump;
    gaze_pos(last_frame, &x, &y, &jump);
    gz_locked = (dx != 0 || dy != 0);
    gz_slow_blink = 0;
    if (gz_locked) {
        saccade_to(clampf((float)dx, -LOCK_PX_X, LOCK_PX_X) * FACES[cur_face].gaze_range / 100.0f,
                   clampf((float)dy, -LOCK_PX_Y, LOCK_PX_Y) * FACES[cur_face].gaze_range / 100.0f
                       + FACES[cur_face].gaze_dy, last_frame, &x, &y);
    } else {
        gz_st = GS_VIEW;
        saccade_to(0, FACES[cur_face].gaze_dy, last_frame, &x, &y);
        gz_until = last_frame + hold_ms(VIEW_HOLD_MIN, VIEW_HOLD_MAX);
    }
}

/* ================================================================ test hooks */
#ifdef FACE_TEST
#include "gfx_font_data.h"

/* Clean draw of what should be on screen: every group at its committed
 * transform (mid face change: new face where the swap has reached). */
void face_test_render(uint16_t *scr)
{
    __typeof__(face_test_ops) keep = face_test_ops;       /* not part of the face's work */
    xpart_t xa[XP_MAX], xb[XP_MAX];
    int na = build(&cur, xa), nb = job != J_NONE ? build(&nxt, xb) : 0;
    for (int y = 0; y < GFX_H; y++) {
        comp_row(xa, na, y, 0, GFX_W, scr + y * GFX_W);
        if (job == J_NONE) continue;
        for (int k = 0; k <= sw_k && k < sw_n; k++) {
            const rect_t *a = &sw[k];
            int end = (k < sw_k) ? a->y + a->h : sw_y;
            if (y < a->y || y >= end) continue;
            uint16_t row[GFX_W];
            comp_row(xb, nb, y, a->x, a->w, row);
            memcpy(scr + y * GFX_W + a->x, row, sizeof(uint16_t) * (size_t)a->w);
        }
    }
    if (z_drawn.s) {                            /* gfx_text: 5x7 glyph in a 6x8 cell */
        const uint8_t *gl = FONT5X7['Z' - FONT_FIRST];
        int s = z_drawn.s;
        for (int y = 0; y < 8 * s; y++)
            for (int x = 0; x < 6 * s; x++) {
                int r = y / s, c = x / s, on = r < 7 && c < 5 && (gl[r] & (0x10 >> c));
                scr[(z_drawn.y + y) * GFX_W + z_drawn.x + x] = on ? C_INK : C_BG;
            }
    }
    face_test_ops = keep;
}

/* Pixels where ink of two different groups (or the z) meets, as drawn now. */
long face_test_overlap(void)
{
    static uint8_t owner[GFX_W * GFX_H];
    xpart_t xp[XP_MAX];
    int n = build(&cur, xp);
    long hits = 0;
    memset(owner, 0xFF, sizeof owner);
    for (int k = 0; k < n; k++) {
        const xpart_t *p = &xp[k];
        if (!p->a4) continue;
        for (int y = p->y0; y < p->y1; y++)
            for (int x = p->x0; x < p->x1; x++) {
                unsigned iu = (unsigned)((p->u0 + p->dudx * x + p->dudy * y) >> 16);
                unsigned iv = (unsigned)((p->v0 + p->dvdx * x + p->dvdy * y) >> 16);
                if (iu >= p->w || iv >= p->h) continue;
                unsigned q = iv * p->w + iu;
                if (!((q & 1) ? (p->a4[q >> 1] & 0x0F) : (p->a4[q >> 1] >> 4))) continue;
                uint8_t *o = &owner[y * GFX_W + x];
                if (*o != 0xFF && *o != p->grp) hits++;
                *o = p->grp;
            }
    }
    if (z_drawn.s)
        for (int y = z_drawn.y; y < z_drawn.y + 8 * z_drawn.s; y++)
            for (int x = z_drawn.x; x < z_drawn.x + 6 * z_drawn.s; x++)
                if (owner[y * GFX_W + x] != 0xFF) hits++;
    return hits;
}

uint32_t face_test_tick_ns(void) { return tick_ns; }   /* 0 = that call drew no frame */
uint32_t face_test_spent_ns(void) { return spent_ns; }
uint32_t face_test_spi_ns_px(void) { return SPI_NS_PX; }   /* timing model, one source */
uint32_t face_test_push_ns(void)   { return PUSH_NS; }   /* model time so far in this frame */

/* Snapshot of the committed state, to see that a face really moves. */
uint32_t face_test_state_hash(void)
{
    uint32_t h = 2166136261u;
    const uint8_t *p = (const uint8_t *)&cur;
    for (size_t i = 0; i < sizeof cur; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}
#endif
