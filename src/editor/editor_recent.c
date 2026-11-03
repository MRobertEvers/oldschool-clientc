/*
 * The recent-squares ring and the camera scheme, with their ini codec
 * (editor_recent.h has the contract and the file's shape).
 */

#include "editor/editor_recent.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
Editor_RecentInit(struct Editor_Recent* recent)
{
    assert(recent);
    memset(recent, 0, sizeof(*recent));
}

static void
recent_key(
    int map_x,
    int map_z,
    const char* storage,
    char out[EDITOR_DOC_KEY_MAX])
{
    if( storage && storage[0] && strcmp(storage, "maps") != 0 )
        snprintf(out, EDITOR_DOC_KEY_MAX, "%s/m%d_%d", storage, map_x, map_z);
    else
        snprintf(out, EDITOR_DOC_KEY_MAX, "m%d_%d", map_x, map_z);
}

void
Editor_RecentNote(
    struct Editor_Recent* recent,
    int map_x,
    int map_z,
    const char* storage)
{
    char key[EDITOR_DOC_KEY_MAX];
    int at = recent->count;

    assert(recent);
    assert(map_x >= 0);
    assert(map_z >= 0);
    recent_key(map_x, map_z, storage, key);
    for( int i = 0; i < recent->count; i++ )
        if( strcmp(recent->keys[i], key) == 0 )
        {
            at = i;
            break;
        }
    if( at == 0 && recent->count > 0 )
        return;
    if( at == recent->count )
    {
        if( recent->count < EDITOR_RECENT_MAX )
            recent->count++;
        else
            at = EDITOR_RECENT_MAX - 1;
    }
    memmove(&recent->keys[1], &recent->keys[0], (size_t)at * sizeof(recent->keys[0]));
    snprintf(recent->keys[0], sizeof(recent->keys[0]), "%s", key);
    recent->serial++;
}

int
Editor_RecentParseKey(
    const char* key,
    int* out_map_x,
    int* out_map_z,
    char* out_storage,
    int storage_size)
{
    const char* slash;
    const char* name;
    char* end;
    long x;
    long z;

    assert(key);
    assert(out_map_x);
    assert(out_map_z);
    assert(out_storage);
    assert(storage_size > 0);
    slash = strrchr(key, '/');
    name = slash ? slash + 1 : key;
    if( name[0] != 'm' || !isdigit((unsigned char)name[1]) )
        return 0;
    x = strtol(name + 1, &end, 10);
    if( *end != '_' || !isdigit((unsigned char)end[1]) )
        return 0;
    z = strtol(end + 1, &end, 10);
    if( *end != '\0' || x < 0 || z < 0 || x > 999 || z > 999 )
        return 0;
    if( slash )
    {
        int const n = (int)(slash - key);
        if( n >= storage_size )
            return 0;
        memcpy(out_storage, key, (size_t)n);
        out_storage[n] = '\0';
    }
    else
        out_storage[0] = '\0';
    *out_map_x = (int)x;
    *out_map_z = (int)z;
    return 1;
}

int
Editor_RecentEntry(
    const struct Editor_Recent* recent,
    int index,
    int* out_map_x,
    int* out_map_z,
    char* out_storage,
    int storage_size)
{
    assert(recent);
    assert(index >= 0);
    if( index >= recent->count )
        return 0;
    return Editor_RecentParseKey(recent->keys[index], out_map_x, out_map_z, out_storage, storage_size);
}

void
Editor_RecentSetScheme(
    struct Editor_Recent* recent,
    enum Editor_CameraScheme scheme)
{
    assert(recent);
    assert((int)scheme >= 0);
    assert(scheme < EDITOR_CAMERA_SCHEME_COUNT);
    if( recent->camera_scheme == scheme )
        return;
    recent->camera_scheme = scheme;
    recent->serial++;
}

const char*
Editor_CameraSchemeName(enum Editor_CameraScheme scheme)
{
    assert((int)scheme >= 0);
    assert(scheme < EDITOR_CAMERA_SCHEME_COUNT);
    return scheme == EDITOR_CAMERA_SCHEME_UNITY ? "unity" : "default";
}

int
Editor_CameraSchemeParse(const char* text)
{
    assert(text);
    if( strcmp(text, "unity") == 0 )
        return EDITOR_CAMERA_SCHEME_UNITY;
    if( strcmp(text, "default") == 0 )
        return EDITOR_CAMERA_SCHEME_DEFAULT;
    return -1;
}

const char*
Editor_RecentPath(void)
{
    const char* configured = getenv("TORIRS_EDITOR_RECENT");

    if( !configured )
        return EDITOR_RECENT_DEFAULT_PATH;
    return *configured ? configured : NULL;
}

/* ---- the codec --------------------------------------------------------------- */

static char*
recent_trim(char* text)
{
    char* end;
    while( *text == ' ' || *text == '\t' )
        text++;
    end = text + strlen(text);
    while( end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n') )
        *--end = '\0';
    return text;
}

int
Editor_RecentDecode(
    struct Editor_Recent* recent,
    const void* data,
    int size)
{
    char const* text = data;
    int at = 0;
    int taken = 0;
    /* 0 none, 1 [recent], 2 [camera]. */
    int section = 0;

    assert(recent);
    assert(size >= 0);
    if( size > 0 )
        assert(data);
    Editor_RecentInit(recent);
    while( at < size )
    {
        char line[EDITOR_DOC_KEY_MAX + 32];
        int n = 0;
        char* body;
        char* eq;
        while( at < size && text[at] != '\n' )
        {
            if( n < (int)sizeof(line) - 1 )
                line[n++] = text[at];
            at++;
        }
        at++;
        line[n] = '\0';
        body = recent_trim(line);
        if( !body[0] || body[0] == ';' || body[0] == '#' )
            continue;
        if( body[0] == '[' )
        {
            if( strcmp(body, "[recent]") == 0 )
                section = 1;
            else if( strcmp(body, "[camera]") == 0 )
                section = 2;
            else
                section = 0;
            continue;
        }
        eq = strchr(body, '=');
        if( !eq )
            continue;
        *eq = '\0';
        body = recent_trim(body);
        eq = recent_trim(eq + 1);
        if( section == 1 && strcmp(body, "square") == 0 )
        {
            int mx;
            int mz;
            char storage[EDITOR_DOC_STORAGE_MAX];
            if( recent->count >= EDITOR_RECENT_MAX
                || !Editor_RecentParseKey(eq, &mx, &mz, storage, (int)sizeof(storage)) )
                continue;
            /* The file lists newest first; appending keeps that order. */
            snprintf(recent->keys[recent->count], sizeof(recent->keys[0]), "%s", eq);
            recent->count++;
            taken++;
        }
        else if( section == 2 && strcmp(body, "scheme") == 0 )
        {
            int const scheme = Editor_CameraSchemeParse(eq);
            if( scheme >= 0 )
                recent->camera_scheme = (enum Editor_CameraScheme)scheme;
        }
    }
    return taken;
}

void
Editor_RecentEncode(
    const struct Editor_Recent* recent,
    char** out_data,
    int* out_size)
{
    int const cap = 64 + EDITOR_RECENT_MAX * (EDITOR_DOC_KEY_MAX + 16) + 64;
    char* buffer;
    int used = 0;

    assert(recent);
    assert(out_data);
    assert(out_size);
    buffer = ToriMem_Alloc(ToriMem_Process(), (size_t)cap);
    assert(buffer);
    used += snprintf(buffer + used, (size_t)(cap - used), "[recent]\nversion = %d\n", EDITOR_RECENT_VERSION);
    for( int i = 0; i < recent->count; i++ )
        used += snprintf(buffer + used, (size_t)(cap - used), "square = %s\n", recent->keys[i]);
    used += snprintf(buffer + used, (size_t)(cap - used), "[camera]\nscheme = %s\n",
        Editor_CameraSchemeName(recent->camera_scheme));
    assert(used < cap);
    *out_data = buffer;
    *out_size = used;
}
