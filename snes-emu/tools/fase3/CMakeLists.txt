# Builds the phase-3 timing test ROM with Python, then the runner that
# executes it and validates the counts.

set(FASE3_ROM "${CMAKE_CURRENT_BINARY_DIR}/fase3_timing.sfc")

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_custom_command(
  OUTPUT "${FASE3_ROM}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build_test_rom.py"
          "${FASE3_ROM}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/build_test_rom.py"
  COMMENT "Building fase3 timing test ROM"
)

add_custom_target(fase3_rom DEPENDS "${FASE3_ROM}")

add_executable(snes_fase3 fase3.cpp)
target_include_directories(snes_fase3 PRIVATE
  "${CMAKE_SOURCE_DIR}/core/include")
target_link_libraries(snes_fase3 PRIVATE snes_core)
add_dependencies(snes_fase3 fase3_rom)
