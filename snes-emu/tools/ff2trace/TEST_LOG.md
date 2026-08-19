# FF2 Trace Tests

ROM: `Final Fantasy II (V1.0) (U).smc`

| Test | Input | Result |
| --- | --- | --- |
| Baseline | No input, 3600 frames | Static title; matches headless snes9x. |
| Start input before joypad fix | Start (`0x1000`) | No response; invalid serial-to-parallel layout. |
| Start after mask fix | Start (`0x1000`) at frame 250, hold 24 | Readback is `0x1000`; no intro transition. |
| A after mask fix | A (`0x0080`) at frame 250, hold 24 | Fade out, APU transfer, intro map and ship scenes run. |
| Long intro | Same A, 6000 frames | Intro reaches map, ship and battle scenes; no CPU/PPU deadlock. |
| A during dialogue | A (`0x0080`) at frame 2000, hold 5 | Readback is `0x0001`; no reliable dialogue transition at that timing. |

The earlier rows that treated `0x1000 -> 0x0080` as a conversion were invalid and
must not be used as reference; `$4218` uses the public masks directly, as in snes9x.

| Control/frontend check | Result |
| --- | --- |
| `snes_tests --test-case='bus: joypad auto-read ($4218/$4219) and manual shift ($4016)'` | 19/19 passed after removing the false conversion. |
| Frontend build | `snes_frontend` links successfully. |
| SDL dummy launch | FFII, Super Mario World and Donkey Kong Country 2 load and run until timeout without load errors. |

Frontend map: arrows/WASD = D-pad; `X`/Space = A; `Z`/Q = B; `V` = X;
`C` = Y; `U` = L; `I` = R; Return = Start; Right Shift/E = Select;
`F5` saves state, `F8` loads state, and Escape returns to the launcher.

Current confirmed blocker: none in the initial boot/intro path. The next test must
identify the exact frame and button that advances the post-intro title, using a
reference emulator or a controlled input sweep. Do not repeat the rows above.
