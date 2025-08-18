cmake_minimum_required(VERSION 3.7)
project(RSClient
    LANGUAGES C
)

# set(PROFILING_FLAGS -g -fno-omit-frame-pointer)

# set(PROFILING_FLAGS -fsanitize-trap=all -fsanitize=undefined)
# set(PROFILING_FLAGS "-fsanitize=address -g -fno-omit-frame-pointer")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O3 -fsanitize=address")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# Add C flags for ASan
set(CMAKE_C_FLAGS "-Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "-g -Og -fsanitize=address")
set(CMAKE_C_FLAGS_RELEASE "-O3")

# Add sanitizer to linker flags for Debug mode
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address")

if(${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    set(USE_FLAGS "-s USE_SDL=2 -s USE_FREETYPE=1 -s USE_BZIP2=1 -s USE_ZLIB=1 -s INITIAL_MEMORY=268435456 -s ALLOW_MEMORY_GROWTH=1 -s EXPORTED_FUNCTIONS=['_main','_get_pixel_buffer','_get_screen_width','_get_screen_height','_render_frame','_set_camera_position','_set_camera_rotation','_set_camera_fov'] -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU32'] -s NO_EXIT_RUNTIME=1")
    set(PRELOAD_FLAGS "--preload-file ../cache@/cache")
    set(DATA_FILE_PATH "build/scene_tile_test_browser.data")
    add_compile_definitions(CACHE_PATH="/cache")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${USE_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${USE_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${USE_FLAGS} ${PRELOAD_FLAGS}")
    set(CMAKE_EXECUTABLE_SUFFIX .html)
else()
    find_package(SDL2 REQUIRED)
    find_package(Freetype REQUIRED)
    find_package(BZip2 REQUIRED)
    find_package(ZLIB REQUIRED)
    add_compile_definitions(CACHE_PATH="../cache")
endif()

# Find OpenGL
if(APPLE)
    find_library(OPENGL_LIBRARY OpenGL)
    find_library(GLUT_LIBRARY GLUT)
else()
    find_package(OpenGL REQUIRED)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    include_directories(RSClient src src/imgui)
else()
    include_directories(RSClient src ${SDL2_INCLUDE_DIRS} ${BZIP2_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS} src/imgui)
endif()

add_executable(main_client
    src/main_client.c
    src/osrs/frustrum_cullmap.c
    src/texture.c
    src/osrs/tables/textures.c
    src/osrs/tables/sprites.c
    src/osrs/blend_underlays.c
    src/osrs/palette.c
    src/osrs/cache.c
    src/osrs/scene_tile.c
    src/osrs/tables/model.c
    src/osrs/archive_decompress.c
    src/osrs/reference_table.c
    src/osrs/tables/config_npctype.c
    src/osrs/tables/config_sequence.c
    src/osrs/tables/config_floortype.c
    src/osrs/tables/framemap.c
    src/osrs/tables/frame.c
    src/osrs/tables/maps.c
    src/osrs/filelist.c
    src/osrs/render.c
    src/osrs/rsbuf.c
    src/flat.c
    src/gouraud.c
    src/gouraud_deob.c
    src/lighting.c
    src/projection.c
    src/texture.c
    src/osrs/anim.c
    src/osrs/xtea.c
    src/osrs/archive.c
    src/osrs/xtea_config.c
    src/osrs/scene_cache.c
    src/datastruct/ht.c
    src/osrs/tables/noise.c
)

add_executable(scene_tile_test
    test/scene_tile_test.cpp
    src/osrs/frustrum_cullmap.c
    src/osrs/tables/textures.c
    src/osrs/tables/sprites.c
    src/osrs/blend_underlays.c
    src/osrs/scene_tile.c
    src/osrs/palette.c
    src/osrs/cache.c
    src/osrs/tables/model.c
    src/osrs/archive_decompress.c
    src/osrs/reference_table.c
    src/osrs/scene.c
    src/osrs/tables/config_npctype.c
    src/osrs/tables/config_sequence.c
    src/osrs/tables/config_floortype.c
    src/osrs/tables/config_locs.c
    src/osrs/tables/config_object.c
    src/osrs/tables/config_idk.c
    src/osrs/tables/framemap.c
    src/osrs/tables/frame.c
    src/osrs/tables/maps.c
    src/osrs/filelist.c
    src/osrs/render.c
    src/osrs/rsbuf.c
    src/gouraud.c
    src/gouraud_deob.c
    src/flat.c
    src/lighting.c
    src/projection.c
    src/texture.c
    src/osrs/anim.c
    src/osrs/game_model.c
    src/osrs/xtea.c
    src/osrs/archive.c
    src/osrs/xtea_config.c
    src/osrs/scene_cache.c
    src/osrs/tables/noise.c
    src/datastruct/ht.c

    src/imgui/imgui.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/imgui_widgets.cpp
    src/imgui/imgui_demo.cpp
    src/imgui/imgui_impl_sdl2.cpp
    src/imgui/imgui_impl_sdlrenderer2.cpp
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    add_executable(scene_tile_test_browser
        test/scene_tile_test_browser.c
        src/osrs/tables/textures.c
        src/osrs/tables/sprites.c
        src/osrs/blend_underlays.c
        src/osrs/scene_tile.c
        src/osrs/palette.c
        src/osrs/cache.c
        src/osrs/tables/model.c
        src/osrs/archive_decompress.c
        src/osrs/reference_table.c
        src/osrs/scene.c
        src/osrs/tables/config_npctype.c
        src/osrs/tables/config_sequence.c
        src/osrs/tables/config_floortype.c
        src/osrs/tables/config_locs.c
        src/osrs/tables/framemap.c
        src/osrs/tables/frame.c
        src/osrs/tables/maps.c
        src/osrs/filelist.c
        src/osrs/render.c
        src/osrs/rsbuf.c
        src/gouraud.c
        src/gouraud_deob.c
        src/flat.c
        src/lighting.c
        src/projection.c
        src/texture.c
        src/osrs/anim.c
        src/osrs/game_model.c
        src/osrs/xtea.c
        src/osrs/archive.c
        src/osrs/xtea_config.c
        src/osrs/scene_cache.c
        src/datastruct/ht.c
        src/osrs/tables/noise.c
    )
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    target_link_libraries(scene_tile_test)
else()
    target_link_libraries(scene_tile_test ${SDL2_LIBRARIES} ${BZIP2_LIBRARIES} ${ZLIB_LIBRARIES})
endif()

# Model viewer with ImGui
add_executable(model_viewer
    test/model_viewer.cpp
    src/imgui/imgui.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/imgui_widgets.cpp
    src/imgui/imgui_demo.cpp
    src/imgui/imgui_impl_sdl2.cpp
    src/imgui/imgui_impl_sdlrenderer2.cpp
)

# Set C++ standard for model viewer
set_target_properties(model_viewer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

set_target_properties(scene_tile_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(UNIX AND NOT APPLE)
    set(EXTRA_LIBS m)
else()
    set(EXTRA_LIBS)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    target_link_libraries(main_client ${EXTRA_LIBS})
    target_link_libraries(scene_tile_test ${EXTRA_LIBS})
    target_link_libraries(scene_tile_test_browser ${EXTRA_LIBS})
    target_link_libraries(model_viewer ${EXTRA_LIBS})

    # Add preload flags to all targets
    set_target_properties(main_client PROPERTIES LINK_FLAGS "${PRELOAD_FLAGS}")
    set_target_properties(scene_tile_test PROPERTIES LINK_FLAGS "${PRELOAD_FLAGS}")
    set_target_properties(scene_tile_test_browser PROPERTIES LINK_FLAGS "${PRELOAD_FLAGS}")
    set_target_properties(model_viewer PROPERTIES LINK_FLAGS "${PRELOAD_FLAGS}")
else()
    target_link_libraries(main_client ${SDL2_LIBRARIES} ${BZIP2_LIBRARIES} ${ZLIB_LIBRARIES} ${EXTRA_LIBS})
    target_link_libraries(scene_tile_test ${SDL2_LIBRARIES} ${BZIP2_LIBRARIES} ${ZLIB_LIBRARIES} ${EXTRA_LIBS})
    target_link_libraries(model_viewer ${SDL2_LIBRARIES} ${EXTRA_LIBS})
endif()

cmake_minimum_required(VERSION 3.10)
project(metal_renderer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Check if we're on macOS
if(APPLE)
    # set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${PROFILING_FLAGS}")

    # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${PROFILING_FLAGS}")

    # Set Objective-C++ as the language
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

    # Find required frameworks
    find_library(COCOA_LIBRARY Cocoa)
    find_library(METAL_LIBRARY Metal)
    find_library(METALKIT_LIBRARY MetalKit)
    find_library(FOUNDATION_LIBRARY Foundation)

    # Find required libraries
    find_package(BZip2 REQUIRED)
    find_package(ZLIB REQUIRED)

    # Add source files
    set(SOURCES
        src/metal_main.mm
        src/osrs/frustrum_cullmap.c
        src/osrs/tables/textures.c
        src/osrs/tables/sprites.c
        src/osrs/blend_underlays.c
        src/osrs/scene_tile.c
        src/osrs/palette.c
        src/osrs/cache.c
        src/osrs/tables/model.c
        src/osrs/archive_decompress.c
        src/osrs/reference_table.c
        src/osrs/scene.c
        src/osrs/tables/config_npctype.c
        src/osrs/tables/config_sequence.c
        src/osrs/tables/config_floortype.c
        src/osrs/tables/config_locs.c
        src/osrs/tables/config_object.c
        src/osrs/tables/config_idk.c
        src/osrs/tables/framemap.c
        src/osrs/tables/frame.c
        src/osrs/tables/maps.c
        src/osrs/filelist.c
        src/osrs/render.c
        src/osrs/rsbuf.c
        src/gouraud.c
        src/gouraud_deob.c
        src/flat.c
        src/lighting.c
        src/projection.c
        src/texture.c
        src/osrs/anim.c
        src/osrs/game_model.c
        src/osrs/xtea.c
        src/osrs/archive.c
        src/osrs/xtea_config.c
        src/osrs/scene_cache.c
        src/osrs/tables/noise.c
        src/datastruct/ht.c
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
        ${BZIP2_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${EXTRA_LIBS}
    )

    # Set compiler flags
    target_compile_options(metal_renderer PRIVATE
        -fobjc-arc
        -fmodules
    )

    # Set include directories
    target_include_directories(metal_renderer PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${BZIP2_INCLUDE_DIRS}
        ${ZLIB_INCLUDE_DIRS}
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

    target_compile_options(main_client PRIVATE ${PROFILING_FLAGS})
    target_compile_options(scene_tile_test PRIVATE ${PROFILING_FLAGS})
else()
    # On non-macOS platforms, create an empty executable
    add_executable(metal_renderer src/dummy.c)
    message(STATUS "Metal renderer is only supported on macOS. Creating empty executable.")
endif()