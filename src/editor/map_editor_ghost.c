#include "editor/map_editor_ghost.h"

#include <assert.h>
#include <string.h>

void
MapEditorGhost_Reset(struct MapEditorGhost* ghost)
{
    assert(ghost);
    memset(ghost, 0, sizeof(*ghost));
    ghost->spawn.entity = -1;
}

/** The loc ghost's own fields back to "none"; the siblings stay. */
static void
ghost_loc_clear(struct MapEditorGhost* ghost)
{
    ghost->active = false;
    ghost->alpha_done = false;
    memset(&ghost->spec, 0, sizeof(ghost->spec));
    ghost->displaced_valid = false;
    memset(&ghost->displaced, 0, sizeof(ghost->displaced));
}

bool
MapEditorGhost_Matches(
    struct MapEditorGhost const* ghost,
    struct MapEditorGhostSpec const* want)
{
    assert(ghost);
    assert(want);

    if( !ghost->active )
        return false;
    return ghost->spec.scene_x == want->scene_x && ghost->spec.scene_z == want->scene_z &&
           ghost->spec.level == want->level && ghost->spec.loc_id == want->loc_id &&
           ghost->spec.shape == want->shape && ghost->spec.angle == want->angle;
}

bool
MapEditorGhost_Remove(
    struct MapEditorGhost* ghost,
    struct MapEditorGhostSpec* out_removed,
    bool* out_has_restore,
    struct MapEditorGhostSpec* out_restore)
{
    assert(ghost);
    assert(out_removed);
    assert(out_has_restore);
    assert(out_restore);

    if( !ghost->active )
        return false;

    /* The ghost's own tile and layer, so the caller can delete what is there.
     * Its loc id is not part of that: a deletion names the slot. */
    *out_removed = ghost->spec;
    *out_has_restore = ghost->displaced_valid;
    if( ghost->displaced_valid )
        *out_restore = ghost->displaced;

    ghost_loc_clear(ghost);
    return true;
}

void
MapEditorGhost_Commit(struct MapEditorGhost* ghost)
{
    assert(ghost);
    ghost_loc_clear(ghost);
}

void
MapEditorGhost_Place(
    struct MapEditorGhost* ghost,
    struct MapEditorGhostSpec const* spec,
    struct MapEditorGhostSpec const* displaced)
{
    assert(ghost);
    assert(spec);

    ghost_loc_clear(ghost);
    ghost->active = true;
    ghost->spec = *spec;
    if( displaced )
    {
        ghost->displaced_valid = true;
        ghost->displaced = *displaced;
    }
}

void
MapEditorGhost_NoteFaded(struct MapEditorGhost* ghost)
{
    assert(ghost);
    ghost->alpha_done = true;
}

bool
MapEditorGhost_NeedsFade(struct MapEditorGhost const* ghost)
{
    assert(ghost);
    return ghost->active && !ghost->alpha_done;
}

/* ---- the spawn ghost ----------------------------------------------------- */

bool
MapEditorSpawnGhost_Matches(
    struct MapEditorSpawnGhost const* ghost,
    struct MapEditorSpawnSpec const* want)
{
    assert(ghost);
    assert(want);
    if( !ghost->active )
        return false;
    return ghost->spec.kind == want->kind && ghost->spec.id == want->id && ghost->spec.scene_x == want->scene_x
           && ghost->spec.scene_z == want->scene_z && ghost->spec.level == want->level;
}

void
MapEditorSpawnGhost_Place(
    struct MapEditorSpawnGhost* ghost,
    struct MapEditorSpawnSpec const* spec)
{
    assert(ghost);
    assert(spec);
    assert(spec->kind == MAP_EDITOR_SPAWN_NPC || spec->kind == MAP_EDITOR_SPAWN_OBJ);
    assert(spec->id >= 0);
    /* Placing over a ghost that is still up would lose its entity. */
    assert(!ghost->active);

    memset(ghost, 0, sizeof(*ghost));
    ghost->active = true;
    ghost->spec = *spec;
    ghost->entity = -1;
}

void
MapEditorSpawnGhost_Land(
    struct MapEditorSpawnGhost* ghost,
    int entity,
    unsigned load_seq,
    int size)
{
    assert(ghost);
    assert(ghost->active);
    assert(ghost->entity < 0);
    assert(entity >= 0);
    ghost->entity = entity;
    ghost->load_seq = load_seq;
    ghost->size = size > 0 ? size : 1;
}

bool
MapEditorSpawnGhost_Pending(struct MapEditorSpawnGhost const* ghost)
{
    assert(ghost);
    return ghost->active && ghost->entity < 0;
}

bool
MapEditorSpawnGhost_Remove(
    struct MapEditorSpawnGhost* ghost,
    unsigned load_seq,
    struct MapEditorSpawnSpec* out_spec,
    int* out_entity)
{
    assert(ghost);
    assert(out_spec);
    assert(out_entity);

    if( !ghost->active )
        return false;
    *out_spec = ghost->spec;
    *out_entity = ghost->entity >= 0 && ghost->load_seq == load_seq ? ghost->entity : -1;
    memset(ghost, 0, sizeof(*ghost));
    ghost->entity = -1;
    return true;
}
