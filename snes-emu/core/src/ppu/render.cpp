#include "ppu/ppu.hpp"

#include <cstdio>
#include <cstdlib>

static bool ppuDebug() {
  static bool on = getenv("PPU_DEBUG") != nullptr;
  return on;
}

// Reverse the 8 bits of a byte (bit 0<->7, 1<->6, ...). The BG/OBJ character
// data stores bit 7 as the left-most pixel (fullsnes), while the plane
// extraction below places the input's bit 0 first.
static std::uint8_t reverseBits8(std::uint8_t x) {
  x = std::uint8_t((x & 0xF0) >> 4 | (x & 0x0F) << 4);
  x = std::uint8_t((x & 0xCC) >> 2 | (x & 0x33) << 2);
  x = std::uint8_t((x & 0xAA) >> 1 | (x & 0x55) << 1);
  return x;
}

namespace snes {

// ---- Layer ----

// V = 0: latch per-frame state. Nothing per-layer yet.
auto Ppu::Layer::newFrame() -> void {}

// H = 0: start a new line.
auto Ppu::Layer::lineStart() -> void {
  mosaic.hcounter = ppu.mosaic_.size;
  mosaic.hoffset = 0;

  renderingIndex = 0;
  if (getenv("PPU_DEBUG") && ppu.scanline() == 1) printf("lineStart tb=%X\n", tileBase);
  pixelCounter = (hscroll & 7) << isHires();

  opt.hoffset = 0;
  opt.voffset = 0;
}

// H = 14: drop the scrolled-off partial tile column.
auto Ppu::Layer::prime() -> void {
  for (auto& data : tiles[0].data) data >>= pixelCounter << 1;
}

// One layer-map entry per 8-dot slot.
auto Ppu::Layer::loadMap() -> void {
  if (ppu.scanline() == 0) return;

  // ares hcounter() counts in cycles (4 per dot): hcounter>>5 = dot>>3.
  uint32 nameTableIndex = (ppu.dot() >> 3) << isHires();
  uint32 x = ppu.dot() & ~7;

  uint32 hpixel = x << isHires();
  uint32 vpixel = ppu.scanline();
  uint32 hscroll_ = hscroll;
  uint32 vscroll_ = vscroll;

  if (isHires()) {
    hscroll_ <<= 1;
    if (ppu.io_.interlace)
      vpixel = vpixel << 1 | (ppu.fieldBit() && !mosaic.enable);
  }
  if (mosaic.enable) {
    vpixel -= ppu.mosaic_.offset() << (isHires() && ppu.io_.interlace);
  }

  bool repeated = false;
repeat:

  uint32 hoffset = hpixel + hscroll_;
  uint32 voffset = vpixel + vscroll_;

  if (ppu.io_.mode == 2 || ppu.io_.mode == 4 || ppu.io_.mode == 6) {
    auto hlookup = ppu.layers_[2].opt.hoffset;
    auto vlookup = ppu.layers_[2].opt.voffset;
    uint32 valid = 13 + id;

    if (ppu.io_.mode == 4) {
      if (hlookup & (1u << valid)) {
        if (!(hlookup & 0x8000)) {
          hoffset = hpixel + (hlookup & ~7) + (hscroll_ & 7);
        } else {
          voffset = vpixel + vlookup;
        }
      }
    } else {
      if (hlookup & (1u << valid)) hoffset = hpixel + (hlookup & ~7) + (hscroll_ & 7);
      if (vlookup & (1u << valid)) voffset = vpixel + vlookup;
    }
  }

  uint32 width = 256 << isHires();
  uint32 hsize = width << tileSize << (screenSize & 1);
  uint32 vsize = width << tileSize << ((screenSize >> 1) & 1);

  hoffset &= hsize - 1;
  voffset &= vsize - 1;

  uint32 vtiles = 3 + tileSize;
  uint32 htiles = !isHires() ? vtiles : 4;

  uint32 htile = hoffset >> htiles;
  uint32 vtile = voffset >> vtiles;

  uint32 hscreen = (screenSize & 1) ? (32 << 5) : 0;
  uint32 vscreen = ((screenSize >> 1) & 1) ? (32 << (5 + (screenSize & 1))) : 0;

  uint16 offset = (htile & 0x1F) | (vtile & 0x1F) << 5;
  if (htile & 0x20) offset += hscreen;
  if (vtile & 0x20) offset += vscreen;

  uint16 address = screenAddress + offset;
  uint16 attributes = ppu.vram_[address & kVramMask];

  auto& tile = tiles[nameTableIndex];
  tile.character = attributes & 0x3FF;
  tile.paletteGroup = (attributes >> 10) & 7;
  tile.priority = priority[(attributes >> 13) & 1];
  tile.hmirror = (attributes >> 14) & 1;
  tile.vmirror = (attributes >> 15) & 1;

  if (htiles == 4 && bool(hoffset & 8) != tile.hmirror) tile.character += 1;
  if (vtiles == 4 && bool(voffset & 8) != tile.vmirror) tile.character += 16;

  // Tile stride in VRAM words follows this layer's bpp (kBpp2=8 words,
  // kBpp4=16, kBpp8=32), not the BG screen mode (ares uses io.mode here).
  uint32 characterMask = kVramMask >> (3 + mode);
  uint32 characterIndex = tileBase >> (3 + mode);
  uint16 origin = (tile.character + characterIndex) & characterMask;

  if (tile.vmirror) voffset ^= 7;
  tile.address = (origin << (3 + mode)) + (voffset & 7);

  // Palette groups: mode 0 packs 4 colors per group (shift 2); 8bpp mode 3
  // packs 256 (shift 8); the remaining 4bpp modes pack 16 (shift 4).
  // The shift follows this layer's bpp (kBpp2/kBpp4/kBpp8), not the BG mode.
  uint32 paletteOffset = ppu.io_.mode == 0 ? id << 5 : 0;
  uint32 paletteSize = 2 << mode;
  tile.palette = paletteOffset + (tile.paletteGroup << paletteSize);

  if (ppuDebug())
    printf("map L=%u lid=%u dot=%u nti=%u addr=%04X attr=%04X char=%u tb=%X pal=%u prio=%u\n", ppu.scanline(), id, ppu.dot(), nameTableIndex, address, attributes, tile.character, tileBase, tile.palette, tile.priority);

  nameTableIndex++;
  if (isHires() && !repeated) {
    repeated = true;
    hpixel += 8;
    goto repeat;
  }
}

// BG3 offset-per-tile entries.
auto Ppu::Layer::loadOffsets(uint32 y) -> void {
  if (ppu.scanline() == 0) return;

  uint32 x = (ppu.dot() >> 3) << 3;

  uint32 hoffset = x + (hscroll & ~7);
  uint32 voffset = y + vscroll;

  uint32 vtiles = 3 + tileSize;
  uint32 htiles = !isHires() ? vtiles : 3;

  uint32 htile = hoffset >> htiles;
  uint32 vtile = voffset >> vtiles;

  uint32 hscreen = (screenSize & 1) ? (32 << 5) : 0;
  uint32 vscreen = ((screenSize >> 1) & 1) ? (32 << (5 + (screenSize & 1))) : 0;

  uint16 offset = (htile & 0x1F) | (vtile & 0x1F) << 5;
  if (htile & 0x20) offset += hscreen;
  if (vtile & 0x20) offset += vscreen;

  uint16 address = screenAddress + offset;
  if (y == 0) opt.hoffset = ppu.vram_[address & kVramMask];
  if (y == 8) opt.voffset = ppu.vram_[address & kVramMask];
}

// One 16-bit character word per slot (2 planes).
auto Ppu::Layer::loadPlanes(uint32 index, bool half) -> void {
  if (ppu.scanline() == 0) return;

  uint32 characterIndex = (ppu.dot() >> 3 << isHires()) + half;

  auto& tile = tiles[characterIndex];
  uint16 data = ppu.vram_[(tile.address + (index << 3)) & kVramMask];

  if (ppuDebug())
    printf("char L=%u lid=%u dot=%u charIdx=%u addr=%04X word=%04X\n", ppu.scanline(), id, ppu.dot(), characterIndex, tile.address + (index << 3), data);

  // The character data stores bit 7 as the left-most pixel. The plane
  // extraction below places the input byte's bit 0 first, so reverse each
  // byte. A horizontally-mirrored tile is drawn in the opposite order, so
  // skip the reversal for those (bit 0 becomes the left-most pixel).
  uint8 lo = uint8(data & 0xFF);
  uint8 hi = uint8(data >> 8);
  if (!tile.hmirror) {
    lo = reverseBits8(lo);
    hi = reverseBits8(hi);
  }

  tile.data[index] = (
      ((((lo * 0x0101010101010101ull & 0x8040201008040201ull) * 0x0102040810204081ull) >> 49) & 0x5555)
    | ((((hi * 0x0101010101010101ull & 0x8040201008040201ull) * 0x0102040810204081ull) >> 48) & 0xAAAA)
  );
}

// One pixel of the layer, below/above pass.
auto Ppu::Layer::draw(bool belowPass) -> void {
  if (ppu.scanline() == 0) return;

  if (belowPass) {
    above.priority = 0;
    below.priority = 0;
    if (!isHires()) return;
  }

  if (mode == kMode7) return mode7Draw();

  auto& tile = tiles[renderingIndex];
  uint8 color = 0;
  if (mode >= kBpp2) { color = (color & ~3) | (tile.data[0] & 3); tile.data[0] >>= 2; }
  if (mode >= kBpp4) { color = (color & ~0x0C) | ((tile.data[1] & 3) << 2); tile.data[1] >>= 2; }
  if (mode >= kBpp8) { color = (color & ~0x30) | ((tile.data[2] & 3) << 4); tile.data[2] >>= 2; }
  if (mode >= kBpp8) { color = (color & ~0xC0) | ((tile.data[3] & 3) << 6); tile.data[3] >>= 2; }

  Pixel pixel;
  pixel.priority = tile.priority;
  pixel.palette = color ? uint8(tile.palette + color) : 0;
  pixel.paletteGroup = tile.paletteGroup;
  if (ppuDebug())
    printf("draw dot=%u pC=%u rI=%u pass=%u ae=%u be=%u pal=%u col=%u\n", ppu.dot(), pixelCounter, renderingIndex, belowPass, aboveEnable, belowEnable, tile.palette, color);

  // One tile fetch covers 8 pixels = 8 draw calls; in hires each draw call
  // consumes one half-pixel, so the tile index still advances every 8 calls.
  if (++pixelCounter == 8) { pixelCounter = 0; renderingIndex++; }

  uint32 x = ppu.dot() - 14;
  if (x == 0 && (!isHires() || !belowPass)) {
    mosaic.hcounter = ppu.mosaic_.size;
    mosaic.pixel = pixel;
  } else if ((!isHires() || !belowPass) && --mosaic.hcounter == 0) {
    mosaic.hcounter = ppu.mosaic_.size;
    mosaic.pixel = pixel;
  } else if (mosaic.enable) {
    pixel = mosaic.pixel;
  }

  if (pixel.palette == 0) return;

  if (!isHires() || belowPass) if (aboveEnable) above = pixel;
  if (!isHires() || !belowPass) if (belowEnable) below = pixel;
}

// Mode 7 rotation/scaling path.
auto Ppu::Layer::mode7Draw() -> void {
  int a = int16(ppu.io_.m7a);
  int b = int16(ppu.io_.m7b);
  int c = int16(ppu.io_.m7c);
  int d = int16(ppu.io_.m7d);

  int hcenter = ppu.io_.m7x & 0x1FFF;
  if (hcenter & 0x1000) hcenter |= ~0x1FFF;
  int vcenter = ppu.io_.m7y & 0x1FFF;
  if (vcenter & 0x1000) vcenter |= ~0x1FFF;
  int hoffset = ppu.io_.hoffsetMode7 & 0x1FFF;
  if (hoffset & 0x1000) hoffset |= ~0x1FFF;
  int voffset = ppu.io_.voffsetMode7 & 0x1FFF;
  if (voffset & 0x1000) voffset |= ~0x1FFF;

  uint32 x = mosaic.hoffset;
  uint32 y = ppu.scanline();
  if (ppu.layers_[0].mosaic.enable) y -= ppu.mosaic_.offset();  // BG2 vertical mosaic uses BG1

  if (!mosaic.enable) {
    mosaic.hoffset += 1;
  } else if (--mosaic.hcounter == 0) {
    mosaic.hcounter = ppu.mosaic_.size;
    mosaic.hoffset += ppu.mosaic_.size;
  }

  if (ppu.io_.hflipMode7) x = 255 - x;
  if (ppu.io_.vflipMode7) y = 255 - y;

  auto clip = [](int n) -> int { return n & 0x2000 ? (n | ~1023) : (n & 1023); };
  int originX = (a * clip(hoffset - hcenter) & ~63) + (b * clip(voffset - vcenter) & ~63) + (b * y & ~63) + (hcenter << 8);
  int originY = (c * clip(hoffset - hcenter) & ~63) + (d * clip(voffset - vcenter) & ~63) + (d * y & ~63) + (vcenter << 8);

  int pixelX = (originX + a * x) >> 8;
  int pixelY = (originY + c * x) >> 8;
  uint16 paletteAddress = ((pixelY & 7) << 3) | (pixelX & 7);

  uint16 tileX = (pixelX >> 3) & 0x7F;
  uint16 tileY = (pixelY >> 3) & 0x7F;
  uint16 tileAddress = (tileY << 7) | tileX;

  bool outOfBounds = (pixelX | pixelY) & ~1023;

  uint8 tile = ppu.io_.repeatMode7 == 3 && outOfBounds ? 0 : ppu.vram_[tileAddress & kVramMask] & 0xFF;
  uint8 palette = ppu.io_.repeatMode7 == 2 && outOfBounds ? 0 : (ppu.vram_[(tile << 6 | paletteAddress) & kVramMask] >> 8) & 0xFF;

  uint32 prio;
  if (id == 0) {
    prio = priority[0];
  } else if (id == 1) {
    prio = priority[(palette >> 7) & 1];
    palette &= 0x7F;
  } else {
    return;
  }

  if (palette == 0) return;

  if (aboveEnable) {
    above.priority = prio;
    above.palette = palette;
    above.paletteGroup = 0;
  }

  if (belowEnable) {
    below.priority = prio;
    below.palette = palette;
    below.paletteGroup = 0;
  }
}

auto Ppu::Layer::resetState() -> void {
  screenSize = 0;
  screenAddress = 0;
  tileBase = 0;
  tileSize = 0;
  mode = kInactive;
  priority[0] = 0;
  priority[1] = 0;
  aboveEnable = false;
  belowEnable = false;
  hscroll = 0;
  vscroll = 0;
  above = {};
  below = {};
  mosaic = {};
  opt = {};
  for (auto& tile : tiles) tile = {};
  renderingIndex = 0;
  if (getenv("PPU_DEBUG") && ppu.scanline() == 1) printf("lineStart tb=%X\n", tileBase);
  pixelCounter = 0;
}

// ---- SpriteEngine ----

// reloadAddress()/refreshFirst() live in io.cpp ($2102/$2103 wiring).

// V = 0: clear overflow flags.
auto Ppu::SpriteEngine::newFrame() -> void {
  timeOver = false;
  rangeOver = false;
}

// H = 0: swap buffers, clear lists.
auto Ppu::SpriteEngine::lineStart() -> void {
  latchedFirst = firstSprite;

  t.x = 0;
  t.y = ppu.scanline();
  t.itemCount = 0;
  t.tileCount = 0;

  t.active = !t.active;
  auto oamItem = t.item[t.active];
  auto oamTile = t.tile[t.active];

  for (uint32 n = 0; n < 32; n++) oamItem[n].valid = false;
  for (uint32 n = 0; n < 34; n++) oamTile[n].valid = false;

  if (t.y == ppu.vdisp() && !ppu.io_.displayDisable) reloadAddress();
  if (t.y >= ppu.vdisp() - 1 || ppu.io_.displayDisable) return;
}

auto Ppu::SpriteEngine::touchesLine(const SpriteObj& sprite) const -> bool {
  if (sprite.x > 256 && sprite.x + ppu.objectWidth(sprite) - 1 < 512) return false;
  uint32 height = ppu.objectHeight(sprite) >> interlace;
  uint32 y = sprite.y;
  if (y > 255) return false;
  return t.y >= y && t.y < y + height;
}

// Range check one sprite (every 2 dots).
auto Ppu::SpriteEngine::probe(uint32 index) -> void {
  if (ppu.io_.displayDisable) return;
  if (t.itemCount > 32) return;

  auto oamItem = t.item[t.active];

  uint8 sprite = (latchedFirst + index) & 0x7F;
  if (!touchesLine(ppu.oamObj_[sprite])) return;
  ppu.latch_.oamAddr = sprite;
  if (ppuDebug())
    printf("probe L=%u dot=%u idx=%u sprite=%u x=%u y=%u hf=%u vf=%u prio=%u pal=%u\n",
           ppu.scanline(), ppu.dot(), index, sprite, ppu.oamObj_[sprite].x, ppu.oamObj_[sprite].y,
           ppu.oamObj_[sprite].hflip, ppu.oamObj_[sprite].vflip, ppu.oamObj_[sprite].priority, ppu.oamObj_[sprite].palette);

  if (t.itemCount++ < 32) {
    oamItem[t.itemCount - 1] = {true, sprite};
  }
}

// One pixel from the finished tile list.
auto Ppu::SpriteEngine::draw() -> void {
  above.priority = 0;
  below.priority = 0;

  auto oamTile = t.tile[!t.active];
  uint32 x = t.x++;

  for (uint32 n = 0; n < 34; n++) {
    const auto& tile = oamTile[n];
    if (!tile.valid) break;

    int px = int(x) - int(tile.x);
    if (px & ~7) continue;

    uint32 color = 0, shift = tile.hflip ? px : 7 - px;
    color += (tile.data >> (shift + 0)) & 1;
    color += (tile.data >> (shift + 7)) & 2;
    color += (tile.data >> (shift + 14)) & 4;
    color += (tile.data >> (shift + 21)) & 8;

    if (ppuDebug())
      printf("sdraw L=%u x=%u tile.x=%u px=%u hf=%u data=%08X color=%u\n",
             ppu.scanline(), x, tile.x, px, tile.hflip, tile.data, color);

    if (color) {
      if (aboveEnable) {
        above.palette = tile.palette + color;
        above.priority = priority[tile.priority];
      }
      if (belowEnable) {
        below.palette = tile.palette + color;
        below.priority = priority[tile.priority];
      }
    }
  }
}

// H = 270: fetch tile data for the line.
auto Ppu::SpriteEngine::loadTiles() -> void {
  auto oamItem = t.item[t.active];
  auto oamTile = t.tile[t.active];

  for (uint32 i = 32; i-- > 0;) {
    if (!oamItem[i].valid) continue;

    if (ppu.io_.displayDisable || ppu.scanline() >= ppu.vdisp() - 1) continue;

    ppu.latch_.oamAddr = oamItem[i].index;
    const auto& sprite = ppu.oamObj_[ppu.latch_.oamAddr];

    uint32 tileWidth = ppu.objectWidth(sprite) >> 3;
    int x = sprite.x;
    int y = (t.y - sprite.y) & 255;
    if (interlace) y <<= 1;

    if (sprite.vflip) {
      if (ppu.objectWidth(sprite) == ppu.objectHeight(sprite)) {
        y = ppu.objectHeight(sprite) - 1 - y;
      } else if (y < int(ppu.objectWidth(sprite))) {
        y = ppu.objectWidth(sprite) - 1 - y;
      } else {
        y = ppu.objectWidth(sprite) + (ppu.objectWidth(sprite) - 1) - (y - ppu.objectWidth(sprite));
      }
    }

    if (interlace) {
      y = !sprite.vflip ? y + ppu.fieldBit() : y - ppu.fieldBit();
    }

    x &= 511;
    y &= 255;

    uint16 tiledataAddress = tileBase;
    if (sprite.nameselect) tiledataAddress += (1 + nameselect) << 12;
    uint16 chrx = sprite.character & 0x0F;
    uint16 chry = (((sprite.character >> 4) & 0x0F) + (y >> 3) & 15) << 4;

    for (uint32 tx = 0; tx < tileWidth; tx++) {
      uint32 sx = (x + (tx << 3)) & 511;
      if (x != 256 && sx >= 256 && sx + 7 < 512) continue;
      if (t.tileCount++ >= 34) break;

      uint32 n = t.tileCount - 1;
      oamTile[n].valid = true;
      oamTile[n].x = sx;
      oamTile[n].priority = sprite.priority;
      oamTile[n].palette = 128 + (sprite.palette << 4);
      oamTile[n].hflip = sprite.hflip;

      uint32 mx = !sprite.hflip ? tx : tileWidth - 1 - tx;
      uint32 pos = tiledataAddress + ((chry + ((chrx + mx) & 15)) << 4);
      uint16 address = (pos & 0xFFF0) + (y & 7);

      oamTile[n].data = ppu.vram_[(address + 0) & kVramMask] | (uint32(ppu.vram_[(address + 8) & kVramMask]) << 16);
      if (ppuDebug())
        printf("tile i=%u n=%u sp=%u valid=%u x=%u sx=%u addr=%04X hf=%u prio=%u data=%08X\n",
               i, n, oamItem[i].index, oamTile[n].valid, x, sx, address, oamTile[n].hflip, oamTile[n].priority, oamTile[n].data);
    }
  }

  if (ppuDebug())
    printf("items L=%u count=%u tiles=%u range=%u time=%u\n",
           ppu.scanline(), t.itemCount, t.tileCount, t.itemCount > 32, t.tileCount > 34);

  rangeOver |= t.itemCount > 32;
  timeOver |= t.tileCount > 34;
}

auto Ppu::SpriteEngine::resetState() -> void {
  aboveEnable = false;
  belowEnable = false;
  interlace = false;
  tileBase = 0;
  nameselect = 0;
  baseSize = 0;
  priority[0] = 0;
  priority[1] = 0;
  priority[2] = 0;
  priority[3] = 0;
  rangeOver = false;
  timeOver = false;
  firstSprite = 0;
  latchedFirst = 0;
  t = {};
  above = {};
  below = {};
}

// ---- WindowMask ----

// H = 0.
auto Ppu::WindowMask::lineStart() -> void {
  x = 0;
}

// Per dot: mask layers + color enable.
auto Ppu::WindowMask::stepPixel() -> void {
  bool one = x >= oneLeft && x <= oneRight;
  bool two = x >= twoLeft && x <= twoRight;
  x++;

  if (maskHit(bg1.oneEnable, one ^ bg1.oneInvert, bg1.twoEnable, two ^ bg1.twoInvert, bg1.mask)) {
    if (bg1.aboveEnable) ppu.layers_[0].above.priority = 0;
    if (bg1.belowEnable) ppu.layers_[0].below.priority = 0;
  }

  if (maskHit(bg2.oneEnable, one ^ bg2.oneInvert, bg2.twoEnable, two ^ bg2.twoInvert, bg2.mask)) {
    if (bg2.aboveEnable) ppu.layers_[1].above.priority = 0;
    if (bg2.belowEnable) ppu.layers_[1].below.priority = 0;
  }

  if (maskHit(bg3.oneEnable, one ^ bg3.oneInvert, bg3.twoEnable, two ^ bg3.twoInvert, bg3.mask)) {
    if (bg3.aboveEnable) ppu.layers_[2].above.priority = 0;
    if (bg3.belowEnable) ppu.layers_[2].below.priority = 0;
  }

  if (maskHit(bg4.oneEnable, one ^ bg4.oneInvert, bg4.twoEnable, two ^ bg4.twoInvert, bg4.mask)) {
    if (bg4.aboveEnable) ppu.layers_[3].above.priority = 0;
    if (bg4.belowEnable) ppu.layers_[3].below.priority = 0;
  }

  if (maskHit(obj.oneEnable, one ^ obj.oneInvert, obj.twoEnable, two ^ obj.twoInvert, obj.mask)) {
    if (obj.aboveEnable) ppu.sprites_.above.priority = 0;
    if (obj.belowEnable) ppu.sprites_.below.priority = 0;
  }

  bool value = maskHit(col.oneEnable, one ^ col.oneInvert, col.twoEnable, two ^ col.twoInvert, col.mask);
  bool array[] = {true, value, !value, false};
  above.colorEnable = array[col.aboveMask];
  below.colorEnable = array[col.belowMask];
}

auto Ppu::WindowMask::maskHit(bool oneEnable, bool one, bool twoEnable, bool two, uint32 mask) const -> bool {
  if (!oneEnable) return two && twoEnable;
  if (!twoEnable) return one;
  if (mask == 0) return one | two;
  if (mask == 1) return one & two;
  return (one ^ two) == 3 - mask;
}

auto Ppu::WindowMask::resetState() -> void {
  bg1 = {};
  bg2 = {};
  bg3 = {};
  bg4 = {};
  obj = {};
  col = {};
  oneLeft = 0;
  oneRight = 0;
  twoLeft = 0;
  twoRight = 0;
  x = 0;
  above = {};
  below = {};
}

// ---- Composer ----

// H = 0: point the row cursor.
auto Ppu::Composer::lineStart() -> void {
  line = nullptr;

  // Visible picture = scanlines 1..224 (non-overscan) / 1..240 (overscan),
  // matching snes9x (FIRST_VISIBLE_LINE = 1, screen row = scanline - 1).
  // Non-overscan keeps the 8-row top border of the video signal.
  uint32 vcounter = ppu.scanline();
  if (!ppu.state_.overscan) {
    vcounter += 7;
  } else if (vcounter > 0) {
    vcounter -= 1;
  } else {
    vcounter = 241;  // scanline 0 has no screen row
  }
  if (vcounter < 240) {
    line = ppu.frameBuffer_ + uint64(vcounter) * kFrameWidth + 26;
  }

  above.color = lookupColor(0);
  below.color = above.color;

  above.colorEnable = false;
  below.colorEnable = false;

  transparent = true;
  mathBlendMode = false;
  mathColorHalve = colorHalve && !blendMode && above.colorEnable;
}

// Per dot: pick + blend + write 2 halves.
auto Ppu::Composer::emitPixel() -> void {
  if (ppu.scanline() == 0) return;

  bool hires = ppu.io_.pseudoHires || ppu.io_.mode == 5 || ppu.io_.mode == 6;
  auto belowColor = pickSub(hires);
  auto aboveColor = pickMain();

  if (!line) return;
  if (getenv("PPU_DEBUG") && ppu.scanline() <= 3 && ppu.dot() < 40)
    printf("emit L=%u dot=%u above=%04X below=%04X\n", ppu.scanline(), ppu.dot(), aboveColor, belowColor);
  *line++ = ppu.io_.displayBrightness << 15 | (hires ? belowColor : aboveColor);
  if (hires) *line++ = ppu.io_.displayBrightness << 15 | aboveColor;
}

auto Ppu::Composer::pickSub(bool hires) -> uint16 {
  if (ppu.io_.displayDisable || (!ppu.state_.overscan && ppu.scanline() >= 225)) return 0;

  uint32 priority = 0;
  if (ppu.layers_[0].below.priority) {
    priority = ppu.layers_[0].below.priority;
    if (directColor && (ppu.io_.mode == 3 || ppu.io_.mode == 4 || ppu.io_.mode == 7)) {
      below.color = lookupDirectColor(ppu.layers_[0].below.palette, ppu.layers_[0].below.paletteGroup);
    } else {
      below.color = lookupColor(ppu.layers_[0].below.palette);
    }
  }
  if (ppu.layers_[1].below.priority > priority) {
    priority = ppu.layers_[1].below.priority;
    below.color = lookupColor(ppu.layers_[1].below.palette);
  }
  if (ppu.layers_[2].below.priority > priority) {
    priority = ppu.layers_[2].below.priority;
    below.color = lookupColor(ppu.layers_[2].below.palette);
  }
  if (ppu.layers_[3].below.priority > priority) {
    priority = ppu.layers_[3].below.priority;
    below.color = lookupColor(ppu.layers_[3].below.palette);
  }
  if (ppu.sprites_.below.priority > priority) {
    priority = ppu.sprites_.below.priority;
    below.color = lookupColor(ppu.sprites_.below.palette);
  }
  if ((transparent = priority == 0)) below.color = lookupColor(0);

  if (!hires) return 0;
  // Hires even half-pixels = sub screen. Matches ares dac.cpp below(): gate
  // below.color with above.colorEnable, and use the per-dot mathBlendMode.
  if (!below.colorEnable) return above.colorEnable ? below.color : uint16(0);
  return mixColors(above.colorEnable ? below.color : 0, mathBlendMode ? above.color : coldataColor());
}

auto Ppu::Composer::pickMain() -> uint16 {
  if (ppu.io_.displayDisable || (!ppu.state_.overscan && ppu.scanline() >= 225)) return 0;

  uint32 priority = 0;
  if (ppu.layers_[0].above.priority) {
    priority = ppu.layers_[0].above.priority;
    if (directColor && (ppu.io_.mode == 3 || ppu.io_.mode == 4 || ppu.io_.mode == 7)) {
      above.color = lookupDirectColor(ppu.layers_[0].above.palette, ppu.layers_[0].above.paletteGroup);
    } else {
      above.color = lookupColor(ppu.layers_[0].above.palette);
    }
    below.colorEnable = bg1ColorEnable;
  }
  if (ppu.layers_[1].above.priority > priority) {
    priority = ppu.layers_[1].above.priority;
    above.color = lookupColor(ppu.layers_[1].above.palette);
    below.colorEnable = bg2ColorEnable;
  }
  if (ppu.layers_[2].above.priority > priority) {
    priority = ppu.layers_[2].above.priority;
    above.color = lookupColor(ppu.layers_[2].above.palette);
    below.colorEnable = bg3ColorEnable;
  }
  if (ppu.layers_[3].above.priority > priority) {
    priority = ppu.layers_[3].above.priority;
    above.color = lookupColor(ppu.layers_[3].above.palette);
    below.colorEnable = bg4ColorEnable;
  }
  if (ppu.sprites_.above.priority > priority) {
    priority = ppu.sprites_.above.priority;
    above.color = lookupColor(ppu.sprites_.above.palette);
    below.colorEnable = objColorEnable && ppu.sprites_.above.palette >= 192;
  }
  if (priority == 0) {
    above.color = lookupColor(0);
    below.colorEnable = backColorEnable;
  }

  if (!ppu.window_.below.colorEnable) below.colorEnable = false;
  above.colorEnable = ppu.window_.above.colorEnable;
  if (getenv("PPU_DEBUG") && ppu.scanline() <= 3 && ppu.dot() >= 14 && ppu.dot() <= 39)
    printf("pick L=%u dot=14 prio=%u,%u,%u,%u ae=%u,%u,%u,%u\n", ppu.scanline(), ppu.layers_[0].above.priority, ppu.layers_[1].above.priority, ppu.layers_[2].above.priority, ppu.layers_[3].above.priority, ppu.layers_[0].aboveEnable, ppu.layers_[1].aboveEnable, ppu.layers_[2].aboveEnable, ppu.layers_[3].aboveEnable);
  if (!below.colorEnable) return above.colorEnable ? above.color : uint16(0);

  if (blendMode && transparent) {
    mathBlendMode = false;
    mathColorHalve = false;
  } else {
    mathBlendMode = blendMode;
    mathColorHalve = colorHalve && above.colorEnable;
  }

  return mixColors(above.colorEnable ? above.color : 0, mathBlendMode ? below.color : coldataColor());
}

auto Ppu::Composer::mixColors(uint32 x, uint32 y) const -> uint16 {
  if (!colorMode) {  // add
    if (!mathColorHalve) {
      uint32 sum = x + y;
      uint32 carry = (sum - ((x ^ y) & 0x0421)) & 0x8420;
      return (sum - carry) | (carry - (carry >> 5));
    } else {
      return (x + y - ((x ^ y) & 0x0421)) >> 1;
    }
  } else {  // sub
    uint32 diff = x - y + 0x8420;
    uint32 borrow = (diff - ((x ^ y) & 0x8420)) & 0x8420;
    if (!mathColorHalve) {
      return (diff - borrow) & (borrow - (borrow >> 5));
    } else {
      return ((diff - borrow) & (borrow - (borrow >> 5)) & 0x7BDE) >> 1;
    }
  }
}

auto Ppu::Composer::lookupColor(uint8 index) const -> uint16 {
  ppu.latch_.cgramAddr = index;
  return ppu.cgram_[index & 0xFF];
}

auto Ppu::Composer::lookupDirectColor(uint8 palette, uint8 group) const -> uint16 {
  // palette = -------- BBGGGRRR
  // group   = -------- -----bgr
  // output  = 0BBb00GG Gg0RRRr0
  return (palette << 7 & 0x6000) + (group << 10 & 0x1000)
       + (palette << 4 & 0x0380) + (group << 5 & 0x0040)
       + (palette << 2 & 0x001C) + (group << 1 & 0x0002);
}

auto Ppu::Composer::coldataColor() const -> uint16 {
  return colorRed | colorGreen << 5 | colorBlue << 10;
}

auto Ppu::Composer::resetState() -> void {
  directColor = false;
  blendMode = false;
  colorMode = false;
  colorHalve = false;
  bg1ColorEnable = false;
  bg2ColorEnable = false;
  bg3ColorEnable = false;
  bg4ColorEnable = false;
  objColorEnable = false;
  backColorEnable = false;
  colorRed = 0;
  colorGreen = 0;
  colorBlue = 0;
  above = {};
  below = {};
  transparent = false;
  mathBlendMode = false;
  mathColorHalve = false;
  line = nullptr;
}

// ---- Mosaic ----

// active() lives in io.cpp.

// H = 0: reload the block counter.
auto Ppu::Mosaic::lineStart() -> void {
  if (ppu.scanline() == 1) {
    vcounter = active() ? size + 1 : 0;
  }
  if (vcounter && !--vcounter) {
    vcounter = active() ? size : 0;
  }
}

auto Ppu::Mosaic::resetState() -> void {
  size = 0;
  vcounter = 0;
}

// ---- Ppu per-dot dispatch ----

// Per-dot render work (called from advanceDot). Order mirrors ares: all
// below passes first (left half-pixel), then all above passes, sprites,
// the window step, and finally the compositor emit.
auto Ppu::paintDot() -> void {
  layers_[0].draw(true);
  layers_[1].draw(true);
  layers_[2].draw(true);
  layers_[3].draw(true);
  layers_[0].draw(false);
  layers_[1].draw(false);
  layers_[2].draw(false);
  layers_[3].draw(false);
  sprites_.draw();
  window_.stepPixel();
  composer_.emitPixel();
}

// Per-dot fetch dispatch by BG mode.
auto Ppu::fetchSlot(uint32 slot) -> void {
  switch (io_.mode) {
    case 0:
      if (slot == 0) layers_[3].loadMap();
      if (slot == 1) layers_[2].loadMap();
      if (slot == 2) layers_[1].loadMap();
      if (slot == 3) layers_[0].loadMap();
      if (slot == 4) layers_[3].loadPlanes(0);
      if (slot == 5) layers_[2].loadPlanes(0);
      if (slot == 6) layers_[1].loadPlanes(0);
      if (slot == 7) layers_[0].loadPlanes(0);
      break;
    case 1:
      if (slot == 0) layers_[2].loadMap();
      if (slot == 1) layers_[1].loadMap();
      if (slot == 2) layers_[0].loadMap();
      if (slot == 3) layers_[2].loadPlanes(0);
      if (slot == 4) layers_[1].loadPlanes(0);
      if (slot == 5) layers_[1].loadPlanes(1);
      if (slot == 6) layers_[0].loadPlanes(0);
      if (slot == 7) layers_[0].loadPlanes(1);
      break;
    case 2:
      if (slot == 0) layers_[1].loadMap();
      if (slot == 1) layers_[0].loadMap();
      if (slot == 2) layers_[2].loadOffsets(0);
      if (slot == 3) layers_[2].loadOffsets(8);
      if (slot == 4) layers_[1].loadPlanes(0);
      if (slot == 5) layers_[1].loadPlanes(1);
      if (slot == 6) layers_[0].loadPlanes(0);
      if (slot == 7) layers_[0].loadPlanes(1);
      break;
    case 3:
      if (slot == 0) layers_[1].loadMap();
      if (slot == 1) layers_[0].loadMap();
      if (slot == 2) layers_[1].loadPlanes(0);
      if (slot == 3) layers_[1].loadPlanes(1);
      if (slot == 4) layers_[0].loadPlanes(0);
      if (slot == 5) layers_[0].loadPlanes(1);
      if (slot == 6) layers_[0].loadPlanes(2);
      if (slot == 7) layers_[0].loadPlanes(3);
      break;
    case 4:
      if (slot == 0) layers_[1].loadMap();
      if (slot == 1) layers_[0].loadMap();
      if (slot == 2) layers_[2].loadOffsets(0);
      if (slot == 3) layers_[1].loadPlanes(0);
      if (slot == 4) layers_[0].loadPlanes(0);
      if (slot == 5) layers_[0].loadPlanes(1);
      if (slot == 6) layers_[0].loadPlanes(2);
      if (slot == 7) layers_[0].loadPlanes(3);
      break;
    case 5:
      if (slot == 0) layers_[1].loadMap();
      if (slot == 1) layers_[0].loadMap();
      if (slot == 2) layers_[1].loadPlanes(0, 0);
      if (slot == 3) layers_[1].loadPlanes(0, 1);
      if (slot == 4) layers_[0].loadPlanes(0, 0);
      if (slot == 5) layers_[0].loadPlanes(1, 0);
      if (slot == 6) layers_[0].loadPlanes(0, 1);
      if (slot == 7) layers_[0].loadPlanes(1, 1);
      break;
    case 6:
      if (slot == 0) layers_[1].loadMap();
      if (slot == 1) layers_[0].loadMap();
      if (slot == 2) layers_[2].loadOffsets(0);
      if (slot == 3) layers_[2].loadOffsets(8);
      if (slot == 4) layers_[0].loadPlanes(0, 0);
      if (slot == 5) layers_[0].loadPlanes(1, 0);
      if (slot == 6) layers_[0].loadPlanes(0, 1);
      if (slot == 7) layers_[0].loadPlanes(1, 1);
      break;
    case 7:
      break;
  }
}

}  // namespace snes