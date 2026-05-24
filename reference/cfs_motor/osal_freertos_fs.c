/**

- @file  osal_freertos_fs.c
- @brief Implementación OSAL Filesystem → FatFS para STM32F407
- 
- Cada función OSAL FS hace dos cosas:
- 1. Gestiona la tabla interna de FDs (OS_FDTable)
- 1. Llama a FatFS — marcado con comentario [FATFS]
- 
- ── Por qué una tabla de FDs y no usar FIL directamente ─────────
- 
- cFE y las apps usan file descriptors como enteros (int32).
- FatFS usa objetos FIL (structs de ~44 bytes cada uno).
- La tabla OS_FDTable[] es el puente entre ambos mundos,
- igual que OS_task_table[] y OS_queue_table[] en osal_freertos.c.
- 
- ── Path translation ────────────────────────────────────────────
- 
- OSAL usa paths Unix-style:  “/cf/startup.scr”
- FatFS usa paths DOS-style:  “0:/cf/startup.scr”
- 
- psp_translate_path() añade el prefijo “0:” al path OSAL.
  */

#include "osal_freertos_fs.h"
#include "ff.h"      /* [FATFS] FatFS API */
#include <string.h>
#include <stdio.h>

/* ══════════════════════════════════════════════════════════════════

- TABLAS INTERNAS
- ══════════════════════════════════════════════════════════════════ */

/** Tabla de file descriptors abiertos */
static OS_FDTableEntry_t OS_FDTable[OS_MAX_FDS];

/** Objeto de sistema de ficheros FatFS (un objeto por volumen) */
static FATFS s_fatfs_obj;  /* [FATFS] FATFS = objeto de volumen montado */

/** Flag: ¿está el volumen montado? */
static uint8 s_fs_mounted = 0;

/* ══════════════════════════════════════════════════════════════════

- TRADUCCIÓN DE PATH
- 
- OSAL:  “/cf/cfe_es_startup.scr”
- FatFS: “0:/cf/cfe_es_startup.scr”
- ══════════════════════════════════════════════════════════════════ */
  static int32 translate_path(const char *osal_path,
  char       *fatfs_path,
  uint32      max_len)
  {
  if (osal_path == NULL || fatfs_path == NULL)
  return OS_INVALID_POINTER;
  
  /* El prefijo “0:” ocupa 2 caracteres extra */
  if ((strlen(osal_path) + 2u) >= max_len)
  return OS_FS_ERR_PATH_TOO_LONG;
  
  /* Construir “0:” + osal_path */
  fatfs_path[0] = '0';
  fatfs_path[1] = ':';
  fatfs_path[2] = '\0';
  strncat(fatfs_path, osal_path, max_len - 3u);
  
  return OS_SUCCESS;
  }

/* ══════════════════════════════════════════════════════════════════

- OS_FS_Init
- ══════════════════════════════════════════════════════════════════ */
  int32 OS_FS_Init(void)
  {
  memset(OS_FDTable, 0, sizeof(OS_FDTable));
  s_fs_mounted = 0;
  
  OS_printf("OSAL_FS: Tablas inicializadas (max_fds=%d)\n", OS_MAX_FDS);
  return OS_SUCCESS;
  }

/* ══════════════════════════════════════════════════════════════════

- OS_FS_Mount — montar el RAM disk y opcionalmente formatear
- ══════════════════════════════════════════════════════════════════ */
  FRESULT fr;
  int32 OS_FS_Mount(const char *path, int format)
  {
  (void)path; /* Solo usamos el volumen “0:” */
  
  
  if (format)
  {
  /*
  * [FATFS] f_mkfs formatea el volumen.
  *
  * MKFS_PARM controla el tipo de FAT:
  *   FM_FAT   → FAT12/FAT16 según tamaño del disco
  *   FM_ANY   → FatFS elige automáticamente
  *
  * Con 24 KB de RAM disk → FAT12 automáticamente.
  *
  * work_buf: buffer temporal durante el formateo.
  * 512 bytes es el mínimo para FAT12.
  */
  uint8_t work_buf[1024];
  //MKFS_PARM mkfs_params = { FM_FAT, 0, 0, 0, 512 }; /* [FATFS] */
  fr = f_mkfs("0:", 0x09, 0, work_buf, sizeof(work_buf));
   //fr = f_mkfs("0:", &mkfs_params, work_buf, sizeof(work_buf)); /* [FATFS] */
   if (fr != FR_OK)  /* [FATFS] FR_OK = 0 */
   {
       OS_printf("OSAL_FS ERROR: f_mkfs falló (fr=%d)\n", (int)fr);
       return OS_ERROR;
   }
   OS_printf("OSAL_FS: RAM disk formateado (FAT12, 24 KB)\n");

  }
  
  /*
  - [FATFS] f_mount registra el objeto de filesystem para el volumen “0:”.
  - Segundo parámetro: 0 = mount lazy (primer acceso), 1 = mount inmediato.
  - Usamos 1 para detectar errores aquí y no más tarde.
    */
    fr = f_mount(&s_fatfs_obj, "0:", 1); /* [FATFS] */
    if (fr != FR_OK)
    {
    OS_printf("OSAL_FS ERROR: f_mount falló (fr=%d)\n", (int)fr);
    return OS_ERROR;
    }
  
  s_fs_mounted = 1;
  OS_printf("OSAL_FS: Volumen '0:/' montado correctamente\n");
  return OS_SUCCESS;
  }

/* ══════════════════════════════════════════════════════════════════

- OS_open
- ══════════════════════════════════════════════════════════════════ */
  int32 OS_open(const char *path, int32 access, uint32 mode)
  {
  (void)mode;  /* ignorado en esta implementación */
  
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
  
  /* Traducir path OSAL → FatFS */
  char fatfs_path[OS_MAX_PATH_LEN + 2];
  if (translate_path(path, fatfs_path, sizeof(fatfs_path)) != OS_SUCCESS)
  return OS_FS_ERR_PATH_TOO_LONG;
  
  /*
  - Traducir flags OSAL → flags FatFS
  - 
  - [FATFS] Flags de apertura:
  - FA_READ          = leer
  - FA_WRITE         = escribir
  - FA_OPEN_EXISTING = error si no existe (default)
  - FA_CREATE_ALWAYS = crear, truncar si existe
  - FA_OPEN_ALWAYS   = abrir si existe, crear si no
    */
    BYTE fatfs_flags = 0; /* [FATFS] BYTE */
  
  switch (access & 0x03)
  {
  case OS_READ_ONLY:
  fatfs_flags = FA_READ | FA_OPEN_EXISTING; /* [FATFS] */
  break;
  case OS_WRITE_ONLY:
  fatfs_flags = FA_WRITE | FA_CREATE_ALWAYS; /* [FATFS] */
  break;
  case OS_READ_WRITE:
  fatfs_flags = FA_READ | FA_WRITE | FA_OPEN_ALWAYS; /* [FATFS] */
  break;
  default:
  return OS_ERROR;
  }
  
  /*
  - [FATFS] f_open abre el fichero y rellena el objeto FIL.
  - El FIL contiene el puntero de posición, buffers internos, etc.
  - Un FIL activo representa un fichero abierto — igual que FILE* en C.
    */
    FRESULT fr = f_open(&OS_FDTable[slot].fatfs_fil,  /* [FATFS] */
    fatfs_path,
    fatfs_flags);
    if (fr != FR_OK)
    {
    OS_printf("OSAL_FS ERROR: f_open('%s') falló (fr=%d)\n",
    fatfs_path, (int)fr);
    return OS_ERROR;
    }
  
  /* Registrar en la tabla */
  strncpy(OS_FDTable[slot].path, path, OS_MAX_PATH_LEN - 1);
  OS_FDTable[slot].flags  = (uint32)access;
  OS_FDTable[slot].in_use = 1;
  
  return (int32)slot;  /* el FD es el índice en la tabla */
  }

/* ══════════════════════════════════════════════════════════════════

- OS_close
- ══════════════════════════════════════════════════════════════════ */
  int32 OS_close(int32 fd)
  {
  if (fd < 0 || fd >= OS_MAX_FDS || !OS_FDTable[fd].in_use)
  return OS_FS_ERR_INVALID_FD;
  
  /*
  - [FATFS] f_close sincroniza y cierra el fichero.
  - IMPORTANTE: sin f_close los datos en caché no se escriben al disco.
    */
    FRESULT fr = f_close(&OS_FDTable[fd].fatfs_fil); /* [FATFS] */
  
  memset(&OS_FDTable[fd], 0, sizeof(OS_FDTableEntry_t));
  
  return (fr == FR_OK) ? OS_SUCCESS : OS_ERROR;
  }

/* ══════════════════════════════════════════════════════════════════

- OS_read
- ══════════════════════════════════════════════════════════════════ */
  int32 OS_read(int32 fd, void *buffer, uint32 nbytes)
  {
  if (fd < 0 || fd >= OS_MAX_FDS || !OS_FDTable[fd].in_use)
  return OS_FS_ERR_INVALID_FD;
  if (buffer == NULL)
  return OS_INVALID_POINTER;
  
  UINT bytes_read = 0;
  
  /*
  - [FATFS] f_read lee hasta nbytes en buffer.
  - bytes_read puede ser < nbytes si alcanzó EOF.
  - Retorna FR_OK incluso en EOF — hay que mirar bytes_read.
    */
    FRESULT fr = f_read(&OS_FDTable[fd].fatfs_fil, /* [FATFS] */
    buffer,
    (UINT)nbytes,
    &bytes_read);
  
  if (fr != FR_OK)
  return OS_ERROR;
  
  return (int32)bytes_read;  /* 0 = EOF, >0 = bytes leídos */
  }

/* ══════════════════════════════════════════════════════════════════

- OS_write
- ══════════════════════════════════════════════════════════════════ */
  int32 OS_write(int32 fd, const void *buffer, uint32 nbytes)
  {
  if (fd < 0 || fd >= OS_MAX_FDS || !OS_FDTable[fd].in_use)
  return OS_FS_ERR_INVALID_FD;
  if (buffer == NULL)
  return OS_INVALID_POINTER;
  
  UINT bytes_written = 0;
  
  /*
  - [FATFS] f_write escribe nbytes desde buffer al fichero.
  - Con FA_CREATE_ALWAYS el puntero empieza en 0.
    */
    FRESULT fr = f_write(&OS_FDTable[fd].fatfs_fil, /* [FATFS] */
    buffer,
    (UINT)nbytes,
    &bytes_written);
  
  if (fr != FR_OK)
  return OS_ERROR;
  
  return (int32)bytes_written;
  }

/* ══════════════════════════════════════════════════════════════════

- OS_lseek
- ══════════════════════════════════════════════════════════════════ */
  int32 OS_lseek(int32 fd, int32 offset, uint32 whence)
  {
  if (fd < 0 || fd >= OS_MAX_FDS || !OS_FDTable[fd].in_use)
  return OS_FS_ERR_INVALID_FD;
  
  /*
  - [FATFS] f_lseek mueve el puntero de posición.
  - FatFS solo admite posición absoluta (equivalente a SEEK_SET).
  - Para SEEK_CUR y SEEK_END calculamos la posición absoluta manualmente.
  - 
  - [FATFS] f_tell devuelve la posición actual.
  - [FATFS] f_size devuelve el tamaño del fichero.
    */
    FSIZE_t new_pos;
  
  switch (whence)
  {
  case OS_SEEK_SET:
  new_pos = (FSIZE_t)offset;
  break;
  case OS_SEEK_CUR:
  new_pos = f_tell(&OS_FDTable[fd].fatfs_fil) + (FSIZE_t)offset; /* [FATFS] */
  break;
  case OS_SEEK_END:
  new_pos = f_size(&OS_FDTable[fd].fatfs_fil) + (FSIZE_t)offset; /* [FATFS] */
  break;
  default:
  return OS_ERROR;
  }
  
  FRESULT fr = f_lseek(&OS_FDTable[fd].fatfs_fil, new_pos); /* [FATFS] */
  if (fr != FR_OK)
  return OS_ERROR;
  
  return (int32)new_pos;
  }

/* ══════════════════════════════════════════════════════════════════

- OS_mkdir
- ══════════════════════════════════════════════════════════════════ */
  int32 OS_mkdir(const char *path, uint32 access)
  {
  (void)access;
  
  if (path == NULL)     return OS_INVALID_POINTER;
  if (!s_fs_mounted)    return OS_ERROR;
  
  char fatfs_path[OS_MAX_PATH_LEN + 2];
  if (translate_path(path, fatfs_path, sizeof(fatfs_path)) != OS_SUCCESS)
  return OS_FS_ERR_PATH_TOO_LONG;
  
  /*
  - [FATFS] f_mkdir crea el directorio.
  - Retorna FR_EXIST si ya existe — lo tratamos como éxito.
    */
    FRESULT fr = f_mkdir(fatfs_path); /* [FATFS] */
    if (fr != FR_OK && fr != FR_EXIST)
    {
    OS_printf("OSAL_FS ERROR: f_mkdir('%s') falló (fr=%d)\n",
    fatfs_path, (int)fr);
    return OS_ERROR;
    }
  
  return OS_SUCCESS;
  }

/* ══════════════════════════════════════════════════════════════════

- OS_stat
- ══════════════════════════════════════════════════════════════════ */
  int32 OS_stat(const char *path, OS_FSStat_t *stat_buf)
  {
  if (path == NULL || stat_buf == NULL) return OS_INVALID_POINTER;
  if (!s_fs_mounted)                    return OS_ERROR;
  
  char fatfs_path[OS_MAX_PATH_LEN + 2];
  if (translate_path(path, fatfs_path, sizeof(fatfs_path)) != OS_SUCCESS)
  return OS_FS_ERR_PATH_TOO_LONG;
  
  /*
  - [FATFS] FILINFO contiene: tamaño, fecha, atributos, nombre.
  - f_stat devuelve FR_NO_FILE si no existe.
    */
    FILINFO fno; /* [FATFS] FILINFO */
    FRESULT fr = f_stat(fatfs_path, &fno); /* [FATFS] */
    if (fr != FR_OK)
    return OS_ERROR;
  
  stat_buf->size   = (uint32)fno.fsize;
  stat_buf->is_dir = (fno.fattrib & AM_DIR) ? 1u : 0u; /* [FATFS] AM_DIR */
  
  return OS_SUCCESS;
  }

/* ══════════════════════════════════════════════════════════════════

- OS_remove
- ══════════════════════════════════════════════════════════════════ */
  int32 OS_remove(const char *path)
  {
  if (path == NULL)  return OS_INVALID_POINTER;
  if (!s_fs_mounted) return OS_ERROR;
  
  char fatfs_path[OS_MAX_PATH_LEN + 2];
  if (translate_path(path, fatfs_path, sizeof(fatfs_path)) != OS_SUCCESS)
  return OS_FS_ERR_PATH_TOO_LONG;
  
  /* [FATFS] f_unlink elimina fichero o directorio vacío */
  FRESULT fr = f_unlink(fatfs_path); /* [FATFS] */
  return (fr == FR_OK) ? OS_SUCCESS : OS_ERROR;
  }

/* ══════════════════════════════════════════════════════════════════

- OS_FS_WriteFile — helper PSP
- 
- Crea o sobreescribe un fichero en una sola operación.
- Usado por PSP_FS_Init() para copiar los archivos de cFE
- desde Flash (const arrays) al RAM disk.
- ══════════════════════════════════════════════════════════════════ */
  int32 fd;
  int32 OS_FS_WriteFile(const char *path, const uint8 *data, uint32 length)
  {
  fd = OS_open(path, OS_WRITE_ONLY, 0);
  if (fd < 0)
  {
  OS_printf("OSAL_FS ERROR: OS_FS_WriteFile('%s') open falló\n", path);
  return OS_ERROR;
  }
  
  int32 written = OS_write(fd, data, length);
  OS_close(fd);
  
  if (written < 0 || (uint32)written != length)
  {
  OS_printf("OSAL_FS ERROR: OS_FS_WriteFile('%s') write incompleto"
  "(%ld/%lu)\n", path, (long)written, (unsigned long)length);
  return OS_ERROR;
  }
  
  return OS_SUCCESS;
  }
