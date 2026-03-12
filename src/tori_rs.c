#include "tori_rs.h"

#include "graphics/dash.h"
#include "graphics/lighting.h"
#include "osrs/buildcachedat.h"
#include "osrs/dash_utils.h"
#include "osrs/gameproto_parse.h"
#include "osrs/gameproto_process.h"
#include "osrs/gio.h"
#include "osrs/loginproto.h"
#include "osrs/packetbuffer.h"
#include "osrs/packetin.h"
#include "osrs/rscache/cache.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/config_versionlist_mapsquare.h"
#include "osrs/rscache/tables_dat/configs_dat.h"
#include "osrs/world_options.h"

// clang-format off
#include "osrs/gameproto_packets_write.u.c"
#include "tori_rs_input.u.c"
#include "tori_rs_net.u.c"
#include "tori_rs_cycle.u.c"
#include "tori_rs_init.u.c"
#include "tori_rs_frame.u.c"
// clang-format on

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char*
libtorirs_script_name_for_kind(enum ScriptKind kind)
{
    switch( kind )
    {
    case SCRIPT_INIT:
        return "init_cache_dat.lua";
    case SCRIPT_LOAD_SCENE_DAT:
        return "empty.lua";
    default:
        return "empty.lua";
    }
}

bool
LibToriRS_GameIsRunning(struct GGame* game)
{
    return game->running;
}

struct ScriptQueue*
LibToriRS_LuaScriptQueue(struct GGame* game)
{
    if( !game )
        return NULL;
    return &game->script_queue;
}

bool
LibToriRS_LuaScriptQueueIsEmpty(struct GGame* game)
{
    if( !game )
        return true;
    return script_queue_empty(&game->script_queue) != 0;
}

void
LibToriRS_LuaScriptQueuePop(
    struct GGame* game,
    struct ToriRSPlatformScript* out)
{
    struct ScriptQueueItem* item = NULL;
    const char* script_name = NULL;

    if( out )
        out->name[0] = '\0';

    if( !game || !out )
        return;

    item = script_queue_pop(&game->script_queue);
    if( !item )
        return;

    script_name = libtorirs_script_name_for_kind(item->args.tag);
    if( script_name )
    {
        strncpy(out->name, script_name, sizeof(out->name) - 1);
        out->name[sizeof(out->name) - 1] = '\0';
    }

    script_queue_free_item(item);
}
