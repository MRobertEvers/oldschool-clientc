/*
 * Workbench 6.3's document ops and their panel actions
 * (edpanel_ops_stamps.h): stamps, the level move, Find same, and the
 * gizmo's copy and turn by a given amount; and the spawns a stamp, the
 * rectangle's clipboard and a square copy carry (one capture, one drop).
 */

#include "edpanel_ops_stamps.h"

#include "edpanel_common.h"
#include "editor/loc_editor_selection.h"
#include "engine/torirs_types.h"
#include "mem/torirs_mem.h"

#include <stdio.h>
#include <string.h>

/** The open square holding absolute tile (x, z), and its local tile. */
static struct Editor_Square*
stamps_square_at(
    struct Editor_Doc* doc,
    int world_x,
    int world_z,
    int* out_x,
    int* out_z)
{
    struct Editor_Square* square;

    if( world_x < 0 || world_z < 0 )
        return NULL;
    square = Editor_DocFindSquare(doc, world_x / EDITOR_SQUARE_X, world_z / EDITOR_SQUARE_Z);
    if( !square || !square->loaded )
        return NULL;
    *out_x = world_x % EDITOR_SQUARE_X;
    *out_z = world_z % EDITOR_SQUARE_Z;
    return square;
}

/* ---- a square's spawns --------------------------------------------------------- */

/** One square's three spawn files, as a capture or a drop reads them: the
 *  edited file (borrowed from the document; NULL when it is not open) and
 *  the generated and the quests' / skills' rows, parsed here from their TEXT
 *  documents (no rows when the document is not open, or the square has no
 *  such file). */
struct StampsSquareSpawns
{
    int used;
    int map_x;
    int map_z;
    const struct Editor_SpawnFile* edited;
    struct Editor_SpawnFile generated;
    struct Editor_SpawnFile scripted;
};

static void
stamps_square_spawns_load(
    struct Editor_Doc* doc,
    int map_x,
    int map_z,
    struct StampsSquareSpawns* out)
{
    struct Editor_DocHandle handle;
    char key[EDITOR_DOC_KEY_MAX];

    memset(out, 0, sizeof(*out));
    out->used = 1;
    out->map_x = map_x;
    out->map_z = map_z;
    Editor_SpawnInit(&out->generated);
    Editor_SpawnInit(&out->scripted);
    Editor_SquareKey(key, map_x, map_z);
    if( Editor_DocFind(doc, EDITOR_DOC_SPAWN, key, &handle) && handle.spawn->loaded )
        out->edited = &handle.spawn->file;
    /* A file that does not parse lists what parsed: the Spawns panel says
     * which line, and a copy is no place to stop for it. */
    Editor_SpawnGeneratedKey(key, map_x, map_z);
    if( Editor_DocFind(doc, EDITOR_DOC_TEXT, key, &handle) && handle.text->text )
        (void)Editor_SpawnParse(&out->generated, handle.text->text, handle.text->length);
    Editor_SpawnScriptedKey(key, map_x, map_z);
    if( Editor_DocFind(doc, EDITOR_DOC_TEXT, key, &handle) && handle.text->text )
        (void)Editor_SpawnParse(&out->scripted, handle.text->text, handle.text->length);
}

static void
stamps_square_spawns_free(struct StampsSquareSpawns* files)
{
    Editor_SpawnFree(&files->generated);
    Editor_SpawnFree(&files->scripted);
    memset(files, 0, sizeof(*files));
}

/** Whether a live spawn equal to `row` stands in the square already: a row
 *  of the edited file, or one of the read-only files' that the edited file
 *  does not suppress. */
static int
stamps_spawn_stands(
    const struct StampsSquareSpawns* files,
    const struct Editor_SpawnRow* row)
{
    if( files->edited && Editor_SpawnFind(files->edited, row) >= 0 )
        return 1;
    if( Editor_SpawnFind(&files->generated, row) < 0 && Editor_SpawnFind(&files->scripted, row) < 0 )
        return 0;
    return !Editor_SpawnIsSuppressed(files->edited, row);
}

/** `file`'s live rows standing in `box`, appended to `stamp`. `suppressor`
 *  is the edited file whose suppression rows hide rows of a read-only
 *  `file`; NULL for the edited file itself (and when there is none). Returns
 *  the pieces added. */
static int
stamps_capture_rows(
    const struct Editor_SpawnFile* file,
    const struct Editor_SpawnFile* suppressor,
    const struct Editor_StampSpawnBox* box,
    struct Editor_Stamp* stamp)
{
    int added = 0;

    for( int l = 0; l < file->line_count; l++ )
    {
        const struct Editor_SpawnLine* line = &file->lines[l];
        struct Editor_StampPiece piece;

        if( line->kind != EDITOR_SPAWN_LINE_ROW || line->row.suppress )
            continue;
        if( line->row.abs_x < box->x0 || line->row.abs_x > box->x1 || line->row.abs_z < box->z0
            || line->row.abs_z > box->z1 || line->row.level < 0 || line->row.level >= EDITOR_SQUARE_LEVELS
            || !(box->levels & (1u << line->row.level)) )
            continue;
        if( suppressor && Editor_SpawnIsSuppressed(suppressor, &line->row) )
            continue;
        memset(&piece, 0, sizeof(piece));
        piece.kind = EDITOR_STAMP_SPAWN;
        piece.dx = line->row.abs_x - box->origin_x;
        piece.dz = line->row.abs_z - box->origin_z;
        piece.dlevel = line->row.level - box->origin_level;
        piece.is_obj = line->row.is_obj;
        snprintf(piece.name, sizeof(piece.name), "%s", line->row.name);
        piece.count = line->row.count < 1 ? 1 : line->row.count;
        added += Editor_StampAdd(stamp, &piece) ? 1 : 0;
    }
    return added;
}

/** Whether square (map_x, map_z) holds any tile of (x0..x1, z0..z1). */
static int
stamps_square_touches(
    int map_x,
    int map_z,
    int x0,
    int z0,
    int x1,
    int z1)
{
    return map_x * EDITOR_SQUARE_X <= x1 && map_x * EDITOR_SQUARE_X + EDITOR_SQUARE_X - 1 >= x0
           && map_z * EDITOR_SQUARE_Z <= z1 && map_z * EDITOR_SQUARE_Z + EDITOR_SQUARE_Z - 1 >= z0;
}

/** The rows of square (map_x, map_z)'s three files standing in `box`, the
 *  edited ones first (the order the Spawns panel lists them in). */
static int
stamps_capture_square(
    struct Editor_Doc* doc,
    int map_x,
    int map_z,
    const struct Editor_StampSpawnBox* box,
    struct Editor_Stamp* stamp)
{
    struct StampsSquareSpawns files;
    int added = 0;

    stamps_square_spawns_load(doc, map_x, map_z, &files);
    if( files.edited )
        added += stamps_capture_rows(files.edited, NULL, box, stamp);
    added += stamps_capture_rows(&files.generated, files.edited, box, stamp);
    added += stamps_capture_rows(&files.scripted, files.edited, box, stamp);
    stamps_square_spawns_free(&files);
    return added;
}

int
Editor_StampCaptureSpawns(
    struct Editor_Doc* doc,
    const struct Editor_StampSpawnBox* box,
    struct Editor_Stamp* stamp)
{
    int added = 0;

    assert(doc);
    assert(box);
    assert(stamp);

    for( int i = 0; i < doc->square_count; i++ )
    {
        const struct Editor_Square* square = &doc->squares[i];

        if( !square->loaded || !stamps_square_touches(square->map_x, square->map_z, box->x0, box->z0, box->x1, box->z1) )
            continue;
        added += stamps_capture_square(doc, square->map_x, square->map_z, box, stamp);
    }
    return added;
}

int
Editor_StampCaptureSquareSpawns(
    struct Editor_Doc* doc,
    int map_x,
    int map_z,
    struct Editor_Stamp* stamp)
{
    struct Editor_StampSpawnBox box;

    assert(doc);
    assert(stamp);
    assert(map_x >= 0);
    assert(map_z >= 0);

    Editor_StampClear(stamp);
    memset(&box, 0, sizeof(box));
    box.x0 = map_x * EDITOR_SQUARE_X;
    box.z0 = map_z * EDITOR_SQUARE_Z;
    box.x1 = box.x0 + EDITOR_SQUARE_X - 1;
    box.z1 = box.z0 + EDITOR_SQUARE_Z - 1;
    box.levels = (1u << EDITOR_SQUARE_LEVELS) - 1u;
    box.origin_x = box.x0;
    box.origin_z = box.z0;
    box.origin_level = 0;
    return stamps_capture_square(doc, map_x, map_z, &box, stamp);
}

/* ---- capture ---------------------------------------------------------------- */

int
Editor_StampCaptureSelection(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    const int* sizes,
    int with_tiles,
    int with_spawns,
    struct Editor_Stamp* stamp)
{
    char name[EDITOR_STAMP_NAME_MAX];
    int origin_x;
    int origin_z;
    int origin_level;
    int x0 = 0;
    int z0 = 0;
    int x1 = -1;
    int z1 = -1;
    unsigned levels = 0;

    assert(doc);
    assert(refs);
    assert(n > 0);
    assert(stamp);

    snprintf(name, sizeof(name), "%s", stamp->name);
    Editor_StampClear(stamp);
    snprintf(stamp->name, sizeof(stamp->name), "%s", name);
    Editor_LocRefWorld(&refs[0], &origin_x, &origin_z);
    origin_level = refs[0].level;

    for( int i = 0; i < n; i++ )
    {
        struct Editor_Square* square = Editor_DocFindSquare(doc, refs[i].map_x, refs[i].map_z);
        int const index = Editor_LocRefResolve(doc, &refs[i]);
        struct Editor_StampPiece piece;
        int wx;
        int wz;

        if( index < 0 )
            continue;
        Editor_LocRefWorld(&refs[i], &wx, &wz);
        memset(&piece, 0, sizeof(piece));
        piece.kind = EDITOR_STAMP_LOC;
        piece.dx = wx - origin_x;
        piece.dz = wz - origin_z;
        piece.dlevel = refs[i].level - origin_level;
        piece.loc_id = square->locs[index].loc_id;
        piece.shape = square->locs[index].shape;
        piece.rotation = square->locs[index].rotation;
        piece.size_x = sizes && sizes[2 * i] > 0 ? sizes[2 * i] : 1;
        piece.size_z = sizes && sizes[2 * i + 1] > 0 ? sizes[2 * i + 1] : 1;
        if( !Editor_StampAdd(stamp, &piece) )
            continue;
        if( x1 < x0 )
        {
            x0 = wx;
            z0 = wz;
            x1 = wx + piece.size_x - 1;
            z1 = wz + piece.size_z - 1;
        }
        if( wx < x0 )
            x0 = wx;
        if( wz < z0 )
            z0 = wz;
        if( wx + piece.size_x - 1 > x1 )
            x1 = wx + piece.size_x - 1;
        if( wz + piece.size_z - 1 > z1 )
            z1 = wz + piece.size_z - 1;
        levels |= 1u << refs[i].level;
    }
    if( stamp->count == 0 )
        return 0;

    /* The ground under it: every tile of the box, on each level a loc
     * stands on, its tokens exactly (an absent tile is a bare line). */
    for( int level = 0; with_tiles && level < EDITOR_SQUARE_LEVELS; level++ )
    {
        if( !(levels & (1u << level)) )
            continue;
        for( int wz = z0; wz <= z1; wz++ )
            for( int wx = x0; wx <= x1; wx++ )
            {
                struct Editor_StampPiece piece;
                int lx;
                int lz;
                struct Editor_Square* square = stamps_square_at(doc, wx, wz, &lx, &lz);
                if( !square )
                    continue;
                memset(&piece, 0, sizeof(piece));
                piece.kind = EDITOR_STAMP_TILE;
                piece.dx = wx - origin_x;
                piece.dz = wz - origin_z;
                piece.dlevel = level - origin_level;
                piece.tile = square->tiles[Editor_TileIndex(lx, lz, level)];
                (void)Editor_StampAdd(stamp, &piece);
            }
    }
    /* The spawns standing in it, whichever file makes each: a house's npcs
     * are generated rows as often as edited ones, and the stamp that left
     * them behind placed an empty house (2026-11-09). */
    if( with_spawns )
    {
        struct Editor_StampSpawnBox box;
        memset(&box, 0, sizeof(box));
        box.x0 = x0;
        box.z0 = z0;
        box.x1 = x1;
        box.z1 = z1;
        box.levels = levels;
        box.origin_x = origin_x;
        box.origin_z = origin_z;
        box.origin_level = origin_level;
        (void)Editor_StampCaptureSpawns(doc, &box, stamp);
    }
    return stamp->count;
}

int
Editor_StampCaptureRect(
    struct Editor_Doc* doc,
    const struct Editor_TileRect* rect,
    struct Editor_Stamp* stamp)
{
    struct Editor_TileRect r;

    assert(doc);
    assert(rect);
    assert(stamp);

    Editor_StampClear(stamp);
    if( !Editor_TileRectNormalise(rect, &r) )
        return 0;
    for( int i = 0; i < doc->square_count; i++ )
    {
        const struct Editor_Square* square = &doc->squares[i];
        if( !square->loaded )
            continue;
        for( int l = 0; l < square->loc_count; l++ )
        {
            const struct Editor_Loc* loc = &square->locs[l];
            int const wx = square->map_x * EDITOR_SQUARE_X + loc->x;
            int const wz = square->map_z * EDITOR_SQUARE_Z + loc->z;
            struct Editor_StampPiece piece;
            if( loc->level != r.level || wx < r.x0 || wx > r.x1 || wz < r.z0 || wz > r.z1 )
                continue;
            memset(&piece, 0, sizeof(piece));
            piece.kind = EDITOR_STAMP_LOC;
            piece.dx = wx - r.x0;
            piece.dz = wz - r.z0;
            piece.loc_id = loc->loc_id;
            piece.shape = loc->shape;
            piece.rotation = loc->rotation;
            piece.size_x = 1;
            piece.size_z = 1;
            (void)Editor_StampAdd(stamp, &piece);
        }
    }
    return Editor_StampCount(stamp, EDITOR_STAMP_LOC);
}

/* ---- the drop --------------------------------------------------------------------- */

/** Where piece `p` lands with the origin at (wx, wz, level); 0 when that is
 *  off the world's planes. */
static int
stamps_landing(
    const struct Editor_StampPiece* p,
    int world_x,
    int world_z,
    int level,
    int* out_x,
    int* out_z,
    int* out_level)
{
    *out_x = world_x + p->dx;
    *out_z = world_z + p->dz;
    *out_level = level + p->dlevel;
    return *out_x >= 0 && *out_z >= 0 && *out_level >= 0 && *out_level < EDITOR_SQUARE_LEVELS;
}

static void
stamps_skip(
    struct Editor_StampDropResult* result,
    int x,
    int z,
    int level)
{
    if( result->skipped++ == 0 )
    {
        result->skipped_x = x;
        result->skipped_z = z;
        result->skipped_level = level;
    }
}

int
Editor_StampDropCommands(
    struct Editor_Doc* doc,
    const struct Editor_Stamp* stamp,
    int world_x,
    int world_z,
    int level,
    struct Editor_Cmd* out,
    int max,
    struct Editor_StampDropResult* result)
{
    static const enum Editor_StampPieceKind k_order[3] = { EDITOR_STAMP_TILE, EDITOR_STAMP_LOC, EDITOR_STAMP_SPAWN };
    /* The landing squares' spawn files, read once each: a drop of 64 x 64
     * tiles lands in four squares at most, and a wider stamp reuses the
     * slots in turn. */
    struct StampsSquareSpawns landing[4];
    int landing_next = 0;
    struct Editor_LocOpResult place;
    int count = 0;

    assert(doc);
    assert(stamp);
    assert(out);
    assert(result);
    assert(level >= 0);
    assert(level < EDITOR_SQUARE_LEVELS);

    memset(landing, 0, sizeof(landing));
    memset(result, 0, sizeof(*result));
    /* Tiles first, so a loc's replace and a spawn land on the new ground in
     * the history as they will in the world. */
    for( int pass = 0; pass < 3; pass++ )
        for( int i = 0; i < stamp->count && count < max; i++ )
        {
            const struct Editor_StampPiece* p = &stamp->pieces[i];
            struct Editor_Square* square;
            int wx;
            int wz;
            int wl;
            int lx;
            int lz;

            if( p->kind != k_order[pass] )
                continue;
            if( !stamps_landing(p, world_x, world_z, level, &wx, &wz, &wl) )
            {
                stamps_skip(result, world_x + p->dx, world_z + p->dz, level + p->dlevel);
                continue;
            }
            square = stamps_square_at(doc, wx, wz, &lx, &lz);
            if( !square )
            {
                stamps_skip(result, wx, wz, wl);
                continue;
            }
            if( p->kind == EDITOR_STAMP_TILE )
            {
                const struct Editor_Tile* now = &square->tiles[Editor_TileIndex(lx, lz, wl)];
                if( memcmp(now, &p->tile, sizeof(*now)) == 0 )
                    continue;
                Editor_CmdInitSquare(&out[count], EDITOR_CMD_TILE, square->map_x, square->map_z);
                out[count].has_before = 1;
                out[count].has_after = 1;
                out[count].u.tile.level = wl;
                out[count].u.tile.x = lx;
                out[count].u.tile.z = lz;
                out[count].u.tile.before = *now;
                out[count].u.tile.after = p->tile;
                count++;
                result->tiles++;
            }
            else if( p->kind == EDITOR_STAMP_LOC )
            {
                if( !Editor_LocOpsPlace(doc, p->loc_id, p->shape, p->rotation, wx, wz, wl, &out[count], max - count,
                        &place) )
                    continue;
                if( out[count].has_before )
                    result->replaced++;
                if( result->ref_count < EDITOR_LOC_SELECTION_MAX )
                    result->refs[result->ref_count++] = place.refs[0];
                count++;
                result->locs++;
            }
            else
            {
                struct StampsSquareSpawns* files = NULL;
                struct Editor_SpawnRow row;
                memset(&row, 0, sizeof(row));
                row.is_obj = p->is_obj;
                snprintf(row.name, sizeof(row.name), "%s", p->name);
                row.abs_x = wx;
                row.abs_z = wz;
                row.level = wl;
                row.count = p->count < 1 ? 1 : p->count;
                for( int s = 0; s < 4 && !files; s++ )
                    if( landing[s].used && landing[s].map_x == square->map_x && landing[s].map_z == square->map_z )
                        files = &landing[s];
                if( !files )
                {
                    files = &landing[landing_next];
                    landing_next = (landing_next + 1) % 4;
                    if( files->used )
                        stamps_square_spawns_free(files);
                    stamps_square_spawns_load(doc, square->map_x, square->map_z, files);
                }
                /* Read before anything applies, so two equal pieces of one
                 * stamp (two goblins on a tile) both land; the same stamp
                 * a second time finds them standing. */
                if( stamps_spawn_stands(files, &row) )
                    continue;
                Editor_SpawnCommand(&out[count], &row, 1);
                count++;
                result->spawns++;
            }
        }
    for( int s = 0; s < 4; s++ )
        if( landing[s].used )
            stamps_square_spawns_free(&landing[s]);
    return count;
}

int
Editor_StampPieceLanding(
    struct Editor_Doc* doc,
    const struct Editor_Stamp* stamp,
    int index,
    int world_x,
    int world_z,
    int level,
    int* out_x,
    int* out_z,
    int* out_level,
    int* out_holder)
{
    const struct Editor_StampPiece* p;
    struct Editor_Square* square;
    int lx;
    int lz;
    int held;

    assert(doc);
    assert(stamp);
    assert(index >= 0);
    assert(index < stamp->count);
    assert(out_x);
    assert(out_z);
    assert(out_level);
    assert(out_holder);

    p = &stamp->pieces[index];
    *out_holder = -1;
    if( !stamps_landing(p, world_x, world_z, level, out_x, out_z, out_level) )
        return 0;
    square = stamps_square_at(doc, *out_x, *out_z, &lx, &lz);
    if( !square )
        return 0;
    if( p->kind == EDITOR_STAMP_LOC )
    {
        held = Editor_SquareLocFind(square, *out_level, lx, lz, p->shape);
        if( held >= 0 )
            *out_holder = square->locs[held].loc_id;
    }
    return 1;
}

/* ---- the level move ------------------------------------------------------------------ */

/** Index into refs[0..n) of a ref naming (square, level, x, z, shape), or -1. */
static int
stamps_ref_index(
    const struct Editor_LocRef* refs,
    int n,
    int map_x,
    int map_z,
    int level,
    int x,
    int z,
    int shape)
{
    for( int i = 0; i < n; i++ )
        if( refs[i].map_x == map_x && refs[i].map_z == map_z && refs[i].level == level && refs[i].x == x
            && refs[i].z == z && refs[i].shape == shape )
            return i;
    return -1;
}

int
Editor_LocOpsLevelMove(
    struct Editor_Doc* doc,
    const struct Editor_LocRef* refs,
    int n,
    int delta,
    struct Editor_Cmd* out,
    int max,
    struct Editor_LevelMoveResult* result)
{
    int index[EDITOR_LOC_SELECTION_MAX];
    int order[EDITOR_LOC_SELECTION_MAX];
    int live = 0;
    int count = 0;

    assert(doc);
    assert(refs);
    assert(n > 0);
    assert(n <= EDITOR_LOC_SELECTION_MAX);
    assert(delta != 0);
    assert(out);
    assert(result);

    memset(result, 0, sizeof(*result));
    result->holder_loc_id = -1;
    for( int i = 0; i < n; i++ )
    {
        struct Editor_Square* square = Editor_DocFindSquare(doc, refs[i].map_x, refs[i].map_z);
        int target;
        int held;

        Editor_LocRefWorld(&refs[i], &result->at_x, &result->at_z);
        result->at_level = refs[i].level;
        if( !square || !square->loaded )
        {
            result->refusal = EDITOR_LEVEL_MOVE_NOT_OPEN;
            return 0;
        }
        index[i] = Editor_LocRefResolve(doc, &refs[i]);
        if( index[i] < 0 )
            continue;
        target = LocEditorSelection_LevelTarget(refs[i].level, delta);
        if( target < 0 )
        {
            result->refusal = EDITOR_LEVEL_MOVE_NO_LEVEL;
            return 0;
        }
        /* Held by a loc the move takes along is fine (it goes on up);
         * held by any other has nowhere to go -- there is no tile "left
         * empty" on another plane, so the swap rule has nothing to say. */
        held = Editor_SquareLocFind(square, target, refs[i].x, refs[i].z, refs[i].shape);
        if( held >= 0
            && stamps_ref_index(refs, n, refs[i].map_x, refs[i].map_z, target, refs[i].x, refs[i].z, refs[i].shape)
                   < 0 )
        {
            result->refusal = EDITOR_LEVEL_MOVE_TAKEN;
            result->holder_loc_id = square->locs[held].loc_id;
            return 0;
        }
        order[live++] = i;
    }
    if( live == 0 )
    {
        result->refusal = EDITOR_LEVEL_MOVE_GONE;
        return 0;
    }
    if( max < live )
        return 0;
    /* The far end first: going up, the top loc leaves its key before the
     * one under it arrives (and the reverse going down). */
    for( int a = 1; a < live; a++ )
        for( int b = a; b > 0; b-- )
        {
            int const lb = refs[order[b]].level;
            int const la = refs[order[b - 1]].level;
            if( delta > 0 ? lb > la : lb < la )
            {
                int const t = order[b];
                order[b] = order[b - 1];
                order[b - 1] = t;
            }
        }
    for( int k = 0; k < live; k++ )
    {
        int const i = order[k];
        struct Editor_Square* square = Editor_DocFindSquare(doc, refs[i].map_x, refs[i].map_z);
        Editor_CmdInitSquare(&out[count], EDITOR_CMD_LOC, refs[i].map_x, refs[i].map_z);
        out[count].has_before = 1;
        out[count].has_after = 1;
        out[count].u.loc.before = square->locs[index[i]];
        out[count].u.loc.after = square->locs[index[i]];
        out[count].u.loc.after.level = refs[i].level + delta;
        count++;
    }
    for( int i = 0; i < n; i++ )
    {
        result->refs[i] = refs[i];
        if( index[i] >= 0 )
            result->refs[i].level = refs[i].level + delta;
    }
    result->ref_count = n;
    return count;
}

int
Editor_LocOpsFindSame(
    struct Editor_Doc* doc,
    int loc_id,
    int level,
    struct Editor_LocRef* out,
    int max,
    int* out_total)
{
    int count = 0;
    int total = 0;

    assert(doc);
    assert(out || max == 0);
    assert(level >= 0);
    assert(level < EDITOR_SQUARE_LEVELS);

    for( int i = 0; i < doc->square_count; i++ )
    {
        const struct Editor_Square* square = &doc->squares[i];
        if( !square->loaded )
            continue;
        for( int l = 0; l < square->loc_count; l++ )
        {
            const struct Editor_Loc* loc = &square->locs[l];
            if( loc->loc_id != loc_id || loc->level != level )
                continue;
            total++;
            if( count < max )
                Editor_LocRefAt(&out[count++], square->map_x * EDITOR_SQUARE_X + loc->x,
                    square->map_z * EDITOR_SQUARE_Z + loc->z, loc->level, loc->shape, loc->loc_id, loc->rotation);
        }
    }
    if( out_total )
        *out_total = total;
    return count;
}

/* ---- the panel actions ------------------------------------------------------------------ */

/** One op at a time; ~8 KB each, so not on the stack. */
static struct Editor_LocOpResult g_op_result;
static struct Editor_LevelMoveResult g_level_result;
static struct Editor_StampDropResult g_drop_result;

/** The armed stamp, and the pick it was armed over (a new pick disarms). */
static struct Editor_Stamp g_armed;
static int g_armed_valid;
static int g_armed_pick_kind;
static int g_armed_pick_id;
/** The unnamed stamp: the rectangle's locs, and the spawns standing in it
 *  (apart, so a paste can leave them out). */
static struct Editor_Stamp g_clip;
static struct Editor_Stamp g_clip_spawns;
/** A square copy's spawns, between its capture and its drop. */
static struct Editor_Stamp g_square_spawns;

/** Square (map_x, map_z)'s three spawn documents into this window's mirror:
 *  the edited `.spawn` a drop writes, and the two read-only ones. A square
 *  with no such file opens as an empty document, once. */
static void
stamps_square_docs(
    struct EditorUi* ui,
    int map_x,
    int map_z)
{
    char key[EDITOR_DOC_KEY_MAX];

    Editor_SquareKey(key, map_x, map_z);
    (void)edp_ensure_doc(ui, EDITOR_DOC_SPAWN, key);
    Editor_SpawnGeneratedKey(key, map_x, map_z);
    (void)edp_ensure_doc(ui, EDITOR_DOC_TEXT, key);
    Editor_SpawnScriptedKey(key, map_x, map_z);
    (void)edp_ensure_doc(ui, EDITOR_DOC_TEXT, key);
}

void
edp_square_spawn_docs(
    struct EditorUi* ui,
    int map_x,
    int map_z)
{
    assert(ui);
    assert(map_x >= 0);
    assert(map_z >= 0);
    if( !ui->editor )
        return;
    stamps_square_docs(ui, map_x, map_z);
}

void
edp_stamp_spawn_docs(
    struct EditorUi* ui,
    int x0,
    int z0,
    int x1,
    int z1)
{
    assert(ui);
    if( !ui->editor )
        return;
    /* By index and by value: opening a document may grow the mirror's
     * tables, never the square array, and a square's coordinates are all
     * this reads. */
    for( int i = 0; i < ui->editor->doc.square_count; i++ )
    {
        int const map_x = ui->editor->doc.squares[i].map_x;
        int const map_z = ui->editor->doc.squares[i].map_z;
        if( !ui->editor->doc.squares[i].loaded || !stamps_square_touches(map_x, map_z, x0, z0, x1, z1) )
            continue;
        stamps_square_docs(ui, map_x, map_z);
    }
}

/** The spawn documents of the squares `stamp`'s spawn pieces land in, with
 *  its origin at (world_x, world_z). */
static void
stamps_landing_docs(
    struct EditorUi* ui,
    const struct Editor_Stamp* stamp,
    int world_x,
    int world_z)
{
    int x0 = 0;
    int z0 = 0;
    int x1 = -1;
    int z1 = -1;

    for( int i = 0; i < stamp->count; i++ )
    {
        const struct Editor_StampPiece* p = &stamp->pieces[i];
        if( p->kind != EDITOR_STAMP_SPAWN )
            continue;
        if( x1 < x0 )
        {
            x0 = p->dx;
            x1 = p->dx;
            z0 = p->dz;
            z1 = p->dz;
        }
        if( p->dx < x0 )
            x0 = p->dx;
        if( p->dx > x1 )
            x1 = p->dx;
        if( p->dz < z0 )
            z0 = p->dz;
        if( p->dz > z1 )
            z1 = p->dz;
    }
    if( x1 < x0 )
        return;
    edp_stamp_spawn_docs(ui, world_x + x0, world_z + z0, world_x + x1, world_z + z1);
}

static int
stamps_need_selection(
    struct EditorUi* ui,
    const char* what)
{
    if( !ui->editor )
    {
        Editor_UiStatus(ui, "%s refused: there is no editor session", what);
        return 0;
    }
    if( ui->tools.loc_count == 0 )
    {
        Editor_UiStatus(ui, "%s refused: select a loc first (Shift+click adds more)", what);
        return 0;
    }
    return 1;
}

static void
stamps_select(
    struct EditorUi* ui,
    const struct Editor_LocRef* refs,
    int count)
{
    if( count > EDITOR_LOC_SELECTION_MAX )
        count = EDITOR_LOC_SELECTION_MAX;
    memcpy(ui->tools.locs, refs, sizeof(refs[0]) * (size_t)count);
    ui->tools.loc_count = count;
    if( ui->hooks.selection_changed )
        ui->hooks.selection_changed(ui->hooks_user);
}

int
edp_locs_level_move(
    struct EditorUi* ui,
    int delta)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;
    const char* const what = delta > 0 ? "Level up" : "Level down";

    assert(ui);
    assert(delta != 0);
    if( !stamps_need_selection(ui, what) )
        return 0;
    count = Editor_LocOpsLevelMove(
        &ui->editor->doc, ui->tools.locs, ui->tools.loc_count, delta, commands, capacity, &g_level_result);
    if( count == 0 )
    {
        const struct Editor_LevelMoveResult* r = &g_level_result;
        if( r->refusal == EDITOR_LEVEL_MOVE_NO_LEVEL )
            Editor_UiStatus(ui, "%s refused: the loc at %d,%d L%d has no level %s it (levels are 0 to 3)", what,
                r->at_x, r->at_z, r->at_level, delta > 0 ? "above" : "below");
        else if( r->refusal == EDITOR_LEVEL_MOVE_NOT_OPEN )
            Editor_UiStatus(ui, "%s refused: m%d_%d is not open; open it to move its locs", what, r->at_x / 64,
                r->at_z / 64);
        else if( r->refusal == EDITOR_LEVEL_MOVE_TAKEN )
            Editor_UiStatus(ui, "%s refused: %d,%d L%d already holds loc %d of that shape", what, r->at_x, r->at_z,
                r->at_level + delta, r->holder_loc_id);
        else
            Editor_UiStatus(ui, "%s refused: the selected locs are no longer there (undone or deleted elsewhere)",
                what);
        return 0;
    }
    applied = edp_apply(ui, commands, count, delta > 0 ? "level up" : "level down");
    if( applied > 0 )
    {
        stamps_select(ui, g_level_result.refs, g_level_result.ref_count);
        Editor_UiStatus(ui, "Moved %d loc%s %s a level", applied, applied == 1 ? "" : "s", delta > 0 ? "up" : "down");
    }
    return applied;
}

int
edp_locs_duplicate_by(
    struct EditorUi* ui,
    int dx,
    int dz)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    assert(dx != 0 || dz != 0);
    if( !stamps_need_selection(ui, "Duplicate") )
        return 0;
    count = Editor_LocOpsDuplicate(
        &ui->editor->doc, ui->tools.locs, ui->tools.loc_count, dx, dz, commands, capacity, &g_op_result);
    if( count == 0 )
    {
        if( g_op_result.blocked > 0 )
        {
            int wx;
            int wz;
            Editor_LocRefWorld(&g_op_result.blocked_at, &wx, &wz);
            Editor_UiStatus(ui, "Duplicate refused: %d,%d L%d already holds loc %d of that shape", wx, wz,
                g_op_result.blocked_at.level, g_op_result.blocked_at.loc_id);
        }
        else
            Editor_UiStatus(ui, "Duplicate refused: the selected locs are no longer there");
        return 0;
    }
    applied = edp_apply(ui, commands, count, "duplicate");
    if( applied > 0 )
    {
        stamps_select(ui, g_op_result.refs, g_op_result.ref_count);
        Editor_UiStatus(ui, "Duplicated %d loc%s by %d,%d; the copies are selected%s", applied,
            applied == 1 ? "" : "s", dx, dz, g_op_result.blocked > 0 ? " (some keys were taken: left out)" : "");
    }
    return applied;
}

int
edp_locs_rotate_by(
    struct EditorUi* ui,
    int quarters)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    int count;
    int applied;

    assert(ui);
    assert(quarters >= 1);
    assert(quarters <= 3);
    if( !stamps_need_selection(ui, "Rotate") )
        return 0;
    count = Editor_LocOpsRotate(
        &ui->editor->doc, ui->tools.locs, ui->tools.loc_count, quarters, commands, capacity, &g_op_result);
    if( count == 0 )
    {
        Editor_UiStatus(ui, "Rotate refused: the selected locs are no longer there");
        return 0;
    }
    applied = edp_apply(ui, commands, count, "rotate");
    if( applied > 0 )
        stamps_select(ui, g_op_result.refs, g_op_result.ref_count);
    return applied;
}

int
edp_locs_find_same(struct EditorUi* ui)
{
    struct Editor_LocRef* found = g_level_result.refs;
    struct Editor_LocRef primary;
    int total = 0;
    int count;

    assert(ui);
    if( !stamps_need_selection(ui, "Find same") )
        return 0;
    primary = ui->tools.locs[0];
    count = Editor_LocOpsFindSame(&ui->editor->doc, primary.loc_id, primary.level, found, EDITOR_LOC_SELECTION_MAX,
        &total);
    /* The primary stays first: the gizmo and the inspector stay on it. */
    for( int i = 1; i < count; i++ )
        if( found[i].map_x == primary.map_x && found[i].map_z == primary.map_z && found[i].x == primary.x
            && found[i].z == primary.z && found[i].shape == primary.shape )
        {
            struct Editor_LocRef const t = found[0];
            found[0] = found[i];
            found[i] = t;
        }
    if( count == 0 )
    {
        Editor_UiStatus(ui, "Find same: loc %d is not in the open squares on level %d", primary.loc_id, primary.level);
        return 0;
    }
    stamps_select(ui, found, count);
    if( total > count )
        Editor_UiStatus(ui, "Find same: selected %d of %d loc %d on level %d (a selection holds %d)", count, total,
            primary.loc_id, primary.level, EDITOR_LOC_SELECTION_MAX);
    else
        Editor_UiStatus(ui, "Find same: selected %d loc %d on level %d in the open squares", count, primary.loc_id,
            primary.level);
    return count;
}

/** The selection's placed footprints from the App's loc records (1x1 for a
 *  record not resident: asked for, it answers a frame or two later). */
static void
stamps_sizes(
    struct EditorUi* ui,
    int* sizes)
{
    for( int i = 0; i < ui->tools.loc_count; i++ )
    {
        const struct Editor_LocRef* ref = &ui->tools.locs[i];
        struct ToriRS_Location* cfg = ui->app_services.loc_record
                                          ? ui->app_services.loc_record(ui->app_services.user_data, ref->loc_id)
                                          : NULL;
        sizes[2 * i] = 1;
        sizes[2 * i + 1] = 1;
        if( !cfg )
        {
            if( ui->app_services.loc_request )
                ui->app_services.loc_request(ui->app_services.user_data, ref->loc_id);
            continue;
        }
        sizes[2 * i] = (ref->rotation & 1) ? cfg->size_z : cfg->size_x;
        sizes[2 * i + 1] = (ref->rotation & 1) ? cfg->size_x : cfg->size_z;
        if( sizes[2 * i] < 1 )
            sizes[2 * i] = 1;
        if( sizes[2 * i + 1] < 1 )
            sizes[2 * i + 1] = 1;
    }
}

/** The loaded text of TEXT document `key` (opened when it is not). NULL
 *  when the session cannot open it. */
static struct Editor_TextDoc*
stamps_text(
    struct EditorUi* ui,
    const char* key)
{
    struct Editor_DocHandle handle;

    if( !edp_ensure_doc(ui, EDITOR_DOC_TEXT, key) )
        return NULL;
    if( !Editor_DocFind(&ui->editor->doc, EDITOR_DOC_TEXT, key, &handle) || !handle.text || !handle.text->loaded )
        return NULL;
    return handle.text;
}

/** Splice TEXT document `key` from what it holds to `text`, inside the open
 *  stroke. Returns the edits applied, -1 when the authority refused. */
static int
stamps_write(
    struct EditorUi* ui,
    const char* key,
    const char* text,
    size_t length)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    struct Editor_TextDoc* doc = stamps_text(ui, key);
    int count;
    int applied;

    if( !doc )
        return -1;
    count = Editor_StampSplices(key, doc->text ? doc->text : "", doc->length, text, length, commands, capacity);
    if( count < 0 )
        return -1;
    if( count == 0 )
        return 0;
    applied = edp_apply(ui, commands, count, "save stamp");
    return applied == count ? applied : -1;
}

int
edp_stamp_save_selection(
    struct EditorUi* ui,
    const char* name,
    int with_tiles,
    int with_spawns,
    int overwrite)
{
    static int sizes[2 * EDITOR_LOC_SELECTION_MAX];
    struct Editor_Stamp stamp;
    struct Editor_TextDoc* existing;
    char key[EDITOR_DOC_KEY_MAX];
    char summary[160];
    char* text;
    size_t length;
    int was_open;
    int ok = 1;

    assert(ui);
    assert(name);
    if( !stamps_need_selection(ui, "Save as stamp") )
        return 0;
    if( !Editor_StampNameValid(name) )
    {
        Editor_UiStatus(ui, "Save as stamp refused: \"%.40s\" is not a stamp name (letters, digits, _ and -, under %d)",
            name, EDITOR_STAMP_NAME_MAX);
        return 0;
    }
    Editor_StampDocKey(key, name);
    existing = stamps_text(ui, key);
    if( !existing )
    {
        Editor_UiStatus(ui, "Save as stamp refused: the session cannot open editor/stamps/%s.stamp", name);
        return 0;
    }
    if( existing->length > 0 && !overwrite )
    {
        Editor_UiStatus(ui, "Save as stamp refused: a stamp named %s exists; choose another name or tick Overwrite",
            name);
        return 0;
    }
    Editor_StampInit(&stamp);
    snprintf(stamp.name, sizeof(stamp.name), "%s", name);
    stamps_sizes(ui, sizes);
    /* The spawn documents of the squares the selection stands in, so the
     * capture reads the generated and the quests' rows too. */
    for( int i = 0; with_spawns && i < ui->tools.loc_count; i++ )
        stamps_square_docs(ui, ui->tools.locs[i].map_x, ui->tools.locs[i].map_z);
    if( Editor_StampCaptureSelection(
            &ui->editor->doc, ui->tools.locs, ui->tools.loc_count, sizes, with_tiles, with_spawns, &stamp)
        == 0 )
    {
        Editor_UiStatus(ui, "Save as stamp refused: the selected locs are no longer there");
        Editor_StampFree(&stamp);
        return 0;
    }
    Editor_StampSummary(&stamp, summary, sizeof(summary));

    /* The stamp and its line in the index, one undo step. */
    was_open = ui->tools.stroke_open;
    if( !was_open )
    {
        Editor_StrokeBegin(ui->editor);
        Editor_HistoryName(ui->editor, "save stamp");
    }
    ui->tools.stroke_open = 1;
    length = Editor_StampEmitNew(&stamp, NULL, 0);
    text = ToriMem_Alloc(ToriMem_Editor(), length + 1);
    assert(text);
    Editor_StampEmitNew(&stamp, text, length + 1);
    if( stamps_write(ui, key, text, length) < 0 )
        ok = 0;
    ToriMem_Free(ToriMem_Editor(), text);
    if( ok )
    {
        struct Editor_TextDoc* index = stamps_text(ui, EDITOR_STAMPS_INDEX_KEY);
        if( !index )
            ok = 0;
        else
        {
            size_t const new_length =
                Editor_StampsIndexWith(index->text ? index->text : "", index->length, name, NULL, 0);
            char* listed = ToriMem_Alloc(ToriMem_Editor(), new_length + 1);
            assert(listed);
            Editor_StampsIndexWith(index->text ? index->text : "", index->length, name, listed, new_length + 1);
            if( stamps_write(ui, EDITOR_STAMPS_INDEX_KEY, listed, new_length) < 0 )
                ok = 0;
            ToriMem_Free(ToriMem_Editor(), listed);
        }
    }
    ui->tools.stroke_open = was_open;
    if( !was_open )
        Editor_StrokeEnd(ui->editor);
    Editor_StampFree(&stamp);
    if( !ok )
    {
        Editor_UiStatus(ui, "Save as stamp refused: the authority did not take the edit (a read-only session?)");
        return 0;
    }
    (void)Editor_SaveDoc(ui->editor, EDITOR_DOC_TEXT, key);
    (void)Editor_SaveDoc(ui->editor, EDITOR_DOC_TEXT, EDITOR_STAMPS_INDEX_KEY);
    Editor_UiStatus(ui, "Saved stamp %s to editor/stamps/%s.stamp", summary, name);
    return 1;
}

int
edp_stamps_list(
    struct EditorUi* ui,
    struct Editor_StampsFile* file)
{
    struct Editor_TextDoc* index;

    assert(ui);
    assert(file);
    Editor_StampsFree(file);
    if( !ui->editor )
        return 0;
    index = stamps_text(ui, EDITOR_STAMPS_INDEX_KEY);
    if( !index )
        return 0;
    (void)Editor_StampsParse(file, index->text, index->length);
    return 1;
}

int
edp_stamp_arm(
    struct EditorUi* ui,
    const char* name)
{
    struct Editor_StampsFile file;
    struct Editor_StampsParseResult parsed;
    struct Editor_TextDoc* doc;
    char key[EDITOR_DOC_KEY_MAX];
    char summary[160];

    assert(ui);
    assert(name);
    if( !ui->editor )
    {
        Editor_UiStatus(ui, "Place stamp refused: there is no editor session");
        return 0;
    }
    if( !Editor_StampNameValid(name) )
    {
        Editor_UiStatus(ui, "Place stamp refused: \"%.40s\" is not a stamp name", name);
        return 0;
    }
    Editor_StampDocKey(key, name);
    doc = stamps_text(ui, key);
    if( !doc || doc->length == 0 )
    {
        Editor_UiStatus(ui, "Place stamp refused: editor/stamps/%s.stamp is missing or empty", name);
        return 0;
    }
    Editor_StampsInit(&file);
    parsed = Editor_StampsParse(&file, doc->text, doc->length);
    if( !parsed.ok || !Editor_StampFromFile(&file, &g_armed) || Editor_StampCount(&g_armed, EDITOR_STAMP_LOC) == 0 )
    {
        if( !parsed.ok )
            Editor_UiStatus(ui, "Place stamp refused: editor/stamps/%s.stamp line %d does not parse", name, parsed.line);
        else
            Editor_UiStatus(ui, "Place stamp refused: editor/stamps/%s.stamp holds no locs", name);
        Editor_StampsFree(&file);
        g_armed_valid = 0;
        return 0;
    }
    Editor_StampsFree(&file);
    Editor_UiSetTool(ui, EDITOR_TOOL_LOC_PLACE);
    g_armed_valid = 1;
    g_armed_pick_kind = ui->tools.pick_kind;
    g_armed_pick_id = ui->tools.pick_id;
    Editor_StampSummary(&g_armed, summary, sizeof(summary));
    Editor_UiStatus(ui, "Placing stamp %s: click drops it, Q turns it, Esc drops the ghost", summary);
    return 1;
}

const struct Editor_Stamp*
edp_stamp_armed(struct EditorUi* ui)
{
    assert(ui);
    if( !g_armed_valid )
        return NULL;
    if( ui->tools.tool != EDITOR_TOOL_LOC_PLACE || ui->tools.pick_kind != g_armed_pick_kind
        || ui->tools.pick_id != g_armed_pick_id )
    {
        g_armed_valid = 0;
        return NULL;
    }
    return &g_armed;
}

void
edp_stamp_disarm(struct EditorUi* ui)
{
    assert(ui);
    (void)ui;
    g_armed_valid = 0;
}

void
edp_stamp_turn(
    struct EditorUi* ui,
    int quarters)
{
    assert(ui);
    if( !edp_stamp_armed(ui) )
        return;
    Editor_StampRotate(&g_armed, quarters);
    Editor_UiStatus(ui, "Stamp %s turned to %d degrees", g_armed.name, g_armed.turned * 90);
}

int
edp_stamp_drop(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level)
{
    int capacity;
    struct Editor_Cmd* commands = edp_scratch(&capacity);
    const struct Editor_Stamp* stamp;
    int count;
    int applied;

    assert(ui);
    stamp = edp_stamp_armed(ui);
    if( !stamp )
        return 0;
    if( !ui->editor )
        return 0;
    /* Every spawn piece's documents open before the drop reads them and the
     * stroke asks for them (a piece landing in a square that is not open is
     * left out by the drop, so only the open ones' are needed). */
    stamps_landing_docs(ui, stamp, world_x, world_z);
    count = Editor_StampDropCommands(&ui->editor->doc, stamp, world_x, world_z, level, commands, capacity,
        &g_drop_result);
    if( count == 0 )
    {
        if( g_drop_result.skipped > 0 )
            Editor_UiStatus(ui, "Place stamp refused: %d,%d L%d is not in an open square (open it first)",
                g_drop_result.skipped_x, g_drop_result.skipped_z, g_drop_result.skipped_level);
        else
            Editor_UiStatus(ui, "Place stamp: %s is already there exactly; nothing changed", stamp->name);
        return 0;
    }
    applied = edp_apply(ui, commands, count, "place stamp");
    if( applied > 0 )
    {
        /* The stamp's spawns are shown with the locs it placed. */
        edp_spawns_note_added(ui, commands, count);
        stamps_select(ui, g_drop_result.refs, g_drop_result.ref_count);
        if( g_drop_result.skipped > 0 )
            Editor_UiStatus(ui, "Placed stamp %s (%d locs, %d tiles, %d spawns); %d pieces left out: %d,%d L%d is not open",
                stamp->name, g_drop_result.locs, g_drop_result.tiles, g_drop_result.spawns, g_drop_result.skipped,
                g_drop_result.skipped_x, g_drop_result.skipped_z, g_drop_result.skipped_level);
        else
            Editor_UiStatus(ui, "Placed stamp %s (%d locs, %d tiles, %d spawns) at %d,%d L%d; Ctrl+Z undoes it",
                stamp->name, g_drop_result.locs, g_drop_result.tiles, g_drop_result.spawns, world_x, world_z, level);
    }
    return applied;
}

int
edp_stamp_clip_copy(
    struct EditorUi* ui,
    const struct Editor_TileRect* rect,
    int with_spawns)
{
    struct Editor_TileRect r;
    int locs;

    assert(ui);
    assert(rect);
    /* A copy replaces the whole clipboard: spawns of the last one never
     * ride along with this one's tiles. */
    Editor_StampClear(&g_clip_spawns);
    if( !ui->editor )
        return 0;
    locs = Editor_StampCaptureRect(&ui->editor->doc, rect, &g_clip);
    if( with_spawns && Editor_TileRectNormalise(rect, &r) && r.level >= 0 && r.level < EDITOR_SQUARE_LEVELS )
    {
        struct Editor_StampSpawnBox box;
        memset(&box, 0, sizeof(box));
        box.x0 = r.x0;
        box.z0 = r.z0;
        box.x1 = r.x1;
        box.z1 = r.z1;
        box.levels = 1u << r.level;
        box.origin_x = r.x0;
        box.origin_z = r.z0;
        box.origin_level = r.level;
        edp_stamp_spawn_docs(ui, r.x0, r.z0, r.x1, r.z1);
        (void)Editor_StampCaptureSpawns(&ui->editor->doc, &box, &g_clip_spawns);
    }
    return locs;
}

int
edp_stamp_clip_paste(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level,
    struct Editor_Cmd* out,
    int max)
{
    assert(ui);
    assert(out || max == 0);
    if( !ui->editor || g_clip.count == 0 || max <= 0 )
        return 0;
    return Editor_StampDropCommands(&ui->editor->doc, &g_clip, world_x, world_z, level, out, max, &g_drop_result);
}

int
edp_stamp_clip_paste_spawns(
    struct EditorUi* ui,
    int world_x,
    int world_z,
    int level,
    struct Editor_Cmd* out,
    int max)
{
    assert(ui);
    assert(out || max == 0);
    if( !ui->editor || g_clip_spawns.count == 0 || max <= 0 )
        return 0;
    stamps_landing_docs(ui, &g_clip_spawns, world_x, world_z);
    return Editor_StampDropCommands(&ui->editor->doc, &g_clip_spawns, world_x, world_z, level, out, max,
        &g_drop_result);
}

int
edp_stamp_clip_locs(void)
{
    return g_clip.count;
}

int
edp_stamp_clip_spawns(void)
{
    return g_clip_spawns.count;
}

int
edp_square_spawn_commands(
    struct EditorUi* ui,
    int from_map_x,
    int from_map_z,
    int to_map_x,
    int to_map_z,
    struct Editor_Cmd* out,
    int max,
    int* out_rows)
{
    int rows;

    assert(ui);
    assert(out || max == 0);
    assert(from_map_x >= 0);
    assert(from_map_z >= 0);
    assert(to_map_x >= 0);
    assert(to_map_z >= 0);
    if( out_rows )
        *out_rows = 0;
    if( !ui->editor || max <= 0 )
        return 0;
    stamps_square_docs(ui, from_map_x, from_map_z);
    rows = Editor_StampCaptureSquareSpawns(&ui->editor->doc, from_map_x, from_map_z, &g_square_spawns);
    if( out_rows )
        *out_rows = rows;
    if( rows == 0 )
        return 0;
    stamps_square_docs(ui, to_map_x, to_map_z);
    return Editor_StampDropCommands(&ui->editor->doc, &g_square_spawns, to_map_x * EDITOR_SQUARE_X,
        to_map_z * EDITOR_SQUARE_Z, 0, out, max, &g_drop_result);
}

void
edp_stamps_free(void)
{
    Editor_StampFree(&g_armed);
    Editor_StampFree(&g_clip);
    Editor_StampFree(&g_clip_spawns);
    Editor_StampFree(&g_square_spawns);
    g_armed_valid = 0;
}
