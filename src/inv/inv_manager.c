#include "inv_manager.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
inv_slot_clear(struct InvSlot* slot)
{
    assert(slot);
    slot->obj_id = INV_MANAGER_EMPTY_OBJ_ID;
    slot->obj_count = 0;
    slot->scene_id = INV_MANAGER_NO_SCENE_ID;
    slot->atlas_index = 0;
}

static void
inv_container_free_slots(struct InvContainer* c)
{
    assert(c);
    free(c->slots);
    c->slots = NULL;
    c->slot_count = 0;
}

static bool
inv_container_alloc_slots(struct InvContainer* c, int slot_count)
{
    assert(c);
    assert(slot_count > 0);

    struct InvSlot* slots = calloc((size_t)slot_count, sizeof(struct InvSlot));
    if( !slots )
        return false;

    for( int i = 0; i < slot_count; i++ )
        inv_slot_clear(&slots[i]);

    c->slots = slots;
    c->slot_count = slot_count;
    return true;
}

static void
inv_manager_notify(struct InvManager* mgr, int container_id)
{
    assert(mgr);
    if( mgr->change_fn )
        mgr->change_fn(mgr->change_userdata, container_id);
}

static int
inv_manager_default_container_for_name(char const* name)
{
    assert(name && name[0] != '\0');
    if( strcmp(name, INV_MANAGER_SOURCE_NAME_WORN) == 0 )
        return INV_MANAGER_CONTAINER_WORN;
    return INV_MANAGER_CONTAINER_BACKPACK;
}

static struct InvContainer*
inv_manager_get_or_create_container(
    struct InvManager* mgr,
    int inv_id,
    int slot_count)
{
    assert(mgr);
    assert(inv_id >= 0);

    for( int i = 0; i < mgr->container_count; i++ )
    {
        if( mgr->containers[i].inv_id == inv_id )
            return &mgr->containers[i];
    }

    if( mgr->container_count >= INV_MANAGER_STORE_MAX )
        return NULL;

    if( slot_count <= 0 )
        slot_count = INV_MANAGER_DEFAULT_BACKPACK_SLOTS;

    struct InvContainer* c = &mgr->containers[mgr->container_count];
    memset(c, 0, sizeof(*c));
    c->inv_id = inv_id;
    if( !inv_container_alloc_slots(c, slot_count) )
        return NULL;

    mgr->container_count++;
    return c;
}

void
InvManager_Init(struct InvManager* mgr)
{
    assert(mgr);
    memset(mgr, 0, sizeof(*mgr));
    mgr->selection.source_id = INV_MANAGER_SOURCE_INVALID;
    mgr->selection.slot = INV_MANAGER_NO_SELECTION;
}

void
InvManager_Free(struct InvManager* mgr)
{
    assert(mgr);
    for( int i = 0; i < mgr->container_count; i++ )
        inv_container_free_slots(&mgr->containers[i]);
    memset(mgr, 0, sizeof(*mgr));
    mgr->selection.source_id = INV_MANAGER_SOURCE_INVALID;
    mgr->selection.slot = INV_MANAGER_NO_SELECTION;
}

void
InvManager_SetChangeCallback(
    struct InvManager* mgr,
    InvManager_ChangeFn fn,
    void* userdata)
{
    assert(mgr);
    mgr->change_fn = fn;
    mgr->change_userdata = userdata;
}

int
InvManager_ResolveSource(
    struct InvManager* mgr,
    char const* name)
{
    assert(mgr);
    assert(name);
    assert(name[0] != '\0');

    for( int i = 0; i < mgr->source_count; i++ )
    {
        if( mgr->sources[i].used && strcmp(mgr->sources[i].name, name) == 0 )
            return i;
    }

    if( mgr->source_count >= INV_MANAGER_SOURCE_MAX )
        return INV_MANAGER_SOURCE_INVALID;

    int const idx = mgr->source_count++;
    struct InvSource* src = &mgr->sources[idx];
    memset(src, 0, sizeof(*src));
    strncpy(src->name, name, sizeof(src->name) - 1);
    src->container_id = inv_manager_default_container_for_name(name);
    src->slot_count = src->container_id == INV_MANAGER_CONTAINER_WORN
                          ? INV_MANAGER_DEFAULT_WORN_SLOTS
                          : INV_MANAGER_DEFAULT_BACKPACK_SLOTS;
    src->used = true;

    if( !inv_manager_get_or_create_container(mgr, src->container_id, src->slot_count) )
    {
        src->used = false;
        mgr->source_count--;
        return INV_MANAGER_SOURCE_INVALID;
    }
    return idx;
}

int
InvManager_EnsureContainer(
    struct InvManager* mgr,
    int container_id,
    int slot_count,
    char const* display_name)
{
    assert(mgr);
    if( container_id < 0 )
        return INV_MANAGER_SOURCE_INVALID;

    for( int i = 0; i < mgr->source_count; i++ )
    {
        if( mgr->sources[i].used && mgr->sources[i].container_id == container_id )
            return i;
    }

    if( mgr->source_count >= INV_MANAGER_SOURCE_MAX )
        return INV_MANAGER_SOURCE_INVALID;

    int const idx = mgr->source_count++;
    struct InvSource* src = &mgr->sources[idx];
    memset(src, 0, sizeof(*src));
    src->container_id = container_id;
    src->slot_count =
        slot_count > 0 ? slot_count : INV_MANAGER_DEFAULT_BACKPACK_SLOTS;
    src->used = true;
    if( display_name && display_name[0] != '\0' )
        strncpy(src->name, display_name, sizeof(src->name) - 1);
    else
        snprintf(src->name, sizeof(src->name), "inv_%d", container_id);

    if( !inv_manager_get_or_create_container(mgr, container_id, src->slot_count) )
    {
        src->used = false;
        mgr->source_count--;
        return INV_MANAGER_SOURCE_INVALID;
    }
    return idx;
}

/* Resolve an "inv source" reference to a container id. The RevConfig / named
 * bind path stores a source *index* into sources[]; the component-pack build
 * path (uitree_build.c UIBUILD_INV) stores the raw component id, which equals
 * the container id the server addresses in UPDATE_INV_FULL/PARTIAL. Source
 * indices are small (< source_count, < INV_MANAGER_SOURCE_MAX) and never
 * collide with the >=93 server container ids, so trying the index first and
 * falling back to a direct container lookup is unambiguous. Returns
 * INV_MANAGER_CONTAINER_NONE when neither resolves. */
static int
inv_resolve_container_id(
    struct InvManager const* mgr,
    int source_id)
{
    if( source_id >= 0 && source_id < mgr->source_count && mgr->sources[source_id].used )
        return mgr->sources[source_id].container_id;
    if( InvManager_FindContainer(mgr, source_id) )
        return source_id;
    return INV_MANAGER_CONTAINER_NONE;
}

int
InvManager_ContainerForSource(
    struct InvManager const* mgr,
    int source_id)
{
    assert(mgr);
    return inv_resolve_container_id(mgr, source_id);
}

struct InvContainer*
InvManager_GetContainer(
    struct InvManager* mgr,
    int container_id)
{
    assert(mgr);
    if( container_id < 0 )
        return NULL;
    for( int i = 0; i < mgr->container_count; i++ )
    {
        if( mgr->containers[i].inv_id == container_id )
            return &mgr->containers[i];
    }
    return NULL;
}

struct InvContainer const*
InvManager_FindContainer(
    struct InvManager const* mgr,
    int container_id)
{
    assert(mgr);
    if( container_id < 0 )
        return NULL;
    for( int i = 0; i < mgr->container_count; i++ )
    {
        if( mgr->containers[i].inv_id == container_id )
            return &mgr->containers[i];
    }
    return NULL;
}

bool
InvManager_GetSlot(
    struct InvManager const* mgr,
    int source_id,
    int slot,
    struct InvSlot* out)
{
    assert(mgr);
    assert(out);
    inv_slot_clear(out);

    if( slot < 0 )
        return false;
    int const container_id = inv_resolve_container_id(mgr, source_id);
    if( container_id == INV_MANAGER_CONTAINER_NONE )
        return false;
    struct InvContainer const* container =
        InvManager_FindContainer(mgr, container_id);
    assert(container);
    assert(container->slots);
    if( slot >= container->slot_count )
        return false;

    *out = container->slots[slot];
    return true;
}

bool
InvManager_SetSlot(
    struct InvManager* mgr,
    int source_id,
    int slot,
    struct InvSlot const* data)
{
    assert(mgr);
    assert(data);
    if( slot < 0 )
        return false;
    int const container_id = inv_resolve_container_id(mgr, source_id);
    if( container_id == INV_MANAGER_CONTAINER_NONE )
        return false;
    struct InvContainer* container = InvManager_GetContainer(mgr, container_id);
    assert(container);
    assert(container->slots);
    if( slot >= container->slot_count )
        return false;

    int obj_id = data->obj_id;
    int obj_count = data->obj_count;
    if( obj_id <= INV_MANAGER_EMPTY_OBJ_ID )
    {
        inv_slot_clear(&container->slots[slot]);
    }
    else
    {
        container->slots[slot].obj_id = obj_id;
        container->slots[slot].obj_count = obj_count > 0 ? obj_count : 1;
        container->slots[slot].scene_id = data->scene_id;
        container->slots[slot].atlas_index = data->atlas_index;
    }

    if( mgr->selection.source_id == source_id && mgr->selection.slot == slot &&
        container->slots[slot].obj_id <= INV_MANAGER_EMPTY_OBJ_ID )
    {
        InvManager_SelectionClear(mgr);
    }

    inv_manager_notify(mgr, container_id);
    return true;
}

bool
InvManager_ClearSlot(
    struct InvManager* mgr,
    int source_id,
    int slot)
{
    struct InvSlot empty;
    inv_slot_clear(&empty);
    return InvManager_SetSlot(mgr, source_id, slot, &empty);
}

bool
InvManager_ApplyFull(
    struct InvManager* mgr,
    int container_id,
    int const* obj_ids,
    int const* obj_counts,
    int count)
{
    assert(mgr);
    struct InvContainer* container = InvManager_GetContainer(mgr, container_id);
    if( !container )
        return false;
    assert(container->slots);

    if( count < 0 )
        count = 0;
    if( count > container->slot_count )
        count = container->slot_count;

    for( int i = 0; i < count; i++ )
    {
        int const oid = obj_ids ? obj_ids[i] : INV_MANAGER_EMPTY_OBJ_ID;
        int const oc = obj_counts ? obj_counts[i] : 0;
        if( oid > INV_MANAGER_EMPTY_OBJ_ID )
        {
            container->slots[i].obj_id = oid;
            container->slots[i].obj_count = oc > 0 ? oc : 1;
            container->slots[i].scene_id = INV_MANAGER_NO_SCENE_ID;
            container->slots[i].atlas_index = 0;
        }
        else
        {
            inv_slot_clear(&container->slots[i]);
        }
    }
    for( int i = count; i < container->slot_count; i++ )
        inv_slot_clear(&container->slots[i]);

    if( mgr->selection.source_id >= 0 &&
        mgr->selection.source_id < mgr->source_count &&
        mgr->sources[mgr->selection.source_id].used &&
        mgr->sources[mgr->selection.source_id].container_id == container_id )
    {
        int const sel = mgr->selection.slot;
        if( sel < 0 || sel >= container->slot_count ||
            container->slots[sel].obj_id <= INV_MANAGER_EMPTY_OBJ_ID )
        {
            InvManager_SelectionClear(mgr);
        }
    }

    inv_manager_notify(mgr, container_id);
    return true;
}

bool
InvManager_ApplyPartial(
    struct InvManager* mgr,
    int container_id,
    int const* slots,
    int const* obj_ids,
    int const* obj_counts,
    int count)
{
    assert(mgr);
    if( !slots || count <= 0 )
        return false;

    struct InvContainer* container = InvManager_GetContainer(mgr, container_id);
    if( !container )
        return false;
    assert(container->slots);

    for( int i = 0; i < count; i++ )
    {
        int const slot = slots[i];
        if( slot < 0 || slot >= container->slot_count )
            continue;
        int const oid = obj_ids ? obj_ids[i] : INV_MANAGER_EMPTY_OBJ_ID;
        int const oc = obj_counts ? obj_counts[i] : 0;
        if( oid > INV_MANAGER_EMPTY_OBJ_ID )
        {
            container->slots[slot].obj_id = oid;
            container->slots[slot].obj_count = oc > 0 ? oc : 1;
            container->slots[slot].scene_id = INV_MANAGER_NO_SCENE_ID;
            container->slots[slot].atlas_index = 0;
        }
        else
        {
            inv_slot_clear(&container->slots[slot]);
        }
    }

    if( mgr->selection.source_id >= 0 &&
        mgr->selection.source_id < mgr->source_count &&
        mgr->sources[mgr->selection.source_id].used &&
        mgr->sources[mgr->selection.source_id].container_id == container_id )
    {
        int const sel = mgr->selection.slot;
        if( sel < 0 || sel >= container->slot_count ||
            container->slots[sel].obj_id <= INV_MANAGER_EMPTY_OBJ_ID )
        {
            InvManager_SelectionClear(mgr);
        }
    }

    inv_manager_notify(mgr, container_id);
    return true;
}

int
InvManager_GetObj(
    struct InvManager const* mgr,
    int container_id,
    int slot)
{
    assert(mgr);
    struct InvContainer const* container =
        InvManager_FindContainer(mgr, container_id);
    if( !container || !container->slots || slot < 0 || slot >= container->slot_count )
        return INV_MANAGER_EMPTY_OBJ_ID;
    return container->slots[slot].obj_id;
}

int
InvManager_GetNum(
    struct InvManager const* mgr,
    int container_id,
    int slot)
{
    assert(mgr);
    struct InvContainer const* container =
        InvManager_FindContainer(mgr, container_id);
    if( !container || !container->slots || slot < 0 || slot >= container->slot_count )
        return 0;
    return container->slots[slot].obj_count;
}

int
InvManager_Total(
    struct InvManager const* mgr,
    int container_id,
    int obj_id)
{
    assert(mgr);
    if( obj_id <= INV_MANAGER_EMPTY_OBJ_ID )
        return 0;
    struct InvContainer const* container =
        InvManager_FindContainer(mgr, container_id);
    if( !container || !container->slots )
        return 0;

    int total = 0;
    for( int i = 0; i < container->slot_count; i++ )
    {
        if( container->slots[i].obj_id == obj_id )
            total += container->slots[i].obj_count;
    }
    return total;
}

int
InvManager_Size(
    struct InvManager const* mgr,
    int container_id)
{
    assert(mgr);
    struct InvContainer const* container =
        InvManager_FindContainer(mgr, container_id);
    if( !container )
        return 0;
    return container->slot_count;
}

bool
InvManager_SwapSlots(
    struct InvManager* mgr,
    int source_id,
    int slot_a,
    int slot_b)
{
    assert(mgr);
    if( slot_a < 0 || slot_b < 0 )
        return false;
    if( slot_a == slot_b )
        return true;

    int const container_id = inv_resolve_container_id(mgr, source_id);
    if( container_id == INV_MANAGER_CONTAINER_NONE )
        return false;
    struct InvContainer* container = InvManager_GetContainer(mgr, container_id);
    assert(container);
    assert(container->slots);
    if( slot_a >= container->slot_count || slot_b >= container->slot_count )
        return false;

    struct InvSlot tmp = container->slots[slot_a];
    container->slots[slot_a] = container->slots[slot_b];
    container->slots[slot_b] = tmp;

    if( mgr->selection.source_id == source_id )
    {
        if( mgr->selection.slot == slot_a )
            mgr->selection.slot = slot_b;
        else if( mgr->selection.slot == slot_b )
            mgr->selection.slot = slot_a;
    }

    inv_manager_notify(mgr, container_id);
    return true;
}

void
InvManager_SelectionReset(struct InvManager* mgr)
{
    assert(mgr);
    mgr->selection.source_id = INV_MANAGER_SOURCE_INVALID;
    mgr->selection.slot = INV_MANAGER_NO_SELECTION;
}

void
InvManager_SelectionSet(
    struct InvManager* mgr,
    int source_id,
    int slot)
{
    assert(mgr);
    mgr->selection.source_id = source_id;
    mgr->selection.slot = slot;
}

void
InvManager_SelectionClear(struct InvManager* mgr)
{
    InvManager_SelectionReset(mgr);
}

bool
InvManager_SelectionIsSet(struct InvManager const* mgr)
{
    assert(mgr);
    return mgr->selection.source_id != INV_MANAGER_SOURCE_INVALID &&
           mgr->selection.slot != INV_MANAGER_NO_SELECTION;
}

bool
InvManager_SelectionMatches(
    struct InvManager const* mgr,
    int source_id,
    int slot)
{
    assert(mgr);
    return mgr->selection.source_id == source_id && mgr->selection.slot == slot;
}
