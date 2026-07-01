#ifndef UI_INV_DATA_SERVICE_H
#define UI_INV_DATA_SERVICE_H

#include "rs_inv_container.h"
#include "uitree.h"

#include <stdbool.h>
#include <stdint.h>

#define UI_INV_SOURCE_INVALID (-1)
#define UI_INV_SOURCE_MAX 16
#define UI_INV_SOURCE_NAME_MAX 64

/** Well-known revconfig / bake source names. */
#define UI_INV_SOURCE_NAME_BACKPACK "inventory"
#define UI_INV_SOURCE_NAME_WORN "worn"

struct UIInvSlotData
{
    int obj_id;
    int obj_count;
    int scene_id;
    int atlas_index;
};

struct UIInvDataSource
{
    char name[UI_INV_SOURCE_NAME_MAX];
    int container_id;
    int slot_count;
    bool used;
};

struct UIInvDataService
{
    struct RSInvContainerStore store;
    struct UIInvDataSource sources[UI_INV_SOURCE_MAX];
    int source_count;
};

void
ui_inv_data_service_init(struct UIInvDataService* svc);

/** Bake-time: resolve or register a named inventory source. Returns source id or UI_INV_SOURCE_INVALID. */
int
ui_inv_data_service_resolve_source(
    struct UIInvDataService* svc,
    char const* name);

int
ui_inv_data_service_container_for_source(
    struct UIInvDataService const* svc,
    int source_id);

bool
ui_inv_data_service_get_slot(
    struct UIInvDataService const* svc,
    int source_id,
    int slot,
    struct UIInvSlotData* out);

bool
ui_inv_data_service_set_slot(
    struct UIInvDataService* svc,
    int source_id,
    int slot,
    struct UIInvSlotData const* data);

void
ui_inv_data_service_seed_from_pool(
    struct UIInvDataService* svc,
    int source_id,
    struct UIInventoryPool const* pool,
    int inv_index);

void
ui_inv_data_service_mark_dirty(struct UIInvDataService* svc, int source_id);

#endif
