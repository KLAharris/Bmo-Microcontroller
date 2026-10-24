/*
 * lcd_test.c — ILI9486 4.0" SPI bring-up test (polling SPI5, no DMA)
 *
 * Wiring (Nucleo-F767ZI, CN9 even pins):
 *   CS    = PD5  (CN9-6)     DC/RS = PD4 (CN9-8)    RESET = PD3 (CN9-10)
 *   SDO   = PF8  (CN9-24)    SCK   = PF7 (CN9-26)   SDI   = PF9 (CN9-28)
 *   VCC + LED = +3V3 (CN8-7)   GND = CN8-11
 */
#include "main.h"
#include "spi.h"
#include "lcd_test.h"

uint16_t lcd_w = 320;
uint16_t lcd_h = 480;

/* ===================== SCREEN CALIBRATION — change these two =====================
 * Run lcd_screen_test() and look at the RED square (should be top-left):
 *   red square shows BLUE          -> flip bit 0x08 in LCD_MADCTL_LANDSCAPE
 *   white area shows BLACK         -> set LCD_INVERT to 1 (or back to 0)
 *   red square top-RIGHT           -> flip bit 0x40
 *   red square bottom-LEFT         -> flip bit 0x80
 *   red square bottom-RIGHT        -> flip both 0x40 and 0x80 (0xC0)
 *   (if a flip moves it the wrong way, undo it and flip the other bit)
 * Common landscape values: 0x28, 0xE8, 0x20, 0xE0, 0x68, 0xA8
 */
#define LCD_MADCTL_LANDSCAPE  0x28
#define LCD_INVERT            1        /* this panel needs inversion ON (0x21). 0 made mint show as navy */

#define MADCTL_PORTRAIT  (0x40 | (LCD_MADCTL_LANDSCAPE & 0x08))

#define CS_LOW()   HAL_GPIO_WritePin(GPIOD, GPIO_PIN_5, GPIO_PIN_RESET)
#define CS_HIGH()  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_5, GPIO_PIN_SET)
#define DC_CMD()   HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, GPIO_PIN_RESET)
#define DC_DATA()  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, GPIO_PIN_SET)
#define RST_LOW()  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, GPIO_PIN_RESET)
#define RST_HIGH() HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, GPIO_PIN_SET)

static void lcd_cmd(uint8_t c)
{
    DC_CMD(); CS_LOW();
    HAL_SPI_Transmit(&hspi5, &c, 1, HAL_MAX_DELAY);
    CS_HIGH();
}

static void lcd_data(const uint8_t *d, uint16_t n)
{
    DC_DATA(); CS_LOW();
    HAL_SPI_Transmit(&hspi5, (uint8_t *)d, n, HAL_MAX_DELAY);
    CS_HIGH();
}

static void lcd_data1(uint8_t v)
{
    lcd_data(&v, 1);
}

/* Send a command followed by its parameter bytes */
static void lcd_seq(uint8_t cmd, const uint8_t *d, uint16_t n)
{
    lcd_cmd(cmd);
    if (n) lcd_data(d, n);
}

void lcd_init(void)
{
    CS_HIGH();
    DC_DATA();

    /* hardware reset */
    RST_HIGH(); HAL_Delay(5);
    RST_LOW();  HAL_Delay(20);
    RST_HIGH(); HAL_Delay(150);

    lcd_cmd(0x01); HAL_Delay(150);                       /* software reset */

    /* ILI9486 power / gamma setup (module maker's sequence) */
    static const uint8_t f1[] = {0x36,0x04,0x00,0x3C,0x0F,0x8F};
    static const uint8_t f2[] = {0x18,0xA3,0x12,0x02,0xB2,0x12,0xFF,0x10,0x00};
    static const uint8_t f8[] = {0x21,0x04};
    static const uint8_t f9[] = {0x00,0x08};
    static const uint8_t c5[] = {0x00,0x91,0x80,0x00};
    static const uint8_t e0[] = {0x0F,0x1F,0x1C,0x0C,0x0F,0x08,0x48,0x98,
                                 0x37,0x0A,0x13,0x04,0x11,0x0D,0x00};
    static const uint8_t e1[] = {0x0F,0x32,0x2E,0x0B,0x0D,0x05,0x47,0x75,
                                 0x37,0x06,0x10,0x03,0x24,0x20,0x00};

    lcd_seq(0xF1, f1, sizeof f1);
    lcd_seq(0xF2, f2, sizeof f2);
    lcd_seq(0xF8, f8, sizeof f8);
    lcd_seq(0xF9, f9, sizeof f9);
    lcd_cmd(0xB4); lcd_data1(0x00);                      /* display inversion control */
    lcd_cmd(0xC1); lcd_data1(0x41);                      /* power control 2 */
    lcd_seq(0xC5, c5, sizeof c5);                        /* VCOM */
    lcd_seq(0xE0, e0, sizeof e0);                        /* positive gamma */
    lcd_seq(0xE1, e1, sizeof e1);                        /* negative gamma */

    lcd_cmd(0x3A); lcd_data1(0x55);                      /* 16-bit RGB565 */
    lcd_cmd(0x36); lcd_data1(MADCTL_PORTRAIT);           /* portrait. Red/blue swapped? clear bit 0x08 */
    lcd_w = 320; lcd_h = 480;

    lcd_cmd(LCD_INVERT ? 0x21 : 0x20);                   /* colour inversion off/on */

    lcd_cmd(0x11); HAL_Delay(150);                       /* sleep out */
    lcd_cmd(0x29); HAL_Delay(20);                        /* display on */
}

void lcd_set_landscape(void)
{
    lcd_cmd(0x36); lcd_data1(LCD_MADCTL_LANDSCAPE);
    lcd_w = 480; lcd_h = 320;
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t col[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    uint8_t row[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    lcd_seq(0x2A, col, 4);                               /* column range */
    lcd_seq(0x2B, row, 4);                               /* row range */
    lcd_cmd(0x2C);                                       /* write pixels */
}

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    static uint8_t line[480 * 2];

    if (x >= lcd_w || y >= lcd_h || w == 0 || h == 0) return;
    if (x + w > lcd_w) w = lcd_w - x;
    if (y + h > lcd_h) h = lcd_h - y;

    for (uint16_t i = 0; i < w; i++) {                   /* high byte first */
        line[2 * i]     = color >> 8;
        line[2 * i + 1] = color & 0xFF;
    }

    lcd_set_window(x, y, x + w - 1, y + h - 1);
    DC_DATA(); CS_LOW();
    for (uint16_t r = 0; r < h; r++)
        HAL_SPI_Transmit(&hspi5, line, w * 2, HAL_MAX_DELAY);
    CS_HIGH();
}

void lcd_push(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *px)
{
    uint32_t left = (uint32_t)w * h * 2;

    if (w == 0 || h == 0 || x + w > lcd_w || y + h > lcd_h) return;

    lcd_set_window(x, y, x + w - 1, y + h - 1);
    DC_DATA(); CS_LOW();
    while (left) {                                       /* HAL length is 16-bit */
        uint16_t n = left > 32768 ? 32768 : (uint16_t)left;
        HAL_SPI_Transmit(&hspi5, (uint8_t *)px, n, HAL_MAX_DELAY);
        px += n; left -= n;
    }
    CS_HIGH();
}

void lcd_fill(uint16_t color)
{
    lcd_fill_rect(0, 0, lcd_w, lcd_h, color);
}

/* Calibration pattern (landscape). Expected:
 *   white background, RED square top-left, GREEN top-right,
 *   BLUE bottom-left, BLACK bottom-right, BMO mint bar in the middle. */
void lcd_screen_test(void)
{
    lcd_set_landscape();
    lcd_fill(LCD_WHITE);
    lcd_fill_rect(0,   0,   80, 80, LCD_RED);
    lcd_fill_rect(400, 0,   80, 80, LCD_GREEN);
    lcd_fill_rect(0,   240, 80, 80, LCD_BLUE);
    lcd_fill_rect(400, 240, 80, 80, LCD_BLACK);
    lcd_fill_rect(140, 130, 200, 60, 0xCF38);            /* #C9E4C3 mint */
}

/* Red, green, blue, white, black — one second each.
   A small square marks the (0,0) corner. */
void lcd_test_cycle(void)
{
    static const uint16_t colors[] = { LCD_RED, LCD_GREEN, LCD_BLUE, LCD_WHITE, LCD_BLACK };

    for (unsigned i = 0; i < sizeof colors / sizeof colors[0]; i++) {
        lcd_fill(colors[i]);
        lcd_fill_rect(0, 0, 40, 40, colors[i] == LCD_WHITE ? LCD_BLACK : LCD_WHITE);
        HAL_Delay(1000);
    }
}
