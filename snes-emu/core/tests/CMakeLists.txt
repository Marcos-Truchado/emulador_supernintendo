add_executable(snes_tests
  main.cpp
  cartridge_tests.cpp
  bus_tests.cpp
  cpu_tests.cpp
  scheduler_tests.cpp
  ppu_timing_tests.cpp
  ppu_mmio_tests.cpp
  ppu_bg_tests.cpp
  ppu_sprite_tests.cpp
  ppu_window_tests.cpp
  ppu_colormath_tests.cpp
  ppu_mode7_tests.cpp
  dma_tests.cpp
  apu_tests.cpp
  interrupt_tests.cpp
  save_state_tests.cpp
)
target_include_directories(snes_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party ${CMAKE_SOURCE_DIR}/core/src)
target_link_libraries(snes_tests PRIVATE snes_core)

enable_testing()
add_test(NAME snes_tests COMMAND snes_tests)