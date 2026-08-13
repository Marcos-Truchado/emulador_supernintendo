// snes_frontend — SDL2 frontend for the SNES core (phase 7).
//
// Window + video (PPU framebuffer -> SDL texture), audio (DSP sample buffer
// -> SDL_QueueAudio) and input (keyboard -> joypad state). The core is only
// driven one frame at a time; save states use the System facade.

#include <SDL.h>

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "snes/snes.hpp"

namespace {

// Display area: 256x224 visible pixels (the standard SNES safe area; the
// core's pixelColor handles the overscan row offset internally).
constexpr int kWidth = 256;
constexpr int kHeight = 224;
constexpr int kScale = 3;

// SNES button bit layout (matches Bus::setJoypad / $4218-$421F).
constexpr snes::uint16 kB = 0x8000, kSelect = 0x2000, kStart = 0x1000;
constexpr snes::uint16 kUp = 0x0800, kDown = 0x0400, kLeft = 0x0200, kRight = 0x0100;
constexpr snes::uint16 kA = 0x0080;

auto readJoypad(const snes::uint8* keys) -> snes::uint16 {
  snes::uint16 v = 0;
  if (keys[SDL_SCANCODE_SPACE]) v |= kA;   // jump
  if (keys[SDL_SCANCODE_Q]) v |= kB;       // run / spin
  if (keys[SDL_SCANCODE_W]) v |= kUp;
  if (keys[SDL_SCANCODE_S]) v |= kDown;
  if (keys[SDL_SCANCODE_A]) v |= kLeft;
  if (keys[SDL_SCANCODE_D]) v |= kRight;
  if (keys[SDL_SCANCODE_RETURN]) v |= kStart;
  if (keys[SDL_SCANCODE_E]) v |= kSelect;
  return v;
}

auto loadStateFile(snes::System& system, const std::string& path) -> bool {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<snes::uint8> data(size > 0 ? size : 0);
  if (size > 0) std::fread(data.data(), 1, data.size(), f);
  std::fclose(f);
  return system.loadState(data);
}

void saveStateFile(snes::System& system, const std::string& path) {
  const std::vector<snes::uint8> data = system.saveState();
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  std::fwrite(data.data(), 1, data.size(), f);
  std::fclose(f);
}

}  // namespace

auto main(int argc, char** argv) -> int {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <rom.sfc>\n", argv[0]);
    return 2;
  }
  const std::string romPath = argv[1];

  snes::System system;
  std::string error;
  if (!system.load(romPath, &error)) {
    std::fprintf(stderr, "load failed: %s\n", error.c_str());
    return 2;
  }
  system.reset();

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Window* window = SDL_CreateWindow("snes-emu", SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED, kWidth * kScale,
                                        kHeight * kScale, SDL_WINDOW_SHOWN);
  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
                                           SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);
  if (!window || !renderer || !texture) {
    std::fprintf(stderr, "SDL setup failed: %s\n", SDL_GetError());
    return 1;
  }

  // Audio: 32kHz stereo int16, fed from the core via SDL_QueueAudio.
  SDL_AudioSpec want{};
  want.freq = 32000;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 1024;
  SDL_AudioDeviceID audio = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
  if (audio) SDL_PauseAudioDevice(audio, 0);

  std::vector<snes::uint8> rgb(size_t(kWidth) * kHeight * 3);

  const std::string statePath = romPath + ".ss";
  bool running = true;
  uint64_t lastRendered = system.renderedFrames();

  // Run one frame and queue its audio; reused by the pre-roll and the loop.
  auto runFrame = [&] {
    const uint64_t target = lastRendered + 1;
    while (system.renderedFrames() < target) system.step();
    lastRendered = target;
    if (audio) {
      snes::int16 samples[2048];
      const size_t n = system.readAudio(samples, 2048);
      if (n > 0) SDL_QueueAudio(audio, samples, snes::uint32(n * sizeof(snes::int16)));
    }
  };

  // Pre-roll a few frames so the audio queue has a buffer before playback.
  for (int i = 0; i < 4; i++) runFrame();

  while (running) {
    const uint64_t frameStart = SDL_GetTicks64();

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) running = false;
      if (ev.type == SDL_KEYDOWN) {
        switch (ev.key.keysym.sym) {
          case SDLK_ESCAPE: running = false; break;
          case SDLK_z: saveStateFile(system, statePath); break;
          case SDLK_x: loadStateFile(system, statePath); break;
          default: break;
        }
      }
    }

    system.setJoypad(0, readJoypad(SDL_GetKeyboardState(nullptr)));
    runFrame();

    // Convert the PPU framebuffer to RGB888 (RGB555 -> RGB888).
    for (int y = 0; y < kHeight; y++) {
      for (int x = 0; x < kWidth; x++) {
        const snes::uint16 v = system.pixelColor(x, y);
        const snes::uint8 r = snes::uint8((v >> 10) & 0x1F), g = snes::uint8((v >> 5) & 0x1F), b = snes::uint8(v & 0x1F);
        snes::uint8* p = &rgb[(size_t(y) * kWidth + x) * 3];
        p[0] = snes::uint8((r << 3) | (r >> 2));
        p[1] = snes::uint8((g << 3) | (g >> 2));
        p[2] = snes::uint8((b << 3) | (b >> 2));
      }
    }
    SDL_UpdateTexture(texture, nullptr, rgb.data(), kWidth * 3);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);

    // Pace to ~60fps (SNES NTSC), accounting for the frame compute time.
    const uint64_t elapsed = SDL_GetTicks64() - frameStart;
    if (elapsed < 16) SDL_Delay(snes::uint32(16 - elapsed));
  }

  if (audio) SDL_CloseAudioDevice(audio);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
