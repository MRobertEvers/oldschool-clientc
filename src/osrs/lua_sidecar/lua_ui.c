#include "lua_ui.h"

#include "osrs/buildcachedat.h"
#include "osrs/game.h"
#include "osrs/lua_sidecar/lua_configfile.h"
#include "osrs/revconfig/revconfig.h"
#include "osrs/revconfig/revconfig_load.h"
#include "osrs/revconfig/static_ui_load.h"
#include "osrs/revconfig/uiscene.h"

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
    if( game->static_ui )
        game->static_ui->component_count = 0;
    if( game->ui_scene )
    {
        int cap = game->ui_scene->elements_count;
        uiscene_free(game->ui_scene);
        game->ui_scene = uiscene_new(cap > 0 ? cap : 512);
    }

    struct RevConfigBuffer* buf = revconfig_buffer_new(64);
    if( !buf )
        return LuaGameType_NewVoid();

    assert(cache_config);
    revconfig_load_fields_from_ini_bytes(cache_config->data, (uint32_t)cache_config->size, buf);

    assert(ui_config);
    revconfig_load_fields_from_ini_bytes(ui_config->data, (uint32_t)ui_config->size, buf);

    if( game->static_ui && game->ui_scene )
        static_ui_from_revconfig_buildcachedat(game->static_ui, game->ui_scene, buildcachedat, buf);

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
    while( (font = buildcachedat_iter_next_font_name(
                iter, name_buf, (int)sizeof(name_buf))) != NULL )
        uiscene_font_add(game->ui_scene, name_buf, font);
    dashmap_iter_free(iter);

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
    char command[strlen(full_command) - sizeof(g_prefix) + 1];
    strncpy(
        command, full_command + sizeof(g_prefix) - 1, strlen(full_command) - sizeof(g_prefix) + 1);
    command[strlen(full_command) - sizeof(g_prefix) + 1] = '\0';

    if( strcmp(command, "load_revconfig") == 0 )
        return LuaUI_load_revconfig(game, buildcachedat, args);
    else if( strcmp(command, "load_fonts") == 0 )
        return LuaUI_load_fonts(game, buildcachedat, args);

    printf("Unknown ui command: %s\n", command);
    assert(false);
    return NULL;
}
