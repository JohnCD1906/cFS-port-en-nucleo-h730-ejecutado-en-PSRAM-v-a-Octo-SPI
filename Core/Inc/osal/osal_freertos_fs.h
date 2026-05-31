/**
 * @file  osal_freertos_fs.h
 * @brief Extension OSAL — API de Filesystem sobre FatFS
 *
 * Añade al OSAL existente las funciones de acceso a ficheros que
 * cFE necesita para leer startup.scr, cargar/guardar tablas .tbl
 * y escribir logs de EVS.
 *
 * Mapeo OSAL FS -> FatFS:
 *   OS_FS_Init()      -> inicializa tabla FDs (sin tocar disco)
 *   OS_FS_Mount()     -> f_mkfs (opcional) + f_mount
 *   OS_open()         -> f_open
 *   OS_close()        -> f_close
 *   OS_read()         -> f_read
 *   OS_write()        -> f_write
 *   OS_lseek()        -> f_lseek
 *   OS_mkdir()        -> f_mkdir
 *   OS_stat()         -> f_stat
 *   OS_remove()       -> f_unlink
 *   OS_FS_WriteFile() -> helper: open + write + close
 */
#ifndef OSAL_FREERTOS_FS_H
#define OSAL_FREERTOS_FS_H

#include "osal/osal_freertos.h"
#include "ff.h"          /* FatFS — generado por CubeMX */

#ifdef __cplusplus
extern "C" {
#endif

/* Maximo de ficheros abiertos simultaneamente */
#define OS_MAX_FDS          8

/* Maxima longitud de path */
#define OS_MAX_PATH_LEN     64

/* Flags para OS_open — compatibles con cFE */
#define OS_READ_ONLY        0x0001
#define OS_WRITE_ONLY       0x0002
#define OS_READ_WRITE       0x0003
#define OS_CREATE           0x0004
#define OS_TRUNC            0x0008

/* Valores de whence para OS_lseek */
#define OS_SEEK_SET         0
#define OS_SEEK_CUR         1
#define OS_SEEK_END         2

/* Codigos de error especificos del filesystem */
#define OS_FS_SUCCESS               OS_SUCCESS
#define OS_FS_ERROR                 OS_ERROR
#define OS_FS_ERR_INVALID_FD        (-20)
#define OS_FS_ERR_PATH_TOO_LONG     (-21)
#define OS_FS_ERR_NAME_TOO_LONG     (-22)
#define OS_FS_ERR_PATH_INVALID      (-23)
#define OS_FS_ERR_NO_FREE_FDS       (-24)

/* Entrada en la tabla de file descriptors */
typedef struct {
    FIL     fatfs_fil;
    char    path[OS_MAX_PATH_LEN];
    uint32  flags;
    uint8   in_use;
} OS_FDTableEntry_t;

/* Informacion de fichero (equivalente a stat) */
typedef struct {
    uint32  size;
    uint32  is_dir;
} OS_FSStat_t;

/* Prototipos */
int32 OS_FS_Init(void);
int32 OS_FS_Mount(const char *path, int format);

int32 OS_open(const char *path, int32 access, uint32 mode);
int32 OS_close(int32 fd);
int32 OS_read(int32 fd, void *buffer, uint32 nbytes);
int32 OS_write(int32 fd, const void *buffer, uint32 nbytes);
int32 OS_lseek(int32 fd, int32 offset, uint32 whence);

int32 OS_mkdir(const char *path, uint32 access);
int32 OS_stat(const char *path, OS_FSStat_t *stat_buf);
int32 OS_remove(const char *path);

int32 OS_FS_WriteFile(const char *path, const uint8 *data, uint32 length);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_FREERTOS_FS_H */
