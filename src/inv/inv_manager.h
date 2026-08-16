#ifndef INV_MANAGER_H
#define INV_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** OSRS inventory container ids. */
#define INV_MANAGER_CONTAINER_NONE (-1)
#define INV_MANAGER_CONTAINER_BACKPACK 93
#define INV_MANAGER_CONTAINER_WORN 94
#define INV_MANAGER_CONTAINER_BANK 95
#define INV_MANAGER_CONTAINER_TRADE_SELF 149
#define INV_MANAGER_CONTAINER_TRADE_OTHER 150
#define INV_MANAGER_CONTAINER_SHOP 516
#define INV_MANAGER_CONTAINER_COLLECTION 620

#define INV_MANAGER_DEFAULT_BACKPACK_SLOTS 28
#define INV_MANAGER_DEFAULT_WORN_SLOTS 14

#define INV_MANAGER_SOURCE_INVALID (-1)
#define INV_MANAGER_SOURCE_MAX 16
#define INV_MANAGER_SOURCE_NAME_MAX 64
#define INV_MANAGER_STORE_MAX 32

#define INV_MANAGER_SOURCE_NAME_BACKPACK "inventory"
#define INV_MANAGER_SOURCE_NAME_WORN "worn"

#define INV_MANAGER_EMPTY_OBJ_ID 0
#define INV_MANAGER_NO_SCENE_ID (-1)
#define INV_MANAGER_NO_SELECTION (-1)

struct InvSlot
{
    int obj_id;
    int obj_count;
    int scene_id;
    int atlas_index;
};

struct InvContainer
{
    int inv_id;
    int slot_count;
    struct InvSlot* slots;
};

struct InvSource
{
    char name[INV_MANAGER_SOURCE_NAME_MAX];
    int container_id;
    int slot_count;
    bool used;
};

struct InvSelection
{
    int source_id;
    int slot;
};

/** Callback when a container changes (for onInvTransmit). */
typedef void (*InvManager_ChangeFn)(
    void* userdata,
    int container_id);

struct InvManager
{
    struct InvContainer containers[INV_MANAGER_STORE_MAX];
    int container_count;

    struct InvSource sources[INV_MANAGER_SOURCE_MAX];
    int source_count;

    struct InvSelection selection;

    InvManager_ChangeFn change_fn;
    void* change_userdata;
};

void
InvManager_Init(struct InvManager* mgr);

void
InvManager_Free(struct InvManager* mgr);

void
InvManager_SetChangeCallback(
    struct InvManager* mgr,
    InvManager_ChangeFn fn,
    void* userdata);

/** Resolve or register a named inventory source. Returns source id or INVALID. */
int
InvManager_ResolveSource(
    struct InvManager* mgr,
    char const* name);

/** Register a container by numeric id. Returns source id or INVALID. */
int
InvManager_EnsureContainer(
    struct InvManager* mgr,
    int container_id,
    int slot_count,
    char const* display_name);

int
InvManager_ContainerForSource(
    struct InvManager const* mgr,
    int source_id);

struct InvContainer*
InvManager_GetContainer(
    struct InvManager* mgr,
    int container_id);

struct InvContainer const*
InvManager_FindContainer(
    struct InvManager const* mgr,
    int container_id);

bool
InvManager_GetSlot(
    struct InvManager const* mgr,
    int source_id,
    int slot,
    struct InvSlot* out);

bool
InvManager_SetSlot(
    struct InvManager* mgr,
    int source_id,
    int slot,
    struct InvSlot const* data);

bool
InvManager_ClearSlot(
    struct InvManager* mgr,
    int source_id,
    int slot);

/** Replace all slots (empty beyond count cleared). scene_id left as NO_SCENE. */
bool
InvManager_ApplyFull(
    struct InvManager* mgr,
    int container_id,
    int const* obj_ids,
    int const* obj_counts,
    int count);

/** Update individual slots without clearing others. */
bool
InvManager_ApplyPartial(
    struct InvManager* mgr,
    int container_id,
    int const* slots,
    int const* obj_ids,
    int const* obj_counts,
    int count);

/** CS2-style: item id at slot, or EMPTY if miss. */
int
InvManager_GetObj(
    struct InvManager const* mgr,
    int container_id,
    int slot);

/** CS2-style: stack size at slot. */
int
InvManager_GetNum(
    struct InvManager const* mgr,
    int container_id,
    int slot);

/** CS2-style: total quantity of obj_id across slots. */
int
InvManager_Total(
    struct InvManager const* mgr,
    int container_id,
    int obj_id);

/** CS2-style: slot capacity, or 0 if unknown. */
int
InvManager_Size(
    struct InvManager const* mgr,
    int container_id);

bool
InvManager_SwapSlots(
    struct InvManager* mgr,
    int source_id,
    int slot_a,
    int slot_b);

void
InvManager_SelectionReset(struct InvManager* mgr);

void
InvManager_SelectionSet(
    struct InvManager* mgr,
    int source_id,
    int slot);

void
InvManager_SelectionClear(struct InvManager* mgr);

bool
InvManager_SelectionIsSet(struct InvManager const* mgr);

bool
InvManager_SelectionMatches(
    struct InvManager const* mgr,
    int source_id,
    int slot);

#endif /* INV_MANAGER_H */
