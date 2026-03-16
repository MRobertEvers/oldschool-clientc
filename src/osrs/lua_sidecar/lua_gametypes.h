#ifndef LUA_GAMETYPES_H
#define LUA_GAMETYPES_H

#include <stdbool.h>

struct LuaGameTypeUserData
{
    void* userdata;
};

struct LuaGameTypeUserDataArray
{
    void* userdata;
    int count;
};

struct LuaGameTypeIntArray
{
    int* values;
    int count;
};

struct LuaGameTypeBool
{
    bool value;
};

struct LuaGameTypeInt
{
    int value;
};

struct LuaGameTypeFloat
{
    float value;
};

struct LuaGameTypeString
{
    char* value;
    int length;
};

struct LuaGameTypeVoid
{};

struct LuaGameTypeVarTypeArray
{
    int count;
    int capacity;
    struct LuaGameType** var_types;
};

enum LuaGameTypeKind
{
    LUAGAMETYPE_USERDATA,
    LUAGAMETYPE_USERDATA_ARRAY,
    LUAGAMETYPE_INT_ARRAY,
    LUAGAMETYPE_VARTYPE_ARRAY,
    LUAGAMETYPE_BOOL,
    LUAGAMETYPE_INT,
    LUAGAMETYPE_FLOAT,
    LUAGAMETYPE_STRING,
    LUAGAMETYPE_VOID,
};

struct LuaGameType
{
    enum LuaGameTypeKind kind;
    union
    {
        struct LuaGameTypeUserData _userdata;
        struct LuaGameTypeUserDataArray _userdata_array;
        struct LuaGameTypeIntArray _int_array;
        struct LuaGameTypeBool _bool;
        struct LuaGameTypeInt _int;
        struct LuaGameTypeFloat _float;
        struct LuaGameTypeString _string;
        struct LuaGameTypeVarTypeArray _var_type_array;
    };
};

struct LuaGameType*
LuaGameType_NewUserData(void* userdata);

struct LuaGameType*
LuaGameType_NewUserDataArray(
    void* userdata,
    int count);

struct LuaGameType*
LuaGameType_NewIntArray(
    int* values,
    int count);

struct LuaGameType*
LuaGameType_NewVarTypeArray(int hint);

struct LuaGameType*
LuaGameType_NewVarTypeArraySliceMove(
    struct LuaGameType* game_type,
    int start);

void
LuaGameType_VarTypeArrayPush(
    struct LuaGameType* var_type_array,
    struct LuaGameType* var_type);

int
LuaGameType_GetVarTypeArrayCount(struct LuaGameType* game_type);

struct LuaGameType*
LuaGameType_GetVarTypeArrayAt(
    struct LuaGameType* game_type,
    int index);

struct LuaGameType*
LuaGameType_NewBool(bool value);

struct LuaGameType*
LuaGameType_NewInt(int value);

struct LuaGameType*
LuaGameType_NewFloat(float value);

struct LuaGameType*
LuaGameType_NewString(
    char* value,
    int length);

struct LuaGameType*
LuaGameType_NewVoid(void);

void
LuaGameType_Free(struct LuaGameType* game_type);

enum LuaGameTypeKind
LuaGameType_GetKind(struct LuaGameType* game_type);

void*
LuaGameType_GetUserData(struct LuaGameType* game_type);

void*
LuaGameType_GetUserDataArray(struct LuaGameType* game_type);

int
LuaGameType_GetUserDataArrayCount(struct LuaGameType* game_type);

int*
LuaGameType_GetIntArray(struct LuaGameType* game_type);

int
LuaGameType_GetIntArrayCount(struct LuaGameType* game_type);

bool
LuaGameType_GetBool(struct LuaGameType* game_type);

int
LuaGameType_GetInt(struct LuaGameType* game_type);

float
LuaGameType_GetFloat(struct LuaGameType* game_type);

char*
LuaGameType_GetString(struct LuaGameType* game_type);

int
LuaGameType_GetStringLength(struct LuaGameType* game_type);

#endif