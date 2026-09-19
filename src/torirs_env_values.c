#include "torirs_env_values.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
ToriRS_EnvNamedArg(
    char const* args,
    char const* name,
    int fallback)
{
    int value = fallback;
    size_t name_length;

    assert(name);
    name_length = strlen(name);
    while( args && *args )
    {
        char const* end = strchr(args, ',');
        size_t length = end ? (size_t)(end - args) : strlen(args);

        if( length > name_length + 1 && strncmp(args, name, name_length) == 0 &&
            args[name_length] == '=' )
        {
            char* parsed_end = NULL;
            long parsed = strtol(args + name_length + 1, &parsed_end, 0);

            if( parsed_end != args + name_length + 1 && parsed >= 0 &&
                parsed_end == args + length )
                value = (int)parsed;
        }
        args = end ? end + 1 : NULL;
    }
    return value;
}

int
ToriRS_EnvNamedArgOrEnv(
    char const* args,
    char const* name,
    char const* env_name,
    int fallback)
{
    char const* env;

    assert(name);
    assert(env_name);
    env = getenv(env_name);
    if( env )
        return (int)strtol(env, NULL, 0);
    return ToriRS_EnvNamedArg(args, name, fallback);
}

int
ToriRS_EnvChunkList(
    char const* spec,
    int* out_chunks,
    int max_pairs)
{
    int count = 0;
    char const* cursor = spec;

    assert(spec);
    assert(out_chunks);
    assert(max_pairs > 0);

    while( count < max_pairs )
    {
        int square_x = 0;
        int square_z = 0;
        int consumed = 0;

        if( sscanf(cursor, " %d , %d%n", &square_x, &square_z, &consumed) != 2 )
            return 0;
        out_chunks[count * 2] = square_x;
        out_chunks[count * 2 + 1] = square_z;
        count++;
        cursor += consumed;
        while( *cursor == ' ' )
            cursor++;
        if( *cursor != ';' )
            break;
        cursor++;
    }
    while( *cursor == ' ' )
        cursor++;
    if( *cursor != '\0' )
        return 0;
    return count;
}

bool
ToriRS_EnvIdListHas(
    char const* list,
    int id)
{
    assert(list);
    if( !*list )
        return false;
    while( *list )
    {
        char* end = NULL;
        long const parsed = strtol(list, &end, 10);

        if( end == list )
            break;
        if( parsed == id )
            return true;
        list = (*end == ',') ? end + 1 : end;
        if( !*list )
            break;
    }
    return false;
}

int
ToriRS_EnvScaleMode(char const* text)
{
    int explicit_scale;

    if( !text || text[0] == '\0' )
        return TORIRS_ENV_SCALE_AUTO;
    if( strcmp(text, "0") == 0 || strcmp(text, "off") == 0 )
        return TORIRS_ENV_SCALE_OFF;
    if( strcmp(text, "1") == 0 || strcmp(text, "auto") == 0 )
        return TORIRS_ENV_SCALE_AUTO;
    explicit_scale = atoi(text);
    return explicit_scale >= 8 ? explicit_scale : TORIRS_ENV_SCALE_AUTO;
}

int
ToriRS_EnvFovOverride(
    char const* text,
    int fov_min,
    int fov_max)
{
    int fov;

    assert(fov_min <= fov_max);
    if( !text || text[0] == '\0' || sscanf(text, "%d", &fov) != 1 || fov <= 0 )
        return -1;
    if( fov < fov_min )
        fov = fov_min;
    if( fov > fov_max )
        fov = fov_max;
    return fov;
}
