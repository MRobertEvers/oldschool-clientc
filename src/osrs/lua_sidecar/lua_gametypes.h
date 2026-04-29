#ifndef LUA_GAMETYPES_H
#define LUA_GAMETYPES_H

#include <stdbool.h>
#include <stdint.h>

struct LuaGameTypeUserData
{
    void* userdata;
};

struct LuaGameTypeUserDataArray
{
    void** userdata;
    int count;
    int capacity;
};

struct LuaGameTypeIntArray
{
    int* values;
    int count;
    int capacity;
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
{
    uint8_t _torirs_empty;
};

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
    LUAGAMETYPE_USERDATA_ARRAY_SPREAD = 11,
    LUAGAMETYPE_VARTYPE_ARRAY_SPREAD = 12,
};

struct LuaGameType
{
    enum LuaGameTypeKind kind;
    union
    {
        struct LuaGameTypeUserData _userdata;
        struct LuaGameTypeUserDataArray _userdata_array;
        struct LuaGameTypeUserDataArray _userdata_array_spread;
        struct LuaGameTypeIntArray _int_array;
        struct LuaGameTypeBool _bool;
        struct LuaGameTypeInt _int;
        struct LuaGameTypeFloat _float;
        struct LuaGameTypeString _string;
        struct LuaGameTypeVarTypeArray _var_type_array;
        struct LuaGameTypeVarTypeArray _var_type_array_spread;
        struct LuaGameTypeVarTypeArrayView _var_type_array_view;
    };
};

struct LuaGameType*
LuaGameType_NewUserData(void* userdata);

struct LuaGameType*
LuaGameType_NewUserDataArray(int count);

struct LuaGameType*
LuaGameType_NewUserDataArraySpread(int count);

struct LuaGameType*
LuaGameType_NewIntArray(int count);

void
LuaGameType_IntArrayPush(
    struct LuaGameType* int_array,
    int value);

struct LuaGameType*
LuaGameType_NewVarTypeArray(int hint);

struct LuaGameType*
LuaGameType_NewVarTypeArraySpread(int count);

struct LuaGameType*
LuaGameType_NewVarTypeArrayView(
    struct LuaGameType* var_types,
    int offset);

void
LuaGameType_UserDataArrayPush(
    struct LuaGameType* userdata_array,
    void* userdata);

void*
LuaGameType_GetUserDataArrayAt(
    struct LuaGameType* game_type,
    int index);

int
LuaGameType_GetUserDataArrayCount(struct LuaGameType* game_type);

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