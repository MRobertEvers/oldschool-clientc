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

struct LuaGameTypeVarTypeArrayView
{
    struct LuaGameType* var_types;
    int offset;
};

enum LuaGameTypeKind
{
    LUAGAMETYPE_USERDATA = 1,
    LUAGAMETYPE_USERDATA_ARRAY = 2,
    LUAGAMETYPE_INT_ARRAY = 3,
    LUAGAMETYPE_VARTYPE_ARRAY = 4,
    LUAGAMETYPE_VARTYPE_ARRAY_VIEW = 5,
    LUAGAMETYPE_BOOL = 6,
    LUAGAMETYPE_INT = 7,
    LUAGAMETYPE_FLOAT = 8,
    LUAGAMETYPE_STRING = 9,
    LUAGAMETYPE_VOID = 10,
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
        struct LuaGameTypeVarTypeArrayView _var_type_array_view;
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
LuaGameType_NewVarTypeArrayView(
    struct LuaGameType* var_types,
    int offset);

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

void
LuaGameType_Print(struct LuaGameType* game_type);

#endif