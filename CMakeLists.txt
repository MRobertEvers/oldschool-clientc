cmake_minimum_required(VERSION 3.7)
project(RSClient LANGUAGES C CXX)

# clangd/IDE: emit build/compile_commands.json (refreshed when you re-run CMake)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CheckIncludeFileCXX)
include(CheckLibraryExists)
check_include_file_cxx("d3d8.h" HAVE_D3D8_H)

# MinGW-w64 x86_64 has d3d8.h but often no libd3d8.a (use runtime LoadLibrary).
# i686 winlibs sysroot ships libd3d8.a next to the compiler — find it explicitly.
set(HAVE_LIBD3D8 0)
set(TORIRS_D3D8_IMPORT_LIB "")
set(TORIRS_DXGUID_IMPORT_LIB "")

if(HAVE_D3D8_H AND DEFINED CMAKE_CXX_COMPILER)
    get_filename_component(_torirs_cxx_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_torirs_d3d8_libdir "${_torirs_cxx_bin}/../i686-w64-mingw32/lib")

    if(EXISTS "${_torirs_d3d8_libdir}/libd3d8.a")
        find_library(TORIRS_D3D8_IMPORT_LIB d3d8 PATHS "${_torirs_d3d8_libdir}" NO_DEFAULT_PATH)
        find_library(TORIRS_DXGUID_IMPORT_LIB dxguid PATHS "${_torirs_d3d8_libdir}" NO_DEFAULT_PATH)

        if(TORIRS_D3D8_IMPORT_LIB AND TORIRS_DXGUID_IMPORT_LIB)
            set(HAVE_LIBD3D8 1)
        endif()
    endif()
endif()

if(HAVE_D3D8_H AND NOT HAVE_LIBD3D8)
    check_library_exists(d3d8 Direct3DCreate8 "" HAVE_LIBD3D8)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# RelWithDebInfo: embed DWARF in binaries (gdb/lldb). Applies to sdl2, bench_sdl2, etc.,
# not only win32 (which also sets -g3 on that target for historical reasons).
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options("$<$<CONFIG:RelWithDebInfo>:-g3;-gdwarf-4;-fno-omit-frame-pointer>")
endif()

# # Enable ASAN
# cmake -DENABLE_ASAN=ON ..
# # Disable ASAN (default)
# cmake -DENABLE_ASAN=OFF ..
option(ENABLE_ASAN "Enable AddressSanitizer (ASAN)" OFF)

# # Enable UBSAN
# cmake -DENABLE_UBSAN=ON ..
# # Disable UBSAN (default)
# cmake -DENABLE_UBSAN=OFF ..
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer (UBSAN)" OFF)
option(BUILD_WEB_NATIVE "Build the web_client_native (SDL-free) Emscripten target" OFF)
option(ENABLE_PACKAGE_BUILD "Copy scripts/configs/cache254 next to executable; use app-root resource paths" OFF)
option(ENABLE_HEAP_INFO "Show heap stats in Nuklear debug overlays (platform_get_memory_info)" OFF)
set(DASH_BUCKET_SORT_MODE "SPARSE_2D"
    CACHE STRING "Bucket sort backend: SPARSE_2D, LINKED_LIST, or PREFIX_SUM")
set_property(CACHE DASH_BUCKET_SORT_MODE PROPERTY STRINGS SPARSE_2D LINKED_LIST PREFIX_SUM)

option(DASH_BUCKET_SORT_USE_LINKED_LIST
    "Legacy alias: when ON, forces DASH_BUCKET_SORT_MODE=LINKED_LIST" OFF)

if(DASH_BUCKET_SORT_USE_LINKED_LIST)
    set(DASH_BUCKET_SORT_MODE "LINKED_LIST" CACHE STRING
        "Bucket sort backend: SPARSE_2D, LINKED_LIST, or PREFIX_SUM" FORCE)
endif()

file(GLOB LUA_SOURCES "src/3rd/lua/*.c")
list(REMOVE_ITEM LUA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/3rd/lua/lua.c" "${CMAKE_CURRENT_SOURCE_DIR}/src/3rd/lua/luac.c")

# --- Common Sources (Logic shared by all platforms) ---
set(COMMON_SOURCES
    ${LUA_SOURCES}
    src/osrs/isaac.c
    src/jbase37.c
    src/tori_rs.c
    src/bmp.c
    src/tori_rs_render.c
    src/LibToriRSPlatformC.c
    src/server/server.c
    src/server/prot.c
    src/graphics/dash.c
    src/graphics/dash_bench.c
    src/graphics/dash_model.c
    src/graphics/dash_minimap.c
    src/graphics/dashmap.c
    src/datastruct/list.c
    src/datastruct/vec.c
    src/datastruct/ringbuf.c
    src/platforms/common/sockstream.c
    src/platforms/common/platform_memory.c
    src/platforms/common/torirs_sprite_argb_rgba.c
    src/platforms/common/torirs_font_glyph_quad.c
    src/platforms/common/torirs_sprite_draw_cpu.cpp
    src/platforms/common/tori_rs_sdl2_gameinput.c
    src/platforms/common/tori_rs_sdl2_gameinput_nuklear.cpp
    src/platforms/common/torirs_nk_sdl_input.cpp
    src/platforms/common/torirs_nk_ui_bridge.cpp
    src/platforms/common/torirs_nuklear_debug_panel.cpp
    src/nuklear/nuklear_impl.c
    src/osrs/ginput.c
    src/osrs/buildcachedat.c
    src/osrs/isaac.c
    src/osrs/buildcachedat_loader.c
    src/osrs/buildcache.c
    src/osrs/buildcache_loader.c
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
    src/osrs/rscache/tables_dat/config_varp.c
    src/osrs/rscache/tables_dat/config_obj.c
    src/osrs/rscache/tables_dat/animframe.c
    src/osrs/rscache/tables_dat/pix8.c
    src/osrs/rscache/tables_dat/pix32.c
    src/osrs/rscache/tables_dat/pixfont.c
    src/osrs/painters.c
    src/osrs/painters_cullmap_baked_path.c
    src/osrs/cache_utils.c
    src/osrs/minimap.c
    src/osrs/minimenu.c
    src/osrs/model_transforms.c
    src/osrs/texture.c
    src/osrs/collision_map.c
    src/osrs/world_pickset.c
    src/osrs/world_options.c
    src/osrs/rscache/filelist.c
    src/osrs/rscache/archive.c
    src/osrs/revconfig/revconfig.c
    src/osrs/revconfig/revconfig_load.c
    src/osrs/revconfig/uitree.c
    src/osrs/revconfig/uitree_grid.c
    src/osrs/revconfig/uitree_load.c
    src/osrs/rs_component_gfx.c
    src/osrs/revconfig/uiscene.c
    src/osrs/rscache/tables_dat/config_component.c
    src/osrs/interface.c
    src/osrs/interface_state.c
    src/osrs/rs_component_state.c
    src/osrs/clientscript_vm.c
    src/osrs/obj_icon.c
    src/osrs/gio_cache_dat.c
    src/osrs/entity_vec.c
    src/osrs/world.c
    src/osrs/decor_buildmap.c
    src/osrs/scene2.c
    src/osrs/heightmap.c
    src/osrs/contour_ground.c
    src/osrs/lightmap.c
    src/osrs/blendmap.c
    src/osrs/overlaymap.c
    src/osrs/terrain_shapemap.c
    src/osrs/sharelight_map.c
    src/osrs/shademap.c
    src/osrs/world_scenebuild.c
    src/osrs/configmap.c
    src/osrs/filepack.c
    src/osrs/dash_utils.c
    src/osrs/script_queue.c
    src/osrs/player_stats.c
    src/osrs/varp_varbit_manager.c
    src/osrs/gameproto_exec.c
    src/osrs/gameproto_parse.c
    src/osrs/gameproto_process.c
    src/osrs/game.c
    src/osrs/game_entity.c
    src/osrs/wordpack.c
    src/osrs/loginproto.c
    src/osrs/datatypes/appearances.c
    src/osrs/datatypes/player_appearance.c
    src/osrs/packets/pkt_player_info.c
    src/osrs/packets/pkt_npc_info.c
    src/osrs/packetbuffer.c
    src/osrs/rscache/compression.c
    src/osrs/rscache/tables/string_utils.c
    src/osrs/rscache/tables_dat/config_versionlist_mapsquare.c
    src/osrs/rscache/tables_dat/config_npc.c
    src/osrs/rscache/cache.c
    src/osrs/rscache/cache_dat.c
    src/osrs/frustrum_cullmap.c
    src/osrs/palette.c
    src/osrs/rscache/archive_decompress.c
    src/osrs/rscache/disk.c
    src/osrs/rscache/reference_table.c
    src/osrs/rscache/xtea_config.c
    src/osrs/rsa.c
    src/osrs/rscache/rsbuf.c
    src/osrs/rscache/tables/component.c
    src/osrs/rscache/bitbuffer.c
    src/osrs/rscache/xtea.c
    src/osrs/revs/revpacket_lc245_2_query.c
    src/osrs/entity_scenebuild.c
    src/osrs/rscache/xtea.c
    src/osrs/revs/revpacket_lc245_2_query.c
    src/osrs/entity_scenebuild.c
    src/osrs/lua_sidecar/luac_sidecar_cachedat.c
    src/osrs/lua_sidecar/lua_buildcachedat.c
    src/osrs/lua_sidecar/lua_dash.c
    src/osrs/lua_sidecar/lua_sidecar_misc.c
    src/osrs/lua_sidecar/lua_game.c
    src/osrs/lua_sidecar/lua_ui.c
    src/osrs/lua_sidecar/lua_gametypes.c
    src/osrs/lua_sidecar/luac_gametypes.c
    src/osrs/lua_sidecar/luac_sidecar.c
    src/osrs/lua_sidecar/lua_api.c
    src/osrs/lua_sidecar/lua_api_ht.c
    src/osrs/lua_sidecar/lua_platform.c
    src/3rd/bzip/bzip.c
    src/3rd/miniz/miniz.c
    src/3rd/tommath/mpi.c
    src/3rd/ini/ini.c
)

# --- Native (OSX/Windows) Target ---
if(NOT EMSCRIPTEN)
    set(SDL2_NATIVE_SOURCES
        ${COMMON_SOURCES}
        test/sdl2.cpp
        src/platforms/platform_impl2_sdl2.cpp
        src/platforms/platform_impl2_sdl2_renderer_soft3d_shared.cpp
        src/platforms/platform_impl2_sdl2_renderer_soft3d.cpp
        src/osrs/lua_sidecar/luac_sidecar_config.c
        src/osrs/lua_sidecar/luac_sidecar_command_dispatch.c
    )

    if(APPLE)
        list(APPEND SDL2_NATIVE_SOURCES src/platforms/platform_impl2_sdl2_port_osx.cpp)
    elseif(WIN32)
        list(APPEND SDL2_NATIVE_SOURCES src/platforms/platform_impl2_sdl2_port_win.cpp)
    else()
        list(APPEND SDL2_NATIVE_SOURCES src/platforms/platform_impl2_sdl2_port_linux.cpp)
    endif()

    if(NOT WIN32)
        list(APPEND SDL2_NATIVE_SOURCES
            src/platforms/platform_impl2_sdl2_renderer_opengl3.cpp
            src/platforms/opengl3/opengl3_helpers.cpp
            src/platforms/opengl3/opengl3_gl_programs.cpp
            src/platforms/opengl3/opengl3_atlas_resources.cpp
            src/platforms/opengl3/opengl3_render.cpp
            src/platforms/opengl3/events/opengl3_event_clear_rect.cpp
            src/platforms/opengl3/events/opengl3_event_begin_3d.cpp
            src/platforms/opengl3/events/opengl3_event_end_3d.cpp
            src/platforms/opengl3/events/opengl3_event_texture_load.cpp
            src/platforms/opengl3/events/opengl3_event_res_tex_load.cpp
            src/platforms/opengl3/events/opengl3_event_model_load.cpp
            src/platforms/opengl3/events/opengl3_event_model_unload.cpp
            src/platforms/opengl3/events/opengl3_event_model_animation_load.cpp
            src/platforms/opengl3/events/opengl3_event_batch3d_begin.cpp
            src/platforms/opengl3/events/opengl3_event_batch3d_model_add.cpp
            src/platforms/opengl3/events/opengl3_event_batch3d_end.cpp
            src/platforms/opengl3/events/opengl3_event_batch3d_clear.cpp
            src/platforms/opengl3/events/opengl3_event_draw_model.cpp
            src/platforms/opengl3/opengl3_add_model_draw_scenery.cpp
            src/platforms/opengl3/pass_3d_builder2_opengl3.cpp
            src/platforms/opengl3/gpu_3d_cache2_opengl3.cpp
        )
    endif()

    if(APPLE)
        list(APPEND SDL2_NATIVE_SOURCES
            src/platforms/platform_impl2_sdl2_renderer_metal.mm
            src/platforms/ToriRSPlatformKit/src/tools/trspk_resource_cache.c
            src/platforms/ToriRSPlatformKit/src/tools/trspk_pass_batch16.c
            src/platforms/ToriRSPlatformKit/src/tools/trspk_pass_batch32.c
            src/platforms/ToriRSPlatformKit/src/tools/trspk_dynamic_slotmap16.c
            src/platforms/ToriRSPlatformKit/src/tools/trspk_dynamic_slotmap32.c
            src/platforms/ToriRSPlatformKit/src/tools/trspk_dynamic_pass.c
            src/platforms/ToriRSPlatformKit/src/tools/trspk_vertex_format.c
            src/platforms/ToriRSPlatformKit/src/tools/trspk_facebuffer.c
            src/platforms/ToriRSPlatformKit/src/tools/trspk_cache_model_loader16.c
            src/platforms/ToriRSPlatformKit/src/tools/trspk_cache_model_loader32.c
            src/platforms/ToriRSPlatformKit/src/tools/trspk_dash.c
            src/platforms/ToriRSPlatformKit/src/trspk_math.c
            src/platforms/ToriRSPlatformKit/src/backends/metal/metal_core.m
            src/platforms/ToriRSPlatformKit/src/backends/metal/metal_shaders.m
            src/platforms/ToriRSPlatformKit/src/backends/metal/metal_3d_cache.m
            src/platforms/ToriRSPlatformKit/src/backends/metal/metal_dynamic_cache.m
            src/platforms/ToriRSPlatformKit/src/backends/metal/metal_draw.m
            src/platforms/ToriRSPlatformKit/src/backends/metal/metal_events.m
        )
    endif()

    add_executable(sdl2 ${SDL2_NATIVE_SOURCES})

    if(WIN32)
        # D3D11 renderer sources are not built; gate references in test/sdl2.cpp.
        target_compile_definitions(sdl2 PRIVATE TORIRS_NO_D3D11=1)
    endif()

    set(BENCH_SDL2_SOURCES ${SDL2_NATIVE_SOURCES})
    list(REMOVE_ITEM BENCH_SDL2_SOURCES test/sdl2.cpp)
    list(APPEND BENCH_SDL2_SOURCES benchmarks/bench_sdl2/bench_sdl2_main.cpp)
    add_executable(bench_sdl2 ${BENCH_SDL2_SOURCES})

    # Optional: U/V wrap (%128 per axis) in deob texel fetch — add
    # target_compile_definitions(bench_sdl2 PRIVATE DEOB_TEXTURE_DEBUG_V_MODULO=1)
    if(WIN32)
        target_compile_definitions(bench_sdl2 PRIVATE TORIRS_NO_D3D11=1)
    endif()

    if(ENABLE_PACKAGE_BUILD)
        target_compile_definitions(bench_sdl2 PRIVATE
            LUA_SCRIPTS_DIR="scripts"
            CONFIG_DIRECTORY="configs"
            CACHE_PATH="cache254"
            CACHE_DAT_PATH="cache254"
        )
        set(_bench_sdl2_pkg_post_build
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/src/osrs/scripts"
            "${CMAKE_CURRENT_BINARY_DIR}/scripts"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/src/osrs/revconfig/configs"
            "${CMAKE_CURRENT_BINARY_DIR}/configs"
        )

        if(EXISTS "${CMAKE_SOURCE_DIR}/../cache254")
            list(APPEND _bench_sdl2_pkg_post_build
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/../cache254"
                "${CMAKE_CURRENT_BINARY_DIR}/cache254"
            )
        endif()

        add_custom_command(TARGET bench_sdl2 POST_BUILD
            ${_bench_sdl2_pkg_post_build}
            COMMENT "ENABLE_PACKAGE_BUILD: copy resources next to bench_sdl2 executable"
        )
    endif()

    if(ENABLE_PACKAGE_BUILD)
        target_compile_definitions(sdl2 PRIVATE
            LUA_SCRIPTS_DIR="scripts"
            CONFIG_DIRECTORY="configs"
            CACHE_PATH="cache254"
            CACHE_DAT_PATH="cache254"
        )
        set(_sdl2_pkg_post_build
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/src/osrs/scripts"
            "${CMAKE_CURRENT_BINARY_DIR}/scripts"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/src/osrs/revconfig/configs"
            "${CMAKE_CURRENT_BINARY_DIR}/configs"
        )

        if(EXISTS "${CMAKE_SOURCE_DIR}/../cache254")
            list(APPEND _sdl2_pkg_post_build
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/../cache254"
                "${CMAKE_CURRENT_BINARY_DIR}/cache254"
            )
        else()
            message(WARNING
                "ENABLE_PACKAGE_BUILD: no directory at ${CMAKE_SOURCE_DIR}/../cache254 — skipping cache copy. "
                "Place or symlink a cache254 folder next to the repo for a full distributable build."
            )
        endif()

        add_custom_command(TARGET sdl2 POST_BUILD
            ${_sdl2_pkg_post_build}
            COMMENT "ENABLE_PACKAGE_BUILD: copy resources next to sdl2 executable"
        )
    endif()

    # Try normal find_package first (MODULE then CONFIG). If that fails — e.g. a
    # partial toolchain drop-in that only ships headers/import-lib/dll but no
    # SDL2Config.cmake, common with winlibs — synthesize SDL2::SDL2 manually
    # from CMAKE_PREFIX_PATH / SDL2_ROOT so we don't silently fall through to a
    # system SDL2 with the wrong architecture (e.g. 64-bit msys2 SDL2 for a 32-bit build).
    find_package(SDL2 QUIET)

    if(NOT TARGET SDL2::SDL2)
        set(_sdl2_search_roots "")

        if(DEFINED SDL2_ROOT)
            list(APPEND _sdl2_search_roots "${SDL2_ROOT}")
        endif()

        if(DEFINED ENV{SDL2_ROOT})
            list(APPEND _sdl2_search_roots "$ENV{SDL2_ROOT}")
        endif()

        foreach(_p ${CMAKE_PREFIX_PATH})
            list(APPEND _sdl2_search_roots "${_p}")
        endforeach()

        if(DEFINED ENV{CMAKE_PREFIX_PATH})
            foreach(_p $ENV{CMAKE_PREFIX_PATH})
                list(APPEND _sdl2_search_roots "${_p}")
            endforeach()
        endif()

        find_path(SDL2_INCLUDE_DIR
            NAMES SDL.h
            HINTS ${_sdl2_search_roots}
            PATH_SUFFIXES include/SDL2 include
        )
        find_library(SDL2MAIN_LIBRARY
            NAMES SDL2main
            HINTS ${_sdl2_search_roots}
            PATH_SUFFIXES lib
        )
        find_library(SDL2_LIBRARY
            NAMES SDL2
            HINTS ${_sdl2_search_roots}
            PATH_SUFFIXES lib
        )

        if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
            message(STATUS "SDL2: no CMake config found, using manual fallback")
            message(STATUS "  SDL2 include: ${SDL2_INCLUDE_DIR}")
            message(STATUS "  SDL2 library: ${SDL2_LIBRARY}")

            if(SDL2MAIN_LIBRARY)
                message(STATUS "  SDL2main    : ${SDL2MAIN_LIBRARY}")
            endif()

            add_library(SDL2::SDL2 UNKNOWN IMPORTED)
            set_target_properties(SDL2::SDL2 PROPERTIES
                IMPORTED_LOCATION "${SDL2_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
            )

            if(WIN32)
                # SDL2 on Win32 needs these Windows libs at link time.
                set_target_properties(SDL2::SDL2 PROPERTIES
                    INTERFACE_LINK_LIBRARIES "mingw32;imm32;setupapi;version;winmm"
                )
            endif()

            if(SDL2MAIN_LIBRARY AND NOT TARGET SDL2::SDL2main)
                add_library(SDL2::SDL2main UNKNOWN IMPORTED)
                set_target_properties(SDL2::SDL2main PROPERTIES
                    IMPORTED_LOCATION "${SDL2MAIN_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
                )
            endif()

            set(SDL2_FOUND TRUE)
        else()
            message(FATAL_ERROR
                "SDL2 not found. Tried CMake config mode and manual fallback. "
                "Set -DCMAKE_PREFIX_PATH=<path-to-SDL2-prefix> (a directory containing "
                "include/SDL2/SDL.h and lib/libSDL2.dll.a or equivalent), or install "
                "SDL2 with a SDL2Config.cmake."
            )
        endif()
    endif()

    target_link_libraries(sdl2 SDL2::SDL2)
    target_link_libraries(bench_sdl2 SDL2::SDL2)

    if(NOT WIN32)
        find_package(OpenGL REQUIRED)
        target_link_libraries(sdl2 OpenGL::GL)
        target_link_libraries(bench_sdl2 OpenGL::GL)
    endif()

    if(WIN32)
        target_link_libraries(sdl2 ws2_32 psapi)
        target_link_options(sdl2 PRIVATE -Wl,-subsystem,console)
        target_link_libraries(bench_sdl2 ws2_32 psapi)
        target_link_options(bench_sdl2 PRIVATE -Wl,-subsystem,console)
    endif()

    # SDL-free Win32 + GDI Soft3D (Windows hosts only; XP SP3–oriented code paths).
    if(WIN32)
        set(WIN32_PLATFORM_SOURCES ${COMMON_SOURCES})
        list(REMOVE_ITEM WIN32_PLATFORM_SOURCES
            src/platforms/common/tori_rs_sdl2_gameinput.c
            src/platforms/common/tori_rs_sdl2_gameinput_nuklear.cpp
            src/platforms/common/torirs_nk_sdl_input.cpp
            src/platforms/common/torirs_nk_ui_bridge.cpp
            src/platforms/common/torirs_nuklear_debug_panel.cpp
        )
        list(APPEND WIN32_PLATFORM_SOURCES
            test/win32.cpp
            src/nuklear/backends/rawfb/nuklear_rawfb_impl.c
            src/platforms/platform_impl2_win32.cpp
            src/platforms/platform_impl2_win32_renderer_gdisoft3d.cpp
            src/osrs/lua_sidecar/luac_sidecar_config.c
            src/osrs/lua_sidecar/luac_sidecar_command_dispatch.c
        )

        if(HAVE_D3D8_H)
            list(APPEND WIN32_PLATFORM_SOURCES
                src/platforms/platform_impl2_win32_renderer_d3d8.cpp
            )
        endif()

        add_executable(win32 ${WIN32_PLATFORM_SOURCES})
        target_link_libraries(win32 PRIVATE ws2_32 psapi winmm gdi32 user32 kernel32)

        if(HAVE_D3D8_H)
            target_compile_definitions(win32 PRIVATE TORIRS_HAS_D3D8=1)

            if(HAVE_LIBD3D8)
                if(TORIRS_D3D8_IMPORT_LIB AND TORIRS_DXGUID_IMPORT_LIB)
                    target_link_libraries(win32 PRIVATE ${TORIRS_D3D8_IMPORT_LIB} ${TORIRS_DXGUID_IMPORT_LIB})
                else()
                    target_link_libraries(win32 PRIVATE d3d8 dxguid)
                endif()

                target_compile_definitions(win32 PRIVATE TORIRS_D3D8_STATIC_LINK=1)
            endif()
        else()
            target_compile_definitions(win32 PRIVATE TORIRS_NO_D3D8=1)
        endif()

        if(MSVC)
            target_link_options(win32 PRIVATE /SUBSYSTEM:CONSOLE)
        else()
            target_link_options(win32 PRIVATE -Wl,-subsystem,console)

            # Always-on debug symbols + frame pointers for the win32 target so segfaults
            # produce meaningful backtraces under gdb even in Release/RelWithDebInfo builds.
            if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "Debug")
                target_compile_options(win32 PRIVATE -O3 -g3 -gdwarf-2 -fno-omit-frame-pointer)
                target_link_options(win32 PRIVATE -O3 -g3 -gdwarf-2)
            endif()

            if(CMAKE_BUILD_TYPE STREQUAL "Release")
                target_compile_options(win32 PRIVATE -O3)
                target_link_options(win32 PRIVATE -O3)
            endif()
        endif()

        if(ENABLE_ASAN)
            target_compile_options(win32 PRIVATE -fsanitize=address -fno-omit-frame-pointer)
            target_link_options(win32 PRIVATE -fsanitize=address)
        endif()

        if(ENABLE_UBSAN)
            target_compile_options(win32 PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
            target_link_options(win32 PRIVATE -fsanitize=undefined)
        endif()

        if(ENABLE_PACKAGE_BUILD)
            target_compile_definitions(win32 PRIVATE
                LUA_SCRIPTS_DIR="scripts"
                CONFIG_DIRECTORY="configs"
                CACHE_PATH="cache254"
                CACHE_DAT_PATH="cache254"
            )
            set(_win32_pkg_post_build
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/src/osrs/scripts"
                "${CMAKE_CURRENT_BINARY_DIR}/scripts"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/src/osrs/revconfig/configs"
                "${CMAKE_CURRENT_BINARY_DIR}/configs"
            )

            if(EXISTS "${CMAKE_SOURCE_DIR}/../cache254")
                list(APPEND _win32_pkg_post_build
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_SOURCE_DIR}/../cache254"
                    "${CMAKE_CURRENT_BINARY_DIR}/cache254"
                )
            endif()

            add_custom_command(TARGET win32 POST_BUILD
                ${_win32_pkg_post_build}
                COMMENT "ENABLE_PACKAGE_BUILD: copy resources next to win32 executable"
            )
        endif()
    endif()

    if(APPLE)
        target_link_libraries(sdl2
            "-framework Metal"
            "-framework MetalKit"
            "-framework QuartzCore"
            "-framework Foundation"
        )
        target_link_libraries(bench_sdl2
            "-framework Metal"
            "-framework MetalKit"
            "-framework QuartzCore"
            "-framework Foundation"
        )

        # Enable ARC only for the Objective-C++ Metal files
        set_source_files_properties(
            src/platforms/metal/metal_helpers.mm
            src/platforms/metal/metal_init.mm
            src/platforms/metal/metal_renderer_core.mm
            src/platforms/metal/metal_render_context.mm
            src/platforms/metal/metal_model_events.mm
            src/platforms/metal/metal_atlas_world.mm
            src/platforms/metal/events/metal_event_begin_3d.mm
            src/platforms/metal/events/metal_event_end_3d.mm
            src/platforms/metal/metal_add_model_draw_scenery.mm
            src/platforms/metal/events/metal_event_batch3d_common.mm
            src/platforms/metal/events/metal_event_batch3d_begin.mm
            src/platforms/metal/events/metal_event_batch3d_model_add.mm
            src/platforms/metal/events/metal_event_batch3d_end.mm
            src/platforms/metal/events/metal_event_batch3d_clear.mm
            src/platforms/metal/events/metal_event_draw_model.mm
            src/platforms/metal/events/metal_event_tex_load.mm
            src/platforms/metal/events/metal_event_batch2d_tex_begin.mm
            src/platforms/metal/events/metal_event_batch2d_tex_end.mm
            src/platforms/metal/pass_3d_builder2_metal.mm
            src/platforms/metal/gpu_3d_cache2_metal.mm
            src/platforms/metal/metal_render.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc"
        )

        # Compile src/Shaders.metal -> Shaders.metallib next to the sdl2 binary (runtime load path)
        set(_SHADERS_METAL "${CMAKE_CURRENT_SOURCE_DIR}/src/Shaders.metal")
        set(_SHADERS_AIR "${CMAKE_CURRENT_BINARY_DIR}/Shaders.air")
        set(_SHADERS_METALLIB "${CMAKE_CURRENT_BINARY_DIR}/Shaders.metallib")
        add_custom_command(
            OUTPUT "${_SHADERS_METALLIB}"
            COMMAND xcrun -sdk macosx metal -c "${_SHADERS_METAL}" -o "${_SHADERS_AIR}"
            COMMAND xcrun -sdk macosx metallib "${_SHADERS_AIR}" -o "${_SHADERS_METALLIB}"
            DEPENDS "${_SHADERS_METAL}"
            BYPRODUCTS "${_SHADERS_AIR}"
            COMMENT "Compiling Shaders.metal -> Shaders.metallib"
        )
        add_custom_target(shaders_metallib DEPENDS "${_SHADERS_METALLIB}")
        add_dependencies(sdl2 shaders_metallib)
        add_dependencies(bench_sdl2 shaders_metallib)
    endif()

    if(ENABLE_ASAN)
        target_compile_options(sdl2 PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(sdl2 PRIVATE -fsanitize=address)
        target_compile_options(bench_sdl2 PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(bench_sdl2 PRIVATE -fsanitize=address)
    endif()

    if(ENABLE_UBSAN)
        target_compile_options(sdl2 PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
        target_link_options(sdl2 PRIVATE -fsanitize=undefined)
        target_compile_options(bench_sdl2 PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
        target_link_options(bench_sdl2 PRIVATE -fsanitize=undefined)
    endif()

endif()

# --- Emscripten Target ---
if(EMSCRIPTEN)
    set(EM_SHELL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/platforms/browser2/shell2.html")

    # Define your functions in a clean list
    set(EXPORTED_FUNC_LIST
        "_main"
        "_free"
        "_malloc"
        "_test_export"
        "_LibToriRS_GetBuffer"
        "_Export_NetGetSharedBufferSize"
        "_Export_NetGetOffset_G2P_Data"
        "_Export_NetGetOffset_G2P_Head"
        "_Export_NetGetOffset_G2P_Tail"
        "_Export_NetGetOffset_P2G_Data"
        "_Export_NetGetOffset_P2G_Head"
        "_Export_NetGetOffset_P2G_Tail"
        "_Export_NetGetOffset_Status"
        "_luajs_LuaGameType_NewUserData"
        "_luajs_LuaGameType_NewUserDataArray"
        "_luajs_LuaGameType_NewIntArray"
        "_luajs_LuaGameType_NewVarTypeArray"
        "_luajs_LuaGameType_VarTypeArrayPush"
        "_luajs_LuaGameType_GetVarTypeArrayCount"
        "_luajs_LuaGameType_GetVarTypeArrayAt"
        "_luajs_LuaGameType_NewBool"
        "_luajs_LuaGameType_NewInt"
        "_luajs_LuaGameType_NewFloat"
        "_luajs_LuaGameType_NewString"
        "_luajs_LuaGameType_NewVoid"
        "_luajs_LuaGameType_Free"
        "_luajs_LuaGameType_GetKind"
        "_luajs_LuaGameType_GetUserData"
        "_luajs_LuaGameType_GetUserDataArray"
        "_luajs_LuaGameType_GetUserDataArrayCount"
        "_luajs_LuaGameType_GetIntArray"
        "_luajs_LuaGameType_GetIntArrayCount"
        "_luajs_LuaGameType_GetBool"
        "_luajs_LuaGameType_GetInt"
        "_luajs_LuaGameType_GetFloat"
        "_luajs_LuaGameType_GetString"
        "_luajs_LuaGameType_GetStringLength"
        "_luajs_LuaGameScript_GetName"
        "_luajs_LuaGameScript_GetArgs"
        "_luajs_CacheDatArchive_deserialize"
        "_luajs_CacheDatArchive_serialized_size"
        "_luajs_CacheDatArchive_serialize_to_buffer"
        "_luajs_CacheDatArchive_free"
        "_luajs_ConfigFile_deserialize"
        "_luajs_ConfigFile_free"
    )

    set(EXPORTED_METHODS_LIST
        "cwrap"
        "ccall"
        "UTF8ToString"
        "HEAPU8"
    )

    # Join them into the comma-separated format Emscripten wants
    list(JOIN EXPORTED_FUNC_LIST "," EXPORTED_FUNC_STR)
    list(JOIN EXPORTED_METHODS_LIST "," EXPORTED_METHODS_STR)

    add_executable(web_client
        ${COMMON_SOURCES}
        test/browser.cpp
        src/platforms/platform_impl2_sdl2.cpp
        src/platforms/platform_impl2_sdl2_port_emscripten.cpp
        src/platforms/platform_impl2_sdl2_renderer_webgl1.cpp
        src/platforms/ToriRSPlatformKit/src/tools/trspk_resource_cache.c
        src/platforms/ToriRSPlatformKit/src/tools/trspk_pass_batch16.c
        src/platforms/ToriRSPlatformKit/src/tools/trspk_pass_batch32.c
        src/platforms/ToriRSPlatformKit/src/tools/trspk_dynamic_slotmap16.c
        src/platforms/ToriRSPlatformKit/src/tools/trspk_dynamic_slotmap32.c
        src/platforms/ToriRSPlatformKit/src/tools/trspk_dynamic_pass.c
        src/platforms/ToriRSPlatformKit/src/tools/trspk_vertex_format.c
        src/platforms/ToriRSPlatformKit/src/tools/trspk_facebuffer.c
        src/platforms/ToriRSPlatformKit/src/tools/trspk_cache_model_loader16.c
        src/platforms/ToriRSPlatformKit/src/tools/trspk_cache_model_loader32.c
        src/platforms/ToriRSPlatformKit/src/tools/trspk_dash.c
        src/platforms/ToriRSPlatformKit/src/trspk_math.c
        src/platforms/ToriRSPlatformKit/src/backends/webgl1/webgl1_core.c
        src/platforms/ToriRSPlatformKit/src/backends/webgl1/webgl1_shaders.c
        src/platforms/ToriRSPlatformKit/src/backends/webgl1/webgl1_3d_cache.c
        src/platforms/ToriRSPlatformKit/src/backends/webgl1/webgl1_dynamic_cache.c
        src/platforms/ToriRSPlatformKit/src/backends/webgl1/webgl1_draw.c
        src/platforms/ToriRSPlatformKit/src/backends/webgl1/webgl1_events.c
        src/platforms/browser2/luajs_sidecar.c
        src/platforms/browser2/luajs_gametypes.c
        src/platforms/browser2/luajs_archives.c
        src/platforms/browser2/luajs_configs.c
        src/platforms/browser2/luajs_sidecar_blobs.c
        src/platforms/browser2/netjs.c
    )

    # Emscripten-specific Compile/Link flags
    target_compile_options(web_client PUBLIC
        -sUSE_SDL=2

        # Chrome 85 / older Wasm engines (Emscripten)
        "-mno-bulk-memory"
        "-mno-reference-types"
        "-mno-mutable-globals"
        "-mno-sign-ext"
    )
    target_link_options(web_client PRIVATE
        -sUSE_SDL=2
        -sMIN_WEBGL_VERSION=1
        -sMAX_WEBGL_VERSION=1
        -sWASM=1
        -sALLOW_MEMORY_GROWTH=1

        # -sTOTAL_MEMORY=38mb
        -sALLOW_TABLE_GROWTH=1
        -sSTACK_SIZE=512kb
        -sNO_EXIT_RUNTIME=1
        -sASSERTions=1

        # -sWASM=0
        --shell-file ${EM_SHELL_FILE}
        -sEXPORTED_FUNCTIONS=${EXPORTED_FUNC_STR}
        -sEXPORTED_RUNTIME_METHODS=${EXPORTED_METHODS_STR}
        --bind

        # Chrome 85 / older Wasm engines
        # -sINITIAL_TABLE=65536
        -sMODULARIZE=0
        -sEXPORT_ES6=0
        "-mno-bulk-memory"
        "-mno-reference-types"
        "-mno-mutable-globals"
        "-mno-sign-ext"
    )

    set(BROWSER_ASSETS
        ${EM_SHELL_FILE}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platforms/browser2/luajs_sidecar.js
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platforms/browser2/luajs_api_maps.js
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platforms/browser2/luajs_gametypes.js
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platforms/browser2/netjs.js
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platforms/browser2/fengari-web.js
    )

    # Tell the linker these are dependencies
    set_target_properties(web_client PROPERTIES
        LINK_DEPENDS "${BROWSER_ASSETS}"
    )

    # Set the output name to .html so Emscripten generates the wrapper
    set_target_properties(web_client PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")

    # Define the directory containing your JS files
    set(BROWSER_JS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/platforms/browser2")

    # Create a command to copy the directory contents to the build folder
    add_custom_command(
        TARGET web_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${BROWSER_JS_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Copying browser JS files to build directory"
    )

    if(BUILD_WEB_NATIVE)
        # SDL-free browser build (Soft3D + 2D canvas putImageData only)
        set(WEB_CLIENT_NATIVE_SOURCES ${COMMON_SOURCES})
        list(REMOVE_ITEM WEB_CLIENT_NATIVE_SOURCES
            src/platforms/common/tori_rs_sdl2_gameinput.c
            src/platforms/common/tori_rs_sdl2_gameinput_nuklear.cpp
            src/platforms/common/torirs_nk_sdl_input.cpp
            src/platforms/common/torirs_nk_ui_bridge.cpp
            src/platforms/common/torirs_nuklear_debug_panel.cpp
            src/nuklear/nuklear_impl.c
        )

        add_executable(web_client_native
            ${WEB_CLIENT_NATIVE_SOURCES}
            test/browser_native.cpp
            src/platforms/platform_impl2_emscripten_native.cpp
            src/platforms/platform_impl2_emscripten_native_renderer_soft3d.cpp
            src/platforms/browser2/luajs_sidecar.c
            src/platforms/browser2/luajs_gametypes.c
            src/platforms/browser2/luajs_archives.c
            src/platforms/browser2/luajs_configs.c
            src/platforms/browser2/luajs_sidecar_blobs.c
            src/platforms/browser2/netjs.c
        )

        target_compile_options(web_client_native PUBLIC
            "-mno-bulk-memory"
            "-mno-reference-types"
            "-mno-mutable-globals"
            "-mno-sign-ext"
        )
        target_link_options(web_client_native PRIVATE
            -sWASM=1
            -sALLOW_MEMORY_GROWTH=1
            -sALLOW_TABLE_GROWTH=1
            -sSTACK_SIZE=512kb
            -sNO_EXIT_RUNTIME=1
            -sASSERTIONS=1
            --shell-file ${EM_SHELL_FILE}
            -sEXPORTED_FUNCTIONS=${EXPORTED_FUNC_STR}
            -sEXPORTED_RUNTIME_METHODS=${EXPORTED_METHODS_STR}
            --bind
            -sMODULARIZE=0
            -sEXPORT_ES6=0
            "-mno-bulk-memory"
            "-mno-reference-types"
            "-mno-mutable-globals"
            "-mno-sign-ext"
        )

        set_target_properties(web_client_native PROPERTIES
            LINK_DEPENDS "${BROWSER_ASSETS}"
            OUTPUT_NAME "index_native"
            SUFFIX ".html"
        )

        add_custom_command(
            TARGET web_client_native POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${BROWSER_JS_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "Copying browser JS files to build directory (web_client_native)"
        )
    endif()
endif()

# Always set ENABLE_HEAP_INFO to 0 or 1 so #if ENABLE_HEAP_INFO works (#ifdef is true even for =0).
foreach(_t_nk_heap sdl2 bench_sdl2 web_client web_client_native win32)
    if(TARGET ${_t_nk_heap})
        if(ENABLE_HEAP_INFO)
            target_compile_definitions(${_t_nk_heap} PRIVATE ENABLE_HEAP_INFO=1)
        else()
            target_compile_definitions(${_t_nk_heap} PRIVATE ENABLE_HEAP_INFO=0)
        endif()
    endif()
endforeach()

foreach(_t_bucket_sort sdl2 bench_sdl2 web_client web_client_native win32)
    if(TARGET ${_t_bucket_sort})
        if(DASH_BUCKET_SORT_MODE STREQUAL "LINKED_LIST")
            target_compile_definitions(${_t_bucket_sort} PRIVATE DASH_BUCKET_SORT_MODE=1)
        elseif(DASH_BUCKET_SORT_MODE STREQUAL "PREFIX_SUM")
            target_compile_definitions(${_t_bucket_sort} PRIVATE DASH_BUCKET_SORT_MODE=2)
        else()
            target_compile_definitions(${_t_bucket_sort} PRIVATE DASH_BUCKET_SORT_MODE=0)
        endif()
    endif()
endforeach()

# --- Global Properties ---
foreach(target sdl2 bench_sdl2 web_client web_client_native win32)
    if(TARGET ${target})
        target_include_directories(${target} PRIVATE src src/nuklear src/platforms/ToriRSPlatformKit/include)
        set_target_properties(${target} PROPERTIES
            C_STANDARD 11
            CXX_STANDARD 17
        )
    endif()
endforeach()