#include "lua_gametypes.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct LuaGameType*
LuaGameType_NewUserData(void* userdata)
{
    struct LuaGameType* game_type = malloc(sizeof(struct LuaGameType));
    if( !game_type )
        return NULL;
    memset(game_type, 0, sizeof(*game_type));
    game_type->kind = LUAGAMETYPE_USERDATA;
    game_type->_userdata.userdata = userdata;
    return game_type;
}

struct LuaGameType*
LuaGameType_NewUserDataArray(
    void* userdata,
    int count)
{
    struct LuaGameType* game_type = malloc(sizeof(struct LuaGameType));
    if( !game_type )
        return NULL;
    memset(game_type, 0, sizeof(*game_type));
    game_type->kind = LUAGAMETYPE_USERDATA_ARRAY;
    game_type->_userdata_array.userdata = userdata;
    game_type->_userdata_array.count = count;
    return game_type;
}

struct LuaGameType*
LuaGameType_NewIntArray(
    int* values,
    int count)
{
    struct LuaGameType* game_type = malloc(sizeof(struct LuaGameType));
    if( !game_type )
        return NULL;
    memset(game_type, 0, sizeof(*game_type));
    game_type->kind = LUAGAMETYPE_INT_ARRAY;
    game_type->_int_array.values = values;
    game_type->_int_array.count = count;
    return game_type;
}

struct LuaGameType*
LuaGameType_NewVarTypeArray(int hint)
{
    struct LuaGameType* game_type = malloc(sizeof(struct LuaGameType));
    if( !game_type )
        return NULL;
    memset(game_type, 0, sizeof(*game_type));
    game_type->kind = LUAGAMETYPE_VARTYPE_ARRAY;
    game_type->_var_type_array.var_types = malloc(sizeof(struct LuaGameType*) * hint);
    game_type->_var_type_array.count = 0;
    game_type->_var_type_array.capacity = hint;
    return game_type;
}

void
LuaGameType_VarTypeArrayPush(
    struct LuaGameType* var_type_array,
    struct LuaGameType* var_type)
{
    if( var_type_array->kind != LUAGAMETYPE_VARTYPE_ARRAY )
        return;

    if( var_type_array->_var_type_array.count >= var_type_array->_var_type_array.capacity )
    {
        var_type_array->_var_type_array.capacity *= 2;
        var_type_array->_var_type_array.var_types = realloc(
            var_type_array->_var_type_array.var_types,
            sizeof(struct LuaGameType*) * var_type_array->_var_type_array.capacity);
    }

    var_type_array->_var_type_array.var_types[var_type_array->_var_type_array.count++] = var_type;
}

int
LuaGameType_GetVarTypeArrayCount(struct LuaGameType* game_type)
{
    assert(game_type->kind == LUAGAMETYPE_VARTYPE_ARRAY);
    return game_type->_var_type_array.count;
}

struct LuaGameType*
LuaGameType_GetVarTypeArrayAt(
    struct LuaGameType* game_type,
    int index)
{
    assert(game_type->kind == LUAGAMETYPE_VARTYPE_ARRAY);
    assert(index >= 0 && index < game_type->_var_type_array.count);
    return game_type->_var_type_array.var_types[index];
}

struct LuaGameType*
LuaGameType_NewBool(bool value)
{
    struct LuaGameType* game_type = malloc(sizeof(struct LuaGameType));
    if( !game_type )
        return NULL;
    memset(game_type, 0, sizeof(*game_type));
    game_type->kind = LUAGAMETYPE_BOOL;
    game_type->_bool.value = value;
    return game_type;
}

struct LuaGameType*
LuaGameType_NewInt(int value)
{
    struct LuaGameType* game_type = malloc(sizeof(struct LuaGameType));
    if( !game_type )
        return NULL;
    memset(game_type, 0, sizeof(*game_type));
    game_type->kind = LUAGAMETYPE_INT;
    game_type->_int.value = value;
    return game_type;
}

struct LuaGameType*
LuaGameType_NewFloat(float value)
{
    struct LuaGameType* game_type = malloc(sizeof(struct LuaGameType));
    if( !game_type )
        return NULL;
    memset(game_type, 0, sizeof(*game_type));
    game_type->kind = LUAGAMETYPE_FLOAT;
    game_type->_float.value = value;
    return game_type;
}

struct LuaGameType*
LuaGameType_NewString(
    char* value,
    int length)
{
    struct LuaGameType* game_type = malloc(sizeof(struct LuaGameType));
    if( !game_type )
        return NULL;
    memset(game_type, 0, sizeof(*game_type));
    game_type->kind = LUAGAMETYPE_STRING;
    game_type->_string.value = value;
    game_type->_string.length = length;
    return game_type;
}

struct LuaGameType*
LuaGameType_NewVoid(void)
{
    struct LuaGameType* game_type = malloc(sizeof(struct LuaGameType));
    if( !game_type )
        return NULL;
    memset(game_type, 0, sizeof(*game_type));
    game_type->kind = LUAGAMETYPE_VOID;
    return game_type;
}

void
LuaGameType_Free(struct LuaGameType* game_type)
{
    if( !game_type )
        return;
    if( game_type->kind == LUAGAMETYPE_VARTYPE_ARRAY )
    {
        for( int i = 0; i < game_type->_var_type_array.count; i++ )
            LuaGameType_Free(game_type->_var_type_array.var_types[i]);
        free(game_type->_var_type_array.var_types);
    }
    free(game_type);
}

enum LuaGameTypeKind
LuaGameType_GetKind(struct LuaGameType* game_type)
{
    return game_type->kind;
}

void*
LuaGameType_GetUserData(struct LuaGameType* game_type)
{
    assert(game_type->kind == LUAGAMETYPE_USERDATA);
    return game_type->_userdata.userdata;
}

void*
LuaGameType_GetUserDataArray(struct LuaGameType* game_type)
{
    assert(game_type->kind == LUAGAMETYPE_USERDATA_ARRAY);
    return game_type->_userdata_array.userdata;
}

int
LuaGameType_GetUserDataArrayCount(struct LuaGameType* game_type)
{
    assert(game_type->kind == LUAGAMETYPE_USERDATA_ARRAY);
    return game_type->_userdata_array.count;
}

int*
LuaGameType_GetIntArray(struct LuaGameType* game_type)
{
    assert(game_type->kind == LUAGAMETYPE_INT_ARRAY);
    return game_type->_int_array.values;
}

int
LuaGameType_GetIntArrayCount(struct LuaGameType* game_type)
{
    assert(game_type->kind == LUAGAMETYPE_INT_ARRAY);
    return game_type->_int_array.count;
}

bool
LuaGameType_GetBool(struct LuaGameType* game_type)
{
    assert(game_type->kind == LUAGAMETYPE_BOOL);
    return game_type->_bool.value;
}

int
LuaGameType_GetInt(struct LuaGameType* game_type)
{
    assert(game_type->kind == LUAGAMETYPE_INT);
    return game_type->_int.value;
}

float
LuaGameType_GetFloat(struct LuaGameType* game_type)
{
    assert(game_type->kind == LUAGAMETYPE_FLOAT);
    return game_type->_float.value;
}

char*
LuaGameType_GetString(struct LuaGameType* game_type)
{
    assert(game_type->kind == LUAGAMETYPE_STRING);
    return game_type->_string.value;
}

int
LuaGameType_GetStringLength(struct LuaGameType* game_type)
{
    assert(game_type->kind == LUAGAMETYPE_STRING);
    return game_type->_string.length;
}
