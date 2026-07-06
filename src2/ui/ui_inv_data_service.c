#include "ui_inv_data_service.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int
ui_inv_data_service_default_container_for_name(char const* name)
{
    assert(name && name[0] != '\0');
    if( strcmp(name, UI_INV_SOURCE_NAME_WORN) == 0 )
        return RS_INV_CONTAINER_WORN;
    return RS_INV_CONTAINER_BACKPACK;
}

void
ui_inv_data_service_init(struct UIInvDataService* svc)
{
    assert(svc);
    memset(svc, 0, sizeof(*svc));
}

int
ui_inv_data_service_resolve_source(
    struct UIInvDataService* svc,
    char const* name)
{
    assert(svc);
    assert(name);
    assert(name[0] != '\0');

    for( int i = 0; i < svc->source_count; i++ )
    {
        if( svc->sources[i].used && strcmp(svc->sources[i].name, name) == 0 )
            return i;
    }

    if( svc->source_count >= UI_INV_SOURCE_MAX )
        return UI_INV_SOURCE_INVALID;

    int const idx = svc->source_count++;
    struct UIInvDataSource* src = &svc->sources[idx];
    memset(src, 0, sizeof(*src));
    strncpy(src->name, name, sizeof(src->name) - 1);
    src->container_id = ui_inv_data_service_default_container_for_name(name);
    src->slot_count = src->container_id == RS_INV_CONTAINER_WORN ? 14 : RS_INV_CONTAINER_MAX_SLOTS;
    src->used = true;

    rs_inv_container_get_or_create(&svc->store, src->container_id, src->slot_count);
    return idx;
}

int
ui_inv_data_service_container_for_source(
    struct UIInvDataService const* svc,
    int source_id)
{
    if( !svc || source_id < 0 || source_id >= svc->source_count )
        return RS_INV_CONTAINER_NONE;
    if( !svc->sources[source_id].used )
        return RS_INV_CONTAINER_NONE;
    return svc->sources[source_id].container_id;
}

bool
ui_inv_data_service_get_slot(
    struct UIInvDataService const* svc,
    int source_id,
    int slot,
    struct UIInvSlotData* out)
{
    if( out )
    {
        out->obj_id = 0;
        out->obj_count = 0;
        out->scene_id = -1;
        out->atlas_index = 0;
    }

    if( !svc || !out || source_id < 0 || source_id >= svc->source_count || slot < 0 )
        return false;
    if( !svc->sources[source_id].used )
        return false;

    int const container_id = svc->sources[source_id].container_id;
    struct RSInvContainer const* container = rs_inv_container_find(&svc->store, container_id);
    if( !container || slot >= container->slot_count )
        return false;

    out->obj_id = container->obj_id[slot];
    out->obj_count = container->obj_count[slot];
    out->scene_id = container->scene_id[slot];
    out->atlas_index = container->atlas_index[slot];
    return true;
}

bool
ui_inv_data_service_set_slot(
    struct UIInvDataService* svc,
    int source_id,
    int slot,
    struct UIInvSlotData const* data)
{
    if( !svc || source_id < 0 || source_id >= svc->source_count || slot < 0 || !data )
        return false;
    if( !svc->sources[source_id].used )
        return false;

    int const container_id = svc->sources[source_id].container_id;
    struct RSInvContainer* container = rs_inv_container_get_or_create(
        &svc->store, container_id, svc->sources[source_id].slot_count);
    if( !container )
        return false;

    rs_inv_container_set_slot(
        container,
        slot,
        data->obj_id,
        data->obj_count > 0 ? data->obj_count : 1,
        data->scene_id,
        data->atlas_index);
    ui_inv_data_service_mark_dirty(svc, source_id);
    return true;
}

void
ui_inv_data_service_seed_from_pool(
    struct UIInvDataService* svc,
    int source_id,
    struct UIInventoryPool const* pool,
    int inv_index)
{
    if( !svc || !pool || source_id < 0 || source_id >= svc->source_count )
        return;
    if( inv_index < 0 || inv_index >= pool->count )
        return;

    int const container_id = svc->sources[source_id].container_id;
    struct RSInvContainer* container =
        rs_inv_container_get_or_create(&svc->store, container_id, RS_INV_CONTAINER_MAX_SLOTS);
    if( !container )
        return;

    struct UIInventory const* inv = &pool->inventories[inv_index];
    int const n =
        inv->item_count < RS_INV_CONTAINER_MAX_SLOTS ? inv->item_count : RS_INV_CONTAINER_MAX_SLOTS;
    for( int i = 0; i < n; i++ )
    {
        rs_inv_container_set_slot(
            container,
            i,
            inv->items[i].obj_id,
            1,
            inv->items[i].scene_id,
            inv->items[i].atlas_index);
    }
    for( int i = n; i < RS_INV_CONTAINER_MAX_SLOTS; i++ )
        rs_inv_container_clear_slot(container, i);
}

void
ui_inv_data_service_mark_dirty(
    struct UIInvDataService* svc,
    int source_id)
{
    if( !svc || source_id < 0 || source_id >= svc->source_count )
        return;
    if( !svc->sources[source_id].used )
        return;
    if( svc->on_container_change )
        svc->on_container_change(svc->on_container_change_ud, svc->sources[source_id].container_id);
}

int
ui_inv_data_service_ensure_container(
    struct UIInvDataService* svc,
    int container_id,
    int slot_count,
    char const* display_name)
{
    if( !svc || container_id < 0 )
        return UI_INV_SOURCE_INVALID;

    for( int i = 0; i < svc->source_count; i++ )
    {
        if( svc->sources[i].used && svc->sources[i].container_id == container_id )
            return i;
    }

    if( svc->source_count >= UI_INV_SOURCE_MAX )
        return UI_INV_SOURCE_INVALID;

    int const idx = svc->source_count++;
    struct UIInvDataSource* src = &svc->sources[idx];
    memset(src, 0, sizeof(*src));
    src->container_id = container_id;
    src->slot_count = slot_count > 0 ? slot_count : RS_INV_CONTAINER_MAX_SLOTS;
    src->used = true;
    if( display_name && display_name[0] != '\0' )
        strncpy(src->name, display_name, sizeof(src->name) - 1);
    else
        snprintf(src->name, sizeof(src->name), "inv_%d", container_id);

    rs_inv_container_get_or_create(&svc->store, container_id, src->slot_count);
    return idx;
}

void
ui_inv_data_service_set_change_callback(
    struct UIInvDataService* svc,
    void (*cb)(void* ud, int container_id),
    void* ud)
{
    if( !svc )
        return;
    svc->on_container_change = cb;
    svc->on_container_change_ud = ud;
}
