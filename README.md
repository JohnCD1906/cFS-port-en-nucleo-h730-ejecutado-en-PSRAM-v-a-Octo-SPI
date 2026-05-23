\# cFS port en STM32H730IBT6Q ejecutado desde PSRAM vía OctoSPI



Porteo del \*\*core Flight System (cFS)\*\* de NASA sobre microcontrolador

\*\*STM32H730IBT6Q\*\* (Cortex-M7) ejecutando código desde \*\*PSRAM externa\*\* mapeada

en memoria vía \*\*OctoSPI\*\*, siguiendo el modelo \*\*BootROM\*\* descrito en la

nota de aplicación ST \*\*AN5188\*\*.



> Estado actual: \*\*OSAL sobre FreeRTOS corriendo en PSRAM\*\*.

> Próximas capas (PSP, cFE ES, EVS, SB, dc\_motor\_app) en integración progresiva.



\---



\## Arquitectura



\### Hardware target



| Componente | Función | Modelo |

|---|---|---|

| MCU | Cortex-M7 @ 64 MHz (HSI) | STM32H730IBT6Q |

| NOR Flash externa | BINARY\_AREA (almacena el `.bin` de la aplicación) | Macronix MX25Q |

| PSRAM externa | CODE\_AREA (ejecución del código) | AP Memory APS128 |

| SRAM interna | DATA\_AREA (`.data`, `.bss`, heap, stack) | AXI-SRAM 320 KB |



\### Modelo BootROM (AN5188)



```

┌─────────────────────────────────────────────────────────────┐

│  1. Power-on / Reset                                        │

│         ↓                                                   │

│  2. Boot App (Flash interna @ 0x08000000) se ejecuta        │

│         ↓                                                   │

│  3. Configura OCTOSPI1 → NOR Flash (memory-mapped @ 0x70..) │

│  4. Configura OCTOSPI2 → PSRAM (memory-mapped @ 0x90..)     │

│         ↓                                                   │

│  5. Copia binario: NOR Flash → PSRAM                        │

│         ↓                                                   │

│  6. Salta a PSRAM (0x90000000) → ejecución de la aplicación │

│         ↓                                                   │

│  7. App usa AXI-SRAM (0x24000000) para datos en runtime     │

└─────────────────────────────────────────────────────────────┘

```



La Boot App vive en este repositorio aparte y queda fuera del scope de

este proyecto. Esta aplicación es lo que la Boot App carga.



\### Mapa de memoria



| Región | Dirección | Tamaño | Uso |

|---|---|---|---|

| Flash interna | `0x08000000` | 128 KB | Boot App (proyecto separado) |

| DTCM RAM | `0x20000000` | 128 KB | \*\*Reservado para RAM disk FatFS (futuro)\*\* |

| AXI-SRAM (D1) | `0x24000000` | 320 KB | `.data`, `.bss`, heap, stack |

| SRAM D2 | `0x30000000` | 32 KB | Libre |

| SRAM D3 | `0x38000000` | 16 KB | Libre |

| NOR Flash externa | `0x70000000` | 16 MB | BINARY\_AREA (binario persistente) |

| PSRAM externa | `0x90000000` | 16 MB | CODE\_AREA (ejecución) |



\---



\## Estado del porteo



| Capa | Estado | Notas |

|---|---|---|

| OSAL sobre FreeRTOS | ✅ Validado | Corriendo en PSRAM, tarea OSAL\_TEST publicando por UART |

| FatFS RAM disk (DTCM) | ⏳ En integración | |

| PSP STM32H730 | ⏳ Pendiente | |

| cFE Executive Services (ES) | ⏳ Pendiente | |

| cFE Event Services (EVS) | ⏳ Pendiente | |

| cFE Software Bus (SB) | ⏳ Pendiente | |

| App `dc\_motor\_app` | ⏳ Pendiente | Validada previamente sobre F439ZI |



\---



\## Hitos técnicos relevantes



\### Fix de `\_impure\_ptr` (newlib reentrancy + `--gc-sections`)



El primer HardFault del porteo se localizó en `\_free\_r` con `CFSR=0x400`

(INVSTATE). Causa: `--gc-sections` + `--specs=nosys.specs` descartaba la

inicialización estática de `\_impure\_ptr` (la estructura `\_reent` de newlib),

dejando un puntero corrupto en `.data` que causaba un `blx` a dirección sin

bit Thumb.



\*\*Fix aplicado\*\* en \*Project Properties → MCU GCC Linker → Miscellaneous →

Other flags\*:



```

\-Wl,--undefined=\_impure\_ptr

```



Verificación con `objdump -s -j .data`: offset `0x20` de `.data` debe contener

`28000024` (little-endian de `0x24000028`), apuntando a `\&\_impure\_data`.



\---



\## Configuración de build



\### Linker script activo



\*\*`Linker\_PSRAM\_ext.ld`\*\* — coloca `.text/.rodata/.isr\_vector` en PSRAM

(`0x90000000`), `.data` con VMA en AXI-SRAM y LMA en PSRAM

(`AT> PSRAM`), `.bss` en AXI-SRAM, heap+stack en AXI-SRAM.



`SCB->VTOR = 0x90000000` se setea al inicio de `main()` antes de `HAL\_Init()`.



\### Reloj de sistema



\- HSI a 64 MHz (sin PLL)

\- VOS Scale 3

\- FLASH\_LATENCY\_1

\- D-cache e I-cache \*\*deshabilitadas\*\* durante bring-up (se habilitarán con

&#x20; MPU configurado tras validar todas las capas cFS).



\---



\## Cómo compilar y flashear



\### Requisitos



\- STM32CubeIDE 1.16 o superior

\- STM32CubeProgrammer

\- Boot App previamente cargada en flash interna `0x08000000`

\- External loader para Macronix MX25Q configurado en CubeProgrammer



\### Build



1\. Importar el proyecto en CubeIDE: \*File → Import → Existing Projects into Workspace\*.

2\. Verificar que el linker activo es `Linker\_PSRAM\_ext.ld` en \*Project Properties → C/C++ Build → Settings → MCU GCC Linker → General\*.

3\. Verificar que el flag `-Wl,--undefined=\_impure\_ptr` está en \*MCU GCC Linker → Miscellaneous → Other flags\*.

4\. \*Project → Build All\* (Ctrl+B).

5\. El `.bin` queda en `Debug/cfs\_app\_psram.bin`.



\### Flashear a NOR externa



1\. STM32CubeProgrammer → Connect (modo SWD).

2\. \*External loaders\* → seleccionar el loader de la MX25Q.

3\. \*Erasing \& Programming\*:

&#x20;  - File path: `Debug/cfs\_app\_psram.bin`

&#x20;  - Start address: `0x70000000`

&#x20;  - ☑ Verify programming

4\. \*Start Programming\* → esperar "Download verified successfully".

5\. Disconnect.

6\. Reset físico de la placa.



\### Verificación por UART



UART8 a 115200 8-N-1. La salida esperada al reset es:



```

=== Jar8 external code execution ===

\- Flash MX25Q: Initializing...OK

\- Flash MX25Q: Setting up memory mapped mode...OK

\- PSRAM APS128: Calibrating DLYB...OK

\- PSRAM APS128: Setting up memory mapped mode...OK

\->Copying app to PSRAM... Talk you in the other side

=== === === === === === === === === ===



=== MAIN APP - cFS Phase 1: OSAL ===

\[1] osKernelInitialize OK

\[2] OS\_API\_Init OK

OSAL: Tarea 'OSAL\_TEST' OK (slot=0, osal\_prio=100→freertos\_prio=34, stack=512 words)

\[osal] OS\_TaskCreate OK

\[3] osKernelStart...

\[osal\_test] Tarea OSAL arrancada

\[osal\_test] tick=0

\[osal\_test] tick=1

...

```



\---



\## Estructura del proyecto



```

cfs\_app\_psram/

├── Core/

│   ├── Inc/         # main.h, osal/, libs/uart\_debug.h

│   └── Src/         # main.c, osal\_freertos.c, osal\_test\_task.c

├── Drivers/         # HAL STM32H7xx + CMSIS

├── Middlewares/Third\_Party/FreeRTOS/  # Kernel FreeRTOS

├── Linker\_PSRAM\_ext.ld   # ← Linker activo

├── cfs\_app\_psram.ioc     # Configuración CubeMX

└── README.md

```



\---



\## Referencias



\- \[AN5188 — STM32H7 system memory boot](https://www.st.com/resource/en/application\_note/an5188.pdf)

\- \[AN4891 — STM32H72x/H73x system architecture](https://www.st.com/resource/en/application\_note/an4891.pdf)

\- \[NASA cFS framework](https://github.com/nasa/cFS)

\- \[OSAL — Operating System Abstraction Layer](https://github.com/nasa/osal)



\---



\## Licencia



Pendiente de definir.

