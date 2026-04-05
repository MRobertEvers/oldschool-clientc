#include "lua_ui.h"

#include "osrs/buildcachedat.h"
#include "osrs/game.h"
#include "osrs/lua_sidecar/lua_configfile.h"
#include "osrs/revconfig/revconfig.h"
#include "osrs/revconfig/revconfig_load.h"
#include "osrs/revconfig/static_ui_load.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/rs_component_state.h"

#include <assert.h>
#include <string.h>

static char const g_prefix[] = "ui_";

/* Helper: get userdata at args[i]. */
static void*
arg_userdata(
    struct LuaGameType* args,
    int i)
{
    struct LuaGameType* elem = LuaGameType_GetVarTypeArrayAt(args, i);
    return LuaGameType_GetUserData(elem);
}

bool
LuaUI_CommandHasPrefix(const char* command)
{
    return strncmp(command, g_prefix, sizeof(g_prefix) - 1) == 0;
}

struct LuaGameType*
LuaUI_load_revconfig(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    assert(game && buildcachedat);
    assert(args);
    struct LuaConfigFile* ui_config = (struct LuaConfigFile*)arg_userdata(args, 0);
    struct LuaConfigFile* cache_config = (struct LuaConfigFile*)arg_userdata(args, 1);

    // Reset UI state (best-effort) before reloading.
    if( game->ui_scene_buffer )
        game->ui_scene_buffer->component_count = 0;
    if( game->ui_scene )
    {
        int cap = game->ui_scene->elements_count;
        int new_cap = cap > 8192 ? cap : 8192;
        uiscene_free(game->ui_scene);
        game->ui_scene = uiscene_new(new_cap);
    }

    struct RevConfigBuffer* buf = revconfig_buffer_new(64);
    if( !buf )
        return LuaGameType_NewVoid();

    assert(cache_config);
    revconfig_load_fields_from_ini_bytes(cache_config->data, (uint32_t)cache_config->size, buf);

    assert(ui_config);
    revconfig_load_fields_from_ini_bytes(ui_config->data, (uint32_t)ui_config->size, buf);

    if( game->ui_scene_buffer && game->ui_scene )
        static_ui_from_revconfig_buildcachedat(
            game->ui_scene_buffer, game->ui_scene, buildcachedat, buf);

    revconfig_buffer_free(buf);
    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaUI_load_fonts(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    struct DashMapIter* iter = buildcachedat_iter_new_fonts(buildcachedat);
    char name_buf[BUILDCACHEDAT_FONT_NAME_MAX + 1];
    struct DashPixFont* font = NULL;
    while( (font = buildcachedat_iter_next_font_name(iter, name_buf, (int)sizeof(name_buf))) !=
           NULL )
        uiscene_font_add(game->ui_scene, name_buf, font);
    dashmap_iter_free(iter);

    /* Ensure default interface fonts exist in UIScene for RS text (p11/p12/b12/q8). */
    static char const* const required_fonts[] = { "p11", "p12", "b12", "q8" };
    for( int i = 0; i < 4; i++ )
    {
        char const* nm = required_fonts[i];
        if( uiscene_font_find_id(game->ui_scene, nm) >= 0 )
            continue;
        struct DashPixFont* f = buildcachedat_get_font(buildcachedat, nm);
        if( f )
            uiscene_font_add(game->ui_scene, nm, f);
    }

    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaUI_load_rs_components(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args)
{
    (void)args;
    if( !game || !game->ui_scene || !game->ui_scene_buffer || !buildcachedat )
        return LuaGameType_NewVoid();

    if( game->rs_component_state )
    {
        rs_component_state_pool_free(game->rs_component_state);
        game->rs_component_state = NULL;
    }

    int max_id = buildcachedat_max_component_id(buildcachedat);
    static_ui_rs_from_buildcachedat(
        game->ui_scene_buffer, game->ui_scene, game->ui_scene2, buildcachedat);
    if( max_id >= 0 )
    {
        game->rs_component_state = rs_component_state_pool_new(max_id + 1);
        if( game->rs_component_state )
            rs_component_state_seed_from_buildcachedat(game->rs_component_state, buildcachedat);
    }

    return LuaGameType_NewVoid();
}

struct LuaGameType*
LuaUI_DispatchCommand(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    char* full_command,
    struct LuaGameType* args)
{
    assert(memcmp(full_command, g_prefix, sizeof(g_prefix) - 1) == 0);
    char command[256];
    size_t suffix_len = strlen(full_command) - sizeof(g_prefix) + 1;
    assert(suffix_len < sizeof(command));
    memcpy(command, full_command + sizeof(g_prefix) - 1, suffix_len);
    command[suffix_len] = '\0';

    if( strcmp(command, "load_revconfig") == 0 )
        return LuaUI_load_revconfig(game, buildcachedat, args);
    else if( strcmp(command, "load_fonts") == 0 )
        return LuaUI_load_fonts(game, buildcachedat, args);
    else if( strcmp(command, "load_rs_components") == 0 )
        return LuaUI_load_rs_components(game, buildcachedat, args);

    printf("Unknown ui command: %s\n", command);
    assert(false);
    return NULL;
}
