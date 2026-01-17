#include "xtea_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_XTEA_KEYS 10000
struct XteaKey
{
    // These should all be archive 5. Archive 5 is the "map Index".
    int archive;
    int group;
    int name_hash;
    char name[32];
    int mapsquare;
    int32_t key[4];
};

static struct XteaKey* xtea_keys = NULL;
static int xtea_key_count = 0;

// Simple JSON parsing functions
static void
skip_whitespace(const char** json)
{
    while( **json == ' ' || **json == '\t' || **json == '\n' || **json == '\r' )
    {
        (*json)++;
    }
}

static int
parse_int(const char** json)
{
    int value = 0;
    int sign = 1;

    skip_whitespace(json);
    if( **json == '-' )
    {
        sign = -1;
        (*json)++;
    }

    while( **json >= '0' && **json <= '9' )
    {
        value = value * 10 + (**json - '0');
        (*json)++;
    }

    return value * sign;
}

static void
parse_string(const char** json, char* out, int max_len)
{
    skip_whitespace(json);
    if( **json != '"' )
        return;
    (*json)++;

    int i = 0;
    while( **json != '"' && i < max_len - 1 )
    {
        out[i++] = **json;
        (*json)++;
    }
    out[i] = '\0';
    (*json)++;
}

static void
parse_key_array(const char** json, int32_t* key)
{
    skip_whitespace(json);
    if( **json != '[' )
        return;
    (*json)++;

    for( int i = 0; i < 4; i++ )
    {
        key[i] = (int32_t)parse_int(json);
        skip_whitespace(json);
        if( **json == ',' )
            (*json)++;
    }

    if( **json == ']' )
        (*json)++;
}

static void
parse_xtea_entry(const char** json, struct XteaKey* key)
{
    skip_whitespace(json);
    if( **json != '{' )
        return;
    (*json)++;

    while( **json != '}' )
    {
        skip_whitespace(json);

        // Parse field name
        char field[32] = { 0 };
        parse_string(json, field, sizeof(field));
        skip_whitespace(json);
        if( **json == ':' )
            (*json)++;

        // Parse field value
        if( strcmp(field, "archive") == 0 )
        {
            key->archive = parse_int(json);
        }
        else if( strcmp(field, "group") == 0 )
        {
            key->group = parse_int(json);
        }
        else if( strcmp(field, "name_hash") == 0 )
        {
            key->name_hash = parse_int(json);
        }
        else if( strcmp(field, "name") == 0 )
        {
            parse_string(json, key->name, sizeof(key->name));
        }
        else if( strcmp(field, "mapsquare") == 0 )
        {
            key->mapsquare = parse_int(json);
        }
        else if( strcmp(field, "key") == 0 )
        {
            parse_key_array(json, key->key);
        }

        skip_whitespace(json);
        if( **json == ',' )
            (*json)++;
    }
    (*json)++;
}

int
xtea_config_load_keys(const char* filename)
{
    FILE* file = fopen(filename, "rb");
    if( !file )
        return -1;

    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Read file content
    char* json = malloc(size + 1);
    if( !json )
    {
        fclose(file);
        return -1;
    }

    fread(json, 1, size, file);
    json[size] = '\0';
    fclose(file);

    // Allocate space for keys
    xtea_keys = malloc(MAX_XTEA_KEYS * sizeof(struct XteaKey));
    if( !xtea_keys )
    {
        free(json);
        return -1;
    }

    // Parse JSON array
    const char* p = json;
    skip_whitespace(&p);
    if( *p != '[' )
    {
        free(json);
        free(xtea_keys);
        return -1;
    }
    p++;

    xtea_key_count = 0;
    while( *p != ']' && xtea_key_count < MAX_XTEA_KEYS )
    {
        parse_xtea_entry(&p, &xtea_keys[xtea_key_count++]);
        skip_whitespace(&p);
        if( *p == ',' )
            p++;
    }

    free(json);
    return xtea_key_count;
}

int32_t*
xtea_config_find_key(int archive, int group)
{
    for( int i = 0; i < xtea_key_count; i++ )
    {
        if( xtea_keys[i].archive == archive && xtea_keys[i].group == group )
            return xtea_keys[i].key;
    }
    return NULL;
}

void
xtea_config_cleanup_keys(void)
{
    if( xtea_keys )
    {
        free(xtea_keys);
        xtea_keys = NULL;
        xtea_key_count = 0;
    }
}