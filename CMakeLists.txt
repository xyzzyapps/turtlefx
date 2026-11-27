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

if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".js")
    if(DEFINED RAYLIB_INCLUDE_DIR AND DEFINED RAYLIB_LIBRARY)
        target_include_directories(turtlefx PRIVATE ${RAYLIB_INCLUDE_DIR})
        target_link_libraries(turtlefx PRIVATE ${RAYLIB_LIBRARY})
    else()
        find_package(raylib REQUIRED)
        target_link_libraries(turtlefx PRIVATE raylib)
    endif()
    target_link_options(turtlefx PRIVATE
        -sUSE_GLFW=3
        -sUSE_WEBGL2=1
        -sFULL_ES3=1
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=67108864
        -sSTACK_SIZE=4194304
        -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString
        -sEXPORTED_FUNCTIONS=_main,_wasm_load_example,_wasm_run_code,_wasm_set_debug,_wasm_debug_cmd,_wasm_set_input_enabled,_wasm_get_debug_enabled,_wasm_get_error,_malloc,_free
        --preload-file ${CMAKE_SOURCE_DIR}/examples@examples
    )
elseif(WIN32)
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
