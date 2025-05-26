cmake_minimum_required(VERSION 3.7)
project(RSClient
    LANGUAGES C
)

find_package(SDL2 REQUIRED)
find_package(BZip2 REQUIRED)
find_package(ZLIB REQUIRED)
include_directories(RSClient src ${SDL2_INCLUDE_DIRS} ${BZIP2_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS})

add_executable(main_rs
    src/main_rs.c
    src/osrs/cache.c
    src/osrs/tables/model.c
    src/buffer.c
    src/osrs/archive_decompress.c
    src/osrs/reference_table.c
    src/osrs/tables/config_npctype.c
    src/osrs/tables/config_sequence.c
    src/osrs/tables/framemap.c
    src/osrs/tables/frame.c
    src/osrs/filelist.c
)

add_executable(rs
    src/main.c
    src/projection.c
    src/gouraud.c
    src/texture.c
    src/lighting.c
    src/buffer.c
    src/osrs/tables/framemap.c
    src/osrs/tables/frame.c
    src/osrs/anim.c
    src/osrs/tables/config_npctype.c
    src/osrs/tables/config_sequence.c
    src/osrs/tables/model.c

    src/osrs_cache.c
    src/osrs/cache.c
    src/osrs/tables/model.c
    src/osrs/archive_decompress.c
    src/osrs/reference_table.c
)

add_executable(osrs_cache_test
    src/osrs_cache_test.c
    src/osrs_cache.c
    src/buffer.c
    src/osrs/xtea.c
    src/osrs/tables/config_sequence.c
    src/osrs/tables/frame.c
    src/osrs/tables/framemap.c
    src/osrs/tables/config_npctype.c
    src/osrs/tables/model.c
    src/osrs/cache.c
    src/osrs/tables/model.c
    src/osrs/archive_decompress.c
    src/osrs/reference_table.c
)

add_executable(texture_test
    src/texture_test.c
    src/texture.c
    src/buffer.c
)

target_link_libraries(main_rs ${SDL2_LIBRARIES} ${BZIP2_LIBRARIES} ${ZLIB_LIBRARIES})
target_link_libraries(rs ${SDL2_LIBRARIES} ${BZIP2_LIBRARIES} ${ZLIB_LIBRARIES})
target_link_libraries(texture_test ${SDL2_LIBRARIES})
target_link_libraries(osrs_cache_test ${SDL2_LIBRARIES} ${BZIP2_LIBRARIES} ${ZLIB_LIBRARIES})

cmake_minimum_required(VERSION 3.10)
project(metal_renderer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Check if we're on macOS
if(APPLE)
    # Set Objective-C++ as the language
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

    # Find required frameworks
    find_library(COCOA_LIBRARY Cocoa)
    find_library(METAL_LIBRARY Metal)
    find_library(METALKIT_LIBRARY MetalKit)
    find_library(FOUNDATION_LIBRARY Foundation)

    # Add source files
    set(SOURCES
        src/metal_main.mm
    )

    # Add shader files
    set(SHADERS
        src/Shaders.metal
    )

    # Create executable
    add_executable(metal_renderer ${SOURCES})

    # Link frameworks
    target_link_libraries(metal_renderer
        ${COCOA_LIBRARY}
        ${METAL_LIBRARY}
        ${METALKIT_LIBRARY}
        ${FOUNDATION_LIBRARY}
    )

    # Set compiler flags
    target_compile_options(metal_renderer PRIVATE
        -fobjc-arc
        -fmodules
    )

    # Set include directories
    target_include_directories(metal_renderer PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )

    # Metal shader compilation
    find_program(XCRUN xcrun)

    if(NOT XCRUN)
        message(FATAL_ERROR "xcrun not found. Make sure Xcode command line tools are installed.")
    endif()

    # Create a custom command to compile Metal shaders
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/Shaders.metallib
        COMMAND ${XCRUN} -sdk macosx metal -c ${CMAKE_SOURCE_DIR}/src/Shaders.metal -o ${CMAKE_BINARY_DIR}/Shaders.air
        COMMAND ${XCRUN} -sdk macosx metallib ${CMAKE_BINARY_DIR}/Shaders.air -o ${CMAKE_BINARY_DIR}/Shaders.metallib
        DEPENDS ${CMAKE_SOURCE_DIR}/src/Shaders.metal
        COMMENT "Compiling Metal shaders"
    )

    # Add the metallib as a dependency
    add_custom_target(metal_shaders DEPENDS ${CMAKE_BINARY_DIR}/Shaders.metallib)
    add_dependencies(metal_renderer metal_shaders)
else()
    # On non-macOS platforms, create an empty executable
    add_executable(metal_renderer)
    message(STATUS "Metal renderer is only supported on macOS. Creating empty executable.")
endif()