cmake_minimum_required(VERSION 3.10)
project(poleis_nice LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure operating system and architecture defines similar to the original Makefiles
set(OS "LINUX" CACHE STRING "Operating system macro passed to the compiler")
set(ARCH "" CACHE STRING "Optional architecture macro passed to the compiler")

if(OS)
  add_compile_definitions(${OS})
endif()
if(ARCH)
  add_compile_definitions(${ARCH})
endif()

add_subdirectory(src)
add_subdirectory(app)
