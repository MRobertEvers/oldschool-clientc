cmake_minimum_required(VERSION 3.10)
project(metal_renderer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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