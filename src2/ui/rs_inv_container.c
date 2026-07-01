#include "rs_inv_container.h"

#include <string.h>

struct RSInvContainer*
rs_inv_container_get_or_create(
    struct RSInvContainerStore* store,
    int inv_id,
    int slot_count)
{
    if( !store || inv_id < 0 )
        return NULL;

    for( int i = 0; i < store->count; i++ )
    {
        if( store->containers[i].inv_id == inv_id )
            return &store->containers[i];
    }

    if( store->count >= RS_INV_CONTAINER_STORE_MAX )
        return NULL;

    struct RSInvContainer* c = &store->containers[store->count++];
    memset(c, 0, sizeof(*c));
    c->inv_id = inv_id;
    if( slot_count <= 0 )
        slot_count = RS_INV_CONTAINER_MAX_SLOTS;
    if( slot_count > RS_INV_CONTAINER_MAX_SLOTS )
        slot_count = RS_INV_CONTAINER_MAX_SLOTS;
    c->slot_count = slot_count;
    for( int s = 0; s < RS_INV_CONTAINER_MAX_SLOTS; s++ )
        c->scene_id[s] = -1;
    return c;
}

struct RSInvContainer const*
rs_inv_container_find(
    struct RSInvContainerStore const* store,
    int inv_id)
{
    if( !store || inv_id < 0 )
        return NULL;
    for( int i = 0; i < store->count; i++ )
    {
        if( store->containers[i].inv_id == inv_id )
            return &store->containers[i];
    }
    return NULL;
}

void
rs_inv_container_set_slot(
    struct RSInvContainer* container,
    int slot,
    int obj_id,
    int obj_count,
    int scene_id,
    int atlas_index)
{
    if( !container || slot < 0 || slot >= RS_INV_CONTAINER_MAX_SLOTS )
        return;
    if( slot >= container->slot_count )
        container->slot_count = slot + 1;
    container->obj_id[slot] = obj_id;
    container->obj_count[slot] = obj_count;
    container->scene_id[slot] = scene_id;
    container->atlas_index[slot] = atlas_index;
}

void
rs_inv_container_clear_slot(
    struct RSInvContainer* container,
    int slot)
{
    rs_inv_container_set_slot(container, slot, 0, 0, -1, 0);
}

void
rs_inv_container_apply_full(
    struct RSInvContainer* container,
    int const* obj_ids,
    int const* obj_counts,
    int count)
{
    if( !container )
        return;
    if( count < 0 )
        count = 0;
    if( count > RS_INV_CONTAINER_MAX_SLOTS )
        count = RS_INV_CONTAINER_MAX_SLOTS;
    if( count > container->slot_count )
        container->slot_count = count;

    for( int i = 0; i < count; i++ )
    {
        int const oid = obj_ids ? obj_ids[i] : 0;
        int const oc = obj_counts ? obj_counts[i] : 0;
        if( oid > 0 )
            rs_inv_container_set_slot(container, i, oid, oc > 0 ? oc : 1, -1, 0);
        else
            rs_inv_container_clear_slot(container, i);
    }
    for( int i = count; i < container->slot_count; i++ )
        rs_inv_container_clear_slot(container, i);
}

void
rs_inv_container_apply_partial(
    struct RSInvContainer* container,
    int const* slots,
    int const* obj_ids,
    int const* obj_counts,
    int count)
{
    if( !container || !slots || count <= 0 )
        return;
    for( int i = 0; i < count; i++ )
    {
        int const slot = slots[i];
        if( slot < 0 || slot >= RS_INV_CONTAINER_MAX_SLOTS )
            continue;
        int const oid = obj_ids ? obj_ids[i] : 0;
        int const oc = obj_counts ? obj_counts[i] : 0;
        if( oid > 0 )
            rs_inv_container_set_slot(container, slot, oid, oc > 0 ? oc : 1, -1, 0);
        else
            rs_inv_container_clear_slot(container, slot);
    }
}
