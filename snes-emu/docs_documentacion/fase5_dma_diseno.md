# Fase 5 — DMA: diseño y plan de desarrollo (GP-DMA primero, luego HDMA)

Fecha: 2026-08-13
Referencias: `docs_documentacion/fullsnes.txt` (noSns v1.6, secciones "SNES DMA
Transfers" y "SNES Timing"), ares (`cpu/dma.cpp`, `cpu/hdma.cpp`) como
referencia de orden/timing/casos borde.

Objetivo: transferencias DMA/HDMA cycle-exactas sobre el scheduler relativo ya
existente (Fase 3). GP-DMA primero (motor compartido + validación), HDMA
después (disparo por HBlank + tabla indirecta).

---

## 0. Estado actual (qué ya existe)

- `core/src/bus/bus.cpp`: **registros de canal DMA `$4300-$437B` ya son storage
  R/W** (`dmaReg_[0x80]`), con el mirror `$43xF→$43xB`, `$43xC-$43xE` = open
  bus, y power/reset correctos (power = `FFh`, reset conserva `A1Bx`).
  `MDMAEN`/`HDMAEN` (`cpuReg_[0x0B]/[0x0C]`) se almacenan pero **no disparan nada**.
- `core/src/scheduler/scheduler.hpp`: scheduler relativo (CPU conductor,
  `delta_` int64). `step(n)` suma master cycles, `sync()` avanza la PPU 4 en 4.
- `core/src/cpu/cpu65816.cpp`: `read/write/idle` hacen `sync()` ANTES del acceso
  y `step(waitStates)` DESPUÉS.
- **NO cableado**: el bus no delega `$2100-$2133` (escribe) ni `$2134-$213F`
  (lee) a la PPU — solo sombras `ppuReg_` y lecturas a 0/open-bus. El DMA que
  escribe a VRAM/CGRAM/OAM lo necesita.

## 1. Decisiones de arquitectura

- **El motor DMA vive en el `Bus`** (ya tiene `dmaReg_`, `Cartridge&`, `Ppu&`).
  Añadimos `Scheduler&` al constructor del `Bus` (3er parámetro) para que el
  motor avance la PPU durante la transferencia.
- **Disparo síncrono**: `writeCpuRegister(0x0B)` (MDMAEN) con valor no nulo
  ejecuta el GP-DMA **bloqueando** (la CPU queda parada), y limpia los bits al
  terminar.
- **Timing**: cada byte = **8 master cycles** (2.68MHz, fullsnes). El motor hace
  `sync()` antes de cada unidad y `step(8 × bytes)` después, con un `sync()`
  final. (El alineamiento inicial a dot y el refresh de 40 master/scanline se
  dejan como refinamiento documentado, no bloquean.)
- **Escritura B-bus**: el motor escribe vía `mmioWrite(0x2100 + bbus, data)`.
  Hoy eso solo toca sombras para `$2100-$2133`; el cableado PPU es la Tarea 2.

## 2. Diseño del motor (GP-DMA)

Registro de canal `x` (base `dmaReg_ + x*16`, byte `r[i]`):

| offset | registro | uso GP-DMA |
|---|---|---|
| 0x0 | DMAPx | bit7 dir (0=A→B, 1=B→A), bits 4-3 step, bits 2-0 unidad |
| 0x1 | BBADx | dirección B-bus (`$21xx`) |
| 0x2/0x3/0x4 | A1TxL/H, A1Bx | dirección A-bus (banco + 16 bits) |
| 0x5/0x6 | DASxL/H | contador de bytes (0 = 10000h) |
| 0x7 | DASBx | (HDMA indirecto) |

Tabla de unidad (DMAPx bits 2-0 → bytes y patrón de dirección B-bus):

| unidad | bytes | direcciones B-bus |
|---|---|---|
| 0 | 1 | xx |
| 1 | 2 | xx, xx+1 |
| 2 | 2 | xx, xx |
| 3 | 4 | xx, xx, xx+1, xx+1 |
| 4 | 4 | xx, xx+1, xx+2, xx+3 |
| 5 | 4 | xx, xx+1, xx, xx+1 |
| 6 | 2 | xx, xx |
| 7 | 4 | xx, xx, xx+1, xx+1 |

Step A-bus (DMAPx bits 4-3): 0 = incremento, 1 = fijo, 2 = decremento, 3 = fijo.

---

## Tareas

### Tarea 1: test de registros de canal (verifica lo ya implementado)

**Files:** crear `core/tests/dma_tests.cpp`; añadir a `core/tests/CMakeLists.txt`.

- [ ] **1.1** Escribir test que verifica power-on (`FFh`), escritura/lectura,
  mirror `$43xF`, open bus `$43xC-$43xE`, y que `reset()` conserva `A1Bx`.
- [ ] **1.2** `cmake --build build --target snes_tests` → verde.

### Tarea 2: motor GP-DMA + disparo

**Files:** modificar `core/src/bus/bus.cpp`, `core/include/snes/snes.hpp`,
`core/src/system/system.cpp`.

**Interfaces:**
- Produce: `Bus(Cartridge&, Ppu&, Scheduler&)`; `Bus::dmaRun()`; `Bus::dmaTransfer(int)`.

- [ ] **2.1** Añadir `Scheduler&` al constructor de `Bus` y cablearlo en `System`.
- [ ] **2.2** Añadir tabla de unidad `dmaUnitBytes[]` y `dmaUnitBbus[]` (o un
  switch) en el bus.
- [ ] **2.3** Implementar `dmaTransfer(ch)`: bucle byte a byte con
  `sync()` + lectura A-bus / escritura B-bus + `step(8×bytes)`, `sync()` final.
- [ ] **2.4** `writeCpuRegister(0x0B)`: almacenar, y si `data != 0`, `dmaRun()`
  (canal 0→7, limpiando bits al terminar).
- [ ] **2.5** Build + suite existente verde.

### Tarea 3: test de ciclo exacto

- [ ] **3.1** Test: DMA de N bytes WRAM→puerto WRAM (`$2180`), verificar que los
  bytes aterrizan en el destino y que la PPU avanza exactamente `2N` dots
  (`N*8` master / 4).
- [ ] **3.2** Test: dirección A-bus con step (incremento/fijo/decremento) y
  unidad 2/4 bytes (VRAM `$2118` write-twice) en un segundo caso.
- [ ] **3.3** Build + verde.

### Tarea 4: cablear bus ↔ PPU B-bus

- [ ] **4.1** `mmioWrite` `$2100-$2133` → además de la sombra, `ppu_.writeRegister`.
- [ ] **4.2** `mmioRead` `$2134-$2136`/`$2138-$213F` → `ppu_.readRegister`; `$2137`
  sigue en el bus (open bus + `captureCounters`).
- [ ] **4.3** Test: DMA WRAM→VRAM (`$2118`/`$2119`) escribe VRAM real
  (verificable con `ppu.vramRead`).
- [ ] **4.4** Build + suite completa verde.

### Tarea 5 (Fase 5b): HDMA

- [ ] Disparo por HBlank (dot exacto de Fase 3), recarga de tabla en V=0,
  tabla directa/indirecta, repeat, "unidad por scanline".
- [ ] Test de gradiente por scanline (cambiar `$2100` color por línea).

---

## Criterio de éxito (Fase 5a)

- GP-DMA transfiere bytes correctos con ciclo exacto (8 master/byte).
- `cputest-full`/`cputest-basic` y la ROM de fase 3 siguen verdes (los ROMs no
  usan DMA, pero el cambio de constructor del Bus no debe romper nada).
