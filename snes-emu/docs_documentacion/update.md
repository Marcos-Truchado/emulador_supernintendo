# Estado del proyecto — Fase 2: Bus mínimo + memoria + WRAM

**Última actualización:** 2026-08-10

Este documento es el punto de recuperación de contexto para cualquier IA que
continúe el proyecto. Léelo íntegro **antes** de tocar el código. La fuente de
verdad del diseño es `/Users/matru/Desktop/emulador_supernintendo/emulador_snes_diseno.md`
(documento de diseño y hoja de ruta del proyecto).

---

## 0. Resumen ejecutivo

El proyecto es un emulador de SNES en C++20 estilo higan/bsnes
(dot/cycle-accurate), con el core totalmente desacoplado del frontend.
Completadas: **Fase 0 (esqueleto/build)** + **Fase 1 (CPU 65816 aislada)** +
**Fase 2 (bus mínimo + memoria + WRAM)**. **NO tocar PPU/APU/scheduler** (Fase
3+); DMA reales son Fase 5.

Estado actual:

- ✅ Core completo portado del WDC65816 de higan/bsnes (fuente de referencia en
  `/tmp/wdc/`): conjunto de instrucciones completo, modos de direccionamiento,
  conteo de ciclos exacto por instrucción (ciclo por ciclo).
- ✅ Desensamblador integrado + trace helpers.
- ✅ **Fase 2 (nuevo)**: bus completo LoROM/HiROM/ExHiROM + WRAM 128KB con
  mirrors + SRAM (tamaño desde cabecera `$FFD8`) + registros base MMIO (mult/div
  `$4202-$4206`, WRAM port `$2180-$2183`, sombras PPU `$2100-$2133`, puertos
  APU `$2140-$217F`, registros DMA `$4300-$437F`, joypad stub) + open bus
  (`lastData_` + `latch`) + valores power/reset según fullsnes.
- ✅ Cartridge loader con detección de cabecera (copier 512B), LoROM/HiROM/ExHiROM,
  `sramSize()` desde el byte de RAM de cabecera.
- ✅ Harness de test `tools/cputest` que ejecuta `cputest-full.sfc`
  (1107 tests de CPU comunitarios) y `cputest-basic.sfc`.
- ✅ **`cputest-full` y `cputest-basic` PASAN**: `SUCCESS: reached 0081a2`.
- ✅ **Tests unitarios**: `snes_tests` = **26 TEST_CASE / 212 assertions** (CPU 8,
  cartridge 7, bus 11) pasan 100%. Cobertura nueva de bus: routing por modo de
  mapeo, mirrors WRAM/SRAM, mult/div (incl. división por cero y quirk RDDIV),
  WRAM port auto-incremento, open bus, power/reset, DMA mirror `$43xF`→`$43xB`.

Fase 2 COMPLETADA. Próximos pasos (Fase 3): scheduler + PPU (consultar
`fullsnes.txt`/`fullsnes.html` antes de implementar timing de CPU/PPU/APU/DMA).

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
  `build/tools/cputest/snes_cputest` (harness).
- Comandos usados en esta sesión:
  - `cmake --build build` en el raíz.
  - `./build/core/tests/snes_tests` → **26 TEST_CASE / 212 assertions**.
  - `./build/tools/cputest/snes_cputest third_party/cputest/cputest-full.sfc`
    → SUCCESS (`0081a2`).
  - `./build/tools/cputest/snes_cputest third_party/cputest/cputest-basic.sfc`
    → SUCCESS.
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

## Cómo compilar/unir todo
- `cd snes-emu && cmake --build build` (o `make -C build`).
- Test unitarios: `./build/core/tests/snes_tests`.
- Cputest: `./build/tools/cputest/snes_cputest <ruta a cputest-full.sfc>` del
   directorio `/var/folders/...`.
- (Re)genera `build/` si CMake cambia: `cmake -S . -B build`.
- Añade `add_subdirectory` cuando crees una herramienta nueva, y no olvides el
  include de los headers de core en el CMakeLists de la tool.