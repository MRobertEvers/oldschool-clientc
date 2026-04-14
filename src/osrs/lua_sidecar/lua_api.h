#ifndef LUA_API_H
#define LUA_API_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct LuaGameType;
struct GGame;
struct BuildCacheDat;
struct DashGraphics;

enum LuaApiDomain
{
    LUA_DOMAIN_BUILDCACHEDAT = 0,
    LUA_DOMAIN_GAME,
    LUA_DOMAIN_DASH,
    LUA_DOMAIN_UI,
    LUA_DOMAIN_MISC,
    LUA_DOMAIN_COUNT
};

typedef enum LuaApiId
{
    LUA_API_INVALID = 0,
#define LUA_API_X(id, full, dom, suff) id,
#include "lua_buildcachedat_api.inc"
#include "lua_game_api.inc"
#include "lua_dash_api.inc"
#include "lua_ui_api.inc"
#include "lua_sidecar_misc_api.inc"
#undef LUA_API_X
    LUA_API__COUNT
} LuaApiId;

void
lua_api_init(void);

/** Full registry string for `id` (for hash verify / debug). */
const char*
lua_api_full_string(LuaApiId id);

/** Full wire string -> id (used by init verification and tooling). */
LuaApiId
lua_api_lookup_full(
    const char* s,
    size_t n);

/** Lua method suffix within a domain (e.g. "model_cache_has") -> id. */
LuaApiId
lua_api_domain_lookup(
    enum LuaApiDomain domain,
    const char* suffix);

struct LuaGameType*
lua_api_dispatch(
    struct GGame* game,
    struct BuildCacheDat* bcd,
    struct DashGraphics* dash,
    LuaApiId id,
    struct LuaGameType* args);

#ifdef __cplusplus
}
#endif

#endif
