/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "fatfs.h"
#include "i2s.h"
#include "rng.h"
#include "sai.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "p4_demo.h"
#include <stdio.h>
#include <string.h>
#include "lcd_test.h"
#include "face_gfx.h"
#include "bmo_face.h"
#include "p5_audio/wakeword.h"                    /* P5: wake word */
#include "p5_audio/audio_sfx.h"                   /* P5: speaker beeps, SD playback */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* 1 = show the calibration pattern instead of the face (fix colour/rotation first) */
#define SCREEN_TEST 0
/* 1 = face demo: the blue USER button B1 changes face (short press = next,
 * hold > 1 s = previous); the face survives the black RESET button; USART3
 * prints each change and, every 10 s, the current face's timing.
 * 0 = normal behaviour (idle face). */
#define FACE_DEMO   0
#define BTN_DEBOUNCE_MS  30
#define BTN_LONG_MS      1000
#define DEMO_REPORT_MS   10000
#define DEMO_MAGIC       0xB30FACE5u
#define TICK_LIMIT_US    25000      /* demo check: hard limit per face frame */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
#if FACE_DEMO
/* Kept across the black RESET button: .sram1 is NOLOAD, so startup code never
 * clears it. After power-off it holds noise, which the magic value and the
 * inverted copy reject (and a power-on reset always starts at 01). */
typedef struct { uint32_t magic, face, inv, pad[5]; } demo_keep_t;   /* one 32-byte cache line */
static demo_keep_t demo_keep __attribute__((section(".sram1"), aligned(32)));
static int power_on;                     /* reset cause, read before anything clears it */

static struct {
  uint32_t t0_ms;                        /* start of this report window */
  uint32_t cyc0;                         /* CYCCNT at the start of the window */
  uint32_t rearmed;                      /* times the counter had been switched off */
  uint32_t worst_cyc, over;              /* worst face frame, frames over the budget */
  uint64_t busy_cyc;                     /* time in face_tick() calls that drew a frame */
  uint64_t poll_cyc;                     /* time in calls that returned at once */
} demo;
static uint32_t change_t0;               /* button press that started a change */
static int      change_open;             /* change time still to be printed */
#endif
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */
static void say(const char *s);
int p4_talk_pressed(void);                      /* P4: green TALK button (push-to-talk), in p4_demo.c */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Print a message to the ST-LINK virtual COM port (USART3, 115200 baud) */
static void say(const char *s)
{
  HAL_UART_Transmit(&huart3, (uint8_t *)s, (uint16_t)strlen(s), 100);
}

/* P5: lets printf() print to the ST-LINK virtual COM port */
int __io_putchar(int ch)
{
  uint8_t c = (uint8_t)ch;
  HAL_UART_Transmit(&huart3, &c, 1, 100);
  return ch;
}

/* Cycle counter on. zero = 0 keeps the count (re-arming inside the loop). */
static void dwt_on(int zero)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;   /* trace block on */
  DWT->LAR = 0xC5ACCE55;                            /* Cortex-M7: unlock DWT writes */
  if (zero) DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* The face's real clock: face_tick() stops drawing before 25 ms by it. */
static uint32_t dwt_cycles(void) { return DWT->CYCCNT; }

#if FACE_DEMO
static void demo_keep_save(face_id_t f)
{
  demo_keep.magic = DEMO_MAGIC;
  demo_keep.face  = (uint32_t)f;
  demo_keep.inv   = ~(uint32_t)f;
  SCB_CleanDCache_by_Addr((uint32_t *)&demo_keep, sizeof demo_keep);   /* D-cache is write-back */
}

static face_id_t demo_keep_load(void)
{
  if (!power_on && demo_keep.magic == DEMO_MAGIC && demo_keep.face < FACE_COUNT
      && demo_keep.inv == ~demo_keep.face)
    return (face_id_t)demo_keep.face;
  return FACE_IDLE;
}

static void demo_stats_reset(uint32_t now)
{
  uint32_t rearmed = demo.rearmed;
  memset(&demo, 0, sizeof demo);
  demo.t0_ms = now;
  demo.cyc0 = DWT->CYCCNT;
  demo.rearmed = rearmed;                          /* counted over the whole run */
}

static void demo_change(int dir, uint32_t now)
{
  face_id_t f = (face_id_t)((face_current() + FACE_COUNT + dir) % FACE_COUNT);
  face_show(f);                                     /* blink transition, runs in face_tick() */
  demo_keep_save(f);
  change_t0 = now;
  change_open = 1;
}

/* Blue button B1 (pressed = High), polled: 30 ms debounce, act on release. */
static void demo_button(uint32_t now)
{
  static uint8_t raw_last, stable;
  static uint32_t t_edge, t_down;
  uint8_t raw = HAL_GPIO_ReadPin(USER_BTN_GPIO_Port, USER_BTN_Pin) == GPIO_PIN_SET;

  if (raw != raw_last) { raw_last = raw; t_edge = now; }
  if (raw == stable || now - t_edge < BTN_DEBOUNCE_MS) return;
  stable = raw;
  if (stable) t_down = now;
  else        demo_change(now - t_down > BTN_LONG_MS ? -1 : +1, now);
}

static void print_ms(char *buf, size_t n, const char *label, uint32_t us)
{
  snprintf(buf, n, "%s%lu.%lu ms", label, (unsigned long)(us / 1000u), (unsigned long)(us % 1000u / 100u));
}

/* After every face_tick(): timing, button, change time, 10 s report.
 * frame = that call ran a face frame (face_frames() moved), else it was a poll. */
static void demo_poll(uint32_t tick_cyc, int frame)
{
  uint32_t now = HAL_GetTick(), cyc_us = SystemCoreClock / 1000000u;
  char line[128], a[32];

  /* Something (e.g. the debugger starting SWV trace) can switch the counter off. */
  if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) || !(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
    dwt_on(0);
    demo.rearmed++;
  }

  if (frame) {
    demo.busy_cyc += tick_cyc;
    if (tick_cyc > demo.worst_cyc) demo.worst_cyc = tick_cyc;
    if (tick_cyc / cyc_us > TICK_LIMIT_US) demo.over++;
  } else {
    demo.poll_cyc += tick_cyc;
  }

  demo_button(now);

  if (change_open && !face_changing()) {
    change_open = 0;
    snprintf(line, sizeof line, "face %-15s change %lu ms\r\n", face_name(face_current()),
             (unsigned long)(now - change_t0));
    say(line);
    demo_stats_reset(HAL_GetTick());
    return;                                         /* window starts after the print, not at `now` */
  }

  if ((int32_t)(now - demo.t0_ms) >= DEMO_REPORT_MS && DWT->CYCCNT == demo.cyc0) {
    snprintf(line, sizeof line, "  DWT stuck: DEMCR=%08lx CTRL=%08lx, no timing this window\r\n",
             (unsigned long)CoreDebug->DEMCR, (unsigned long)DWT->CTRL);
    say(line);
    dwt_on(0);
    demo_stats_reset(HAL_GetTick());
    return;
  }
  if ((int32_t)(now - demo.t0_ms) >= DEMO_REPORT_MS) {
    uint32_t span_us = (now - demo.t0_ms) * 1000u;
    uint32_t load10 = (uint32_t)(demo.busy_cyc / cyc_us * 1000u / span_us);   /* 0.1 % */
    uint32_t poll10 = (uint32_t)(demo.poll_cyc / cyc_us * 1000u / span_us);
    print_ms(a, sizeof a, "worst tick ", demo.worst_cyc / cyc_us);
    snprintf(line, sizeof line, "  %-15s %s, over 25 ms: %lu, CPU %lu.%lu %% (+ empty polls %lu.%lu %%)%s\r\n",
             face_name(face_current()), a, (unsigned long)demo.over,
             (unsigned long)(load10 / 10u), (unsigned long)(load10 % 10u),
             (unsigned long)(poll10 / 10u), (unsigned long)(poll10 % 10u),
             demo.rearmed ? " (DWT was switched off and re-enabled)" : "");
    say(line);
    demo_stats_reset(HAL_GetTick());               /* printing took time: start after it */
  }
}
#endif

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
#if FACE_DEMO
  power_on = __HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != RESET;   /* power-on, not the RESET button */
  __HAL_RCC_CLEAR_RESET_FLAGS();
#endif
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2S2_Init();
  MX_RNG_Init();
  MX_SAI2_Init();
  MX_SPI5_Init();
  MX_USART3_UART_Init();
  MX_TIM3_Init();
  MX_SPI3_Init();
  MX_FATFS_Init();
  /* USER CODE BEGIN 2 */
  say("\r\n=== BMO boot OK ===\r\n");
  say("build " __DATE__ " " __TIME__ "\r\n");     /* which firmware is on the board */

  gfx_init();                                   /* LCD on, landscape */
  say("LCD init done\r\n");

#if SCREEN_TEST
  lcd_screen_test();
  say("Screen test pattern shown\r\n");
  while (1) { }
#endif

  uint32_t seed = 0;
  if (HAL_RNG_GenerateRandomNumber(&hrng, &seed) != HAL_OK) seed = HAL_GetTick();
  face_init(seed);
  dwt_on(1);                                        /* cycle counter: the face's real clock */
  face_set_clock(dwt_cycles, SystemCoreClock / 1000000u);
  say("Face ready\r\n");
  p4_demo_init();                                   /* P4: prints the key help */
  audio_sfx_play(SFX_BEEP);                         /* P5: startup beep */
  wakeword_start();                                 /* P5: start listening */
  say("Wake word listening\r\n");

#if FACE_DEMO
  char line[96];
  uint32_t c0 = DWT->CYCCNT;                        /* check it runs, and at what rate */
  HAL_Delay(10);
  uint32_t dc = DWT->CYCCNT - c0;
  if (dc)
    snprintf(line, sizeof line, "DWT ok: %lu cycles in ~10 ms (216 MHz = 2160000)\r\n", (unsigned long)dc);
  else
    snprintf(line, sizeof line, "DWT stuck: DEMCR=%08lx CTRL=%08lx LSR=%08lx\r\n",
             (unsigned long)CoreDebug->DEMCR, (unsigned long)DWT->CTRL, (unsigned long)DWT->LSR);
  say(line);

  face_id_t start = demo_keep_load();
  face_show(start);                                 /* before the first tick: switches at once */
  demo_keep_save(start);
  say("FACE_DEMO: blue button = next face, hold > 1 s = previous\r\n");
  snprintf(line, sizeof line, "face %-15s (%s)\r\n", face_name(start),
           power_on ? "power-on" : "kept across reset");
  say(line);
  demo_stats_reset(HAL_GetTick());
#endif
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
#if FACE_DEMO
    uint32_t f0 = face_frames(), c0 = DWT->CYCCNT;
#endif
    face_tick(HAL_GetTick());                   /* all face life; stops drawing before 25 ms */
    p4_demo_tick(HAL_GetTick());                /* P4: read serial keys, run games and home screen */
    /* ---- P5 audio: wake word, sound effects, SD voice lines and songs ---- */
    {
      static int song_on = 0;                   /* 1 while a song is playing */
      static int next_track = 1;                /* "music" plays TRACK1, TRACK2, ... */
      const int num_tracks = 6;               /* how many TRACKn.WAV are on the card */

      if (song_on && !audio_busy()) song_on = 0;            /* song ended by itself */

      /* don't listen during BMO's own short sounds, but keep listening during songs */
      wakeword_set_paused((audio_busy() && !song_on) || p4_game_running());

      if (p4_talk_pressed()) {                              /* push-to-talk: listen without "hey BMO" */
        say("TALK pressed - say games or music\r\n");
        if (song_on) { audio_stop(); song_on = 0; }
        wakeword_force_wake();                              /* opens the 4 s window for games / music */
        audio_sfx_play(SFX_WAKE);
      }

      int ww = (int)wakeword_poll();

      if (ww == WW_HEY_BMO && song_on) {                    /* "hey BMO" during a song = stop */
        say("HEY BMO! (stop song)\r\n");
        audio_stop();
        song_on = 0;
        audio_sfx_play(SFX_OK);
      } else if (ww == WW_HEY_BMO) {
        say("HEY BMO!\r\n");
        audio_sfx_play(SFX_WAKE);
        audio_say("HI.WAV");                                /* voice line from the SD card */
      } else if (ww == WW_GAMES) {
        say("GAMES!\r\n");
        audio_sfx_play(SFX_OK);
        audio_say("GAMES.WAV");
      } else if (ww == WW_MUSIC) {
        char name[16];
        say("MUSIC!\r\n");
        snprintf(name, sizeof name, "TRACK%d.WAV", next_track);
        audio_sfx_play(SFX_OK);
        if (audio_say(name) == 0) song_on = 1;
        next_track = next_track % num_tracks + 1;
      }
      audio_sfx_poll();                         /* keeps sounds and SD playback going */
    }
#if FACE_DEMO
    demo_poll(DWT->CYCCNT - c0, face_frames() != f0);
#endif
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 216;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 9;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_7) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
