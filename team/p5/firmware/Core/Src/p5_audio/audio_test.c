/* audio_test.c — P5 bring-up step 1: steady tone through SAI2 -> MAX98357A -> speaker.
 *
 * Uses the SAI2 setup in team/p5/firmware/cubemx/p5_test.ioc (to be added to P2's boum.ioc):
 *   Master Transmit, I2S standard, 16 Bits Extended, 22.05 kHz, stereo,
 *   DMA2 Stream 2, circular, half word.
 *
 * 441 Hz is used instead of 440 Hz because 22050 / 441 = 50 samples per cycle
 * exactly, so the buffer loops forever with no click at the seam.
 */
#include "p5_audio/audio_test.h"
#include "main.h"
#include "sai.h"
#include <math.h>
#include <stdio.h>
#include <stdint.h>

#define TONE_FS      22050
#define TONE_HZ      441
#define TONE_CYCLES  10
#define TONE_FRAMES  (TONE_CYCLES * TONE_FS / TONE_HZ)   /* 500 stereo frames */
#define TONE_AMPL    16000                                /* ~ -6 dBFS: clear, not painful */

/* DMA buffer in SRAM1 (same layout as the team project), 32-byte aligned for cache maintenance. */
static int16_t tone_buf[TONE_FRAMES * 2] __attribute__((section(".sram1"), aligned(32)));

void audio_test_tone_start(void)
{
    for (int i = 0; i < TONE_FRAMES; i++) {
        float phase = 2.0f * 3.14159265f * (float)(i * TONE_HZ) / (float)TONE_FS;
        int16_t s = (int16_t)(TONE_AMPL * sinf(phase));
        tone_buf[2 * i]     = s;   /* left: the slot the amp plays when SD is driven high */
        tone_buf[2 * i + 1] = s;   /* right: same sample, so it works either way */
    }

    /* DCache is on: push the samples from cache to RAM so the DMA sees them. */
    SCB_CleanDCache_by_Addr((uint32_t *)tone_buf, sizeof(tone_buf));

    /* Size counts 16-bit items (left + right), not frames. */
    HAL_StatusTypeDef st = HAL_SAI_Transmit_DMA(&hsai_BlockA2, (uint8_t *)tone_buf, TONE_FRAMES * 2);
    printf("audio_test: SAI2 DMA start = %d (0 = OK), buffer at 0x%08lX\r\n",
           (int)st, (unsigned long)(uintptr_t)tone_buf);

    HAL_Delay(5);                                                /* clocks running first */
    HAL_GPIO_WritePin(AMP_SD_GPIO_Port, AMP_SD_Pin, GPIO_PIN_SET); /* un-mute the amp */
}

void audio_test_tone_stop(void)
{
    HAL_GPIO_WritePin(AMP_SD_GPIO_Port, AMP_SD_Pin, GPIO_PIN_RESET); /* mute first: no pop */
    HAL_SAI_DMAStop(&hsai_BlockA2);
}
