# Emulador de Super Nintendo (SNES)

Emulador de Super Nintendo escrito en **C++20**, con filosofía *dot/cycle-accurate* (al estilo higan/bsnes) y una arquitectura que separa por completo el **core** de emulación del **frontend** gráfico.

> Autor: Marcos Truchado Antón · Proyecto personal. Este repositorio **no incluye ROMs** (material con copyright), (nintendo dejame en paz).

---

##  Estado del proyecto

| | |
|---|---|
| **Estado** |  **En pruebas (beta)** — el emulador funciona, pero sigue en desarrollo activo |
| **Plataforma probada** | **Únicamente macOS con chip Apple Silicon (probado en M5)** |
| **Vídeo / Audio / Input** | SDL2 |

No se ha probado en Linux, Windows ni en Macs Intel: la compatibilidad con otras plataformas es esperable en el core (es C++ portable), pero **no está garantizada ni verificada**.

---

## 🎮 Compatibilidad (juegos verificados)

Juegos comprobados como **funcionales al 100%**:

| Juego | Estado | Notas |
|---|---|---|
| Alien vs Predator | Funcional | — |
| Donkey Kong Country 2 | Funcional | Las pantallas de carga tardan bastante, pero funcionan |
| Donkey Kong Country 3 | Funcional | Igual que DKC2: cargas lentas pero correctas |
| Final Fantasy | Funcional | Gráficos con algún bug visual puntual |
| Street Fighter 1 y 2 | Funcional | Bugs visuales en algunas pantallas |
| Super Mario Bros | Funcional | — |
| Super Mario Kart | Funcional | — |
| Top Gun | Funcional | — |
| Super Smash T.V. | Funcional | — |

El resto del catálogo de SNES **no ha sido validado**: un juego puede funcionar perfectamente, parcialmente o no arrancar. Los juegos que usan chips especiales de cartucho (SA-1, SuperFX, DSP-1, CX4…) **no están soportados todavía** (ver [Limitaciones](#-limitaciones-conocidas)).

---

##  Características

- **CPU Ricoh 5A22 (núcleo 65816)**: intérprete puro (sin JIT), set completo de instrucciones y modos de direccionamiento, conteo de ciclos exacto por instrucción, modo emulación/native, interrupciones NMI/IRQ.
- **Scheduler relativo**: modelo de sincronización por *catch-up* (estilo byuu/bsnes). La CPU actúa de "director de orquesta" y avanza al resto de chips justo lo necesario para mantenerlos sincronizados ciclo a ciclo.
- **PPU dot-accurate**: modos de fondo 0–7 (incluido Modo 7 con transformación afín), sprites/OAM (128 sprites, límites reales de 32 sprites y 34 tiles por línea), ventanas, color math (suma/resta/halve/fixed color), mosaic, hires/pseudoHires, interlace.
- **DMA + HDMA**: los 8 canales DMA generales y HDMA (tablas directas e indirectas, modos single/repeat), base de los efectos raster.
- **APU completa**: S-SMP (SPC700, 256 opcodes, 64 KB de RAM propia, boot ROM, timers) + S-DSP (8 voces, decodificación BRR/ADPCM, envolventes ADSR/gain, mezclador estéreo a 32 kHz).
- **Bus completo**: mapeo LoROM / HiROM / ExHiROM, WRAM 128 KB con mirrors, SRAM con pila, registros MMIO, *open bus*, joypad auto-leído y manual.
- **Save states**: guardar/cargar el estado completo de la máquina en cualquier momento.
- **Frontend SDL2** con lanzador propio estilo retro: lista los juegos de tu carpeta, navegación con teclado o mando, pantalla completa y escalado 4:3 auténtico.
- **Suite de tests**: más de 120 casos de prueba (~270.000 aserciones) con doctest, más el harness `cputest` (1.107 tests comunitarios de CPU, todos en verde).

---

##  Arquitectura del proyecto

### Principio fundamental: core desacoplado del frontend

El proyecto se divide en dos módulos con una frontera dura:

```
┌─────────────────────────────────────────────────────────────┐
│                    FRONTEND (SDL2 / macOS)                  │
│   ventana · vídeo · audio · teclado/mando · save states     │
│                          ▲                                  │
│                          │  API pública (snes.hpp)          │
│                          ▼                                  │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                     CORE (librería estática)                │
│                                                             │
│   ┌─────────┐   ┌──────────┐   ┌─────────┐   ┌──────────┐   │
│   │ CPU     │──▶│ Scheduler│──▶│ PPU     │   │ APU      │   │
│   │ 65816   │   │ (relojes)│   │ (vídeo) │   │ SMP+DSP  │   │
│   └────┬────┘   └──────────┘   └────▲────┘   └────▲─────┘   │
│        │                            │             │         │
│        └────────────┬───────────────┴─────────────┘         │
│                     ▼                                       │
│              ┌──────────────┐      ┌────────────┐           │
│              │ Bus (memoria,│◀────▶│ Cartridge  │           │
│              │ MMIO, DMA)   │      │ (mappers)  │           │
│              └──────────────┘      └────────────┘           │
└─────────────────────────────────────────────────────────────┘
```

- El **core** (`core/`) es una librería estática sin ninguna dependencia de plataforma: no incluye SDL2 ni cabeceras del sistema operativo. Expone una API mínima y limpia en `core/include/snes/snes.hpp`: cargar ROM, ejecutar instrucción/frame, leer framebuffer, leer buffer de audio, inyectar input, guardar/cargar estado.
- El **frontend** (`frontend/`) es el único módulo que toca librerías de plataforma. Se encarga de ventana, refresco de vídeo, salida de audio, mapeo de teclado/mando y persistencia.
- Esta separación (misma filosofía que un *libretro core*) permite en el futuro añadir otros frontends sin tocar el core.

### El scheduler: el corazón del emulador

El SNES real tiene varios relojes distintos que deben ejecutarse **en orden exacto**, porque los juegos cambian scroll, paletas o interrupciones a mitad de línea:

- Reloj maestro NTSC: ~21,477 MHz. La CPU corre a divisores de ese reloj (6/8/12 ciclos maestro por acceso, según región de memoria).
- La PPU avanza **4 ciclos maestro por dot (píxel)**: 341 dots por scanline × 262 scanlines por frame.
- La APU tiene su **propio oscilador** (~24,576 MHz); se modela con la razón clásica 21:24 respecto al reloj maestro.

En lugar de alternar 1 ciclo de cada chip (carísimo) o ejecutar frames completos por separado (rompe efectos raster), se usa el modelo de **scheduler relativo con catch-up**:

1. Cada componente es un "hilo cooperativo" (`Thread`) con su contador de ciclos.
2. La CPU ejecuta una instrucción; cada acceso a memoria consulta sus waitstates y notifica al scheduler.
3. Cuando alguien necesita el estado actualizado de otro componente, se llama a su `catch_up()`: ese chip avanza tantos ciclos como haga falta hasta quedar exactamente sincronizado.

Esto reproduce fielmente el comportamiento temporal real: NMI/IRQ disparan en el dot exacto, HDMA transfiere en cada HBlank y los efectos raster salen "gratis" del modelo.

### Componentes del core

| Módulo | Ruta | Qué modela |
|---|---|---|
| **CPU** | `core/src/cpu/` | 65816 completo: tabla de 256 opcodes expandida según flags M/X (8/16 bits), ciclos exactos heredados de la tradición higan, desensamblador integrado para depurar |
| **PPU** | `core/src/ppu/` | Timing por dots + pipeline de render por línea (fondos, sprites, ventanas, compositor/color math). Framebuffer interno de 564×242 en RGB555 |
| **APU** | `core/src/apu/` | SPC700 (intérprete completo) + DSP (BRR, ADSR, mezclador estéreo 32 kHz) + RAM de audio privada |
| **Bus** | `core/src/bus/` | Enrutado de memoria por banco/dirección, WRAM/SRAM, registros MMIO, open bus, DMA y HDMA |
| **Cartridge** | `core/src/cartridge/` | Carga de ROM, detección de cabecera de copiadora (512 B), detección heurística LoROM/HiROM/ExHiROM, tamaño de SRAM |
| **Scheduler** | `core/src/scheduler/` | Delta relativo entre hilos cooperativos (CPU↔PPU, CPU↔APU) |
| **Serialize** | `core/src/serialize/` | Serializador binario mínimo usado por los save states |

### Flujo de un frame

```
Frontend (60 fps) ──▶ System::step()
                         │
                         ├─ CPU ejecuta 1 instrucción (ciclos exactos)
                         ├─ Scheduler sincroniza: PPU avanza dot a dot
                         │    └─ VBlank → NMI · H/V-IRQ · HDMA en HBlank
                         └─ APU avanza (SMP + DSP → buffer de audio)
                         
Frontend lee: framebuffer (pixelColor) + muestras (readAudio)
```

El frontend pide un frame (`renderedFrames()`), el core ejecuta instrucciones hasta completarlo, y luego el frontend vuelca el framebuffer a una textura SDL y el audio al dispositivo de sonido.

---

## 🔧 Requisitos y compilación (macOS)

Requisitos:

- macOS (probado en un M5 (mac air))
- Xcode Command Line Tools (clang con C++20)
- [CMake](https://cmake.org) ≥ 3.20
- [SDL2](https://www.libsdl.org) (vía Homebrew)
-> Hare un docker
```bash
# Dependencias
brew install cmake sdl2

# Compilar
cd snes-emu
cmake -S . -B build
cmake --build build

# Ejecutar
./build/frontend/snes_frontend                # abre la interfaz grafica
./build/frontend/snes_frontend ruta/juego.sfc # carga directa de una ROM (juego)
```

> **Nota sobre el lanzador:** el menú lista las ROMs `.smc/.sfc` de una carpeta configurada en `frontend/src/main.cpp` (constante `kGamesDir`). Ajústala a tu carpeta de juegos o usa el modo de carga directa pasando la ROM como argumento. Las ROMs **no están incluidas** en el repositorio.

---

##  Controles

### Teclado

| Tecla | Acción |
|---|---|
| Flechas / WASD | Dirección |
| `X` o `Espacio` | A |
| `Z` o `Q` | B |
| `V` | X |
| `C` | Y |
| `U` | L |
| `I` | R |
| `Enter` | Start |
| `Shift` der. o `E` | Select |
| `F5` / `F8` | Guardar / cargar estado |
| `F11` | Pantalla completa |
| `Esc` | Volver al menú / salir |

### Mando (solo probe xbox, vía SDL GameController)

Mapeo por posición física, igual que el pad original de SNES:

| Xbox | SNES |
|---|---|
| `A` (abajo-derecha) | B |
| `B` (abajo-izquierda) | A |
| `Y` (arriba-derecha) | X |
| `X` (arriba-izquierda) | Y |
| `LB` / `RB` | L / R |
| `Menu` / `View` | Start / Select |
| D-pad o stick izq. | Direcciones |

Atajos en juego: `View + RB` guardar estado · `View + LB` cargar estado · `View + Menu` volver al menú.

En el lanzador: navegar con D-pad/stick, lanzar con el botón derecho (posición SNES `A`), salir con el botón inferior.

Los mandos tienen **hotplug**: se pueden conectar/desconectar en caliente.

---

## Save states

- `F5` guarda el estado completo (CPU + PPU + APU + bus + scheduler) en un archivo `.ss` junto a la ROM.
- `F8` lo restaura.
- Formato binario propio (magic `SNSS`) con validación: si el archivo está truncado o corrupto, la carga se rechaza de forma segura.

---

## Tests y validación

La precisión no se valida solo "viendo si el juego se ve bien":

```bash
cd snes-emu
cmake --build build --target snes_tests
./build/core/tests/snes_tests        # suite doctest: >120 TEST_CASEs (de momento) / ~270k aserciones
```

Además:

- **`tools/cputest`**: harness que ejecuta la ROM de tests comunitarios de CPU (`cputest-full.sfc`, 1.107 casos) — todos en verde.
- **`tools/fase3`**: ROM de timing generada en tiempo de compilación (cuenta VBlanks, NMI, IRQ y HBlanks con valores exactos).
- **`tools/aputrace`, `tools/ff2trace`**: utilidades de trazas para depurar APU y juegos concretos.

Cobertura de la suite: CPU (opcodes, ciclos, interrupciones), cartridge (cabeceras/mappers), bus (mapa de memoria, MMIO, open bus), scheduler, PPU (timing, fondos modos 0–7, sprites/OAM, ventanas, color math, modo 7), DMA/HDMA, APU (boot ROM, BRR) y save states (identidad byte a byte tras snapshot+load).

---

## Estructura del repo 

```
emulador_supernintendo/
├── README.md                  ← este documento
├── emulador_snes_diseno.md    ← documento de diseño y hoja de ruta original
├── snes-emu/                  ← el proyecto
│   ├── CMakeLists.txt         ← build raíz (C++20, warnings estrictos)
│   ├── core/                  ← EL CORE (sin dependencias de plataforma)
│   │   ├── include/snes/snes.hpp   ← API pública
│   │   ├── src/cpu/           ← 65816 (opcodes, addressing, disasm)
│   │   ├── src/ppu/           ← timing + render (fondos/sprites/mode7/color math)
│   │   ├── src/apu/           ← SPC700 + DSP
│   │   ├── src/bus/           ← memoria, MMIO, DMA/HDMA
│   │   ├── src/cartridge/     ← loader + mappers
│   │   ├── src/scheduler/     ← hilos cooperativos + catch-up
│   │   ├── src/system/        ← fachada System (load/reset/step/run)
│   │   ├── src/serialize/     ← serializador de save states
│   │   └── tests/             ← suite doctest
│   ├── frontend/              ← frontend SDL2 (ventana, audio, input, lanzador)
│   ├── tools/                 ← cputest, fase3, aputrace, ff2trace
│   └── third_party/           ← doctest, cputest
└── juegos/                    ← tus ROMs (ignoradas por git, no incluidas)
```

---

## Limitaciones actuales

- **Solo NTSC** (60 Hz): PAL no implementado.
- **Chips especiales de cartucho no soportados**: SA-1 (*Kirby Super Star*, *Super Mario RPG*…), SuperFX (*Star Fox*, *Yoshi's Island*…), DSP-1, CX4, S-DD1, SPC7110. Esos juegos no funcionarán.
- **Audio**: falta eco/reverb, interpolación gaussiana, noise y pitch modulation; ADSR simplificado. El sonido base (BRR + envelopes) sí funciona.
- **Gráficos**: algún bug visual puntual en *Final Fantasy* y en algunas pantallas de *Street Fighter*.
- **Cargas lentas** en *Donkey Kong Country 2/3*: funcionan, pero las pantallas de carga tardan más que en hardware real.
- Detalles de timing fino pendientes (líneas largas/cortas, refresh DRAM, etc.), documentados en `docs_documentacion/`.

---

##  Hoja de ruta a futuro (21 de agosto 2026)

- [x] Fase 0 — Esqueleto, build, loader de ROM
- [x] Fase 1 — CPU 65816 aislada (validada con 1.107 tests comunitarios)
- [x] Fase 2 — Bus, memoria, WRAM/SRAM, MMIO, open bus
- [x] Fase 3 — Scheduler relativo + timing PPU + NMI/IRQ reales
- [x] Fase 4 — Render PPU completo (fondos, sprites, ventanas, color math, modo 7)
- [x] Fase 5 — DMA y HDMA
- [x] Fase 6 — APU: SPC700 + DSP
- [x] Fase 7 — Integración: frontend SDL2, audio, input, save states
- [ ] Fase 8 — Chips especiales de cartucho (DSP-1 → SA-1 → SuperFX)
- [ ] Mejoras de audio (eco/reverb, gaussiana, noise, PMON)
- [ ] Validación contra más juegos comerciales

---

## Referencias

Proyecto imposible sin la documentación de la comunidad:

- **fullsnes** (nocash) — la referencia hardware registro a registro.
- **SNESdev wiki** — snes.nesdev.org.
- WDC 65C816 datasheet + *"Programming the 65816"* (Eyes & Lichty).
- *"Emulator Schedulers"* (byuu) — el modelo de scheduler relativo implementado aquí.
- Código fuente de **bsnes/higan** y **ares** — referencia de arquitectura dot-accurate.

## Licencia

Proyecto personal hecho por gusto. Este proyecto no incluye ni distribuye material propietario (no incluye juegos (ROMs)).
