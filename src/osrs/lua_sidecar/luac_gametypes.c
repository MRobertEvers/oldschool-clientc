#include "luac_gametypes.h"

#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "lua_gametypes.h"

#include <stdlib.h>
#include <string.h>

struct LuaGameType*
LuacGameType_FromLua(
    struct lua_State* L,
    int idx)
{
    idx = lua_absindex(L, idx);
    int t = lua_type(L, idx);

    switch( t )
    {
    case LUA_TNIL:
        return LuaGameType_NewVoid();

    case LUA_TBOOLEAN:
        return LuaGameType_NewBool(lua_toboolean(L, idx) ? 1 : 0);

    case LUA_TNUMBER:
    {
        int isint = 0;
        lua_Integer i = lua_tointegerx(L, idx, &isint);
        if( isint )
            return LuaGameType_NewInt((int)i);
        return LuaGameType_NewFloat((float)lua_tonumber(L, idx));
    }

    case LUA_TSTRING:
    {
        size_t len = 0;
        const char* s = lua_tolstring(L, idx, &len);
        char* copy = malloc(len + 1);
        if( !copy )
            return NULL;
        memcpy(copy, s, len);
        copy[len] = '\0';
        struct LuaGameType* gt = LuaGameType_NewString(copy, (int)len);
        if( !gt )
            free(copy);
        return gt;
    }

    case LUA_TLIGHTUSERDATA:
        return LuaGameType_NewUserData(lua_touserdata(L, idx));

    case LUA_TTABLE:
    {
        lua_Integer n = luaL_len(L, idx);
        if( n <= 0 )
            return LuaGameType_NewVoid();

        /* Check if sequence of integers */
        int all_int = 1;
        for( lua_Integer i = 1; i <= n && all_int; i++ )
        {
            if( lua_rawgeti(L, idx, i) != LUA_TNUMBER )
                all_int = 0;
            lua_pop(L, 1);
        }
        if( all_int )
        {
            int* vals = malloc((size_t)n * sizeof(int));
            if( !vals )
                return NULL;
            for( lua_Integer i = 1; i <= n; i++ )
            {
                lua_rawgeti(L, idx, i);
                vals[i - 1] = (int)lua_tointeger(L, -1);
                lua_pop(L, 1);
            }
            struct LuaGameType* gt = LuaGameType_NewIntArray(vals, (int)n);
            if( !gt )
                free(vals);
            return gt;
        }

        /* Check if sequence of lightuserdata */
        int all_ud = 1;
        for( lua_Integer i = 1; i <= n && all_ud; i++ )
        {
            if( lua_rawgeti(L, idx, i) != LUA_TLIGHTUSERDATA )
                all_ud = 0;
            lua_pop(L, 1);
        }
        if( all_ud )
        {
            void** ptrs = malloc((size_t)n * sizeof(void*));
            if( !ptrs )
                return NULL;
            for( lua_Integer i = 1; i <= n; i++ )
            {
                lua_rawgeti(L, idx, i);
                ptrs[i - 1] = lua_touserdata(L, -1);
                lua_pop(L, 1);
            }
            struct LuaGameType* gt = LuaGameType_NewUserDataArray(ptrs, (int)n);
            if( !gt )
                free(ptrs);
            return gt;
        }

        /* Mixed sequence -> VarTypeArray */
        struct LuaGameType* arr = LuaGameType_NewVarTypeArray((int)n > 0 ? (int)n : 4);
        if( !arr )
            return NULL;
        for( lua_Integer i = 1; i <= n; i++ )
        {
            lua_rawgeti(L, idx, i);
            struct LuaGameType* elem = LuacGameType_FromLua(L, -1);
            lua_pop(L, 1);
            if( elem )
                LuaGameType_VarTypeArrayPush(arr, elem);
        }
        return arr;
    }

    default:
        return LuaGameType_NewVoid();
    }
}

void
LuacGameType_PushToLua(
    struct lua_State* L,
    struct LuaGameType* gt)
{
    if( !gt )
    {
        lua_pushnil(L);
        return;
    }

    switch( LuaGameType_GetKind(gt) )
    {
    case LUAGAMETYPE_VOID:
        lua_pushnil(L);
        break;

    case LUAGAMETYPE_BOOL:
        lua_pushboolean(L, LuaGameType_GetBool(gt) ? 1 : 0);
        break;

    case LUAGAMETYPE_INT:
        lua_pushinteger(L, LuaGameType_GetInt(gt));
        break;

    case LUAGAMETYPE_FLOAT:
        lua_pushnumber(L, LuaGameType_GetFloat(gt));
        break;

    case LUAGAMETYPE_STRING:
    {
        char* s = LuaGameType_GetString(gt);
        int len = LuaGameType_GetStringLength(gt);
        lua_pushlstring(L, s ? s : "", (size_t)len);
        break;
    }

    case LUAGAMETYPE_USERDATA:
        lua_pushlightuserdata(L, LuaGameType_GetUserData(gt));
        break;

    case LUAGAMETYPE_INT_ARRAY:
    {
        int* vals = LuaGameType_GetIntArray(gt);
        int n = LuaGameType_GetIntArrayCount(gt);
        lua_createtable(L, n, 0);
        for( int i = 0; i < n; i++ )
        {
            lua_pushinteger(L, vals[i]);
            lua_rawseti(L, -2, i + 1);
        }
        break;
    }

    case LUAGAMETYPE_USERDATA_ARRAY:
    {
        void* data = LuaGameType_GetUserDataArray(gt);
        int n = LuaGameType_GetUserDataArrayCount(gt);
        void** ptrs = (void**)data;
        lua_createtable(L, n, 0);
        for( int i = 0; i < n; i++ )
        {
            lua_pushlightuserdata(L, ptrs[i]);
            lua_rawseti(L, -2, i + 1);
        }
        break;
    }

    case LUAGAMETYPE_VARTYPE_ARRAY:
    {
        int n = LuaGameType_GetVarTypeArrayCount(gt);
        lua_createtable(L, n, 0);
        for( int i = 0; i < n; i++ )
        {
            struct LuaGameType* elem = LuaGameType_GetVarTypeArrayAt(gt, i);
            LuacGameType_PushToLua(L, elem);
            lua_rawseti(L, -2, i + 1);
        }
        break;
    }

    default:
        lua_pushnil(L);
        break;
    }
}

void
LuacGameType_Free(struct LuaGameType* gt)
{
    if( !gt )
        return;
    if( LuaGameType_GetKind(gt) == LUAGAMETYPE_STRING )
    {
        char* s = LuaGameType_GetString(gt);
        free(s);
    }
    else if( LuaGameType_GetKind(gt) == LUAGAMETYPE_INT_ARRAY )
    {
        int* vals = LuaGameType_GetIntArray(gt);
        free(vals);
    }
    else if( LuaGameType_GetKind(gt) == LUAGAMETYPE_USERDATA_ARRAY )
    {
        void* data = LuaGameType_GetUserDataArray(gt);
        free(data);
    }
    else if( LuaGameType_GetKind(gt) == LUAGAMETYPE_VARTYPE_ARRAY )
    {
        int n = LuaGameType_GetVarTypeArrayCount(gt);
        for( int i = 0; i < n; i++ )
            LuacGameType_Free(LuaGameType_GetVarTypeArrayAt(gt, i));
        free(gt->_var_type_array.var_types);
        free(gt);
        return;
    }
    LuaGameType_Free(gt);
}
