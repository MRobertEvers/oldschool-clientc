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

/* ---- the spawn preview --------------------------------------------------- */

uint32_t
MapEditorSpawnPreview_RowHash(struct MapEditorSpawnPreviewRow const* row)
{
    uint32_t hash = 2166136261u;
    int values[7];

    assert(row);
    values[0] = row->row.is_obj;
    values[1] = row->row.abs_x;
    values[2] = row->row.abs_z;
    values[3] = row->row.level;
    values[4] = row->row.count;
    values[5] = row->edited;
    values[6] = row->suppressed;
    for( int i = 0; i < 7; i++ )
        hash = (hash ^ (uint32_t)values[i]) * 16777619u;
    for( const char* c = row->row.name; *c; c++ )
        hash = (hash ^ (uint32_t)(unsigned char)*c) * 16777619u;
    return hash;
}

static bool
preview_rows_same(
    struct MapEditorSpawnPreviewRow const* a,
    struct MapEditorSpawnPreviewRow const* b)
{
    return a->row.is_obj == b->row.is_obj && a->row.abs_x == b->row.abs_x && a->row.abs_z == b->row.abs_z
           && a->row.level == b->row.level && a->row.count == b->row.count && a->edited == b->edited
           && a->suppressed == b->suppressed && strcmp(a->row.name, b->row.name) == 0;
}

int
MapEditorSpawnPreview_Reconcile(
    struct MapEditorSpawnPreview* preview,
    struct MapEditorSpawnPreviewRow const* rows,
    int count,
    struct MapEditorSpawnPreviewItem* out_removed)
{
    static struct MapEditorSpawnPreviewItem next[MAP_EDITOR_SPAWN_PREVIEW_MAX];
    int removed = 0;

    assert(preview);
    assert(rows || count == 0);
    assert(out_removed);
    assert(count <= MAP_EDITOR_SPAWN_PREVIEW_MAX);

    for( int i = 0; i < preview->count; i++ )
        preview->items[i].claimed = false;
    for( int r = 0; r < count; r++ )
    {
        uint32_t const hash = MapEditorSpawnPreview_RowHash(&rows[r]);
        struct MapEditorSpawnPreviewItem* item = &next[r];
        int kept = 0;

        for( int i = 0; i < preview->count && !kept; i++ )
        {
            struct MapEditorSpawnPreviewItem* old = &preview->items[i];
            if( old->claimed || old->hash != hash || !preview_rows_same(&old->want, &rows[r]) )
                continue;
            old->claimed = true;
            *item = *old;
            kept = 1;
        }
        if( kept )
            continue;
        memset(item, 0, sizeof(*item));
        item->want = rows[r];
        item->hash = hash;
        item->id = MAP_EDITOR_SPAWN_PREVIEW_ID_UNKNOWN;
        item->entity = -1;
        item->element_id = -1;
        item->size = 1;
    }
    for( int i = 0; i < preview->count; i++ )
        if( !preview->items[i].claimed && preview->items[i].entity >= 0 )
            out_removed[removed++] = preview->items[i];
    memcpy(preview->items, next, sizeof(next[0]) * (size_t)count);
    preview->count = count;
    preview->cursor = 0;
    return removed;
}

void
MapEditorSpawnPreview_WorldLost(struct MapEditorSpawnPreview* preview)
{
    assert(preview);
    for( int i = 0; i < preview->count; i++ )
    {
        preview->items[i].entity = -1;
        preview->items[i].element_id = -1;
        preview->items[i].size = 1;
        preview->items[i].faded = false;
    }
    preview->cursor = 0;
}

struct MapEditorSpawnPreviewItem*
MapEditorSpawnPreview_FindElement(
    struct MapEditorSpawnPreview* preview,
    int element_id)
{
    assert(preview);
    if( element_id < 0 )
        return NULL;
    for( int i = 0; i < preview->count; i++ )
        if( preview->items[i].element_id == element_id )
            return &preview->items[i];
    return NULL;
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

/* ---- the wander preview ------------------------------------------------------ */

int
MapEditorWander_Rects(
    int x,
    int z,
    int level,
    int size,
    int wanderrange,
    int maxrange,
    struct MapEditorWanderRect* out,
    int max)
{
    int count = 0;

    assert(out || max == 0);

    if( size < 1 )
        size = 1;
    if( wanderrange < 0 )
        wanderrange = MAP_EDITOR_WANDER_DEFAULT;
    if( maxrange < 0 )
        maxrange = MAP_EDITOR_LEASH_DEFAULT;
    if( wanderrange > MAP_EDITOR_WANDER_RANGE_MAX )
        wanderrange = MAP_EDITOR_WANDER_RANGE_MAX;
    if( maxrange > MAP_EDITOR_WANDER_RANGE_MAX )
        maxrange = MAP_EDITOR_WANDER_RANGE_MAX;

    if( wanderrange > 0 && count < max )
    {
        out[count].kind = MAP_EDITOR_WANDER_AREA;
        out[count].x0 = x - wanderrange;
        out[count].z0 = z - wanderrange;
        out[count].x1 = x + size - 1 + wanderrange;
        out[count].z1 = z + size - 1 + wanderrange;
        out[count].level = level;
        count++;
    }
    if( maxrange > wanderrange && count < max )
    {
        out[count].kind = MAP_EDITOR_WANDER_LEASH;
        out[count].x0 = x - maxrange;
        out[count].z0 = z - maxrange;
        out[count].x1 = x + size - 1 + maxrange;
        out[count].z1 = z + size - 1 + maxrange;
        out[count].level = level;
        count++;
    }
    return count;
}

int
MapEditorWander_RectTiles(struct MapEditorWanderRect const* rect)
{
    assert(rect);
    return (rect->x1 - rect->x0 + 1) * (rect->z1 - rect->z0 + 1);
}

/* ---- the multi-piece ghost ---------------------------------------------------- */

void
MapEditorPieceGhost_Clear(struct MapEditorPieceGhost* ghost)
{
    assert(ghost);
    ghost->source = MAP_EDITOR_PIECE_GHOST_NONE;
    ghost->count = 0;
    ghost->taken_count = 0;
    ghost->dropped = 0;
}

bool
MapEditorPieceGhost_Matches(
    struct MapEditorPieceGhost const* ghost,
    enum MapEditorPieceGhostSource source,
    int key_x,
    int key_z,
    int key_level,
    int key_turn,
    unsigned key_serial)
{
    assert(ghost);
    return ghost->source != MAP_EDITOR_PIECE_GHOST_NONE && ghost->source == source && ghost->key_x == key_x
           && ghost->key_z == key_z && ghost->key_level == key_level && ghost->key_turn == key_turn
           && ghost->key_serial == key_serial;
}

void
MapEditorPieceGhost_Begin(
    struct MapEditorPieceGhost* ghost,
    enum MapEditorPieceGhostSource source,
    int key_x,
    int key_z,
    int key_level,
    int key_turn,
    unsigned key_serial)
{
    assert(ghost);
    assert(source != MAP_EDITOR_PIECE_GHOST_NONE);
    MapEditorPieceGhost_Clear(ghost);
    ghost->source = source;
    ghost->key_x = key_x;
    ghost->key_z = key_z;
    ghost->key_level = key_level;
    ghost->key_turn = key_turn;
    ghost->key_serial = key_serial;
}

bool
MapEditorPieceGhost_Add(
    struct MapEditorPieceGhost* ghost,
    struct MapEditorPieceGhostPiece const* piece)
{
    assert(ghost);
    assert(piece);
    assert(ghost->source != MAP_EDITOR_PIECE_GHOST_NONE);
    assert(piece->size_x >= 1);
    assert(piece->size_z >= 1);
    if( ghost->count >= MAP_EDITOR_PIECE_GHOST_MAX )
    {
        ghost->dropped++;
        return false;
    }
    ghost->pieces[ghost->count++] = *piece;
    if( piece->taken )
        ghost->taken_count++;
    return true;
}
