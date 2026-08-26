#include "coprocessor/srtc.hpp"

#include <ctime>
#include <limits>

namespace snes {

const unsigned Srtc::kMonths[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

auto Srtc::handles(uint24 address) const -> bool {
  uint32 bank = address >> 16;
  if (bank > 0x3F && bank < 0x80) return false;
  if (bank >= 0x80) bank -= 0x80;
  if (bank > 0x3F) return false;
  uint32 offs = address & 0xFFFF;
  return offs == 0x2800 || offs == 0x2801;
}

void Srtc::power() { reset(); }

void Srtc::reset() {
  mode_ = Read;
  index_ = -1;
  updateTime();
}

void Srtc::updateTime() {
  uint32 rtcTime = uint32(reg_[16]) | (uint32(reg_[17]) << 8) | (uint32(reg_[18]) << 16) |
                   (uint32(reg_[19]) << 24);
  std::time_t current = std::time(nullptr);
  // ponytail: 64-bit host → simple delta; truncated to uint32 for the timestamp regs.
  std::time_t diff = 0;
  if (current >= (std::time_t)rtcTime) diff = current - (std::time_t)rtcTime;
  // Negative diff (restored future timestamp) → freeze clock until host catches up.

  if (diff > 0) {
    unsigned second = reg_[0] + reg_[1] * 10;
    unsigned minute = reg_[2] + reg_[3] * 10;
    unsigned hour = reg_[4] + reg_[5] * 10;
    unsigned day = reg_[6] + reg_[7] * 10;
    unsigned month = reg_[8];
    unsigned year = reg_[9] + reg_[10] * 10 + reg_[11] * 100;
    unsigned wday = reg_[12];
    day--;
    month--;
    year += 1000;
    second += (unsigned)diff;
    while (second >= 60) {
      second -= 60;
      minute++;
      if (minute < 60) continue;
      minute = 0;
      hour++;
      if (hour < 24) continue;
      hour = 0;
      day++;
      wday = (wday + 1) % 7;
      unsigned days = kMonths[month % 12];
      if (days == 28) {
        bool leap = false;
        if ((year % 4) == 0) {
          leap = true;
          if ((year % 100) == 0 && (year % 400) != 0) leap = false;
        }
        if (leap) days++;
      }
      if (day < days) continue;
      day = 0;
      month++;
      if (month < 12) continue;
      month = 0;
      year++;
    }
    day++;
    month++;
    year -= 1000;
    reg_[0] = second % 10;
    reg_[1] = second / 10;
    reg_[2] = minute % 10;
    reg_[3] = minute / 10;
    reg_[4] = hour % 10;
    reg_[5] = hour / 10;
    reg_[6] = day % 10;
    reg_[7] = day / 10;
    reg_[8] = month;
    reg_[9] = year % 10;
    reg_[10] = (year / 10) % 10;
    reg_[11] = year / 100;
    reg_[12] = wday % 7;
  }
  uint32 cur = (uint32)current;
  reg_[16] = cur & 0xFF;
  reg_[17] = (cur >> 8) & 0xFF;
  reg_[18] = (cur >> 16) & 0xFF;
  reg_[19] = (cur >> 24) & 0xFF;
}

auto Srtc::weekday(unsigned year, unsigned month, unsigned day) -> unsigned {
  unsigned y = 1900, m = 1, sum = 0;
  year = year < 1900 ? 1900 : year;
  month = month < 1 ? 1 : (month > 12 ? 12 : month);
  day = day < 1 ? 1 : (day > 31 ? 31 : day);
  while (y < year) {
    bool leap = false;
    if ((y % 4) == 0) {
      leap = true;
      if ((y % 100) == 0 && (y % 400) != 0) leap = false;
    }
    sum += leap ? 366 : 365;
    y++;
  }
  while (m < month) {
    unsigned days = kMonths[m - 1];
    if (days == 28) {
      bool leap = false;
      if ((y % 4) == 0) {
        leap = true;
        if ((y % 100) == 0 && (y % 400) != 0) leap = false;
      }
      if (leap) days++;
    }
    sum += days;
    m++;
  }
  sum += day - 1;
  return (sum + 1) % 7;  // 1900-01-01 was Monday
}

auto Srtc::read(uint24 address) -> uint8 {
  uint32 offs = address & 0xFFFF;
  if (offs != 0x2800) return 0x00;  // 2801 is write-only
  if (mode_ != Read) return 0x00;
  if (index_ < 0) {
    updateTime();
    index_++;
    return 0x0F;
  }
  if (index_ > 12) {
    index_ = -1;
    return 0x0F;
  }
  return reg_[index_++];
}

void Srtc::write(uint24 address, uint8 data) {
  uint32 offs = address & 0xFFFF;
  if (offs != 0x2801) return;
  data &= 0x0F;
  if (data == 0x0D) {
    mode_ = Read;
    index_ = -1;
    return;
  }
  if (data == 0x0E) {
    mode_ = Command;
    return;
  }
  if (data == 0x0F) return;
  if (mode_ == Write) {
    if (index_ >= 0 && index_ < 12) {
      reg_[index_++] = data;
      if (index_ == 12) {
        unsigned day = reg_[6] + reg_[7] * 10;
        unsigned month = reg_[8];
        unsigned year = reg_[9] + reg_[10] * 10 + reg_[11] * 100 + 1000;
        reg_[index_++] = (uint8)weekday(year, month, day);
      }
    }
  } else if (mode_ == Command) {
    if (data == 0) {
      mode_ = Write;
      index_ = 0;
    } else if (data == 4) {
      mode_ = Ready;
      index_ = -1;
      for (int i = 0; i < 13; i++) reg_[i] = 0;
    } else {
      mode_ = Ready;
    }
  }
}

void Srtc::serialize(Writer& w) const {
  w.raw(reg_.data(), reg_.size());
  w.u8((uint8)mode_);
  // index in [-1, 19]; store biased
  w.u8((uint8)(index_ + 1));
}

void Srtc::deserialize(Reader& r) {
  r.raw(reg_.data(), reg_.size());
  mode_ = (Mode)r.u8();
  index_ = (int)r.u8() - 1;
  updateTime();
}

}  // namespace snes
