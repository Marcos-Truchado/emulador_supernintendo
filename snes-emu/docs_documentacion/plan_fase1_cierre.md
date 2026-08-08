# Plan de cierre de Fase 1 — WAI, interrupciones externas, power/reset, ciclos

> **Para agentes:** ejecutar task por task en orden. Cada task termina con un
> deliverable verificable. Este archivo es temporal y consultable: cualquier IA
> que continúe debe leerlo entero antes de tocar el código.

**Goal:** completar la Fase 1 de la CPU 65816 (lo que la auditoría encontró
faltante): parking de WAI, interrupciones externas NMI/IRQ con su máquina de
estados, `power()` antes de `reset()` en `System::load()`, y tests unitarios de
CPU que validen estado, interrupciones y conteo de ciclos.

**Architecture:** el core ya es un port ciclo-a-ciclo del WDC65816 de
higan/ares (referencia en `/tmp/wdc/`, ares actual en GitHub). Se replica el
modelo de interrupciones de ares (`CPU::main()`: prioridad NMI > IRQ, vectores
E/native, `interrupt()` con 2 ciclos de preludio y push del P con bit X=0 en E
mode). Sin scheduler todavía: NMI/IRQ se muestrean en frontera de instrucción
(equivalente a higan pre-lastCycle; refinable en Fase 3).

**Tech Stack:** C++20, CMake/Ninja, doctest (ya integrado).

## Referencias (verificadas en esta sesión)

- fullsnes: excepciones `NNsSSSDD 7,8` (7 ciclos E, 8 native); BRK/COP
  `CPsSSSDD 7,8`; BRK/PHP empujan P con B=1 (en E mode es el bit X=1 natural),
  IRQ/NMI empujan P con B=0 (`P & ~0x10` en E mode).
- ares `sfc/cpu/cpu.cpp` `CPU::main()`: `if(r.wai) return instructionWait();
  if(r.stp) return instructionStop();` luego NMI (`vector = E?0xfffa:0xffea`)
  > IRQ (`E?0xfffe:0xffee`) > Reset.
- ares `wdc65816.cpp` `interrupt()`: `read(PC.d); idle(); N push(PC.b);
  push(PC.h); push(PC.l); push(EF ? P & ~0x10 : P); IF=1; DF=0; PC=vector;`
- ares `wdc65816.hpp`: `irq` = pin; `wai` = "cleared after interrupt
  triggered"; `stp` = "never cleared". WAI aparca `while(wai && !synchronizing())
  idle();` + un `idle()` final.
- ares `sfc/cpu/irq.cpp`: `irqTest()` gatea IRQ con `!r.p.i`.

## Estado actual (qué falta)

| # | Falta | Archivo | Estado |
|---|-------|---------|--------|
| 1 | WAI no aparca (executa la siguiente instrucción) | `cpu65816.cpp` `execute()` | BUG |
| 2 | STP no consume ciclos de idle mientras está parado | `cpu65816.cpp` `execute()` | menor |
| 3 | `interruptPending()` hardcodeado `false`; sin NMI/IRQ | `cpu65816.hpp` | FALTA |
| 4 | `interrupt()` (2 ciclos preludio + push + vector) | `cpu65816.cpp` | FALTA |
| 5 | `System::load()` no llama `power()` antes de `reset()` | `system.cpp` | FALTA |
| 6 | Sin tests unitarios de CPU (solo cartucho/bus) | `core/tests/` | FALTA |
| 7 | Conteo de ciclos nunca validado | `core/tests/` | FALTA |

---

## Task 1: WAI/STP parking en `execute()`

**Files:**
- Modify: `core/src/cpu/cpu65816.cpp` (`execute()`, líneas 162-167)

**Interfaces:**
- Consumes: `waiting()`, `stopped()`, `interruptPending()` (ver Task 2),
  `idle()`, `wai`, `stp` (miembros ya existentes).
- Produces: comportamiento WAI aparcado (1 ciclo/step) y despertar con
  interrupción; STP parado para siempre consumiendo 1 ciclo/step.

- [ ] **Step 1: Reescribir `execute()`** en `core/src/cpu/cpu65816.cpp`:

```cpp
auto Cpu65816::execute() -> uint64 {
  cycles_ = 0;
  if (stopped()) { idle(); return cycles_; }   // STP: hard stop, idles forever
  if (waiting()) {                             // WAI: parked until interrupt
    if (!interruptPending()) { idle(); return cycles_; }
    wai = false;
    idle();                                    // trailing idle before dispatch
  }
  if (!interruptPending()) { instruction(); return cycles_; }
  if (nmi) { nmi = false; vector = EF ? 0xfffa : 0xffea; interrupt(); return cycles_; }
  if (irq) { irq = false; vector = EF ? 0xfffe : 0xffee; interrupt(); return cycles_; }
  return cycles_;
}
```

(Compila solo tras la Task 2, que añade `nmi`/`interrupt()`.)

## Task 2: Interrupciones externas NMI/IRQ

**Files:**
- Modify: `core/src/cpu/cpu65816.hpp`
- Modify: `core/src/cpu/cpu65816.cpp`

**Interfaces:**
- Consumes: `push/pull`, `read`, `idle`, `EF`, `P`, `vector` (existentes).
- Produces:
  - Público: `auto setNmi(bool value) -> void;`
  - Público: `auto setIrq(bool value) -> void;`
  - Público: `auto interruptPending() const -> bool;` (real, no hardcode)
  - Privado: `bool nmi = 0;`
  - Privado: `auto interrupt() -> void;`

- [ ] **Step 1: Header** (`cpu65816.hpp`):
  - Reemplazar la línea 63 `auto interruptPending() const -> bool { return false; }`
    por la declaración `auto interruptPending() const -> bool;`
  - Añadir a la sección pública (tras `interruptPending()`):
    ```cpp
    // external interrupt pins (level model; cleared on dispatch)
    auto setNmi(bool value) -> void;
    auto setIrq(bool value) -> void;
    ```
  - Añadir a la sección privada (junto a `bool irq = 0;`, línea 153):
    ```cpp
    bool nmi = 0;  // NMI pin (cleared on dispatch, NMI > IRQ priority)
    ```
  - Añadir a la sección privada (junto a `instructionInterrupt`, línea 310):
    ```cpp
    auto interrupt() -> void;  // external interrupt dispatch (NMI/IRQ)
    ```

- [ ] **Step 2: Implementar** en `cpu65816.cpp` (tras `execute()`):

```cpp
auto Cpu65816::interruptPending() const -> bool {
  return nmi || (irq && !IF);   // IRQ gated by the I flag (ares irqTest)
}

auto Cpu65816::interrupt() -> void {
  read(PC.d);                   // N: prelude bus read
  idle();                       // N: prelude idle
  if (!EF) push(PC.b);          // PBR only in native mode
  push(PC.h);
  push(PC.l);
  push(EF ? uint8(P) & ~0x10 : uint8(P));  // B flag = 0 in E mode
  IF = 1;
  DF = 0;
  PC.l = read(vector.w + 0);
  PC.h = read(vector.w + 1);
  PC.b = 0x00;
}

auto Cpu65816::setNmi(bool value) -> void { nmi = value; }
auto Cpu65816::setIrq(bool value) -> void { irq = value; }
```

- [ ] **Step 3: Limpiar pins en power/reset** (`cpu65816.cpp`): en `power()`
  tras `irq = 0;` añadir `nmi = 0;`; en `reset()` tras `irq = 0;` añadir
  `nmi = 0;`.

## Task 3: `power()` antes de `reset()` en `System::load()`

**Files:**
- Modify: `core/src/system/system.cpp` (líneas 18-22)

- [ ] **Step 1:** en `System::load()`, antes de `cpu_->reset();` añadir
  `cpu_->power();`:
  ```cpp
  if (!cartridge_->load(filename, error)) return false;
  cpu_->power();   // fresh power-on state, then load the reset vector
  cpu_->reset();
  return true;
  ```

## Task 4: Tests unitarios de CPU

**Files:**
- Create: `core/tests/cpu_tests.cpp`
- Modify: `core/tests/CMakeLists.txt` (añadir `cpu_tests.cpp` y include dir
  `${CMAKE_SOURCE_DIR}/core/src` para `cpu/cpu65816.hpp`)

**Interfaces:**
- Consumes: `snes::Memory`, `Cpu65816` (`power/reset/execute/pc/setPc/stack/
  flagP/emulation/waiting/stopped/acc/xReg/yReg/setNmi/setIrq`), doctest.

- [ ] **Step 1: Crear `core/tests/cpu_tests.cpp`** con el stub de memoria y los
  8 TEST_CASE:

```cpp
#include <doctest/doctest.h>

#include "snes/snes.hpp"
#include "cpu/cpu65816.hpp"

namespace {

// Minimal 64K memory for CPU-only tests (bank 0).
struct TestMemory : snes::Memory {
  snes::uint8 data[0x10000] = {};
  auto read(snes::uint24 address) -> snes::uint8 override {
    return data[address & 0xffff];
  }
  auto write(snes::uint24 address, snes::uint8 data) -> void override {
    this->data[address & 0xffff] = data;
  }
};

void poke(TestMemory& m, snes::uint24 addr, snes::uint8 v) {
  m.data[addr & 0xffff] = v;
}
void poke16(TestMemory& m, snes::uint24 addr, snes::uint16 v) {
  m.data[addr & 0xffff] = v & 0xff;
  m.data[(addr & 0xffff) + 1] = v >> 8;
}

}  // namespace

TEST_CASE("cpu: power and reset state") {
  TestMemory mem;
  Cpu65816 cpu(mem);
  cpu.power();
  CHECK(cpu.emulation());
  CHECK(cpu.flagP() == 0x34);
  CHECK(cpu.pc() == 0);
  CHECK(cpu.stack() == 0x01ff);

  poke16(mem, 0xfffc, 0x8000);
  cpu.reset();
  CHECK(cpu.pc() == 0x008000);
  CHECK(cpu.emulation());
  CHECK(cpu.flagP() == 0x34);
  CHECK((cpu.flagP() & 0x30) == 0x30);  // M=X=1 forced in E mode
  CHECK(cpu.xReg() == 0);
  CHECK(cpu.yReg() == 0);
}

TEST_CASE("cpu: WAI parks until NMI wakes it") {
  TestMemory mem;
  Cpu65816 cpu(mem);
  cpu.power();
  poke16(mem, 0xfffc, 0x8000);
  cpu.reset();
  poke(mem, 0x8000, 0xcb);  // wai
  poke(mem, 0x8001, 0xea);  // nop (must NOT execute while parked)
  poke16(mem, 0xffea, 0x0100);  // E-mode NMI vector

  CHECK(cpu.execute() == 0);        // wai: flag only
  CHECK(cpu.pc() == 0x008001);
  CHECK(cpu.waiting());
  CHECK(cpu.execute() == 1);        // parked: 1 idle/step
  CHECK(cpu.pc() == 0x008001);
  CHECK(cpu.execute() == 1);

  cpu.setNmi(true);
  CHECK(cpu.execute() == 8);        // wake: 1 trailing idle + 7 (E-mode NMI)
  CHECK_FALSE(cpu.waiting());
  CHECK(cpu.pc() == 0x000100);
  CHECK(cpu.flagP() & 0x04);        // I=1 after interrupt
  CHECK(cpu.stack() == 0x01fc);     // 3 pushes in E mode
  CHECK((mem.data[0x01fd] & 0x10) == 0);  // B flag pushed as 0
  CHECK(mem.data[0x01fe] == 0x01);  // PCL
  CHECK(mem.data[0x01ff] == 0x80);  // PCH
}

TEST_CASE("cpu: NMI in native mode pushes 4 bytes") {
  TestMemory mem;
  Cpu65816 cpu(mem);
  cpu.power();
  poke16(mem, 0xfffc, 0x8000);
  cpu.reset();
  poke(mem, 0x8000, 0x18);  // clc
  poke(mem, 0x8001, 0xfb);  // xce -> E=0
  poke(mem, 0x8002, 0xea);  // nop
  poke16(mem, 0xffea, 0x0100);  // native NMI vector

  cpu.execute();             // clc
  cpu.execute();             // xce
  CHECK_FALSE(cpu.emulation());

  cpu.setNmi(true);
  CHECK(cpu.execute() == 8);        // native NMI: 2 prelude + 4 push + 2 read
  CHECK(cpu.pc() == 0x000100);
  CHECK(cpu.stack() == 0x01fb);     // 4 pushes
  CHECK(mem.data[0x01ff] == 0x00);  // PBR (deepest)
  CHECK(mem.data[0x01fe] == 0x80);  // PCH
  CHECK(mem.data[0x01fd] == 0x02);  // PCL
  CHECK(mem.data[0x01fc] == 0x35);  // P (C=1 from XCE)
}

TEST_CASE("cpu: IRQ is gated by the I flag") {
  TestMemory mem;
  Cpu65816 cpu(mem);
  cpu.power();
  poke16(mem, 0xfffc, 0x8000);
  cpu.reset();
  poke(mem, 0x8000, 0xea);  // nop (I=1 in E mode: IRQ must not fire)
  poke(mem, 0x8001, 0x58);  // cli
  poke(mem, 0x8002, 0xea);  // nop (IRQ fires here)
  poke16(mem, 0xfffe, 0x0200);  // E-mode IRQ vector (shared with BRK)

  cpu.setIrq(true);
  cpu.execute();             // nop: IRQ ignored (I=1)
  CHECK(cpu.pc() == 0x008001);
  CHECK(cpu.flagP() & 0x04);

  cpu.execute();             // cli: I=0
  CHECK((cpu.flagP() & 0x04) == 0);

  CHECK(cpu.execute() == 7); // IRQ fires (E mode): 2 prelude + 3 push + 2 read
  CHECK(cpu.pc() == 0x000200);
  CHECK(cpu.flagP() & 0x04); // I set again
}

TEST_CASE("cpu: NMI takes priority over IRQ") {
  TestMemory mem;
  Cpu65816 cpu(mem);
  cpu.power();
  poke16(mem, 0xfffc, 0x8000);
  cpu.reset();
  poke(mem, 0x8000, 0x58);  // cli
  poke(mem, 0x8001, 0xea);  // nop
  poke16(mem, 0xffea, 0x0100);  // NMI vector
  poke16(mem, 0xfffe, 0x0200);  // IRQ vector

  cpu.execute();             // cli
  cpu.setNmi(true);
  cpu.setIrq(true);
  cpu.execute();
  CHECK(cpu.pc() == 0x000100);   // NMI wins
  CHECK_FALSE(cpu.irqLine() == false);  // (IRQ pin still held; dispatched later)
}

TEST_CASE("cpu: canonical cycle counts") {
  struct Case {
    const char* name;
    std::vector<snes::uint8> prog;
    int cycles;
  };
  const std::vector<Case> cases = {
      {"nop", {0xea}, 2},
      {"lda #imm", {0xa9, 0x12}, 2},
      {"lda dp", {0xa5, 0x34}, 3},
      {"lda abs", {0xad, 0x00, 0x20}, 4},
      {"lda abs,x (8-bit X)", {0xbd, 0x00, 0x20}, 4},
      {"lda (dp),y (Y=0)", {0xb1, 0x34}, 5},
      {"jmp abs", {0x4c, 0x00, 0x80}, 3},
      {"jsr abs", {0x20, 0x00, 0x80}, 6},
      {"rts", {0x60}, 6},
      {"bne not taken", {0xd0, 0x00}, 2},
      {"bne taken", {0xd0, 0x02}, 3},
      {"xce", {0xfb}, 2},
      {"brk (E mode)", {0x00}, 7},
  };
  for (const auto& c : cases) {
    TestMemory mem;
    Cpu65816 cpu(mem);
    cpu.power();
    for (size_t i = 0; i < c.prog.size(); i++) poke(mem, 0x8000 + i, c.prog[i]);
    poke16(mem, 0xfffc, 0x8000);
    cpu.reset();
    cpu.setPc(0x8000);
    CHECK_MESSAGE(cpu.execute() == (snes::uint64)c.cycles, c.name);
  }
}

TEST_CASE("cpu: STP halts forever, WAI parked cycles") {
  TestMemory mem;
  Cpu65816 cpu(mem);
  cpu.power();
  poke16(mem, 0xfffc, 0x8000);
  cpu.reset();
  poke(mem, 0x8000, 0xdb);  // stp

  CHECK(cpu.execute() == 0);        // STP: flag only
  CHECK(cpu.stopped());
  CHECK(cpu.execute() == 1);        // parked idle
  CHECK(cpu.execute() == 1);
  CHECK(cpu.pc() == 0x008001);
}
```

- [ ] **Step 2: `core/tests/CMakeLists.txt`**:

```cmake
add_executable(snes_tests
  main.cpp
  cartridge_tests.cpp
  cpu_tests.cpp
)
target_include_directories(snes_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party ${CMAKE_SOURCE_DIR}/core/src)
target_link_libraries(snes_tests PRIVATE snes_core)
```

(El include dir `core/src` es necesario para `#include "cpu/cpu65816.hpp"`,
mismo patrón que `tools/cputest/CMakeLists.txt`.)

## Task 5: Verificación completa

- [ ] **Step 1:** `cmake --build build` desde `snes-emu/` → debe compilar sin
  errores ni warnings nuevos.
- [ ] **Step 2:** `./build/core/tests/snes_tests` → 8 TEST_CASE nuevos + 4
  existentes, todo verde.
- [ ] **Step 3:** cputest: `./build/tools/cputest/snes_cputest cputest-full.sfc`
  (desde el dir de la ROM) → debe seguir `SUCCESS` 1107/1107 (el harness no usa
  WAI/STP; STP se detecta por `stopped()` antes de `step()`, sin cambios).
- [ ] **Step 4:** Actualizar `docs_documentacion/update.md`: marcar WAI +
  NMI/IRQ + power/reset + tests de ciclos como completados (sección 5, Bug 5).

## Fuera de alcance (no tocar)

- Scheduler, PPU, APU, DMA, fast/slow ROM (Fases 2-5).
- Muestreo de interrupciones en "último ciclo de opcode" (marcadores `L` de
  ares / `lastCycle`): refinable cuando exista el scheduler (Fase 3). El modelo
  actual muestrea en frontera de instrucción.
- NMI edge-detection (el pin se modela como nivel; el dispatch lo limpia):
  cuando la PPU genere el edge real en Fase 3, se repolará.

---

## ✅ EJECUTADO (2026-08-08) — correcciones sobre el plan original

El plan se ejecutó completo. Los tests tal como estaban escritos tenían 3
errores que se corrigieron en `cpu_tests.cpp`:

1. **WAI/STP primera ejecución = 1 ciclo, no 0**: el dispatch de este port
   hace `switch(fetch())` dentro de `instruction()`, así que el fetch del
   opcode cuenta (igual que ares, que despacha `table[fetch()]`). Los
   `CHECK(cpu.execute() == 0)` de WAI/STP pasaron a `== 1`.
2. **Vector NMI en modo E es `$FFFA`, no `$FFEA`** (fullsnes): los tests WAI y
   NMI-priority pokeaban `0xffea` ("E-mode NMI vector") — mal. Corregido a
   `0xfffa`. (El test native sí usa `0xffea`, correcto.)
3. **`bne not taken` tomaba la rama**: tras reset Z=0, así que `bne` se tomaba
   (3 ciclos). Añadido campo `p` al struct de casos: `0x36` (Z=1) para el caso
   no-tomado.

Además: el TEST_CASE "NMI takes priority over IRQ" usaba `cpu.irqLine()`, que
no existe; se reemplazó por: verificar I=1 tras el NMI (el IRQ sostenido no
despacha), luego `cli` en el handler y comprobar que el IRQ sí se despacha
después (7 ciclos).

El código de producción (`execute()`, `interruptPending()`, `interrupt()`,
`setNmi/setIrq`, power/reset, `System::load()`) quedó EXACTAMENTE como en el
plan (solo `vector.w` → `vector`, porque `vector` es `uint16` plano).

Verificación final: build verde; `snes_tests` 11 casos / 80 assertions;
`cputest-full.sfc` sigue `SUCCESS` 1107/1107.
