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
    assert(slots);

    for( int i = 0; i < slot_count; i++ )
        inv_slot_clear(&slots[i]);

    c->slots = slots;
    c->slot_count = slot_count;
    return true;
}

/*
 * Widen a container in place, keeping what is already in it.
 *
 * A container's slot count is not something the first packet about it gets to
 * decide for the rest of the session, and treating it that way is what made a
 * backpack stop accepting items part-way down.
 *
 * UPDATE_INV_FULL's "capacity" is written by the server as the *used prefix* —
 * slots up to and including the last occupied one (torirs_server_container.c's
 * `used_prefix`, an explicit "the empty tail costs nothing to omit"). The
 * client used it as the container's size, so a login that restored 17 items
 * built a 17-slot backpack: every later UPDATE_INV_PARTIAL naming slot 17 or
 * beyond was dropped by the `slot >= slot_count` bound below, and
 * `emit_rs_inv_slots` drew those cells as empty because the host lookup missed.
 * The server counted 28 slots and 3 free; the player saw eleven empty cells and
 * `::give` answering "did not fit". Nothing was wrong with the server's
 * placement — `ToriRSServer_ContainerAdd` fills the first free slot and always did.
 * The cells the player was aiming at existed only on the server.
 *
 * The bank has the same shape and the worse consequence: it is transmitted
 * whole rather than per slot, so ApplyFull's clamp silently discarded every
 * slot past the prefix the bank happened to have when it first opened.
 *
 * So: the wire may name any slot, and naming one past the end grows the
 * container rather than being dropped. Growth only — a later, shorter FULL
 * still clears its tail (that is the server saying those slots are empty, not
 * that they are gone).
 */
static bool
inv_container_ensure_slots(struct InvContainer* c, int slot_count)
{
    struct InvSlot* slots;
    int const old_count = c ? c->slot_count : 0;

    assert(c);
    if( slot_count <= old_count )
        return true;
    if( slot_count > INV_MANAGER_MAX_SLOTS )
        return false;
    if( !c->slots )
        return inv_container_alloc_slots(c, slot_count);

    slots = realloc(c->slots, (size_t)slot_count * sizeof(struct InvSlot));
    assert(slots);
    for( int i = old_count; i < slot_count; i++ )
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
        {
            /* A later, larger claim about the same container widens it. */
            if( slot_count > 0 )
                (void)inv_container_ensure_slots(&mgr->containers[i], slot_count);
            return &mgr->containers[i];
        }
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
        {
            /* Already known, but perhaps as fewer slots than this caller has
             * just been told about — the first UPDATE_INV_FULL for a container
             * carries only its used prefix. See inv_container_ensure_slots. */
            struct InvContainer* c = InvManager_GetContainer(mgr, container_id);
            if( c && slot_count > 0 )
            {
                (void)inv_container_ensure_slots(c, slot_count);
                if( c->slot_count > mgr->sources[i].slot_count )
                    mgr->sources[i].slot_count = c->slot_count;
            }
            return i;
        }
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
    {
        /* Emptying a slot the container does not have yet is already true, and
         * refusing to allocate for it keeps a stray slot index from sizing the
         * container. An item, though, has to land: see
         * inv_container_ensure_slots for the backpack this used to swallow. */
        if( data->obj_id <= INV_MANAGER_EMPTY_OBJ_ID )
            return true;
        if( !inv_container_ensure_slots(container, slot + 1) )
            return false;
    }

    int obj_id = data->obj_id;
    int obj_count = data->obj_count;
    if( obj_id <= INV_MANAGER_EMPTY_OBJ_ID )
    {
        inv_slot_clear(&container->slots[slot]);
    }
    else
    {
        container->slots[slot].obj_id = obj_id;
        /* Count zero is kept — see InvManager_ApplyFull. */
        container->slots[slot].obj_count = obj_count;
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
    /* Widen rather than truncate: a FULL that names more slots than the
     * container was created with is the server correcting an earlier, shorter
     * claim about its size (inv_container_ensure_slots). */
    if( count > container->slot_count && !inv_container_ensure_slots(container, count) )
        count = container->slot_count;

    for( int i = 0; i < count; i++ )
    {
        int const oid = obj_ids ? obj_ids[i] : INV_MANAGER_EMPTY_OBJ_ID;
        int const oc = obj_counts ? obj_counts[i] : 0;
        if( oid > INV_MANAGER_EMPTY_OBJ_ID )
        {
            /*
             * Zero is a COUNT, not an empty slot, and this used to round it up
             * to one.
             *
             * Emptiness is the obj id's to say — the branch above already tests
             * it, and the encoder writes the pair independently for exactly
             * this reason (torirs_server_encode.c: "`obj_id >= 0` is the occupancy
             * test, not `count > 0`"). Two things on the wire legitimately
             * carry an obj with a count of zero: a bank placeholder, and an
             * out-of-stock shop line, which a shop keeps in place at zero
             * rather than deleting (ToriRSServer_ContainerClearSlot).
             *
             * The floor made both of them lie, and it lied differently
             * depending on the obj: a stackable at zero stock drew a yellow
             * "1", and an unstackable at zero stock drew no number at all,
             * because `emit_obj_stack_count` skips an unstackable whose count
             * is exactly 1. That is the whole of "the shop says 1 when it has
             * 0". Icon rasterization is unaffected — every call site that needs
             * a drawable quantity clamps at its own use (uitree_emit.c).
             */
            int const norm_count = oc;
            /* Server echo of an unchanged slot (e.g. the UPDATE_INV after an
             * optimistic drag swap) keeps its baked icon: resetting scene_id
             * leaves the slot iconless until the reconcile task runs — a
             * one-frame flicker on every drop. */
            if( container->slots[i].obj_id != oid ||
                container->slots[i].obj_count != norm_count )
            {
                container->slots[i].scene_id = INV_MANAGER_NO_SCENE_ID;
                container->slots[i].atlas_index = 0;
            }
            container->slots[i].obj_id = oid;
            container->slots[i].obj_count = norm_count;
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
    if( count <= 0 )
        return false;
    assert(slots);

    struct InvContainer* container = InvManager_GetContainer(mgr, container_id);
    if( !container )
        return false;
    assert(container->slots);

    for( int i = 0; i < count; i++ )
    {
        int const slot = slots[i];
        if( slot < 0 )
            continue;
        int const oid = obj_ids ? obj_ids[i] : INV_MANAGER_EMPTY_OBJ_ID;
        int const oc = obj_counts ? obj_counts[i] : 0;
        /* Past the end: an item widens the container, an empty slot is already
         * what it says (inv_container_ensure_slots). */
        if( slot >= container->slot_count &&
            (oid <= INV_MANAGER_EMPTY_OBJ_ID ||
             !inv_container_ensure_slots(container, slot + 1)) )
            continue;
        if( oid > INV_MANAGER_EMPTY_OBJ_ID )
        {
            /* Count zero is kept — see ApplyFull for the two things on the wire
             * that carry one and what rounding it up did to them. */
            int const norm_count = oc;
            /* Unchanged slot keeps its baked icon (see ApplyFull). */
            if( container->slots[slot].obj_id != oid ||
                container->slots[slot].obj_count != norm_count )
            {
                container->slots[slot].scene_id = INV_MANAGER_NO_SCENE_ID;
                container->slots[slot].atlas_index = 0;
            }
            container->slots[slot].obj_id = oid;
            container->slots[slot].obj_count = norm_count;
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
