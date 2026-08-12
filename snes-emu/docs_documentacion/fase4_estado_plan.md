# Fase 4 — PPU: estado actual y plan para completar

Fecha: 2026-08-11 (actualizado 2026-08-13)
Referencias: `docs_documentacion/fullsnes.txt` (noSns v1.6, fuente primaria),
`/Users/matru/Desktop/fase_4_emulador_inspiracion/` (ares PPU, referencia de
orden/timing/casos borde) y `docs_documentacion/refs/` (ares background.cpp +
dac.cpp, copiados localmente).

> ✅ **ESTADO REAL 2026-08-13**: pipeline de fondos y **sprites/OAM**
> IMPLEMENTADOS y **suite completa VERDE**: 85/85 test cases, 269,463
> assertions. Fondos (modos 0-6, mosaic, offset-per-tile, hires, INIDISP):
> ver `update.md` §10.4/§10.8. Sprites/OAM (probe/draw/loadTiles, prioridades,
> overflows range/time, sizes 8x8..64x64, reset OAM en V=225): ver
> `update.md` §10.9 (un único bug del core: el trabajo H=0 de la línea 0
> nunca corría; el resto eran bugs de test). cputest-full/basic + ROM fase 3
> siguen en SUCCESS.

Objetivo de la fase 4: renderizado real del PPU — fondos (modos 0-7), sprites/OAM,
mode 7, ventanas, color math, mosaic — siguiendo TDD (tests en rojo primero).

---

## 1. Partes COMPLETAS y verificadas por tests (GREEN)

Estado actual de la suite: **85/85 test cases, 269,463 assertions, SUCCESS**
(`./build/core/tests/snes_tests`; filtro ppu: `--test-case="ppu:*"` = 51/51).

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

### Pipeline de fondos — `core/src/ppu/render.cpp` (nuevo, VERDE)
Verificado por `core/tests/ppu_bg_tests.cpp` (13 test cases) + MMIO + timing:
- `Layer::*` (lineStart/prime/loadMap/loadOffsets/loadPlanes/draw/mode7Draw/
  resetState), `fetchSlot()` (tablas de slots por modo 0-7), `paintDot()`,
  `Composer::*`, `WindowMask::*`, `Mosaic::*`.
- Fondos modos 0-6 (2bpp/4bpp/8bpp), mosaic h/v, offset-per-tile (modos 2/6),
  hires (modos 5/6), INIDISP. Detalle: `update.md` §10.2/§10.8.

### Sprites/OAM — `core/src/ppu/render.cpp` (nuevo, VERDE)
Verificado por `core/tests/ppu_sprite_tests.cpp` (11 test cases):
- `SpriteEngine::probe/draw/loadTiles` (port de ares): 32 items/línea +
  range overflow, 34 tiles/línea + time overflow, prioridades vs BG1, orden
  de solape (índice OAM menor gana), rotación de prioridad ($2103 bit7),
  hflip/vflip/nameselect, sizes 8x8..64x64 (tablas small/large), interlace
  de sprites, reset de dirección OAM en V=225.
- Un bug del core resuelto en el camino: el trabajo H=0 de la línea 0 no se
  ejecutaba tras power/reset (el contador arranca en `dot_=0` y `advanceDot()`
  incrementa antes de procesar). Fix: `startLine()` + flag `pendingStart_`.
  Detalle: `update.md` §10.9.

---

## 2. Partes implementadas pero SIN test directo (verificar en próximos ciclos)
- Tablas de prioridad/modo de `recomputeLayers()` — verificadas indirectamente
  por los tests de fondos (prioridad cruzada) y sprites (prioridad vs BG1).
- `objectWidth()`/`objectHeight()` — verificadas por el test de sizes 8x8..64x64.
- `Mosaic::active()` (io.cpp) — usada por mosaic h/v, sin test aislado.
- Latch de interlace/overscan: `fieldBit()` lee `io_.interlace` directo;
  el latch por frame (`state_.interlace`) lo hará el trabajo de frame-start
  (pendiente, ver §3).

---

## 3. Lo que falta para cerrar la fase 4

Todo lo de abajo ya existe en el código (portado de ares) pero **sin test
verde** (ver `update.md` §10.3 para el detalle):

- **Modo 7** — `Layer::mode7Draw()` completo (matriz A/B/C/D, center, flips,
  repeat modes, EXTBG). Falta TDD: tests de transformación M7 (A/B/C/D,
  HOFS/VOFS, center, flips, repeat modes, EXTBG).
- **Ventanas** — `WindowMask::stepPixel/maskHit` completos (incl. window de
  color). Falta test ($2123-$212A, $212E, $2133).
- **Color math** — `mixColors` (add/sub/halve), `lookupDirectColor`,
  `coldataColor`. **REVISAR** que `io.cpp` ($2130-$2132) conecta CGADSUB/
  COLDATA a los campos del Composer; falta test de add/sub/halve, FIXED y
  direct color en modo 7.
- **mode 4** (8bpp + offset-per-tile): `fetchSlot` case 4 existe, sin test.
- **pseudoHires** ($2133 bit3 → `io_.pseudoHires`): usado en `emitPixel`, sin test.
- **Latch de interlace/overscan** en frame-start y `fieldBit()` con
  `state_.interlace` (hoy `fieldBit()` lee `io_.interlace` directo).
- **Verificación de trama completa** (frames renderizados, bordes, vblank).

---

## 4. Plan para terminar la fase 4 (en orden, TDD)

1. ✅ **Sintaxis doctest** en `ppu_bg_tests.cpp` (paréntesis en comparaciones
   con `|`) → compilar → ROJO.
2. ✅ **Pipeline de fondos** (GREEN): `render.cpp` completo + cableado de
   `advanceDot()` + `resetState()` en power/reset + CMake.
3. ✅ **TDD sprites/OAM**: `ppu_sprite_tests.cpp` (11 test cases verdes) —
   ver `update.md` §10.9.
4. **TDD modo 7**: tests de transformación M7 (A/B/C/D, HOFS/VOFS, center,
   flips, repeat modes, EXTBG) → `Layer::mode7Draw()`.
5. **TDD ventanas + color math**: tests $2123-$212A/$212E-$2133 (máscaras,
   inversión, combinación) y CGADSUB/COLDATA (add/sub/halve, FIXED, direct
   color en modo 7) → `WindowMask` completo + `Composer` blend.
6. **Cierre de fase**: mode 4, pseudoHires, latch interlace/overscan en
   frame-start, `fieldBit()` con `state_.interlace`, verificación de trama
   completa (frames renderizados, bordes, vblank), suite completa en verde.
7. (Opcional, si el rendimiento lo pide) batching de la cadencia por dot.

Criterio de éxito: suite completa GREEN con todos los features de la fase 4
(mismos resultados que fullsnes/ares para los casos probados).

---

## 5. Ficheros implicados

| Fichero | Estado |
|---|---|
| `core/src/ppu/ppu.hpp` | Arquitectura completa (motores, IoRegs, contrato timing) |
| `core/src/ppu/io.cpp` | MMIO completo — VERDE (tests) |
| `core/src/ppu/timing.cpp` | Fase 3 verde; advanceDot cableado al render + fix H=0 línea 0 (`startLine`/`pendingStart_`) |
| `core/src/ppu/render.cpp` | IMPLEMENTADO — pipeline completo (Layer, fetchSlot, Composer, WindowMask, Mosaic, SpriteEngine) |
| `core/tests/ppu_mmio_tests.cpp` | 21 tests VERDE |
| `core/tests/ppu_bg_tests.cpp` | 13 test cases fondos: **13 verdes** |
| `core/tests/ppu_sprite_tests.cpp` | 11 test cases sprites/OAM: **11 verdes** |
| `core/tests/CMakeLists.txt` | Incluye `ppu_bg_tests.cpp` y `ppu_sprite_tests.cpp` |
| `core/CMakeLists.txt` | Incluye `src/ppu/render.cpp` |

Comandos:
- Build: `cmake --build build --target snes_tests` (desde `core/..` del repo)
- Tests: `./build/core/tests/snes_tests "ppu:*"` o completo `./build/core/tests/snes_tests`
