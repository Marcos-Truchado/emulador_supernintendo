# Fase 3 — Scheduler relativo CPU↔PPU: Diseño y desarrollo

> Documento de diseño para el desarrollo futuro de la Fase 3. Contiene toda la
> investigación del chat, las decisiones tomadas y las correcciones críticas
> (verificadas contra `fullsnes.txt` local, noSns v1.6). **Sin implementación.**
>
> Fuentes de verdad:
> - `docs_documentacion/fullsnes.txt` — noSns v1.6 (referencias por línea citadas en §15).
> - byuu, *Emulator Schedulers* — https://bsnes.org/articles/schedulers
>   (byuu.net da error de transporte; archivado también en
>   `github.com/higan-emu/emulation-articles`).
> - Roadmap: `emulador_snes_diseno.md` (§0.1 scheduler, §1.2 relojes, §9 roadmap, §10 testing).

---

## 1. Objetivo y criterio de salida

**Roadmap §9 Fase 3 (texto literal del diseño):** scheduler relativo estilo byuu
(contador delta CPU/PPU + `catch_up`); PPU **sin renderizado real**, solo generando
el **timing correcto** (VBlank/HBlank/dots) y **disparando NMI/IRQ en el momento
exacto**. Criterio de salida: **test ROM sencillo que solo cuente VBlanks/IRQs
con conteo exacto**.

Puntos clave del diseño (roadmap §0.1, §1.2):
- El scheduler es "el punto más delicado" del emulador; se valida con conteo exacto
  ANTES de conectar el renderizado real (Fase 4).
- CPU y PPU comparten el reloj maestro NTSC de **21.47727 MHz**; la PPU avanza
  **4 ciclos maestro por dot** (ciclos: 1364/scanline, 341 dots).
- SMP/DSP usan reloj propio (~24.576 MHz, razón 21:24) — Fase 6, la abstracción
  `Thread` debe dejarlo preparado.

---

## 2. Estado actual (Fase 2 cerrada)

- `snes_tests` = **26 TEST_CASE / 212 assertions** verdes (8 CPU, 7 cartridge, 11 bus).
- `cputest-full` (240228 instrucciones) y `cputest-basic` (164776): ambos
  `SUCCESS: reached 0081a2`.
- Bus completo: mapeo LoROM/HiROM/ExHiROM, WRAM 128KB + mirrors, SRAM, MMIO
  (mult/div, WRAM port, sombras PPU, APU ports, DMA regs + mirror `$43xF`→mismo
  canal, joypad stub), open bus `latch()`, power/reset fullsnes.
- `update.md` documenta el estado (título "Fase 2", §6 mapa/MMIO/power-reset, §5 comandos).

---

## 3. Decisiones ya tomadas (preguntas clarificadoras del chat)

### 3.1 Test ROM: **Ambos**
1. **ROM propia forjada con Python 3.9.6** — instrucciones 65816 ensambladas a
   mano dentro del script, autocontenida, versionada en `third_party/` (mismo
   patrón que cputest). No hay ca65/ensamblador instalado.
2. **Test ROMs de la comunidad** (blargg: `vblnk_time`, `nmi_test`, `irq_test`)
   como validación extra — requieren entrega real de interrupciones al CPU
   (ver §10.4, sub-paso 3b).

### 3.2 Fast/slow: **completo ya** (no en Fase 7)
- El timing **NO se lee de la cabecera `$7FD5`** (bit4 es solo informativo).
- El registro que manda en runtime es **`$420D` MEMSEL bit0** (0=SlowROM,
  1=FastROM; **0 en reset**), escrito por el juego.
- Franjas (ver §9): 12 ciclos joypad; 6 fijos I/O; 8 fijos WRAM/WS1; WS2 conmutable.
- Implica: `ciclos_acceso(direccion) -> u8` en el bus, `$420D` registrable,
  helpers de memoria de la CPU multiplicando ciclos por acceso.
- cputest no se ve afectado (SlowROM por defecto, y su ROM va en WS1).

### 3.3 Área conmutable: **fullsnes literal, solo WS2 conmuta**
- WS1 (`00-3F:8000-FFFF` LoROM + `40-7D:0000-FFFF` HiROM) = **2.68MHz (8 ciclos),
  FIJO, nunca conmuta**.
- WS2 (`80-BF:8000-FFFF` + `C0-FF:0000-FFFF`) = conmuta **8/6** según `$420D`
  bit0 (2.68MHz = 21.47727/8; 3.58MHz = 21.47727/6).
- ⚠️ La spec original del usuario (que `00-3F/80-BF` conmutan) era **incorrecta**;
  se corrigió tras consultar fullsnes (§4.5).

---

## 4. Correcciones críticas (verificadas en fullsnes local)

### 4.1 `$4210` y `$4212` **NO se comportan igual** ⚠️ (corrección del usuario)

Ambos tienen bit7 "VBlank", pero son mecanismos distintos. **No implementarlos
con la misma lógica:**

| | `$4210` RDNMI bit7 | `$4212` HVBJOY bit7 |
|---|---|---|
| Qué es | **Flag de NMI latcheado** (Read/Ack) | **Estado en vivo**: "¿estoy en VBlank ahora?" |
| Set | H=0.5, V=225 (begin of VBlank, incluso con NMI deshabilitado) | H=0, V=225 |
| Clear | Auto al final de VBlank (H=0, V=0) **Y al leer el registro** | **Nunca por lectura** — solo cuando el VBlank real termina (H=0, V=0) |
| Lecturas repetidas | Devuelve 0 tras la primera lectura | Devuelve 1 durante TODO el VBlank, sin importar cuántas veces se lea |

Consecuencia práctica (motivo de la corrección): los juegos que **polling a
`$4212` en un loop** para esperar a VBlank sin usar NMI (patrón muy común)
verían el bit desaparecer tras la primera lectura y se **colgarían o romperían
el timing** si se implementa con clear-on-read.

Fullsnes literal (líneas 757-765):
> 4210h bit7: "Vblank NMI Flag (0=None, 1=Interrupt Request) (set on Begin of Vblank)"...
> "The NMI flag gets set at begin of Vblank (this happens even if NMIs are disabled).
> The flag gets reset automatically at end of Vblank, and gets also reset after reading
> from this register."
>
> 4212h bit7: "V-Blank Period Flag (0=No, 1=VBlank)"; bit6: "H-Blank Period Flag".
> "The Hblank flag gets toggled in ALL scanlines (including during Vblank/Vsync).
> Both Vblank and Hblank are always toggling (even during Forced Blank, and no matter
> if IRQs or NMIs are enabled)."

Regla de oro: **`$4210` = evento latcheado con ack; `$4212` = espejo en vivo del
estado actual de los contadores.** La implementación de `$4212` lee el estado del
contador V en el dot actual; la de `$4210` mantiene un flag con clear-on-read.

### 4.2 Flag NMI interno del CPU (edge-detection) — nota para Fase 4+ ⚠️

Fullsnes (línea 760) documenta un **tercer flag**, interno del CPU, distinto de
`$4210.bit7`, que es el que realmente dispara la ejecución de la NMI:

> "The CPU includes another internal NMI flag, which gets set when
> '[4200h].7 AND [4210h].7' changes from 0-to-1, and gets cleared when the NMI
> gets executed (which should happen around after the next opcode) (if a DMA
> transfer is in progress, then it is somewhere after the DMA, in that case the
> NMI can get executed outside of the Vblank period, ie. at a time when [4210h].7
> is no longer set)."

Implicaciones:
- Es un mecanismo de **edge-detect**: el flag interno se activa en el flanco
  **0→1** de `NMITIMEN.7 AND RDNMI.7`, no por nivel.
- Se limpia **al ejecutarse la NMI** (~tras el siguiente opcode), no al leer `$4210`.
- Con DMA en curso, la NMI se ejecuta después de la DMA, **fuera del VBlank**
  (cuando `$4210.7` ya no está set) — por eso es edge, no nivel.
- **Fase 3: basta con modelar `$4210.bit7` tal cual.** Dejar una nota en el
  código/registro de esta semántica para no chocar en Fase 4+ al conectar NMI/IRQ
  al core del CPU (el ack por lectura de `$4210` evita la re-ejecución de NMI viejas).

### 4.3 HBlank: dots verificados H=274 set / H=1 clear (antes: 1099-1363) ⚠️

Los números "1099-1363" citados antes eran una aproximación en ciclos maestro.
Verificados en la sección SNES Timing H/V Events (líneas 14376-14401), fullsnes
da los dots **del contador H**:

- **H=274 → set hblank flag**
- **H=1 → clear hblank flag** (del siguiente escanline; el flag queda set de
  dot 274 hasta dot 0 inclusive del siguiente escanline)
- En ciclos maestro: set ≈ 274*4 = 1096, clear ≈ 1*4 = 4 (de la línea siguiente).

**Usar estos valores, no 1099/1363.**

### 4.4 VBlank: V=225 (NTSC), no V=241 ⚠️

- **H=0, V=225 → set VBlank flag** (línea 107 de la tabla de eventos).
- **H=0.5, V=225 → set NMI flag** (medio dot DESPUÉS del flag de VBlank).
- **H=0, V=0 → clear VBlank flag + reset NMI flag (auto-ack)**.
- "Begin of Vblank Period" = **V=225/240** (NTSC/PAL); dibujo: V=1..224, H=22-277.

### 4.5 Inconsistencia de redacción del propio fullsnes (Memory Map vs MEMSEL)

- La tabla **"Overall Memory Map"** (línea 203) lista:
  `80-BF:8000-FFFF WS2 LoROM "max 3.58MHz"` y `C0-FF:0000-FFFF WS2 HiROM "max 3.58MHz"`,
  sin mencionar el bit de control ni el default de 2.68MHz. Un lector naive
  concluiría que WS2 corre siempre a 3.58MHz.
- **MEMSEL** (líneas 243-249) es explícito: "Memory-2 consists of address
  8000h-FFFFh in bank 80h-BFh, and address 0000h-FFFFh in bank C0h-FFh";
  bit0 0=2.68MHz, 1=3.58MHz, **0 on reset**.
- **Resolución (ya implementada en Fase 2): MEMSEL manda.** Default = 2.68MHz
  (8 ciclos), 3.58MHz (6 ciclos) solo cuando el juego escribe `$420D` bit0=1.
  Coincide con todas las fuentes externas (byuu/Anomie) y con lo ya implementado.

---

## 5. Scheduler relativo (modelo byuu)

### 5.1 Modelo

- Un único **`int64` delta CPU↔PPU** (sin escalares: ambos corren al mismo
  reloj maestro 21.47727 MHz).
- **La CPU es el conductor** (como en bsnes): ejecuta instrucciones y cada
  acceso a bus resta sus ciclos del delta (`scheduler.step(cycles)`).
- **La PPU es el único hilo cooperativo** por ahora: suma ciclos en bucle
  (`ppu.step(4)` por dot) hasta alcanzar al CPU (`catch_up()`).
- La interfaz `Thread` (contador propio + `step()`) deja preparada la extensión
  a SMP/DSP en Fase 6 (ellos sí llevarán ratio de reloj 21:24).

Algoritmo (descripción, no código):

```
CPU (conductor):
  ejecutar instrucción:
    cada acceso a bus (read/write/idle, incl. fetch de opcode):
      acc = ciclos_acceso(direccion)          // §9
      delta -= acc
      scheduler.sync()                        // catch_up al dot actual
    al final de instrucción:
      sync()                                  // PPU al día antes de decidir IRQ/NMI

sync() = catchUp():
  mientras delta >= 0:
    ppu.step(1 dot = 4 ciclos maestro)        // avanza al dot siguiente
    delta -= 4
```

### 5.2 ¿Por qué sync tras CADA acceso a bus?

- Los reads de `$4210/$4211/$4212`, los writes de `$4200/$4207-$420A` y las
  lecturas del contador deben ver la PPU **en el dot exacto del acceso**
  (p. ej. un `LDA $4212` a mitad de instrucción ve el VBlank del dot actual).
- El patrón de polling `$4212` (esperar VBlank) depende de esto para no
  congelarse ni perder un frame.
- Coste: ~1 dot (4 ciclos maestro) por acceso — aceptable; la alternativa
  (eventos/latches) es sobre-ingeniería en esta fase.

### 5.3 Enfoques descartados (para registro)

| Enfoque | Veredicto |
|---|---|
| **A. Scheduler relativo 1:1 (ELEGIDO)** | único int64, CPU adelanta, PPU persigue, `catch_up`; igual que bsnes |
| B. Timestamps absolutos (estilo higan) | más caro y complejo sin beneficio (mismo reloj maestro) |
| C. Event queue (calendario de eventos) | innecesario: la PPU timing-only solo tiene eventos lineales por dot/scanline |

### 5.4 Estructura de archivos prevista

```
core/src/scheduler/thread.hpp      # interfaz Thread: contador master clocks + step()
core/src/scheduler/scheduler.hpp   # Scheduler: delta int64, step(), sync(), catchUp()
core/src/scheduler/scheduler.cpp
core/src/ppu/ppu.hpp               # PPU: contadores, flags, registros (API mínima)
core/src/ppu/timing.cpp            # PPU: lógica de timing-only (sin renderizado)
core/src/bus/bus.cpp               # + ciclos_acceso(addr); + $420D; reads $4210/$4211/$4212 → PPU
core/include/snes/snes.hpp         # ampliar API (PPU, Scheduler)
core/src/system/system.cpp         # step()/run(): conductor con scheduler
core/tests/scheduler_tests.cpp     # test unitario del delta/sync
core/tests/ppu_timing_tests.cpp    # test unitario de la tabla de eventos
```

---

## 6. PPU timing-only — spec exacta (verificada)

### 6.1 Relojes y derivados (fullsnes línea 14274 y SNES Timings)

```
Reloj maestro NTSC:  21.47727 MHz
1 dot          = 4 ciclos maestro
1 scanline     = 1364 ciclos maestro = 341 dots
Línea normal   = 1364 ciclos, dots H=0..339 (cuatro dots son de 5 ciclos)
Línea long     = 1368 ciclos, dots H=0..340  (solo 50Hz interlace / 30Hz)
Línea short    = 1360 ciclos (V=240 en 60Hz field=1)
Frame NTSC     = 262 scanlines, V=0..261
60.09880627 Hz = 21.477270 / (262*1364 - 4/2)   // el -4/2 = alternancia línea short
PAL:           21.281370 MHz, 312 scanlines (V=0..311), 50.00697891 Hz  → FUERA DE ALCANCE Fase 3
```

### 6.2 Contadores

- **H counter**: 0..340 (341 dots); en líneas normales llega a 339.
- **V counter**: 0..261 (NTSC); V=262 solo en interlace field=0 → fuera de alcance.
- **Frame counter**: `uint64_t`, incrementa al completar V=261.

### 6.3 Tabla de eventos H/V (VERIFICADA, fullsnes líneas 14376-14401) ⚠️

| Evento | Valor exacto fullsnes |
|---|---|
| Set VBlank flag | **H=0, V=225** |
| Set NMI flag | **H=0.5, V=225** (medio dot después del flag VBlank) |
| Clear VBlank flag + reset NMI (auto-ack) | **H=0, V=0** |
| Set HBlank flag | **H=274** |
| Clear HBlank flag | **H=1** (del siguiente escanline) |
| H-IRQ | **H = HTIME+3.5** (ver nota §7.4) |
| V-IRQ (o HV-IRQ con HTIME=0) | **H=2.5, V=VTIME** (ver nota §7.4) |
| HV-IRQ (HTIME=1..339) | **H=HTIME+3.5, V=VTIME** (ver nota §7.4) |
| Begin of Vblank Period (NMI, joypad, OAMADD) | **V=225** (NTSC) / 240 (PAL) |
| Begin of Drawing Period | V=1 |
| Draw picture | H=22-277, V=1-224 |
| REFRESH (pausa CPU 40 ciclos maestro = 10 dots) | **H=133.5** (fuera de alcance Fase 3) |
| Reload OAMADD | H=10, V=225 (fuera de alcance Fase 3) |
| Joypad auto-read | H=32.5..95.5, V=225, 4224 clks (fuera de alcance Fase 3) |
| HDMA transfers | V=0..224 en H=278; reload H=6, V=0 (fuera de alcance Fase 3) |
| Short scanline | V=240 (60Hz field=1) (fuera de alcance Fase 3) |
| Last line | V=261 (frames normales) |

Implementación del ciclo por dot:

```
ppu.step(4 ciclos maestro):            // llamado 1 vez por dot desde catch_up
  dot++
  si dot == 341: dot = 0; scanline++; si scanline == 262: scanline = 0; frame++
  evaluar eventos en el dot actual (tabla de arriba)
  actualizar flags $4210/$4211/$4212
```

---

## 7. Registros PPU — spec exacta (verificada, fullsnes líneas 738-768)

### 7.1 `$4200` NMITIMEN (W) — Interrupt Enable y Joypad Request

```
7   VBlank NMI Enable (0=Disable, 1=Enable)   ← inicialmente deshabilitado en reset
6   (no usado)
5-4 H/V IRQ mode:
      0 = Disable
      1 = At H=HTIME + V=Any
      2 = At V=VTIME + H=0
      3 = At H=HTIME + V=VTIME
3-1 (no usado)
0   Joypad Enable (0=Disable, 1=Enable Auto-Read)
```

- **Deshabilitar IRQ (bits 4-5 → 0) además ACKEA los IRQs.**
- **Deshabilitar NMI (bit7 → 0) NO ackea** (el flag de `$4210` sigue su vida).
- Consumido por la PPU (deja de ser almacenamiento muerto del bus).

### 7.2 `$4207/$4208` HTIMEL/HTIMEH (W)

```
8-0  H-Count Timer Value (0..339) (+/-1 en long/short lines) (0=leftmost)
15-9 (no usado)
```

### 7.3 `$4209/$420A` VTIMEL/VTIMEH (W)

```
8-0  V-Count Timer Value (0..261/311, NTSC/PAL) (+1 en interlace) (0=top)
15-9 (no usado)
```

### 7.4 `$4211` TIMEUP (R) — H/V-Timer IRQ Flag (Read/Ack)

```
7   H/V-Count Timer IRQ Flag (0=None, 1=Interrupt Request)
6-0 (no usado)
```

- **Clear al leer** el registro (Read/Ack).
- **EXCEPCIÓN:** si se lee exactamente en el instante del trigger (condición
  cierta durante 4-8 ciclos maestro), el CPU recibe bit7=1 **pero el flag NO se
  limpia**.
- **Clear también al deshabilitar IRQs** (`$4200` bits 4-5 → 0).
- ⚠️ **A diferencia de NMI, los handlers IRQ DEBEN ackear**: si no se lee el
  registro, la IRQ se re-ejecuta inmediatamente tras el RTI.

**Nota de implementación (inconsistencia interna documentada):** la descripción
de `$4207/$4209` dice que el flag se setea "cuando el H-Counter se iguala al
valor del registro" (`H==HTIME`, `V==VTIME`), mientras la tabla de eventos
(línea 14388) da los disparos en `HTIME+3.5` / `H=2.5, V=VTIME`. Es el mismo
tipo de redacción floja de fullsnes que en §4.5. **Decisión: implementar según
la nota de registro** (`H==HTIME` para H-IRQ y modo 3; `V==VTIME ∧ H==0` para
V-IRQ), que coincide con bsnes; los +3.5/+2.5 son el retardo de muestreo del
CPU (que de todos modos solo mira la línea de IRQ entre accesos). Refinarlo en
Fase 4+ si algún test ROM hardware-accurate lo exige.

### 7.5 `$4210` RDNMI (R) — V-Blank NMI Flag + CPU version (Read/Ack) ⚠️ §4.1

```
7   Vblank NMI Flag (0=None, 1=Interrupt Request) (set on Begin of Vblank)
3-0 CPU 5A22 Version Number = 2
```

- Set al begin of VBlank (**incluso con NMIs deshabilitados**).
- Clear automático al fin de VBlank (H=0, V=0).
- Clear **también al leer el registro** (Read/Ack) → ver §4.1.
- El único caso donde el ack importa: deshabilitar y re-habilitar NMI puede
  re-ejecutar una NMI vieja; el ack lo evita.

### 7.6 `$4212` HVBJOY (R) — flags en vivo ⚠️ §4.1

```
7   V-Blank Period Flag (0=No, 1=VBlank)      ← ESTADO EN VIVO, SIN clear-on-read
6   H-Blank Period Flag (0=No, 1=HBlank)      ← ESTADO EN VIVO
5-1 (no usado)
0   Auto-Joypad-Read Busy Flag (1=Busy)
```

- Reflejan el estado actual de los contadores (V≥225 → bit7=1; H en
  274..340∪0 → bit6=1). **Nunca se limpian por lectura.**
- El flag HBlank se alterna en **todas** las scanlines, incluidas las de
  VBlank/Vsync, y siempre (incluso en Forced Blank y con IRQ/NMI deshabilitados).

### 7.7 Estado inicial (power/reset)

- `$4200` = 0x00 (NMI off, IRQ off, joypad off).
- `$4207-$420A` = 0 (HTIME=0, VTIME=0).
- `$4210` bits 3-0 = 2 (versión CPU); bit7 = 0.
- `$4211` = 0, `$4212` = 0.
- Contadores: H=0, V=0, frame=0 → "SNES starts at this time after /RESET" (H=0, V=0, F=0).

---

## 8. Integración con la CPU

1. **`$420D` registrable** en el bus: registro 8-bit, reset/power = 0x00
   (SlowROM). Bit0 = velocidad WS2.
2. **`ciclos_acceso(direccion) -> u8`** en el bus (tabla §9), llamada por los
   helpers de memoria de la CPU (read/write/idle **e fetch de opcode** — el
   fetch también paga waitstates en SNES) → los ciclos extra del acceso se
   restan del delta antes de `sync()`.
3. **`sync()` tras cada acceso** (§5.1) → los reads `$4210/$4211/$4212` y los
   writes `$4200/$4207-$420A` ven la PPU al día.
4. **`System::step()`** = una instrucción (fetch + ejecutar + sync final).
   `System::run(n)` = bucle de `step()` (para el runner del test ROM).
5. Los reads `$4210/$4211/$4212` del bus delegan en la PPU (que hace el
   clear-on-read o devuelve el estado vivo según §7); `$4200/$4207-$420A` se
   escriben a la PPU.

---

## 9. Ciclos de acceso de memoria — spec exacta (verificada) ⚠️ §3.3

### 9.1 Tabla (fullsnes: Overall Memory Map + System Area + MEMSEL)

| Rango | Contenido | Velocidad | Ciclos |
|---|---|---|---|
| `$0000-$1FFF` (bancos 00-3F/80-BF) | Mirror 8K WRAM | 2.68MHz | **8** |
| `$2000-$20FF`, `$2200-$3FFF` | Unused (NO son mirrors WRAM) | 3.58MHz | **6** |
| `$2100-$21FF` | I/O (B-Bus) | 3.58MHz | **6** |
| `$4000-$41FF` | I/O joypad manual | 1.78MHz | **12** |
| `$4200-$5FFF` | I/O | 3.58MHz | **6** |
| `$6000-$7FFF` | Expansion | 2.68MHz | **8** |
| `$7E-$7F:0000-FFFF` | WRAM 128KB | 2.68MHz | **8** |
| `00-3F:8000-FFFF` | **WS1** LoROM (vector area incluida) | 2.68MHz | **8 FIJO** |
| `40-7D:0000-FFFF` | **WS1** HiROM | 2.68MHz | **8 FIJO** |
| `80-BF:8000-FFFF` | **WS2** LoROM | 2.68/3.58MHz | **8/6 según `$420D` bit0** |
| `C0-FF:0000-FFFF` | **WS2** HiROM | 2.68/3.58MHz | **8/6 según `$420D` bit0** |

Derivados: 2.684658 MHz = 21.47727/8 ("mismo access time que WRAM");
3.579545 MHz = 21.47727/6.

### 9.2 Reglas de implementación

- **WS1 jamás conmuta** (siempre 8). Solo WS2 responde a `$420D`.
- `$420D` bit0 = 0 en reset (SlowROM) → el estado por defecto es 8 ciclos en todo.
- La cabecera `$7FD5` es informativa; no condiciona nada.
- cputest no cambia: su ROM vive en WS1 y no toca `$420D`.

---

## 10. Test ROMs

### 10.1 ROM propia (Python 3.9.6, sin ensamblador)

- Script que emite el .sfc ensamblando 65816 a mano (opcodes con su longitud),
  autocontenido, versionado en `third_party/` (patrón cputest).
- **Lo que debe medir** (criterio de salida del roadmap):
  1. **Conteo de VBlanks**: loop de N frames detectando VBlank por polling de
     `$4212` bit7 (nunca por `$4210` — para ejercitar la corrección §4.1) y/o
     leyendo `$4210` (ack) una vez por frame. Resultado: conteo == N exacto.
  2. **Conteo de IRQs H/V**: configurar `$4200` = modo 2 (V-IRQ, VTIME fijo),
     contar triggers de `$4211` (con ack) durante M frames, comparar contra el
     valor esperado derivado de la tabla de eventos §6.3.
  3. **HBlank sampling**: leer `$4212` bit6 en un bucle y verificar el patrón
     esperado de HBlank por scanline (el timing de `ciclos_acceso` condiciona
     cuántas muestras caben — se valida el rango, no el conteo bruto).
- **Mecanismo de salida**: seguir EXACTAMENTE el del harness cputest
  (`tools/cputest/cputest.cpp` — resultado a dirección fija + runner que lo
  imprime, p. ej. `SUCCESS: reached 0081a2`).

### 10.2 Test ROMs de la comunidad (blargg)

- `vblnk_time` (cuenta VBlanks), `nmi_test`, `irq_test`.
- Requieren **entrega real de NMI/IRQ al CPU** (vectores, push/RTI, sampling de
  la línea de interrupción entre opcodes) → corresponden al sub-paso 3b (§11),
  que puede quedar como hito separado si el hilo no llega a implementarlos.
- Fuente de descarga: repositorios comunitarios de test ROMs SNES (ver
  `fullsnes.txt` / web); versionar en `third_party/` igual que cputest.

---

## 11. Orden de implementación (pasos exactos)

1. **Scheduler núcleo**: `core/src/scheduler/thread.hpp` (interfaz `Thread`:
   contador de master clocks + `step()`), `scheduler.hpp/.cpp` (delta `int64`,
   `step(cycles)` resta, `sync()`/`catchUp()` corre la PPU al día). Test unitario
   simple del contador (`scheduler_tests.cpp`).
2. **PPU timing-only**: `core/src/ppu/ppu.hpp` + `timing.cpp`: contadores
   dot/scanline/frame, evaluación de eventos por dot según §6.3, flags
   `$4210/$4211/$4212` y registros `$4200/$4207-$420A` con la semántica EXACTA
   de §7 (especialmente §7.5 vs §7.6). Test unitario de la tabla de eventos
   (`ppu_timing_tests.cpp`).
3. **Bus**: reads `$4210/$4211/$4212` y writes `$4200/$4207-$420A` delegando a
   la PPU; `$420D` registrable (reset=0x00). Test de integración bus↔PPU.
4. **Waitstates**: `ciclos_acceso(addr)` con la tabla §9 (WS1 fijo 8, WS2 por
   `$420D`, joypad 12, I/O 6); test unitario por franja.
5. **CPU**: helpers de memoria llaman `ciclos_acceso()` (incl. fetch) y
   `scheduler.sync()` tras cada acceso; `System::step()` = instrucción + sync
   final.
6. **Test ROM propio** (Python) + runner al estilo cputest; correr y ajustar
   hasta conteo exacto.
7. **Regresión completa** (§12).
8. **(3b, opcional)** Entrega de NMI/IRQ al CPU: flag interno de edge-detect
   (§4.2), sampling de la línea entre instrucciones, push de vectores
   (`$FFEA-$FFEB` NMI, `$FFEE-$FFEF` IRQ) y RTI; validar con blargg
   `nmi_test`/`irq_test`.

---

## 12. Validación

Comandos (desde el raíz `snes-emu/`):

```bash
cmake --build build                      # build completo
./build/core/tests/snes_tests            # 26 TEST_CASE / 212 assertions (regresión)
./build/tools/cputest/snes_cputest third_party/cputest/cputest-full.sfc   # SUCCESS 0081a2
./build/tools/cputest/snes_cputest third_party/cputest/cputest-basic.sfc  # SUCCESS
./build/tools/<nuevo_runner> <test_rom_fase3.sfc>                         # conteo exacto
```

- Nota: `ctest` no registra tests (el `enable_testing()` está en un
  subdirectory) → ejecutar `snes_tests` directamente.
- Si cambia CMake: `cmake -S . -B build`.
- Los tests nuevos (scheduler, ppu timing, waitstates, test ROM) deben quedar
  registrados en `update.md` al cerrar la fase, como se hizo en Fase 2.

---

## 13. Fuera de alcance de la Fase 3 (documentar en el código)

- Refresh DRAM: pausa del CPU de 40 ciclos maestro en H=133.5 (≈3% más lento).
- Long/short scanlines (±4 ciclos): V=240 short en 60Hz field=1; V=311 long en
  50Hz interlace.
- PAL (312 scanlines, 21.28137 MHz) e interlace.
- Latches de contador `$213C/$213D`, forced blank, joypad auto-read,
  OAMADD reload, HDMA/DMA timing.
- Entrega real de NMI/IRQ al core del CPU (sub-paso 3b opcional, §11.8).
- Cualquier renderizado (Fase 4).

---

## 14. Check-list del criterio de salida (roadmap §9)

- [ ] Scheduler relativo int64 con `catch_up` funcionando (CPU conduce, PPU persigue).
- [ ] PPU timing-only: 341 dots/scanline, 262 scanlines, eventos §6.3 exactos.
- [ ] `$4210` con clear-on-read y `$4212` como estado en vivo (¡no iguales!).
- [ ] IRQ H/V según nota de registro (§7.4) con ack correcto en `$4211`.
- [ ] Waitstates completos (§9) con `$420D` y WS1 fijo/WS2 conmutable.
- [ ] Test ROM propio: conteo de VBlanks/IRQs exacto en N frames.
- [ ] Regresión: `snes_tests` 26/212 + cputest-full/basic SUCCESS.
- [ ] (Extra) blargg `vblnk_time`/`nmi_test`/`irq_test` si se hace 3b.
- [ ] `update.md` actualizado con el cierre de la fase.

---

## 15. Referencias de fullsnes.txt (número de línea)

| Línea | Contenido |
|---|---|
| 201-211 | SNES Memory Map / Overall Memory Map (tabla de velocidades, §4.5) |
| 243-249 | `$420D` MEMSEL — Memory-2 Waitstate Control (§9) |
| 745-748 | `$4200` NMITIMEN (§7.1) |
| 752-755 | `$4207/$4208` HTIME, `$4209/$420A` VTIME (§7.2-7.3) |
| 757, 760 | `$4210` RDNMI + flag NMI interno del CPU (§4.1, §4.2, §7.5) |
| 764 | `$4211` TIMEUP (§7.4) |
| 765 | `$4212` HVBJOY (§7.6) |
| 721 | V counter 0..261 NTSC |
| 14263+ | SNES Timings (relojes, long/short lines, frame rate) |
| 14376-14401 | SNES Timing H/V Events — tabla VERIFICADA de eventos (§6.3) |
| 14402-14407 | PPU H-Counter-Latch quantities (0..340) |

---

## 16. Erratas corregidas en este documento (respecto al diseño previo)

1. `$4210` vs `$4212`: NO comparten clear-on-read (corrección del usuario, §4.1).
2. Flag NMI interno del CPU por edge-detect `[4200].7 AND [4210].7` (nota Fase 4+, §4.2).
3. HBlank: 1099-1363 (aprox. en ciclos maestro) → **H=274 set / H=1 clear** (dots del contador H, §4.3).
4. VBlank: V=241 → **V=225** (NTSC), NMI flag en H=0.5 (§4.4).
5. Área conmutable: spec original del usuario (00-3F/80-BF) → **solo WS2**
   (80-BF:8000-FFFF + C0-FF:0000-FFFF) (§3.3, §4.5).
6. IRQ H/V: tabla de eventos (HTIME+3.5 / H=2.5) vs nota de registro (H==HTIME) →
   implementar nota de registro, documentar la discrepancia (§7.4).
