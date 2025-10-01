cmake_minimum_required(VERSION 3.18.1)

# Only set build type if Android Studio hasn't already set it
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
    message(STATUS "CMAKE_BUILD_TYPE not set, defaulting to Debug")
else()
    message(STATUS "CMAKE_BUILD_TYPE already set to: ${CMAKE_BUILD_TYPE}")
endif()

# Force Android platform detection
if(NOT ANDROID)
    message(FATAL_ERROR "This CMakeLists.txt is designed for Android builds only. Please use Android NDK.")
endif()

project(scene_tile_test_android)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Force Android platform
set(CMAKE_SYSTEM_NAME Android)
set(CMAKE_SYSTEM_VERSION 1)

# Ensure we're using Android NDK compilers
if(NOT CMAKE_ANDROID_NDK)
    message(FATAL_ERROR "Android NDK not found. Please set ANDROID_NDK_HOME environment variable.")
endif()

# Set Android-specific compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DANDROID -fPIC")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DANDROID -fPIC")

# Debug-specific flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g -DDEBUG")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g -DDEBUG")
    message(STATUS "Debug build: Using -O0 optimization and debug symbols")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DNDEBUG")
    message(STATUS "Release build: Using -O3 optimization")
else()
    # Default for other build types (RelWithDebInfo, MinSizeRel, etc.)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -g")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -g")
    message(STATUS "${CMAKE_BUILD_TYPE} build: Using -O2 optimization with debug symbols")
endif()

# Android-specific settings
set(CMAKE_ANDROID_STL_TYPE c++_shared)

# ARM-specific settings
if(ANDROID_ABI STREQUAL "arm64-v8a" OR ANDROID_ABI STREQUAL "armeabi-v7a")
    # Enable ARM NEON for ARM builds
    add_compile_definitions(__ARM_NEON__=1)

    # Disable Intel SSE for ARM builds
    add_compile_definitions(INTEL_SSE=0)
elseif(ANDROID_ABI STREQUAL "x86" OR ANDROID_ABI STREQUAL "x86_64")
    # Enable Intel SSE for x86 builds
    add_compile_definitions(INTEL_SSE=1)
    add_compile_definitions(ARM_NEON=0)
endif()

# Force ARM compilation flags
if(ANDROID_ABI STREQUAL "armeabi-v7a")
    add_compile_options(-march=armv7-a -mthumb -mfpu=neon)
elseif(ANDROID_ABI STREQUAL "arm64-v8a")
    add_compile_options(-march=armv8-a)
endif()

# SDL2 configuration
set(SDL2_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/src/main/cpp/SDL2/include)

# Try to find SDL2 using find_package first
find_package(SDL2 QUIET)

# If SDL2 is not found via find_package, use manual configuration
if(NOT SDL2_FOUND)
    message(STATUS "SDL2 not found via find_package, using manual configuration")

    # Set SDL2 paths manually
    set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})

    # Use the built SDL2 library
    set(SDL2_LIBRARY_PATH ${CMAKE_SOURCE_DIR}/src/main/cpp/SDL2/libs/android/${ANDROID_ABI}/libSDL2.so)

    if(EXISTS ${SDL2_LIBRARY_PATH})
        # Add the library directory to the linker search path
        link_directories(${CMAKE_SOURCE_DIR}/src/main/cpp/SDL2/libs/android/${ANDROID_ABI})
        set(SDL2_LIBRARIES SDL2)
        message(STATUS "Found built SDL2 library: ${SDL2_LIBRARY_PATH}")
        message(STATUS "Added library directory: ${CMAKE_SOURCE_DIR}/src/main/cpp/SDL2/libs/android/${ANDROID_ABI}")
    else()
        message(WARNING "Built SDL2 library not found at: ${SDL2_LIBRARY_PATH}")
        message(WARNING "You may need to build SDL2 from source first.")
        set(SDL2_LIBRARIES "")
    endif()
else()
    message(STATUS "SDL2 found via find_package")
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/../src
    ${CMAKE_SOURCE_DIR}/../src/imgui
    ${CMAKE_SOURCE_DIR}/src/main/cpp
    ${SDL2_INCLUDE_DIRS}
)

# Add compile definitions
add_compile_definitions(
    CACHE_PATH="/data/data/com.scenetile.test/files/cache"
    ANDROID=1
    __ANDROID__=1
    SDL_VIDEO_DRIVER_ANDROID=1
    SDL_AUDIO_DRIVER_ANDROID=1
    SDL_JOYSTICK_ANDROID=1
    SDL_HAPTIC_ANDROID=1
    SDL_VIDEO_DRIVER_DUMMY=0

    # Benchmarked on ARMv7 Moto X this does give an improvement.
    ARM_NEON_FLOAT_DIV=1
)

# ARM-specific compiler flags
if(ANDROID_ABI STREQUAL "armeabi-v7a" OR ANDROID_ABI STREQUAL "arm64-v8a")
    add_compile_options(-fPIC)
    add_compile_options(-fno-strict-aliasing)
    add_compile_options(-fno-omit-frame-pointer)
endif()

# Source files for the Android build
set(ANDROID_SOURCES
    src/main/cpp/android_main.cpp
    src/main/cpp/android_platform.cpp
    ../src/shared_tables.c
    ../src/string_utils.c
    ../src/osrs/frustrum_cullmap.c
    ../src/osrs/bounds_cylinder.c
    ../src/osrs/tables/textures.c
    ../src/osrs/tables/sprites.c
    ../src/osrs/blend_underlays.c
    ../src/osrs/scene_tile.c
    ../src/osrs/palette.c
    ../src/osrs/cache.c
    ../src/osrs/world.c
    ../src/osrs/tables/model.c
    ../src/osrs/archive_decompress.c
    ../src/osrs/reference_table.c
    ../src/osrs/scene.c
    ../src/osrs/tables/config_npctype.c
    ../src/osrs/tables/config_sequence.c
    ../src/osrs/tables/config_floortype.c
    ../src/osrs/tables/config_locs.c
    ../src/osrs/tables/config_object.c
    ../src/osrs/tables/config_idk.c
    ../src/osrs/tables/framemap.c
    ../src/osrs/tables/frame.c
    ../src/osrs/tables/maps.c
    ../src/osrs/filelist.c
    ../src/graphics/render.c
    ../src/osrs/rsbuf.c
    ../src/graphics/gouraud_deob.c
    ../src/graphics/lighting.c
    ../src/osrs/anim.c
    ../src/osrs/model_transforms.c
    ../src/osrs/xtea.c
    ../src/osrs/archive.c
    ../src/osrs/xtea_config.c
    ../src/osrs/scene_cache.c
    ../src/datastruct/ht.c
    ../src/osrs/tables/noise.c
    ../src/imgui/imgui.cpp
    ../src/imgui/imgui_draw.cpp
    ../src/imgui/imgui_tables.cpp
    ../src/imgui/imgui_widgets.cpp
    ../src/imgui/imgui_demo.cpp
    ../src/imgui/imgui_impl_sdl2.cpp
    ../src/imgui/imgui_impl_sdlrenderer2.cpp
    ../src/3rd/bzip/bzip.c
    ../src/3rd/miniz/miniz.c
)

# Create the shared library
add_library(scene_tile_test_android SHARED ${ANDROID_SOURCES})

# Ensure SDL_main is properly exported
# target_compile_definitions(scene_tile_test_android PRIVATE
# SDL_MAIN_AVAILABLE=1
# )

# Export SDL_main symbol
target_link_options(scene_tile_test_android PRIVATE
    -Wl,--export-dynamic
)

# Link libraries
target_link_libraries(scene_tile_test_android
    ${SDL2_LIBRARIES}
    android
    log
    GLESv2
    EGL
    atomic
)

# Set target properties
set_target_properties(scene_tile_test_android PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Compiler flags for Android
target_compile_options(scene_tile_test_android PRIVATE
    -Wall
    -Wextra
)

# Linker flags for Android
# target_link_options(scene_tile_test_android PRIVATE
# -Wl,--gc-sections
# -Wl,--strip-all
# )
