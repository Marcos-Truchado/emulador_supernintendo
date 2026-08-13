cmake_minimum_required(VERSION 3.20)
project(snes-emu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

add_subdirectory(core)
add_subdirectory(tools)
add_subdirectory(frontend)