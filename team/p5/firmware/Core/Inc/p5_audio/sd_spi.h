#ifndef SD_SPI_H
#define SD_SPI_H

/* SD card over SPI3 (PC10 SCK, PC11 MISO, PC12 MOSI, PD2 = SD_CS) for FatFs.
 * Called from FATFS/Target/user_diskio.c (see the comments in sd_spi.c). */

#include "diskio.h"

#ifdef __cplusplus
extern "C" {
#endif

DSTATUS SD_disk_initialize(BYTE pdrv);
DSTATUS SD_disk_status(BYTE pdrv);
DRESULT SD_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
DRESULT SD_disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
DRESULT SD_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);

#ifdef __cplusplus
}
#endif

#endif /* SD_SPI_H */
