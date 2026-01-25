
cmake_minimum_required(VERSION 3.7)
project(RSClient
    LANGUAGES C CXX
)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Enable AddressSanitizer for Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g -O0)
    add_link_options(-fsanitize=address)
endif()

# OSX SDL2 Game executable (macOS only)
if(APPLE AND NOT ${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    add_executable(osx
        test/osx.cpp
        src/tori_rs.c
        src/bmp.c
        src/server/server.c
        src/osrs/buildcachedat.c
        src/server/prot.c
        src/datastruct/list.c
        src/datastruct/vec.c
        src/graphics/shared_tables.c
        src/graphics/lighting.c
        src/osrs/rscache/tables/model.c
        src/osrs/rscache/tables/maps.c
        src/osrs/rscache/tables/config_locs.c
        src/osrs/rscache/tables/config_floortype.c
        src/osrs/rscache/tables/config_object.c
        src/osrs/rscache/tables/config_sequence.c
        src/osrs/rscache/tables/config_npctype.c
        src/osrs/rscache/tables/noise.c
        src/osrs/rscache/tables/framemap.c
        src/osrs/rscache/tables/frame.c
        src/osrs/rscache/tables/textures.c
        src/osrs/rscache/tables/sprites.c
        src/osrs/rscache/tables_dat/config_textures.c
        src/osrs/rscache/tables_dat/config_idk.c
        src/osrs/rscache/tables_dat/config_obj.c
        src/osrs/rscache/tables_dat/animframe.c
        src/osrs/rscache/tables_dat/pix8.c
        src/osrs/rscache/tables_dat/pix32.c
        src/osrs/rscache/tables_dat/pixfont.c
        src/osrs/painters.c
        src/osrs/dashlib.c
        src/osrs/cache_utils.c
        src/osrs/minimap.c
        src/osrs/model_transforms.c

        src/graphics/dash.c
        src/graphics/dashmap.c

        src/osrs/texture.c
        src/osrs/scene.c
        src/osrs/scenebuilder.c
        src/osrs/rscache/filelist.c
        src/osrs/grender.c
        src/osrs/rscache/archive.c
        src/osrs/gio.c
        src/osrs/gio_assets.c
        src/osrs/gio_cache.c

        src/osrs/gio_cache_dat.c
        src/osrs/task_init_io.c
        src/osrs/task_init_scene.c
        src/osrs/task_init_scene_dat.c
        src/osrs/task_load_models_dat.c
        src/osrs/configmap.c
        src/osrs/filepack.c
        src/osrs/dash_utils.c
        src/osrs/gametask.c
        src/osrs/gameproto.c
        src/osrs/packetbuffer.c
        src/osrs/rscache/compression.c
        src/osrs/rscache/tables/string_utils.c
        src/osrs/rscache/tables_dat/config_versionlist_mapsquare.c

        src/osrs/rscache/cache.c
        src/osrs/rscache/cache_dat.c
        src/osrs/scene.c
        src/osrs/frustrum_cullmap.c
        src/osrs/palette.c
        src/osrs/rscache/archive_decompress.c
        src/osrs/rscache/cache_inet.c
        src/osrs/rscache/disk.c
        src/osrs/rscache/reference_table.c
        src/osrs/rscache/xtea_config.c
        src/3rd/bzip/bzip.c
        src/3rd/miniz/miniz.c
        src/osrs/rscache/rsbuf.c
        src/osrs/rscache/xtea.c

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
