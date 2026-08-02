cmake_minimum_required(VERSION 3.22)
project(screen_render C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Repo root (this file lives in tools/screen_render).
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(HERE "${CMAKE_CURRENT_SOURCE_DIR}")

include(FetchContent)

# Build LVGL like the firmware does (same version/config) but via a host wrapper
# that keeps every visual setting and only swaps the memory backend to the C heap
# (see lv_conf_host.h for why the 24 KiB MCU pool can't be reused on a 64-bit PC).
set(LV_BUILD_CONF_PATH "${HERE}/lv_conf_host.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    lvgl
    URL https://github.com/lvgl/lvgl/archive/refs/tags/v9.5.0.tar.gz
    URL_HASH SHA256=34a955cdf3a2d005507b704e87357af669a114523b6d3f77b5344fdc68717bc6
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(lvgl)

file(GLOB LVGL_SOURCES CONFIGURE_DEPENDS
    ${lvgl_SOURCE_DIR}/src/*.c
    ${lvgl_SOURCE_DIR}/src/core/*.c
    ${lvgl_SOURCE_DIR}/src/display/*.c
    ${lvgl_SOURCE_DIR}/src/draw/*.c
    ${lvgl_SOURCE_DIR}/src/draw/convert/lv_draw_buf_convert.c
    ${lvgl_SOURCE_DIR}/src/draw/sw/*.c
    ${lvgl_SOURCE_DIR}/src/draw/sw/blend/*.c
    ${lvgl_SOURCE_DIR}/src/font/*.c
    ${lvgl_SOURCE_DIR}/src/font/fmt_txt/*.c
    ${lvgl_SOURCE_DIR}/src/indev/*.c
    ${lvgl_SOURCE_DIR}/src/layouts/*.c
    ${lvgl_SOURCE_DIR}/src/layouts/flex/*.c
    ${lvgl_SOURCE_DIR}/src/layouts/grid/*.c
    ${lvgl_SOURCE_DIR}/src/libs/bin_decoder/*.c
    ${lvgl_SOURCE_DIR}/src/misc/*.c
    ${lvgl_SOURCE_DIR}/src/misc/cache/*.c
    ${lvgl_SOURCE_DIR}/src/misc/cache/class/*.c
    ${lvgl_SOURCE_DIR}/src/misc/cache/instance/*.c
    ${lvgl_SOURCE_DIR}/src/osal/*.c
    ${lvgl_SOURCE_DIR}/src/stdlib/*.c
    ${lvgl_SOURCE_DIR}/src/stdlib/builtin/*.c
    ${lvgl_SOURCE_DIR}/src/stdlib/clib/*.c   # host uses the C-heap allocator
    ${lvgl_SOURCE_DIR}/src/themes/*.c
    ${lvgl_SOURCE_DIR}/src/themes/default/*.c
    ${lvgl_SOURCE_DIR}/src/tick/*.c
    ${lvgl_SOURCE_DIR}/src/widgets/*.c
    ${lvgl_SOURCE_DIR}/src/widgets/*/*.c
)
set_property(TARGET lvgl PROPERTY SOURCES ${LVGL_SOURCES})

add_executable(render
    # real firmware UI / app / emulator code (unmodified)
    ${ROOT}/Core/Src/app.cpp
    ${ROOT}/Core/Src/emulators.cpp
    ${ROOT}/Core/Src/emulator_browser.cpp
    ${ROOT}/Core/Src/chip8_screen.cpp
    ${ROOT}/Core/Src/zx48_screen.cpp
    ${ROOT}/Core/Src/chip8.c
    ${ROOT}/Core/Src/zx48.c
    ${ROOT}/Core/Src/vendor/superzazu_z80/z80.c
    ${ROOT}/Core/Src/runtime_workspace.c
    ${ROOT}/Core/Src/rompack.c
    ${ROOT}/Core/Src/ui/Screen.cpp
    ${ROOT}/Core/Src/ui/Menu.cpp
    ${ROOT}/Core/Src/ui/Slider.cpp
    ${ROOT}/Core/Src/ui/ProgressBar.cpp
    ${ROOT}/Core/Src/ui/Theme.cpp
    ${ROOT}/Core/Src/ui/BatteryIndicator.cpp
    # host backend
    ${HERE}/host_display.c
    ${HERE}/host_stubs.c
    ${HERE}/render_main.cpp
)

# Shim headers must win over the real Drivers HAL headers, so list them first.
target_include_directories(render BEFORE PRIVATE
    ${HERE}/shim
    ${HERE}
)
target_include_directories(render PRIVATE
    ${ROOT}/Core/Inc
    ${ROOT}/Core/Src   # for "vendor/superzazu_z80/z80.h"
)

target_link_libraries(render lvgl::lvgl)
