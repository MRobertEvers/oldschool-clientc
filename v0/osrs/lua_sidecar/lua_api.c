#include "lua_api.h"

#include "lua_buildcachedat.h"
#include "lua_dash.h"
#include "lua_game.h"
#include "lua_sidecar_misc.h"
#include "lua_ui.h"
#include "osrs/buildcachedat.h"
#include "osrs/game.h"

#include "graphics/dash.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
lua_api_ht_verify(void);

static int g_lua_api_inited;

static const char* const k_lua_api_full[LUA_API__COUNT] = {
    [LUA_API_INVALID] = "",
#define LUA_API_X(id, full, dom, suff) [id] = full,
#include "lua_buildcachedat_api.inc"
#include "lua_game_api.inc"
#include "lua_dash_api.inc"
#include "lua_ui_api.inc"
#include "lua_sidecar_misc_api.inc"
#undef LUA_API_X
};

static const char* const k_lua_api_suffix[LUA_API__COUNT] = {
    [LUA_API_INVALID] = "",
#define LUA_API_X(id, full, dom, suff) [id] = suff,
#include "lua_buildcachedat_api.inc"
#include "lua_game_api.inc"
#include "lua_dash_api.inc"
#include "lua_ui_api.inc"
#include "lua_sidecar_misc_api.inc"
#undef LUA_API_X
};

static const signed char k_lua_api_dom[LUA_API__COUNT] = {
    [LUA_API_INVALID] = -1,
#define LUA_API_X(id, full, dom, suff) [id] = (signed char)(dom),
#include "lua_buildcachedat_api.inc"
#include "lua_game_api.inc"
#include "lua_dash_api.inc"
#include "lua_ui_api.inc"
#include "lua_sidecar_misc_api.inc"
#undef LUA_API_X
};

typedef struct LuaApiDomPair
{
    const char* suffix;
    LuaApiId id;
} LuaApiDomPair;

enum
{
    LUA_API_DOM_MAX = 64
};

static LuaApiDomPair g_lua_api_dom_rows[LUA_DOMAIN_COUNT][LUA_API_DOM_MAX];
static int g_lua_api_dom_count[LUA_DOMAIN_COUNT];

static int
lua_api_dom_pair_cmp(
    const void* a,
    const void* b)
{
    const LuaApiDomPair* x = (const LuaApiDomPair*)a;
    const LuaApiDomPair* y = (const LuaApiDomPair*)b;
    return strcmp(x->suffix, y->suffix);
}

const char*
lua_api_full_string(LuaApiId id)
{
    if( (int)id <= 0 || (int)id >= LUA_API__COUNT )
        return "";
    const char* s = k_lua_api_full[id];
    return s ? s : "";
}

void
lua_api_init(void)
{
    if( g_lua_api_inited )
        return;
    g_lua_api_inited = 1;

    for( int d = 0; d < LUA_DOMAIN_COUNT; d++ )
        g_lua_api_dom_count[d] = 0;

    for( LuaApiId id = (LuaApiId)1; (int)id < LUA_API__COUNT; id = (LuaApiId)((int)id + 1) )
    {
        int d = (int)k_lua_api_dom[id];
        if( d < 0 || d >= LUA_DOMAIN_COUNT )
            continue;
        int n = g_lua_api_dom_count[d]++;
        assert(n < LUA_API_DOM_MAX);
        g_lua_api_dom_rows[d][n].suffix = k_lua_api_suffix[id];
        g_lua_api_dom_rows[d][n].id = id;
    }

    for( int d = 0; d < LUA_DOMAIN_COUNT; d++ )
    {
        if( g_lua_api_dom_count[d] > 1 )
            qsort(
                g_lua_api_dom_rows[d],
                (size_t)g_lua_api_dom_count[d],
                sizeof(LuaApiDomPair),
                lua_api_dom_pair_cmp);
    }

    lua_api_ht_verify();
}

LuaApiId
lua_api_domain_lookup(
    enum LuaApiDomain domain,
    const char* suffix)
{
    if( !suffix || suffix[0] == '\0' || (int)domain < 0 || domain >= LUA_DOMAIN_COUNT )
        return LUA_API_INVALID;

    int lo = 0;
    int hi = g_lua_api_dom_count[domain] - 1;
    const LuaApiDomPair* row = g_lua_api_dom_rows[domain];
    while( lo <= hi )
    {
        int mid = (lo + hi) / 2;
        int c = strcmp(row[mid].suffix, suffix);
        if( c == 0 )
            return row[mid].id;
        if( c < 0 )
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return LUA_API_INVALID;
}

struct LuaGameType*
lua_api_dispatch(
    struct GGame* game,
    struct BuildCacheDat* bcd,
    struct DashGraphics* dash,
    LuaApiId id,
    struct LuaGameType* args)
{
    if( !game || !bcd || (int)id <= 0 || (int)id >= LUA_API__COUNT )
        return NULL;

    switch( id )
    {
#include "lua_api_dispatch.inc"
    default:
        printf("lua_api_dispatch: unknown id %d\n", (int)id);
        assert(false);
        return NULL;
    }
}
