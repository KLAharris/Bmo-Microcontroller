/* mic_stream.c — P5: stream the INMP441 to a laptop over the ST-LINK USB serial port.
 *
 * Used for wake-word data collection: the laptop script (record_clips.py) receives
 * the audio and saves labelled WAV files for Edge Impulse.
 *
 * Audio format sent: 16 kHz, 16-bit signed, mono (the mic's left slot).
 *
 * Packet (little-endian), one per 32 ms DMA half-buffer:
 *   "BMOA"  4 bytes   magic, so the laptop can find the start of a packet
 *   seq     uint16    counts up by 1 per packet; a gap means audio was dropped
 *   count   uint16    samples in this packet (512)
 *   pcm     int16[512]
 *   sum     uint16    sum of the pcm bytes, to catch corrupted packets
 *
 * Needs in the .ioc (same as mic_test):
 *   I2S2: Half-Duplex Master, Master Receive, Philips, 24 bit on 32 bit frame, 16 kHz
 *   DMA:  SPI2_RX, Circular, Half Word / Half Word
 *   USART3 on PD8/PD9. mic_stream_start() switches it to 921600 baud itself.
 *
 * Do not printf while streaming: text would be mixed into the audio packets.
 */
#include "p5_audio/mic_stream.h"
#include "main.h"
#include "i2s.h"
#include "usart.h"
#include <stdio.h>
#include <stdint.h>

#define MIC_FRAMES       1024               /* stereo frames per DMA buffer = 64 ms        */
#define MIC_HALF_FRAMES  (MIC_FRAMES / 2)   /* 512 frames = 32 ms per half                 */
#define HW_PER_FRAME     4                  /* L hi, L lo, R hi, R lo (half-word DMA)      */
#define STREAM_BAUD      921600U

/* 24-bit -> 16-bit conversion. A shift of 8 would be plain 16-bit; 6 adds x4 gain
 * because the INMP441 is quiet. The wake-word code on the robot MUST use the same
 * value, or the model sees audio at a different volume than it was trained on. */
#define MIC_GAIN_SHIFT   7

/* DMA buffer in SRAM1 (same layout as the team project), 32-byte aligned for cache maintenance. */
static uint16_t mic_buf[MIC_FRAMES * HW_PER_FRAME] __attribute__((section(".sram1"), aligned(32)));

typedef struct __attribute__((packed)) {
    char     magic[4];
    uint16_t seq;
    uint16_t count;
    int16_t  pcm[MIC_HALF_FRAMES];
    uint16_t sum;
} mic_packet_t;

static mic_packet_t pkt = { .magic = { 'B', 'M', 'O', 'A' }, .count = MIC_HALF_FRAMES };

static volatile uint8_t  half_ready;      /* bit0 = first half ready, bit1 = second half */
static volatile uint16_t half_seq[2];     /* packet number of each half                  */
static volatile uint16_t seq_counter;

void mic_stream_start(void)
{
    if (hi2s2.hdmarx == NULL ||
        hi2s2.hdmarx->Init.PeriphDataAlignment != DMA_PDATAALIGN_HALFWORD ||
        hi2s2.hdmarx->Init.MemDataAlignment    != DMA_MDATAALIGN_HALFWORD) {
        printf("mic_stream: SPI2_RX DMA must exist and be Half Word / Half Word. Not starting.\r\n");
        return;
    }

    printf("mic_stream: switching serial to %lu baud, 16 kHz 16-bit mono\r\n",
           (unsigned long)STREAM_BAUD);
    HAL_Delay(20);                                   /* let that line finish sending */

    if (huart3.Init.BaudRate != STREAM_BAUD) {
        huart3.Init.BaudRate = STREAM_BAUD;
        HAL_UART_Init(&huart3);
    }

    /* Size counts 32-bit samples (left + right); the HAL doubles it for half-word DMA. */
    HAL_I2S_Receive_DMA(&hi2s2, mic_buf, MIC_FRAMES * 2);
}

void mic_stream_poll(void)
{
    uint8_t r = half_ready;
    if (!r) return;
    int which = (r & 1) ? 0 : 1;
    if (r == 3 && (int16_t)(half_seq[1] - half_seq[0]) < 0) which = 1;   /* both ready: send the older one first */
    __disable_irq();
    half_ready &= (uint8_t)~(1u << which);
    __enable_irq();

    uint16_t *p = &mic_buf[which * MIC_HALF_FRAMES * HW_PER_FRAME];
    /* DCache is on: drop stale cache lines so we read what the DMA just wrote. */
    SCB_InvalidateDCache_by_Addr((uint32_t *)p, MIC_HALF_FRAMES * HW_PER_FRAME * sizeof(uint16_t));

    uint16_t sum = 0;
    for (int i = 0; i < MIC_HALF_FRAMES; i++) {
        uint16_t hi = p[i * HW_PER_FRAME];           /* left slot = the mic (L/R tied to GND) */
        uint16_t lo = p[i * HW_PER_FRAME + 1];
        int32_t  v  = (int32_t)(((uint32_t)hi << 16) | lo) >> 8;   /* signed 24-bit */
        v >>= MIC_GAIN_SHIFT;
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        pkt.pcm[i] = (int16_t)v;
        uint16_t u = (uint16_t)v;
        sum = (uint16_t)(sum + (u & 0xFF) + (u >> 8));
    }
    pkt.seq = half_seq[which];
    pkt.sum = sum;

    /* About 11 ms at 921600 baud, well inside the 32 ms until the next half is ready. */
    HAL_UART_Transmit(&huart3, (uint8_t *)&pkt, sizeof(pkt), 100);
}

/* If the main loop falls behind, a half is overwritten and its seq number is skipped,
 * so the laptop sees the gap and tells you to re-record that clip. */
void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance != SPI2) return;
    half_seq[0] = seq_counter++;
    half_ready |= 1;
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance != SPI2) return;
    half_seq[1] = seq_counter++;
    half_ready |= 2;
}
