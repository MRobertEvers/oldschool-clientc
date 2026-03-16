#include "platform_impl2_osx_sdl2.h"

extern "C" {
#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "osrs/buildcachedat_loader.h"
#include "osrs/filepack.h"
#include "osrs/game.h"
#include "osrs/gio.h"
#include "osrs/gio_cache_dat.h"
#include "osrs/lua_sidecar/lua_buildcachedat.h"
#include "osrs/lua_sidecar/lua_gametypes.h"
#include "osrs/lua_sidecar/luac_gametypes.h"
#include "osrs/lua_sidecar/luac_sidecar.h"
#include "osrs/lua_sidecar/luac_sidecar_cachedat.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables_dat/config_idk.h"
#include "osrs/rscache/tables_dat/config_obj.h"
#include "osrs/rscache/tables_dat/config_textures.h"
#include "osrs/rscache/tables_dat/config_versionlist_mapsquare.h"
#include "osrs/rscache/tables_dat/configs_dat.h"
#include "osrs/scripts/lua_cache_fnnos.h"
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

static struct LuaGameType*
game_callback(
    void* ctx,
    struct LuaGameType* args)
{
    struct Platform2_OSX_SDL2* platform = (struct Platform2_OSX_SDL2*)ctx;
    struct BuildCacheDat* bcd = platform->buildcachedat;

    // if( !args || LuaGameType_GetKind(args) != LUAGAMETYPE_VARTYPE_ARRAY )
    //     return NULL;
    // int n = LuaGameType_GetVarTypeArrayCount(args);
    // if( n < 1 )
    //     return NULL;

    // struct LuaGameType* func_name_gt = LuaGameType_GetVarTypeArrayAt(args, 0);
    // if( LuaGameType_GetKind(func_name_gt) != LUAGAMETYPE_STRING )
    //     return NULL;
    // const char* name = LuaGameType_GetString(func_name_gt);

    // /* call_args = VarTypeArray of args[1..n-1] (actual method args, no func name) */
    // struct LuaGameType* call_args = LuaGameType_NewVarTypeArray(n > 1 ? n - 1 : 1);
    // if( !call_args )
    //     return NULL;
    // for( int i = 1; i < n; i++ )
    //     LuaGameType_VarTypeArrayPush(call_args, LuaGameType_GetVarTypeArrayAt(args, i));

    // /* Functions needing GGame: ga = [game, args[1], args[2], ...]. ga owns elements from args.
    // */ auto build_ga_with_game = [&]() {
    //     struct LuaGameType* ga = LuaGameType_NewVarTypeArray(n);
    //     LuaGameType_VarTypeArrayPush(ga, LuaGameType_NewUserData(game));
    //     for( int i = 1; i < n; i++ )
    //         LuaGameType_VarTypeArrayPush(ga, LuaGameType_GetVarTypeArrayAt(args, i));
    //     return ga;
    // };

    // if( strcmp(name, "init_varp_varbit_from_config_jagfile") == 0 )
    // {
    //     struct LuaGameType* ga = build_ga_with_game();
    //     LuaBuildCacheDat_init_varp_varbit_from_config_jagfile(bcd, ga);
    //     LuacGameType_Free(ga);
    //     free(call_args->_var_type_array.var_types);
    //     free(call_args);
    //     return NULL;
    // }
    // if( strcmp(name, "load_component_sprites_from_media") == 0 )
    // {
    //     struct LuaGameType* ga = LuaGameType_NewVarTypeArray(1);
    //     LuaGameType_VarTypeArrayPush(ga, LuaGameType_NewUserData(game));
    //     LuaBuildCacheDat_load_component_sprites_from_media(bcd, ga);
    //     LuaGameType_Free(ga);
    //     LuaGameType_Free(call_args);
    //     return NULL;
    // }
    // if( strcmp(name, "cache_textures") == 0 )
    // {
    //     struct LuaGameType* ga = build_ga_with_game();
    //     LuaBuildCacheDat_cache_textures(bcd, ga);
    //     LuacGameType_Free(ga);
    //     free(call_args->_var_type_array.var_types);
    //     free(call_args);
    //     return NULL;
    // }
    // if( strcmp(name, "cache_media") == 0 )
    // {
    //     struct LuaGameType* ga = build_ga_with_game();
    //     LuaBuildCacheDat_cache_media(bcd, ga);
    //     LuacGameType_Free(ga);
    //     free(call_args->_var_type_array.var_types);
    //     free(call_args);
    //     return NULL;
    // }
    // if( strcmp(name, "cache_title") == 0 )
    // {
    //     struct LuaGameType* ga = build_ga_with_game();
    //     LuaBuildCacheDat_cache_title(bcd, ga);
    //     LuacGameType_Free(ga);
    //     free(call_args->_var_type_array.var_types);
    //     free(call_args);
    //     return NULL;
    // }
    // if( strcmp(name, "finalize_scene") == 0 )
    // {
    //     struct LuaGameType* ga = build_ga_with_game();
    //     LuaBuildCacheDat_finalize_scene(bcd, ga);
    //     LuacGameType_Free(ga);
    //     free(call_args->_var_type_array.var_types);
    //     free(call_args);
    //     return NULL;
    // }

    // LuaGameType_Free(call_args);
    // return NULL;
}

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

    platform->lua_sidecar = LuaCSidecar_New(platform, game_callback);
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

static struct LuaGameType*
on_lua_async_call(
    struct Platform2_OSX_SDL2* platform,
    int command,
    struct LuaGameType* args)
{
    struct CacheDat* cache_dat = platform->cache_dat;

    switch( command )
    {
    case FUNC_LOAD_ARCHIVE:
        return LuaCSidecar_CachedatLoadArchive(cache_dat, args);
    default:
        assert(false && "Unknown cachedat function");
        return NULL;
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

static struct LuaGameType*
build_args(struct ToriRSPlatformScript* script)
{
    struct LuaGameType* args = LuaGameType_NewVarTypeArray(script->argno);

    if( args )
    {
        for( int i = 0; i < script->argno && i < 10; i++ )
        {
            struct LuaGameType* arg = NULL;
            switch( script->args[i].type )
            {
            case 0:
                arg = LuaGameType_NewInt(script->args[i]._iarg);
                break;
            case 1:
                arg =
                    LuaGameType_NewString(script->args[i]._strarg, strlen(script->args[i]._strarg));
                break;
            }
            if( arg )
                LuaGameType_VarTypeArrayPush(args, arg);
        }
    }
    return args;
}

void
Platform2_OSX_SDL2_RunLuaScripts(
    struct Platform2_OSX_SDL2* platform,
    struct GGame* game)
{
    platform->current_game = game;
    while( !LibToriRS_LuaScriptQueueIsEmpty(game) )
    {
        struct ToriRSPlatformScript script;
        memset(&script, 0, sizeof(script));
        LibToriRS_LuaScriptQueuePop(game, &script);

        if( !platform->lua_sidecar )
            return;

        struct LuaCScriptCall script_call = { 0 };
        strcpy(script_call.name, script.name);
        struct LuaCYield yield = { 0 };
        int script_status = 0;

        struct LuaGameType* args = build_args(&script);

        script_status = LuaCSidecar_RunScript(platform->lua_sidecar, &script_call, args, &yield);
        LuaGameType_Free(args);

        while( script_status == LUACSIDECAR_YIELDED )
        {
            struct LuaGameType* result = on_lua_async_call(platform, yield.command, yield.args);

            script_status = LuaCSidecar_ResumeScript(platform->lua_sidecar, result, &yield);
            LuaGameType_Free(result);
        }
    }
}