#include "platform_impl2_osx_sdl2.h"

extern "C" {
#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "common/luac_sidecar.h"
#include "osrs/buildcachedat_loader.h"
#include "osrs/filepack.h"
#include "osrs/game.h"
#include "osrs/gio.h"
#include "osrs/gio_cache_dat.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables_dat/config_idk.h"
#include "osrs/rscache/tables_dat/config_obj.h"
#include "osrs/rscache/tables_dat/config_textures.h"
#include "osrs/rscache/tables_dat/config_versionlist_mapsquare.h"
#include "osrs/rscache/tables_dat/configs_dat.h"
#include "osrs/texture.h"
#include "tori_rs.h"
#include "tori_rs_render.h"
}

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_PATH "../cache254"
#define LUA_SCRIPTS_DIR "/Users/matthewevers/Documents/git_repos/3draster/src/osrs/scripts"

static void
transform_mouse_coordinates(
    int window_mouse_x,
    int window_mouse_y,
    int* game_mouse_x,
    int* game_mouse_y,
    struct Platform2_OSX_SDL2* platform)
{
    // Calculate the same scaling transformation as the rendering
    // Use the fixed game screen dimensions, not the window dimensions
    float src_aspect = (float)platform->game_screen_width / (float)platform->game_screen_height;
    float dst_aspect = (float)platform->drawable_width / (float)platform->drawable_height;

    int dst_x, dst_y, dst_w, dst_h;

    if( src_aspect > dst_aspect )
    {
        // Source is wider - fit to width
        dst_w = platform->drawable_width;
        dst_h = (int)(platform->drawable_width / src_aspect);
        dst_x = 0;
        dst_y = (platform->drawable_height - dst_h) / 2;
    }
    else
    {
        // Source is taller - fit to height
        dst_h = platform->drawable_height;
        dst_w = (int)(platform->drawable_height * src_aspect);
        dst_y = 0;
        dst_x = (platform->drawable_width - dst_w) / 2;
    }

    // Transform window coordinates to game coordinates
    // Account for the offset and scaling of the game rendering area
    if( window_mouse_x < dst_x || window_mouse_x >= dst_x + dst_w || window_mouse_y < dst_y ||
        window_mouse_y >= dst_y + dst_h )
    {
        // Mouse is outside the game rendering area
        *game_mouse_x = -1;
        *game_mouse_y = -1;
    }
    else
    {
        // Transform from window coordinates to game coordinates
        float relative_x = (float)(window_mouse_x - dst_x) / dst_w;
        float relative_y = (float)(window_mouse_y - dst_y) / dst_h;

        *game_mouse_x = (int)(relative_x * platform->game_screen_width);
        *game_mouse_y = (int)(relative_y * platform->game_screen_height);
    }
}

struct Platform2_OSX_SDL2*
Platform2_OSX_SDL2_New(void)
{
    struct Platform2_OSX_SDL2* platform =
        (struct Platform2_OSX_SDL2*)malloc(sizeof(struct Platform2_OSX_SDL2));
    memset(platform, 0, sizeof(struct Platform2_OSX_SDL2));

    platform->lua_sidecar = LuaCSidecar_New();
    if( !platform->lua_sidecar )
    {
        free(platform);
        return NULL;
    }

    platform->cache_dat = cache_dat_new_from_directory(CACHE_PATH);
    platform->buildcachedat = buildcachedat_new();

    return platform;
}

void
Platform2_OSX_SDL2_Free(struct Platform2_OSX_SDL2* platform)
{
    if( platform->lua_sidecar )
        LuaCSidecar_Free(platform->lua_sidecar);
    if( platform->cache_dat )
        cache_dat_free(platform->cache_dat);
    free(platform);
}

bool
Platform2_OSX_SDL2_InitForSoft3D(
    struct Platform2_OSX_SDL2* platform,
    int screen_width,
    int screen_height)
{
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    platform->window = SDL_CreateWindow(
        "Game",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        screen_width,
        screen_height,
        SDL_WINDOW_RESIZABLE);
    if( !platform->window )
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    platform->window_width = screen_width;
    platform->window_height = screen_height;
    // Store game screen dimensions in drawable_width/drawable_height
    // (these represent the game's logical screen size, not the actual drawable size)
    platform->drawable_width = screen_width;
    platform->drawable_height = screen_height;

    // Store fixed game screen dimensions for mouse coordinate transformation
    platform->game_screen_width = screen_width;
    platform->game_screen_height = screen_height;

    platform->last_frame_time_ticks = SDL_GetTicks64();

    return true;
}

void
Platform2_OSX_SDL2_PollEvents(
    struct Platform2_OSX_SDL2* platform,
    struct GInput* input,
    int chat_focused)
{
    input->mouse_clicked = 0;
    input->mouse_clicked_x = -1;
    input->mouse_clicked_y = -1;
    input->mouse_clicked_right = 0;
    input->mouse_clicked_right_x = -1;
    input->mouse_clicked_right_y = -1;
    input->chat_key_char = 0;
    input->chat_key_return = 0;
    input->chat_key_backspace = 0;
    /* Sync mouse_button_down with actual SDL state so we don't miss events */
    {
        Uint32 buttons = SDL_GetMouseState(nullptr, nullptr);
        input->mouse_button_down = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) ? 1 : 0;
    }

    uint64_t current_frame_time = SDL_GetTicks64();
    input->time_delta_accumulator_seconds +=
        (double)(current_frame_time - platform->last_frame_time_ticks) / 1000.0f;
    platform->last_frame_time_ticks = current_frame_time;

    SDL_Event event;
    while( SDL_PollEvent(&event) )
    {
        // Process ImGui events first
        ImGui_ImplSDL2_ProcessEvent(&event);

        // Check if ImGui wants to capture mouse input
        ImGuiIO& io = ImGui::GetIO();
        bool imgui_wants_mouse = io.WantCaptureMouse;

        if( event.type == SDL_QUIT )
        {
            input->quit = true;
        }
        else if( event.type == SDL_WINDOWEVENT )
        {
            if( event.window.event == SDL_WINDOWEVENT_RESIZED ||
                event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED )
            {
                SDL_GetWindowSize(
                    platform->window, &platform->window_width, &platform->window_height);
                // Since we don't have SDL renderer, drawable size = window size
                platform->drawable_width = platform->window_width;
                platform->drawable_height = platform->window_height;

                // Update ImGui display size for proper scaling
                io.DisplaySize =
                    ImVec2((float)platform->drawable_width, (float)platform->drawable_height);

                // Recalculate mouse coordinates after resize
                int current_mouse_x, current_mouse_y;
                SDL_GetMouseState(&current_mouse_x, &current_mouse_y);
                transform_mouse_coordinates(
                    current_mouse_x, current_mouse_y, &input->mouse_x, &input->mouse_y, platform);
            }
        }
        else if( event.type == SDL_MOUSEMOTION )
        {
            if( !imgui_wants_mouse )
            {
                transform_mouse_coordinates(
                    event.motion.x, event.motion.y, &input->mouse_x, &input->mouse_y, platform);
            }
            else
            {
                // Mouse is over ImGui, mark as outside game area
                input->mouse_x = -1;
                input->mouse_y = -1;
            }
        }
        else if( event.type == SDL_MOUSEBUTTONDOWN )
        {
            if( !imgui_wants_mouse && event.button.button == SDL_BUTTON_LEFT )
            {
                input->mouse_button_down = 1;
                input->mouse_clicked = 1;
                transform_mouse_coordinates(
                    event.button.x,
                    event.button.y,
                    &input->mouse_clicked_x,
                    &input->mouse_clicked_y,
                    platform);
            }
            else if( !imgui_wants_mouse && event.button.button == SDL_BUTTON_RIGHT )
            {
                input->mouse_clicked_right = 1;
                transform_mouse_coordinates(
                    event.button.x,
                    event.button.y,
                    &input->mouse_clicked_right_x,
                    &input->mouse_clicked_right_y,
                    platform);
            }
        }
        else if( event.type == SDL_MOUSEBUTTONUP )
        {
            if( event.button.button == SDL_BUTTON_LEFT )
                input->mouse_button_down = 0;
        }
        else if( event.type == SDL_KEYDOWN )
        {
            Uint16 mod = SDL_GetModState();
            int shift = (mod & KMOD_SHIFT) ? 1 : 0;
            int skip_movement = chat_focused;
            switch( event.key.keysym.sym )
            {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                input->chat_key_return = 1;
                break;
            case SDLK_BACKSPACE:
                input->chat_key_backspace = 1;
                break;
            case SDLK_SPACE:
                input->chat_key_char = ' ';
                if( !skip_movement )
                    input->space_pressed = 1;
                break;
            case SDLK_ESCAPE:
                input->quit = true;
                break;
            case SDLK_UP:
                if( !skip_movement )
                    input->up_pressed = 1;
                break;
            case SDLK_DOWN:
                if( !skip_movement )
                    input->down_pressed = 1;
                break;
            case SDLK_LEFT:
                if( !skip_movement )
                    input->left_pressed = 1;
                break;
            case SDLK_RIGHT:
                if( !skip_movement )
                    input->right_pressed = 1;
                break;
            case SDLK_s:
                if( skip_movement )
                    input->chat_key_char = shift ? 'S' : 's';
                else
                    input->s_pressed = 1;
                break;
            case SDLK_w:
                if( skip_movement )
                    input->chat_key_char = shift ? 'W' : 'w';
                else
                    input->w_pressed = 1;
                break;
            case SDLK_d:
                if( skip_movement )
                    input->chat_key_char = shift ? 'D' : 'd';
                else
                    input->d_pressed = 1;
                break;
            case SDLK_a:
                if( skip_movement )
                    input->chat_key_char = shift ? 'A' : 'a';
                else
                    input->a_pressed = 1;
                break;
            case SDLK_r:
                if( skip_movement )
                    input->chat_key_char = shift ? 'R' : 'r';
                else
                    input->r_pressed = 1;
                break;
            case SDLK_f:
                if( skip_movement )
                    input->chat_key_char = shift ? 'F' : 'f';
                else
                    input->f_pressed = 1;
                break;
            case SDLK_m:
                if( skip_movement )
                    input->chat_key_char = shift ? 'M' : 'm';
                else
                    input->m_pressed = 1;
                break;
            case SDLK_n:
                if( skip_movement )
                    input->chat_key_char = shift ? 'N' : 'n';
                else
                    input->n_pressed = 1;
                break;
            case SDLK_i:
                if( skip_movement )
                    input->chat_key_char = shift ? 'I' : 'i';
                else
                    input->i_pressed = 1;
                break;
            case SDLK_k:
                if( skip_movement )
                    input->chat_key_char = shift ? 'K' : 'k';
                else
                    input->k_pressed = 1;
                break;
            case SDLK_l:
                if( skip_movement )
                    input->chat_key_char = shift ? 'L' : 'l';
                else
                    input->l_pressed = 1;
                break;
            case SDLK_j:
                if( skip_movement )
                    input->chat_key_char = shift ? 'J' : 'j';
                else
                    input->j_pressed = 1;
                break;
            case SDLK_q:
                if( skip_movement )
                    input->chat_key_char = shift ? 'Q' : 'q';
                else
                    input->q_pressed = 1;
                break;
            case SDLK_e:
                if( skip_movement )
                    input->chat_key_char = shift ? 'E' : 'e';
                else
                    input->e_pressed = 1;
                break;
            case SDLK_PERIOD:
                if( skip_movement )
                    input->chat_key_char = '.';
                else
                    input->period_pressed = 1;
                break;
            case SDLK_COMMA:
                if( skip_movement )
                    input->chat_key_char = ',';
                else
                    input->comma_pressed = 1;
                break;
            default:
            {
                SDL_Keycode sym = event.key.keysym.sym;
                if( sym >= SDLK_a && sym <= SDLK_z )
                    input->chat_key_char = shift ? (sym - 32) : sym;
                else if( sym >= SDLK_0 && sym <= SDLK_9 )
                    input->chat_key_char = sym;
                else if( sym >= 32 && sym <= 126 )
                    input->chat_key_char = sym;
                break;
            }
            }
        }
        else if( event.type == SDL_KEYUP )
        {
            switch( event.key.keysym.sym )
            {
            case SDLK_s:
                input->s_pressed = 0;
                break;
            case SDLK_w:
                input->w_pressed = 0;
                break;
            case SDLK_d:
                input->d_pressed = 0;
                break;
            case SDLK_a:
                input->a_pressed = 0;
                break;
            case SDLK_r:
                input->r_pressed = 0;
                break;
            case SDLK_f:
                input->f_pressed = 0;
                break;
            case SDLK_UP:
                input->up_pressed = 0;
                break;
            case SDLK_DOWN:
                input->down_pressed = 0;
                break;
            case SDLK_LEFT:
                input->left_pressed = 0;
                break;
            case SDLK_RIGHT:
                input->right_pressed = 0;
                break;
            case SDLK_SPACE:
                input->space_pressed = 0;
                break;
            case SDLK_m:
                input->m_pressed = 0;
                break;
            case SDLK_n:
                input->n_pressed = 0;
                break;
            case SDLK_i:
                input->i_pressed = 0;
                break;
            case SDLK_k:
                input->k_pressed = 0;
                break;
            case SDLK_l:
                input->l_pressed = 0;
                break;
            case SDLK_j:
                input->j_pressed = 0;
                break;
            case SDLK_PERIOD:
                input->period_pressed = 0;
                break;
            case SDLK_COMMA:
                input->comma_pressed = 0;
                break;
            case SDLK_q:
                input->q_pressed = 0;
                break;
            case SDLK_e:
                input->e_pressed = 0;
                break;
            }
        }
    }
}

static void
on_gio_req_init(
    struct Platform2_OSX_SDL2* platform,
    struct GIOQueue* io,
    struct GIOMessage* message)
{
    (void)platform;
    (void)io;
    (void)message;
}

static void
on_gio_req_asset(
    struct Platform2_OSX_SDL2* platform,
    struct GIOQueue* io,
    struct GIOMessage* message,
    struct CacheDat* cache_dat)
{
    (void)platform;
    if( cache_dat )
        gioqb_cache_dat_fullfill(io, cache_dat, message);
}

void
Platform2_OSX_SDL2_PollIO(
    struct Platform2_OSX_SDL2* platform,
    struct GGame* game)
{
    (void)platform;
    if( !game || !game->io || !game->cache_dat )
        return;

    struct GIOMessage message = { 0 };
    while( gioqb_read_next(game->io, &message) )
    {
        switch( message.kind )
        {
        case GIO_REQ_INIT:
            on_gio_req_init(platform, game->io, &message);
            break;
        case GIO_REQ_ASSET:
            on_gio_req_asset(platform, game->io, &message, game->cache_dat);
            break;
        default:
            assert(false && "Unknown GIO request kind");
            break;
        }
    }

    gioqb_remove_finalized(game->io);
}

static void
on_lua_async_call(
    struct Platform2_OSX_SDL2* platform,
    struct GGame* game,
    struct LuaCAsyncCall* async_call,
    struct LuaCAsyncResult* result)
{
    struct CacheDatArchive* archive = NULL;
    struct BuildCacheDat* buildcachedat = game ? game->buildcachedat : platform->buildcachedat;
    struct CacheDat* cache_dat = game ? game->cache_dat : platform->cache_dat;

    memset(result, 0, sizeof(*result));

    switch( async_call->command )
    {
    case FUNC_LOAD_ARCHIVE:
    {
        int table_id = (int)async_call->args[0];
        int archive_id = (int)async_call->args[1];
        archive = cache_dat_archive_new_load(cache_dat, table_id, archive_id);
        if( archive )
        {
            result->args[0].type = 2; // ptr
            result->args[0]._ptrarg = archive;
            result->argno = 1;
        }
        else
        {
            printf("Failed to load archive: table_id=%d, archive_id=%d\n", table_id, archive_id);
            result->argno = 0;
        }
    }
    break;
    case FUNC_STORE_CONTAINER_JAGFILE:
    {
        assert(async_call->argno >= 2);
        char* filename = (char*)async_call->args[0];
        archive = (struct CacheDatArchive*)async_call->args[1];

        struct FileListDat* filelist = filelist_dat_new_from_cache_dat_archive(archive);

        buildcachedat_set_named_jagfile(buildcachedat, filename, filelist);
    }
    break;
    case FUNC_STORE_CONTAINER_JAGFILEPACK:
        break;
    case FUNC_STORE_CONTAINER_JAGFILEPACK_INDEXED:
        break;
    case FUNC_STORE_CONTAINER_JAGFILEPACK_FROM_JAGFILE:
    {
        char const* container_name = (char const*)async_call->args[0];
        char const* source_container_name = (char const*)async_call->args[1];
        char const* filename = (char const*)async_call->args[2];

        struct FileListDat* source_filelist =
            buildcachedat_named_jagfile(buildcachedat, source_container_name);

        int filename_idx = filelist_dat_find_file_by_name(source_filelist, filename);
        if( filename_idx == -1 )
        {
            printf(
                "File '%s' not found in source container '%s' for indexed jagfilepack\n",
                filename,
                source_container_name);
            assert(false && "Source file not found in source container");
            break;
        }

        void* data_ptr = source_filelist->files[filename_idx];
        int data_size = source_filelist->file_sizes[filename_idx];

        buildcachedat_add_jagfilepack(buildcachedat, container_name, data_ptr, data_size);
    }
    break;
    case FUNC_STORE_CONTAINER_JAGFILEPACK_INDEXED_FROM_JAGFILE:
    {
        char const* container_name = (char const*)async_call->args[0];
        char const* source_container_name = (char const*)async_call->args[1];
        char const* filename = (char const*)async_call->args[2];
        char const* index_filename = (char const*)async_call->args[3];

        struct FileListDat* source_filelist =
            buildcachedat_named_jagfile(buildcachedat, source_container_name);

        int filename_idx = filelist_dat_find_file_by_name(source_filelist, filename);
        int index_idx = filelist_dat_find_file_by_name(source_filelist, index_filename);
        if( filename_idx == -1 || index_idx == -1 )
        {
            printf(
                "File '%s' not found in source container '%s' for indexed jagfilepack\n",
                filename,
                source_container_name);
            assert(false && "Source file not found in source container");
            break;
        }

        void* data_ptr = source_filelist->files[filename_idx];
        int data_size = source_filelist->file_sizes[filename_idx];
        void* index_data_ptr = source_filelist->files[index_idx];
        int index_data_size = source_filelist->file_sizes[index_idx];

        buildcachedat_add_jagfilepack_indexed(
            buildcachedat, container_name, data_ptr, data_size, index_data_ptr, index_data_size);
    }
    break;
    case FUNC_STORE_DATA_RAW:
    {
        int id = (int)async_call->args[0];
        void* ptr = (void*)async_call->args[1];
        int dtype = (int)async_call->args[2];

        struct CacheDatArchive* archive = (struct CacheDatArchive*)ptr;
        if( !archive || !archive->data )
            break;

        void* data_ptr = archive->data;
        int data_size = archive->data_size;

        switch( dtype )
        {
        case 1: /* Model */
            buildcachedat_loader_cache_model(buildcachedat, id, data_size, data_ptr);
            break;
        case 2: /* MapTerrain */
            buildcachedat_loader_cache_map_terrain(buildcachedat, 0, id, data_size, data_ptr);
            break;
        case 3: /* MapScenery */
            buildcachedat_loader_cache_map_scenery(buildcachedat, 0, id, data_size, data_ptr);
            break;
        case 12: /* FrameBlob - animbaseframes */
            buildcachedat_loader_cache_animbaseframes(buildcachedat, id, data_size, data_ptr);
            break;
        default:
            break;
        }
        cache_dat_archive_free(archive);
    }
    break;
    case FUNC_STORE_DATA_FROM_CONTAINER_WITH_ID:
    {
        if( async_call->argno < 3 )
            break;
        int id = (int)async_call->args[0];
        char const* cname = (char const*)async_call->args[1];
        int dtype = (int)async_call->args[2];

        struct BuildCacheDatContainer* cont = buildcachedat_named_container(buildcachedat, cname);
        if( !cont )
        {
            printf("STORE_DATA_FROM_CONTAINER_WITH_ID: container '%s' not found\n", cname);
            break;
        }

        if( cont->kind == BuildCacheContainerKind_JagfilePackIndexed )
        {
            struct FileListDatIndexed* idx = filelist_dat_indexed_new_from_decode(
                (char*)cont->_jagfilepack_indexed.index_data,
                cont->_jagfilepack_indexed.index_data_size,
                (char*)cont->_jagfilepack_indexed.data,
                cont->_jagfilepack_indexed.data_size);
            if( idx && id >= 0 && id < idx->offset_count )
            {
                int data_size = (id + 1 < idx->offset_count)
                                    ? (idx->offsets[id + 1] - idx->offsets[id])
                                    : (idx->data_size - idx->offsets[id]);
                char* data = idx->data + idx->offsets[id];

                if( dtype == 4 ) /* ConfigMapScenery */
                {
                    struct CacheConfigLocation* config_loc =
                        (struct CacheConfigLocation*)malloc(sizeof(struct CacheConfigLocation));
                    memset(config_loc, 0, sizeof(*config_loc));
                    config_locs_decode_inplace(config_loc, data, data_size, CONFIG_LOC_DECODE_DAT);
                    config_loc->_id = id;
                    buildcachedat_add_config_loc(buildcachedat, id, config_loc);
                }
                else if( dtype == 7 ) /* ConfigObject */
                {
                    // struct CacheDatConfigObj* obj = cache_dat_config_obj_decode(data, data_size);
                    // if( obj )
                    //     buildcachedat_add_obj(buildcachedat, id, obj);
                }
            }
            if( idx )
                filelist_dat_indexed_free(idx);
        }
        free(cont);
    }
    break;
    case FUNC_STORE_DATA_FROM_CONTAINER_WITH_IDS:
    {
        /* args: cname, dtype, then count and id list (argno-3 ids) */
        if( async_call->argno < 4 )
            break;
        char const* cname = (char const*)async_call->args[0];
        int dtype = (int)async_call->args[1];
        int count = (int)async_call->args[2];

        struct BuildCacheDatContainer* cont = buildcachedat_named_container(buildcachedat, cname);
        if( !cont )
            break;

        if( cont->kind == BuildCacheContainerKind_JagfilePackIndexed )
        {
            struct FileListDatIndexed* idx = filelist_dat_indexed_new_from_decode(
                (char*)cont->_jagfilepack_indexed.index_data,
                cont->_jagfilepack_indexed.index_data_size,
                (char*)cont->_jagfilepack_indexed.data,
                cont->_jagfilepack_indexed.data_size);
            if( idx )
            {
                for( int k = 0; k < count && (3 + k) < async_call->argno; k++ )
                {
                    int id = (int)async_call->args[3 + k];
                    if( id < 0 || id >= idx->offset_count )
                        continue;
                    int data_size = (id + 1 < idx->offset_count)
                                        ? (idx->offsets[id + 1] - idx->offsets[id])
                                        : (idx->data_size - idx->offsets[id]);
                    char* data = idx->data + idx->offsets[id];

                    if( dtype == 4 )
                    {
                        struct CacheConfigLocation* config_loc =
                            (struct CacheConfigLocation*)malloc(sizeof(struct CacheConfigLocation));
                        memset(config_loc, 0, sizeof(*config_loc));
                        config_locs_decode_inplace(
                            config_loc, data, data_size, CONFIG_LOC_DECODE_DAT);
                        config_loc->_id = id;
                        buildcachedat_add_config_loc(buildcachedat, id, config_loc);
                    }
                    else if( dtype == 7 )
                    {
                        // struct CacheDatConfigObj* obj =
                        //     cache_dat_config_obj_decode(data, data_size);
                        // if( obj )
                        //     buildcachedat_add_obj(buildcachedat, id, obj);
                    }
                }
                filelist_dat_indexed_free(idx);
            }
        }
        free(cont);
    }
    break;
    case FUNC_STORE_DATA_FROM_CONTAINER_WITH_NAME:
    {
        if( async_call->argno < 3 )
            break;
        char const* name = (char const*)async_call->args[0];
        char const* cname = (char const*)async_call->args[1];
        int dtype = (int)async_call->args[2];

        struct FileListDat* filelist = buildcachedat_named_jagfile(buildcachedat, cname);
        if( !filelist )
        {
            printf("STORE_DATA_FROM_CONTAINER_WITH_NAME: container '%s' not found\n", cname);
            break;
        }

        if( dtype == 10 ) /* TextureDefinitions */
        {
            int texture_id = 0;
            if( sscanf(name, "%d.dat", &texture_id) == 1 || sscanf(name, "%d", &texture_id) == 1 )
            {
                struct CacheDatTexture* tex =
                    cache_dat_texture_new_from_filelist_dat(filelist, texture_id, 0);
                if( tex )
                {
                    int anim_dir = 0, anim_speed = 0;
                    if( texture_id == 17 || texture_id == 24 )
                    {
                        anim_dir = 2; /* TEXANIM_DIRECTION_V_DOWN */
                        anim_speed = 2;
                    }
                    struct DashTexture* dash_tex =
                        texture_new_from_texture_sprite(tex, anim_dir, anim_speed);
                    if( dash_tex )
                    {
                        buildcachedat_add_texture(buildcachedat, texture_id, dash_tex);
                        if( game && game->sys_dash )
                            dash3d_add_texture(game->sys_dash, texture_id, dash_tex);
                    }
                    cache_dat_texture_free(tex);
                }
            }
        }
    }
    break;
    case FUNC_STORE_DATA_FROM_CONTAINER_ALL:
    {
        if( async_call->argno < 2 )
            break;
        char const* cname = (char const*)async_call->args[0];
        int dtype = (int)async_call->args[1];

        if( strcmp(cname, "config_jagfile") == 0 )
        {
            if( dtype == 9 ) /* ConfigSequence */
                buildcachedat_loader_init_sequences_from_config_jagfile(buildcachedat);
            else if( dtype == 12 ) /* FrameBlob - treat as sequences */
                buildcachedat_loader_init_sequences_from_config_jagfile(buildcachedat);
            break;
        }

        if( strcmp(cname, "config_loctypes") == 0 && dtype == 4 ) /* ConfigMapScenery */
        {
            struct BuildCacheDatContainer* cont =
                buildcachedat_named_container(buildcachedat, cname);
            if( !cont || cont->kind != BuildCacheContainerKind_JagfilePackIndexed )
            {
                if( cont )
                    free(cont);
                break;
            }
            struct FileListDatIndexed* idx = filelist_dat_indexed_new_from_decode(
                (char*)cont->_jagfilepack_indexed.index_data,
                cont->_jagfilepack_indexed.index_data_size,
                (char*)cont->_jagfilepack_indexed.data,
                cont->_jagfilepack_indexed.data_size);
            free(cont);
            if( idx )
            {
                for( int i = 0; i < idx->offset_count; i++ )
                {
                    struct CacheConfigLocation* config_loc =
                        (struct CacheConfigLocation*)malloc(sizeof(struct CacheConfigLocation));
                    memset(config_loc, 0, sizeof(*config_loc));
                    int data_size = (i + 1 < idx->offset_count)
                                        ? (idx->offsets[i + 1] - idx->offsets[i])
                                        : (idx->data_size - idx->offsets[i]);
                    config_locs_decode_inplace(
                        config_loc, idx->data + idx->offsets[i], data_size, CONFIG_LOC_DECODE_DAT);
                    config_loc->_id = i;
                    buildcachedat_add_config_loc(buildcachedat, i, config_loc);
                }
                filelist_dat_indexed_free(idx);
            }
            break;
        }

        if( strcmp(cname, "idktypes") == 0 && dtype == 8 ) /* ConfigIdKit */
        {
            struct BuildCacheDatContainer* cont =
                buildcachedat_named_container(buildcachedat, cname);
            if( !cont || cont->kind != BuildCacheContainerKind_JagfilePack )
            {
                if( cont )
                    free(cont);
                break;
            }
            struct CacheDatConfigIdkList* idk_list = cache_dat_config_idk_list_new_decode(
                cont->_jagfilepack.data, cont->_jagfilepack.data_size);
            free(cont);
            if( idk_list )
            {
                for( int i = 0; i < idk_list->idks_count; i++ )
                    buildcachedat_add_idk(buildcachedat, i, idk_list->idks[i]);
                free(idk_list->idks);
                free(idk_list);
            }
            break;
        }

        if( strcmp(cname, "objtypes") == 0 && dtype == 7 ) /* ConfigObject */
        {
            struct BuildCacheDatContainer* cont =
                buildcachedat_named_container(buildcachedat, cname);
            if( !cont || cont->kind != BuildCacheContainerKind_JagfilePackIndexed )
            {
                if( cont )
                    free(cont);
                break;
            }
            struct CacheDatConfigObjList* obj_list = cache_dat_config_obj_list_new_decode(
                (char*)cont->_jagfilepack_indexed.index_data,
                cont->_jagfilepack_indexed.index_data_size,
                (char*)cont->_jagfilepack_indexed.data,
                cont->_jagfilepack_indexed.data_size);
            free(cont);
            if( obj_list )
            {
                for( int i = 0; i < obj_list->objs_count; i++ )
                {
                    if( obj_list->objs[i] )
                        buildcachedat_add_obj(buildcachedat, i, obj_list->objs[i]);
                }
                free(obj_list->objs);
                free(obj_list);
            }
            break;
        }
    }
    break;
    case FUNC_HAS_DATA:
    {
        int dtype = (int)async_call->args[0];
        int id = (int)async_call->args[1];
        bool has = false;
        switch( dtype )
        {
        case 1: /* Model */
            has = buildcachedat_get_model(buildcachedat, id) != NULL;
            break;
        case 2: /* MapTerrain */
            has = buildcachedat_get_map_terrain(buildcachedat, (id >> 16) & 0xFFFF, id & 0xFFFF) !=
                  NULL;
            break;
        case 3: /* MapScenery */
            has =
                buildcachedat_get_scenery(buildcachedat, (id >> 16) & 0xFFFF, id & 0xFFFF) != NULL;
            break;
        case 12: /* FrameBlob - animbaseframes */
            has = buildcachedat_get_animbaseframes(buildcachedat, id) != NULL;
            break;
        default:
            break;
        }
        result->args[0].type = 4; /* bool */
        result->args[0]._barg = has;
        result->argno = 1;
    }
    break;
    case FUNC_HAS_CONT:
    {
        char const* name = (char const*)async_call->args[1];
        struct FileListDat* jag = buildcachedat_named_jagfile(buildcachedat, name);
        bool has = jag != NULL;
        result->args[0].type = 4; /* bool */
        result->args[0]._barg = has;
        result->argno = 1;
    }
    break;
    case FUNC_LIST_IDS:
        break;
    case FUNC_LIST_FIELD:
    {
        if( async_call->argno < 2 )
            break;
        int dtype = (int)async_call->args[0];
        char const* field = (char const*)async_call->args[1];

        if( dtype == 3 && strcmp(field, "model_id") == 0 ) /* MapScenery, model_id */
        {
            int* model_ids = NULL;
            int count = buildcachedat_loader_get_scenery_model_ids(buildcachedat, 0, &model_ids);
            if( count >= 0 && model_ids )
            {
                struct LuaCIntArray* arr =
                    (struct LuaCIntArray*)malloc(sizeof(struct LuaCIntArray));
                if( arr )
                {
                    arr->ids = model_ids;
                    arr->count = count;
                    result->args[0].type = 5; /* int_array */
                    result->args[0]._ptrarg = arr;
                    result->argno = 1;
                }
                else
                    free(model_ids);
            }
        }
    }
    break;
    case FUNC_LOAD_ARCHIVES:
    {
        int table_id = (int)async_call->args[0];
        int count = async_call->argno >= 2 ? (int)async_call->args[1] : 0;
        if( count <= 0 || count > 256 )
        {
            result->argno = 0;
            break;
        }
        struct CacheDatArchive** archives =
            (struct CacheDatArchive**)malloc(count * sizeof(struct CacheDatArchive*));
        if( !archives )
        {
            result->argno = 0;
            break;
        }
        int loaded = 0;
        for( int i = 0; i < count && (2 + i) < async_call->argno; i++ )
        {
            int archive_id = (int)async_call->args[2 + i];
            struct CacheDatArchive* a = NULL;

            switch( table_id )
            {
            case CACHE_DAT_MAPS:
            {
                int chunk_x, chunk_z;
                if( archive_id & 1 ) /* scenery: archive_id = map_id + 1 */
                {
                    chunk_x = (archive_id - 1) >> 16;
                    chunk_z = (archive_id - 1) & 0xFFFF;
                    a = gioqb_cache_dat_map_scenery_new_load(cache_dat, chunk_x, chunk_z);
                }
                else /* terrain: archive_id = map_id */
                {
                    chunk_x = archive_id >> 16;
                    chunk_z = archive_id & 0xFFFF;
                    a = gioqb_cache_dat_map_terrain_new_load(cache_dat, chunk_x, chunk_z);
                }
                break;
            }
            case CACHE_DAT_MODELS:
                a = gioqb_cache_dat_models_new_load(cache_dat, archive_id);
                break;
            case CACHE_DAT_ANIMATIONS:
                a = gioqb_cache_dat_animbaseframes_new_load(cache_dat, archive_id);
                break;
            case CACHE_DAT_SOUNDS:
                a = gioqb_cache_dat_sound_new_load(cache_dat, archive_id);
                break;
            case CACHE_DAT_CONFIGS:
                switch( archive_id )
                {
                case CONFIG_DAT_TITLE_AND_FONTS:
                    a = gioqb_cache_dat_config_title_new_load(cache_dat);
                    break;
                case CONFIG_DAT_CONFIGS:
                    a = gioqb_cache_dat_config_configs_new_load(cache_dat);
                    break;
                case CONFIG_DAT_INTERFACES:
                    a = gioqb_cache_dat_config_interfaces_new_load(cache_dat);
                    break;
                case CONFIG_DAT_MEDIA_2D_GRAPHICS:
                    a = gioqb_cache_dat_config_media_2d_graphics_new_load(cache_dat);
                    break;
                case CONFIG_DAT_VERSION_LIST:
                    a = gioqb_cache_dat_config_version_list_new_load(cache_dat);
                    break;
                case CONFIG_DAT_TEXTURES:
                    a = gioqb_cache_dat_config_texture_sprites_new_load(cache_dat);
                    break;
                case CONFIG_DAT_CHAT_SYSTEM:
                    // a = gioqb_cache_dat_config_chat_system_new_load(cache_dat);
                    break;
                default:
                    a = cache_dat_archive_new_load(cache_dat, table_id, archive_id);
                    break;
                }
                break;
            default:
                a = cache_dat_archive_new_load(cache_dat, table_id, archive_id);
                break;
            }
            if( a )
                archives[loaded++] = a;
        }
        if( loaded > 0 )
        {
            struct LuaCPtrArray* arr = (struct LuaCPtrArray*)malloc(sizeof(struct LuaCPtrArray));
            if( arr )
            {
                arr->ptrs = (void**)archives;
                arr->count = loaded;
                result->args[0].type = 6; /* ptr_array */
                result->args[0]._ptrarg = arr;
                result->argno = 1;
            }
            else
            {
                for( int i = 0; i < loaded; i++ )
                    cache_dat_archive_free(archives[i]);
                free(archives);
            }
        }
        else
            free(archives);
    }
    break;
    default:
    {
        printf("Unknown async command: %d\n", async_call->command);
        assert(false && "Unknown async command in on_lua_async_call");
    }
    break;
    }
}

#if LUA_VERSION_NUM >= 504
static int
lua_resume_compat(
    lua_State* co,
    lua_State* from,
    int narg)
{
    int nres = 0;
    return lua_resume(co, from, narg, &nres);
}
#else
#define lua_resume_compat lua_resume
#endif

void
Platform2_OSX_SDL2_RunLuaScripts(
    struct Platform2_OSX_SDL2* platform,
    struct GGame* game)
{
    while( !LibToriRS_LuaScriptQueueIsEmpty(game) )
    {
        struct ToriRSPlatformScript script;
        memset(&script, 0, sizeof(script));
        LibToriRS_LuaScriptQueuePop(game, &script);

        if( !platform->lua_sidecar )
            return;

        struct LuaCScriptCall script_call = { 0 };
        struct LuaCAsyncCall async_call = { 0 };
        struct LuaCAsyncResult async_result = { 0 };
        int script_status = 0;

        strcpy(script_call.name, script.name);
        for( int i = 0; i < script.argno && i < 10; i++ )
        {
            script_call.args[i].type = script.args[i].type;
            if( script.args[i].type == 0 )
                script_call.args[i]._iarg = script.args[i]._iarg;
            else if( script.args[i].type == 1 )
                script_call.args[i]._strarg = script.args[i]._strarg;
            script_call.argno += 1;
        }

        script_status = LuaCSidecar_RunScript(platform->lua_sidecar, &script_call, &async_call);
        while( script_status == LUACSIDECAR_YIELDED )
        {
            on_lua_async_call(platform, NULL, &async_call, &async_result);
            memset(&async_call, 0, sizeof(async_call));

            script_status =
                LuaCSidecar_ResumeScript(platform->lua_sidecar, &async_call, &async_result);
            memset(&async_result, 0, sizeof(async_result));
        }
    }
}