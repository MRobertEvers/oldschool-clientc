#include "varp_manager.h"

#include <stdio.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct DatCursor
{
    const uint8_t* data;
    size_t size;
    size_t pos;
};

static int
dat_g1(struct DatCursor* c)
{
    assert(c->pos < c->size);
    return c->data[c->pos++];
}

static int
dat_g2(struct DatCursor* c)
{
    assert(c->pos + 1 < c->size);
    int hi = c->data[c->pos++];
    int lo = c->data[c->pos++];
    return (hi << 8) | lo;
}

static int
dat_g4(struct DatCursor* c)
{
    assert(c->pos + 3 < c->size);
    int b0 = c->data[c->pos++];
    int b1 = c->data[c->pos++];
    int b2 = c->data[c->pos++];
    int b3 = c->data[c->pos++];
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

static void
dat_skip_cstring(struct DatCursor* c)
{
    while( c->pos < c->size )
    {
        if( c->data[c->pos++] == 0 || c->data[c->pos - 1] == '\n' )
            break;
    }
}

static void
notify_change(
    struct VarPManager* mgr,
    int varp_id)
{
    if( mgr->change_fn )
        mgr->change_fn(mgr->change_userdata, varp_id);
}

/* Server-applied update. Distinct from notify_change: only these reach the
 * widget var-transmit dispatch (see VarPManager_ServerUpdateFn). */
static void
notify_server_update(
    struct VarPManager* mgr,
    int varp_id)
{
    if( mgr->server_update_fn )
        mgr->server_update_fn(mgr->server_update_userdata, varp_id);
}

static bool
alloc_var_arrays(
    struct VarPManager* mgr,
    int count)
{
    free(mgr->var);
    free(mgr->var_serv);
    mgr->var = NULL;
    mgr->var_serv = NULL;

    if( count <= 0 )
        return true;

    mgr->var = calloc((size_t)count, sizeof(int));
    mgr->var_serv = calloc((size_t)count, sizeof(int));
    if( !mgr->var || !mgr->var_serv )
    {
        free(mgr->var);
        free(mgr->var_serv);
        mgr->var = NULL;
        mgr->var_serv = NULL;
        return false;
    }
    return true;
}

/*
 * Untyped mode: with no VarpType table loaded (dat1 boot path), the var
 * arrays grow on demand so button toggles and VARP packets still land.
 * With types loaded, ids beyond the table stay rejected as before.
 */
static bool
ensure_untyped_var_capacity(
    struct VarPManager* mgr,
    int variable)
{
    int grown;
    int* var;
    int* var_serv;

    if( variable < mgr->varp_count )
        return true;
    if( mgr->varp_types )
        return false;

    grown = mgr->varp_count == 0 ? 64 : mgr->varp_count;
    while( grown <= variable )
        grown *= 2;

    var = calloc((size_t)grown, sizeof(int));
    var_serv = calloc((size_t)grown, sizeof(int));
    if( !var || !var_serv )
    {
        free(var);
        free(var_serv);
        return false;
    }
    if( mgr->varp_count > 0 )
    {
        memcpy(var, mgr->var, (size_t)mgr->varp_count * sizeof(int));
        memcpy(var_serv, mgr->var_serv, (size_t)mgr->varp_count * sizeof(int));
    }
    free(mgr->var);
    free(mgr->var_serv);
    mgr->var = var;
    mgr->var_serv = var_serv;
    mgr->varp_count = grown;
    return true;
}

static void
apply_varp_value(
    struct VarPManager* mgr,
    int variable,
    int value)
{
    if( variable < 0 )
        return;
    if( variable >= mgr->varp_count && !ensure_untyped_var_capacity(mgr, variable) )
        return;

    mgr->var_serv[variable] = value;

    if( mgr->var[variable] != value )
    {
        mgr->var[variable] = value;
        notify_change(mgr, variable);
    }

    /* Outside the equality check on purpose: the reference records the id in its
     * changed-varp ring for every server update, so a redundant resend still
     * re-runs the hooks that list it. */
    notify_server_update(mgr, variable);
}

static void
decode_varp_type(
    struct VarPType* out,
    struct DatCursor* c)
{
    memset(out, 0, sizeof(*out));

    while( 1 )
    {
        int opcode = dat_g1(c);
        if( opcode == 0 )
            break;

        switch( opcode )
        {
        case 1:
        case 2:
            (void)dat_g1(c);
            break;
        case 3:
        case 4:
        case 6:
        case 8:
        case 11:
            break;
        case 5:
            out->clientcode = dat_g2(c);
            break;
        case 7:
            (void)dat_g4(c);
            break;
        case 10:
            dat_skip_cstring(c);
            break;
        default:
            break;
        }
    }
}

static void
decode_varbit_type(
    struct VarBitType* out,
    struct DatCursor* c)
{
    memset(out, 0, sizeof(*out));
    out->basevar = -1;

    while( 1 )
    {
        int opcode = dat_g1(c);
        if( opcode == 0 )
            break;

        switch( opcode )
        {
        case 1:
            out->basevar = dat_g2(c);
            out->startbit = dat_g1(c);
            out->endbit = dat_g1(c);
            break;
        case 10:
            dat_skip_cstring(c);
            break;
        default:
            break;
        }
    }
}

void
VarPManager_Init(struct VarPManager* mgr)
{
    assert(mgr);
    memset(mgr, 0, sizeof(*mgr));

    for( int i = 0; i < VARP_MANAGER_READBIT_MAX; i++ )
        mgr->readbit[i] = (1 << i) - 1;
}

void
VarPManager_Free(struct VarPManager* mgr)
{
    if( !mgr )
        return;

    free(mgr->varp_types);
    mgr->varp_types = NULL;
    mgr->varp_count = 0;

    free(mgr->varbit_types);
    mgr->varbit_types = NULL;
    mgr->varbit_count = 0;

    free(mgr->var);
    mgr->var = NULL;

    free(mgr->var_serv);
    mgr->var_serv = NULL;

    mgr->change_fn = NULL;
    mgr->change_userdata = NULL;
    mgr->server_update_fn = NULL;
    mgr->server_update_userdata = NULL;
}

bool
VarPManager_SetVarpTypes(
    struct VarPManager* mgr,
    const struct VarPType* types,
    int count)
{
    assert(mgr);
    assert(count >= 0);
    assert(count == 0 || types);

    free(mgr->varp_types);
    mgr->varp_types = NULL;
    mgr->varp_count = 0;

    if( count == 0 )
    {
        if( !alloc_var_arrays(mgr, 0) )
            return false;
        return true;
    }

    mgr->varp_types = malloc((size_t)count * sizeof(struct VarPType));
    if( !mgr->varp_types )
        return false;

    memcpy(mgr->varp_types, types, (size_t)count * sizeof(struct VarPType));
    mgr->varp_count = count;

    if( !alloc_var_arrays(mgr, count) )
    {
        free(mgr->varp_types);
        mgr->varp_types = NULL;
        mgr->varp_count = 0;
        return false;
    }
    return true;
}

bool
VarPManager_SetVarbitTypes(
    struct VarPManager* mgr,
    const struct VarBitType* types,
    int count)
{
    assert(mgr);
    assert(count >= 0);
    assert(count == 0 || types);

    free(mgr->varbit_types);
    mgr->varbit_types = NULL;
    mgr->varbit_count = 0;

    if( count == 0 )
        return true;

    mgr->varbit_types = malloc((size_t)count * sizeof(struct VarBitType));
    if( !mgr->varbit_types )
        return false;

    memcpy(mgr->varbit_types, types, (size_t)count * sizeof(struct VarBitType));
    mgr->varbit_count = count;
    return true;
}

bool
VarPManager_LoadVarpDat(
    struct VarPManager* mgr,
    const uint8_t* data,
    size_t size)
{
    assert(mgr);
    assert(data);

    struct DatCursor c = { .data = data, .size = size, .pos = 0 };
    int count = dat_g2(&c);

    struct VarPType* types = NULL;
    if( count > 0 )
    {
        types = calloc((size_t)count, sizeof(struct VarPType));
        if( !types )
            return false;

        for( int i = 0; i < count; i++ )
            decode_varp_type(&types[i], &c);
    }

    bool ok = VarPManager_SetVarpTypes(mgr, types, count);
    free(types);
    return ok;
}

bool
VarPManager_LoadVarbitDat(
    struct VarPManager* mgr,
    const uint8_t* data,
    size_t size)
{
    assert(mgr);
    assert(data);

    struct DatCursor c = { .data = data, .size = size, .pos = 0 };
    int count = dat_g2(&c);

    struct VarBitType* types = NULL;
    if( count > 0 )
    {
        types = calloc((size_t)count, sizeof(struct VarBitType));
        if( !types )
            return false;

        for( int i = 0; i < count; i++ )
            decode_varbit_type(&types[i], &c);
    }

    /*
     * The blob is self-describing only in its count, so a decode that stops short of
     * the end means the per-record shape is wrong — the same exact-consumption check
     * rscache uses, and the one the reference prints "varbit load mismatch" for. Warn
     * rather than fail: the types read so far are still usable.
     */
    if( c.pos != size )
        fprintf(
            stderr,
            "varbit.dat: decoded %d types but consumed %zu of %zu bytes\n",
            count,
            c.pos,
            size);

    bool ok = VarPManager_SetVarbitTypes(mgr, types, count);
    free(types);
    return ok;
}

int
VarPManager_GetVarp(
    const struct VarPManager* mgr,
    int id)
{
    assert(mgr);
    if( id < 0 || id >= mgr->varp_count )
        return 0;
    return mgr->var[id];
}

int
VarPManager_GetVarbit(
    const struct VarPManager* mgr,
    int id)
{
    assert(mgr);
    if( id < 0 || id >= mgr->varbit_count )
        return 0;

    const struct VarBitType* vb = &mgr->varbit_types[id];
    if( vb->basevar < 0 || vb->basevar >= mgr->varp_count )
        return 0;

    /*
     * `endbit` is inclusive, so the width is the difference plus one. Without the
     * +1 every varbit reads one bit too narrow and a single-bit varbit — the
     * commonest kind, and what `startbit == endbit` means — masks to zero and
     * always reads 0. The reference masks with `(1 << (msb - lsb + 1)) - 1`.
     */
    int bit_count = vb->endbit - vb->startbit + 1;
    if( bit_count <= 0 || bit_count >= VARP_MANAGER_READBIT_MAX )
        return 0;

    int mask = mgr->readbit[bit_count];
    return (mgr->var[vb->basevar] >> vb->startbit) & mask;
}

int
VarPManager_GetClientcode(
    const struct VarPManager* mgr,
    int id)
{
    assert(mgr);
    if( id < 0 || id >= mgr->varp_count )
        return 0;

    /* `varp_count` is not the length of `varp_types` in untyped mode — it is the
     * capacity of the var arrays, which ensure_untyped_var_capacity grows on the
     * first server VARP with no type table loaded (the dat1 boot: only varbit.dat
     * is read, there is no varp.dat load). An id inside that grown capacity then
     * passes the bound check above while `varp_types` is still NULL. That is a
     * null deref on every VARP packet the moment a dat1 client logs in. Untyped
     * varps have no clientcode by definition, so report none. */
    if( !mgr->varp_types )
        return 0;
    return mgr->varp_types[id].clientcode;
}

void
VarPManager_SetChangeCallback(
    struct VarPManager* mgr,
    VarPManager_ChangeFn fn,
    void* userdata)
{
    assert(mgr);
    mgr->change_fn = fn;
    mgr->change_userdata = userdata;
}

void
VarPManager_SetServerUpdateCallback(
    struct VarPManager* mgr,
    VarPManager_ServerUpdateFn fn,
    void* userdata)
{
    assert(mgr);
    mgr->server_update_fn = fn;
    mgr->server_update_userdata = userdata;
}

void
VarPManager_ApplySmall(
    struct VarPManager* mgr,
    int variable,
    int value)
{
    assert(mgr);
    apply_varp_value(mgr, variable, value);
}

void
VarPManager_ApplyLarge(
    struct VarPManager* mgr,
    int variable,
    int value)
{
    assert(mgr);
    apply_varp_value(mgr, variable, value);
}

void
VarPManager_SetVarpOptimistic(
    struct VarPManager* mgr,
    int variable,
    int value)
{
    assert(mgr);
    if( variable < 0 )
        return;
    if( variable >= mgr->varp_count && !ensure_untyped_var_capacity(mgr, variable) )
        return;

    if( mgr->var[variable] != value )
    {
        mgr->var[variable] = value;
        notify_change(mgr, variable);
    }
}

void
VarPManager_SetVarbitOptimistic(
    struct VarPManager* mgr,
    int varbit_id,
    int value)
{
    assert(mgr);
    if( varbit_id < 0 || varbit_id >= mgr->varbit_count )
        return;

    const struct VarBitType* vb = &mgr->varbit_types[varbit_id];
    if( vb->basevar < 0 || vb->basevar >= mgr->varp_count )
        return;

    /* Inclusive `endbit`, as in GetVarbit — the two must agree or a written
     * varbit reads back as a different value. */
    int bit_count = vb->endbit - vb->startbit + 1;
    if( bit_count <= 0 || bit_count >= VARP_MANAGER_READBIT_MAX )
        return;

    int mask = mgr->readbit[bit_count];
    int base_val = mgr->var[vb->basevar];
    int cleared = base_val & ~(mask << vb->startbit);
    int updated = cleared | ((value & mask) << vb->startbit);
    VarPManager_SetVarpOptimistic(mgr, vb->basevar, updated);
}

void
VarPManager_ApplySync(struct VarPManager* mgr)
{
    assert(mgr);

    for( int i = 0; i < mgr->varp_count; i++ )
    {
        if( mgr->var[i] != mgr->var_serv[i] )
        {
            mgr->var[i] = mgr->var_serv[i];
            notify_change(mgr, i);
            notify_server_update(mgr, i);
        }
    }
}

void
VarPManager_ResetAll(struct VarPManager* mgr)
{
    assert(mgr);

    for( int i = 0; i < mgr->varp_count; i++ )
    {
        mgr->var_serv[i] = 0;
        if( mgr->var[i] != 0 )
        {
            mgr->var[i] = 0;
            notify_change(mgr, i);
        }
    }

    /* Reference: the reset handler advances the changed-varp counter by the ring
     * size (`field1031 += 32`), which every widget reads as "more changes than I
     * can enumerate" and fires unconditionally. -1 is our spelling of that. */
    notify_server_update(mgr, -1);
}

int
VarPManager_ResolveTransform(
    const struct VarPManager* mgr,
    const int* transforms,
    int transform_count,
    int transform_varbit,
    int transform_varp)
{
    assert(mgr);

    if( !transforms || transform_count <= 0 )
        return -1;

    int transform_index = -1;

    if( transform_varbit != -1 )
        transform_index = VarPManager_GetVarbit(mgr, transform_varbit);
    else if( transform_varp != -1 )
        transform_index = VarPManager_GetVarp(mgr, transform_varp);

    if( transform_index >= 0 && transform_index < transform_count - 1 &&
        transforms[transform_index] != -1 )
        return transforms[transform_index];

    return transforms[transform_count - 1];
}
