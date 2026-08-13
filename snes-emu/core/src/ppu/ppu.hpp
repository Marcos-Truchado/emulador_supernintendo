#pragma once

#include <functional>

#include "scheduler/thread.hpp"
#include "snes/snes.hpp"

namespace snes {

// Picture Processing Unit — Phase 4: real rendering.
//
// Phase 3 kept the dot-accurate H/V counters and interrupt semantics; phase 4
// adds the full B-Bus register file ($2100-$213F), the framebuffer, and the
// per-dot render pipeline (backgrounds modes 0-7, sprites/OAM, windows,
// color math, mode 7). Verified against fullsnes (noSns v1.6) and the ares
// PPU as the reference for formulas, ordering, timing and edge cases; the
// code below re-expresses that pipeline with its own naming.
//
// Timing model (unchanged from phase 3):
//   dot       = 4 master cycles, dot_ 0..340
//   scanline  = 341 dots,        scanline_ 0..261 (NTSC)
//   frame     = 262 scanlines
//
// Render pipeline (per line, mirrors the ares main()/cycle() cadence mapped
// onto our dot counter):
//   H=0        frame start (V=0 only: latch interlace/overscan, layer/sprite
//              frame state), then scanline-start of every engine, and at
//              V=240 the frame-end (renderedFrames_++).
//   H=0..254   sprite range check every 2 dots (probe(scanline/2)).
//   H=0..263   one map/character fetch slot per dot (fetchSlot(dot&7),
//              dispatched by BG mode), skipping the partial first columns.
//   H=14       prime the per-layer tile shifters (remove the scrolled-off
//              partial tile column).
//   H=14..269  every dot: below pass (layers), above pass (layers), sprite
//              draw, window step, compositor emit (writes 2 half-pixels).
//   H=270      sprite tile fetch (HBlank start).
//   H=270..340 HBlank: nothing render-related.
//   V>240      no per-dot work (VBlank); lines 241-261 only run the
//              scanline-start engines (compositor keeps line_ = null).
//
// Framebuffer: 564 x 242 NTSC. Row 0..239 map to visible lines; the +26
// column offset reproduces the horizontal border of the video signal, and
// non-overscan frames are shifted down 8 rows (dac line addressing). Every
// entry is 16-bit: bit15 = display-brightness flag, bits 14-0 = RGB555.
//
// Out of scope (documented in update.md): PAL, long/short scanlines, DRAM
// refresh, counter sync at V=128, interlace field counter (fieldBit() is
// derived from frame parity and only used where SETINI.1/0 is set), and
// sprite-fetch self-stepping (the fetch budget fits inside HBlank).
class Ppu : public Thread {
 public:
  Ppu();

  // Thread: advance masterCycles (must be a multiple of 4) one dot at a
  // time; the Scheduler calls this with 4.
  auto step(uint64 masterCycles) -> void override;

  // Power-on and soft-reset register/counter values (fullsnes I/O map).
  auto power() -> void;
  auto reset() -> void;

  // ---- MMIO from the bus (CPU side) ----
  auto write4200(uint8 data) -> void;  // NMITIMEN
  auto write4207(uint8 data) -> void;  // HTIMEL
  auto write4208(uint8 data) -> void;  // HTIMEH
  auto write4209(uint8 data) -> void;  // VTIMEL
  auto write420A(uint8 data) -> void;  // VTIMEH
  auto read4210() -> uint8;            // RDNMI (Read/Ack)
  auto read4211() -> uint8;            // TIMEUP (Read/Ack)
  auto read4212() -> uint8;            // HVBJOY (live)

  // Phase 4: B-Bus ports. $2100-$2133 are write-only (reads return the PPU1
  // MDR, fullsnes "PPU1 open bus = last value read from PPU1"); $2134-$213F
  // are read-only and handled by readRegister(). $2137 SLHV stays in the
  // bus (it returns the CPU open bus and is gated by WRIO bit7).
  auto writeRegister(uint8 offset, uint8 data) -> void;  // $2100-$2133
  auto readRegister(uint8 offset) -> uint8;              // $2134-$213F
  auto captureCounters() -> void;      // $2137 SLHV latch strobe
  auto setWrio(uint8 data) -> void { wrioBit7_ = (data >> 7) & 1; }  // $4201

  // ---- interrupt delivery sinks (phase 3b; wired by System) ----
  auto setNmiPin(std::function<void(bool)> sink) -> void { nmiPin_ = std::move(sink); }
  auto setIrqPin(std::function<void(bool)> sink) -> void { irqPin_ = std::move(sink); }

  // ---- counters / flags (tests, runner, later phases) ----
  auto dot() const -> uint16 { return dot_; }            // H counter 0..340
  auto scanline() const -> uint16 { return scanline_; }  // V counter 0..261
  auto frame() const -> uint64 { return frame_; }
  auto renderedFrames() const -> uint64 { return renderedFrames_; }
  auto vblankFlag() const -> bool { return vblank_; }     // $4210 bit7 latch
  auto irqFlag() const -> bool { return irqFlag_; }       // $4211 bit7 latch
  auto hblank() const -> bool { return hblank_; }         // live $4212 bit6
  auto vblankPeriod() const -> bool { return scanline_ >= 225; }
  auto nmitimen() const -> uint8 { return nmitimen_; }

  // ---- phase 4: framebuffer / memory readback (tests, runner) ----
  // 564 x 242 NTSC; entries are bit15 | RGB555 (bit15 = brightness flag).
  auto frameBuffer() const -> const uint16* { return frameBuffer_; }
  auto frameWidth() const -> int { return kFrameWidth; }
  auto frameHeight() const -> int { return kFrameHeight; }
  // Visible-coordinate read (x 0..255, y 0..239): maps to the framebuffer
  // row/column used by the compositor (border offset + overscan shift).
  auto pixelColor(int x, int y) const -> uint16;
  auto vramRead(uint16 wordAddress) const -> uint16 { return vram_[wordAddress & kVramMask]; }
  auto oamReadRaw(uint16 address) const -> uint8;
  auto cgramRead(uint8 index) const -> uint16 { return cgram_[index & 0xFF]; }

  // ---- phase 4: register readback for tests ----
  auto bgMode() const -> uint8 { return io_.mode; }
  auto displayDisabled() const -> bool { return io_.displayDisable; }
  auto oamAddress() const -> uint16 { return io_.oamAddr; }
  auto vramAddress() const -> uint16 { return io_.vramAddr; }
  auto cgramAddress() const -> uint8 { return io_.cgramAddr; }
  auto statRangeOver() const -> bool { return sprites_.rangeOver; }
  auto statTimeOver() const -> bool { return sprites_.timeOver; }
  // PPU1 open bus (fullsnes): reads of the write-twice/data ports
  // ($2104-$2106, $2108-$210A, $2114-$2116, $2118-$211A, $2124-$2126,
  // $2128-$212A) return the value most recently read from $2134-$2136,
  // $2138-$213A, $213E instead of the CPU open bus.
  auto ppu1Mdr() const -> uint8 { return ppu1_.mdr; }

 private:
  static constexpr uint16 kVramMask = 0x7FFF;
  static constexpr int kFrameWidth = 564;   // 512 visible + 2 x 26 border
  static constexpr int kFrameHeight = 242;  // 240 visible + 2 border rows

  struct SpriteObj;  // full definition below (OAM section); used by SpriteEngine

  enum LayerMode : uint8 { kBpp2, kBpp4, kBpp8, kMode7, kInactive };

  struct Layer {
    Layer(Ppu& ppu, uint8 id) : ppu(ppu), id(id) {}
    Ppu& ppu;
    const uint8 id;  // 0..3 (BG1..BG4)

    // ---- per-layer register state ($2105-$2114, $212C/$212D) ----
    uint8 screenSize = 0;      // $2107-$210A bits 0-1
    uint16 screenAddress = 0;  // $2107-$210A bits 2-7, word address
    uint16 tileBase = 0;       // $210B/$210C nibble, word address
    uint8 tileSize = 0;        // $2105 bits 4-7 (8x8 / 16x16)
    uint8 mode = kInactive;    // recomputeLayers() from the BG screen mode
    uint8 priority[2] = {};    // per-tile priority bit -> display priority
    bool aboveEnable = false;  // $212C TM bit
    bool belowEnable = false;  // $212D TS bit
    uint16 hscroll = 0;        // BGnHOFS (write-twice)
    uint16 vscroll = 0;        // BGnVOFS (write-twice)

    struct Pixel {
      uint8 priority = 0;  // 0 = transparent
      uint8 palette = 0;
      uint8 paletteGroup = 0;
    } above, below;  // outputs read by the compositor

    struct MosaicS {
      bool enable = false;   // $2106 bit
      uint16 hcounter = 0;
      uint16 hoffset = 0;
      Pixel pixel;
    } mosaic;

    struct OffsetPerTile {
      uint16 hoffset = 0;  // set in BG3 only; used by BG1/BG2
      uint16 voffset = 0;
    } opt;

    struct Tile {
      uint16 address = 0;    // VRAM word address of the row to fetch
      uint16 character = 0;  // tile number (10 bits)
      uint8 palette = 0;     // CGRAM base of the tile
      uint8 paletteGroup = 0;
      uint8 priority = 0;
      bool hmirror = false;
      bool vmirror = false;
      uint16 data[4] = {};   // interleaved bitplanes (2 planes per word)
    } tiles[66];

    uint8 renderingIndex = 0;  // tile currently being rendered
    uint8 pixelCounter = 0;    // pixel within the tile (3 bits)

    // ---- per-line / per-frame ----
    auto isHires() const -> bool { return ppu.io_.mode == 5 || ppu.io_.mode == 6; }
    auto newFrame() -> void;   // V=0
    auto lineStart() -> void;  // H=0
    auto prime() -> void;      // H=14: drop the scrolled-off tile column
    auto loadMap() -> void;    // one 16-bit map entry per 8-dot slot
    auto loadOffsets(uint32 y) -> void;   // BG3 offset-per-tile entries
    auto loadPlanes(uint32 index, bool half = false) -> void;  // tile words
    auto draw(bool belowPass) -> void;    // one pixel, below/above pass
    auto mode7Draw() -> void;             // rotation/scaling path
    auto resetState() -> void;
  };

  struct SpriteEngine {
    SpriteEngine(Ppu& ppu) : ppu(ppu) {}
    Ppu& ppu;

    // ---- register state ($2101, $212C/$212D, $2133.1) ----
    bool aboveEnable = false;  // $212C bit4
    bool belowEnable = false;  // $212D bit4
    bool interlace = false;    // $2133 bit1
    uint16 tileBase = 0;       // $2101 bits 0-2
    uint8 nameselect = 0;      // $2101 bits 3-4
    uint8 baseSize = 0;        // $2101 bits 5-7
    uint8 priority[4] = {};    // per-sprite priority -> display priority
    bool rangeOver = false;    // >32 sprites on one line
    bool timeOver = false;     // >34 sprite tiles on one line

    uint8 firstSprite = 0;      // $2103 bit7 priority rotation
    uint8 latchedFirst = 0;     // latched at line start

    struct Item {
      bool valid = false;
      uint8 index = 0;
    };
    struct Tile {
      bool valid = false;
      uint16 x = 0;
      uint8 priority = 0;
      uint8 palette = 0;
      bool hflip = false;
      uint32 data = 0;
    };
    struct State {
      uint32 x = 0;
      uint32 y = 0;
      uint32 itemCount = 0;
      uint32 tileCount = 0;
      bool active = false;
      Item item[2][32] = {};
      Tile tile[2][34] = {};
    } t;

    struct Pixel {
      uint8 priority = 0;
      uint8 palette = 0;
    } above, below;  // outputs read by the compositor

    auto reloadAddress() -> void;   // $2102/$2103 write or VBlank start
    auto refreshFirst() -> void;    // recompute io.firstSprite
    auto newFrame() -> void;        // V=0: clear overflow flags
    auto lineStart() -> void;       // H=0: swap buffers, clear lists
    auto probe(uint32 index) -> void;      // range check one sprite
    auto touchesLine(const struct SpriteObj& sprite) const -> bool;
    auto draw() -> void;            // one pixel from the finished tile list
    auto loadTiles() -> void;       // H=270: fetch tile data for the line
    auto resetState() -> void;
  };

  struct WindowMask {
    WindowMask(Ppu& ppu) : ppu(ppu) {}
    Ppu& ppu;

    struct Layer {
      bool oneInvert = false;
      bool oneEnable = false;
      bool twoInvert = false;
      bool twoEnable = false;
      uint8 mask = 0;
      bool aboveEnable = false;
      bool belowEnable = false;
    } bg1, bg2, bg3, bg4, obj;

    struct Color {
      bool oneEnable = false;
      bool oneInvert = false;
      bool twoEnable = false;
      bool twoInvert = false;
      uint8 mask = 0;
      uint8 aboveMask = 0;
      uint8 belowMask = 0;
    } col;

    uint8 oneLeft = 0, oneRight = 0, twoLeft = 0, twoRight = 0;  // $2126-$2129
    uint32 x = 0;  // current pixel column (0..255 per line)

    struct Pixel {
      bool colorEnable = false;
    } above, below;

    auto lineStart() -> void;      // H=0: x = 0
    auto stepPixel() -> void;      // per dot: mask layers + color enable
    auto maskHit(bool oneEnable, bool one, bool twoEnable, bool two, uint32 mask) const -> bool;
    auto resetState() -> void;
  };

  struct Composer {
    Composer(Ppu& ppu) : ppu(ppu) {}
    Ppu& ppu;

    // ---- register state ($2130-$2132) ----
    bool directColor = false;   // $2130 bit0
    bool blendMode = false;     // $2130 bit1
    bool colorMode = false;     // $2131 bit7 (0=add, 1=sub)
    bool colorHalve = false;    // $2131 bit6
    bool bg1ColorEnable = false, bg2ColorEnable = false, bg3ColorEnable = false,
         bg4ColorEnable = false, objColorEnable = false, backColorEnable = false;
    uint8 colorRed = 0, colorGreen = 0, colorBlue = 0;  // $2132

    struct Screen {
      uint16 color = 0;
      bool colorEnable = false;
    } above, below;
    bool transparent = false;
    bool mathBlendMode = false;
    bool mathColorHalve = false;

    uint16* line = nullptr;  // current row cursor into ppu.frameBuffer_

    auto lineStart() -> void;      // H=0: point the row cursor
    auto emitPixel() -> void;      // per dot: pick + blend + write 2 halves
    auto pickSub(bool hires) -> uint16;   // front-most sub-screen pixel
    auto pickMain() -> uint16;            // front-most main-screen pixel
    auto mixColors(uint32 x, uint32 y) const -> uint16;  // add/sub + halve
    auto lookupColor(uint8 index) const -> uint16;
    auto lookupDirectColor(uint8 palette, uint8 group) const -> uint16;
    auto coldataColor() const -> uint16;
    auto resetState() -> void;
  };

  struct Mosaic {
    Mosaic(Ppu& ppu) : ppu(ppu) {}
    Ppu& ppu;
    uint8 size = 0;      // $2106 bits 4-7 + 1
    uint8 vcounter = 0;  // vertical position within the current block

    auto active() const -> bool;
    auto offset() const -> uint32 { return size - vcounter; }
    auto lineStart() -> void;  // H=0: reload the block counter
    auto resetState() -> void;
  };

  // ---- sprite object data (OAM) ----
  struct SpriteObj {
    uint16 x = 0;         // 9 bits
    uint8 y = 0;
    uint8 character = 0;
    bool nameselect = false;
    bool vflip = false;
    bool hflip = false;
    uint8 priority = 0;
    uint8 palette = 0;
    bool size = false;
  };

  // ---- memory ----
  uint16 vram_[32768] = {};
  SpriteObj oamObj_[128] = {};
  uint16 cgram_[256] = {};

  struct IoRegs {
    uint8 displayBrightness = 0;  // $2100 bits 0-3
    bool displayDisable = false;  // $2100 bit7

    uint16 oamBaseAddr = 0;  // $2102/$2103 reload value (10 bits)
    uint16 oamAddr = 0;      // $2102/$2103 current address (10 bits)
    bool oamPriority = false;  // $2103 bit7

    uint8 mode = 0;        // $2105 bits 0-2
    bool bgPriority = false;  // $2105 bit3

    uint16 hoffsetMode7 = 0;  // $210D (M7HOFS)
    uint16 voffsetMode7 = 0;  // $210E (M7VOFS)

    uint8 vramIncrementSize = 0;  // $2115 bits 0-1 -> {1,32,128,128}
    uint8 vramMapping = 0;        // $2115 bits 2-3
    bool vramIncrementMode = false;  // $2115 bit7

    uint16 vramAddr = 0;  // $2116/$2117 (word address)

    bool hflipMode7 = false;   // $211A bit0
    bool vflipMode7 = false;   // $211A bit1
    uint8 repeatMode7 = 0;     // $211A bits 6-7

    uint16 m7a = 0, m7b = 0, m7c = 0, m7d = 0;  // $211B-$211E
    uint16 m7x = 0, m7y = 0;                    // $211F/$2120

    uint8 cgramAddr = 0;  // $2121
    bool cgramAddrLatch = false;  // 1st/2nd access flipflop

    bool interlace = false;   // $2133 bit0
    bool overscan = false;    // $2133 bit2
    bool pseudoHires = false; // $2133 bit3
    bool extbg = false;       // $2133 bit6

    uint16 hcounter = 0;  // $213C latched H counter
    uint16 vcounter = 0;  // $213D latched V counter
  } io_;

  struct Latch {
    uint16 vram = 0;      // VRAM read prefetch register
    uint8 oam = 0;        // OAM write-twice low byte
    uint8 cgram = 0;      // CGRAM write-twice low byte
    uint8 bgofsPPU1 = 0;  // scroll write-twice latch
    uint8 bgofsPPU2 = 0;  // scroll write-twice latch (3 bits, fullsnes)
    uint8 mode7 = 0;      // M7 write-twice latch
    bool counters = false;  // $213F bit6 latch flag
    bool hcounter = false;  // OPHCT 1st/2nd read flipflop
    bool vcounter = false;  // OPVCT 1st/2nd read flipflop
    uint16 oamAddr = 0;   // OAM address during rendering
    uint8 cgramAddr = 0;  // CGRAM address during rendering
  } latch_;

  struct Mdr {
    uint8 version = 1;
    uint8 mdr = 0;
  } ppu1_, ppu2_;

  struct State {
    bool interlace = false;   // latched at V=0
    bool overscan = false;    // latched at V=0
    uint16 vdisp = 225;       // live: !overscan ? 225 : 240
  } state_;

  // ---- engines ----
  Layer layers_[4];
  SpriteEngine sprites_;
  WindowMask window_;
  Composer composer_;
  Mosaic mosaic_;

  // ---- timing state (phase 3) ----
  uint16 dot_ = 0;
  uint16 scanline_ = 0;
  uint64 frame_ = 0;
  uint64 renderedFrames_ = 0;
  bool pendingStart_ = false;  // H=0 V=0 work deferred to the first step (power)

  uint8 nmitimen_ = 0;
  uint16 htime_ = 0;
  uint16 vtime_ = 0;
  bool vblank_ = false;
  bool irqFlag_ = false;
  bool hblank_ = false;
  bool nmiEdgePrev_ = false;
  bool wrioBit7_ = false;

  std::function<void(bool)> nmiPin_;
  std::function<void(bool)> irqPin_;

  uint16 frameBuffer_[kFrameWidth * kFrameHeight] = {};

  // ---- phase 3 timing ----
  auto advanceDot() -> void;
  auto driveIrqPin() -> void;

  // ---- phase 4 rendering ----
  auto paintDot() -> void;   // per-dot render work (called from advanceDot)
  auto fetchSlot(uint32 slot) -> void;  // per-dot fetch dispatch by BG mode
  auto startLine() -> void;  // H=0: frame start (V=0 only) + engine line starts
  auto vdisp() const -> uint32 { return state_.vdisp; }
  // Field parity derives from the interlace bit latched at frame start
  // (state_.interlace, see startLine) combined with the frame parity.
  auto fieldBit() const -> bool { return state_.interlace && (frame_ & 1); }
  auto overscanActive() const -> bool { return state_.overscan; }

  // ---- phase 4 MMIO helpers ----
  auto resetRegisters() -> void;
  auto mapVramAddress() const -> uint16;
  auto readVramWord() -> uint16;
  auto writeVramByte(bool high, uint8 data) -> void;
  auto readOamByte(uint16 address) -> uint8;
  auto writeOamByte(uint16 address, uint8 data) -> void;
  auto readCgramByte(bool high, uint8 address) -> uint8;
  auto writeCgramWord(uint8 address, uint16 data) -> void;
  auto recomputeLayers() -> void;
  auto objectWidth(const SpriteObj& obj) const -> uint32;
  auto objectHeight(const SpriteObj& obj) const -> uint32;
};

}  // namespace snes
