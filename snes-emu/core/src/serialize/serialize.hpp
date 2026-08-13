#pragma once

// serialize.hpp — minimal binary serializer used for save states (phase 7).
//
// Every core component exposes serialize(Writer&)/deserialize(Reader&). The
// Writer accumulates bytes little-endian; the Reader bounds-checks and, on an
// out-of-range read, returns 0 and latches `!ok()`, so a truncated/corrupt
// state stream fails cleanly instead of crashing the emulator.

#include <cstdint>
#include <cstring>
#include <vector>

namespace snes {

class Writer {
 public:
  auto u8(std::uint8_t v) -> void { buf_.push_back(v); }
  auto u16(std::uint16_t v) -> void { u8(std::uint8_t(v & 0xff)); u8(std::uint8_t(v >> 8)); }
  auto u32(std::uint32_t v) -> void { u16(std::uint16_t(v & 0xffff)); u16(std::uint16_t(v >> 16)); }
  auto u64(std::uint64_t v) -> void { u32(std::uint32_t(v)); u32(std::uint32_t(v >> 32)); }
  auto b(bool v) -> void { u8(v ? 1 : 0); }
  auto raw(const void* p, std::size_t n) -> void {
    const auto* q = static_cast<const std::uint8_t*>(p);
    buf_.insert(buf_.end(), q, q + n);
  }
  auto data() const -> const std::vector<std::uint8_t>& { return buf_; }

 private:
  std::vector<std::uint8_t> buf_;
};

class Reader {
 public:
  explicit Reader(const std::vector<std::uint8_t>& data) : buf_(data) {}

  auto u8() -> std::uint8_t {
    if (pos_ >= buf_.size()) { ok_ = false; return 0; }
    return buf_[pos_++];
  }
  auto u16() -> std::uint16_t { return std::uint16_t(u8()) | (std::uint16_t(u8()) << 8); }
  auto u32() -> std::uint32_t { return std::uint32_t(u16()) | (std::uint32_t(u16()) << 16); }
  auto u64() -> std::uint64_t { return std::uint64_t(u32()) | (std::uint64_t(u32()) << 32); }
  auto b() -> bool { return u8() != 0; }
  auto raw(void* p, std::size_t n) -> void {
    if (pos_ + n > buf_.size()) { ok_ = false; return; }
    std::memcpy(p, buf_.data() + pos_, n);
    pos_ += n;
  }
  auto ok() const -> bool { return ok_; }

 private:
  const std::vector<std::uint8_t>& buf_;
  std::size_t pos_ = 0;
  bool ok_ = true;
};

}  // namespace snes
