/* sd_spi.c — P5: SD card over SPI3 for FatFs (based on the classic ElmChan MMC/SD SPI driver).
 *
 * CubeMX: SPI3 Full-Duplex Master, 8 bit, MSB first, CPOL Low, CPHA 1 Edge, software NSS,
 *         prescaler 256 (slow, needed for card start-up; this file speeds it up afterwards).
 *         PD2 = GPIO output "SD_CS", starts High.   FATFS: User-defined.
 *
 * Hook it up in FATFS/Target/user_diskio.c (inside the USER CODE blocks):
 *     #include "p5_audio/sd_spi.h"                       (USER CODE BEGIN DECL)
 *     USER_initialize : return SD_disk_initialize(pdrv);
 *     USER_status     : return SD_disk_status(pdrv);
 *     USER_read       : return SD_disk_read(pdrv, buff, sector, count);
 *     USER_write      : return SD_disk_write(pdrv, buff, sector, count);
 *     USER_ioctl      : return SD_disk_ioctl(pdrv, cmd, buff);
 *
 * Polling only (no DMA, no interrupts), so there is nothing to do for the cache.
 */
#include "p5_audio/sd_spi.h"
#include "main.h"
#include "spi.h"
#include <string.h>
#include <stdio.h>

#define SD_DEBUG 1          /* 1 = print what the card answers (to find SD problems) */
#if SD_DEBUG
#define DBG(...) printf(__VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

/* 54 MHz APB1 / 16 = 3.4 MHz: safe with jumper wires and SD modules with a level shifter
 * (a song needs only about 0.35 Mbit/s). /4 (13.5 MHz) gave FatFs error 1 on the breadboard.
 * If reads still fail, try SPI_BAUDRATEPRESCALER_64. */
#define SD_FAST_PRESCALER  SPI_BAUDRATEPRESCALER_16

/* SD commands */
#define CMD0   (0)          /* GO_IDLE_STATE */
#define CMD1   (1)          /* SEND_OP_COND (MMC) */
#define ACMD41 (0x80 + 41)  /* SEND_OP_COND (SDC) */
#define CMD8   (8)          /* SEND_IF_COND */
#define CMD9   (9)          /* SEND_CSD */
#define CMD12  (12)         /* STOP_TRANSMISSION */
#define CMD16  (16)         /* SET_BLOCKLEN */
#define CMD17  (17)         /* READ_SINGLE_BLOCK */
#define CMD18  (18)         /* READ_MULTIPLE_BLOCK */
#define CMD24  (24)         /* WRITE_BLOCK */
#define CMD55  (55)         /* APP_CMD */
#define CMD58  (58)         /* READ_OCR */

/* card types */
#define CT_MMC   0x01
#define CT_SD1   0x02
#define CT_SD2   0x04
#define CT_BLOCK 0x08       /* block addressing (SDHC/SDXC) */

static volatile DSTATUS stat = STA_NOINIT;
static BYTE card_type;
static uint32_t cur_prescaler;

/* ---- low level --------------------------------------------------------------------- */
static void cs_high(void) { HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET); }
static void cs_low(void)  { HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET); }

static BYTE xchg(BYTE d)
{
    BYTE r = 0xFF;
    HAL_SPI_TransmitReceive(&hspi3, &d, &r, 1, 10);
    return r;
}

/* Receive n bytes (MOSI kept high: the buffer is filled with 0xFF and sent while receiving). */
static void rcvr_multi(BYTE *buf, UINT n)
{
    memset(buf, 0xFF, n);
    HAL_SPI_TransmitReceive(&hspi3, buf, buf, (uint16_t)n, 100);
}

static void xmit_multi(const BYTE *buf, UINT n)
{
    HAL_SPI_Transmit(&hspi3, (uint8_t *)buf, (uint16_t)n, 100);
}

static void set_speed(uint32_t prescaler)
{
    __HAL_SPI_DISABLE(&hspi3);
    hspi3.Init.BaudRatePrescaler = prescaler;
    HAL_SPI_Init(&hspi3);
    cur_prescaler = prescaler;
}

static int wait_ready(uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();
    BYTE d;
    do {
        d = xchg(0xFF);
    } while (d != 0xFF && HAL_GetTick() - t0 < ms);
    return d == 0xFF;
}

static void deselect(void)
{
    cs_high();
    xchg(0xFF);             /* one extra clock so the card releases MISO */
}

static int select(void)
{
    cs_low();
    xchg(0xFF);
    if (wait_ready(500)) return 1;
    deselect();
    return 0;
}

static int rcvr_datablock(BYTE *buf, UINT n)
{
    uint32_t t0 = HAL_GetTick();
    BYTE token;
    do {
        token = xchg(0xFF);
    } while (token == 0xFF && HAL_GetTick() - t0 < 200);
    if (token != 0xFE) { DBG("sd: data token %02X (expected FE)\r\n", token); return 0; }
    rcvr_multi(buf, n);
    xchg(0xFF); xchg(0xFF);  /* discard CRC */
    return 1;
}

static int xmit_datablock(const BYTE *buf, BYTE token)
{
    if (!wait_ready(500)) return 0;
    xchg(token);
    xmit_multi(buf, 512);
    xchg(0xFF); xchg(0xFF);  /* dummy CRC */
    BYTE resp = xchg(0xFF);
    return (resp & 0x1F) == 0x05;
}

static BYTE send_cmd(BYTE cmd, DWORD arg)
{
    BYTE n, res;

    if (cmd & 0x80) {        /* ACMD = CMD55 + CMD */
        cmd &= 0x7F;
        res = send_cmd(CMD55, 0);
        if (res > 1) return res;
    }

    if (cmd == CMD0) {       /* before CMD0 the card holds MISO low, so it never looks "ready": */
        deselect();          /* just select it and send, don't wait for 0xFF */
        cs_low();
        xchg(0xFF);
    } else if (cmd != CMD12) {
        deselect();
        if (!select()) return 0xFF;
    }

    xchg(0x40 | cmd);
    xchg((BYTE)(arg >> 24));
    xchg((BYTE)(arg >> 16));
    xchg((BYTE)(arg >> 8));
    xchg((BYTE)arg);
    n = 0x01;                /* dummy CRC + stop bit */
    if (cmd == CMD0) n = 0x95;
    if (cmd == CMD8) n = 0x87;
    xchg(n);

    if (cmd == CMD12) xchg(0xFF);   /* skip a stuff byte */
    n = 10;
    do {
        res = xchg(0xFF);
    } while ((res & 0x80) && --n);
    return res;
}

/* ---- FatFs interface ---------------------------------------------------------------- */
DSTATUS SD_disk_initialize(BYTE pdrv);

/* ST's diskio.c only calls disk_initialize once per power-up. If that first try failed
 * (card not in yet, loose wire...), every later mount would just fail with FR_DISK_ERR.
 * So the read/write/ioctl functions retry the start-up themselves when needed. */
static int ensure_init(void)
{
    if (stat & STA_NOINIT) SD_disk_initialize(0);
    return !(stat & STA_NOINIT);
}

DSTATUS SD_disk_initialize(BYTE pdrv)
{
    BYTE n, ty = 0, ocr[4];

    if (pdrv) return STA_NOINIT;

    set_speed(SPI_BAUDRATEPRESCALER_256);           /* about 210 kHz for start-up */
    cs_high();
    for (n = 10; n; n--) xchg(0xFF);                /* 80 dummy clocks */

    BYTE r0 = send_cmd(CMD0, 0);
    DBG("\r\nsd: v3 driver, CMD0 -> %02X (01 = card found)\r\n", r0);
    if (r0 == 1) {                                  /* card is in idle state */
        uint32_t t0 = HAL_GetTick();
        if (send_cmd(CMD8, 0x1AA) == 1) {           /* SD v2 */
            for (n = 0; n < 4; n++) ocr[n] = xchg(0xFF);
            DBG("sd: CMD8 echo %02X %02X (01 AA = OK)\r\n", ocr[2], ocr[3]);
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
                while (HAL_GetTick() - t0 < 1000 && send_cmd(ACMD41, 1UL << 30)) { }
                if (HAL_GetTick() - t0 < 1000 && send_cmd(CMD58, 0) == 0) {
                    for (n = 0; n < 4; n++) ocr[n] = xchg(0xFF);
                    ty = (ocr[0] & 0x40) ? (CT_SD2 | CT_BLOCK) : CT_SD2;
                }
            }
        } else {                                    /* SD v1 or MMC */
            BYTE cmd;
            if (send_cmd(ACMD41, 0) <= 1) { ty = CT_SD1; cmd = ACMD41; }
            else                          { ty = CT_MMC; cmd = CMD1; }
            while (HAL_GetTick() - t0 < 1000 && send_cmd(cmd, 0)) { }
            if (HAL_GetTick() - t0 >= 1000 || send_cmd(CMD16, 512) != 0) ty = 0;
        }
    }
    card_type = ty;
    deselect();
    DBG("sd: card type %02X (0C = SDHC, 0 = failed)\r\n", ty);

    if (ty) {
        set_speed(SD_FAST_PRESCALER);
        stat &= (DSTATUS)~STA_NOINIT;
    } else {
        stat = STA_NOINIT;
    }
    return stat;
}

DSTATUS SD_disk_status(BYTE pdrv)
{
    if (pdrv) return STA_NOINIT;
    return stat;
}

static DRESULT read_blocks(BYTE *buff, DWORD sector, UINT count)
{
    if (count == 1) {
        BYTE r = send_cmd(CMD17, sector);
        if (r == 0 && rcvr_datablock(buff, 512)) count = 0;
        else if (r) DBG("sd: CMD17 sector %lu -> %02X\r\n", (unsigned long)sector, r);
    } else {
        BYTE r = send_cmd(CMD18, sector);
        if (r == 0) {
            do {
                if (!rcvr_datablock(buff, 512)) break;
                buff += 512;
            } while (--count);
            send_cmd(CMD12, 0);
        } else {
            DBG("sd: CMD18 sector %lu -> %02X\r\n", (unsigned long)sector, r);
        }
    }
    deselect();
    return count ? RES_ERROR : RES_OK;
}

DRESULT SD_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv || !count) return RES_PARERR;
    if (!ensure_init()) return RES_NOTRDY;
    if (!(card_type & CT_BLOCK)) sector *= 512;     /* byte addressing on old cards */

    DRESULT r = read_blocks(buff, sector, count);
    if (r != RES_OK && cur_prescaler != SPI_BAUDRATEPRESCALER_256) {
        /* Reading failed: the wires may be too long for this speed. Go slower and retry. */
        uint32_t slower = (cur_prescaler == SD_FAST_PRESCALER) ? SPI_BAUDRATEPRESCALER_64
                                                               : SPI_BAUDRATEPRESCALER_256;
        DBG("sd: read failed, slowing SPI down and retrying\r\n");
        set_speed(slower);
        r = read_blocks(buff, sector, count);
        if (r != RES_OK && slower != SPI_BAUDRATEPRESCALER_256) {
            set_speed(SPI_BAUDRATEPRESCALER_256);
            r = read_blocks(buff, sector, count);
        }
        DBG("sd: retry %s\r\n", r == RES_OK ? "worked" : "failed too");
    }
    return r;
}

DRESULT SD_disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv || !count) return RES_PARERR;
    if (!ensure_init()) return RES_NOTRDY;
    if (!(card_type & CT_BLOCK)) sector *= 512;

    while (count) {                                 /* one block at a time: simple, writes are rare */
        if (send_cmd(CMD24, sector) != 0 || !xmit_datablock(buff, 0xFE)) break;
        buff += 512;
        sector += (card_type & CT_BLOCK) ? 1 : 512;
        count--;
    }
    deselect();
    return count ? RES_ERROR : RES_OK;
}

DRESULT SD_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    DRESULT res = RES_ERROR;
    BYTE csd[16];

    if (pdrv) return RES_PARERR;
    if (!ensure_init()) return RES_NOTRDY;

    switch (cmd) {
    case CTRL_SYNC:
        if (select()) res = RES_OK;
        break;
    case GET_SECTOR_COUNT:
        if (send_cmd(CMD9, 0) == 0 && rcvr_datablock(csd, 16)) {
            DWORD csize;
            if ((csd[0] >> 6) == 1) {               /* CSD v2 (SDHC/SDXC) */
                csize = csd[9] + ((DWORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
                *(DWORD *)buff = csize << 10;
            } else {                                /* CSD v1 */
                BYTE n = (BYTE)((csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2);
                csize = (csd[8] >> 6) + ((DWORD)csd[7] << 2) + ((DWORD)(csd[6] & 3) << 10) + 1;
                *(DWORD *)buff = csize << (n - 9);
            }
            res = RES_OK;
        }
        break;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        res = RES_OK;
        break;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        res = RES_OK;
        break;
    default:
        res = RES_PARERR;
        break;
    }
    deselect();
    return res;
}
