# Fase 4 — PPU: estado actual y plan para completar

Fecha: 2026-08-11 (actualizado 2026-08-12)
Referencias: `docs_documentacion/fullsnes.txt` (noSns v1.6, fuente primaria),
`/Users/matru/Desktop/fase_4_emulador_inspiracion/` (ares PPU, referencia de
orden/timing/casos borde) y `docs_documentacion/refs/` (ares background.cpp +
dac.cpp, copiados localmente).

> ⚠️ **ESTADO REAL 2026-08-12**: el pipeline de fondos ya está IMPLEMENTADO
> (`core/src/ppu/render.cpp` existe y el render está cableado en advanceDot).
> Suite `--test-case="ppu:*"` = **40 | 37 passed | 3 failed** (mode 5, mode 6,
> INIDISP). Diagnóstico exacto de los 9 fallos restantes, fixes de test y
> próximos pasos: **ver `update.md` §10** (no leer este archivo como estado).

Objetivo de la fase 4: renderizado real del PPU — fondos (modos 0-7), sprites/OAM,
mode 7, ventanas, color math, mosaic — siguiendo TDD (tests en rojo primero).

---

## 1. Partes COMPLETAS y verificadas por tests (GREEN)

Estado actual de la suite: **61/61 test cases, 269,297 assertions, SUCCESS**.
Se ejecuta con `./build/core/tests/snes_tests "ppu:*"` desde `packages`… (repo del
emulador: `./build/core/tests/snes_tests`).

### Ciclo B-Bus MMIO completo — `core/src/ppu/io.cpp` (nuevo, con tests)
Verificado por `core/tests/ppu_mmio_tests.cpp` (21 test cases) + los tests de
timing de fase 3:

| Área | Detalle verificado |
|---|---|
| Registros de power-on | fullsnes I/O map: INIDISP=80h (blank + brillo 0), VMAIN=0Fh (paso 128, modo 3), M7A/M7B=00FFh, resto 0, STAT77 versión |
| $2100 INIDISP | forced blank; reset de dirección OAM al salir de blank en V=225 |
| $2102-$2104 OAM | write-twice (par→latch, impar→WORD), tabla alta 200h-21Fh byte único, auto-incremento, espejos 220h-3FFh |
| $2105 BGMODE | bits 0-2 |
| $2115/$2116-$2119 VRAM | incremento tras byte bajo/alto, pasos 1/32/128/128, traducción de dirección (rotación 8/9/10 bits) |
| $2139/$213A prefetch | quirk "primera palabra entregada dos veces" (refetch antes del incremento) |
| Puertas de acceso | VRAM/CGRAM bloqueadas durante display activo (dirección sí avanza) |
| $2134-$213F | MPYL/M/H (int16×int8), OAMDATAREAD, VMDATA prefetch, CGDATAREAD, OPHCT/OPVCT con flipflops 1ª/2ª lectura, STAT77 (bit4=MDR PPU1), STAT78 (bit6 latch flag, bit5 open bus PPU2), $213F reinicia latch+flipflops |
| $2137 SLHV | latch de contadores solo mientras WRIO bit7 está/estuvo puesto |
| $2130-$2132 CGWSEL/CGADSUB/COLDATA | escritura/lectura de campos (sin efecto visual todavía) |

Además, ya implementado en `io.cpp` y usado por MMIO:
- `Ppu::Ppu()` — cablea motores (`layers_`, `sprites_`, `window_`, `composer_`, `mosaic_`).
- `resetRegisters()` — valores de power-on y remapeo de modo.
- `mapVramAddress()`, `readVramWord()`/`writeVramByte()`, `readOamByte()`/`writeOamByte()`,
  `readCgramByte()`/`writeCgramWord()`, `SpriteEngine::reloadAddress()`/`refreshFirst()`,
  `Mosaic::active()`, `objectWidth()`/`objectHeight()`, `recomputeLayers()`
  (tablas de modo/prioridades para modos 0-7), `captureCounters()`,
  `writeRegister()` $2100-$2133, `readRegister()` $2134-$213F.
- `pixelColor(x, y)` — readback del framebuffer con desplazamientos +26/+8.

### Fase 3 (base, ya verde)
- Contadores H/V dot-accuracy (341 dots × 262 líneas), NMI/IRQ, VBlank/HBlank,
  HTIME/VTIME, pins — `core/tests/ppu_timing_tests.cpp`, `interrupt_tests.cpp`.

### Arquitectura del render (diseñada y fijada en `core/src/ppu/ppu.hpp`)
- Motores `Layer layers_[4]`, `SpriteEngine sprites_`, `WindowMask window_`,
  `Composer composer_`, `Mosaic mosaic_` con referencia `Ppu&`.
- Contrato de timing por dot (comentario cabecera): H=0 frame/line-start,
  H=0..254 probe sprites cada 2 dots, H=0..263 fetchSlot(dot&7), H=14 prime,
  H=14..269 below/above+obj+window+compositor, H=270 fetch tiles de sprites.
- Layout VRAM de tiles confirmado contra fullsnes (filas por pares de planos:
  2bpp=1 palabra/fila, 4bpp=2, 8bpp=4; `(origin << 3+mode) + (voffset&7)`).

---

## 2. Partes implementadas pero SIN test directo (verificar en próximos ciclos)
- Tablas de prioridad/modo de `recomputeLayers()` (comprobadas solo de forma
  indirecta a través de tests MMIO; se verifican con los tests de fondos).
- `objectWidth()`/`objectHeight()` (tablas de tamaño de sprite de fullsnes).
- `Mosaic::active()`.
- Latch de interlace/overscan: `fieldBit()` lee `io_.interlace` directo;
  el latch por frame (`state_.interlace`) lo hará el trabajo de frame-start.

---

## 3. Partes NO implementadas todavía (faltan por hacer)

### Motor de render completo — `core/src/ppu/render.cpp` (nuevo, sin crear)
- `Layer::newFrame/lineStart/prime/loadMap/loadOffsets/loadPlanes/draw/mode7Draw/resetState`
  (fetch de mapas/planos, shifter de píxeles, passes below/above, mosaic h).
- `Ppu::fetchSlot(slot)` — despacho de 8 slots por modo (tabla verificada contra ares main.cpp).
- `Ppu::paintDot()` — cadencia por dot.
- `Composer::lineStart/emitPixel/pickSub/pickMain/mixColors/lookupColor/lookupDirectColor/coldataColor/resetState`.
- `WindowMask::lineStart/stepPixel/maskHit/resetState`.
- `Mosaic::lineStart/resetState` (contador vertical de bloques).
- `SpriteEngine::lineStart/newFrame/probe/draw/loadTiles/resetState` (solo existe
  el esqueleto; probe/evaluate es el grueso).
- Cableado en `timing.cpp::advanceDot()` + llamadas a `resetState` de motores
  en `power()/reset()`.

### Tests en ROJO (escritos, sin compilar aún)
- `core/tests/ppu_bg_tests.cpp` — 12 test cases de fondos (modos 0-6):
  tile 2bpp/4bpp/8bpp, grupos de paleta, transparencia, hmirror/vmirror,
  hscroll fino, 16x16 (char+16), prioridad cruzada de capas, TM gating,
  BG4 (offset de paleta 96), offset-per-tile modos 2/6 (lag de una columna,
  vlookup), hires (half-píxeles), mosaic horizontal+vertical, INIDISP.
  **Estado: no compila** — error de doctest "Expression Too Complex" por
  `CHECK(... == 0x8000 | 0x7FFF)` (faltan paréntesis en el RHS; corrección
  mecánica pendiente).

### Lo que falta de la fase 4 entera
- Modo 7 (rotación/escalado, EXTBG, M7SEL repeat, transformación matricial).
- Ventanas ($2123-$212A, $212E-$2133) y color math (CGADSUB/COLDATA, add/sub,
  halve, direct color para modos 3/4/7 — el direct color 3/4 ya está testeado
  en el plan de bg).
- Sprites/OAM: evaluación por línea (probe), fetch de tiles, draw con
  prioridades, overflows range/time.
- Latch de interlace/overscan en frame-start y `fieldBit()` con `state_.interlace`.
- Verificación de trama completa (renderedFrames, vblank).

---

## 4. Plan para terminar la fase 4 (en orden, TDD)

1. **Arreglar la sintaxis de doctest** en `ppu_bg_tests.cpp` (paréntesis en las
   comparaciones con `|`) → compilar → confirmar ROJO (los tests fallan porque
   `fetchSlot`/`paintDot` no están cableados).
2. **Implementar el pipeline de fondos** (GREEN):
   - `core/src/ppu/render.cpp`: `Layer::*`, `fetchSlot()`, `paintDot()`,
     `Composer::*`, `WindowMask::*`, `Mosaic::*`, esqueleto de `SpriteEngine`
     (lineStart/newFrame/draw no-op seguro).
   - Cablear `advanceDot()` según el contrato de la cabecera (H=0 frame/line,
     probe, fetch, prime, passes, loadTiles) y `resetState()` en power/reset.
   - Añadir `src/ppu/render.cpp` a `core/CMakeLists.txt`.
   - Verificar: 61+ tests GREEN, `bun typecheck` (si aplica en este repo).
3. **TDD sprites/OAM**: `core/tests/ppu_sprite_tests.cpp` → completar
   `probe()`/`evaluate()`, `draw()`, `loadTiles()`, overflows, OAM address
   reset en V=225 (quirk ya testeado en MMIO), sizes 8x8/16x16/32x32/64x64.
4. **TDD modo 7**: tests de transformación M7 (A/B/C/D, HOFS/VOFS, center,
   flips, repeat modes, EXTBG) → `Layer::mode7Draw()`.
5. **TDD ventanas + color math**: tests $2123-$212A/$212E-$2133 (máscaras,
   inversión, combinación) y CGADSUB/COLDATA (add/sub/halve, FIXED, direct
   color en modo 7) → `WindowMask` completo + `Composer` blend.
6. **Cierre de fase**: latch interlace/overscan en frame-start, `fieldBit()`
   con `state_.interlace`, verificación de trama completa (frames renderizados,
   bordes, vblank), suite completa en verde.
7. (Opcional, si el rendimiento lo pide) batching de la cadencia por dot.

Criterio de éxito: suite completa GREEN con todos los features de la fase 4
(mismos resultados que fullsnes/ares para los casos probados).

---

## 5. Ficheros implicados

| Fichero | Estado |
|---|---|
| `core/src/ppu/ppu.hpp` | Arquitectura completa (motores, IoRegs, contrato timing) |
| `core/src/ppu/io.cpp` | MMIO completo — VERDE (tests) |
| `core/src/ppu/timing.cpp` | Fase 3 verde; advanceDot ya cableado al render |
| `core/src/ppu/render.cpp` | **IMPLEMENTADO** (2026-08-12) — pipeline completo; ver update.md §10 para el estado rojo/verde |
| `core/tests/ppu_mmio_tests.cpp` | 21 tests VERDE |
| `core/tests/ppu_bg_tests.cpp` | 13 tests: 10 VERDE, 3 ROJO (hires 5/6 + INIDISP; ver update.md §10.4) |
| `core/tests/CMakeLists.txt` | Ya incluye `ppu_bg_tests.cpp` |
| `core/CMakeLists.txt` | Ya incluye `src/ppu/render.cpp` |

Comandos:
- Build: `cmake --build build --target snes_tests` (desde `core/..` del repo)
- Tests: `./build/core/tests/snes_tests "ppu:*"` o completo `./build/core/tests/snes_tests`
