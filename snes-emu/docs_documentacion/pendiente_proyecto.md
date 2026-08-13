# Pendiente para acabar el proyecto

Fecha: 2026-08-13
Estado: fases 0-6 completadas (CPU, bus, scheduler, PPU, DMA/HDMA, APU SMP+DSP).

## Obligatorio (Fase 7 — integración)

1. **Cablear `$2140-$2143`** (bus ↔ `Apu::readPort/writePort`) — handshake BBAA.
   Sin esto la mayoría de juegos se quedan esperando a la APU y no arrancan.
2. **Timers del SMP contando** (`$FA-$FF`) — algunos drivers de sonido se
   cuelgan si `TnOUT` no avanza.
3. **Frontend** (SDL2): mostrar el framebuffer del PPU + leer input (joypad).
4. **Salida de audio**: buffer de muestras del DSP → tarjeta de sonido.
5. **Save states** (guardar/cargar estado).
6. **Validación contra juegos comerciales** (LoROM/HiROM base).

### Opcional (juegos concretos)
- **Chips especiales**: SA-1, SuperFX (GSU), DSP-1/2/3/4, CX4, S-DD1, SPC7110.
  Cada uno es un módulo aparte; el diseño los deja fuera del núcleo base.

## Mejoras de audio (calidad, no bloquean)

1. **Echo/reverb** (`ESA`/`EDL`/`EON`/`EVOL`/`FIR`) — ambiente y algunos
   instrumentos.
2. **Interpolación gaussiana** — suaviza muestras a pitch distinto del grabado.
3. **Noise** (`NON`) — baterías (caja, hi-hat) y efectos (explosiones, viento).
4. **PMON** (pitch modulation) — vibrato / sweeps de frecuencia.
5. **ADSR decay/release completo** — las notas se desvanecen bien (hoy solo
   attack + sustain + gain directo).
