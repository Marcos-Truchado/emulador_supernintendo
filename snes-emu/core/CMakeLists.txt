# The core is a platform-free static library; it must never include SDL2 or
# any OS header. Frontend/tools link against it.
add_library(snes_core STATIC
  src/cartridge/cartridge.cpp
  src/bus/bus.cpp
  src/system/system.cpp
  src/scheduler/scheduler.cpp
  src/ppu/timing.cpp
  src/ppu/io.cpp
  src/ppu/render.cpp
  src/ppu/serialize.cpp
  src/apu/apu.cpp
  src/cpu/cpu65816.cpp
  src/cpu/algorithms.cpp
  src/cpu/opcodes.cpp
  src/cpu/instructions-read.cpp
  src/cpu/instructions-write.cpp
  src/cpu/instructions-modify.cpp
  src/cpu/instructions-pc.cpp
  src/cpu/instructions-other.cpp
  src/cpu/disassembler.cpp
)
target_include_directories(snes_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(snes_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_subdirectory(tests)