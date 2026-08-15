#include "varc_manager.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
VarCManager_Init(struct VarCManager* mgr)
{
    assert(mgr);
    memset(mgr, 0, sizeof(*mgr));
}

void
VarCManager_Free(struct VarCManager* mgr)
{
    if( !mgr )
        return;

    free(mgr->ints);
    mgr->ints = NULL;
    mgr->int_count = 0;

    for( int i = 0; i < mgr->string_count; i++ )
        free(mgr->strings[i]);
    free(mgr->strings);
    mgr->strings = NULL;
    mgr->string_count = 0;

    mgr->change_fn = NULL;
    mgr->change_userdata = NULL;
}

void
VarCManager_SetChangeCallback(
    struct VarCManager* mgr,
    VarCManager_ChangeFn fn,
    void* userdata)
{
    assert(mgr);
    mgr->change_fn = fn;
    mgr->change_userdata = userdata;
}

static void
notify_change(
    struct VarCManager* mgr,
    int varc_id,
    bool is_string)
{
    if( mgr->change_fn )
        mgr->change_fn(mgr->change_userdata, varc_id, is_string);
}

/* Grow the int store so `id` is addressable. Returns false on OOM (id stays
 * unaddressable, matching VarPManager's grow-on-demand behaviour). */
static bool
ensure_int_capacity(
    struct VarCManager* mgr,
    int id)
{
    int grown;
    int* ints;

    if( id < mgr->int_count )
        return true;

    grown = mgr->int_count == 0 ? 64 : mgr->int_count;
    while( grown <= id )
        grown *= 2;

    ints = malloc((size_t)grown * sizeof(int));
    assert(ints);
    /* RuneLite rev 239's Varcs integer getter returns -1 when the map has no
     * entry. Keep unused array slots at that sentinel so an explicit write of
     * zero remains distinguishable from an unset varc. */
    for( int i = 0; i < grown; i++ )
        ints[i] = -1;
    if( mgr->int_count > 0 )
        memcpy(ints, mgr->ints, (size_t)mgr->int_count * sizeof(int));
    free(mgr->ints);
    mgr->ints = ints;
    mgr->int_count = grown;
    return true;
}

static bool
ensure_string_capacity(
    struct VarCManager* mgr,
    int id)
{
    int grown;
    char** strings;

    if( id < mgr->string_count )
        return true;

    grown = mgr->string_count == 0 ? 64 : mgr->string_count;
    while( grown <= id )
        grown *= 2;

    strings = calloc((size_t)grown, sizeof(char*));
    assert(strings);
    if( mgr->string_count > 0 )
        memcpy(strings, mgr->strings, (size_t)mgr->string_count * sizeof(char*));
    free(mgr->strings);
    mgr->strings = strings;
    mgr->string_count = grown;
    return true;
}

int
VarCManager_GetInt(
    const struct VarCManager* mgr,
    int id)
{
    assert(mgr);
    if( id < 0 || id >= mgr->int_count )
        return -1;
    return mgr->ints[id];
}

void
VarCManager_SetInt(
    struct VarCManager* mgr,
    int id,
    int value)
{
    assert(mgr);
    if( id < 0 || !ensure_int_capacity(mgr, id) )
        return;
    if( mgr->ints[id] != value )
    {
        mgr->ints[id] = value;
        notify_change(mgr, id, false);
    }
}

const char*
VarCManager_GetString(
    const struct VarCManager* mgr,
    int id)
{
    assert(mgr);
    if( id < 0 || id >= mgr->string_count || !mgr->strings[id] )
        return "";
    return mgr->strings[id];
}

void
VarCManager_SetString(
    struct VarCManager* mgr,
    int id,
    const char* value)
{
    assert(mgr);
    if( id < 0 || !ensure_string_capacity(mgr, id) )
        return;
    if( !value )
        value = "";

    const char* current = mgr->strings[id] ? mgr->strings[id] : "";
    if( strcmp(current, value) == 0 )
        return;

    char* copy = strdup(value);
    assert(copy);
    free(mgr->strings[id]);
    mgr->strings[id] = copy;
    notify_change(mgr, id, true);
}

void
VarCManager_ResetAll(struct VarCManager* mgr)
{
    assert(mgr);

    for( int i = 0; i < mgr->int_count; i++ )
    {
        if( mgr->ints[i] != -1 )
        {
            mgr->ints[i] = -1;
            notify_change(mgr, i, false);
        }
    }
    for( int i = 0; i < mgr->string_count; i++ )
    {
        if( mgr->strings[i] && mgr->strings[i][0] != '\0' )
        {
            free(mgr->strings[i]);
            mgr->strings[i] = NULL;
            notify_change(mgr, i, true);
        }
    }
}
