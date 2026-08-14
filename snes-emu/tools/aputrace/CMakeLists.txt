add_executable(snes_aputrace aputrace.cpp)
target_include_directories(snes_aputrace PRIVATE
  ${CMAKE_SOURCE_DIR}/core/include
  ${CMAKE_SOURCE_DIR}/core/src
)
target_link_libraries(snes_aputrace PRIVATE snes_core)

add_executable(snes_stateload stateload.cpp)
target_include_directories(snes_stateload PRIVATE
  ${CMAKE_SOURCE_DIR}/core/include
  ${CMAKE_SOURCE_DIR}/core/src
)
target_link_libraries(snes_stateload PRIVATE snes_core)
