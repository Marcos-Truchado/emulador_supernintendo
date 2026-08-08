# Emulador de Super Nintendo (SNES) en C++ — Documento de diseño y hoja de ruta

**Autor:** Matru
**Objetivo del proyecto:** emulador de SNES preciso (dot/cycle-accurate) y razonablemente eficiente, escrito en C++, con el core totalmente desacoplado del frontend, apto para macOS.

Este proyecto es un **emulador de Super Nintendo (SNES) para macOS**.

**Consultar antes de implementar timing de CPU/PPU/APU/DMA**
Referencia de hardware: docs_documentacion/fullsnes.txt
Consultar antes de implementar timing de CPU/PPU/APU/DMA.

---

## 0. Puntos críticos de diseño (leer antes que nada)

Estos dos puntos son los que más riesgo tienen de contaminar todo el proyecto si se deciden mal desde el principio. Cualquier IA que colabore debe tenerlos presentes antes de tocar el scheduler o de plantearse añadir chips especiales de cartucho.

### 0.1 El scheduler es el punto más delicado de todo el proyecto

El modelo de byuu (bsnes/higan) mantiene **un entero de 64 bits con signo por cada relación 1:1 entre dos componentes** — para el SNES base son solo dos relaciones: CPU↔PPU y SMP↔DSP (porque DSP solo habla con SMP, y PPU solo habla con CPU; no hay una relación N:N que gestionar).

Mecanismo en la práctica:
- Cada vez que el componente A ejecuta, incrementa su contador de ciclos y ajusta el contador compartido con B.
- Cuando A necesita "ver" el estado actual de B (p. ej. la CPU lee `$2140` y necesita saber qué ha escrito ahí el SMP hasta este instante exacto), se llama a `B.catch_up()`, que hace avanzar a B tantos ciclos como haga falta para quedar sincronizado con A.
- Esto evita tanto el enfoque ingenuo de alternar 1 ciclo de cada componente (correcto pero carísimo) como el de ejecutar frames completos por separado (barato pero rompe cualquier efecto raster).

Por qué es tan delicado:
- **Si el orden de catch-up entre componentes no se decide con cuidado**, aparecen dependencias circulares sutiles: A necesita el estado de B hasta cierto ciclo, pero calcular ese estado de B requiere saber si A le escribió algo antes de ese ciclo exacto. Un error de un solo ciclo aquí no falla en tests sintéticos simples — falla meses después, en un juego concreto, y es muy difícil de rastrear hasta el origen real.
- **Si el modelo de sincronización se acopla directamente al código de cada chip** en vez de tratarse como una capa/interfaz genérica (`step()` / `catch_up()` sobre una abstracción de "hilo cooperativo"), cualquier cambio de precisión futuro en un componente obliga a rehacer el scheduler entero. Esto es justo lo que le pasó a byuu en las primeras versiones de bsnes.
- **Es la base de todo lo demás**: si el scheduler está mal, cualquier bug posterior en sprites, color math o HDMA puede en realidad ser un desfase de sincronización disfrazado de bug del componente que se está mirando. Por eso el roadmap (§9) exige validar el scheduler con un test ROM que solo cuente VBlanks/IRQs (Fase 3) *antes* de invertir tiempo en renderizado real (Fase 4).

Recomendación práctica: implementar primero solo la relación CPU↔PPU (sin APU) y validarla exhaustivamente contra el conteo exacto esperado (1364 dots/scanline, 262 scanlines/frame NTSC) antes de añadir la relación SMP↔DSP.

### 0.2 Por qué SA-1 / SuperFX / DSP-1 quedan fuera del alcance inicial

No son "un poco más de trabajo": cada uno rompe una asunción distinta del diseño base y requiere generalizar el scheduler antes de haberlo validado con el caso simple.

- **SA-1** (Kirby Super Star, Super Mario RPG, Kirby's Dream Land 3): es literalmente una **segunda CPU 65816 completa** dentro del cartucho, corriendo a 10.74 MHz, con memoria parcialmente compartida (I-RAM de 2KB, BW-RAM) y capacidad de interrumpirse mutuamente con la CPU principal por IRQ. Añadirlo implica pasar de un scheduler de "un par CPU↔PPU" a uno con un tercer hilo cooperativo, más las reglas de colisión de acceso simultáneo a memoria compartida (penalización de rendimiento real que el hardware modela y que un emulador preciso también debería). Viviría como módulo dentro de `cartridge/`, no dentro de `cpu/`, porque conceptualmente es parte del cartucho.
- **SuperFX** (Star Fox, Yoshi's Island): no es un 65816 en absoluto — es una **CPU RISC propietaria** (Argonaut) con un ISA completamente distinto, pensada para cálculo de gráficos poligonales y generación de bitmaps que el PPU estándar convierte a tiles. Requiere un intérprete nuevo desde cero, con su propia relación de reloj con el bus.
- **DSP-1** (Pilotwings, Super Mario Kart): coprocesador matemático de punto fijo consultado por la CPU vía registros mapeados ("pregunta y espera respuesta"), sin hilo propio en el scheduler — es, con diferencia, el más sencillo de integrar de los tres.

Conclusión operativa: el SNES base (CPU 65816 + PPU + APU + DMA/HDMA + LoROM/HiROM) debe quedar sólido y validado primero. Cada chip especial se añade después como módulo aislado en `cartridge/`, activado condicionalmente según lo que declare la cabecera de la ROM, sin tocar el core ya validado.

---

## 1. Decisiones de arquitectura ya tomadas

Estas cuatro decisiones son las que condicionan todo lo demás. Cualquier código o diseño posterior debe ser coherente con ellas.

### 1.1 Nivel de precisión: dot/cycle-based
La PPU avanza **pixel a pixel (dot a dot)** en sincronía exacta con el reloj maestro, igual que hace el core SNES de higan/bsnes. Esto implica:
- No hay "renderizado por scanline completo" ni "por frame completo": el estado del PPU debe ser consultable en cualquier dot.
- Los efectos de raster (cambios de scroll/paleta a mitad de scanline, HDMA) salen "gratis" porque el propio modelo temporal ya es exacto.
- Coste: mucha más carga de CPU (host) y mucha más complejidad de diseño que un enfoque scanline-based. Esto es aceptable dado el objetivo del proyecto (precisión > velocidad bruta).

### 1.2 Sincronización CPU/PPU/APU: relojes concretos a modelar
El mecanismo de scheduler (catch-up relativo) ya está descrito en detalle en §0.1 — léelo antes de esta sección. Lo que añade esta sección es el dato concreto de qué relojes hay que modelar y con qué proporción, algo que no es intuitivo y que hay que tener a mano al escribir el código:

- Relación con tu experiencia: esto es, en espíritu, un problema de sincronización de relojes lógicos/orden de eventos muy parecido a lo que ya trabajaste en sistemas distribuidos (vector clocks, causal multicast) — aquí en miniatura y con un solo proceso.
- Referencia clave: artículo de byuu "Emulator Schedulers" (byuu.net/design/schedulers) — es lectura obligatoria antes de escribir una sola línea del scheduler.

Relación de relojes a modelar explícitamente:
- CPU y PPU comparten el **reloj maestro** (~21.477 MHz NTSC / ~21.28 MHz PAL). La CPU corre a divisores variables de ese reloj (6, 8 o 12 ciclos maestro por ciclo de CPU, según si accede a "fast" o "slow" memory, y si el modo rápido de ROM está activo). La PPU avanza 4 ciclos maestro por dot (pixel).
- El SMP (CPU de audio, S-SMP) y el DSP corren con un oscilador **completamente distinto** (~24.576 MHz), sin relación de ratio limpia con el reloj CPU/PPU. La aproximación estándar (y suficientemente precisa) es tratarlos como razón 21:24, es decir, por cada 21 ciclos del reloj maestro CPU/PPU se ejecutan 24 ciclos del reloj maestro de audio (1 ciclo de S-SMP = 24 ciclos de su reloj maestro de audio, 2 ciclos de S-DSP).
- La comunicación CPU↔APU pasa exclusivamente por los 4 puertos de I/O mapeados en `$2140-$2143` (lado CPU) / puertos equivalentes en el espacio del SPC700 — no hay bus compartido, así que el catch-up aquí es más sencillo que CPU↔PPU.

### 1.3 Interpretación (no JIT)
Intérprete simple: se decodifica y ejecuta instrucción a instrucción, sin recompilación dinámica a código nativo. Es la elección correcta para un primer emulador preciso: JIT añade una capa entera de complejidad (generación de código, invalidación de caché al escribir en RAM ejecutable, etc.) que no aporta nada a la precisión y solo tiene sentido cuando el rendimiento ya es el cuello de botella real.

### 1.4 Separación core/frontend desde el día 1
Dos módulos con una frontera dura:
- **Core**: CPU (65816) + PPU + APU (SMP+DSP) + Bus + Cartucho/mappers + Scheduler. No sabe nada de ventanas, audio del sistema operativo, ni input real. Expone una API mínima (cargar ROM, ejecutar N frames/ciclos, leer framebuffer, leer buffer de audio, inyectar input, guardar/cargar estado).
- **Frontend**: en tu caso, macOS. Se encarga de ventana, refresco de vídeo, salida de audio, mapeo de teclado/mando y persistencia de partidas/config. El core no debe tener ni un solo `#include` de una librería de plataforma.

Esta separación es la misma filosofía "libretro core" que usan higan/bsnes/Mesen-S y es lo que permite, más adelante, tener varios frontends (macOS nativo, SDL2 multiplataforma, libretro core para RetroArch) sin tocar el core.

---

## 2. Qué es "un emulador para Mac" en la práctica

En macOS, un frontend nativo implica:
- **Vídeo**: lo más simple y portable es render vía **SDL2** (Metal como backend por debajo, sin que tengas que tocar Metal directamente) o **Metal** directo si quieres aprovechar shaders custom (filtros HD Mode 7, escalado, shaders tipo CRT). Para empezar: SDL2 es la opción pragmática — multiplataforma gratis y con buen soporte de audio/input integrado.
- **Audio**: SDL2 Audio (o CoreAudio si quieres ir directo a low-level en Mac) alimentado por el buffer de muestras que genera el DSP del core a 32 kHz.
- **Input**: SDL2 GameController API cubre mandos USB/Bluetooth modernos (incluye mandos tipo Xbox/PlayStation) sin código específico de Mac.
- **Empaquetado**: en una fase tardía, un `.app` bundle con `CMake` + `Info.plist` si quieres distribución nativa; no es prioritario al principio, basta con un binario ejecutable desde terminal.

No hace falta AppKit/SwiftUI para nada del core ni siquiera del frontend inicial: SDL2 ya te da una ventana y un bucle de eventos multiplataforma sobre macOS sin fricción.

---

## 3. Organización del código (estructura de repositorio)

```
snes-emu/
├── CMakeLists.txt                 # build system raíz
├── README.md
├── docs/                          # notas técnicas propias, resúmenes de registros, decisiones de diseño
│   └── decisiones_arquitectura.md
├── core/                          # EL CORE — sin dependencias de plataforma, librería estática/dinámica
│   ├── CMakeLists.txt
│   ├── include/snes/               # cabeceras públicas del core (API que consume el frontend)
│   │   └── snes.hpp
│   ├── src/
│   │   ├── scheduler/              # scheduler relativo CPU<->PPU, SMP<->DSP
│   │   │   ├── scheduler.hpp/.cpp
│   │   │   └── thread.hpp          # abstracción de "hilo cooperativo" (contador de ciclos, catch_up)
│   │   ├── cpu/                    # 65816
│   │   │   ├── cpu65816.hpp/.cpp
│   │   │   ├── cpu65816_disasm.cpp # útil desde el día 1 para depurar
│   │   │   ├── opcodes.cpp         # tabla de opcodes (256 base + variantes por tamaño de registro)
│   │   │   └── addressing_modes.cpp
│   │   ├── ppu/                    # Picture Processing Unit(es) — en SNES real son 2 chips (PPU1/PPU2)
│   │   │   ├── ppu.hpp/.cpp
│   │   │   ├── background.cpp      # BG1-4, modos 0-7
│   │   │   ├── sprites.cpp         # OAM, evaluación de sprites por línea
│   │   │   ├── mode7.cpp           # transformación afín Modo 7
│   │   │   ├── color_math.cpp      # ventanas, adición/sustracción de color, transparencias
│   │   │   └── timing.cpp          # dot/scanline counters, VBlank/HBlank, NMI/IRQ triggers
│   │   ├── apu/                    # audio: SPC700 (SMP) + DSP + 64KB RAM de audio
│   │   │   ├── smp.hpp/.cpp        # CPU de sonido (S-SMP), set de instrucciones propio
│   │   │   ├── dsp.hpp/.cpp        # generador de sonido de 8 canales (ADPCM, ADSR, eco)
│   │   │   └── apu_timers.cpp      # 3 timers internos del SMP
│   │   ├── bus/                    # bus principal + mapeo de memoria + DMA/HDMA
│   │   │   ├── bus.hpp/.cpp
│   │   │   ├── dma.cpp             # 8 canales DMA generales
│   │   │   └── hdma.cpp            # HDMA (DMA por scanline, clave para muchos efectos)
│   │   ├── cartridge/              # ROM + mappers/boards
│   │   │   ├── cartridge.hpp/.cpp
│   │   │   ├── mapper_lorom.cpp
│   │   │   ├── mapper_hirom.cpp
│   │   │   ├── mapper_exhirom.cpp  # opcional, fase tardía
│   │   │   └── sram.cpp            # guardado en batería
│   │   ├── input/
│   │   │   └── controller.hpp/.cpp # pad estándar, más adelante multitap/ratón/etc.
│   │   └── system.hpp/.cpp         # ensambla todo, expone la API pública (run_frame, reset, etc.)
│   └── tests/                      # tests unitarios del core (ver §7)
├── frontend/                       # todo lo dependiente de plataforma
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp
│       ├── video.cpp                # SDL2 render del framebuffer
│       ├── audio.cpp                # SDL2 audio callback
│       └── input.cpp                # SDL2 GameController -> core input
├── tools/                          # utilidades de desarrollo
│   ├── disasm_cli/                  # desensamblador standalone para depurar ROMs
│   └── trace_diff/                  # comparar traza ciclo a ciclo contra un emulador de referencia
└── third_party/                    # SDL2, catch2/doctest, etc. (vía submódulos o FetchContent)
```

Principios de organización:
- **Un módulo = un chip real.** Facilita mapear cada bug a "qué componente físico se está modelando mal" y facilita también leer la documentación en paralelo al código.
- El `scheduler` no vive dentro de ningún chip: es el árbitro. Cada chip implementa una interfaz mínima tipo `Thread` con `step(cycles)` y expone su reloj actual para que el scheduler calcule catch-up.
- Nada en `core/` incluye SDL2 ni ninguna cabecera de plataforma. Si sientes la tentación de hacerlo, es señal de que esa lógica pertenece al frontend.

---

## 4. Componentes técnicos a implementar (con qué hay que lidiar exactamente)

### 4.1 CPU — Ricoh 5A22 (65816 + extras)
- Núcleo **65816**: extensión de 16 bits del 6502, con modo *emulation* (arranca aquí, compatible 6502) y modo *native*. Registros A/X/Y de 8 o 16 bits conmutables en caliente (flags M y X en el registro de estado), banco de memoria de 24 bits (registros DBR/PBR), stack relocalizable, nuevos modos de direccionamiento (direct page relocalizable, long addressing, stack-relative, etc.).
- El **5A22** (el chip real que monta la consola) añade sobre el 65816 puro: multiplicación/división por hardware (`$4202-$4206`), los 8 canales DMA + HDMA, los registros de joypad auto-leídos, y los timers de IRQ por posición H/V (`$4207-$420A`).
- Implementación recomendada: tabla de 256 opcodes base, con lógica condicional según flags M/X para las variantes de 8/16 bits (patrón común: generar las variantes con macros o templates de C++ para no duplicar 3 veces cada opcode).
- Ciclos: cada instrucción tiene un coste variable no trivial (depende del modo de direccionamiento, de si cruza página, de si M/X están a 8 o 16 bits, de si la memoria accedida es "fast" o "slow"). Esto debe alimentar directamente al scheduler.
- Documentación: datasheet oficial de Western Design Center (WDC) del 65C816, y el libro "Programming the 65816" (Eyes & Lichty) — referencia canónica del set de instrucciones y de los modos de direccionamiento.

### 4.2 PPU — dos chips (PPU1 "Ariitma"/PPU2), tratados como una unidad lógica
- **Modos de fondo (BG modes 0-7)**: hasta 4 capas de fondo simultáneas con distintas profundidades de color según el modo; el Modo 7 es un caso especial (una sola capa con transformación afín/matriz para rotación y escalado — usado en F-Zero, Mario Kart, etc.).
- **Sprites (OAM)**: 128 sprites máx., evaluación por scanline con el límite real de 32 sprites y 34 tiles por línea (y el comportamiento de "time over"/"range over" cuando se excede, que algunos juegos usan deliberadamente).
- **Color math**: ventanas (windows) por capa, suma/resta de color, modos de transparencia — es de lo más intrincado de todo el chip y una fuente enorme de bugs sutiles de precisión.
- **Timing preciso**: 1364 "dots" (ciclos maestro / 4, aprox. 340 puntos visibles) por scanline, 262 líneas en NTSC (312 en PAL), con VBlank/HBlank en posiciones exactas que disparan NMI y los IRQ programables por H/V.
- **VRAM/CGRAM/OAM**: accesibles normalmente solo durante VBlank o "forced blank"; hay comportamientos de acceso fuera de esas ventanas que producen corrupción (algunos juegos lo explotan sin querer y hay que reproducirlo igual).
- Documentación imprescindible: sección PPU de **fullsnes** (nocash) y la wiki de **SNESdev** (snes.nesdev.org) para el detalle registro a registro.

### 4.3 APU — SPC700 (SMP) + DSP
- El **SMP** es una CPU de 8 bits con su propio set de instrucciones (no es un 6502, es una arquitectura distinta), 64 KB de RAM propia, y solo se comunica con la CPU principal vía 4 puertos de I/O mapeados (`$2140-$2143`).
- El **DSP** genera audio de 8 canales con envolventes ADSR, muestras comprimidas en formato BRR (ADPCM propietario de Nintendo), y un motor de eco/reverb.
- Esto es un subsistema casi independiente: se puede desarrollar y testear de forma bastante aislada del resto una vez el bus mínimo de comunicación con la CPU esté listo.
- Documentación: sección APU de fullsnes; también existen documentos específicos del SPC700 (opcode tables) y del formato BRR ampliamente documentados por la escena de ripeo de música SNES (SPC).

### 4.4 Bus, DMA y HDMA
- El **bus principal** enruta accesos de memoria según banco/dirección a: WRAM, registros PPU, registros APU (vía los puertos I/O), registros del propio 5A22 (multiplicación, joypad, DMA), y el cartucho.
- **DMA general**: 8 canales, transferencias a máxima velocidad entre cualquier dirección A-bus y los registros del B-bus (típicamente VRAM/CGRAM/OAM), roban ciclos de CPU mientras están activos.
- **HDMA**: variante que se dispara *una vez por scanline* durante HBlank, y es el mecanismo detrás de la inmensa mayoría de efectos de raster (gradientes, splits de scroll, "curvas" de carretera, etc.). Modelarlo bien depende directamente de que el timing dot-accurate de la PPU (§4.2) esté correcto.
- Documentación: sección "SNES DMA Transfers" de fullsnes.

### 4.5 Cartucho y mappers
- Los dos formatos de mapeo de memoria dominantes: **LoROM** y **HiROM** (más variantes menos comunes como ExHiROM). Los chips especiales de cartucho (SA-1, SuperFX, DSP-1) quedan fuera de esta fase — motivo detallado en §0.2, tratamiento en Fase 8 (§9).
- Detección de cabecera de ROM (o carencia de ella: hay ROMs sin cabecera de 512 bytes y con ella, hay que detectarlo heurísticamente igual que hacen los emuladores de referencia).
- SRAM con pila (battery-backed save) para partidas guardadas.

### 4.6 Input
- Lectura de mandos estándar vía el protocolo serie de auto-lectura de joypad de la consola (registros `$4016/$4017` y los shadow registers auto-leídos `$4218-$421F`).

---

## 5. Conceptos técnicos clave que hay que tener interiorizados

- **Reloj maestro y sus divisores** (§1.2).
- **Fast ROM vs Slow ROM**: el cartucho puede declarar acceso a 3.58 MHz ("fast", 6 ciclos maestro) o 2.68 MHz ("slow", 8 ciclos maestro) según la región de memoria — afecta directamente al conteo de ciclos de cada instrucción de CPU.
- **Open bus**: cuando se lee una dirección "no mapeada" o un registro write-only, el bus devuelve el último valor que circuló por él, no un valor fijo — varios juegos dependen de esto y romperlos es una fuente típica de incompatibilidades sutiles.
- **NMI vs IRQ programable**: el NMI se dispara al entrar en VBlank (si está habilitado); el IRQ programable (`$4207-$420A`) se dispara en una posición H/V arbitraria configurada por el juego — es el mecanismo central detrás de los splits de pantalla y hay que dispararlo exactamente en el dot correcto.
- **Forced blank y ventanas de acceso a VRAM/OAM/CGRAM**.
- **LoROM/HiROM y el mapeo de bancos de 24 bits del 65816**.
- **BRR (Bit Rate Reduction)**: formato de compresión de audio propio de Nintendo usado por el DSP.

---

## 6. Documentación a leer (en el orden recomendado)

1. **fullsnes (nocash SNES specs)** — problemkaputt.de/fullsnes.htm — la referencia más completa y detallada que existe, registro a registro, quirk a quirk. Es la "biblia". Vas a volver a este documento constantemente durante todo el proyecto.
2. **SNESdev wiki** (snes.nesdev.org/wiki) y **wiki.superfamicom.org** — más digeribles que fullsnes para una primera pasada por cada subsistema (PPU registers, memory map, DMA), buenos para orientarte antes de sumergirte en fullsnes.
3. **Datasheet del WDC 65C816** (Western Design Center) + libro *"Programming the 65816: Including the 6502, 65C02, and 65802"* (Eyes & Lichty) — referencia canónica del set de instrucciones del CPU.
4. **Artículo de byuu "Emulator Schedulers"** (byuu.net/design/schedulers) — lectura obligatoria antes de tocar el scheduler; explica exactamente el modelo relativo que vas a implementar, con el caso concreto de la SNES como ejemplo.
5. **Código fuente de bsnes / higan** (github.com/bsnes-emu/bsnes, github.com/higan-emu/higan) — el propio código, no solo para copiar sino para contrastar decisiones de diseño una vez tengas tu propia implementación funcionando; especialmente el core `sfc/` de higan es el ejemplo vivo de arquitectura dot-accurate con scheduler relativo escrito en C++.
6. **Mesen-S** (github.com/SourMesen/Mesen-S) — otro emulador cycle-accurate de referencia, en C++, útil para contrastar implementación del PPU y comparar comportamientos de casos límite.
7. **Test ROMs**: la suite de test ROMs de la escena de desarrollo homebrew SNES (buscar "SNES test roms" en snes.nesdev.org — hay tests específicos de CPU, de timing de PPU, de DMA) — imprescindibles para validar precisión sin depender de "si el juego comercial X se ve bien".

---

## 7. Proyectos de referencia (para inspirarte y contrastar, no para copiar literalmente)

| Proyecto | Por qué mirarlo |
|---|---|
| **higan / bsnes** (byuu/Near) | El estándar de oro en precisión dot-accurate para SNES. Tu propia elección de arquitectura (scheduler relativo, catch-up) es literalmente la suya. |
| **ares** | Fork moderno y activo de higan, mismo linaje de diseño, útil si el código de higan queda desactualizado respecto a C++ moderno. |
| **Mesen-S** | Otro cycle-accurate en C++, con foco fuerte en debugging tooling — útil para inspirarte en tus propias herramientas de depuración. |
| **Snes9x** | No es cycle-accurate (es más "scanline/batch-based"), pero es útil para contrastar la diferencia de complejidad y de trade-offs frente al enfoque que tú elegiste. |

---

## 8. Tecnologías y herramientas

- **Lenguaje**: C++ (C++20 recomendado: conceptos, `std::span`, mejoras de constexpr te van a facilitar mucho la tabla de opcodes y el manejo de bits).
- **Build system**: **CMake** (multiplataforma, estándar de facto, buen soporte en macOS con Xcode generator o Ninja).
- **Testing**: **Catch2** o **doctest** para tests unitarios del core (ver §9).
- **Frontend gráfico/audio/input**: **SDL2** (via FetchContent o Homebrew en Mac) — la opción más rápida para tener algo jugable en pantalla sin acoplarte a APIs de Apple desde el día 1.
- **Depuración**: merece la pena invertir pronto en un desensamblador del 65816 integrado (log de traza instrucción a instrucción) — es la herramienta #1 para diagnosticar por qué un juego real se cuelga o hace algo raro, y te permite comparar traza contra un emulador de referencia ya validado.
- **Control de versiones**: Git + GitHub, igual que tus otros proyectos.

---

## 9. Fases de diseño (roadmap)

### Fase 0 — Esqueleto y build
- Estructura de repo (§3), CMake funcionando, SDL2 enlazado, ventana vacía abriéndose en Mac.
- Loader de ROM: detección de cabecera, LoROM/HiROM, extracción de metadata básica.
- **Criterio de salida**: el binario arranca, carga una ROM, no crashea.

### Fase 1 — CPU 65816 aislada
- Implementar el set completo de instrucciones y modos de direccionamiento.
- Implementar el conteo de ciclos por instrucción (todavía sin scheduler real: puedes testear la CPU standalone contra un set de test ROMs de CPU pura).
- Desensamblador + trace logger desde ya.
- **Criterio de salida**: pasar los test ROMs de CPU 65816 conocidos de la comunidad (comparando traza ciclo a ciclo si es posible).

### Fase 2 — Bus mínimo + memoria + WRAM
- Mapeo de memoria LoROM/HiROM, WRAM, registros base.
- Sin PPU/APU reales todavía (se puede stubear).

### Fase 3 — Scheduler relativo CPU↔PPU
- Implementar el modelo de byuu: contador de delta CPU/PPU, `catch_up()`.
- PPU aún sin renderizado real, solo generando timing correcto de VBlank/HBlank/dots y disparando NMI/IRQ en el momento exacto.
- **Criterio de salida**: un test ROM sencillo que solo cuenta VBlanks/IRQs debe dar el conteo exacto esperado.

### Fase 4 — PPU: renderizado real
- Backgrounds (modos 0-6), sprites/OAM, luego Modo 7, luego color math y ventanas (de lo más simple a lo más intrincado, en ese orden).
- **Criterio de salida**: homebrew de test gráfico (hay test ROMs específicos de PPU) se renderiza correctamente pixel a pixel.

### Fase 5 — DMA y HDMA
- DMA general primero (más simple), luego HDMA (depende de que el timing por scanline de la Fase 3/4 ya sea exacto).
- **Criterio de salida**: efectos de raster típicos (gradiente de color por HDMA) se ven correctos.

### Fase 6 — APU: SMP + DSP
- Puede desarrollarse en paralelo a las fases 4-5 si te apetece alternar, dado que es bastante independiente una vez el bus de comunicación (`$2140-$2143`) existe.
- SMP primero (CPU de audio) validado con sus propios test ROMs si existen, luego DSP (síntesis, BRR, ADSR, eco).
- **Criterio de salida**: audio reconocible saliendo de un juego real.

### Fase 7 — Integración con juegos comerciales reales
- Aquí es donde salen a la luz todos los quirks no cubiertos por tests sintéticos: open bus, casos límite de sprites, timing fino de IRQ, etc.
- Recomendado: empezar por juegos técnicamente simples (Super Mario World es el "hola mundo" de facto de la comunidad de emuladores SNES) y subir en complejidad (Kirby's Dream Course, Yoshi's Island —usa SuperFX, mejor dejarlo para el final—, Star Fox —también chip especial—).
- **Criterio de salida**: catálogo creciente de juegos "jugables sin bugs visibles".

### Fase 8 (opcional, posterior) — Extras y chips especiales de cartucho
- **Chips especiales (SA-1, SuperFX, DSP-1)**: se dejan fuera a propósito hasta esta fase — ver §0.2 para el detalle de por qué cada uno rompe una asunción distinta del diseño base (SA-1 exige un tercer hilo cooperativo en el scheduler y modelar colisiones de memoria compartida; SuperFX exige un intérprete RISC nuevo desde cero; DSP-1 es el único de integración razonablemente simple, sin hilo propio). No se tocan hasta tener el core base (CPU 65816 + PPU + APU + DMA/HDMA + LoROM/HiROM) sólido y validado contra test ROMs — meterlos antes obligaría a generalizar el scheduler sin haber validado primero el caso simple de dos hilos.
- Orden recomendado si se abordan: DSP-1 primero (más aislado, sin hilo propio), luego SA-1, luego SuperFX (el más costoso, justifica por sí solo un sub-proyecto).
- Otros extras: save states, rewind, shaders de vídeo, netplay, libretro core.

---

## 10. Testing y validación de precisión

- **Test ROMs sintéticos** por componente (CPU, PPU timing, DMA) — primera línea de defensa, rápidos de ejecutar, dan un veredicto binario claro.
- **Trace comparison**: generar traza de ejecución instrucción a instrucción (PC, registros, ciclos) y compararla contra la traza de un emulador de referencia ya validado (bsnes en modo debug, por ejemplo) para el mismo punto de una ROM — divergencia = bug localizado con precisión quirúrgica.
- **Corpus de juegos comerciales** como test de integración de alto nivel, pero *nunca* como sustituto de los test ROMs sintéticos: un juego puede "verse bien" y aun así estar mal emulado en detalles que ese juego concreto no explota.

---

## 11. Cómo usar este documento con una IA (guía operativa)

Cuando pidas ayuda a una IA (Claude u otra) sobre una parte concreta del proyecto:

1. Indica **en qué fase** estás (§9) y **qué componente** exacto vas a tocar (§4).
2. Si el componente tiene una sección de documentación específica en §6, pide primero que se consulte esa fuente (fullsnes/SNESdev/datasheet 65816) antes de generar código — no code first, docs first.
3. Recuerda a la IA la decisión de arquitectura relevante de §1 (especialmente el modelo de scheduler, §1.2) para que cualquier código que proponga sea coherente con el catch-up relativo y no con un modelo scanline-based o de eventos absoluto.
4. Pide siempre que el código nuevo respete la frontera core/frontend de §1.4 y la estructura de carpetas de §3.
5. Para cualquier tabla de opcodes, timing de ciclos, o layout de registro (bit a bit), pide que se cite o contraste explícitamente contra fullsnes/SNESdev — son datos donde un error de un solo bit rompe compatibilidad con juegos reales, y no hay que fiarse de memoria genérica de "cómo funcionan los 6502-like".

---

## 12. Resumen ejecutivo (para no perderse)

- **Qué**: emulador de SNES en C++, precisión dot/cycle-accurate estilo higan.
- **Cómo se sincroniza todo**: scheduler relativo con catch-up, siguiendo el modelo de byuu.
- **Cómo se ejecuta el código del juego**: intérprete puro del 65816, sin JIT.
- **Cómo está organizado**: core (CPU+PPU+APU+bus+cartucho) totalmente aislado de un frontend en SDL2 para macOS.
- **Por dónde empezar de verdad**: Fase 0 (esqueleto) → Fase 1 (CPU 65816 aislada, validada con test ROMs) → Fase 3 (scheduler + timing PPU sin render) antes incluso de pintar un solo pixel — el orden importa porque el timing es la base de todo lo demás.
- **Documento de cabecera obligatorio**: fullsnes (nocash). Si hay duda sobre un registro o timing, la respuesta está ahí.
