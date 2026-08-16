#include "lua_platform.h"

#include <stdlib.h>

struct LuaGameScript*
LuaGameScript_New()
{
    struct LuaGameScript* script = malloc(sizeof(struct LuaGameScript));
    if( !script )
        return NULL;
    return script;
}

void
LuaGameScript_Free(struct LuaGameScript* script)
{
    if( script )
    {
        free(script);
        script = NULL;
    }
}

char*
LuaGameScript_GetName(struct LuaGameScript* game_script)
{
    return game_script->name;
}

struct LuaGameType*
LuaGameScript_GetArgs(struct LuaGameScript* game_script)
{
    return game_script->args;
}