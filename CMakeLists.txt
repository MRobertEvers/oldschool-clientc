cmake_minimum_required(VERSION 3.7)
project(RSClient LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# # Enable ASAN
# cmake -DENABLE_ASAN=ON ..
# # Disable ASAN (default)
# cmake -DENABLE_ASAN=OFF ..
option(ENABLE_ASAN "Enable AddressSanitizer (ASAN)" OFF)

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
    src/graphics/dash_minimap.c
    src/graphics/dashmap.c
    src/datastruct/list.c
    src/datastruct/vec.c
    src/datastruct/ringbuf.c
    src/platforms/common/sockstream.c
    src/platforms/common/platform_memory.c
    src/platforms/common/tori_rs_sdl2_gameinput.c
    src/platforms/common/tori_rs_sdl2_gameinput_imgui.cpp
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
    src/osrs/lua_sidecar/lua_game.c
    src/osrs/lua_sidecar/lua_ui.c
    src/osrs/lua_sidecar/lua_gametypes.c
    src/osrs/lua_sidecar/luac_gametypes.c
    src/osrs/lua_sidecar/luac_sidecar.c
    src/osrs/lua_sidecar/lua_platform.c
    src/imgui/imgui.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/imgui_widgets.cpp
    src/3rd/bzip/bzip.c
    src/3rd/miniz/miniz.c
    src/3rd/tommath/mpi.c
    src/3rd/ini/ini.c
)

# --- Native (OSX/Windows) Target ---
if(NOT EMSCRIPTEN)
    set(OSX_SOURCES
        ${COMMON_SOURCES}
        test/osx.cpp
        src/platforms/platform_impl2_osx_sdl2.cpp
        src/platforms/platform_impl2_sdl2_renderer_soft3d_shared.cpp
        src/platforms/platform_impl2_osx_sdl2_renderer_soft3d.cpp
        src/osrs/lua_sidecar/luac_sidecar_config.c
        src/imgui/imgui_impl_sdl2.cpp
        src/imgui/imgui_impl_sdlrenderer2.cpp
    )

    if(NOT WIN32)
        list(APPEND OSX_SOURCES
            src/platforms/platform_impl2_osx_sdl2_renderer_opengl3.cpp
            src/osrs/pix3dgl.cpp
            src/imgui/imgui_impl_opengl3.cpp
        )
    endif()

    if(APPLE)
        list(APPEND OSX_SOURCES
            src/platforms/platform_impl2_osx_sdl2_renderer_metal.mm
            src/imgui/imgui_impl_metal.mm
        )
    endif()

    if(WIN32)
        list(APPEND OSX_SOURCES
            src/platforms/platform_impl2_osx_sdl2_renderer_d3d11.cpp
            src/imgui/imgui_impl_dx11.cpp
        )
    endif()

    add_executable(osx ${OSX_SOURCES})

    find_package(SDL2 REQUIRED)
    target_link_libraries(osx SDL2::SDL2)

    if(NOT WIN32)
        find_package(OpenGL REQUIRED)
        target_link_libraries(osx OpenGL::GL)
    endif()

    if(APPLE)
        target_link_libraries(osx
            "-framework Metal"
            "-framework MetalKit"
            "-framework QuartzCore"
            "-framework Foundation"
        )
        # Enable ARC only for the Objective-C++ Metal files
        set_source_files_properties(
            src/platforms/platform_impl2_osx_sdl2_renderer_metal.mm
            src/imgui/imgui_impl_metal.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc"
        )

        # Compile src/Shaders.metal -> Shaders.metallib next to the osx binary (runtime load path)
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
        add_dependencies(osx shaders_metallib)
    endif()

    if(WIN32)
        target_link_libraries(osx ws2_32 psapi d3d11 dxgi d3dcompiler)
        target_link_options(osx PRIVATE -Wl,-subsystem,console)
    endif()

    if(ENABLE_ASAN)
        target_compile_options(osx PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(osx PRIVATE -fsanitize=address)
    endif()
    add_executable(interface161_test
        tools/interface161_test/main.c
        src/osrs/rscache/tables/component.c
        src/osrs/rscache/rsbuf.c
        src/bmp.c
        src/osrs/rscache/cache.c
        src/osrs/rscache/disk.c
        src/osrs/rscache/reference_table.c
        src/osrs/rscache/archive_decompress.c
        src/osrs/rscache/compression.c
        src/osrs/rscache/archive.c
        src/osrs/rscache/xtea.c
        src/osrs/rscache/xtea_config.c
        src/osrs/rscache/filelist.c
        src/osrs/rscache/bitbuffer.c
        src/osrs/rscache/tables/string_utils.c
        src/osrs/rscache/tables/sprites.c
        src/3rd/bzip/bzip.c
        src/3rd/miniz/miniz.c
    )
    target_include_directories(interface161_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    if( NOT WIN32 )
        target_link_libraries( interface161_test PRIVATE m )
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
        src/platforms/platform_impl2_emscripten_sdl2.cpp
        src/platforms/platform_impl2_sdl2_renderer_soft3d_shared.cpp
        src/platforms/platform_impl2_emscripten_sdl2_renderer_soft3d.cpp
        src/platforms/platform_impl2_emscripten_sdl2_renderer_webgl1.cpp
        src/osrs/pix3dgl_opengles2.cpp
        src/platforms/browser2/luajs_sidecar.c
        src/platforms/browser2/luajs_gametypes.c
        src/platforms/browser2/luajs_archives.c
        src/platforms/browser2/luajs_configs.c
        src/platforms/browser2/netjs.c
        src/imgui/imgui_impl_sdl2.cpp
        src/imgui/imgui_impl_sdlrenderer2.cpp
        src/imgui/imgui_impl_opengl3.cpp
    )

    # Emscripten-specific Compile/Link flags
    target_compile_options(web_client PUBLIC 
        -sUSE_SDL=2
        # // These seem to be needed for webgl1 on motorola
        # // Chrome 85
        "-mno-bulk-memory"
        "-mno-reference-types"
        "-mno-mutable-globals"
        "-mno-sign-ext"
    )
    target_link_options(web_client PRIVATE
        -sUSE_SDL=2
        -sWASM=1
        -sALLOW_MEMORY_GROWTH=1
        -sALLOW_TABLE_GROWTH=1
        -sSTACK_SIZE=512kb
        -sNO_EXIT_RUNTIME=1
        -sASSERTions=1
        --shell-file ${EM_SHELL_FILE}
        -sEXPORTED_FUNCTIONS=${EXPORTED_FUNC_STR}
        -sEXPORTED_RUNTIME_METHODS=${EXPORTED_METHODS_STR}
        --bind
        # // These seem to be needed for webgl1 on motorola
        # // Chrome 85
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