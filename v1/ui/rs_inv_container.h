#ifndef RS_INV_CONTAINER_H
#define RS_INV_CONTAINER_H

#include <stdbool.h>
#include <stdint.h>

/** OSRS inventory container ids (official client). */
#define RS_INV_CONTAINER_NONE (-1)
#define RS_INV_CONTAINER_BACKPACK 93
#define RS_INV_CONTAINER_WORN 94

#define RS_INV_CONTAINER_MAX_SLOTS 28
#define RS_INV_CONTAINER_STORE_MAX 16

struct RSInvContainer
{
    int inv_id;
    int slot_count;
    int obj_id[RS_INV_CONTAINER_MAX_SLOTS];
    int obj_count[RS_INV_CONTAINER_MAX_SLOTS];
    /** Pre-resolved icon scene ids; -1 if none. */
    int scene_id[RS_INV_CONTAINER_MAX_SLOTS];
    int atlas_index[RS_INV_CONTAINER_MAX_SLOTS];
};

struct RSInvContainerStore
{
    struct RSInvContainer containers[RS_INV_CONTAINER_STORE_MAX];
    int count;
};

struct RSInvContainer*
rs_inv_container_get_or_create(
    struct RSInvContainerStore* store,
    int inv_id,
    int slot_count);

struct RSInvContainer const*
rs_inv_container_find(
    struct RSInvContainerStore const* store,
    int inv_id);

void
rs_inv_container_set_slot(
    struct RSInvContainer* container,
    int slot,
    int obj_id,
    int obj_count,
    int scene_id,
    int atlas_index);

void
rs_inv_container_clear_slot(
    struct RSInvContainer* container,
    int slot);

/** Replace all slots in a container (empty slots beyond count are cleared). */
void
rs_inv_container_apply_full(
    struct RSInvContainer* container,
    int const* obj_ids,
    int const* obj_counts,
    int count);

/** Update individual slots without clearing others. */
void
rs_inv_container_apply_partial(
    struct RSInvContainer* container,
    int const* slots,
    int const* obj_ids,
    int const* obj_counts,
    int count);

#endif
