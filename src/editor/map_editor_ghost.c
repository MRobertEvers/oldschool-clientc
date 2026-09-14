#include "editor/map_editor_ghost.h"

#include <assert.h>
#include <string.h>

void
MapEditorGhost_Reset(struct MapEditorGhost* ghost)
{
    assert(ghost);
    memset(ghost, 0, sizeof(*ghost));
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

    MapEditorGhost_Reset(ghost);
    return true;
}

void
MapEditorGhost_Commit(struct MapEditorGhost* ghost)
{
    assert(ghost);
    MapEditorGhost_Reset(ghost);
}

void
MapEditorGhost_Place(
    struct MapEditorGhost* ghost,
    struct MapEditorGhostSpec const* spec,
    struct MapEditorGhostSpec const* displaced)
{
    assert(ghost);
    assert(spec);

    MapEditorGhost_Reset(ghost);
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
