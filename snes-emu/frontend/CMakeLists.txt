cmake_minimum_required(VERSION 3.20)

# SDL2 frontend for the SNES core: window + video (PPU framebuffer), audio
# (DSP sample buffer) and input (joypad). Lives entirely outside core/ and is
# the only module allowed to depend on platform libraries.
find_package(SDL2 CONFIG REQUIRED)

add_executable(snes_frontend src/main.cpp)
target_link_libraries(snes_frontend PRIVATE snes_core SDL2::SDL2 SDL2::SDL2main)
target_include_directories(snes_frontend PRIVATE ${CMAKE_SOURCE_DIR}/core/include)
