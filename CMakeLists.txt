
cmake_minimum_required(VERSION 3.7)
project(RSClient
    LANGUAGES C CXX
)

# set(PROFILING_FLAGS -g -fno-omit-frame-pointer)

# set(PROFILING_FLAGS -fsanitize-trap=all -fsanitize=undefined)
# set(PROFILING_FLAGS "-fsanitize=address -g -fno-omit-frame-pointer")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Fix: Use C flags instead of C++ flags for C project
if(WIN32 AND MSVC)
    # Windows MSVC-specific compiler flags
    set(CMAKE_C_FLAGS "/W4 /wd4100") # /W4 for more warnings, /wd4100 to disable unused parameter warning
    set(CMAKE_CXX_FLAGS "/W4 /wd4100") # Same for C++
    set(CMAKE_C_FLAGS_DEBUG "/Zi /Od /MDd")
    set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Od /MDd")
    
    # Enhanced Release flags for better branching performance
    set(CMAKE_C_FLAGS_RELEASE "/O2 /Ob2 /Ot /Oi /Oy /GL /MD /DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /Ot /Oi /Oy /GL /MD /DNDEBUG")
    
    # Enable link-time code generation for Release builds
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG")
    
    # Profile-Guided Optimization configurations
    # For PGO instrumentation build (first step):
    # set(CMAKE_C_FLAGS_RELEASE_PGI "/O2 /Ob2 /Ot /Oi /Oy /GL /fp:fast /Qpar /arch:AVX2 /MD")
    # set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG:PGINSTRUMENT")
    
    # For PGO optimized build (second step):
    # set(CMAKE_C_FLAGS_RELEASE_PGO "/O2 /Ob2 /Ot /Oi /Oy /GL /fp:fast /Qpar /arch:AVX2 /MD")
    # set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG:PGOPTIMIZE")
    
    # Enable C99 features
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_STANDARD_REQUIRED ON)
else()
    # GCC/MinGW and other compilers
    set(CMAKE_C_FLAGS "-Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration -Werror=return-type")
    set(CMAKE_C_FLAGS_DEBUG "-g -O0 -fsanitize=address -fno-omit-frame-pointer")
    
    set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unused-parameter -Werror=return-type")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address -fno-omit-frame-pointer")
    
    # Set architecture-specific flags
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        # x86_64 specific optimizations
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -g -O3 -DNDEBUG")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g -O3 -DNDEBUG")
    else()
        # ARM64 and other architectures - use generic optimizations
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -g -O3 -DNDEBUG")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g -O3 -DNDEBUG")
    endif()
    
    # Enable C99 features
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_STANDARD_REQUIRED ON)
endif()

# Remove duplicate C flags section
# set(CMAKE_C_FLAGS "-Wall -Wextra")
# set(CMAKE_C_FLAGS_DEBUG "-g -fno-omit-frame-pointer -fsanitize=address")
# set(CMAKE_C_FLAGS_RELEASE "-O3")

# Fix: Properly configure ASan for Debug mode (Unix/Linux only, excluding Emscripten)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT ${CMAKE_SYSTEM_NAME} MATCHES "Emscripten" AND NOT WIN32)
    # Set ASan environment options
    set(ENV{ASAN_OPTIONS} "detect_leaks=1:abort_on_error=1:check_initialization_order=1:strict_init_order=1")
    
    # Add ASan compiler flags
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer")
    
    # Ensure ASan is linked first by setting proper linker flags
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=address ${CMAKE_EXE_LINKER_FLAGS_DEBUG}")
    set(CMAKE_MODULE_LINKER_FLAGS_DEBUG "-fsanitize=address ${CMAKE_MODULE_LINKER_FLAGS_DEBUG}")
    set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "-fsanitize=address ${CMAKE_SHARED_LINKER_FLAGS_DEBUG}")
    
    # Add runtime path for ASan
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -Wl,-rpath,\$ORIGIN")
    # set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -Wl,--enable-new-dtags")
endif()

# Remove the old ASan configuration that was causing issues
# if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT ${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
#     # Set environment variable to ensure ASan runtime is loaded first
#     set(ENV{ASAN_OPTIONS} "detect_leaks=1:abort_on_error=1:check_initialization_order=1:strict_init_order=1")
#     
#     # Add ASan-specific compiler flags
#     set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-common -fno-builtin")
#     
#     # Ensure ASan is linked first
#     set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -Wl,--whole-archive -fsanitize=address -Wl,--no-whole-archive")
# endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    # Override ASan flags for Emscripten - ASan is not supported
    set(CMAKE_C_FLAGS_DEBUG "-g -O0")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "")
    set(CMAKE_MODULE_LINKER_FLAGS_DEBUG "")
    set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "")
    
    # Remove debug info from Release builds to avoid binaryen optimization warning
    # Enable WASM SIMD for better performance
    set(CMAKE_C_FLAGS_RELEASE "-O3 -D__ARM_NEON -DNDEBUG -msimd128")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -D__ARM_NEON -DNDEBUG -msimd128")
    
    set(COMPILE_FLAGS "-s USE_SDL=2 -s USE_FREETYPE=1 -s -s USE_WEBGL2=1 -msimd128")
    set(LINK_FLAGS "-s USE_SDL=2 -s USE_FREETYPE=1 -s -s INITIAL_MEMORY=268435456 -s ALLOW_MEMORY_GROWTH=1 -s NO_EXIT_RUNTIME=1 -s USE_WEBGL2=1 -s MAX_WEBGL_VERSION=2 -s MIN_WEBGL_VERSION=2 -s FULL_ES3=1 -msimd128")
    set(PRELOAD_FLAGS "--preload-file ../cache@/cache")
    set(DATA_FILE_PATH "build/scene_tile_test_browser.data")
    add_compile_definitions(CACHE_PATH="/cache")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMPILE_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMPILE_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LINK_FLAGS} ${PRELOAD_FLAGS}")
    set(CMAKE_EXECUTABLE_SUFFIX .html)
    
    # Add SDL2 include path for Emscripten
    include_directories(${EMSCRIPTEN_ROOT_PATH}/cache/sysroot/include)
elseif(WIN32)
    set(CMAKE_EXECUTABLE_SUFFIX .exe)
    
    # Detect MinGW compiler and configure for static linking
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND WIN32)
        set(IS_MINGW TRUE)
        # Force static linking for MinGW
        set(SDL2_STATIC ON)
        set(SDL2_SHARED OFF)
        find_package(SDL2 REQUIRED)
        find_package(Freetype REQUIRED)
    else()
        find_package(SDL2 REQUIRED)
        find_package(Freetype REQUIRED)
    endif()
    
    add_compile_definitions(CACHE_PATH="../cache")
else()
    find_package(SDL2 REQUIRED)
    find_package(Freetype REQUIRED)
    add_compile_definitions(CACHE_PATH="../cache")
endif()

# Find OpenGL
if(APPLE)
    find_library(OPENGL_LIBRARY OpenGL)
    find_library(GLUT_LIBRARY GLUT)
elseif(WIN32)
    # Windows OpenGL configuration
    find_package(OpenGL REQUIRED)
    # Windows-specific libraries
    set(WINDOWS_LIBS opengl32)
else()
    find_package(OpenGL REQUIRED)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    include_directories(RSClient src src/imgui)
elseif(WIN32)
    include_directories(RSClient src src/imgui)
else()
    include_directories(RSClient src ${SDL2_INCLUDE_DIRS} src/imgui)
endif()

if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    add_executable(scene_tile_test
        test/scene_tile_test.cpp
        src/shared_tables.c
        src/string_utils.c
        src/osrs/frustrum_cullmap.c
        src/osrs/bounds_cylinder.c
        src/osrs/tables/textures.c
        src/osrs/tables/sprites.c
        src/osrs/blend_underlays.c
        src/osrs/scene_tile.c
        src/osrs/palette.c
        src/osrs/cache.c
        src/osrs/cache_inet.c
        src/osrs/disk.c
        src/osrs/world.c
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
        src/graphics/render.c
        src/osrs/rsbuf.c
        src/graphics/gouraud_deob.c
        src/graphics/lighting.c
        src/osrs/anim.c
        src/osrs/model_transforms.c
        src/osrs/xtea.c
        src/osrs/archive.c
        src/osrs/xtea_config.c
        src/osrs/scene_cache.c
        src/datastruct/ht.c
        src/osrs/tables/noise.c

        src/imgui/imgui.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp
        src/imgui/imgui_demo.cpp
        src/imgui/imgui_impl_sdl2.cpp
        src/imgui/imgui_impl_sdlrenderer2.cpp

        src/3rd/bzip/bzip.c
        src/3rd/miniz/miniz.c
    )
endif()

# Model viewer with ImGui
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    add_executable(model_viewer
        test/model_viewer.cpp
        src/shared_tables.c
        src/string_utils.c
        src/osrs/frustrum_cullmap.c
        src/osrs/bounds_cylinder.c
        src/osrs/tables/textures.c
        src/osrs/tables/sprites.c
        src/osrs/blend_underlays.c
        src/osrs/scene_tile.c
        src/osrs/palette.c
        src/osrs/cache.c
        src/osrs/cache_inet.c
        src/osrs/disk.c
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
        src/graphics/render.c
        src/osrs/rsbuf.c
        src/graphics/lighting.c
        src/osrs/anim.c
        src/osrs/model_transforms.c
        src/osrs/xtea.c
        src/osrs/archive.c
        src/osrs/xtea_config.c
        src/osrs/scene_cache.c
        src/datastruct/ht.c
        src/osrs/tables/noise.c

        src/imgui/imgui.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp
        src/imgui/imgui_demo.cpp
        src/imgui/imgui_impl_sdl2.cpp
        src/imgui/imgui_impl_sdlrenderer2.cpp

        src/3rd/bzip/bzip.c
        src/3rd/miniz/miniz.c
    )
endif()

# Model viewer with ImGui
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    add_executable(model_viewerfx
        test/model_viewerfx.cpp
        src/shared_tables.c
        src/string_utils.c
        src/osrs/frustrum_cullmap.c
        src/osrs/bounds_cylinder.c
        src/osrs/tables/textures.c
        src/osrs/tables/sprites.c
        src/osrs/blend_underlays.c
        src/osrs/scene_tile.c
        src/osrs/palette.c
        src/osrs/cache.c
        src/osrs/cache_inet.c
        src/osrs/disk.c
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
        src/graphics/render.c
        src/osrs/rsbuf.c
        src/graphics/lighting.c
        src/osrs/anim.c
        src/osrs/model_transforms.c
        src/osrs/xtea.c
        src/osrs/archive.c
        src/osrs/xtea_config.c
        src/osrs/scene_cache.c
        src/datastruct/ht.c
        src/osrs/tables/noise.c

        src/imgui/imgui.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp
        src/imgui/imgui_demo.cpp
        src/imgui/imgui_impl_sdl2.cpp
        src/imgui/imgui_impl_opengl3.cpp

        src/3rd/bzip/bzip.c
        src/3rd/miniz/miniz.c

        src/osrs/pix3dgl.cpp
    )
endif()

# Asset server (Linux and macOS only)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Emscripten" AND NOT WIN32)
    add_executable(asset_server
        test/asset_server.c
        src/osrs/cache.c
        src/osrs/cache_inet.c
        src/osrs/disk.c
        src/osrs/archive.c
        src/osrs/archive_decompress.c
        src/osrs/reference_table.c
        src/osrs/rsbuf.c
        src/osrs/xtea.c
        src/osrs/xtea_config.c
        src/3rd/bzip/bzip.c
        src/3rd/miniz/miniz.c
    )
    
    # Link pthread
    find_package(Threads REQUIRED)
    target_link_libraries(asset_server Threads::Threads)
    
    # Link math library on Linux
    if(UNIX AND NOT APPLE)
        target_link_libraries(asset_server m)
    endif()
endif()

# OSX SDL2 Game executable (macOS only)
if(APPLE AND NOT ${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    add_executable(osx_sdl2_game
        test/osx_sdl2.cpp
        src/libgame.c
        src/libinput.c
        src/platforms/platform_impl_osx_sdl2.cpp
        src/platforms/platform_impl_osx_sdl2_renderer_soft3d.cpp
        src/platforms/platform_impl_osx_sdl2_renderer_opengl3.cpp
        
        src/shared_tables.c
        src/string_utils.c
        src/osrs/frustrum_cullmap.c
        src/osrs/bounds_cylinder.c
        src/osrs/tables/textures.c
        src/osrs/tables/sprites.c
        src/osrs/blend_underlays.c
        src/osrs/scene_tile.c
        src/osrs/palette.c
        src/osrs/cache.c
        src/osrs/disk.c
        src/osrs/cache_inet.c
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
        src/graphics/render.c
        src/osrs/rsbuf.c
        src/graphics/lighting.c
        src/osrs/anim.c
        src/osrs/model_transforms.c
        src/osrs/xtea.c
        src/osrs/archive.c
        src/osrs/xtea_config.c
        src/osrs/scene_cache.c
        src/datastruct/ht.c
        src/osrs/tables/noise.c
        src/osrs/pix3dgl.cpp


        src/imgui/imgui.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp
        src/imgui/imgui_demo.cpp
        src/imgui/imgui_impl_sdl2.cpp
        src/imgui/imgui_impl_sdlrenderer2.cpp
        src/imgui/imgui_impl_opengl3.cpp

        src/3rd/bzip/bzip.c
        src/3rd/miniz/miniz.c
    )
endif()

# Add SDL2 include directories for Windows
if(WIN32)
    target_include_directories(scene_tile_test PRIVATE ${SDL2_INCLUDE_DIRS})
    target_include_directories(model_viewer PRIVATE ${SDL2_INCLUDE_DIRS})
    target_include_directories(model_viewerfx PRIVATE ${SDL2_INCLUDE_DIRS})
endif()

# Add SDL2 include directories for osx_sdl2_game
if(APPLE AND NOT ${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    target_include_directories(osx_sdl2_game PRIVATE ${SDL2_INCLUDE_DIRS} ${OPENGL_INCLUDE_DIRS})
endif()

# ImGui and other files that use sqrt()/pow() require at least SSE support for return values,
# even in scalar builds. This applies per-source compile flags to override global -mno-sse.
# The scalar build is meant to benchmark projection code without SIMD, not break standard library functions.
if(NOT WIN32 AND NOT ${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
   
endif()

# Fix: Add ASan target properties for scene_tile_test (Unix/Linux only, excluding Emscripten)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT ${CMAKE_SYSTEM_NAME} MATCHES "Emscripten" AND NOT WIN32)
    set_target_properties(scene_tile_test PROPERTIES
        LINK_FLAGS "-fsanitize=address"
    )
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    # ImGui browser version with SDL2 + ImGui support
    add_executable(scene_tile_test_imgui_browser
        test/scene_tile_test_imgui_browser.cpp
        src/shared_tables.c
        src/string_utils.c
        src/osrs/frustrum_cullmap.c
        src/osrs/bounds_cylinder.c
        src/osrs/tables/textures.c
        src/osrs/tables/sprites.c
        src/osrs/blend_underlays.c
        src/osrs/scene_tile.c
        src/osrs/palette.c
        src/osrs/cache.c
        src/osrs/cache_inet.c
        src/osrs/disk.c
        src/osrs/world.c
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
        src/graphics/render.c
        src/osrs/rsbuf.c
        src/graphics/gouraud_deob.c
        src/graphics/lighting.c
        src/osrs/anim.c
        src/osrs/model_transforms.c
        src/osrs/xtea.c
        src/osrs/archive.c
        src/osrs/xtea_config.c
        src/osrs/scene_cache.c
        src/datastruct/ht.c
        src/osrs/tables/noise.c

        src/imgui/imgui.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp
        src/imgui/imgui_demo.cpp
        src/imgui/imgui_impl_sdl2.cpp
        src/imgui/imgui_impl_sdlrenderer2.cpp

        src/3rd/bzip/bzip.c
        src/3rd/miniz/miniz.c
    )

    add_executable(main
        src/main.cpp
        src/shared_tables.c
        src/string_utils.c
        src/osrs/frustrum_cullmap.c
        src/osrs/bounds_cylinder.c
        src/osrs/tables/textures.c
        src/osrs/tables/sprites.c
        src/osrs/blend_underlays.c
        src/osrs/scene_tile.c
        src/osrs/palette.c
        src/osrs/cache.c
        src/osrs/cache_inet.c
        src/osrs/disk.c
        src/osrs/world.c
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
        src/graphics/render.c
        src/osrs/rsbuf.c
        src/graphics/gouraud_deob.c
        src/graphics/lighting.c
        src/osrs/anim.c
        src/osrs/model_transforms.c
        src/osrs/xtea.c
        src/osrs/archive.c
        src/osrs/xtea_config.c
        src/osrs/scene_cache.c
        src/datastruct/ht.c
        src/osrs/tables/noise.c

        src/imgui/imgui.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp
        src/imgui/imgui_demo.cpp
        src/imgui/imgui_impl_sdl2.cpp
        src/imgui/imgui_impl_sdlrenderer2.cpp
        src/imgui/imgui_impl_sdlrenderer2.cpp

        src/3rd/bzip/bzip.c
        src/3rd/miniz/miniz.c
    )

    # Set C++ standard for ImGui browser target
    set_target_properties(scene_tile_test_imgui_browser PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )

    set_target_properties(main PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )

    # Add custom command to copy main files to public/build
    add_custom_command(TARGET main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/public/build"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/main.html"
            "${CMAKE_BINARY_DIR}/main.js"
            "${CMAKE_BINARY_DIR}/main.wasm"
            "${CMAKE_BINARY_DIR}/main.data"
            "${CMAKE_SOURCE_DIR}/public/build/"
        COMMENT "Copying main Emscripten build files to public/build directory"
    )

    # Model viewer FX for browser with OpenGL ES
    add_executable(model_viewerfx_browser
        test/model_viewerfx.cpp
        src/shared_tables.c
        src/string_utils.c
        src/osrs/frustrum_cullmap.c
        src/osrs/bounds_cylinder.c
        src/osrs/tables/textures.c
        src/osrs/tables/sprites.c
        src/osrs/blend_underlays.c
        src/osrs/scene_tile.c
        src/osrs/palette.c
        src/osrs/cache.c
        src/osrs/cache_inet.c
        src/osrs/disk.c
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
        src/graphics/render.c
        src/osrs/rsbuf.c
        src/graphics/lighting.c
        src/osrs/anim.c
        src/osrs/model_transforms.c
        src/osrs/xtea.c
        src/osrs/archive.c
        src/osrs/xtea_config.c
        src/osrs/scene_cache.c
        src/datastruct/ht.c
        src/osrs/tables/noise.c

        src/imgui/imgui.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp
        src/imgui/imgui_demo.cpp
        src/imgui/imgui_impl_sdl2.cpp
        src/imgui/imgui_impl_opengl3.cpp

        src/3rd/bzip/bzip.c
        src/3rd/miniz/miniz.c

        src/osrs/pix3dgl.cpp
    )

    set_target_properties(model_viewerfx_browser PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )

    # Emscripten SDL2 WebGL1 game executable
    add_executable(emscripten_sdl2_webgl1
        test/emscripten_sdl2_webgl1.cpp
        src/libgame.c
        src/libinput.c
        src/platforms/platform_impl_emscripten_sdl2.cpp
        src/platforms/platform_impl_emscripten_sdl2_renderer_webgl1.cpp
        
        src/shared_tables.c
        src/string_utils.c
        src/osrs/frustrum_cullmap.c
        src/osrs/bounds_cylinder.c
        src/osrs/tables/textures.c
        src/osrs/tables/sprites.c
        src/osrs/blend_underlays.c
        src/osrs/scene_tile.c
        src/osrs/palette.c
        src/osrs/cache.c
        src/osrs/cache_inet.c
        src/osrs/disk.c
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
        src/graphics/render.c
        src/osrs/rsbuf.c
        src/graphics/lighting.c
        src/osrs/anim.c
        src/osrs/model_transforms.c
        src/osrs/xtea.c
        src/osrs/archive.c
        src/osrs/xtea_config.c
        src/osrs/scene_cache.c
        src/datastruct/ht.c
        src/osrs/tables/noise.c
        src/osrs/pix3dgl_opengles2.cpp

        src/imgui/imgui.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp
        src/imgui/imgui_demo.cpp
        src/imgui/imgui_impl_sdl2.cpp
        src/imgui/imgui_impl_opengl3.cpp

        src/3rd/bzip/bzip.c
        src/3rd/miniz/miniz.c
    )

    set_target_properties(emscripten_sdl2_webgl1 PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )

    # Override global compile flags for WebGL1
    target_compile_options(emscripten_sdl2_webgl1 PRIVATE 
        "SHELL:-s USE_WEBGL2=0" 
        "SHELL:-s MAX_WEBGL_VERSION=1" 
        "SHELL:-s MIN_WEBGL_VERSION=1" 
        "SHELL:-s FULL_ES2=1"
    )

    target_link_libraries(emscripten_sdl2_webgl1 ${EXTRA_LIBS})
    # WebGL1-specific link flags - later flags override earlier ones in Emscripten
    set_target_properties(emscripten_sdl2_webgl1 PROPERTIES 
        LINK_FLAGS "${LINK_FLAGS} ${PRELOAD_FLAGS} -s USE_WEBGL2=0 -s MAX_WEBGL_VERSION=1 -s MIN_WEBGL_VERSION=1 -s FULL_ES2=1 -s MODULARIZE=0 -s EXPORT_ES6=0 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s NO_EXIT_RUNTIME=1 -s NO_DISABLE_EXCEPTION_CATCHING -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU32'] --shell-file ${CMAKE_SOURCE_DIR}/public/shell.html"
    )

endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
elseif(WIN32)
    target_link_libraries(scene_tile_test SDL2::SDL2 ${WINDOWS_LIBS})
    target_link_libraries(model_viewer SDL2::SDL2 ${WINDOWS_LIBS})
    target_link_libraries(model_viewerfx SDL2::SDL2 ${WINDOWS_LIBS} ${OPENGL_LIBRARIES})
else()
    target_link_libraries(scene_tile_test ${SDL2_LIBRARIES})
    target_link_libraries(model_viewer ${SDL2_LIBRARIES})
    target_link_libraries(model_viewerfx ${SDL2_LIBRARIES} ${OPENGL_LIBRARIES})

endif()


# Set C++ standard for model viewer (only on non-Emscripten platforms)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    set_target_properties(model_viewer PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )

    # Fix: Add ASan target properties for model_viewer (Unix/Linux only, excluding Emscripten)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT WIN32)
        # set_target_properties(model_viewer PROPERTIES
        #     LINK_FLAGS "-fsanitize=address"
        # )
    endif()

    set_target_properties(scene_tile_test PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )

    set_target_properties(model_viewerfx PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    
    # Set C++ standard for osx_sdl2_game (macOS only)
    if(APPLE)
        set_target_properties(osx_sdl2_game PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
        )
    endif()
endif()


if(UNIX AND NOT APPLE)
    set(EXTRA_LIBS m)
else()
    set(EXTRA_LIBS)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    target_link_libraries(scene_tile_test_imgui_browser ${EXTRA_LIBS})
    set_target_properties(scene_tile_test_imgui_browser PROPERTIES LINK_FLAGS "${LINK_FLAGS} ${PRELOAD_FLAGS} -s EXPORTED_FUNCTIONS=['_main','_get_pixel_buffer','_get_screen_width','_get_screen_height','_render_frame','_set_camera_position','_set_camera_rotation','_set_camera_fov'] -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU32']")

    target_link_libraries(main ${EXTRA_LIBS})
    set_target_properties(main PROPERTIES LINK_FLAGS "${LINK_FLAGS} ${PRELOAD_FLAGS} -s USE_WEBGL2=1 -s MAX_WEBGL_VERSION=2 -s MIN_WEBGL_VERSION=1 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s NO_EXIT_RUNTIME=1 -s NO_DISABLE_EXCEPTION_CATCHING -s FULL_ES3=1 -s USE_SDL=2 -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU32'] --shell-file ${CMAKE_SOURCE_DIR}/public/shell.html")

    target_link_libraries(model_viewerfx_browser ${EXTRA_LIBS})
    set_target_properties(model_viewerfx_browser PROPERTIES LINK_FLAGS "${LINK_FLAGS} ${PRELOAD_FLAGS} -s USE_WEBGL2=0 -s MAX_WEBGL_VERSION=1 -s MODULARIZE=0 -s MIN_WEBGL_VERSION=1 -s FULL_ES2=1 -s EXPORT_ES6=0 -s WASM=1 -s -s ALLOW_MEMORY_GROWTH=1 -s NO_EXIT_RUNTIME=1 -s NO_DISABLE_EXCEPTION_CATCHING -s USE_SDL=2 -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU32'] --shell-file ${CMAKE_SOURCE_DIR}/public/shell.html")
elseif(WIN32)
    target_link_libraries(scene_tile_test SDL2::SDL2 ${WINDOWS_LIBS} ${EXTRA_LIBS})
    target_link_libraries(model_viewer SDL2::SDL2 ${WINDOWS_LIBS} ${EXTRA_LIBS})
    target_link_libraries(model_viewerfx SDL2::SDL2 ${WINDOWS_LIBS} ${OPENGL_LIBRARIES} ${EXTRA_LIBS})
    
    # Detect MinGW compiler
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND WIN32)
        set(IS_MINGW TRUE)
        # Add static linking flags for MinGW
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    else()
        set(IS_MINGW FALSE)
    endif()
    
    # Function to copy DLLs for a target
    function(copy_dlls_to_target target_name)
        if(IS_MINGW)
            # MinGW-specific DLL copying
            # Find MinGW installation directory
            get_filename_component(MINGW_BIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
            
            # Copy MinGW runtime DLLs
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/libwinpthread-1.dll"
                "${CMAKE_CURRENT_BINARY_DIR}/"
                COMMENT "Copying MinGW runtime DLLs for ${target_name}"
            )
            
            # These are only necessary for dynamic linking.
            # # Also copy other common MinGW runtime DLLs if they exist
            # add_custom_command(TARGET ${target_name} POST_BUILD
            #     COMMAND ${CMAKE_COMMAND} -E copy_if_different
            #     "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
            #     "${CMAKE_CURRENT_BINARY_DIR}/"
            #     COMMENT "Copying libgcc_s_seh-1.dll for ${target_name}"
            # )
            
            # add_custom_command(TARGET ${target_name} POST_BUILD
            #     COMMAND ${CMAKE_COMMAND} -E copy_if_different
            #     "${MINGW_BIN_DIR}/libstdc++-6.dll"
            #     "${CMAKE_CURRENT_BINARY_DIR}/"
            #     COMMENT "Copying libstdc++-6.dll for ${target_name}"
            # )
            
            # Copy SDL2.dll from lib directory if it exists (only for dynamic linking)
            if(NOT SDL2_STATIC AND EXISTS "${CMAKE_SOURCE_DIR}/lib/SDL2.dll")
                add_custom_command(TARGET ${target_name} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_SOURCE_DIR}/lib/SDL2.dll"
                    "${CMAKE_CURRENT_BINARY_DIR}/"
                    COMMENT "Copying SDL2.dll for ${target_name}"
                )
            endif()
        else()
            # MSVC-specific DLL copying (existing logic)
            # Use relative path from project root to vcpkg
            set(VCPKG_BIN_DIR "${CMAKE_SOURCE_DIR}/vcpkg/installed/x64-windows/bin")
            set(VCPKG_DEBUG_BIN_DIR "${CMAKE_SOURCE_DIR}/vcpkg/installed/x64-windows/debug/bin")
            
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                # Copy debug DLLs to Debug directory
                add_custom_command(TARGET ${target_name} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Debug"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${VCPKG_DEBUG_BIN_DIR}/SDL2d.dll"
                    "${CMAKE_CURRENT_BINARY_DIR}/Debug/"
                    COMMENT "Copying debug DLLs for ${target_name}"
                )
            else()
                # Copy release DLLs to build root
                add_custom_command(TARGET ${target_name} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${VCPKG_BIN_DIR}/SDL2.dll"
                    "${CMAKE_CURRENT_BINARY_DIR}/"
                    COMMENT "Copying release DLLs for ${target_name}"
                )
            endif()
        endif()
    endfunction()
    
    # Apply DLL copying to all targets
    copy_dlls_to_target(scene_tile_test)
    copy_dlls_to_target(model_viewer)
    copy_dlls_to_target(model_viewerfx)
else()
    target_link_libraries(scene_tile_test ${SDL2_LIBRARIES} ${EXTRA_LIBS})
    target_link_libraries(model_viewer ${SDL2_LIBRARIES} ${EXTRA_LIBS})
    if(APPLE)
        target_link_libraries(model_viewerfx ${SDL2_LIBRARIES} ${OPENGL_LIBRARY} ${EXTRA_LIBS})
        target_link_libraries(osx_sdl2_game ${SDL2_LIBRARIES} ${OPENGL_LIBRARY} ${EXTRA_LIBS})
    else()
        target_link_libraries(model_viewerfx ${SDL2_LIBRARIES} ${OPENGL_LIBRARIES} ${EXTRA_LIBS})
    endif()
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

    set(SOURCES
        src/metal_main.mm
        src/shared_tables.c
        src/osrs/bounds_cylinder.c
        src/string_utils.c
        src/osrs/frustrum_cullmap.c
        src/osrs/tables/textures.c
        src/osrs/tables/sprites.c
        src/osrs/blend_underlays.c
        src/osrs/scene_tile.c
        src/osrs/palette.c
        src/osrs/cache.c
        src/osrs/disk.c
        src/osrs/cache_inet.c
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
        src/graphics/render.c
        src/osrs/rsbuf.c
        src/graphics/lighting.c
        src/osrs/anim.c
        src/osrs/model_transforms.c
        src/osrs/xtea.c
        src/osrs/archive.c
        src/osrs/xtea_config.c
        src/osrs/scene_cache.c
        src/osrs/tables/noise.c
        src/datastruct/ht.c

        src/3rd/bzip/bzip.c
        src/3rd/miniz/miniz.c
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

    target_compile_options(scene_tile_test PRIVATE ${PROFILING_FLAGS})
elseif(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    # On non-macOS platforms (except Emscripten), create an empty executable
    add_executable(metal_renderer src/dummy.c)
    message(STATUS "Metal renderer is only supported on macOS. Creating empty executable.")
else()
    # On Emscripten, don't create metal_renderer at all
    message(STATUS "Metal renderer is not supported on Emscripten. Skipping.")
endif()