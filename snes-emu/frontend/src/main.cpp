// snes_frontend — SDL2 frontend for the SNES core.
//
// Two modes:
//   - Launcher (no ROM argument): a retro-styled menu that lists the games in
//     the juegos folder and runs the selected one on click.
//   - Direct play (ROM argument): runs the given ROM immediately.
//
// Window + video (PPU framebuffer -> SDL texture), audio (DSP sample buffer
// -> SDL_QueueAudio) and input (keyboard -> joypad state). The core is only
// driven one frame at a time; save states use the System facade.

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <dirent.h>
#include <string>
#include <vector>

#include "snes/snes.hpp"

#include "font.hpp"

namespace {

// Display area: 256x224 visible pixels (the standard SNES safe area; the
// core's pixelColor handles the overscan row offset internally).
constexpr int kWidth = 256;
constexpr int kHeight = 224;
constexpr int kScale = 3;

// Retro palette (SNES button colors).
struct RGB { uint8_t r, g, b; };
constexpr RGB kYellow{255, 224, 0};
constexpr RGB kGreen{0, 200, 0};
constexpr RGB kBlue{0, 140, 255};
constexpr RGB kRed{230, 0, 0};
constexpr RGB kGray{190, 190, 190};
constexpr RGB kWhite{255, 255, 255};

// Where the ROM images live.
constexpr const char* kGamesDir = "/Users/matru/Desktop/emulador_supernintendo/juegos";

// Toggle between fullscreen (desktop) and a normal window.
void toggleFullscreen(SDL_Window* window) {
  const bool full = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
  SDL_SetWindowFullscreen(window, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

// SNES button bit layout (matches Bus::setJoypad / $4218-$421F).
constexpr snes::uint16 kB = 0x8000, kSelect = 0x2000, kStart = 0x1000;
constexpr snes::uint16 kUp = 0x0800, kDown = 0x0400, kLeft = 0x0200, kRight = 0x0100;
constexpr snes::uint16 kA = 0x0080, kX = 0x0040, kY = 0x4000;
constexpr snes::uint16 kL = 0x0020, kR = 0x0010;

auto readJoypad(const snes::uint8* keys) -> snes::uint16 {
  snes::uint16 v = 0;
  if (keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_X]) v |= kA;
  if (keys[SDL_SCANCODE_Q] || keys[SDL_SCANCODE_Z]) v |= kB;
  if (keys[SDL_SCANCODE_V]) v |= kX;
  if (keys[SDL_SCANCODE_C]) v |= kY;
  if (keys[SDL_SCANCODE_U]) v |= kL;
  if (keys[SDL_SCANCODE_I]) v |= kR;
  if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) v |= kUp;
  if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) v |= kDown;
  if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) v |= kLeft;
  if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) v |= kRight;
  if (keys[SDL_SCANCODE_RETURN]) v |= kStart;
  if (keys[SDL_SCANCODE_RSHIFT] || keys[SDL_SCANCODE_E]) v |= kSelect;
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

// --- launcher helpers ---

struct GameEntry {
  std::string name;  // display name (without extension)
  std::string path;  // full path
};

// Scan a directory for .smc/.sfc images, skipping hidden files and the
// .ss save-state sidecars. Sorted alphabetically.
auto listGames(const std::string& dir) -> std::vector<GameEntry> {
  std::vector<GameEntry> games;
  std::vector<std::string> entries;
  // Manual directory scan avoids std::filesystem so the frontend keeps a
  // single, simple dependency set. We use the POSIX dirent API below.
  if (auto* d = opendir(dir.c_str())) {
    while (auto* e = readdir(d)) {
      const std::string n = e->d_name;
      if (n.size() < 2 || n[0] == '.') continue;
      const std::string lower = [&] {
        std::string s = n;
        for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
      }();
      if (lower.ends_with(".smc") || lower.ends_with(".sfc")) {
        entries.push_back(n);
      }
    }
    closedir(d);
  }
  std::sort(entries.begin(), entries.end());
  for (const auto& n : entries) {
    GameEntry g;
    g.name = n.substr(0, n.find_last_of('.'));
    g.path = dir + "/" + n;
    games.push_back(std::move(g));
  }
  return games;
}

// Draw the stylized SNES logo (colored "SUPER NINTENDO" banner + four
// buttons) centered at (cx, cy).
void drawLogo(SDL_Renderer* renderer, int cx, int cy) {
  SDL_Texture* txt = renderText(renderer, "SUPER NINTENDO", kRed.r, kRed.g, kRed.b, 3);
  SDL_Texture* sub = renderText(renderer, "entertainment system", kGray.r, kGray.g, kGray.b, 1);
  if (txt) {
    int w = 0, h = 0;
    SDL_QueryTexture(txt, nullptr, nullptr, &w, &h);
    SDL_Rect dst{cx - w / 2, cy - h / 2 - 14, w, h};
    SDL_RenderCopy(renderer, txt, nullptr, &dst);
    SDL_DestroyTexture(txt);
  }
  if (sub) {
    int w = 0, h = 0;
    SDL_QueryTexture(sub, nullptr, nullptr, &w, &h);
    SDL_Rect dst{cx - w / 2, cy + 10, w, h};
    SDL_RenderCopy(renderer, sub, nullptr, &dst);
    SDL_DestroyTexture(sub);
  }
  // Four colored buttons (red/yellow/green/blue) under the banner.
  const RGB btns[4] = {kRed, kYellow, kGreen, kBlue};
  const int size = 14, gap = 8;
  const int total = 4 * size + 3 * gap;
  for (int i = 0; i < 4; i++) {
    SDL_Rect r{cx - total / 2 + i * (size + gap), cy + 34, size, size};
    SDL_SetRenderDrawColor(renderer, btns[i].r, btns[i].g, btns[i].b, 255);
    SDL_RenderFillRect(renderer, &r);
  }
}

// Draw the centered launcher frame and return the selected game path, or an
// empty string when the user quit.
auto launcher(SDL_Renderer* renderer, SDL_Window* window) -> std::string {
  const std::vector<GameEntry> games = listGames(kGamesDir);

  bool running = true;
  std::string selected;

  while (running) {
    int winW = 0, winH = 0;
    SDL_GetWindowSize(window, &winW, &winH);

    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) { running = false; break; }
      if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) { running = false; break; }
      if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F11) toggleFullscreen(window);
      if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
        // Game list hit-testing (rows are recomputed below).
        const int listX = winW * 62 / 100;
        const int rowH = 30;
        const int listY = 170;
        for (size_t i = 0; i < games.size(); i++) {
          SDL_Rect r{listX, listY + int(i) * rowH, winW - listX - 40, rowH};
          if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
            selected = games[i].path;
            running = false;
            break;
          }
        }
      }
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    // Title + subtitle (top-center).
    {
      SDL_Texture* t = renderText(renderer, "emulador supernintendo", kYellow.r, kYellow.g, kYellow.b, 4);
      if (t) {
        int w = 0, h = 0;
        SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect dst{winW / 2 - w / 2, 30, w, h};
        SDL_RenderCopy(renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
      }
    }
    {
      SDL_Texture* t = renderText(renderer, "creador: Marcos Truchado Antón", kGray.r, kGray.g, kGray.b, 2);
      if (t) {
        int w = 0, h = 0;
        SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect dst{winW / 2 - w / 2, 30 + 4 * 8 + 8, w, h};
        SDL_RenderCopy(renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
      }
    }

    // Logo in the left-center area.
    drawLogo(renderer, winW * 30 / 100, winH / 2 - 20);

    // Game list header + entries on the right.
    {
      SDL_Texture* t = renderText(renderer, "JUEGOS", kBlue.r, kBlue.g, kBlue.b, 2);
      if (t) {
        int w = 0, h = 0;
        SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect dst{winW * 62 / 100, 120, w, h};
        SDL_RenderCopy(renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
      }
    }

    const int listX = winW * 62 / 100;
    const int rowH = 30;
    for (size_t i = 0; i < games.size(); i++) {
      SDL_Rect r{listX, 170 + int(i) * rowH, winW - listX - 40, rowH};
      const bool hover = (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h);
      const RGB c = hover ? kYellow : kGreen;
      SDL_Texture* t = renderText(renderer, games[i].name, c.r, c.g, c.b, 2);
      if (t) {
        int w = 0, h = 0;
        SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect dst{r.x, r.y, w, h};
        SDL_RenderCopy(renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
      }
    }

    // Hint.
    {
      SDL_Texture* t = renderText(renderer, "Flechas/WASD mover - X/Space A - Z/Q B - Enter Start",
                                  kWhite.r, kWhite.g, kWhite.b, 1);
      if (t) {
        int w = 0, h = 0;
        SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect dst{winW / 2 - w / 2, winH - 46, w, h};
        SDL_RenderCopy(renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
      }
    }
    {
      SDL_Texture* t = renderText(renderer, "Shift/E Select - V X - C Y - U L - I R - F5 guardar - F8 cargar",
                                  kWhite.r, kWhite.g, kWhite.b, 1);
      if (t) {
        int w = 0, h = 0;
        SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect dst{winW / 2 - w / 2, winH - 28, w, h};
        SDL_RenderCopy(renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
      }
    }

    SDL_RenderPresent(renderer);
    SDL_Delay(16);
  }

  return selected;
}

// Run one ROM. Returns true to go back to the launcher (ESC), false to quit.
auto runGame(const std::string& romPath, SDL_Renderer* renderer, SDL_Window* window,
             SDL_AudioDeviceID audio) -> bool {
  snes::System system;
  std::string error;
  if (!system.load(romPath, &error)) {
    std::fprintf(stderr, "load failed: %s\n", error.c_str());
    return false;
  }
  system.reset();

  SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
                                           SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);
  if (!texture) return false;

  std::vector<snes::uint8> rgb(size_t(kWidth) * kHeight * 3);
  const std::string statePath = romPath + ".ss";
  bool running = true;
  uint64_t lastRendered = system.renderedFrames();

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

  for (int i = 0; i < 4; i++) runFrame();

  while (running) {
    const uint64_t frameStart = SDL_GetTicks64();

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) return false;
      if (ev.type == SDL_KEYDOWN) {
        switch (ev.key.keysym.sym) {
          case SDLK_ESCAPE: return true;  // back to launcher
          case SDLK_F11: toggleFullscreen(window); break;
          case SDLK_F5: saveStateFile(system, statePath); break;
          case SDLK_F8: loadStateFile(system, statePath); break;
          default: break;
        }
      }
    }

    SDL_PumpEvents();
    system.setJoypad(0, readJoypad(SDL_GetKeyboardState(nullptr)));
    runFrame();

    for (int y = 0; y < kHeight; y++) {
      for (int x = 0; x < kWidth; x++) {
        const snes::uint16 v = system.pixelColor(x, y);
        const snes::uint8 r = snes::uint8(v & 0x1F), g = snes::uint8((v >> 5) & 0x1F), b = snes::uint8((v >> 10) & 0x1F);
        snes::uint8* p = &rgb[(size_t(y) * kWidth + x) * 3];
        p[0] = snes::uint8((r << 3) | (r >> 2));
        p[1] = snes::uint8((g << 3) | (g >> 2));
        p[2] = snes::uint8((b << 3) | (b >> 2));
      }
    }
    SDL_UpdateTexture(texture, nullptr, rgb.data(), kWidth * 3);
    SDL_RenderClear(renderer);
    // Letterbox the 256x224 frame to 4:3 (authentic SNES TV aspect) inside
    // the current window.
    {
      int winW = 0, winH = 0;
      SDL_GetWindowSize(window, &winW, &winH);
      const float aspect = 4.0f / 3.0f;
      SDL_Rect dst;
      if (float(winW) / float(winH) > aspect) {
        dst.h = winH;
        dst.w = int(winH * aspect);
        dst.x = (winW - dst.w) / 2;
        dst.y = 0;
      } else {
        dst.w = winW;
        dst.h = int(winW / aspect);
        dst.x = 0;
        dst.y = (winH - dst.h) / 2;
      }
      SDL_RenderCopy(renderer, texture, nullptr, &dst);
    }
    SDL_RenderPresent(renderer);

    const uint64_t elapsed = SDL_GetTicks64() - frameStart;
    if (elapsed < 16) SDL_Delay(snes::uint32(16 - elapsed));
  }

  SDL_DestroyTexture(texture);
  return false;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  std::string romPath;
  if (argc >= 2) romPath = argv[1];

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Window* window = SDL_CreateWindow("emulador supernintendo", SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED, kWidth * kScale,
                                        kHeight * kScale,
                                        SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_RESIZABLE);
  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!window || !renderer) {
    std::fprintf(stderr, "SDL setup failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_AudioSpec want{};
  want.freq = 32000;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 1024;
  SDL_AudioDeviceID audio = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
  if (audio) SDL_PauseAudioDevice(audio, 0);

  int rc = 0;

  if (!romPath.empty()) {
    // Direct play: run the given ROM; ESC quits.
    runGame(romPath, renderer, window, audio);
  } else {
    // Launcher loop: pick a game, run it, return on ESC.
    bool quit = false;
    while (!quit) {
      std::string game = launcher(renderer, window);
      if (game.empty()) { quit = true; break; }
      if (!runGame(game, renderer, window, audio)) quit = true;
    }
  }

  if (audio) SDL_CloseAudioDevice(audio);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return rc;
}
