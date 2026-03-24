#include "lua_gametypes.h"

#include <assert.h>
#include <stdio.h>
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
LuaGameType_NewUserDataArray(int count)
{
    struct LuaGameType* game_type = malloc(sizeof(struct LuaGameType));
    if( !game_type )
        return NULL;
    memset(game_type, 0, sizeof(*game_type));
    game_type->kind = LUAGAMETYPE_USERDATA_ARRAY;
    game_type->_userdata_array.userdata = malloc(sizeof(void*) * count);
    game_type->_userdata_array.count = 0;
    game_type->_userdata_array.capacity = count;
    return game_type;
}

struct LuaGameType*
LuaGameType_NewUserDataArraySpread(int count)
{
    struct LuaGameType* game_type = malloc(sizeof(struct LuaGameType));
    if( !game_type )
        return NULL;
    memset(game_type, 0, sizeof(*game_type));
    game_type->kind = LUAGAMETYPE_USERDATA_ARRAY_SPREAD;
    game_type->_userdata_array_spread.userdata = malloc(sizeof(void*) * count);
    game_type->_userdata_array_spread.count = 0;
    game_type->_userdata_array_spread.capacity = count;
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

struct LuaGameType*
LuaGameType_NewVarTypeArraySpread(int count)
{
    struct LuaGameType* game_type = malloc(sizeof(struct LuaGameType));
    if( !game_type )
        return NULL;
    memset(game_type, 0, sizeof(*game_type));
    game_type->kind = LUAGAMETYPE_VARTYPE_ARRAY_SPREAD;
    game_type->_var_type_array_spread.var_types = malloc(sizeof(struct LuaGameType*) * count);
    game_type->_var_type_array_spread.count = 0;
    game_type->_var_type_array_spread.capacity = count;
    return game_type;
}

struct LuaGameType*
LuaGameType_NewVarTypeArrayView(
    struct LuaGameType* var_types,
    int offset)
{
    struct LuaGameType* game_type = malloc(sizeof(struct LuaGameType));
    if( !game_type )
        return NULL;
    memset(game_type, 0, sizeof(*game_type));
    game_type->kind = LUAGAMETYPE_VARTYPE_ARRAY_VIEW;
    game_type->_var_type_array_view.var_types = var_types;
    game_type->_var_type_array_view.offset = offset;
    return game_type;
}

void
LuaGameType_UserDataArrayPush(
    struct LuaGameType* userdata_array,
    void* userdata)
{
    switch( userdata_array->kind )
    {
    case LUAGAMETYPE_USERDATA_ARRAY:
        if( userdata_array->_userdata_array.count >= userdata_array->_userdata_array.capacity )
        {
            userdata_array->_userdata_array.capacity *= 2;
            userdata_array->_userdata_array.userdata = realloc(
                userdata_array->_userdata_array.userdata,
                sizeof(void*) * userdata_array->_userdata_array.capacity);
        }
        userdata_array->_userdata_array.userdata[userdata_array->_userdata_array.count] = userdata;
        userdata_array->_userdata_array.count++;
        break;
    case LUAGAMETYPE_USERDATA_ARRAY_SPREAD:
        userdata_array->_userdata_array_spread
            .userdata[userdata_array->_userdata_array_spread.count] = userdata;
        userdata_array->_userdata_array_spread.count++;
        break;
    default:
        assert(false);
        break;
    }
}

void*
LuaGameType_GetUserDataArrayAt(
    struct LuaGameType* game_type,
    int index)
{
    switch( game_type->kind )
    {
    case LUAGAMETYPE_USERDATA_ARRAY:
    {
        assert(index >= 0 && index < game_type->_userdata_array.count);
        return game_type->_userdata_array.userdata[index];
    }
    case LUAGAMETYPE_USERDATA_ARRAY_SPREAD:
    {
        assert(index >= 0 && index < game_type->_userdata_array_spread.count);
        return game_type->_userdata_array_spread.userdata[index];
    }
    default:
        assert(false);
        return NULL;
    }
}

int
LuaGameType_GetUserDataArrayCount(struct LuaGameType* game_type)
{
    switch( game_type->kind )
    {
    case LUAGAMETYPE_USERDATA_ARRAY:
        return game_type->_userdata_array.count;
    case LUAGAMETYPE_USERDATA_ARRAY_SPREAD:
        return game_type->_userdata_array_spread.count;
    default:
        assert(false);
        return 0;
    }
}

void
LuaGameType_VarTypeArrayPush(
    struct LuaGameType* var_type_array,
    struct LuaGameType* var_type)
{
    if( var_type_array->kind == LUAGAMETYPE_VARTYPE_ARRAY )
    {
        if( var_type_array->_var_type_array.count >= var_type_array->_var_type_array.capacity )
        {
            var_type_array->_var_type_array.capacity *= 2;
            var_type_array->_var_type_array.var_types = realloc(
                var_type_array->_var_type_array.var_types,
                sizeof(struct LuaGameType*) * var_type_array->_var_type_array.capacity);
        }
        var_type_array->_var_type_array.var_types[var_type_array->_var_type_array.count++] =
            var_type;
        return;
    }
    if( var_type_array->kind == LUAGAMETYPE_VARTYPE_ARRAY_SPREAD )
    {
        if( var_type_array->_var_type_array.count >= var_type_array->_var_type_array.capacity )
        {
            var_type_array->_var_type_array.capacity *= 2;
            var_type_array->_var_type_array.var_types = realloc(
                var_type_array->_var_type_array.var_types,
                sizeof(struct LuaGameType*) * var_type_array->_var_type_array.capacity);
        }
        var_type_array->_var_type_array_spread
            .var_types[var_type_array->_var_type_array_spread.count++] = var_type;
        return;
    }
}

int
LuaGameType_GetVarTypeArrayCount(struct LuaGameType* game_type)
{
    switch( game_type->kind )
    {
    case LUAGAMETYPE_VARTYPE_ARRAY:
        return game_type->_var_type_array.count;
    case LUAGAMETYPE_VARTYPE_ARRAY_SPREAD:
        return game_type->_var_type_array_spread.count;
    case LUAGAMETYPE_VARTYPE_ARRAY_VIEW:
        return LuaGameType_GetVarTypeArrayCount(LuaGameType_GetVarTypeArrayAt(game_type, 0)) -
               game_type->_var_type_array_view.offset;
    default:
        assert(false);
        return 0;
    }
}

struct LuaGameType*
LuaGameType_GetVarTypeArrayAt(
    struct LuaGameType* game_type,
    int index)
{
    switch( game_type->kind )
    {
    case LUAGAMETYPE_VARTYPE_ARRAY:
    {
        assert(index >= 0 && index < game_type->_var_type_array.count);
        return game_type->_var_type_array.var_types[index];
    }
    case LUAGAMETYPE_VARTYPE_ARRAY_SPREAD:
    {
        assert(index >= 0 && index < game_type->_var_type_array_spread.count);
        return game_type->_var_type_array_spread.var_types[index];
    }
    case LUAGAMETYPE_VARTYPE_ARRAY_VIEW:
        return LuaGameType_GetVarTypeArrayAt(
            game_type->_var_type_array_view.var_types,
            index + game_type->_var_type_array_view.offset);
    default:
        assert(false);
        return NULL;
    }
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
    else if( game_type->kind == LUAGAMETYPE_VARTYPE_ARRAY_SPREAD )
    {
        for( int i = 0; i < game_type->_var_type_array_spread.count; i++ )
            LuaGameType_Free(game_type->_var_type_array_spread.var_types[i]);
        free(game_type->_var_type_array_spread.var_types);
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
    switch( game_type->kind )
    {
    case LUAGAMETYPE_USERDATA_ARRAY:
        return game_type->_userdata_array.userdata;
    case LUAGAMETYPE_USERDATA_ARRAY_SPREAD:
        return game_type->_userdata_array_spread.userdata;
    default:
        assert(false);
        return NULL;
    }
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

void
LuaGameType_Print(struct LuaGameType* game_type)
{
    switch( game_type->kind )
    {
    case LUAGAMETYPE_USERDATA:
        printf("LuaGameType_UserData: %p\n", game_type->_userdata.userdata);
        break;
    case LUAGAMETYPE_INT_ARRAY:
        printf("LuaGameType_IntArray: (%d) [", game_type->_int_array.count);
        for( int i = 0; i < game_type->_int_array.count; i++ )
            printf("%d, ", game_type->_int_array.values[i]);
        printf("]\n");
        break;
    case LUAGAMETYPE_BOOL:
        printf("LuaGameType_Bool: %d\n", game_type->_bool.value);
        break;
    case LUAGAMETYPE_INT:
        printf("LuaGameType_Int: %d\n", game_type->_int.value);
        break;
    case LUAGAMETYPE_FLOAT:
        printf("LuaGameType_Float: %f\n", game_type->_float.value);
        break;
    case LUAGAMETYPE_STRING:
        printf("LuaGameType_String: %s\n", game_type->_string.value);
        printf("LuaGameType_StringLength: %d\n", game_type->_string.length);
        break;
    case LUAGAMETYPE_VOID:
        printf("LuaGameType_Void\n");
        break;
    case LUAGAMETYPE_VARTYPE_ARRAY:
        printf("LuaGameType_VarTypeArray: (%d) [\n", game_type->_var_type_array.count);
        for( int i = 0; i < game_type->_var_type_array.count; i++ )
        {
            LuaGameType_Print(game_type->_var_type_array.var_types[i]);
        }
        printf("]\n");
        break;
    case LUAGAMETYPE_VARTYPE_ARRAY_SPREAD:
        printf("LuaGameType_VarTypeArraySpread: (%d) [\n", game_type->_var_type_array_spread.count);
        for( int i = 0; i < game_type->_var_type_array_spread.count; i++ )
        {
            LuaGameType_Print(game_type->_var_type_array_spread.var_types[i]);
        }
        printf("]\n");
        break;
    case LUAGAMETYPE_USERDATA_ARRAY:
        printf("LuaGameType_UserDataArray: (%d) [", game_type->_userdata_array.count);
        for( int i = 0; i < game_type->_userdata_array.count; i++ )
            printf("%p, ", game_type->_userdata_array.userdata + i);
        printf("]\n");
        break;
    case LUAGAMETYPE_VARTYPE_ARRAY_VIEW:
        printf(
            "LuaGameType_VarTypeArrayView: (%d) [",
            LuaGameType_GetVarTypeArrayCount(LuaGameType_GetVarTypeArrayAt(game_type, 0)));
        for( int i = 0;
             i < LuaGameType_GetVarTypeArrayCount(LuaGameType_GetVarTypeArrayAt(game_type, 0));
             i++ )
            printf("%p, ", LuaGameType_GetVarTypeArrayAt(game_type, i));
        printf("]\n");
    default:
        printf("LuaGameType_Unknown: %d\n", game_type->kind);
        break;
    }
}