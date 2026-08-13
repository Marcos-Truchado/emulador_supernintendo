# Estado del proyecto — Fases 3/3b + 4 (PPU rendering, en curso)

**Última actualización:** 2026-08-13

Este documento es el punto de recuperación de contexto para cualquier IA que
continúe el proyecto. Léelo íntegro **antes** de tocar el código. La fuente de
verdad del diseño es `/Users/matru/Desktop/emulador_supernintendo/emulador_snes_diseno.md`
(documento de diseño y hoja de ruta del proyecto).

---

## 0. Resumen ejecutivo

El proyecto es un emulador de SNES en C++20 estilo higan/bsnes
(dot/cycle-accurate), con el core totalmente desacoplado del frontend.
Completadas: **Fase 0 (esqueleto/build)** + **Fase 1 (CPU 65816 aislada)** +
**Fase 2 (bus mínimo + memoria + WRAM)** + **Fase 3 (scheduler + PPU
timing-only)** + **Fase 3b (entrega real de NMI/IRQ al CPU)** + **Fase 4
(rendering PPU completo: fondos, sprites, ventanas, color math, modo 7)** +
**Fase 5 (DMA/HDMA)**.
Próximas: **Fase 6 (APU: SMP+DSP)**, **Fase 7 (integración con juegos
comerciales)** — ver §12.

Estado actual:

- ✅ Core completo portado del WDC65816 de higan/bsnes (fuente de referencia en
  `/tmp/wdc/`): conjunto de instrucciones completo, modos de direccionamiento,
  conteo de ciclos exacto por instrucción (ciclo por ciclo).
- ✅ Desensamblador integrado + trace helpers.
- ✅ **Fase 2**: bus completo LoROM/HiROM/ExHiROM + WRAM 128KB con
  mirrors + SRAM (tamaño desde cabecera `$FFD8`) + registros base MMIO (mult/div
  `$4202-$4206`, WRAM port `$2180-$2183`, sombras PPU `$2100-$2133`, puertos
  APU `$2140-$217F`, registros DMA `$4300-$437F`, joypad stub) + open bus
  (`lastData_` + `latch`) + valores power/reset según fullsnes.
- ✅ Cartridge loader con detección de cabecera (copier 512B), LoROM/HiROM/ExHiROM,
  `sramSize()` desde el byte de RAM de cabecera.
- ✅ Harness de test `tools/cputest` que ejecuta `cputest-full.sfc`
  (1107 tests de CPU comunitarios) y `cputest-basic.sfc`.
- ✅ **`cputest-full` y `cputest-basic` PASAN**: `SUCCESS: reached 0081a2`.
- ✅ **Fase 3 (nuevo)**: scheduler relativo byuu (delta `int64`, sync de 4 en 4
  master = 1 dot) + PPU timing-only (dot/scanline/frame NTSC, tabla de eventos
  fullsnes H/V, flags `$4210` latched read/ack vs `$4212` live mirror, IRQ
  register-note modos 1/2/3, HTIME/VTIME bracketed) + waitstates por región
  (WS1 8, WS2 8→6 con `$420D`, WRAM/SRAM/exp. 8, I/O 6, joypad 12) con
  `sync()` antes del acceso y `step(waitStates)` después.
- ✅ **Test ROM propio** `tools/fase3` (generada en build-time): VBlank x10,
  V-IRQ x10, NMI latch `$4210`==0x82 x10, HBlank 27/200 — pasa.
- ✅ **Tests unitarios**: `snes_tests` = **47 TEST_CASE / 269181 assertions**
  (CPU 8, cartridge 7, bus 13, scheduler 3, ppu timing 12 + integración 3 +
  interrupt 3) pasan 100%. Ver §8.2 para erratas del diseño fase3 (HTIME/VTIME
  power = 0x1FF, eventos post-incremento, `$4212` bit6 en H=0 = 1).
- ✅ **Fase 3b (nuevo)**: entrega real de NMI/IRQ al CPU — edge-detect interno
  del flag NMI en la PPU (`NMITIMEN.7 AND latch $4210`, fullsnes), pin IRQ como
  espejo de nivel del latch `$4211`, sinks `std::function` cableados en
  `System` → `Cpu65816::setNmi/setIrq`. Vectores + push/pull + RTI ya
  existían en el CPU y se validan de extremo a extremo con ROMs mínimas
  LoROM (saltar al vector, RTI vuelve, NMI 1/frame, IRQ nivel re-dispara sin
  ack, read `$4211` limita a 1/frame). Ver §9.

Fase 3 + 3b COMPLETADAS. Próximos pasos (Fase 4): rendering PPU (consultar
`fullsnes.txt`/`fullsnes.html` antes de implementar timing de CPU/PPU/APU/DMA);
validar con blargg `nmi_test`/`irq_test` si se quiere contraste extra.

---

## 1. Estructura de repo actual

```
snes-emu/
├── CMakeLists.txt                 # build raíz (add_subdirectory core, tools)
├── docs/
│   └── update.md                  # ESTE DOCUMENTO (estado + continuación)
├── core/
│   ├── CMakeLists.txt             # lib estática snes_core (+ src cpu/system)
│   ├── include/snes/snes.hpp      # API pública del core (Memory, Cartridge, Bus, System)
│   ├── src/
│   │   ├── bus/bus.cpp            # Bus completo: LoROM/HiROM/ExHiROM + WRAM/SRAM + MMIO
│   │   ├── cartridge/cartridge.cpp # Loader + detección de mapeo/cabecera + sramSize()
│   │   ├── system/system.cpp     # Facade System: load/reset/step/run
│   │   ├── cpu/
│   │   │   ├── cpu65816.hpp        # NUCLÍ del core de la CPU (registros, flags, dispatch)
│   │   │   ├── cpu65816.cpp        # reset/power/execute + accessors
│   │   │   ├── algorithms.cpp        # ADC/SBC/AND/.. (8/16 bit)
│   │   │   ├── opcodes.cpp          # dispatch 4-vías (M/X)
│   │   │   ├── instruction.hpp       # tabla de opcodes 256, expandida 4 veces
│   │   │   ├── instructions-read.cpp / -write.cpp / -modify.cpp / -pc.cpp / -other.cpp
│   │   │   └── disassembler.cpp      # desensamblador + opcodeName
│   │   └── (ppu/, apu/, scheduler/, input/ — carpetas vacías, FASE 2+)
│   └── tests/                     # doctest: main.cpp + cartridge_tests.cpp
├── tools/
│   └── cputest/
│       ├── cputest.cpp            # harness que ejecuta cputest-full.sfc
│       └── CMakeLists.txt
├── frontend/                      # vacío (Fase posterior, fuera de alcance)
└── third_party/                   # doctest
```

---

## 2. API pública (core/include/snes/snes.hpp)

- `class Memory` — interfaz 24-bit de memoria: `read(uint24)`, `write(uint24, uint8)`.
- `class Cartridge` — `load(filename|vector)`, `mapMode()`, `rom()`, `romSize()`,
  `hasCopierHeader()`, `sramSize()`, `romOffset(uint24)` (offset bruto; -1 = ventana
  sin ROM; el wrapping módulo tamaño es responsabilidad del bus, como una mask ROM).
- `class Bus : public Memory` — `read/write` con el mapa completo por modo de
  mapeo + `power()`/`reset()` (valores fullsnes, bracketed vs un-bracketed),
  `sram()` (buffer de batería), `openBus()` (`lastData_`), acceso de test a
  registros: `ppuRegister()`, `apuPort()`, `cpuRegister()`, `dmaRegister()`,
  `wramAddress()`.
- `class System` — `load()` (llama `bus_->power()` + `cpu_->power()` + `cpu_->reset()`),
  `reset()` (llama `bus_->reset()` + `cpu_->reset()`), `step()` (1 instrucción,
  devuelve ciclos), `run(maxCycles)`, accessors `cpu()`, `bus()`, `cartridge()`.
- Tipos: `uint8/int8/uint16/int16/uint24 (uint32)/uint32/uint64`.

## 6. Fase 2 — Bus mínimo + memoria + WRAM (diseño implementado)

### 6.1 Mapa de memoria por modo de mapeo

Sistema (todos los modos), bancos `$00-3F/$80-BF`, offset:
- `$0000-$1FFF` → 8KB mirror de WRAM; `$2000-$20FF` y `$2200-$3FFF` → mirrors
  de WRAM (comportamiento de Fase 1, conservado).
- `$2100-$21FF` → puertos B-Bus (PPU/APU/WRAM port).
- `$4000-$43FF` → registros CPU on-chip; `$4400-$5FFF` → open bus.
- `$6000-$7FFF` → SRAM (HiROM `$20-3F/$A0-BF`; ExHiROM `$80-BF`), si no hay SRAM
  → open bus. `$8000-$FFFF` → ROM.

WRAM 128KB: `$7E:0000-FFFF` y `$7F:0000-FFFF`; HiROM además mirrors completos
en `$3E-3F/$BE-BF`.

Cartucho (window 64KB), según `cartridge_.mapMode()`:
- **LoROM**: `$40-7D/$C0-FF:0000-FFFF` → ROM lineal `(bank & 0x3F) << 16 + offs`
  (los bancos 40-5F son espejo de `$00-3F:8000`, 60-7D continúan). SRAM en
  `$70-7D/$F0-FF:0000-7FFF`.
- **HiROM**: `$40-7D:0000-FFFF` → ROM; `$C0-FF` → espejo; bancos sistema
  `:8000-FFFF` → espejo de `$40-7D:0000-7FFF`. SRAM en `$20-3F/$A0-BF:6000-7FFF`.
- **ExHiROM**: `$40-7D` → primeros 4MB, `$C0-FF` → siguientes 4MB; sistema
  `:8000-FFFF` → espejo de `$40-7D:0000-7FFF`. Sin SRAM.

Open bus: toda lectura no mapeada devuelve `lastData_` (último byte en el bus
de datos, tracks de writes y reads). Las ROMs de tamaño potencia de dos envuelven
módulo tamaño (comportamiento de mask ROM real).

### 6.2 Registros MMIO implementados (Fase 2)

- `$4200` NMITIMEN / `$4201` WRIO / `$4207-420A` HTIME/VTIME: almacenamiento
  (timing es Fase 3+).
- `$4202/$4203` WRMPYA/WRMPYB: multiplicación 8×8 → `$4216/$4217`; quirk de
  fullsnes: también escribe RDDIVL=WRMPYB, RDDIVH=0.
- `$4204-$4206` WRDIV: división 16/8 → cociente `$4214/$4215`, resto
  `$4216/$4217`; división por cero: cociente `$FFFF`, resto = dividendo.
- `$2180` puerto WRAM (r/w, auto-incremento, dirección 17-bit enmascarada a
  128KB), `$2181-$2183` WMADDL/M/WRIT.
- `$2100-$2133` PPU write-only → sombras (`ppuReg_`); reads → open bus.
- `$2140-$217F` puertos APU (`apuPort_`, espejos incluidos) — almacenamiento.
- `$4300-$437F` DMA (8 canales × 16B, `dmaReg_`): DMAP/A1TxL/H/A1Bx/DASxL/H/
  DASBx + mirror `$43xF`→`$43xB` (mismo canal). Lecturas `$43xC-$43xE` → open bus.
- `$4016/$4017`, `$4218-$421F` joypad → 0 (stub, necesario para el fail-detect
  del cputest).
- `$4210` RDNMI: versión CPU 2 (bits 3-0) + flag vblank alternado (bit7) para
  `wait_for_vblank`; `$4211` TIMEUP alterna igual; `$4212` HVBJOY → 0.
- `$2134-$2136` (resultado PPU1 mult) → 0; `$2137` → open bus; `$2138-$213F` → 0.

### 6.3 power()/reset() (fullsnes columna derecha)

- `power()`: tamaño SRAM desde `cartridge_.sramSize()` y la limpia; `lastData_=0`;
  registros DMA → `$FF` (A1Bx indefinido = `xxh` en fullsnes → `$FF`); WRIO/HTIME/
  VTIME → `$FF/$01`; RDDIV/RDMPY → 0.
- `reset()`: solo valores sin corchetes de fullsnes: NMITIMEN→0, WRIO→`$FF`,
  HTIME/VTIME→`$FF/$01`, MDMAEN/HDMAEN/MEMSEL→0, DMAPx→`$FF` (A1Bx se conserva).
- `System::load()` llama `bus_->power()` antes de `cpu_->power()`; `System::reset()`
  llama `bus_->reset()` antes de `cpu_->reset()`.

## 4. CPU 65816 — diseño e implementación

### 4.1 Modelo de registros
- `A/X/Y/S/D` son `r16` (union 16-bit con vistas `.l`/`.h`), `S` es el stack.
- `PC` es `r24` (`.d` = 24-bit, `.b` = banco, `.w` = offset 16-bit).
- `P8 P` — status register como campos de bits (`c,z,i,d,x,m,v,n`) con
  conversión a `uint8` y asignación desde `uint8`.
- Macros de acceso: `CF`, `ZF`, `IF`, `DF`, `XF`, `MF`, `VF`, `NF`, `EF` (E).
- `E` emulación mode, `irq`, `wai`, `stp`, `vector`.

### 4.2 Ciclos y memoria
- **Todo acceso a memoria y todo ciclo "dummy" = 1 ciclo de CPU**
  (las ROMs de test asumen scrolling slow): cada `read/write/idle/fetch/push/
  pull/readDirect/readDirectN/readStack/etc.` incrementa `cycles_`.
- Tabla de ciclos **exacta** heredada de higan `memory.cpp`:
  - `idleIRQ()`: salvo interrupción pendiente, `idle()`.
  - `idle2()`: `if (D.l) idle()`.
  - `idle4(x,y)`: `if (!XF || x>>8 != y>>8) idle()`.
  - `idle6(addr)`: `if (EF && PC.h != addr>>8) idle()`.
  - `readDirect(off)`/`writeDirect(off,data)`: `if (EF && !D.l) ... D.w | uint8(off)`,
    si no `uint16(D.w + off)`.
  - `readDirectN(off)`: nunca suma D, wrapping a 16 bits.
  - `readBank/writeBank`: `(B << 16) + addr` — **importante**: el `addr` NO se
    mascara a 16 bits; así un reader de una palabra que cruza hasta el banco
    ($FFFF+1 → siguiente banco) funciona. (Bug de `test0003` arreglado al
    eliminar el `& 0xffff`).
  - `readStack/writeStack`: `uint16(S.w + off)`.
  - `fetch()`: `read((PC.b << 16) | PC.w++)`.
  - `push(data)`: `EF ? S.l-- : S.w--`; `pull()`: `S.w++` luego leer.
  - `pushN/pullN`: sin wrap en modo E.

### 4.3 Execución
- `power()`: PC=0, A/X/Y=0, S=$01FF, D=0, B=0, P=$34 (E=1), vector=$FFFC,
  limpia pins `nmi`/`irq`/`wai`/`stp`.
- `reset()`: P=$34 E=1, M/X=1 forzados, X/Y.h=0, S.h=0x01, lee vector de reset
  (`$FFFC`) de memoria para PC. También limpia los pins.
- `execute()`: resetea `cycles_` y despacha en orden: STP (idle infinito,
  1 ciclo/step), WAI aparcado (1 ciclo/step hasta interrupción; al despertar
  un `idle()` extra), luego NMI > IRQ (`vector = EF ? $FFFA/$FFFE : $FFEA/$FFEE`),
  si no, `instruction()`.
- `interrupt()`: `read(PC.d)` + `idle()` (preludio), push PBR (solo native),
  PCH, PCL, P (`P & ~0x10` en modo E), `IF=1`, `DF=0`, carga el vector.
- `interruptPending()` = `nmi || (irq && !IF)` (IRQ gateada por I).
- `stopped()` = `stp`, `waiting()` = `wai`.

### 4.4 Dispatch 4-vías (opcodes.cpp)
Modelo clásico de higan: una tabla de 256 opcodes (`instruction.hpp`) expandida
por macros según M/X (8/16-bit). El esquema:
- `opA`: instrucciones no afectadas por M/X.
- `opM`/`opX`: con sufijo `8`/`16` según M/X.
- La tabla se incluye 4 veces dentro de `instruction()`.

El `instruction.hpp` es la tabla literal (casos del switch), no un .cpp.

### 4.5 Datos de tests y verificación
- Fuente de test ROMs: `/tmp/wdc/` (copia de referencia de higan).
- ROM standalone para CPU: `cputest-full.sfc` (LoROM, cabecera `$30`,
  262144 bytes, no copier header, vector reset `$8000`).
  - `wait_for_vblank`, `init_mem` escriben STP en $1000/$1004/$1008/$100C
    (handlers de BRK/COP errantes).
  - `init_test`, `save_results` guardan registros en WRAM (ZEROPAGE); el
    harness lee el layout:
    - `+$10`: `test_num` (word)
    - `+$12`: `result_a`, `+$14`: `result_x`, `+$16`: `result_y`,
      `+$18`: `result_p` (word), `+$1A`: `result_s`, `+$1C`: `result_d`,
      `+$1E`: `result_dbr`.
  - `success` handler: `jsl success → ... → @end: jmp @end` en `$0081A2`
    (auto-loop; detectado por el harness).
  - `fail`: escribe registros y entra en `wait_for_key`, que hace
    `bit $4212 → bne` y `bit $4218 → bmi` hasta que un joypad A se pulsa;
    con el bus stub (garage 0) se queda infinitamente espinado en `$008240-$82 47`.
    El harness trata eso como fallo.
- **Harness** `tools/cputest/cputest.cpp`: carga con `system.load`, `reset`,
  bucle `step()`. Para `$0081A2` → SUCCESS. Si PC cae en el loop de espera
  de `wait_for_key` (`0x008240`) → FAIL y dump de registros almacenados.
- **Resultado actual**: **PASSA** los 1107 tests (SUCCESS en `$0081A2`).
  Dump de éxito: `test_num=1609 result_a=cc5a result_x=3456 result_y=5678
  result_p=0028`.

## 5. Historial de fallos y correcciones (sesión actual)

### Bug #1 — `readBank`/`writeBank` masking 16-bit (test0003)
**Síntoma**: cputest bloqueaba en el test 3 (`adc ($10,x)` con B=$7E, D=$FF00,
X=0x0123), A estaba mal y arrastró el error hasta el 3.

**Causa raíz**: `readBank(uint24 address)` y `writeBank` montaban
`(B << 16) + (address & 0xFFFF)`. El masking a 16 bit rompía el cruce del
operando hacia el siguiente banco: leer la 2ª mitad de una word en `$xxFFFF`
debía ir a `$B:0000` del siguiente banco. El dieno de referencia no aplica
mascara, deja que `read()` reciba el 24-bits completo.

**Fix**: quitar el `& 0xFFFF`:
```
auto Cpu65816::readBank(uint24 address) -> uint8 { return read((B << 16) + address); }
```
Ahora pasa `test0003` y el harness continúa hasta test0039.

### Bug 2 — test0039 `adc ($10,s),y` → RESUELTO
**Síntoma**: el harness fallaba en `test_num=39` en `wait_for_key`:
```
stored: test_num=39 result_a=1113 result_x=00ee result_y=0078 result_p=0130
```
`result_a=0x1113` (sumaba 1 de más) y `result_p=0x0130` (faltaban N y Z).

**Causa raíz**: el test nº39 real de la ROM (addr ~$009032) era
`adc ($f7,x)` en **modo emulación** con `D=$01AA, X=$EE`. La ROM NO coincide
con `tests-full.inc` (que usa `61 F6`; la ROM tiene `61 F7`). Se trata de la
**quirk documentada de `(direct,X)` en modo E** (ver Bug 4). El `result_a`
extraño era un efecto aguas abajo: el puntero leído era incorrecto y el ADC
sumaba datos basura; N/Z no coincidían con lo esperado.

**Fix**: implementar la quirk `(dp,X)` con `readDirectX` (Bug 4). Al corregirla,
el test 39 (y todos los siguientes hasta el 984) pasó.

### Bug 3 — PLB en modo emulación con S=$01FF (quirk) → RESUELTO
**Síntoma**: `test_num=985` (Test 03d9: `plb`, E=1, S=$01FF, `($000200)=$3d`,
esperado DBR=$3d) fallaba en `wait_for_key`.

**Causa raíz**: `instructionPullB` usaba `pull()` que en modo E envuelve el
stack en $01FF (S.l++): con S=$01FF leía de $0100 en vez de $0200. El manual
no lista PLB entre las instrucciones que pueden salir de la página de stack,
pero el hardware real lee de $0200 (documentado en el README del cputest).

**Fix** (`instructions-other.cpp`): usar `pullN()` (sin wrap en E) y restaurar
`S.h = 0x01` tras el pull, igual que PLD:
```cpp
B = pullN();
ZF = B == 0;
NF = B & 0x80;
if (EF) S.h = 0x01;
```

### Bug 4 — Quirk `(direct,X)` en modo emulación con D.l != 0 → RESUELTO
**Síntoma**: base del Bug 2. El modo `(dp,X)` leía el byte alto del puntero en
la dirección equivocada.

**Causa raíz**: comportamiento no documentado del 65816 real (validado por el
cputest y comentado en su README): con E=1 y `D&$FF != 0`, el byte bajo del
puntero se lee de `dp + X + D` (suma completa, sin wrap) pero el byte alto se
lee de `dp + X + D + 1` con el `+1` **envolviendo dentro de la página**. Ej:
E=1, D=$11A, X=$EE, `lda ($F7,X)` → $F7+$11A+$EE=$2FF; bajo en $2FF, alto en
$200 (no $300). Solo aplica a este modo de direccionamiento (leer y escribir).

**Fix** (`cpu65816.cpp`/`.hpp` + `instructions-read.cpp`/`instructions-write.cpp`):
nuevo helper `readDirectX(address, offset)` (patrón de ares):
```cpp
if (EF && D.l) return read(((D.w + address) & 0xffff00) | uint8(D.w + address + offset));
return readDirect(address + offset);
```
Usado en `instructionIndexedIndirectRead8/16` y `instructionIndexedIndirectWrite8/16`:
`V.l = readDirectX(U.l + X.w, 0); V.h = readDirectX(U.l + X.w, 1);`

### Bug 5 — WAI/STP sin parking + sin interrupciones externas → RESUELTO
**Síntoma**: `instructionWait`/`instructionStop` solo ponían `wai`/`stp`;
`execute()` nunca los chequeaba, así que WAI/STP ejecutaban la instrucción
siguiente. `interruptPending()` estaba hardcodeado a `false`: sin NMI/IRQ.

**Causa raíz**: el port omitió la máquina de estados de `CPU::main()` de ares
(`if(r.wai) return instructionWait(); if(r.stp) return instructionStop();` +
despacho de interrupciones) y `System::load()` nunca llamaba `power()`.

**Fix** (`cpu65816.cpp`/`.hpp`, `system.cpp`):
- `execute()` reescrito: STP aparca para siempre (1 idle/step), WAI aparca
  hasta que `interruptPending()`, al despertar limpia `wai` y consume un
  `idle()` extra; luego despacha NMI > IRQ en frontera de instrucción.
- Nuevos: `setNmi(bool)`, `setIrq(bool)` (pins de nivel, se limpian al
  despachar), `interruptPending()` real (`nmi || (irq && !IF)`), e
  `interrupt()` (preludio 2 ciclos, push PBR solo native, push P con B=0 en
  modo E, `IF=1`, `DF=0`, carga vector).
- Vectores: NMI `EF ? $FFFA : $FFEA`; IRQ `EF ? $FFFE : $FFEE` (7 ciclos E /
  8 native, igual que BRK/COP según fullsnes `NNsSSSDD 7,8`).
- `power()`/`reset()` limpian `nmi`; `System::load()` llama `cpu_->power()`
  antes de `cpu_->reset()`.
- Validación: 8 tests unitarios de CPU nuevos (incluye tabla de ciclos
  canónicos y conteo de ciclos de NMI/IRQ/BRK). `cputest-full` sigue pasando
  1107/1107 (no usa WAI/STP; el harness detecta STP por `stopped()`).
- Nota: el muestreo de NMI/IRQ se hace en frontera de instrucción (equivalente
  a higan pre-`lastCycle`); refinable cuando exista el scheduler (Fase 3).

## 5. Tests y validación

- Ejecutables build tree: `build/core/tests/snes_tests` (doctest),
  `build/tools/cputest/snes_cputest` (harness), `build/tools/fase3/snes_fase3`
  (test ROM fase 3, ROM generada en build/tools/fase3/).
- Comandos usados en esta sesión:
  - `cmake --build build` en el raíz.
  - `./build/core/tests/snes_tests` → **40 TEST_CASE / 269133 assertions**.
  - `./build/tools/cputest/snes_cputest third_party/cputest/cputest-full.sfc`
    → SUCCESS (`0081a2`).
  - `./build/tools/cputest/snes_cputest third_party/cputest/cputest-basic.sfc`
    → SUCCESS.
  - `./build/tools/fase3/snes_fase3 build/tools/fase3/fase3_timing.sfc`
    → SUCCESS (VBlank x10, V-IRQ x10, NMI ack x10, HBlank 27/173).
- ROMs del harness versionadas en `third_party/cputest/` (antes en
  `/var/folders/.../opencode/snestests/cputest/`); su fuente de referencia
  (tests-full.inc, tests_table.inc) está en `/tmp/wdc/`.
- Nota: `ctest` no registra tests (el `enable_testing()` está en un
  subdirectory, debe subirse al raíz) — ejecutar `snes_tests` directamente.

## 7. ❗Cómo continuar el curso (siguiente IA)

1. **Lee este archivo completo y además**:
   - `/Users/matru/Desktop/emulador_supernintendo/emulador_snes_diseno.md` (§1.2 scheduler,
     §4.1 CPU, §9 roadmap, §10 testing).
   - Referencia hardware antes de implementar timing: `fullsnes.html`/`fullsnes.txt`.
2. **Fase 2 COMPLETADA**: bus LoROM/HiROM/ExHiROM + WRAM + SRAM + registros
   base MMIO + open bus + power/reset; `snes_tests` 26/212 verdes, cputest-full
   y cputest-basic pasan. Fase 3 en curso: scheduler + PPU (ver §1.2 scheduler
   y §4.1 CPU del diseño). Al implementar timing, consultar `fullsnes.txt`.
3. **Mantener infraestructura**: cada fix bien comentado, con su sección en
   `docs/update.md` (historial de bugs) y los tests de cputest verdes.
4. **NO ampliar alcance**: si una solución necesita tocar PPU/APU/scheduler,
   PARAR y avisar. Solo fase 0/1/2.

## 8. Fase 3 — Scheduler + PPU timing-only (COMPLETADA)

Diseño: `docs_documentacion/fase3_scheduler_diseno.md`; fuentes de verdad:
`fullsnes.txt` (I/O map columna derecha, "SNES Timing Oscillators / H/V
Counters / H/V Events") y `fullsnes.html`.

### 8.1 Implementado

- **Scheduler relativo byuu** (`core/src/scheduler/`): `Thread` (pasivo,
  `step(masterCycles)`), `Scheduler` con delta `int64` — `step()` acumula,
  `sync()` avanza de 4 en 4 (un dot). `System::step()` = instrucción + sync
  final.
- **PPU timing-only** (`core/src/ppu/`): dot = 4 master, scanline = 341 dots,
  frame NTSC = 262 líneas. Eventos evaluados al ENTRAR en el dot (los
  contadores primero, los flags después — un `lda $4212` en H=0,V=225 ya ve
  bit7 set). Tabla de eventos fullsnes:
  - VBlank set H=0,V=225; limpieza+auto-ack NMI H=0,V=0.
  - HBlank set H=274, clear H=1 → set durante H=274..340 y H=0 (¡bit6 de
    $4212 = 0xC0 en H=0 dentro de VBlank, no 0x80!).
  - IRQ flag modo 1/2/3 (`$4200` bits 5-4) con semántica register-note:
    H==HTIME cualquier V / V==VTIME con H=0 / ambos. Latch hasta read/ack
    o disable IRQ. Disable NMI NO toca el latch de `$4210`.
- **Registros**: `$4210` RDNMI latch read/ack + versión CPU 0x02; `$4211`
  TIMEUP latch read/ack; `$4212` HVBJOY espejo LIVE (nunca clear-on-read);
  `$4200/$4207-$420A` delegados desde el bus. `$4200` = 0x00 en power/reset;
  HTIME/VTIME (bracketed) sobreviven al reset soft.
- **Waitstates** (`Bus::waitStates`): WRAM/SRAM/expansión 8, B-Bus/CPU I/O 6,
  joypad `$4000-$41FF` 12, WS2 8→6 con `$420D` bit0, WS1 fijo 8; idle CPU 6.
  El CPU hace `sync()` ANTES del acceso y `step(waitStates)` después.
- **Test ROM propio** (`tools/fase3/`): builder Python (ensamblador 2 pasadas)
  + runner `snes_fase3`. Conteos exactos: VBlank x10 (por `$4212`, nunca
  `$4210`), V-IRQ x10 (modo 2, VTIME=100, read/ack), NMI latch `$4210`==0x82
  x10, HBlank 27/200 muestras (rango 5-45% — valida rango, no conteo bruto).

### 8.2 Erratas vs fase3_scheduler_diseno.md

1. **Power-on HTIME/VTIME = 0x1FF** (fullsnes `4207h=(FFh) 4208h=(01h)`), no 0
   como decía §7.7 del doc.
2. **Eventos al entrar en el dot**: la evaluación debe ser post-incremento;
   con pre-incremento el flag tarda un dot en aparecer (primer fix de la
   sesión).
3. **Evento NMI en H=0.5** indistinguible a granularidad de dot → mismo dot
   que VBlank (documentado en `ppu.hpp`).
4. **`$4212` bit6 en H=0 es 1** (HBlank H=274..340,0) — tests iniciales
   esperaban 0x80/0x00 y debían ser 0xC0/0x40.
5. cputest-full registra 2237844 instrucciones (el `240228` del resumen de
   Fase 2 era de una corrida parcial); el criterio es el mismo: SUCCESS en
   `0081a2`.

### 8.3 Validación (todo verde al cierre)

```bash
cmake --build build
./build/core/tests/snes_tests        # 47 TEST_CASE / 269181 assertions
./build/tools/cputest/snes_cputest third_party/cputest/cputest-full.sfc   # SUCCESS 0081a2
./build/tools/cputest/snes_cputest third_party/cputest/cputest-basic.sfc  # SUCCESS
./build/tools/fase3/snes_fase3 build/tools/fase3/fase3_timing.sfc         # SUCCESS
```

- La ROM de fase3 se genera en build-time (CMake custom command, Python3).
- La entrega real de NMI/IRQ al CPU se completó como **Fase 3b** — ver §9.
- Pendiente para Fase 4: rendering; latches `$213C-$213F`; líneas
  largas/cortas (341/342/340); PAL.

## 9. Fase 3b — Entrega real de NMI/IRQ al CPU (COMPLETADA)

El punto 1 del plan (§11.8/§13 de `fase3_scheduler_diseno.md`): la PPU pasa de
"latchea flags que el juego lee" a "notifica al CPU". El CPU ya tenía los pins
externos `setNmi/setIrq` (modelo de nivel, limpiados al despachar) y el
dispatch con vectores E/native correctos (`execute()`: nmi→`$FFFA`/`$FFEA`,
irq→`$FFFE`/`$FFEE`) + `interrupt()`/RTI validados por `cpu_tests` — esa parte
no cambió. Lo nuevo es el **mecanismo de notificación** y su semántica.

### 9.1 Diseño (semántica exacta 65816, fullsnes l.744-760)

- **NMI — edge-detect interno en la PPU**: fullsnes l.760 — "NMI flag gets set
  when `[4200h].7 AND [4210h].7` changes from 0-to-1". Cada dot se computa
  `nmiSource = NMITIMEN.7 && latch_$4210` (`vblank_`) y el pin se levanta solo
  en el flanco 0→1 (`nmiEdgePrev_`). El CPU limpia su pin al despachar, así que
  un flanco = un NMI; re-habilitar `$4200` bit7 dentro de un VBlank pendiente
  rearma el flanco → "NMI viejo mal-ejecutado" (fullsnes).
- **IRQ — nivel**: el pin es un espejo LIVE de `irqFlag_` ($4211 latch). Se
  re-conduce cada dot (un IRQ sin ack re-dispara tras cada RTI — semántica de
  nivel) y también en cada sitio que limpia el latch sin avance de dot: read
  `$4211`, disable IRQ vía `$4200` bits 5-4, power/reset. El gateo por I vive
  en el CPU (ya existía).
- **Cableado**: `Ppu` expone dos sinks `std::function<void(bool)>`
  (`setNmiPin`/`setIrqPin`, vacíos por defecto → no-op en tests unitarios de
  PPU); `System` los cablea a `cpu_->setNmi/setIrq` en su constructor. El
  modelo cooperativo no cambia: la PPU solo corre dentro de `sync()`, y el
  flanco se evalúa exactamente en el dot del evento; el CPU lo ve en el
  siguiente dispatch de `execute()` (entre instrucciones, como el hardware).

### 9.2 Implementado

- `core/src/ppu/ppu.hpp` + `timing.cpp`: edge-detect NMI en `advanceDot()`,
  `driveIrqPin()` (espejo de nivel) llamada cada dot y en los 5 sitios de
  mutación del latch, power/reset con `nmiEdgePrev_ = false`.
- `core/src/system/system.cpp`: constructor cablea los dos pins.
- `core/tests/ppu_timing_tests.cpp`: 4 tests nuevos de sinks (1 raise por
  frame; re-armado tras read `$4210` mid-VBlank; disable + re-enable
  mid-VBlank = mis-execute; espejo nivel IRQ con read/disable).
- `core/tests/interrupt_tests.cpp` (nuevo, registrado en el CMake): 3 tests de
  integración con ROMs LoROM mínimas en memoria (sin fichero):
  1. **NMI**: salta al vector `$FFFA` (E mode), P pushed con B=0, I=1 dentro
     del handler, exactamente 1 por frame (edge), RTI vuelve al loop con P
     restaurada, 2 NMIs en 2 frames.
  2. **IRQ gateo + nivel**: IRQ latchea con I=1 y NO despacha; tras `cli`
     despacha y, sin ack, cada RTI re-entra (≥5 despachos); RTI restaura I=0.
  3. **IRQ ack**: handler lee `$4211` → exactamente 2 despachos en 2 frames
     (una vez por frame, modo 2) y no re-dispara en el resto del frame.

### 9.3 Erratas / notas

1. La excepción de fullsnes "read `$4211` exactamente en el instante del
   trigger conserva el flag" sigue sin modelarse (indistinguible a
   granularidad de dot).
2. El pin NMI queda "levantado" mientras la fuente AND siga activa y el CPU no
   haya despachado (WAI se despierta igual: `interruptPending()` incluye nmi).
3. Los tests de integración arrancan el System replicando `System::load()`
   (cartridge load con vector + power/reset manuales) y necesitan el vector de
   reset en `$FFFC` — olvidarlo hace que el CPU arranque en el relleno 0xEA y
   acabe en BRKs (`$0000`), lo que produjo falsos fallos en la primera pasada.

### 9.4 Validación

```bash
cmake --build build
./build/core/tests/snes_tests   # 47 TEST_CASE / 269181 assertions, 100%
```

cputest-full/basic y la ROM de fase3 siguen verdes (la ROM de fase3 nunca
habilita bit7 de `$4200`, así que no recibe NMI — su polling de `$4210`/`$4211`
no cambia).

## Cómo compilar/unir todo
- `cd snes-emu && cmake --build build` (o `make -C build`).
- Test unitarios: `./build/core/tests/snes_tests`.
- Cputest: `./build/tools/cputest/snes_cputest <ruta a cputest-full.sfc>` del
   directorio `/var/folders/...`.
- (Re)genera `build/` si CMake cambia: `cmake -S . -B build`.
- Añade `add_subdirectory` cuando crees una herramienta nueva, y no olvides el
  include de los headers de core en el CMakeLists de la tool.

---

# 10. Fase 4 — PPU rendering de fondos (EN CURSO)

Documento de recuperación específico de la fase 4. Plan completo:
`docs_documentacion/fase4_estado_plan.md`. Referencias hardware: `fullsnes.txt`
(fuente primaria) y ares PPU-performance (referencia de orden/timing/casos
borde, copiada en `docs_documentacion/refs/`):
- `refs/ares-ppu-performance-background.cpp` — loadMap/loadOffsets/loadPlanes/
  mode7Draw/prime de ares (usado para portar `Layer::*`).
- `refs/ares-ppu-performance-dac.cpp` — DAC (compositor) de ares: el gate de
  color math por source, el orden even/odd half-pixel en hires y `pixel()`.
  **ÚSALO antes de tocar `Composer::pickSub/pickMain`.**

## 10.1 Estado de la suite

```bash
cmake --build build --target snes_tests        # desde snes-emu/
./build/core/tests/snes_tests --test-case="ppu:*"
# 66 test cases | 66 passed | 0 failed | 34 skipped (los 34 son cpu/bus/cart/
# scheduler/interrupt, no correr con el filtro ppu:*)
# assertions: 269237 | 269237 passed | 0 failed
./build/core/tests/snes_tests                 # suite completa
# 100 test cases | 100 passed | 0 failed | assertions: 269526
# cputest-full/basic y la ROM de fase 3 siguen verdes (validación §8.3)
```

**FASE 4 COMPLETA (2026-08-13)**: 100/100 test cases, 269,526 assertions.
Fondos, sprites, ventanas, color math, modo 7, pseudoHires y el latch de campo
están implementados y en verde — ver §10.10 para el cierre y el detalle.

## 10.2 HECHO Y COMPROBADO (verde — NO re-depurar)

- **Pipeline completo de fondos cableado en `timing.cpp::advanceDot()`**:
  frame-start (V=0: latch `state_.interlace/overscan` + `newFrame` de cada
  motor), `lineStart` de todos los motores en H=0, `sprites_.probe` cada 2
  dots (H=0..254), `fetchSlot(dot&7)` cada dot (H=0..263), `prime()` en H=14,
  `paintDot()` en H=14..269 (8 draws below/above + sprites + window +
  compositor), `sprites_.loadTiles()` en H=270, `renderedFrames_++` en V=240.
- **`render.cpp`**: `Layer` completo (lineStart/prime/loadMap/loadOffsets/
  loadPlanes/draw/mode7Draw/resetState), `fetchSlot` (tablas de slots por
  modo 0-7), `paintDot`, `Composer` (lineStart/emitPixel/pickSub/pickMain/
  mixColors/lookupColor/lookupDirectColor/coldataColor/resetState),
   `WindowMask` (lineStart/stepPixel/maskHit/resetState), `Mosaic`
   (lineStart/resetState/active en io.cpp), `SpriteEngine`
   (newFrame/lineStart/touchesLine/probe/draw/loadTiles/resetState — portado
   de ares, con tests desde 2026-08-13, ver §10.9).
- **Test fixture `BgFixture`** (`core/tests/ppu_bg_tests.cpp:17-56`) — pieza
  clave, ver §10.6 invariantes.
- **10 test cases GREEN** (verifican fondos): mode 0 (2bpp básico, paleta/
  transparencia/hmirror/vmirror, hscroll fino, 16x16 char+16, 16x16 vscroll,
  BG4 con offset de paleta), mode 1 (4bpp, prioridad cruzada, TM gating),
  mode 2 (offset-per-tile con lag de 1 columna + vlookup), mode 3 (8bpp +
  direct color), mosaic horizontal+vertical. Esto valida de verdad:
  - El fetch de mapas y planos por slot (tablas de `fetchSlot`).
  - El shift de píxeles de `draw()` y el avance de tile cada 8 draw calls.
  - `prime()` con el desplazamiento fino de hscroll.
  - **Shift de paleta por modo** (`render.cpp:123-127`): mode 0 → `<<2` con
    offset `id<<5`; mode 3 (8bpp) → `<<8`; resto (4bpp) → `<<4`.
  - Offset-per-tile modos 2 (hlookup con lag de columna, vlookup de fila) y
    la tabla de offsets de BG3.
  - Mosaic horizontal y vertical (incl. `$2106` bits por capa y `Mosaic::`).
  - El gating TM/TS (`io.cpp` $212C/$212D → `aboveEnable/belowEnable`).

## 10.3 HECHO pero SIN COMPROBAR (implementado, sin test verde)

Estos existen en el código (portados de ares) pero **no tienen ningún test**:
- **Modo 7**: `Layer::mode7Draw()` completo (render.cpp:233-302) — matriz
  A/B/C/D, center, flips, repeat modes, EXTBG. **Sin test** (los checks
  `0x0000/0x7FFF/0x001F` del test INIDISP NO son modo 7; el plan original
  decía test mode 7 en rojo pero nunca se escribió).
- **Ventanas**: `WindowMask::stepPixel/maskHit` completos, incl. window
  de color (máscaras de math en `composer.above/below.colorEnable`).
  Falta test ($2123-$212A, $212E, $2133).
- **Color math**: `mixColors` (add/sub/halve, portado de ares dac.cpp),
  `lookupDirectColor`, `coldataColor`; los flags del Composer
  (`bg1ColorEnable`…) se escriben… — **REVISAR**: verificar que
  `io.cpp` ($2130-$2132) conecta CGADSUB/COLDATA a los campos del Composer
  (blendMode/colorMode/colorHalve/*ColorEnable/colorRed-Green-Blue). Solo
  existe la lectura/escritura de campos MMIO (comprobada en fase 4 §1 del
  plan); el efecto visual no está probado.
- **pseudoHires** (`$2133` bit3 → `io_.pseudoHires`): usado en `emitPixel`,
  sin test.
- **Interlace/overscan**: `state_.interlace/overscan` latch en frame-start
  (timing.cpp:79-80) y `fieldBit()` — sin test directo.
- **mode 4** (8bpp + offset-per-tile): `fetchSlot` case 4 existe; sin test.
- **Búsqueda de tiles del 0x2100 para el puerto OAM…**: no, esto no aplica.

## 10.4 Diagnóstico de los fallos (RESUELTO — ver §10.8 para el cierre)

### Bug A — `Composer::pickSub` en hires sin color math devuelve 0
Síntoma: en mode 5/6 los half-píxeles pares (sub screen) del **primer dot de
la línea** (dot 14) salen como backdrop (`below=0000` en la traza emit);
los dots siguientes sí funcionan (`below=7FFF/7C00`).

Causa: `render.cpp:635`
```cpp
if (!below.colorEnable) return above.colorEnable ? below.color : uint16(0);
```
- `emitPixel` llama `pickSub` ANTES que `pickMain` (render.cpp:594-595).
- `composer.above.colorEnable` se resetea a false en `lineStart`
  (render.cpp:581) y solo lo pone `pickMain` (render.cpp:679) o
  `window_.stepPixel` (render.cpp:538-539). En el dot 14 de la línea el
  orden deja `above.colorEnable` en el estado que el dot anterior dejó.
  Y en cualquier caso, **sin CGADSUB el comportamiento correcto NO es 0**:
  el `pixel()` de ares (refs/ares-ppu-performance-dac.cpp:48-54) hace
  passthrough del color del pick cuando el source no tiene color math
  habilitado:
  ```
  if(!io.colorEnable[above.source]) return above.color;
  ```
  (En hires el even half es `pixel(x, below[x], above[x])`, dac.cpp:41.)

Fix recomendado (según ares): cuando `!hires` devolver 0 (igual que ahora);
cuando hires y el source ganador del below pick NO tiene math habilitado →
devolver `below.color` tal cual; cuando SÍ tiene math → blend:
```cpp
// modelo ares dac.cpp pixel(): passthrough salvo color math habilitado
if (!below.colorEnable) return below.color;               // passthrough
if (!blendMode) return mixColors(below.color, coldataColor(), mathColorHalve);
return mixColors(below.color, above.color, mathColorHalve);
```
- Nota: `below.colorEnable` aquí debería ser el enable CGADSUB del **source
  que ganó el below pick** (capa 0-3/obj/COL), no el derivado del main pick
  de `pickMain`. Para los tests actuales (sin $2131) basta passthrough; el
  modelo fino de math hires se valida en la fase de ventanas+color math.
- `pickMain` (render.cpp:682) ya hace passthrough correcto
  (`above.colorEnable ? above.color : 0` donde above.colorEnable es la
  window de color) — NO tocar.

### Bug B — mode 5: tile 1 en hires = char+1, no char 0 (dos problemas)
1. En hires (modos 5/6) cada cell del mapa de 16 half-píxeles usa DOS chars:
   el segundo half-tile usa `character+1` (render.cpp:113,
   `if (htiles == 4 && bool(hoffset & 8) != tile.hmirror) tile.character += 1;`
   — portado de ares background.cpp). La traza lo confirma:
   `map … nti=1 … char=1` y `char L=1 dot=6 charIdx=1 addr=2100 word=0000`.
   El test (ppu_bg_tests.cpp:316-343) solo escribió char 0 (0x2000/0x2008) y
   espera `26+8 == 0x8000|0x7FFF` (hp8) — hp8 es el primer half-pixel del
   tile char+1 = 0x2100 = 0 → backdrop → FALLA. **El render es correcto; el
   test está mal.** Fix del test: escribir también
   `f.vram(0x2100, 0xF00F); f.vram(0x2108, 0x0000);` (o cambiar la
   expectativa a backdrop).
2. La aserción 342 `pixelColor(3, 1) == (0x8000 | 0x7C00)` está mal
   planteada: `pixelColor(x,y)` lee la columna `26+x` del framebuffer, que
   en hires es el half-píxel `x` (no el left-half del píxel x). Col 29 =
   hp3 = píxel 3 del tile 0 = color 1 = 0x7FFF, no 0x7C00. Fix del test:
   `pixelColor(4, 1)` (hp4 = 0x7C00) o esperar 0x8000|0x7FFF en (3,1).

### Bug C — mode 6: datos de chars en direcciones equivocadas del test
En mode 6 (4bpp hires) el stride entre chars es `origin << (3+mode) =
<< 9 = 512` bytes (address = char<<9 + fila, tileBase 0x2000 →
characterIndex 0x10 → char c en `0x2000 + c*0x200`). El test escribe los
tiles en `0x2000 + 16*c` (stride 16 = layout 2bpp) → char 1 en 0x2010
(= char 16 del mapa) y char 2 en 0x2020 (= char 32) quedan VACÍOS (0) → los
cols 1 y 2 del mapa (que usan char 1 y char 2 → 0x2200/0x2400) salen
backdrop. Fix del test (ppu_bg_tests.cpp:359-362):
```cpp
for (int c = 0; c <= 2; c++) {
  f.vram(0x2000 + 0x200 * c, 0x00FF);   // (era 0x2000 + 16 * c)
  f.vram(0x2008 + 0x200 * c, 0x0000);
}
```
(o cambiar los tiles del mapa a char 0 con hoffset distintos; lo simple es
mover los datos). Junto con el bug A, el col 0 (26+0) y cols 1/2 quedan
verdes.

### Bug D — test INIDISP: `paint(1,0)` repetido = step(0) (fixture)
Síntoma: las 3 aserciones posteriores a reescribir $2100 leen siempre
`0xFFFF` (el píxel viejo). `paint(y,x)` avanza SOLO hacia delante
(`target - current`); pintar el mismo (1,0) dos veces → delta 0 → ningún
dot → el framebuffer conserva el valor anterior (no hay "repintado").

Fix del test (ppu_bg_tests.cpp:424-439): avanzar a píxeles siguientes tras
cada writeRegister de $2100 y verificar en esa x:
```cpp
f.paint(1, 0);
CHECK(f.ppu.pixelColor(0, 1) == (0x8000 | 0x7FFF));
f.ppu.writeRegister(0x00, 0x80);   // forced blank
f.paint(1, 1);
CHECK(f.ppu.pixelColor(1, 1) == 0x0000);
f.ppu.writeRegister(0x00, 0x00);   // brightness 0 (sin blank)
f.paint(1, 2);
CHECK(f.ppu.pixelColor(2, 1) == 0x7FFF);
f.ppu.writeRegister(0x00, 0x0F); f.ppu.writeRegister(0x2C, 0x00);
f.cgram(0, 0x001F);
f.paint(1, 3);
CHECK(f.ppu.pixelColor(3, 1) == (0x8000 | 0x001F));
```
Esto comprueba que el render en blank sí escribe negro (los picks devuelven
0 con `displayDisable`, luma 0 → `*line++ = 0`).

## 10.5 Próximos pasos (en orden)

1. ✅ Fix A (pickSub passthrough) + fixes de test B/C/D → `--test-case="ppu:*"` → 40/40.
2. ✅ Suite completa sin filtro: 74/74 (cpu/bus/cart/scheduler/interrupt verdes).
3. ✅ cputest-full/basic + ROM fase 3 siguen verdes.
4. ✅ **TDD sprites/OAM** (`ppu_sprite_tests.cpp`, 11 test cases) — ver §10.9.
5. Fase 4 siguiente: modo 7 → ventanas + color math → mode 4 / interlace /
   pseudoHires (ver `fase4_estado_plan.md` §4).
6. Limpieza opcional al final: quitar prints de depuración (todos tras
   `getenv("PPU_DEBUG")`), el guard de runaway de `Ppu::step` y `ppuDebug()`.
   (Hecho: el print `map2` temporal de depuración del bug E se eliminó.)

## 10.6 Invariantes verificadas (NO re-verificar)

- `Ppu::step(masterCycles)` es RELATIVO y solo avanza: el fixture calcula
  `target - current` en dots (kLine = 341*4). Nunca pintar hacia atrás
  (negativo = desbordamiento gigante = guard de runaway de timing.cpp, que
  aborta >500000 dots — dejar el guard).
- El display píxel x de la línea y se pinta en el dot `14+x`; el framebuffer
  es 564 de ancho, fila `y+8` (non-overscan) y columna `26+x` (emitPixel
  escribe col 26 = píxel 0; en hires escribe col 26+2x y 26+2x+1).
- `pixelColor(x,y)` y `raw(y,col)` usan esos desplazamientos; `pixelColor`
  NO es "left half" en hires (ver bug B.2).
- vscroll -1 por capa ($210E/$2110/$2112/$2114 = 0x03FF) en el fixture:
  línea 1 → fila 0 del mapa. Sin esto los tests de fila 0 fallan.
- `renderingIndex` avanza cada **8 draw calls** también en hires
  (render.cpp:213): en hires cada dot hay 2 draw calls (below/above), cada
  fetch de tile cubre 8 half-píxeles. (El fix `8u << isHires()` era el bug.)
- Shift de paleta: mode 0 → `<<2` + offset `id<<5`; mode 3 → `<<8`; resto →
  `<<4` (render.cpp:125-127) — validado por mode 2 y 3. NO usar `2 << mode`.
- Layout de chars: `tile.address = origin << (3+mode) + (voffset&7)` con
  `origin = (char + tileBase>>(3+mode)) & mask`; en hires stride = 512 bytes
  (mode 5/6), en 4bpp mode 2/5 = 256, en 8bpp mode 3/4 = 512.
- `loadPlanes` lee 2 words por slot (bit-spread de ares, render.cpp:179-182);
  en hires el 2º half-tile del cell usa char+1 (render.cpp:113).
- Offset-per-tile modos 2/6: valid 13+id, lag de 1 columna (los offsets se
  leen en el slot anterior), vlookup con `(hlookup & 0x8000)` etc. —
  verificado por el test mode 2.
- Mosaic: `mosaic.pixel`/`hcounter` en draw (render.cpp:216-224) y el
  contador vertical en `Mosaic::lineStart` — verificado por el test mosaic.
- TM/TS ($212C/$212D) son correctos en io.cpp (writes verificados en MMIO);
  los tests hires escriben TS para el sub screen.
- ares `pixel()` de dac.cpp (refs/): con ventanas de color `windowBelow` no
  activas y sin color math, el even half en hires = passthrough del color
  del below pick. El orden even/odd de `emitPixel` (render.cpp:600-601)
  coincide con dac.cpp:40-43.
- Runaway guard `Ppu::step` (timing.cpp) aborta >500000 dots — es una red
  de seguridad para tests, no un bug.

## 10.7 Ficheros de la fase 4

| Fichero | Estado |
|---|---|
| `core/src/ppu/render.cpp` | NUEVO — pipeline completo (Layer, fetchSlot, Composer, WindowMask, Mosaic, SpriteEngine) |
| `core/src/ppu/io.cpp` | NUEVO — MMIO completo (fase 4 §1 del plan) + acceso VRAM/OAM/CGRAM |
| `core/src/ppu/ppu.hpp` | Arquitectura motores + contrato timing (modificado) |
| `core/src/ppu/timing.cpp` | advanceDot cableado al render + runaway guard (modificado) |
| `core/tests/ppu_bg_tests.cpp` | 13 test cases fondos: **13 verdes** |
| `core/tests/ppu_mmio_tests.cpp` | 21 test cases MMIO verdes |
| `core/tests/ppu_sprite_tests.cpp` | 11 test cases sprites/OAM: **11 verdes** (ver §10.9) |
| `core/CMakeLists.txt` / `tests/CMakeLists.txt` | incluyen render.cpp, ppu_bg_tests.cpp y ppu_sprite_tests.cpp |
| `docs_documentacion/refs/` | ares background.cpp + dac.cpp (referencia) |

Comandos útiles:
- Build: `cmake --build build --target snes_tests` (desde `snes-emu/`).
- Suite ppu: `./build/core/tests/snes_tests --test-case="ppu:*"`.
- Trazas: `PPU_DEBUG=1 ./build/core/tests/snes_tests --test-case="ppu: mode 5*"`
  (prints: lineStart, map, char, draw, pick, emit, raw, px, w212C).

## 10.8 Cierre de los 9 fallos (2026-08-12) — TODO VERDE

9 fallos en 3 test cases → **0 fallos**. La suite completa pasa
**74/74 test cases / 269351 assertions**, y cputest-full/basic + ROM fase 3
siguen en SUCCESS (validación §8.3).

| Bug | Fichero tocado | Resolución |
|---|---|---|
| A — `pickSub` hires sin color math devuelve 0 | `render.cpp` | Passthrough del color del below pick cuando no hay math habilitado (modelo ares dac.cpp `pixel()`). Era el único bug real del core. |
| B — mode 5: tile 1 = char+1 | `ppu_bg_tests.cpp` | Test escribía solo char 0; se añadió el tile-data de char 1 (`0x2100/0x2108`) y se corrigió el índice de la aserción 342. |
| C — mode 6: stride de chars | `ppu_bg_tests.cpp` | Datos de chars movidos a stride 0x200 (layout 4bpp hires, `origin<<9`); NOTA: ver Bug E — el fallo residual final era otra cosa. |
| D — INIDISP `paint(1,0)` repetido | `ppu_bg_tests.cpp` | El fixture solo avanza hacia delante; se reescribieron las aserciones con x siguientes tras cada write de `$2100`. |
| E — mode 6: vlookup corrompe voffset | `ppu_bg_tests.cpp` | (No estaba en el diagnóstico original). El test escribía la tabla de offsets en la fila V (`0x1000+col`) cuando la fila H vive en `0x13E0+col` — layout de 2 filas por el vscroll −1 del fixture, idéntico al test mode 2. La entrada `0x2008` en la fila V disparaba el vlookup (bit 13 = valid BG1) → `voffset = 1 + 0x2008` → fila 1 del mapa → cell 2 leía `0x0022` (backdrop). Fix: tabla de offsets a 2 filas (H `0x13E0+c`, V `0x1000+c`) con todos los valores 0. El render es correcto (coincide con ares: hlookup fila `bg3.voffset+0`, vlookup fila `bg3.voffset+8`, lag de una celda); el test estaba mal planteado. |

Nota de precisión: el emulador NO implementa la exención de la primera
columna de ares (`if(offsetX >= (1 << tileWidth))` en offset-per-tile) ni el
desplazamiento `<< hires` del hscroll de BG3 en `loadOffsets`; en los casos
cubiertos por los tests el resultado coincide (la exención la da el orden de
slots: loadOffsets corre después de la loadMap del cell 0), pero conviene
contrastarlo contra ares al validar modos 4/6 con offsets no nulos.

## 10.9 Cierre TDD sprites/OAM (2026-08-13) — TODO VERDE

Completado el punto 3 del plan (`fase4_estado_plan.md` §4): los 11 test cases
de `core/tests/ppu_sprite_tests.cpp` pasan. Cubren: span 8x8, hflip/vflip,
nameselect, prioridades vs BG1 (mode 0), orden de solape (índice OAM menor
gana), rotación de prioridad ($2103 bit7), 32 items/línea + range overflow,
34 tiles/línea + time overflow, sizes 8x8..64x64 (tablas small/large),
interlace de sprites y reset de dirección OAM en V=225.

**Un único bug real del core** (el resto eran bugs de test):

| Bug | Fichero | Resolución |
|---|---|---|
| El trabajo H=0 de la línea 0 nunca corría | `timing.cpp`/`ppu.hpp` | El contador arranca en `dot_=0` y `advanceDot()` incrementa ANTES de procesar eventos, así que el `frame-start`/`lineStart`/`probe(0)`/`fetchSlot(0)` de la línea 0 se perdían (el sprite 0 no se evaluaba en la línea 0; lo que se veía era el OAM zeroed 1..32). Fix: extraer `startLine()` y diferir el trabajo H=0 al primer `step` vía un flag `pendingStart_` (se ejecuta ya con los writes del juego visibles). No cambia la semántica de timing de fase 3 (los 269463 assertions incluyen timing/bg verdes). |

**Bugs de test corregidos** (en `ppu_sprite_tests.cpp`):

| Bug | Resolución |
|---|---|
| Fixture con OAM zeroed (128 sprites en x=0,y=0 tocaban línea 0) | El `SpriteFixture` aparca los 128 sprites en `y=255` y deja la dirección OAM en 0. |
| `oam()` escribía el byte alto de OAM en la dirección equivocada para índices impares | `$2102` solo setea direcciones pares; para `idx>>2` impar se pisa el byte par y el auto-incremento cae en el impar. |
| nameselect | faltaba el bit por-sprite (OAM attr bit 0); solo se ponía el global ($2101 bits 3-4). |
| prioridad 0 vs BG1 | el mapa de BG1 (0x0000) colisionaba con los tiles de sprite (`solidTiles`); mapa movido a 0x0400 (BG1SC=1). |
| 34 tile budget | sprites eran 8x8 (faltaba `size=true`) y faltaba `paint(1,16)` antes de `px(16,1)`. |
| sizes no-cuadrados (bs 6/7) | `solidTiles(w)` no cubría la altura; ahora `solidTiles(h)`. |
| 32 items | faltaba `paint(1,248)` antes de `px(248,1)` (el píxel aún no se había pintado). |

Validación (2026-08-13):

```bash
cmake --build build --target snes_tests
./build/core/tests/snes_tests --test-case="ppu:*"   # 51/51
./build/core/tests/snes_tests                        # 85/85, 269463 assertions
./build/tools/cputest/snes_cputest third_party/cputest/cputest-full.sfc   # SUCCESS
./build/tools/cputest/snes_cputest third_party/cputest/cputest-basic.sfc  # SUCCESS
./build/tools/fase3/snes_fase3 build/tools/fase3/fase3_timing.sfc         # SUCCESS
```

## 10.10 Cierre de fase 4 (2026-08-13) — modo 7, color math, pseudoHires, latch de campo, trama completa

La fase 4 (renderizado real del PPU) queda **COMPLETA**: 100/100 test cases,
269,526 assertions. Todos los subsistemas de render están implementados y con
tests sintéticos verdes:

| Área | Tests |
|---|---|
| MMIO `$2100-$213F` | `ppu_mmio_tests.cpp` (21) |
| Fondos modos 0-6 (2bpp/4bpp/8bpp, mosaic, offset-per-tile, hires, INIDISP) | `ppu_bg_tests.cpp` (13) |
| mode 4 (8bpp + offset-per-tile) | `ppu_bg_tests.cpp` (2) |
| pseudoHires (`$2133` bit 3) | `ppu_bg_tests.cpp` (1) |
| Sprites/OAM | `ppu_sprite_tests.cpp` (11) |
| Ventanas (`$2123-$212B`, TMW/TSW, color window) | `ppu_window_tests.cpp` (3) |
| Color math (add/sub, halve, fixed color, backdrop) | `ppu_colormath_tests.cpp` (3) |
| Modo 7 (identidad, rotación, screen-over, EXTBG) | `ppu_mode7_tests.cpp` (4) |
| Latch de interlace/overscan (`fieldBit`) | `ppu_timing_tests.cpp` (1) |
| Trama completa (renderedFrames, geometría) | `ppu_timing_tests.cpp` (1) |

Últimos cierres (los tres puntos que quedaban de `fase4_estado_plan.md` §3):

- **`fieldBit()` con el latch** (`ppu.hpp`): ahora lee `state_.interlace`
  (latcheado en `startLine` en V=0) en lugar de `io_.interlace` (vivo). Test:
  el campo no cambia si se deshabilita interlace a mitad de frame.
- **pseudoHires** (`ppu_bg_tests.cpp`): en modo 0 con `$2133` bit 3, cada dot
  pinta dos half-píxeles — par = sub (BG2), impar = main (BG1).
- **Verificación de trama completa** (`ppu_timing_tests.cpp`): `renderedFrames`
  incrementa una vez por frame (en V=240) y la geometría del framebuffer es
  564×242.

Bug del core corregido en modo 7 durante esta fase: `mode7Draw` no enmascaraba
`tileX/tileY` a 7 bits (usaba `uint16` en vez del `n7` de ares), así que el
wrap (`repeatMode7` 0/1) de píxeles fuera de rango leía el tile equivocado —
el bit 7 de `tileX` se colaba en `tileY`. Fix: `& 0x7F` (ver `update.md` y
`ppu_mode7_tests.cpp` "screen-over").

### Faltan los tests visuales (homebrew ROM)

Los tests sintéticos cubren cada componente aislado, pero **falta la validación
visual de extremo a extremo**: un ROM homebrew sencillo que renderice sprites,
fondos, modo 7 y color math a la vez y se compare contra un emulador de
referencia (ares) o fullsnes. Queda pendiente para más adelante.

## 11. Fase 5 — DMA y HDMA (COMPLETADA)

Diseño: `docs_documentacion/fase5_dma_diseno.md`. Referencias: fullsnes ("SNES
DMA Transfers", "SNES Timing"), ares (`cpu/dma.cpp`/`cpu/hdma.cpp`).

### 11.1 Implementado

- **Motor GP-DMA** (`bus.cpp`): `dmaRun()`/`dmaTransfer()` disparado al escribir
  `$420B` (bloqueante, bits auto-limpiados). Canal 0→7, unidad de transferencia
  0-7 (1/2/4 bytes con patrón de dirección B-bus de fullsnes), step A-bus
  (incremento/fijo/decremento), dirección A→B y B→A, `DAS=0` = 65536 bytes.
  Timing: 8 master cycles por byte (2.68MHz), `sync()` antes de cada unidad y
  `step(8×bytes)` después, `sync()` final.
- **Motor HDMA** (`bus.cpp`): `hdmaReset()` (recarga de tabla en V=0 desde
  A1Tx) + `hdmaRun()` (una unidad por scanline en HBlank). Tabla directa e
  indirecta (puntero de 2 bytes), `00h`=termina, `01h-80h`=single (una unidad +
  pausa), `81h-FFh`=repeat (una unidad por línea). Refleja el progreso en
  `$43x8-$43xA` (A2Ax/NTRLx).
- **Cableado bus ↔ PPU B-bus**: `$2100-$2133` escribe → `ppu.writeRegister`;
  `$2134-$2136`/`$2138-$213F` lee → `ppu.readRegister`; `$2137` SLHV →
  `captureCounters` + open bus.
- **Sinks de la PPU**: `setFrameStartSink` (V=0) y `setHblankSink` (H=274,
  solo líneas visibles) → cableados por `System` a `bus_->hdmaReset()/
  hdmaRun()`.
- **Tests** (`core/tests/dma_tests.cpp`, 8 test cases): registros de canal,
  ciclo exacto (N bytes → 2N dots), patrón de unidad + step, DMA→VRAM,
  HDMA directo (gradiente), HDMA single, HDMA indirecto, GP-DMA B→A (OAM).

### 11.2 Bugs corregidos durante la fase

| Bug | Fichero | Resolución |
|---|---|---|
| Step A-bus "fixed" incrementaba dentro de la unidad | `bus.cpp` | El modo fijo lee la misma dirección A-bus por byte (memfill); ahora `step==1/3` usa `aoffs` sin `+i`. |
| Delegación de lectura B-bus pasaba `offs-0x2134` en vez de `offs` | `bus.cpp` | `ppu.readRegister(uint8(offs))` (el offset real 0x34-0x3F); antes leía el registro equivocado (B→A roto). |

### 11.3 Refinamientos documentados (no bloquean, sin referencia exacta)

- **Dot exacto del disparo HDMA**: se usa H=274 (inicio de HBlank); ares dispara
  en un dot concreto de HBlank. Ajustable si un test de hardware lo exige.
- **Alineación del primer byte de DMA** (6-8 master según alineación al dot) y
  **refresh** (40 master/scanline) — no modelados; el costo es 8 master/byte
  plano.
- **Retardo de arranque del GP-DMA**: fullsnes dice "después de unos clk"; el
  disparo es inmediato (dentro del write de `$420B`).
- **`0x80` en HDMA** (single con 128 líneas): el NTRLx de 7 bits no puede
  representar 128; se trata como single con 128 líneas, con el contador
  truncado a 0 en el readback.

### 11.4 Validación

```bash
cmake --build build --target snes_tests
./build/core/tests/snes_tests                  # 108/108, 269566 assertions
./build/tools/cputest/snes_cputest third_party/cputest/cputest-full.sfc   # SUCCESS
./build/tools/fase3/snes_fase3 build/tools/fase3/fase3_timing.sfc         # SUCCESS
```

## 12. Próximas fases (6-7)

- **Fase 6 — APU: SMP + DSP**: el SPC700 (SMP) + DSP, puertos `$2140-$217F`,
  formato BRR. Comunicación CPU↔APU por los 4 puertos de I/O (no hay bus
  compartido). El scheduler ya reserva el hueco (ratio 21:24 en `thread.hpp`).
- **Fase 7 — Integración con juegos comerciales reales**: validación contra
  juegos comerciales y chips especiales (SA-1, SuperFX, DSP-1) — estos se dejan
  fuera a propósito hasta esta fase.

Pendientes menores documentados de fase 4 (no bloquean): PAL, líneas
largas/cortas (341/342/340), refresh DRAM, sync de contadores en V=128, e
interlace de campo completo (STAT78 bit 7 usa `frame_ & 1`, sin contador de
campo separado).