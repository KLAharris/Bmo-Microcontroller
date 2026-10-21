/* audio_sfx.c — P5: BMO's speaker. Sound effects made in code + WAV files from the SD card,
 * through SAI2 -> MAX98357A -> speaker.
 *
 * CubeMX:
 *   SAI2 block A: Master Transmit, I2S standard, 16 Bits Extended, 22 kHz, stereo,
 *   DMA SAI2_A circular, half word.  PF13 = GPIO output "AMP_SD", starts Low (muted).
 *   For WAV files also: SPI3 + FATFS User-defined + sd_spi.c (set AUDIO_USE_SD to 1).
 *
 * How WAV files play: the DMA loops over one buffer made of two halves. While one half is
 * playing, audio_sfx_poll() reads the next part of the file into the other half.
 * Each half is 0.2 s, so the main loop may be busy for up to about 0.15 s without a glitch.
 */
#include "p5_audio/audio_sfx.h"
#include "main.h"
#include "sai.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#ifndef AUDIO_USE_SD
#define AUDIO_USE_SD 1          /* 0 = sound effects only (no FatFs in the project) */
#endif

#if AUDIO_USE_SD
#include "fatfs.h"              /* USERFatFS, USERPath, f_open, f_read ... */
#endif

#define SFX_FS        22050
#define SFX_VOLUME    14000     /* sound effects: 0..32767. Raise for louder */
#define HALF_FRAMES   4410      /* 0.2 s per half buffer */
#define BUF_FRAMES    (HALF_FRAMES * 2)
#define TWO_PI        6.2831853f

/* Stereo output buffer in SRAM1, 32-byte aligned for cache maintenance.
 * 8820 frames x 2 channels x 2 bytes = 35 280 bytes (a multiple of 32). */
static int16_t out_buf[BUF_FRAMES * 2] __attribute__((section(".sram1"), aligned(32)));

enum { MODE_IDLE, MODE_SFX, MODE_FILE };
static volatile uint8_t mode = MODE_IDLE;
static volatile uint8_t sfx_finished;      /* set in the interrupt at the end of a sound effect */
static volatile uint8_t need_fill;         /* bit0 = first half free, bit1 = second half free  */
static volatile int8_t  last_half = -1;    /* half that holds the end of the file, or -1       */
static volatile uint8_t file_finished;

static char next_path[16];                 /* WAV queued to play after the sound effect        */
static int  volume_q8 = 256;               /* WAV volume, 256 = 100 %                          */

/* ---- helpers ---------------------------------------------------------------------- */
static void amp_on(void)  { HAL_GPIO_WritePin(AMP_SD_GPIO_Port, AMP_SD_Pin, GPIO_PIN_SET); }
static void amp_off(void) { HAL_GPIO_WritePin(AMP_SD_GPIO_Port, AMP_SD_Pin, GPIO_PIN_RESET); }

static void hw_stop(void)
{
    amp_off();                             /* mute first: no pop */
    HAL_SAI_DMAStop(&hsai_BlockA2);
    mode = MODE_IDLE;
    need_fill = 0;
    last_half = -1;
}

/* ---- sound effects (made in code) ------------------------------------------------- */
static int add_sweep(int at, float sec, float f0, float f1)
{
    int n = (int)(sec * SFX_FS);
    int fade = SFX_FS / 200;               /* 5 ms fade in/out: no click */
    float phase = 0.0f;
    for (int i = 0; i < n && at + i < BUF_FRAMES; i++) {
        float t = (float)i / (float)n;
        float f = f0 + (f1 - f0) * t;
        phase += TWO_PI * f / (float)SFX_FS;
        if (phase > TWO_PI) phase -= TWO_PI;
        float env = 1.0f;
        if (i < fade)     env = (float)i / (float)fade;
        if (i > n - fade) env = (float)(n - i) / (float)fade;
        int16_t s = (int16_t)(SFX_VOLUME * env * sinf(phase));
        out_buf[2 * (at + i)]     = s;
        out_buf[2 * (at + i) + 1] = s;
    }
    return at + n;
}

static int add_silence(int at, float sec)
{
    int n = (int)(sec * SFX_FS);
    for (int i = 0; i < n && at + i < BUF_FRAMES; i++) {
        out_buf[2 * (at + i)] = 0;
        out_buf[2 * (at + i) + 1] = 0;
    }
    return at + n;
}

void audio_sfx_play(int sfx)
{
    if (mode != MODE_IDLE) hw_stop();

    int n = 0;
    switch (sfx) {
    case SFX_WAKE:                         /* "bee-boop!" */
        n = add_sweep(n, 0.10f, 900.0f, 1400.0f);
        n = add_silence(n, 0.03f);
        n = add_sweep(n, 0.14f, 1400.0f, 2300.0f);
        break;
    case SFX_OK:                           /* "bip" */
        n = add_sweep(n, 0.08f, 2000.0f, 2000.0f);
        break;
    default:                               /* plain beep */
        n = add_sweep(n, 0.30f, 1000.0f, 1000.0f);
        break;
    }
    n = add_silence(n, 0.02f);             /* ends on silence */
    if (n > BUF_FRAMES) n = BUF_FRAMES;

    SCB_CleanDCache_by_Addr((uint32_t *)out_buf, sizeof(out_buf));
    sfx_finished = 0;
    mode = MODE_SFX;
    HAL_SAI_Transmit_DMA(&hsai_BlockA2, (uint8_t *)out_buf, (uint16_t)(n * 2));
    amp_on();
}

/* ---- WAV files from the SD card -------------------------------------------------- */
#if AUDIO_USE_SD
static uint8_t  sd_mounted;
static FIL      wav;
static uint32_t data_left;                 /* bytes of audio still in the file */
static uint8_t  wav_channels;
static int16_t  read_buf[HALF_FRAMES * 2]; /* raw samples from the file (mono or stereo) */

static int mount(void)
{
    if (sd_mounted) return 0;
    FRESULT r = f_mount(&USERFatFS, USERPath, 1);
    if (r != FR_OK) {
        printf("audio: SD card mount failed (FatFs error %d). Card in? FAT32?\r\n", (int)r);
        return -1;
    }
    sd_mounted = 1;
    printf("audio: SD card ready\r\n");
    return 0;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* Reads the WAV header and leaves the file at the start of the audio data. */
static int open_wav(const char *path)
{
    uint8_t h[12];
    UINT br;

    if (f_open(&wav, path, FA_READ) != FR_OK) {
        printf("audio: can't open %s\r\n", path);
        return -2;
    }
    if (f_read(&wav, h, 12, &br) != FR_OK || br != 12 ||
        memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) {
        printf("audio: %s is not a WAV file\r\n", path);
        f_close(&wav);
        return -3;
    }

    int have_fmt = 0;
    for (;;) {                             /* walk the chunks until "data" */
        uint8_t ch[8];
        if (f_read(&wav, ch, 8, &br) != FR_OK || br != 8) break;
        uint32_t size = rd32(ch + 4);
        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t f[16];
            if (size < 16 || f_read(&wav, f, 16, &br) != FR_OK || br != 16) break;
            uint16_t format = rd16(f), chans = rd16(f + 2), bits = rd16(f + 14);
            uint32_t rate = rd32(f + 4);
            if (format != 1 || bits != 16 || (chans != 1 && chans != 2)) {
                printf("audio: %s must be 16-bit PCM mono or stereo\r\n", path);
                f_close(&wav);
                return -4;
            }
            if (rate != 22050)
                printf("audio: warning, %s is %lu Hz (expected 22050): pitch will be wrong\r\n",
                       path, (unsigned long)rate);
            wav_channels = (uint8_t)chans;
            have_fmt = 1;
            f_lseek(&wav, f_tell(&wav) + (size - 16) + (size & 1));
        } else if (memcmp(ch, "data", 4) == 0) {
            if (!have_fmt) break;
            data_left = size;
            return 0;
        } else {
            f_lseek(&wav, f_tell(&wav) + size + (size & 1));    /* skip LIST etc. */
        }
    }
    printf("audio: %s has no audio data\r\n", path);
    f_close(&wav);
    return -5;
}

/* Fills one half of the output buffer from the file. Returns 1 if the file ended here. */
static int fill_half(int half)
{
    int16_t *dst = &out_buf[half * HALF_FRAMES * 2];
    UINT want = (UINT)HALF_FRAMES * wav_channels * 2;
    if (want > data_left) want = (UINT)data_left;
    UINT got = 0;
    if (want) f_read(&wav, read_buf, want, &got);
    data_left -= got;

    int frames = (int)(got / (wav_channels * 2u));
    for (int i = 0; i < frames; i++) {
        int32_t l, r;
        if (wav_channels == 1) { l = r = read_buf[i]; }
        else                   { l = read_buf[2 * i]; r = read_buf[2 * i + 1]; }
        l = (l * volume_q8) >> 8;
        r = (r * volume_q8) >> 8;
        dst[2 * i]     = (int16_t)l;
        dst[2 * i + 1] = (int16_t)r;
    }
    for (int i = frames; i < HALF_FRAMES; i++) {    /* pad the end with silence */
        dst[2 * i] = 0;
        dst[2 * i + 1] = 0;
    }
    SCB_CleanDCache_by_Addr((uint32_t *)dst, HALF_FRAMES * 2 * sizeof(int16_t));
    return (frames < HALF_FRAMES) || data_left == 0;
}

static int start_file(const char *path)
{
    if (mount() != 0) return -1;
    int r = open_wav(path);
    if (r != 0) return r;

    last_half = -1;
    file_finished = 0;
    need_fill = 0;
    if (fill_half(0))      last_half = 0;
    else if (fill_half(1)) last_half = 1;

    mode = MODE_FILE;
    HAL_SAI_Transmit_DMA(&hsai_BlockA2, (uint8_t *)out_buf, (uint16_t)(BUF_FRAMES * 2));
    amp_on();
    printf("audio: playing %s\r\n", path);
    return 0;
}
#endif /* AUDIO_USE_SD */

int audio_say(const char *path)
{
#if AUDIO_USE_SD
    if (mode == MODE_SFX) {                /* let the chirp finish, then play the file */
        strncpy(next_path, path, sizeof(next_path) - 1);
        next_path[sizeof(next_path) - 1] = 0;
        return 0;
    }
    if (mode == MODE_FILE) { f_close(&wav); }
    if (mode != MODE_IDLE) hw_stop();
    return start_file(path);
#else
    (void)path;
    return -1;
#endif
}

void audio_stop(void)
{
    next_path[0] = 0;
#if AUDIO_USE_SD
    if (mode == MODE_FILE) f_close(&wav);
#endif
    if (mode != MODE_IDLE) hw_stop();
}

void audio_set_volume(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    volume_q8 = percent * 256 / 100;
}

int audio_busy(void)
{
    return mode != MODE_IDLE || next_path[0] != 0;
}

/* ---- DMA interrupts ----------------------------------------------------------------- */
void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai != &hsai_BlockA2 || mode != MODE_FILE) return;
    /* first half just finished playing */
    if (last_half == 0) { amp_off(); file_finished = 1; }
    else need_fill |= 1;
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai != &hsai_BlockA2) return;
    if (mode == MODE_SFX) {                /* end of the sound effect */
        amp_off();
        sfx_finished = 1;
        return;
    }
    if (mode == MODE_FILE) {               /* second half just finished playing */
        if (last_half == 1) { amp_off(); file_finished = 1; }
        else need_fill |= 2;
    }
}

/* ---- main loop ---------------------------------------------------------------------- */
void audio_sfx_poll(void)
{
    if (mode == MODE_SFX && sfx_finished) {
        hw_stop();
        sfx_finished = 0;
    }

#if AUDIO_USE_SD
    if (mode == MODE_FILE) {
        if (file_finished) {
            f_close(&wav);
            hw_stop();
            file_finished = 0;
        } else {
            for (int half = 0; half < 2; half++) {
                if (!(need_fill & (1u << half))) continue;
                __disable_irq();
                need_fill &= (uint8_t)~(1u << half);
                __enable_irq();
                if (last_half < 0 && fill_half(half)) last_half = (int8_t)half;
                else if (last_half >= 0) {   /* file already ended: play silence */
                    int16_t *dst = &out_buf[half * HALF_FRAMES * 2];
                    memset(dst, 0, HALF_FRAMES * 2 * sizeof(int16_t));
                    SCB_CleanDCache_by_Addr((uint32_t *)dst, HALF_FRAMES * 2 * sizeof(int16_t));
                }
            }
        }
    }

    if (mode == MODE_IDLE && next_path[0]) {   /* queued file after a sound effect */
        char p[sizeof(next_path)];
        strcpy(p, next_path);
        next_path[0] = 0;
        start_file(p);
    }
#endif
}
