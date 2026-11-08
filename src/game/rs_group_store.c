#include "rs_group_store.h"

#include "log/torirs_log.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void
group_value_clear(struct RS_GroupValue* value)
{
    ToriMem_Free(ToriMem_Assets(), value->string_value);
    value->string_value = NULL;
}

static void
group_instance_free(struct RS_GroupInstance* group)
{
    if( !group )
        return;
    for( int scope = 0; scope < RS_GROUP_SCOPES; scope++ )
    {
        for( int i = 0; i < group->value_count[scope]; i++ )
            group_value_clear(&group->values[scope][i]);
        ToriMem_Free(ToriMem_Assets(), group->values[scope]);
    }
    ToriMem_Free(ToriMem_Assets(), group);
}

void
RS_GroupStore_Free(struct RS_GroupStore* store)
{
    if( !store )
        return;
    for( int i = 0; i < RS_GROUP_SLOTS; i++ )
    {
        group_instance_free(store->slots[i]);
        store->slots[i] = NULL;
    }
}

static char*
group_strdup(char const* s)
{
    size_t const n = strlen(s) + 1;
    char* copy = ToriMem_Alloc(ToriMem_Assets(), n);

    assert(copy);
    memcpy(copy, s, n);
    return copy;
}

/* deob class638's constructor: each variable at its type's zero value. */
static struct RS_GroupInstance*
group_instance_new(
    struct ToriRS_GroupType const* type,
    int index,
    int def_id,
    int64_t uid)
{
    struct RS_GroupInstance* group = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*group));

    assert(group);
    group->index = index;
    group->def_id = def_id;
    group->uid = uid;
    for( int scope = 0; scope < RS_GROUP_SCOPES; scope++ )
    {
        int const count = type->var_count[scope];

        group->value_count[scope] = count;
        if( count <= 0 )
            continue;
        group->values[scope] =
            ToriMem_Calloc(ToriMem_Assets(), (size_t)count, sizeof(*group->values[scope]));
        assert(group->values[scope]);
        for( int i = 0; i < count; i++ )
        {
            struct ToriRS_GroupTypeVar const* var = &type->vars[scope][i];
            struct RS_GroupValue* value = &group->values[scope][i];
            int const zero = (var->type_id == 0 || var->type_id == 1) ? 0 : -1;

            value->base = var->base;
            value->int_value = zero;
            value->long_value = zero;
            if( var->base == 2 )
                value->string_value = group_strdup("");
        }
    }
    return group;
}

struct GroupReader
{
    uint8_t const* data;
    int length;
    int pos;
    bool failed;
};

static int
read_u8(struct GroupReader* r)
{
    if( r->pos >= r->length )
    {
        r->failed = true;
        return 0;
    }
    return r->data[r->pos++];
}

static int
read_u16(struct GroupReader* r)
{
    int const hi = read_u8(r);

    return (hi << 8) | read_u8(r);
}

static int32_t
read_i32(struct GroupReader* r)
{
    uint32_t value = 0;

    for( int i = 0; i < 4; i++ )
        value = (value << 8) | (uint32_t)read_u8(r);
    return (int32_t)value;
}

static int64_t
read_i64(struct GroupReader* r)
{
    uint64_t value = 0;

    for( int i = 0; i < 8; i++ )
        value = (value << 8) | (uint64_t)read_u8(r);
    return (int64_t)value;
}

/* deob method13936 + method13943: a 7-bit varint, zigzag signed. */
static int32_t
read_zigzag(struct GroupReader* r)
{
    uint32_t value = 0;
    int shift = 0;

    for( ;; )
    {
        int const byte = read_u8(r);

        if( r->failed || shift > 28 )
        {
            r->failed = true;
            return 0;
        }
        value |= (uint32_t)(byte & 0x7f) << shift;
        if( !(byte & 0x80) )
            break;
        shift += 7;
    }
    return (int32_t)(value >> 1) ^ -(int32_t)(value & 1);
}

/* A string, NUL-terminated (deob method13923). */
static char*
read_string(struct GroupReader* r)
{
    int const start = r->pos;
    char* copy;
    size_t n;

    while( r->pos < r->length && r->data[r->pos] != 0 )
        r->pos++;
    if( r->pos >= r->length )
    {
        r->failed = true;
        return NULL;
    }
    n = (size_t)(r->pos - start);
    copy = ToriMem_Alloc(ToriMem_Assets(), n + 1);
    assert(copy);
    memcpy(copy, r->data + start, n);
    copy[n] = '\0';
    r->pos++;
    return copy;
}

/* deob method14184: -1 for none. */
static int
read_group_id(struct GroupReader* r)
{
    if( r->pos < r->length && r->data[r->pos] >= 128 )
        return read_u16(r) - 32769;
    return read_u8(r) - 1;
}

/* deob Statics.method11558, for one scope. */
static void
read_scope_values(
    struct GroupReader* r,
    struct RS_GroupValue* values,
    int count)
{
    for( int i = 0; i < count && !r->failed; i++ )
    {
        struct RS_GroupValue* value = &values[i];

        if( value->base == 2 )
        {
            char* s = read_string(r);

            if( s )
            {
                group_value_clear(value);
                value->string_value = s;
            }
        }
        else if( value->base == 1 )
            value->long_value = read_i64(r);
        else
            value->int_value = read_zigzag(r);
    }
}

/* deob class630.method13790. */
static void
read_slot(
    struct GroupReader* r,
    struct RS_GroupStore* store,
    struct ToriRS_GroupTypes const* types,
    int index,
    RS_GroupStore_NotifyFn notify,
    void* user)
{
    int const def_id = read_group_id(r);
    struct RS_GroupInstance* group = store->slots[index];
    bool created = false;
    int64_t uid;

    if( r->failed )
        return;
    if( def_id == -1 )
    {
        if( group )
        {
            int const old = group->def_id;

            group_instance_free(group);
            store->slots[index] = NULL;
            if( notify )
                notify(user, old, index, -1);
        }
        return;
    }
    uid = read_i64(r);
    if( r->failed )
        return;
    if( def_id < 0 || def_id >= types->count )
    {
        /* The record's width is the config's: nothing past it can be read. */
        TORIRS_LOG("group: config %d is not loaded\n", def_id);
        r->failed = true;
        return;
    }
    if( !group || group->def_id != def_id || group->uid != uid )
    {
        if( group )
        {
            int const old = group->def_id;

            group_instance_free(group);
            store->slots[index] = NULL;
            if( notify )
                notify(user, old, index, -1);
        }
        group = group_instance_new(&types->types[def_id], index, def_id, uid);
        store->slots[index] = group;
        created = true;
    }
    for( int scope = 0; scope < RS_GROUP_SCOPES; scope++ )
        read_scope_values(r, group->values[scope], group->value_count[scope]);
    if( created && notify )
        notify(user, def_id, index, uid);
}

void
RS_GroupStore_ApplyFull(
    struct RS_GroupStore* store,
    struct ToriRS_GroupTypes const* types,
    uint8_t const* data,
    int length,
    RS_GroupStore_NotifyFn notify,
    void* user)
{
    struct GroupReader r = { data, length, 0, false };

    assert(store);
    assert(types);
    assert(data);
    while( r.pos < r.length && !r.failed )
    {
        int const index = read_u8(&r);

        read_slot(&r, store, types, index, notify, user);
    }
    if( r.failed )
        TORIRS_LOG("group: GROUP_FULL stopped at byte %d of %d\n", r.pos, r.length);
}

void
RS_GroupStore_ApplyVar(
    struct RS_GroupStore* store,
    int index,
    int packed,
    int32_t int_value,
    int64_t long_value,
    char const* string_value)
{
    struct RS_GroupInstance* group;
    struct RS_GroupValue* value;
    int const def_id = (packed >> 20) & 0x7ff;
    int const scope = (packed >> 18) & 0x3;
    int const slot = packed & 0xffff;

    assert(store);
    if( index < 0 || index >= RS_GROUP_SLOTS )
        return;
    group = store->slots[index];
    if( !group || group->def_id != def_id )
        return;
    if( scope >= RS_GROUP_SCOPES || slot >= group->value_count[scope] )
    {
        TORIRS_LOG("group: var scope %d slot %d is out of range\n", scope, slot);
        return;
    }
    value = &group->values[scope][slot];
    if( value->base == 2 )
    {
        char* copy = group_strdup(string_value ? string_value : "");

        group_value_clear(value);
        value->string_value = copy;
    }
    else if( value->base == 1 )
        value->long_value = long_value;
    else
        value->int_value = int_value;
}

struct RS_GroupInstance const*
RS_GroupStore_FindUid(
    struct RS_GroupStore const* store,
    int64_t uid)
{
    assert(store);
    for( int i = 0; i < RS_GROUP_SLOTS; i++ )
        if( store->slots[i] && store->slots[i]->uid == uid )
            return store->slots[i];
    return NULL;
}
