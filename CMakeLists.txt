cmake_minimum_required(VERSION 3.7)
project(RSClient LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
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
    src/server/server.c
    src/osrs/buildcachedat.c
    src/osrs/isaac.c
    src/osrs/buildcachedat_loader.c
    src/osrs/buildcache.c
    src/osrs/buildcache_loader.c
    src/server/prot.c
    src/datastruct/list.c
    src/datastruct/vec.c
    src/datastruct/ringbuf.c
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
    src/osrs/cache_utils.c
    src/osrs/minimap.c
    src/osrs/minimenu.c
    src/osrs/model_transforms.c
    src/tori_rs_render.c
    src/graphics/dash.c
    src/graphics/dashmap.c
    src/osrs/texture.c
    src/osrs/collision_map.c
    src/osrs/world_pickset.c
    src/osrs/world_options.c
    src/osrs/rscache/filelist.c
    src/osrs/rscache/archive.c
    src/osrs/gio.c
    src/osrs/gio_assets.c
    src/osrs/gio_cache.c
    src/osrs/revconfig/revconfig.c
    src/osrs/revconfig/revconfig_load.c
    src/osrs/revconfig/static_ui.c
    src/osrs/revconfig/static_ui_load.c
    src/osrs/revconfig/uiscene.c
    src/osrs/rscache/tables_dat/config_component.c
    src/osrs/interface.c
    src/osrs/obj_icon.c
    src/osrs/gio_cache_dat.c
    src/osrs/world.c
    src/osrs/decor_buildmap.c
    src/osrs/scene2.c
    src/osrs/heightmap.c
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
    src/3rd/bzip/bzip.c
    src/3rd/miniz/miniz.c
    src/3rd/tommath/mpi.c
    src/3rd/ini/ini.c
    src/osrs/rsa.c
    src/osrs/rscache/rsbuf.c
    src/osrs/rscache/bitbuffer.c
    src/osrs/rscache/xtea.c
    src/osrs/revs/revpacket_lc245_2_query.c
    src/osrs/entity_scenebuild.c
    src/osrs/rscache/xtea.c
    src/osrs/revs/revpacket_lc245_2_query.c
    src/osrs/entity_scenebuild.c
    src/platforms/common/sockstream.c
    src/osrs/lua_sidecar/lua_cachedat_sidecar.c
    src/osrs/lua_sidecar/luac_sidecar.c
    src/imgui/imgui.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/imgui_widgets.cpp
)

# --- Native (OSX/Windows) Target ---
if(NOT EMSCRIPTEN)
    add_executable(osx
        ${COMMON_SOURCES}
        test/osx.cpp
        src/platforms/platform_impl2_osx_sdl2.cpp
        src/platforms/platform_impl2_osx_sdl2_renderer_soft3d.cpp
        src/imgui/imgui_impl_sdl2.cpp
        src/imgui/imgui_impl_opengl3.cpp
        src/imgui/imgui_impl_sdlrenderer2.cpp
    )

    find_package(SDL2 REQUIRED)
    target_link_libraries(osx SDL2::SDL2)

    if(WIN32)
        target_link_libraries(osx ws2_32)
        target_link_options(osx PRIVATE -Wl,-subsystem,console)
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(osx PRIVATE -fpermissive)
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
        src/platforms/platform_impl2_emscripten_sdl2.cpp
        src/platforms/browser2/luajs_sidecar.c
        src/imgui/imgui_impl_sdl2.cpp
        src/imgui/imgui_impl_sdlrenderer2.cpp
    )

    # Emscripten-specific Compile/Link flags
    target_compile_options(web_client PUBLIC -sUSE_SDL=2)
    target_link_options(web_client PRIVATE
        -sUSE_SDL=2
        -sWASM=1
        -sALLOW_MEMORY_GROWTH=1
        -sNO_EXIT_RUNTIME=1
        -sASSERTions=1
        --shell-file ${EM_SHELL_FILE}
        -sEXPORTED_FUNCTIONS=${EXPORTED_FUNC_STR}
        -sEXPORTED_RUNTIME_METHODS=${EXPORTED_METHODS_STR}
    )

    set(BROWSER_ASSETS
        ${EM_SHELL_FILE}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platforms/browser2/luajs_sidecar.js
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
endif()

# --- Global Properties ---
foreach(target osx web_client)
    if(TARGET ${target})
        target_include_directories(${target} PRIVATE src src/imgui)
        set_target_properties(${target} PROPERTIES
            C_STANDARD 11
            CXX_STANDARD 17
        )
    endif()
endforeach()