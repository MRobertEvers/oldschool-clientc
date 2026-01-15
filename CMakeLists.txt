
cmake_minimum_required(VERSION 3.7)
project(RSClient
    LANGUAGES C CXX
)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Enable AddressSanitizer for Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g -Og)
    add_link_options(-fsanitize=address)
endif()

# OSX SDL2 Game executable (macOS only)
if(APPLE AND NOT ${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    add_executable(osx
        test/osx.cpp
        src/libg.c
        src/datastruct/list.c
        src/datastruct/vec.c
        src/shared_tables.c
        src/graphics/lighting.c
        src/osrs/tables/model.c
        src/osrs/tables/maps.c
        src/osrs/tables/config_locs.c
        src/osrs/tables/config_floortype.c
        src/osrs/tables/config_object.c
        src/osrs/tables/config_sequence.c
        src/osrs/tables/config_npctype.c
        src/osrs/tables/noise.c
        src/osrs/tables/textures.c
        src/osrs/tables/sprites.c
        src/osrs/painters.c
        src/osrs/dashlib.c
        src/osrs/model_transforms.c

        src/graphics/dash.c
        src/graphics/dashmap.c

        src/osrs/texture.c
        src/osrs/scene.c
        src/osrs/scenebuilder.c
        src/osrs/filelist.c
        src/osrs/grender.c
        src/osrs/archive.c
        src/osrs/gio.c
        src/osrs/gio_assets.c
        src/osrs/gio_cache.c
        src/osrs/gtask.c
        src/osrs/gtask_init_io.c
        src/osrs/gtask_init_scene.c
        src/osrs/configmap.c

        src/osrs/cache.c
        src/osrs/scene.c
        src/osrs/bounds_cylinder.c
        src/osrs/frustrum_cullmap.c
        src/osrs/palette.c
        src/osrs/archive_decompress.c
        src/osrs/cache_inet.c
        src/osrs/disk.c
        src/osrs/reference_table.c
        src/osrs/xtea_config.c
        src/3rd/bzip/bzip.c
        src/3rd/miniz/miniz.c
        src/osrs/rsbuf.c
        src/osrs/xtea.c

        src/platforms/platform_impl2_osx_sdl2.cpp
        src/platforms/platform_impl2_osx_sdl2_renderer_soft3d.cpp

        src/imgui/imgui.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp
        src/imgui/imgui_impl_sdl2.cpp
        src/imgui/imgui_impl_opengl3.cpp
        src/imgui/imgui_impl_sdlrenderer2.cpp
    )

    target_include_directories(osx PRIVATE src src/imgui)

    find_package(SDL2 REQUIRED)
    find_package(Freetype REQUIRED)

    target_link_libraries(osx SDL2::SDL2)
    target_include_directories(osx PRIVATE ${SDL2_INCLUDE_DIRS})
endif()

set_target_properties(osx PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
