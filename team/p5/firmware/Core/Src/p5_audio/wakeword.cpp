/* wakeword.cpp — P5: "hey BMO" (+ "games" / "music") on the STM32, using the Edge Impulse model.
 *
 * How it works:
 *  - The INMP441 DMA (I2S2, same settings as mic_stream) fills a buffer. In the DMA interrupt
 *    each 32 ms half is converted to 16-bit audio EXACTLY like mic_stream did for the training
 *    clips (>> 8, then >> MIC_GAIN_SHIFT) and stored in a 2 s ring buffer.
 *  - Every 0.125 s the main loop runs the model on the newest 1.5 s (takes about 90 ms).
 *    The interrupt keeps recording meanwhile, so no audio is lost.
 *  - "hey BMO" counts when 2 of the last 3 windows are above THRESHOLD, or one window is above
 *    SURE. This stops most false wake-ups from similar words ("hey Bob").
 *  - "games"/"music" need only one window above CMD_THRESHOLD, because they are only listened
 *    for in the few seconds after "hey BMO".
 *
 * Feedback for testing (no speaker needed):
 *  - Serial terminal at 115200: one line of scores per window, plus a big message on detection.
 *  - LEDs: green = "hey BMO", blue = "games", red = "music" (on for 1 s).
 *
 * Build notes:
 *  - The project must be C++ (right-click project > Convert to C++). main.c stays C.
 *  - Exclude mic_stream.c from the build: it has the same DMA callbacks as this file.
 */
#include "p5_audio/wakeword.h"
#include "main.h"
#include "i2s.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

/* ---- settings ------------------------------------------------------------------------- */
#define MIC_GAIN_SHIFT  7       /* MUST match mic_stream.c when the clips were recorded      */
#define THRESHOLD       0.70f   /* "hey BMO": score needed in 2 of the last 3 windows          */
#define SURE            0.95f   /* "hey BMO": or this score in a single window                */
#define CMD_THRESHOLD   0.60f   /* "games"/"music": score needed in one window (after wake)    */
#define CMD_WINDOW_MS   4000    /* "games"/"music" only count this long after "hey BMO"       */
#define COOLDOWN_MS     1500    /* ignore everything for this long after a detection          */
#define WW_DEBUG        1       /* 1 = print scores every window (set 0 in the team build)    */
#define WW_LEDS         1       /* 1 = flash the Nucleo LEDs on detections (0 in team build)  */

/* ---- mic DMA (same layout as mic_stream.c) -------------------------------------------- */
#define MIC_FRAMES       1024
#define MIC_HALF_FRAMES  (MIC_FRAMES / 2)
#define HW_PER_FRAME     4
static uint16_t mic_buf[MIC_FRAMES * HW_PER_FRAME] __attribute__((section(".sram1"), aligned(32)));

/* ---- ring buffer of converted audio --------------------------------------------------- */
#define RING_LEN   32768u                                   /* 2.05 s at 16 kHz, power of 2 */
#define WINDOW     EI_CLASSIFIER_RAW_SAMPLE_COUNT           /* 24000 = 1.5 s               */
#define STRIDE     (EI_CLASSIFIER_FREQUENCY / 8)            /* 2000  = 0.125 s             */
static int16_t ring[RING_LEN];
static volatile uint32_t total_samples;                     /* counts up forever           */

static uint32_t win_end;          /* the window being classified ends here */
static uint32_t last_run;

static int idx_hey = -1, idx_games = -1, idx_music = -1;
static uint8_t hist_hey;          /* last 3 windows, 1 bit each */
static uint32_t wake_time, cooldown_until, led_off_at;
static bool awake;
static bool paused;

/* ---- interrupt side ------------------------------------------------------------------- */
static void store_half(int which)
{
    uint16_t *p = &mic_buf[which * MIC_HALF_FRAMES * HW_PER_FRAME];
    SCB_InvalidateDCache_by_Addr((uint32_t *)p, MIC_HALF_FRAMES * HW_PER_FRAME * sizeof(uint16_t));

    uint32_t w = total_samples;
    for (int i = 0; i < MIC_HALF_FRAMES; i++) {
        uint16_t hi = p[i * HW_PER_FRAME];
        uint16_t lo = p[i * HW_PER_FRAME + 1];
        int32_t  v  = (int32_t)(((uint32_t)hi << 16) | lo) >> 8;   /* signed 24-bit */
        v >>= MIC_GAIN_SHIFT;
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        ring[(w + i) & (RING_LEN - 1)] = (int16_t)v;
    }
    total_samples = w + MIC_HALF_FRAMES;
}

extern "C" void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance == SPI2) store_half(0);
}

extern "C" void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance == SPI2) store_half(1);
}

/* ---- model input: the 1.5 s ending at win_end, as float ------------------------------- */
static int get_audio(size_t offset, size_t length, float *out)
{
    uint32_t start = win_end - WINDOW + (uint32_t)offset;
    for (size_t i = 0; i < length; i++)
        out[i] = (float)ring[(start + i) & (RING_LEN - 1)];
    return 0;
}

/* ---- LEDs: LD1 green PB0, LD2 blue PB7, LD3 red PB14 ---------------------------------- */
static void leds_init(void)
{
#if !WW_LEDS
    return;
#endif
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {};
    g.Pin   = GPIO_PIN_0 | GPIO_PIN_7 | GPIO_PIN_14;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_7 | GPIO_PIN_14, GPIO_PIN_RESET);
}

static void led_flash(uint16_t pin)
{
#if !WW_LEDS
    (void)pin;
    return;
#endif
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_7 | GPIO_PIN_14, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, pin, GPIO_PIN_SET);
    led_off_at = HAL_GetTick() + 1000;
}

static int pct(float v) { return (int)(v * 100.0f + 0.5f); }

/* ---- public ------------------------------------------------------------------------- */
extern "C" void wakeword_start(void)
{
    leds_init();

    for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        const char *l = ei_classifier_inferencing_categories[i];
        if (strcmp(l, "hey_bmo") == 0) idx_hey   = i;
        if (strcmp(l, "games")   == 0) idx_games = i;
        if (strcmp(l, "music")   == 0) idx_music = i;
    }
    printf("\r\n\r\n=== BMO wake word (v5) ===\r\n");
    printf("model: %d labels, window %d samples, %d Hz\r\n",
           EI_CLASSIFIER_LABEL_COUNT, (int)WINDOW, (int)EI_CLASSIFIER_FREQUENCY);
    if (idx_hey < 0) printf("ERROR: no 'hey_bmo' label in the model!\r\n");

    if (hi2s2.hdmarx == NULL ||
        hi2s2.hdmarx->Init.PeriphDataAlignment != DMA_PDATAALIGN_HALFWORD ||
        hi2s2.hdmarx->Init.MemDataAlignment    != DMA_MDATAALIGN_HALFWORD) {
        printf("ERROR: SPI2_RX DMA must be Half Word / Half Word. Not starting.\r\n");
        return;
    }
    HAL_I2S_Receive_DMA(&hi2s2, mic_buf, MIC_FRAMES * 2);
    printf("listening... say \"hey BMO\"\r\n\r\n");
}

/* Pause while BMO is making a sound, so it doesn't hear itself (and games get the CPU).
 * When listening starts again, the old audio (BMO's own voice) is replaced with silence,
 * and the 4 s for "games"/"music" starts again from now. */
extern "C" void wakeword_set_paused(int on)
{
    if (on && !paused) {
        paused = true;
    } else if (!on && paused) {
        __disable_irq();
        memset(ring, 0, sizeof(ring));
        __enable_irq();
        hist_hey = 0;
        last_run = total_samples;
        if (awake) wake_time = HAL_GetTick();
        paused = false;
    }
}

/* "hey BMO": 2 of the last 3 windows >= THRESHOLD, or one window >= SURE */
static bool check_wake(int idx, float *scores, uint8_t *hist)
{
    if (idx < 0) return false;
    *hist = (uint8_t)(((*hist << 1) | (scores[idx] >= THRESHOLD ? 1u : 0u)) & 0x7u);
    int n = (*hist & 1) + ((*hist >> 1) & 1) + ((*hist >> 2) & 1);
    return n >= 2 || scores[idx] >= SURE;
}

/* "games" / "music": one window is enough (only used right after "hey BMO") */
static bool check_cmd(int idx, float *scores)
{
    return idx >= 0 && scores[idx] >= CMD_THRESHOLD;
}

extern "C" int wakeword_poll(void)
{
    uint32_t now = HAL_GetTick();
    if (WW_LEDS && led_off_at && (int32_t)(now - led_off_at) >= 0) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_7 | GPIO_PIN_14, GPIO_PIN_RESET);
        led_off_at = 0;
    }
    if (paused) return WW_NONE;            /* BMO is talking: don't listen, save the CPU */
    if (awake && now - wake_time > CMD_WINDOW_MS) {
        awake = false;
        printf("   (stopped listening for commands)\r\n");
    }

    uint32_t total = total_samples;
    if (total < WINDOW || total - last_run < STRIDE) return WW_NONE;
    last_run = total;
    win_end  = total;

    signal_t signal;
    signal.total_length = WINDOW;
    signal.get_data     = &get_audio;
    ei_impulse_result_t result;
    memset(&result, 0, sizeof(result));

    uint32_t t0 = HAL_GetTick();
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
    uint32_t ms = HAL_GetTick() - t0;
    if (err != EI_IMPULSE_OK) {
        printf("run_classifier error %d\r\n", (int)err);
        return WW_NONE;
    }
    if (total_samples - win_end > RING_LEN - WINDOW)
        printf("warning: model too slow, audio overwritten\r\n");

    float s[EI_CLASSIFIER_LABEL_COUNT];
    for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) s[i] = result.classification[i].value;

#if WW_DEBUG
    for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++)
        printf("%s %3d  ", result.classification[i].label, pct(s[i]));
    printf("(%lu ms)\r\n", (unsigned long)ms);
#else
    (void)ms;
#endif

    bool hey   = check_wake(idx_hey, s, &hist_hey);
    bool games = check_cmd(idx_games, s) && s[idx_games] >= s[idx_music];
    bool music = check_cmd(idx_music, s) && s[idx_music] >  s[idx_games];

    if ((int32_t)(now - cooldown_until) < 0) return WW_NONE;

    int ev = WW_NONE;
    if (hey) {
        ev = WW_HEY_BMO;
        awake = true;
        wake_time = now;
        led_flash(GPIO_PIN_0);
        printf("\r\n*********  HEY BMO!  *********  (say \"games\" or \"music\")\r\n\r\n");
    } else if (awake && games) {
        ev = WW_GAMES;
        awake = false;
        led_flash(GPIO_PIN_7);
        printf("\r\n>>>>>>>>>  GAMES  <<<<<<<<<\r\n\r\n");
    } else if (awake && music) {
        ev = WW_MUSIC;
        awake = false;
        led_flash(GPIO_PIN_14);
        printf("\r\n>>>>>>>>>  MUSIC  <<<<<<<<<\r\n\r\n");
    }
    if (ev != WW_NONE) {
        cooldown_until = now + COOLDOWN_MS;
        hist_hey = 0;
    }
    return ev;
}
