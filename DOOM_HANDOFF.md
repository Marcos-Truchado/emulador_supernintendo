# Handoff: Doom (SuperFX) pantalla negra tras título — continuar mañana

## Reglas del repo
- Repo: `/Users/matru/Desktop/emulador_supernintendo`, rama `main`. NO commitear sin pedirlo.
- Build: `cmake --build snes-emu/build --target snes_core snes_tests snes_frontend -j8` desde la raíz.
- Tests: `./snes-emu/build/core/tests/snes_tests -tc="superfx*"` (33/33 verde, 88 asserts).
  Suite total: 187 casos, 150 pass, **37 fallos SOLO `ppu_window` pre-existentes** (no tocar).
- Shell = zsh. Verificar siempre con build + tests + ROM real. Ser honesto (no afirmar fix sin evidencia).

## Hecho y verificado (NO rehacer)
1. **Yoshi crash paso 3460883 → ARREGLADO.** Causa: `INC/DEC R14` refrescaba el ROM-buffer;
   snes9x (`fxinst.cpp` `FX_INC`/`FX_DEC` sin `TESTR14`/`READR14`) y el HW no lo hacen.
   Fix = quitar 2 líneas de `refillRomBuffer()` en `INC`/`DEC` (`superfx.cpp`).
   Se mantiene refill en `IWT`/`LM`/`TO`. Test nuevo: `superfx: INC/DEC R14 does not
   refill ROM buffer (stale GETB)` (rojo-antes/verde-después). Yoshi: sin STP, 800+ frames,
   píxeles en frame 100.
2. **MERGE Z: NO TOCAR.** snes9x lo tiene invertido; fullsnes l.4991 (`"(not set when zero!)"`)
   + ares confirman nuestro código original. Quedó test de Z=0 como red.
3. **Fast-forward Tab** en `frontend/src/main.cpp` (8 frames/tick, mudo, limpia cola audio).
4. **Scaffolding debug BORRADO** (`getenv`/`printf`/anillos/contador). Diff actual = solo
   funcional + tests + Tab.

## Fix de hoy (aplicado, compilado, tests verdes, PENDIENTE de veredicto final)
- **Bug latch H/V:** `Bus` nunca avisaba a la PPU de escrituras a `$4201` (WRIO):
  `Ppu::setWrio` / `captureCounters` existían pero nadie los llamaba → `wrioBit7_`
  siempre false → `captureCounters()` early-return → `$2137/$213C/$213D` congelados.
  Juegos que esperan raster (Doom, StarFox poll H/V) se colgaban.
- **Fix en `bus.cpp`:** `writeCpuRegister` caso `0x01` guarda `old`, en flanco 1→0 llama
  `ppu_.captureCounters()` + siempre `ppu_.setWrio(data)`; `power()` y `reset()` llaman
  `ppu_.setWrio(0xFF)`.
- Referencia: `fullsnes.txt` l.~719 (`$2137/$213C/$213D/$213F`, flip-flops, flag bit6,
  "flipflops NO se resetean al latchear, solo al leer `$213F`").

## Estado Doom (lo que hay que mirar mañana)
- ROM: `juegos/Doom (USA).sfc` (2MB). Estado: `juegos/Doom (USA).sfc.ss` (frame 16602,
  guardado en pantalla negra post-título con audio).
- Snapshot del estado: `pc=7E2634`, `INIDISP=80`, `$212C=00`, 0/57344 píxeles,
  GSU viva (`R14` avanzando por ROM, sesiones completan), CPU viva.
- Tras el fix: el PC **salió del bucle `7E2634-7E263D`** y recorre `7E2060/00010C/7E2621…`,
  avanzando ~1 instr / 400 frames por `7E2621→7E2632`. Esa rutina es (volcado WRAM ya hecho):
  `SEP #$20, PHA, LDA $2137, LDA $213F, LDA $213D, CMP #$17, BEQ…`
  = **espera al scanline $17**. Con el latch arreglado debería salir; queda confirmar que
  `INIDISP` deja `80` y aparecen píxeles (no se vio en 6000 frames post-estado).
- Harnesses fuente (si `/tmp` se limpió, recrear; compilan con
  `clang++ -O2 -std=c++20 -I snes-emu/core/include -I snes-emu/core/src <f>.cpp snes-emu/build/core/libsnes_core.a`):
  `/tmp/doomstate.cpp` (snapshot PPU/GSU/píxeles), `/tmp/doomwait3.cpp` (avanza 6000 frames
  desde el estado e imprime `INIDISP/píxeles/pc`), `/tmp/doomgsu.cpp` (regs GSU),
  `/tmp/doomprog.cpp` (`R14/R10/R12/SFR`), `/tmp/doomwram.cpp` (dump WRAM `7E25F0-7E2660`).

## Referencias en disco
- `snes-emu/docs_documentacion/fullsnes.txt` (H/V latch ~l.719, GSU mapa ~l.4724, I/O ~l.4778).
- snes9x: `/tmp/gsuref/{fxinst.h,fxinst.cpp,fxemu.cpp}` (si falta: re-descargar de
  `https://raw.githubusercontent.com/snes9xgit/snes9x/master/`).
- Disassembly Yoshi (útil como modelo de rutinas GSU/SNES): `/tmp/yidis/disassembly/`
  (`bank0A.asm` descompresor, `bank08.asm` `gsu_decompress_lc_lz1`, `bank00.asm` setups).

## Plan para mañana (en orden)
1. Recompilar, repetir tests, cargar `Doom (USA).sfc.ss` y correr largo (con Tab):
   ¿sale de la espera al scanline 17? ¿`INIDISP != 80` + píxeles > 50?
2. Si sí → Doom funciona; siguiente: StarFox (mismo fix H/V lo desatasca) y `ppu_window`.
3. Si tras ~10000 frames sigue en `80`/negro con PC fijo → segundo bug: trazar qué lee
   (`$213D` valor devuelto vs scanline real) y auditar `io_.hcounter/vcounter` y `dot_/scanline_`.
