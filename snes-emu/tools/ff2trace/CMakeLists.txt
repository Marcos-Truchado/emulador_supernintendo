add_executable(snes_ff2trace ff2trace.cpp)
target_include_directories(snes_ff2trace PRIVATE
  ${CMAKE_SOURCE_DIR}/core/include
  ${CMAKE_SOURCE_DIR}/core/src
)
target_link_libraries(snes_ff2trace PRIVATE snes_core)