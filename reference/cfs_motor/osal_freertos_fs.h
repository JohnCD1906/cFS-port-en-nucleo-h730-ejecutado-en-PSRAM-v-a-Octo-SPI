/**

- @file  osal_freertos_fs.h
- @brief Extensión OSAL — API de Filesystem sobre FatFS
- 
- Añade al OSAL existente las funciones de acceso a ficheros que
- cFE necesita para leer startup.scr, cargar/guardar tablas .tbl
- y escribir logs de EVS.
- 
- ── Mapeo OSAL FS → FatFS ───────────────────────────────────────
- 
- OS_FS_Init()     → f_mount() sobre RAM disk
- OS_open()        → f_open()
- OS_close()       → f_close()
- OS_read()        → f_read()
- OS_write()       → f_write()
- OS_lseek()       → f_lseek()
- OS_mkdir()       → f_mkdir()
- OS_stat()        → f_stat()
- OS_FS_WriteFile()→ f_open + f_write + f_close (helper PSP)
- OS_FS_Mount()    → f_mkfs + f_mount
- 
- ── Flags de OS_open ────────────────────────────────────────────
- 
- OS_READ_ONLY     → FA_READ
- OS_WRITE_ONLY    → FA_WRITE | FA_CREATE_ALWAYS
- OS_READ_WRITE    → FA_READ | FA_WRITE | FA_OPEN_ALWAYS
- 
- ── Tabla de file descriptors ───────────────────────────────────
- 
- OSAL expone file descriptors como enteros (int32).
- Internamente hay una tabla OS_FDTable[OS_MAX_FDS] que
- mapea cada fd a un FIL de FatFS.
- 
- @note  Este módulo usa FatFS (ff.h / ff.c) que CubeMX genera
- ```
     bajo Middlewares/Third_Party/FatFs/.
  ```

*/

#ifndef OSAL_FREERTOS_FS_H
#define OSAL_FREERTOS_FS_H

#include "osal_freertos.h"
#include "ff.h"          /* FatFS — generado por CubeMX */

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════

- CONSTANTES
- ══════════════════════════════════════════════════════════════════ */

/** Máximo de ficheros abiertos simultáneamente */
#define OS_MAX_FDS          8

/** Máxima longitud de path */
#define OS_MAX_PATH_LEN     64

/* Flags para OS_open — compatibles con cFE */
#define OS_READ_ONLY        0x0001
#define OS_WRITE_ONLY       0x0002
#define OS_READ_WRITE       0x0003
#define OS_CREATE           0x0004   /**< crear si no existe */
#define OS_TRUNC            0x0008   /**< truncar al abrir   */

/* Valores de whence para OS_lseek */
#define OS_SEEK_SET         0
#define OS_SEEK_CUR         1
#define OS_SEEK_END         2

/* Códigos de error específicos del filesystem */
#define OS_FS_SUCCESS       OS_SUCCESS
#define OS_FS_ERROR         OS_ERROR
#define OS_FS_ERR_INVALID_FD    (-20)
#define OS_FS_ERR_PATH_TOO_LONG (-21)
#define OS_FS_ERR_NAME_TOO_LONG (-22)
#define OS_FS_ERR_PATH_INVALID  (-23)
#define OS_FS_ERR_NO_FREE_FDS   (-24)

/* ══════════════════════════════════════════════════════════════════

- TIPOS
- ══════════════════════════════════════════════════════════════════ */

/** Entrada en la tabla de file descriptors */
typedef struct {
FIL     fatfs_fil;              /**< FIL de FatFS (objeto de fichero) */
char    path[OS_MAX_PATH_LEN];  /**< path con el que se abrió        */
uint32  flags;                  /**< OS_READ_ONLY / OS_WRITE_ONLY… */
uint8   in_use;                 /**< 1 si este fd está en uso        */
} OS_FDTableEntry_t;

/** Información de fichero (equivalente a stat) */
typedef struct {
uint32  size;                   /**< tamaño en bytes              */
uint32  is_dir;                 /**< 1 si es directorio           */
} OS_FSStat_t;

/* ══════════════════════════════════════════════════════════════════

- PROTOTIPOS
- ══════════════════════════════════════════════════════════════════ */

/* ── Inicialización ─────────────────────────────────────────────── */

/**

- @brief Inicializa las tablas internas de OSAL FS.
- ```
     Llamar antes de OS_FS_Mount().
  ```

*/
int32 OS_FS_Init(void);

/**

- @brief Monta el volumen FAT sobre el RAM disk.
- @param path   punto de montaje: “0:/”
- @param format 1 = formatear primero (f_mkfs), 0 = solo montar
- @return OS_SUCCESS o OS_ERROR
  */
  int32 OS_FS_Mount(const char *path, int format);

/* ── Operaciones sobre ficheros ─────────────────────────────────── */

/**

- @brief Abre (o crea) un fichero.
- @param path   path del fichero, ej: “/cf/cfe_es_startup.scr”
- @param access OS_READ_ONLY, OS_WRITE_ONLY o OS_READ_WRITE
- @param mode   ignorado (compatibilidad cFS), pasar 0
- @return file descriptor (≥0) o código de error negativo
  */
  int32 OS_open(const char *path, int32 access, uint32 mode);

/**

- @brief Cierra un file descriptor.
  */
  int32 OS_close(int32 fd);

/**

- @brief Lee hasta nbytes del fichero en buffer.
- @return bytes leídos, 0 en EOF, negativo en error
  */
  int32 OS_read(int32 fd, void *buffer, uint32 nbytes);

/**

- @brief Escribe nbytes del buffer al fichero.
- @return bytes escritos o código de error negativo
  */
  int32 OS_write(int32 fd, const void *buffer, uint32 nbytes);

/**

- @brief Mueve el puntero de posición del fichero.
- @param whence OS_SEEK_SET, OS_SEEK_CUR o OS_SEEK_END
- @return nueva posición o código de error negativo
  */
  int32 OS_lseek(int32 fd, int32 offset, uint32 whence);

/* ── Operaciones sobre directorios/paths ───────────────────────── */

/**

- @brief Crea un directorio.
- @param path   path del directorio, ej: “/cf”
- @param access ignorado, pasar 0
  */
  int32 OS_mkdir(const char *path, uint32 access);

/**

- @brief Obtiene información de un fichero o directorio.
  */
  int32 OS_stat(const char *path, OS_FSStat_t *stat_buf);

/**

- @brief Elimina un fichero.
  */
  int32 OS_remove(const char *path);

/* ── Helper del PSP ─────────────────────────────────────────────── */

/**

- @brief Crea o sobreescribe un fichero con el contenido dado.
- ```
     Usado por PSP_FS_Init() para poblar el RAM disk.
  ```
- @param path    path destino, ej: “/cf/cfe_es_startup.scr”
- @param data    puntero al contenido (normalmente en Flash)
- @param length  número de bytes a escribir
- @return OS_SUCCESS o código de error
  */
  int32 OS_FS_WriteFile(const char *path, const uint8 *data, uint32 length);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_FREERTOS_FS_H */
