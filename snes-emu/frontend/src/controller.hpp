#pragma once

// Xbox One controller input for the SNES frontend, via the SDL2
// GameController API. SDL ships the mapping database for Xbox One wired and
// Bluetooth controllers on macOS, so no manual button-index mapping is
// needed.
//
// SNES -> Xbox One mapping (by physical position: every SNES face button
// sits in the same spot on the Xbox pad, like the original controller):
//   B->A (bottom-left)  Y->X (top-left)  A->B (bottom-right)  X->Y (top-right)
//   L->LB  R->RB  Start->Menu  Select->View
// D-pad -> D-pad; the left stick also moves (deadzone ~30% of the axis
// range, so accidental diagonals don't register).
//
// The Gamepad is hotplug-aware (SDL_CONTROLLERDEVICEADDED/REMOVED) and
// exposes the SNES joypad bitset for the core plus raw edge-triggered
// presses for launcher navigation and emulator shortcuts such as
// Select+RB save / Select+LB load / Select+Start exit.

#include <SDL.h>

#include <array>
#include <cstdint>

#include "snes/snes.hpp"

namespace {

constexpr int kStickDeadzone = 10000;  // ~30% of the full 32768 axis range

class Gamepad {
 public:
  // Hotplug: feed every SDL_CONTROLLERDEVICEADDED / SDL_CONTROLLERDEVICEREMOVED
  // event here. The first connected controller is used.
  auto handleEvent(const SDL_Event& ev) -> void {
    if (ev.type == SDL_CONTROLLERDEVICEADDED && !gc_) {
      gc_ = SDL_GameControllerOpen(ev.cdevice.which);
      id_ = gc_ ? SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc_)) : -1;
    } else if (ev.type == SDL_CONTROLLERDEVICEREMOVED && ev.cdevice.which == id_) {
      SDL_GameControllerClose(gc_);
      gc_ = nullptr;
      id_ = -1;
    }
  }

  auto connected() const -> bool { return gc_ != nullptr; }

  // Snapshot the current button state for edge detection; call once per
  // frame before reading pressed().
  auto poll() -> void { prev_ = current(); }

  // SNES joypad bitset (layout of System::setJoypad / $4218): face and
  // shoulder buttons by SNES position, Select/Start, D-pad and left stick.
  // 0 when no controller is connected.
  auto joypad() const -> snes::uint16 {
    snes::uint16 v = 0;
    if (!gc_) return 0;
    if (held(SDL_CONTROLLER_BUTTON_A)) v |= 0x8000;                // SNES B (bottom-left)
    if (held(SDL_CONTROLLER_BUTTON_X)) v |= 0x4000;                // SNES Y (top-left)
    if (held(SDL_CONTROLLER_BUTTON_BACK)) v |= 0x2000;             // SNES Select
    if (held(SDL_CONTROLLER_BUTTON_START)) v |= 0x1000;            // SNES Start
    v |= direction();
    if (held(SDL_CONTROLLER_BUTTON_B)) v |= 0x0080;                // SNES A (bottom-right)
    if (held(SDL_CONTROLLER_BUTTON_Y)) v |= 0x0040;                // SNES X (top-right)
    if (held(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) v |= 0x0020;     // SNES L
    if (held(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) v |= 0x0010;    // SNES R
    return v;
  }

  // Direction bits from the D-pad and the left stick (Up 0x0800, Down 0x0400,
  // Left 0x0200, Right 0x0100); used by joypad() and menu navigation.
  auto direction() const -> snes::uint16 {
    snes::uint16 d = 0;
    if (!gc_) return 0;
    if (held(SDL_CONTROLLER_BUTTON_DPAD_UP) || axis(SDL_CONTROLLER_AXIS_LEFTY) < -kStickDeadzone) d |= 0x0800;
    if (held(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || axis(SDL_CONTROLLER_AXIS_LEFTY) > kStickDeadzone) d |= 0x0400;
    if (held(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || axis(SDL_CONTROLLER_AXIS_LEFTX) < -kStickDeadzone) d |= 0x0200;
    if (held(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || axis(SDL_CONTROLLER_AXIS_LEFTX) > kStickDeadzone) d |= 0x0100;
    return d;
  }

  // Raw button state; false when disconnected.
  auto held(SDL_GameControllerButton b) const -> bool {
    return gc_ && SDL_GameControllerGetButton(gc_, b) != 0;
  }

  // Rising edge: pressed this frame but not the previous one.
  auto pressed(SDL_GameControllerButton b) const -> bool {
    return held(b) && !prev_[b];
  }

 private:
  auto axis(SDL_GameControllerAxis a) const -> int16_t {
    return gc_ ? SDL_GameControllerGetAxis(gc_, a) : 0;
  }

  auto current() const -> std::array<bool, SDL_CONTROLLER_BUTTON_MAX> {
    std::array<bool, SDL_CONTROLLER_BUTTON_MAX> s{};
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) s[i] = held(SDL_GameControllerButton(i));
    return s;
  }

  SDL_GameController* gc_ = nullptr;
  SDL_JoystickID id_ = -1;
  std::array<bool, SDL_CONTROLLER_BUTTON_MAX> prev_{};
};

}  // namespace
