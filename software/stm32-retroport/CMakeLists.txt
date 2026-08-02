cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME stm32-proj)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM, C and C++ languages
enable_language(C CXX ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

include(FetchContent)

set(LV_BUILD_CONF_PATH "${CMAKE_SOURCE_DIR}/Core/Inc/lv_conf.h" CACHE PATH "" FORCE)
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
    ${lvgl_SOURCE_DIR}/src/themes/*.c
    ${lvgl_SOURCE_DIR}/src/themes/default/*.c
    ${lvgl_SOURCE_DIR}/src/tick/*.c
    ${lvgl_SOURCE_DIR}/src/widgets/*.c
    ${lvgl_SOURCE_DIR}/src/widgets/*/*.c
)
set_property(TARGET lvgl PROPERTY SOURCES ${LVGL_SOURCES})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/app.cpp
    Core/Src/audio.c
    Core/Src/audio_assets.c
    Core/Src/chip8.c
    Core/Src/chip8_screen.cpp
    Core/Src/display.c
    Core/Src/emulator_browser.cpp
    Core/Src/emulators.cpp
    Core/Src/input.c
    Core/Src/power.c
    Core/Src/rompack.c
    Core/Src/rom_upload.c
    Core/Src/runtime_workspace.c
    Core/Src/settings.c
    Core/Src/st7735.c
    Core/Src/usbd_cdc_if.c
    Core/Src/usbd_conf.c
    Core/Src/usbd_desc.c
    Core/Src/w25q128.c
    Core/Src/zx48.c
    Core/Src/zx48_screen.cpp
    Core/Src/vendor/superzazu_z80/z80.c
    Core/Src/ui/BatteryIndicator.cpp
    Core/Src/ui/ProgressBar.cpp
    Core/Src/ui/Theme.cpp
    Core/Src/ui/Screen.cpp
    Core/Src/ui/Menu.cpp
    Core/Src/ui/Slider.cpp
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pcd.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pcd_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_spi.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_usb.c
    Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c
    Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c
    Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c
    Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src/usbd_cdc.c
)

set_source_files_properties(
    Core/Src/vendor/superzazu_z80/z80.c
    Core/Src/zx48.c
    Core/Src/zx48_screen.cpp
    PROPERTIES COMPILE_OPTIONS "-O3"
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Inc
    Middlewares/ST/STM32_USB_Device_Library/Core/Inc
    Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USE_USB_FS
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    -Wl,--start-group
    lvgl::lvgl
    -Wl,--end-group

    # Add user defined libraries
)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
    COMMENT "Generating ${CMAKE_PROJECT_NAME}.bin and ${CMAKE_PROJECT_NAME}.hex"
)
