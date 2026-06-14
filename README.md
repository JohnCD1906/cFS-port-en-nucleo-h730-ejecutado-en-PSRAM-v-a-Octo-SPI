# cFS port en STM32H730IBT6Q ejecutado desde PSRAM vía OctoSPI

Porteo del **core Flight System (cFS)** de NASA sobre un microcontrolador
**STM32H730IBT6Q** (Cortex-M7), ejecutando el código desde **PSRAM externa**
mapeada en memoria vía **OctoSPI**, siguiendo un modelo de arranque tipo
**BootROM** (una Boot App en flash interna copia el binario de NOR a PSRAM y
salta).

> **Estado: PORTEO COMPLETO Y FUNCIONAL (modo simulación).**
> Una aplicación cFS real (`dc_motor_app`) corre end-to-end sobre el Software
> Bus, ejecutándose desde PSRAM. El control de hardware (TIM1) está en modo
> simulación; el PWM físico queda como trabajo futuro (ver Deuda técnica).

---

## Arquitectura

### Stack de software

```
┌──────────────────────────────────────────────┐
│  dc_motor_app   (SeqTask, CtrlTask, PWMTask)  │  Aplicación
├──────────────────────────────────────────────┤
│  cFE SB    (Software Bus: pipes, routing)     │
│  cFE EVS   (Event Services)                   │  cFE core
│  cFE ES    (Executive Services, memory pool)  │
├──────────────────────────────────────────────┤
│  PSP STM32H730  (boot detect, RAM disk, time) │  Platform Support
├──────────────────────────────────────────────┤
│  OSAL  (tasks, queues, mutexes, filesystem)   │  OS Abstraction
├──────────────────────────────────────────────┤
│  FreeRTOS (Cortex-M7 r0p1) + FatFS            │  RTOS + FS
├──────────────────────────────────────────────┤
│  STM32H730IBT6Q  ejecutando desde PSRAM       │  Hardware
└──────────────────────────────────────────────┘
```

### Hardware target

| Componente | Función | Modelo |
|---|---|---|
| MCU | Cortex-M7 @ 160 MHz (config Boot App) | STM32H730IBT6Q |
| NOR Flash externa | Almacena el `.bin` de la aplicación | Macronix MX25Q |
| PSRAM externa | Ejecución del código (CODE_AREA) | AP Memory APS128 |
| SRAM interna | `.data`, `.bss`, heap, stack | AXI-SRAM (D1) |

### Modelo de arranque (Boot App externa)

La Boot App (proyecto separado, mantenido por el desarrollador del hardware)
vive en flash interna `0x08000000` y realiza:

```
1. Power-on / Reset
2. Configura OCTOSPI -> NOR Flash (memory-mapped @ 0x70000000)
3. Configura OCTOSPI -> PSRAM     (memory-mapped @ 0x90000000)
4. Copia (shadow-load) el binario NOR -> PSRAM, con D-cache clean
5. Configura PLL (HCLK 160 MHz), I-cache, D-cache, MPU regiones Flash/PSRAM
6. Setea VTOR = 0x90000000 y salta a PSRAM
```

La aplicación de este repositorio es lo que la Boot App carga y ejecuta.
**La aplicación NO reconfigura el reloj** (heredamos el HCLK de la Boot App;
tocar el reloj rompería el timing del OctoSPI desde el que se ejecuta el código).

### Mapa de memoria

| Región | Dirección | Tamaño | Uso |
|---|---|---|---|
| Flash interna | `0x08000000` | 128 KB | Boot App (proyecto separado) |
| DTCM | `0x20000000` | 128 KB | **RAM disk FatFS (64 KB)** + libre |
| AXI-SRAM (D1) | `0x24000000` | 320 KB | `.data`, `.bss`, heap, stack, pool SB |
| SRAM (D2) | `0x30000000` | 32 KB | Reservada para buffers cFE (sin usar aún) |
| SRAM (D3) | `0x38000000` | 16 KB | Reset area + user area |
| NOR Flash externa | `0x70000000` | 16 MB | Binario persistente |
| PSRAM externa | `0x90000000` | 16 MB | Ejecución (`.text`, `.rodata`, vectores) |

El RAM disk del FatFS se ubica en **DTCM** (0 wait states, fuera del rango
cacheable, sin contención con el bus AXI donde viven heap/stack).

---

## Estado del porteo por capas

| Capa | Estado | Validación |
|---|---|---|
| OSAL sobre FreeRTOS | ✅ | Tareas, colas, mutexes corriendo en PSRAM |
| FatFS RAM disk (DTCM) | ✅ | mkfs + mount + read/write validados |
| PSP STM32H730 | ✅ | Cold/warm boot detect, RAM disk, time |
| cFE Executive Services | ✅ | Parser startup.scr, máquina de estados, memory pool |
| cFE Event Services | ✅ | Registro de apps, filtrado, eventos por UART |
| cFE Software Bus | ✅ | Pipes, subscribe, routing, transmit/receive |
| dc_motor_app | ✅ | 3 tareas comunicándose por SB (control PWM simulado) |
| PWM físico (TIM1) | ⏳ | Modo simulación; hardware real pendiente |

---

## Hitos técnicos del porteo

### 1. Fix de `_impure_ptr` (newlib + `--gc-sections`)

El primer HardFault se localizó en `_free_r` (`CFSR=0x400`, INVSTATE). Causa:
`--gc-sections` descartaba la inicialización de `_impure_ptr` (estructura
`_reent` de newlib), dejando un puntero corrupto en `.data`.

**Fix:** flag de linker `-Wl,--undefined=_impure_ptr` en
*MCU GCC Linker → Miscellaneous → Other flags*.

### 2. Migración del port FreeRTOS a Cortex-M7 r0p1

CubeMX genera el port en `Middlewares/.../GCC/ARM_CM4F/`, pero el H730 es
Cortex-M7. El contenido de esa carpeta fue **parcheado manualmente** a CM7 r0p1
incluyendo el workaround del **Errata 837070** (`cpsid i` / `cpsie i` alrededor
del `msr basepri` en `xPortPendSVHandler`).

> ⚠️ **Deuda técnica:** el código es CM7 pero vive en la carpeta `ARM_CM4F`.
> Tras cualquier *Generate Code* de CubeMX, verificar que el parche persiste:
> `git diff Middlewares/.../GCC/ARM_CM4F/port.c`

### 3. Fix del arranque: cFE antes del scheduler

`CFE_ES_Main()` corre en el contexto de `main()`, **antes** de `osKernelStart()`.
El código original esperaba a que las apps llegaran a `LATE_INIT`/`RUNNING` con
bucles de `OS_TaskDelay()`. Pero **`vTaskDelay()` antes de
`vTaskStartScheduler()` corrompe las listas internas de tareas de FreeRTOS**,
impidiendo que el scheduler despache la única tarea de usuario (síntoma: la app
no corría salvo que existiera una segunda tarea "ancla").

**Fix:** se eliminaron los bucles de espera pre-scheduler en `CFE_ES_Main`. Las
apps inicializan normalmente cuando el scheduler arranca.

> **Deuda técnica:** el modelo 100% correcto sería convertir `CFE_ES_Main` en
> una tarea dentro del scheduler (como en cFS real), en lugar de código
> pre-scheduler.

---

## Convención de mensajes (UART, 115200 8-N-1)

Los logs distinguen tres orígenes por prefijo, todos en inglés:

| Prefijo | Origen | Apagable |
|---|---|---|
| `[PORT_DEBUG]` | Andamiaje del porteo (PSP, OSAL, FS, main) | **Sí** |
| `ES:` / `EVS:` / `SB:` | cFS core services | No |
| `[DC_MOTOR]` / `[SeqTask]` ... | Aplicación | No |
| `[MX25Q]` / `[APS128]` / `[INFO]` | Boot App (externa) | (controla el desarrollador del HW) |

Los mensajes `[PORT_DEBUG]` se silencian comentando `#define PORT_DEBUG_ENABLE`
en `Core/Inc/port_debug.h`, dejando solo la traza de cFS y de la app para una
build de "producción".

`OS_printf` está serializado con un mutex (creado en `OS_API_Init`), así que los
mensajes de tareas concurrentes no se entremezclan. El lock solo se activa
cuando el scheduler está corriendo.

---

## Cómo compilar y flashear

### Requisitos
- STM32CubeIDE
- STM32CubeProgrammer con external loader para la NOR Macronix MX25Q
- Boot App previamente cargada en flash interna `0x08000000`

### Build
1. Importar el proyecto en CubeIDE.
2. Verificar linker activo: `Linker_PSRAM_ext.ld`.
3. Verificar flag `-Wl,--undefined=_impure_ptr` en *MCU GCC Linker →
   Miscellaneous*.
4. *Project → Build All*. El binario queda en `Debug/cfs_app_psram.bin`.

### Flashear a NOR externa
1. CubeProgrammer → Connect (SWD).
2. Seleccionar el external loader de la MX25Q.
3. Erasing & Programming: file `Debug/cfs_app_psram.bin`, start address
   `0x70000000`, Verify activado.
4. Start Programming → reset físico.

---

## Estructura del proyecto

```
cfs_app_psram/
├── Core/
│   ├── Inc/
│   │   ├── osal/        osal_freertos.h, osal_freertos_fs.h
│   │   ├── psp/         psp_stm32h730.h
│   │   ├── cfe/         cfe_es_stm32.h, cfe_evs_stm32.h, cfe_sb_stm32.h
│   │   ├── app/         dc_motor_app.h
│   │   ├── port_debug.h
│   │   ├── cfe_es_platform_cfg.h
│   │   └── cfe_default_files.h
│   └── Src/
│       ├── osal/        osal_freertos.c, osal_freertos_fs.c
│       ├── psp/         psp_stm32h730.c
│       ├── cfe/         cfe_es_stm32.c, cfe_evs_stm32.c, cfe_sb_stm32.c
│       ├── app/         dc_motor_app.c
│       └── main.c
├── FATFS/               user_diskio.c (RAM disk en DTCM)
├── Middlewares/Third_Party/FreeRTOS/  (port CM7 r0p1 en carpeta ARM_CM4F)
├── Linker_PSRAM_ext.ld
├── cfs_app_psram.ioc
└── reference/cfs_motor/  (snapshot de los archivos validados en F439ZI)
```

---

## Deuda técnica pendiente

1. **TIM1 real (PWM físico):** la app corre con `hw_pwm_set()` en simulación
   (`#ifdef DC_MOTOR_HW_ENABLED`). Para mover un motor real hay que configurar
   TIM1 en el `.ioc` y activar el define.
2. **Port FreeRTOS en carpeta equivocada:** CM7 r0p1 vive en `ARM_CM4F/`.
   Migrar a `ARM_CM7/r0p1/` y actualizar el `.cproject`.
3. **`CFE_ES_Main` como tarea:** refactorizar para que corra dentro del
   scheduler en lugar de en `main()`, recuperando el sync de startup real.
4. **`OS_MAX_FDS = 8`:** puede quedar corto cuando varias apps abran archivos
   simultáneamente. Revisar al añadir más apps.
5. **Coalescing del memory pool:** `CFE_ES_PutPoolBuf` no fusiona bloques libres
   adyacentes (fragmentación a largo plazo).
6. **SRAM D2 sin usar:** reservada para buffers cFE pero el pool del SB vive
   actualmente en `.bss` (AXI-SRAM). Mover si se necesita aislamiento.
7. **Caches/MPU:** la app hereda la config de la Boot App. Si en el futuro se
   gestiona MPU desde la app, configurar PSRAM como Write-Through.

---

## Referencias

- [AN5188 — STM32H7 system memory boot](https://www.st.com/resource/en/application_note/an5188.pdf)
- [NASA cFS](https://github.com/nasa/cFS)
- [NASA OSAL](https://github.com/nasa/osal)
- FreeRTOS Errata 837070 (Cortex-M7 r0p1)
