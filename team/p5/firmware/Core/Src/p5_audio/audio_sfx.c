/* audio_sfx.c — P5: short BMO sound effects through SAI2 -> MAX98357A -> speaker.
 *
 * The sounds are made by code (no sound files needed), so they work without an SD card.
 * audio_sfx_play() returns at once; the DMA plays the sound in the background, so the
 * wake word, screen and buttons keep running.
 *
 * Needs the same CubeMX setup as audio_test.c:
 *   SAI2 block A: Master Transmit, I2S standard, 16 Bits Extended, 22.05 kHz, stereo,
 *   DMA SAI2_A circular, half word.  PF13 = GPIO output "AMP_SD", starts Low (muted).
 */
#include "p5_audio/audio_sfx.h"
#include "main.h"
#include "sai.h"
#include <math.h>
#include <stdint.h>

#define SFX_FS       22050
#define SFX_VOLUME   14000      /* 0..32767. Raise for louder, lower if it sounds harsh */
#define MAX_FRAMES   8820       /* 0.4 s at 22.05 kHz */
#define TWO_PI       6.2831853f

/* Stereo buffer in SRAM1, 32-byte aligned for cache maintenance. */
static int16_t sfx_buf[((MAX_FRAMES * 2 + 15) / 16) * 16] __attribute__((section(".sram1"), aligned(32)));

static volatile uint8_t playing;
static volatile uint8_t finished;

/* Adds a tone that sweeps from f0 to f1 Hz, with 5 ms fades so there is no click.
 * Returns the frame after the tone. */
static int add_sweep(int at, float sec, float f0, float f1)
{
    int n = (int)(sec * SFX_FS);
    int fade = SFX_FS / 200;                       /* 5 ms */
    float phase = 0.0f;
    for (int i = 0; i < n && at + i < MAX_FRAMES; i++) {
        float t = (float)i / (float)n;
        float f = f0 + (f1 - f0) * t;
        phase += TWO_PI * f / (float)SFX_FS;
        if (phase > TWO_PI) phase -= TWO_PI;
        float env = 1.0f;
        if (i < fade)         env = (float)i / (float)fade;
        if (i > n - fade)     env = (float)(n - i) / (float)fade;
        int16_t s = (int16_t)(SFX_VOLUME * env * sinf(phase));
        sfx_buf[2 * (at + i)]     = s;
        sfx_buf[2 * (at + i) + 1] = s;
    }
    return at + n;
}

static int add_silence(int at, float sec)
{
    int n = (int)(sec * SFX_FS);
    for (int i = 0; i < n && at + i < MAX_FRAMES; i++) {
        sfx_buf[2 * (at + i)] = 0;
        sfx_buf[2 * (at + i) + 1] = 0;
    }
    return at + n;
}

void audio_sfx_play(int sfx)
{
    if (playing) {                                 /* cut off the previous sound */
        HAL_GPIO_WritePin(AMP_SD_GPIO_Port, AMP_SD_Pin, GPIO_PIN_RESET);
        HAL_SAI_DMAStop(&hsai_BlockA2);
        playing = 0;
    }

    int n = 0;
    switch (sfx) {
    case SFX_WAKE:                                 /* "bee-boop!" */
        n = add_sweep(n, 0.10f, 900.0f, 1400.0f);
        n = add_silence(n, 0.03f);
        n = add_sweep(n, 0.14f, 1400.0f, 2300.0f);
        break;
    case SFX_OK:                                   /* "bip" */
        n = add_sweep(n, 0.08f, 2000.0f, 2000.0f);
        break;
    default:                                       /* plain beep */
        n = add_sweep(n, 0.30f, 1000.0f, 1000.0f);
        break;
    }
    n = add_silence(n, 0.02f);                     /* ends on silence */
    if (n > MAX_FRAMES) n = MAX_FRAMES;

    /* DCache is on: push the samples to RAM so the DMA sees them. */
    SCB_CleanDCache_by_Addr((uint32_t *)sfx_buf, sizeof(sfx_buf));

    finished = 0;
    playing  = 1;
    HAL_SAI_Transmit_DMA(&hsai_BlockA2, (uint8_t *)sfx_buf, (uint16_t)(n * 2));
    HAL_GPIO_WritePin(AMP_SD_GPIO_Port, AMP_SD_Pin, GPIO_PIN_SET);   /* un-mute */
}

/* End of the buffer: mute straight away. The DMA is circular, so it is stopped in
 * audio_sfx_poll() (outside the interrupt); while muted, nothing is heard. */
void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai != &hsai_BlockA2) return;
    HAL_GPIO_WritePin(AMP_SD_GPIO_Port, AMP_SD_Pin, GPIO_PIN_RESET);
    finished = 1;
}

void audio_sfx_poll(void)
{
    if (playing && finished) {
        HAL_SAI_DMAStop(&hsai_BlockA2);
        playing = 0;
        finished = 0;
    }
}

int audio_busy(void)
{
    return playing;
}
