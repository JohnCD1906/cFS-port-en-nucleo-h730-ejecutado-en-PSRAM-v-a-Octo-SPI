/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * @file    user_diskio.c
  * @brief   This file includes a diskio driver skeleton to be completed by the user.
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

#ifdef USE_OBSOLETE_USER_CODE_SECTION_0
/*
 * Warning: the user section 0 is no more in use (starting from CubeMx version 4.16.0)
 * To be suppressed in the future.
 * Kept to ensure backward compatibility with previous CubeMx versions when
 * migrating projects.
 * User code previously added there should be copied in the new user sections before
 * the section contents can be deleted.
 */
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */
#endif

/* USER CODE BEGIN DECL */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "ff_gen_drv.h"

/* RAM disk en DTCM del STM32H730 — direccion fija por arquitectura.
 * IMPORTANTE: estas constantes deben coincidir con PSP_RAMDISK_ADDR
 * y PSP_RAMDISK_SIZE en Core/Inc/psp/psp_stm32h730.h                       */
#define RAMDISK_BASE_ADDR   0x20000000UL   /* DTCM */
#define RAMDISK_SIZE_BYTES  (64u * 1024u)  /* 64 KB */
#define RAMDISK_SECTOR_SIZE 512u
#define RAMDISK_SECTOR_CNT  (RAMDISK_SIZE_BYTES / RAMDISK_SECTOR_SIZE)  /* 128 */

/* Puntero al inicio del RAM disk en DTCM */
static volatile uint8_t * const s_ramdisk = (uint8_t *)RAMDISK_BASE_ADDR;

/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;

/* USER CODE END DECL */

/* Private function prototypes -----------------------------------------------*/
DSTATUS USER_initialize (BYTE pdrv);
DSTATUS USER_status (BYTE pdrv);
DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
  DRESULT USER_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  DRESULT USER_ioctl (BYTE pdrv, BYTE cmd, void *buff);
#endif /* _USE_IOCTL == 1 */

Diskio_drvTypeDef  USER_Driver =
{
  USER_initialize,
  USER_status,
  USER_read,
#if  _USE_WRITE
  USER_write,
#endif  /* _USE_WRITE == 1 */
#if  _USE_IOCTL == 1
  USER_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_initialize (
	BYTE pdrv           /* Physical drive nmuber to identify the drive */
)
{
	/* USER CODE BEGIN INIT */
	    /* DTCM siempre esta presente y lista. No requiere init de HW.
	     * Simplemente marcamos disk listo.                                       */
	    (void)pdrv;
	    Stat = 0;   /* 0 = sin flags de error, disco listo */
	    return Stat;
	  /* USER CODE END INIT */
}

/**
  * @brief  Gets Disk Status
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_status (
	BYTE pdrv       /* Physical drive number to identify the drive */
)
{
	/* USER CODE BEGIN STATUS */
	    (void)pdrv;
	    return Stat;
	  /* USER CODE END STATUS */
}

/**
  * @brief  Reads Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT USER_read (
	BYTE pdrv,      /* Physical drive nmuber to identify the drive */
	BYTE *buff,     /* Data buffer to store read data */
	DWORD sector,   /* Sector address in LBA */
	UINT count      /* Number of sectors to read */
)
{
	/* USER CODE BEGIN READ */
	    (void)pdrv;

	    if (Stat & STA_NOINIT) return RES_NOTRDY;
	    if (buff == NULL)      return RES_PARERR;
	    if (sector + count > RAMDISK_SECTOR_CNT) return RES_PARERR;

	    memcpy((void *)buff,
	           (const void *)(s_ramdisk + (sector * RAMDISK_SECTOR_SIZE)),
	           count * RAMDISK_SECTOR_SIZE);
	    return RES_OK;
	  /* USER CODE END READ */
}

/**
  * @brief  Writes Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT USER_write (
	BYTE pdrv,          /* Physical drive nmuber to identify the drive */
	const BYTE *buff,   /* Data to be written */
	DWORD sector,       /* Sector address in LBA */
	UINT count          /* Number of sectors to write */
)
{
	/* USER CODE BEGIN WRITE */
	    (void)pdrv;

	    if (Stat & STA_NOINIT) return RES_NOTRDY;
	    if (buff == NULL)      return RES_PARERR;
	    if (sector + count > RAMDISK_SECTOR_CNT) return RES_PARERR;

	    memcpy((void *)(s_ramdisk + (sector * RAMDISK_SECTOR_SIZE)),
	           (const void *)buff,
	           count * RAMDISK_SECTOR_SIZE);
	    return RES_OK;
	  /* USER CODE END WRITE */
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT USER_ioctl (
	BYTE pdrv,      /* Physical drive nmuber (0..) */
	BYTE cmd,       /* Control code */
	void *buff      /* Buffer to send/receive control data */
)
{
	/* USER CODE BEGIN IOCTL */
	    (void)pdrv;
	    DRESULT res = RES_ERROR;

	    if (Stat & STA_NOINIT) return RES_NOTRDY;

	    switch (cmd)
	    {
	        case CTRL_SYNC:
	            /* DTCM es RAM directa, no hay cache que flushear */
	            res = RES_OK;
	            break;

	        case GET_SECTOR_COUNT:
	            *(DWORD *)buff = RAMDISK_SECTOR_CNT;
	            res = RES_OK;
	            break;

	        case GET_SECTOR_SIZE:
	            *(WORD *)buff = RAMDISK_SECTOR_SIZE;
	            res = RES_OK;
	            break;

	        case GET_BLOCK_SIZE:
	            /* tamaño minimo de borrado en bloques de sector */
	            *(DWORD *)buff = 1;
	            res = RES_OK;
	            break;

	        default:
	            res = RES_PARERR;
	            break;
	    }

	    return res;
	  /* USER CODE END IOCTL */
}
#endif /* _USE_IOCTL == 1 */

