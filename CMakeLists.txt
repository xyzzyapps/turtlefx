cmake_minimum_required(VERSION 3.20)
project(turtlefx C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(turtlefx
    src/main.c
    src/logo_parser.c
    src/logo_vm.c
    src/renderer.c
)

target_include_directories(turtlefx PRIVATE src)

if(WIN32)
    # Prefer installed MSYS2/UCRT64 or system raylib
    find_path(RAYLIB_INCLUDE_DIR raylib.h HINTS "C:/msys64/ucrt64/include")
    find_library(RAYLIB_LIBRARY NAMES raylib HINTS "C:/msys64/ucrt64/lib")
    if(RAYLIB_INCLUDE_DIR AND RAYLIB_LIBRARY)
        target_include_directories(turtlefx PRIVATE ${RAYLIB_INCLUDE_DIR})
        target_link_libraries(turtlefx PRIVATE ${RAYLIB_LIBRARY} opengl32 gdi32 winmm m)
    else()
        find_package(raylib REQUIRED)
        target_link_libraries(turtlefx PRIVATE raylib opengl32 gdi32 winmm m)
    endif()
else()
    find_package(raylib REQUIRED)
    target_link_libraries(turtlefx PRIVATE raylib m)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(turtlefx PRIVATE -Wall -Wextra -Wno-unused-parameter -O2)
endif()
