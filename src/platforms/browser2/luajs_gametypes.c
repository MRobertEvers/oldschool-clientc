#include "osrs/lua_sidecar/lua_gametypes.h"

#include <emscripten.h>
#include <stdbool.h>

/* Emscripten exports for lua_gametypes. Structs are malloc'd in WASM and
 * pointers passed to JS. JS uses these functions to create, read, and free. */

EMSCRIPTEN_KEEPALIVE
struct LuaGameType*
luajs_LuaGameType_NewUserData(void* userdata)
{
    return LuaGameType_NewUserData(userdata);
}

EMSCRIPTEN_KEEPALIVE
struct LuaGameType*
luajs_LuaGameType_NewUserDataArray(int count)
{
    return LuaGameType_NewUserDataArray(count);
}

EMSCRIPTEN_KEEPALIVE
struct LuaGameType*
luajs_LuaGameType_NewUserDataArraySpread(int count)
{
    return LuaGameType_NewUserDataArraySpread(count);
}

EMSCRIPTEN_KEEPALIVE
struct LuaGameType*
luajs_LuaGameType_NewIntArray(
    int* values,
    int count)
{
    return LuaGameType_NewIntArray(values, count);
}

EMSCRIPTEN_KEEPALIVE
struct LuaGameType*
luajs_LuaGameType_NewVarTypeArray(int hint)
{
    return LuaGameType_NewVarTypeArray(hint);
}

EMSCRIPTEN_KEEPALIVE
void
luajs_LuaGameType_VarTypeArrayPush(
    struct LuaGameType* var_type_array,
    struct LuaGameType* var_type)
{
    LuaGameType_VarTypeArrayPush(var_type_array, var_type);
}

EMSCRIPTEN_KEEPALIVE
int
luajs_LuaGameType_GetVarTypeArrayCount(struct LuaGameType* game_type)
{
    return LuaGameType_GetVarTypeArrayCount(game_type);
}

EMSCRIPTEN_KEEPALIVE
struct LuaGameType*
luajs_LuaGameType_GetVarTypeArrayAt(
    struct LuaGameType* game_type,
    int index)
{
    return LuaGameType_GetVarTypeArrayAt(game_type, index);
}

EMSCRIPTEN_KEEPALIVE
struct LuaGameType*
luajs_LuaGameType_NewBool(bool value)
{
    return LuaGameType_NewBool(value);
}

EMSCRIPTEN_KEEPALIVE
struct LuaGameType*
luajs_LuaGameType_NewInt(int value)
{
    return LuaGameType_NewInt(value);
}

EMSCRIPTEN_KEEPALIVE
struct LuaGameType*
luajs_LuaGameType_NewFloat(float value)
{
    return LuaGameType_NewFloat(value);
}

EMSCRIPTEN_KEEPALIVE
struct LuaGameType*
luajs_LuaGameType_NewString(
    char* value,
    int length)
{
    return LuaGameType_NewString(value, length);
}

EMSCRIPTEN_KEEPALIVE
struct LuaGameType*
luajs_LuaGameType_NewVoid(void)
{
    return LuaGameType_NewVoid();
}

EMSCRIPTEN_KEEPALIVE
void
luajs_LuaGameType_Free(struct LuaGameType* game_type)
{
    LuaGameType_Free(game_type);
}

EMSCRIPTEN_KEEPALIVE
enum LuaGameTypeKind
luajs_LuaGameType_GetKind(struct LuaGameType* game_type)
{
    return LuaGameType_GetKind(game_type);
}

EMSCRIPTEN_KEEPALIVE
void*
luajs_LuaGameType_GetUserData(struct LuaGameType* game_type)
{
    return LuaGameType_GetUserData(game_type);
}

EMSCRIPTEN_KEEPALIVE
void*
luajs_LuaGameType_GetUserDataArray(struct LuaGameType* game_type)
{
    return LuaGameType_GetUserDataArray(game_type);
}

EMSCRIPTEN_KEEPALIVE
void*
luajs_LuaGameType_GetUserDataArrayAt(
    struct LuaGameType* game_type,
    int index)
{
    return LuaGameType_GetUserDataArrayAt(game_type, index);
}

EMSCRIPTEN_KEEPALIVE
int
luajs_LuaGameType_GetUserDataArrayCount(struct LuaGameType* game_type)
{
    return LuaGameType_GetUserDataArrayCount(game_type);
}

EMSCRIPTEN_KEEPALIVE
int*
luajs_LuaGameType_GetIntArray(struct LuaGameType* game_type)
{
    return LuaGameType_GetIntArray(game_type);
}

EMSCRIPTEN_KEEPALIVE
int
luajs_LuaGameType_GetIntArrayCount(struct LuaGameType* game_type)
{
    return LuaGameType_GetIntArrayCount(game_type);
}

EMSCRIPTEN_KEEPALIVE
bool
luajs_LuaGameType_GetBool(struct LuaGameType* game_type)
{
    return LuaGameType_GetBool(game_type);
}

EMSCRIPTEN_KEEPALIVE
int
luajs_LuaGameType_GetInt(struct LuaGameType* game_type)
{
    return LuaGameType_GetInt(game_type);
}

EMSCRIPTEN_KEEPALIVE
float
luajs_LuaGameType_GetFloat(struct LuaGameType* game_type)
{
    return LuaGameType_GetFloat(game_type);
}

EMSCRIPTEN_KEEPALIVE
char*
luajs_LuaGameType_GetString(struct LuaGameType* game_type)
{
    return LuaGameType_GetString(game_type);
}

EMSCRIPTEN_KEEPALIVE
int
luajs_LuaGameType_GetStringLength(struct LuaGameType* game_type)
{
    return LuaGameType_GetStringLength(game_type);
}
