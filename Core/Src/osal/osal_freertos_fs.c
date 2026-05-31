/**
 * @file  osal_freertos_fs.c
 * @brief Implementacion OSAL Filesystem sobre FatFS
 *
 * Cada funcion OSAL FS hace dos cosas:
 *   1. Gestiona la tabla interna de FDs (OS_FDTable)
 *   2. Llama a FatFS
 *
 * Path translation:
 *   OSAL  usa paths Unix-style:  "/cf/startup.scr"
 *   FatFS usa paths DOS-style:   "0:/cf/startup.scr"
 *   translate_path() añade el prefijo "0:".
 */

#include "osal/osal_freertos_fs.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>

/* Tabla de file descriptors abiertos */
static OS_FDTableEntry_t OS_FDTable[OS_MAX_FDS];

/* Objeto de sistema de ficheros FatFS (un objeto por volumen) */
static FATFS s_fatfs_obj;

/* Flag: ¿esta el volumen montado? */
static uint8 s_fs_mounted = 0;

/* ─────────────────────────────────────────────────────────────────
 * TRADUCCION DE PATH: "/cf/foo" -> "0:/cf/foo"
 * ───────────────────────────────────────────────────────────────── */
static int32 translate_path(const char *osal_path,
                            char *fatfs_path, uint32 max_len)
{
    if (osal_path == NULL || fatfs_path == NULL)
        return OS_INVALID_POINTER;

    if ((strlen(osal_path) + 2u) >= max_len)
        return OS_FS_ERR_PATH_TOO_LONG;

    fatfs_path[0] = '0';
    fatfs_path[1] = ':';
    fatfs_path[2] = '\0';
    strncat(fatfs_path, osal_path, max_len - 3u);

    return OS_SUCCESS;
}

/* ═════════════════════════════════════════════════════════════════
 * OS_FS_Init
 * ═════════════════════════════════════════════════════════════════ */
int32 OS_FS_Init(void)
{
    memset(OS_FDTable, 0, sizeof(OS_FDTable));
    s_fs_mounted = 0;
    OS_printf("OSAL_FS: Tablas inicializadas (max_fds=%d)\n", OS_MAX_FDS);
    return OS_SUCCESS;
}

/* ═════════════════════════════════════════════════════════════════
 * OS_FS_Mount — montar el RAM disk y opcionalmente formatear
 * ═════════════════════════════════════════════════════════════════ */
int32 OS_FS_Mount(const char *path, int format)
{
    (void)path;
    FRESULT fr;

    if (format)
        {
            uint8_t work_buf[1024];

            /* API vieja (FatFS R0.12c) — firma:
             * f_mkfs(path, opt, au, work, len)                                   */
            fr = f_mkfs("0:", FM_FAT | FM_SFD, 0, work_buf, sizeof(work_buf));

        if (fr != FR_OK)
        {
            OS_printf("OSAL_FS ERROR: f_mkfs fallo (fr=%d)\n", (int)fr);
            return OS_ERROR;
        }
        OS_printf("OSAL_FS: RAM disk formateado (FAT12)\n");
    }

    /* f_mount registra el objeto FATFS para el volumen "0:".
     * Segundo parametro: 1 = mount inmediato (detecta errores aqui).    */
    fr = f_mount(&s_fatfs_obj, "0:", 1);
    if (fr != FR_OK)
    {
        OS_printf("OSAL_FS ERROR: f_mount fallo (fr=%d)\n", (int)fr);
        return OS_ERROR;
    }

    s_fs_mounted = 1;
    OS_printf("OSAL_FS: Volumen '0:/' montado correctamente\n");
    return OS_SUCCESS;
}

/* ═════════════════════════════════════════════════════════════════
 * OS_open
 * ═════════════════════════════════════════════════════════════════ */
int32 OS_open(const char *path, int32 access, uint32 mode)
{
    (void)mode;

    if (path == NULL)         return OS_INVALID_POINTER;
    if (!s_fs_mounted)        return OS_ERROR;
    if (strlen(path) >= OS_MAX_PATH_LEN) return OS_FS_ERR_PATH_TOO_LONG;

    /* Buscar un FD libre */
    int slot = -1;
    for (int i = 0; i < OS_MAX_FDS; i++)
    {
        if (!OS_FDTable[i].in_use) { slot = i; break; }
    }
    if (slot < 0)
    {
        OS_printf("OSAL_FS ERROR: no hay FDs libres\n");
        return OS_FS_ERR_NO_FREE_FDS;
    }

    char fatfs_path[OS_MAX_PATH_LEN + 2];
    if (translate_path(path, fatfs_path, sizeof(fatfs_path)) != OS_SUCCESS)
        return OS_FS_ERR_PATH_TOO_LONG;

    BYTE fatfs_flags = 0;
    switch (access & 0x03)
    {
        case OS_READ_ONLY:
            fatfs_flags = FA_READ | FA_OPEN_EXISTING;
            break;
        case OS_WRITE_ONLY:
            fatfs_flags = FA_WRITE | FA_CREATE_ALWAYS;
            break;
        case OS_READ_WRITE:
            fatfs_flags = FA_READ | FA_WRITE | FA_OPEN_ALWAYS;
            break;
        default:
            return OS_ERROR;
    }

    FRESULT fr = f_open(&OS_FDTable[slot].fatfs_fil, fatfs_path, fatfs_flags);
    if (fr != FR_OK)
    {
        OS_printf("OSAL_FS ERROR: f_open('%s') fallo (fr=%d)\n",
                  fatfs_path, (int)fr);
        return OS_ERROR;
    }

    strncpy(OS_FDTable[slot].path, path, OS_MAX_PATH_LEN - 1);
    OS_FDTable[slot].flags  = (uint32)access;
    OS_FDTable[slot].in_use = 1;

    return (int32)slot;
}

/* ═════════════════════════════════════════════════════════════════
 * OS_close
 * ═════════════════════════════════════════════════════════════════ */
int32 OS_close(int32 fd)
{
    if (fd < 0 || fd >= OS_MAX_FDS || !OS_FDTable[fd].in_use)
        return OS_FS_ERR_INVALID_FD;

    FRESULT fr = f_close(&OS_FDTable[fd].fatfs_fil);
    memset(&OS_FDTable[fd], 0, sizeof(OS_FDTableEntry_t));

    return (fr == FR_OK) ? OS_SUCCESS : OS_ERROR;
}

/* ═════════════════════════════════════════════════════════════════
 * OS_read
 * ═════════════════════════════════════════════════════════════════ */
int32 OS_read(int32 fd, void *buffer, uint32 nbytes)
{
    if (fd < 0 || fd >= OS_MAX_FDS || !OS_FDTable[fd].in_use)
        return OS_FS_ERR_INVALID_FD;
    if (buffer == NULL)
        return OS_INVALID_POINTER;

    UINT bytes_read = 0;
    FRESULT fr = f_read(&OS_FDTable[fd].fatfs_fil, buffer,
                         (UINT)nbytes, &bytes_read);
    if (fr != FR_OK)
        return OS_ERROR;

    return (int32)bytes_read;
}

/* ═════════════════════════════════════════════════════════════════
 * OS_write
 * ═════════════════════════════════════════════════════════════════ */
int32 OS_write(int32 fd, const void *buffer, uint32 nbytes)
{
    if (fd < 0 || fd >= OS_MAX_FDS || !OS_FDTable[fd].in_use)
        return OS_FS_ERR_INVALID_FD;
    if (buffer == NULL)
        return OS_INVALID_POINTER;

    UINT bytes_written = 0;
    FRESULT fr = f_write(&OS_FDTable[fd].fatfs_fil, buffer,
                          (UINT)nbytes, &bytes_written);
    if (fr != FR_OK)
        return OS_ERROR;

    return (int32)bytes_written;
}

/* ═════════════════════════════════════════════════════════════════
 * OS_lseek
 * ═════════════════════════════════════════════════════════════════ */
int32 OS_lseek(int32 fd, int32 offset, uint32 whence)
{
    if (fd < 0 || fd >= OS_MAX_FDS || !OS_FDTable[fd].in_use)
        return OS_FS_ERR_INVALID_FD;

    FSIZE_t new_pos;
    switch (whence)
    {
        case OS_SEEK_SET:
            new_pos = (FSIZE_t)offset;
            break;
        case OS_SEEK_CUR:
            new_pos = f_tell(&OS_FDTable[fd].fatfs_fil) + (FSIZE_t)offset;
            break;
        case OS_SEEK_END:
            new_pos = f_size(&OS_FDTable[fd].fatfs_fil) + (FSIZE_t)offset;
            break;
        default:
            return OS_ERROR;
    }

    FRESULT fr = f_lseek(&OS_FDTable[fd].fatfs_fil, new_pos);
    if (fr != FR_OK)
        return OS_ERROR;

    return (int32)new_pos;
}

/* ═════════════════════════════════════════════════════════════════
 * OS_mkdir
 * ═════════════════════════════════════════════════════════════════ */
int32 OS_mkdir(const char *path, uint32 access)
{
    (void)access;

    if (path == NULL)  return OS_INVALID_POINTER;
    if (!s_fs_mounted) return OS_ERROR;

    char fatfs_path[OS_MAX_PATH_LEN + 2];
    if (translate_path(path, fatfs_path, sizeof(fatfs_path)) != OS_SUCCESS)
        return OS_FS_ERR_PATH_TOO_LONG;

    FRESULT fr = f_mkdir(fatfs_path);
    if (fr != FR_OK && fr != FR_EXIST)
    {
        OS_printf("OSAL_FS ERROR: f_mkdir('%s') fallo (fr=%d)\n",
                  fatfs_path, (int)fr);
        return OS_ERROR;
    }

    return OS_SUCCESS;
}

/* ═════════════════════════════════════════════════════════════════
 * OS_stat
 * ═════════════════════════════════════════════════════════════════ */
int32 OS_stat(const char *path, OS_FSStat_t *stat_buf)
{
    if (path == NULL || stat_buf == NULL) return OS_INVALID_POINTER;
    if (!s_fs_mounted)                    return OS_ERROR;

    char fatfs_path[OS_MAX_PATH_LEN + 2];
    if (translate_path(path, fatfs_path, sizeof(fatfs_path)) != OS_SUCCESS)
        return OS_FS_ERR_PATH_TOO_LONG;

    FILINFO fno;
    FRESULT fr = f_stat(fatfs_path, &fno);
    if (fr != FR_OK)
        return OS_ERROR;

    stat_buf->size   = (uint32)fno.fsize;
    stat_buf->is_dir = (fno.fattrib & AM_DIR) ? 1u : 0u;

    return OS_SUCCESS;
}

/* ═════════════════════════════════════════════════════════════════
 * OS_remove
 * ═════════════════════════════════════════════════════════════════ */
int32 OS_remove(const char *path)
{
    if (path == NULL)  return OS_INVALID_POINTER;
    if (!s_fs_mounted) return OS_ERROR;

    char fatfs_path[OS_MAX_PATH_LEN + 2];
    if (translate_path(path, fatfs_path, sizeof(fatfs_path)) != OS_SUCCESS)
        return OS_FS_ERR_PATH_TOO_LONG;

    FRESULT fr = f_unlink(fatfs_path);
    return (fr == FR_OK) ? OS_SUCCESS : OS_ERROR;
}

/* ═════════════════════════════════════════════════════════════════
 * OS_FS_WriteFile — helper PSP
 * Crea o sobreescribe un fichero en una sola operacion.
 * ═════════════════════════════════════════════════════════════════ */
int32 OS_FS_WriteFile(const char *path, const uint8 *data, uint32 length)
{
    int32 fd = OS_open(path, OS_WRITE_ONLY, 0);
    if (fd < 0)
    {
        OS_printf("OSAL_FS ERROR: WriteFile('%s') open fallo\n", path);
        return OS_ERROR;
    }

    int32 written = OS_write(fd, data, length);
    OS_close(fd);

    if (written < 0 || (uint32)written != length)
    {
        OS_printf("OSAL_FS ERROR: WriteFile('%s') write incompleto (%ld/%lu)\n",
                  path, (long)written, (unsigned long)length);
        return OS_ERROR;
    }

    return OS_SUCCESS;
}
