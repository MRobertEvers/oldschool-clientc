#include "plugin/plugin_frame_binding.h"

#include <assert.h>
#include <string.h>

void
PluginFrameBinding_Invalidate(struct PluginFrameBinding* binding)
{
    assert(binding);
    binding->bound_valid = 0;
    binding->bound_tree = NULL;
    binding->sidetab_valid = 0;
    binding->slot_node_gen = 0;
}

/* ---- slot nodes ---- */

bool
PluginFrameBinding_SlotNodesCurrent(
    struct PluginFrameBinding const* binding,
    uint32_t generation)
{
    assert(binding);
    return binding->slot_node_gen == generation;
}

void
PluginFrameBinding_SetSlotNode(
    struct PluginFrameBinding* binding,
    int slot,
    int32_t node)
{
    assert(binding);
    assert(slot >= 0);
    assert(slot < TORIRS_HOST_SURFACE_PLACEABLE_COUNT);
    binding->slot_node[slot] = node;
}

void
PluginFrameBinding_SlotNodesResolved(
    struct PluginFrameBinding* binding,
    uint32_t generation)
{
    assert(binding);
    binding->slot_node_gen = generation;
}

int32_t
PluginFrameBinding_SlotNode(struct PluginFrameBinding const* binding, int slot)
{
    assert(binding);
    assert(slot >= 0);
    assert(slot < TORIRS_HOST_SURFACE_PLACEABLE_COUNT);
    return binding->slot_node[slot];
}

/* ---- the binder's pass ---- */

bool
PluginFrameBinding_BoundCurrent(
    struct PluginFrameBinding const* binding,
    struct UITree const* tree,
    uint32_t generation,
    uint32_t id_generation,
    int roles_count)
{
    assert(binding);
    return binding->bound_valid && binding->bound_tree == tree &&
           binding->bound_generation == generation &&
           binding->bound_id_generation == id_generation &&
           binding->role_ids_for_count == roles_count;
}

void
PluginFrameBinding_DropBound(struct PluginFrameBinding* binding)
{
    assert(binding);
    binding->bound_valid = 0;
}

void
PluginFrameBinding_MarkBound(
    struct PluginFrameBinding* binding,
    struct UITree const* tree,
    uint32_t generation,
    uint32_t id_generation)
{
    assert(binding);
    assert(tree);
    binding->bound_tree = tree;
    binding->bound_generation = generation;
    binding->bound_id_generation = id_generation;
    binding->bound_valid = 1;
}

/* ---- interned frame-role ids ---- */

bool
PluginFrameBinding_RoleIdsCurrent(
    struct PluginFrameBinding const* binding,
    int roles_count)
{
    assert(binding);
    return binding->role_ids_for_count == roles_count;
}

void
PluginFrameBinding_ClearRoleIds(struct PluginFrameBinding* binding, int slot)
{
    assert(binding);
    assert(slot >= 0);
    assert(slot < PLUGIN_FRAME_BINDING_SLOTS);
    memset(binding->role_id[slot], 0, sizeof(binding->role_id[slot]));
}

void
PluginFrameBinding_SetRoleId(
    struct PluginFrameBinding* binding,
    int slot,
    int index,
    uint16_t role_id)
{
    assert(binding);
    assert(slot >= 0);
    assert(slot < PLUGIN_FRAME_BINDING_SLOTS);
    assert(index >= 0);
    assert(index < 1 + PLUGIN_FRAME_BINDING_MEMBERS);
    binding->role_id[slot][index] = role_id;
}

void
PluginFrameBinding_SetCompassClickRole(
    struct PluginFrameBinding* binding,
    uint16_t role_id)
{
    assert(binding);
    binding->compass_click_role = role_id;
}

void
PluginFrameBinding_RoleIdsInterned(
    struct PluginFrameBinding* binding,
    int roles_count)
{
    assert(binding);
    binding->role_ids_for_count = roles_count;
}

uint16_t const*
PluginFrameBinding_RoleIds(struct PluginFrameBinding const* binding, int slot)
{
    assert(binding);
    assert(slot >= 0);
    assert(slot < PLUGIN_FRAME_BINDING_SLOTS);
    return binding->role_id[slot];
}

uint16_t
PluginFrameBinding_CompassClickRole(struct PluginFrameBinding const* binding)
{
    assert(binding);
    return binding->compass_click_role;
}

/* ---- stamps ---- */

int
PluginFrameBinding_StampCount(struct PluginFrameBinding const* binding)
{
    assert(binding);
    return binding->stamp_count;
}

struct PluginFrameStamp
PluginFrameBinding_Stamp(struct PluginFrameBinding const* binding, int i)
{
    assert(binding);
    assert(i >= 0);
    assert(i < binding->stamp_count);
    return binding->stamp[i];
}

void
PluginFrameBinding_ClearStamps(struct PluginFrameBinding* binding)
{
    assert(binding);
    binding->stamp_count = 0;
}

void
PluginFrameBinding_PushStamp(
    struct PluginFrameBinding* binding,
    int32_t node,
    uint64_t incarnation)
{
    assert(binding);
    assert(binding->stamp_count < PLUGIN_FRAME_STAMP_MAX);
    binding->stamp[binding->stamp_count].node = node;
    binding->stamp[binding->stamp_count].incarnation = incarnation;
    binding->stamp_count++;
}

/* ---- facet ids ---- */

bool
PluginFrameBinding_FacetIdsCurrent(
    struct PluginFrameBinding const* binding,
    int refs_count,
    int roles_count)
{
    assert(binding);
    return binding->facet_valid && binding->facet_refs_count == refs_count &&
           binding->facet_roles_count == roles_count;
}

void
PluginFrameBinding_StoreFacetIds(
    struct PluginFrameBinding* binding,
    int refs_count,
    int roles_count,
    struct PluginFacetIds const* ids)
{
    assert(binding);
    assert(ids);
    binding->facet_refs_count = refs_count;
    binding->facet_roles_count = roles_count;
    binding->facet = *ids;
    binding->sidetab_valid = 0;
    binding->facet_valid = 1;
}

struct PluginFacetIds const*
PluginFrameBinding_FacetIds(struct PluginFrameBinding const* binding)
{
    assert(binding);
    return &binding->facet;
}

/* ---- tab stones ---- */

bool
PluginFrameBinding_SidetabNodesCurrent(
    struct PluginFrameBinding const* binding,
    uint32_t generation,
    uint32_t id_generation)
{
    assert(binding);
    return binding->sidetab_valid && binding->sidetab_generation == generation &&
           binding->sidetab_id_generation == id_generation;
}

void
PluginFrameBinding_BeginSidetabNodes(
    struct PluginFrameBinding* binding,
    uint32_t generation,
    uint32_t id_generation)
{
    assert(binding);
    binding->sidetab_generation = generation;
    binding->sidetab_id_generation = id_generation;
    binding->sidetab_valid = 1;
}

void
PluginFrameBinding_SetSidetabNode(
    struct PluginFrameBinding* binding,
    int tab,
    int32_t node)
{
    assert(binding);
    assert(tab >= 0);
    assert(tab < PLUGIN_FRAME_BINDING_MEMBERS);
    binding->sidetab_node[tab] = node;
}

int
PluginFrameBinding_SidetabOf(struct PluginFrameBinding const* binding, int32_t node)
{
    assert(binding);
    for( int tab = 0; tab < PLUGIN_FRAME_BINDING_MEMBERS; tab++ )
        if( binding->sidetab_node[tab] == node )
            return tab;
    return -1;
}
