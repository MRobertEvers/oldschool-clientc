#include "edpanel_region.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <string.h>

/* ---- addressing ------------------------------------------------------------- */

/** The open square holding world tile (x, z), and the tile's local coords. */
static struct Editor_Square*
region_square_at(
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

/** The command for one tile, appended when it changes anything; *dropped
 *  counts a change there was no room for. */
static void
region_emit_tile(
    const struct Editor_Square* square,
    int x,
    int z,
    int level,
    const struct Editor_Tile* after,
    struct Editor_Cmd* out,
    int* count,
    int max,
    int* dropped)
{
    const struct Editor_Tile* before = &square->tiles[Editor_TileIndex(x, z, level)];
    struct Editor_Cmd* command;

    if( memcmp(before, after, sizeof(*before)) == 0 )
        return;
    if( *count >= max )
    {
        (*dropped)++;
        return;
    }
    command = &out[(*count)++];
    Editor_CmdInitSquare(command, EDITOR_CMD_TILE, square->map_x, square->map_z);
    command->u.tile.level = level;
    command->u.tile.x = x;
    command->u.tile.z = z;
    command->u.tile.before = *before;
    command->u.tile.after = *after;
}

int
Editor_RegionDocTile(
    void* user,
    int world_x,
    int world_z,
    int* out_shape,
    int* out_rotation)
{
    struct Editor_RegionDocTiles const* ctx = user;
    struct Editor_Square* square;
    const struct Editor_Tile* tile;
    int x;
    int z;

    assert(ctx);
    square = region_square_at(ctx->doc, world_x, world_z, &x, &z);
    if( !square )
        return EDITOR_REGION_HAS_NONE;
    tile = &square->tiles[Editor_TileIndex(x, z, ctx->level)];
    if( !tile->has_overlay )
        return EDITOR_REGION_HAS_NONE;
    /* A shape past the client's twelve is not one the planner can read back
     * or replace by halves: another overlay, as far as the plan goes. */
    if( tile->overlay_id != ctx->overlay_id || tile->shape >= EDITOR_REGION_SHAPES )
        return EDITOR_REGION_HAS_OTHER;
    *out_shape = tile->shape;
    *out_rotation = tile->rotation & 3;
    return EDITOR_REGION_HAS_SAME;
}

/* ---- overlays ----------------------------------------------------------------- */

/* The plan over `existing`, with its stats. */
static int
region_plan(
    const struct Editor_RegionParams* params,
    const struct Editor_RegionGrid* grid,
    Editor_RegionTileFn existing,
    void* user,
    struct Editor_RegionTile* out,
    int max,
    struct Editor_RegionStats* stats)
{
    int const planned = Editor_RegionPlan(params, grid, existing, user, out, max);
    if( stats )
    {
        memset(stats, 0, sizeof(*stats));
        stats->planned = planned;
        for( int i = 0; i < planned; i++ )
        {
            stats->edges += out[i].shape > 0;
            stats->cleared += out[i].shape < 0;
            stats->poor += out[i].shape >= 0 && out[i].misfit > EDITOR_REGION_POOR_FIT;
        }
    }
    return planned;
}

/* The plan's TILE commands: `overlay_id` at each planned shape, or off. */
static int
region_commands(
    struct Editor_Doc* doc,
    int level,
    const struct Editor_RegionParams* params,
    int overlay_id,
    const struct Editor_RegionGrid* grid,
    Editor_RegionTileFn existing,
    void* user,
    struct Editor_Cmd* out,
    int max,
    struct Editor_RegionStats* stats)
{
    struct Editor_RegionStats local;
    struct Editor_RegionTile* plan;
    size_t const room = (size_t)grid->w * (size_t)grid->h;
    int planned;
    int written = 0;

    plan = ToriMem_Alloc(ToriMem_Editor(), sizeof(*plan) * room);
    assert(plan);
    planned = region_plan(params, grid, existing, user, plan, (int)room, &local);
    for( int i = 0; i < planned; i++ )
    {
        struct Editor_Square* square;
        struct Editor_Tile after;
        int x;
        int z;

        square = region_square_at(doc, plan[i].world_x, plan[i].world_z, &x, &z);
        if( !square )
        {
            local.closed++;
            continue;
        }
        after = square->tiles[Editor_TileIndex(x, z, level)];
        if( plan[i].shape < 0 )
        {
            after.has_overlay = 0;
            after.overlay_id = 0;
            after.shape = 0;
            after.rotation = 0;
        }
        else
        {
            after.has_overlay = 1;
            after.overlay_id = (uint16_t)overlay_id;
            after.shape = (uint8_t)plan[i].shape;
            after.rotation = (uint8_t)plan[i].rotation;
        }
        region_emit_tile(square, x, z, level, &after, out, &written, max, &local.dropped);
    }
    ToriMem_Free(ToriMem_Editor(), plan);
    if( stats )
        *stats = local;
    return written;
}

int
Editor_RegionOverlayPlan(
    struct Editor_Doc* doc,
    int level,
    const struct Editor_RegionParams* params,
    int overlay_id,
    const struct Editor_RegionGrid* grid,
    struct Editor_RegionTile* out,
    int max,
    struct Editor_RegionStats* stats)
{
    struct Editor_RegionDocTiles ctx;

    assert(doc);
    assert(level >= 0);
    assert(level < EDITOR_SQUARE_LEVELS);
    assert(params);
    assert(overlay_id >= 0);
    assert(overlay_id <= 0xffff);
    assert(grid);
    assert(out);
    ctx.doc = doc;
    ctx.level = level;
    ctx.overlay_id = overlay_id;
    return region_plan(params, grid, Editor_RegionDocTile, &ctx, out, max, stats);
}

int
Editor_RegionOverlayCommands(
    struct Editor_Doc* doc,
    int level,
    const struct Editor_RegionParams* params,
    int overlay_id,
    const struct Editor_RegionGrid* grid,
    struct Editor_Cmd* out,
    int max,
    struct Editor_RegionStats* stats)
{
    struct Editor_RegionDocTiles ctx;

    assert(doc);
    assert(level >= 0);
    assert(level < EDITOR_SQUARE_LEVELS);
    assert(params);
    assert(overlay_id >= 0);
    assert(overlay_id <= 0xffff);
    assert(grid);
    assert(out);
    ctx.doc = doc;
    ctx.level = level;
    ctx.overlay_id = overlay_id;
    return region_commands(doc, level, params, overlay_id, grid, Editor_RegionDocTile, &ctx, out, max, stats);
}

/* ---- underlays ---------------------------------------------------------------- */

int
Editor_RegionUnderlayCommands(
    struct Editor_Doc* doc,
    int level,
    int underlay_id,
    const struct Editor_RegionGrid* grid,
    int feather,
    uint32_t seed,
    struct Editor_Cmd* out,
    int max,
    struct Editor_RegionStats* stats)
{
    struct Editor_RegionStats local;
    size_t room;
    int* xs;
    int* zs;
    int painted;
    int written = 0;

    assert(doc);
    assert(level >= 0);
    assert(level < EDITOR_SQUARE_LEVELS);
    assert(underlay_id >= 0);
    assert(underlay_id <= EDITOR_UNDERLAY_MAX);
    assert(grid);
    assert(out);
    room = (size_t)grid->w * (size_t)grid->h;
    xs = ToriMem_Alloc(ToriMem_Editor(), sizeof(*xs) * room);
    zs = ToriMem_Alloc(ToriMem_Editor(), sizeof(*zs) * room);
    assert(xs);
    assert(zs);
    memset(&local, 0, sizeof(local));
    painted = Editor_RegionDither(grid, feather, seed, xs, zs, (int)room);
    local.planned = painted;
    for( int i = 0; i < painted; i++ )
    {
        struct Editor_Square* square;
        struct Editor_Tile after;
        int x;
        int z;

        square = region_square_at(doc, xs[i], zs[i], &x, &z);
        if( !square )
        {
            local.closed++;
            continue;
        }
        after = square->tiles[Editor_TileIndex(x, z, level)];
        after.underlay_id = (uint16_t)underlay_id;
        region_emit_tile(square, x, z, level, &after, out, &written, max, &local.dropped);
    }
    ToriMem_Free(ToriMem_Editor(), xs);
    ToriMem_Free(ToriMem_Editor(), zs);
    if( stats )
        *stats = local;
    return written;
}

/* ---- Tidy --------------------------------------------------------------------- */

/* Editor_RegionTileFn for Tidy: the run's tiles are the overlay; a tile of
 * the same overlay outside the run is another run, held fixed as OTHER. */
struct RegionTidyTiles
{
    struct Editor_RegionDocTiles doc;
    const struct Editor_RegionTidy* tidy;
};

static int
region_tidy_tile(
    void* user,
    int world_x,
    int world_z,
    int* out_shape,
    int* out_rotation)
{
    struct RegionTidyTiles* ctx = user;
    struct Editor_RegionGrid const* grid = &ctx->tidy->grid;
    int const has = Editor_RegionDocTile(&ctx->doc, world_x, world_z, out_shape, out_rotation);
    int const tx = world_x - grid->x0;
    int const tz = world_z - grid->z0;

    if( has != EDITOR_REGION_HAS_SAME )
        return has;
    if( tx < 0 || tz < 0 || tx >= grid->w || tz >= grid->h || !ctx->tidy->member[(size_t)tz * grid->w + tx] )
        return EDITOR_REGION_HAS_OTHER;
    return EDITOR_REGION_HAS_SAME;
}

int
Editor_RegionTidyBegin(
    struct Editor_Doc* doc,
    int level,
    int world_x,
    int world_z,
    int smooth_halves,
    int hardness,
    struct Editor_RegionTidy* tidy)
{
    struct Editor_RegionDocTiles ctx;
    struct Editor_Square* square;
    const struct Editor_Tile* tile;
    int const room = EDITOR_REGION_SPAN_MAX * EDITOR_REGION_SPAN_MAX;
    int* xs;
    int* zs;
    int run;
    int clipped = 0;
    int margin;
    int min_x;
    int min_z;
    int max_x;
    int max_z;
    int x;
    int z;
    int ok;

    assert(doc);
    assert(level >= 0);
    assert(level < EDITOR_SQUARE_LEVELS);
    assert(smooth_halves >= 0);
    assert(tidy);
    memset(tidy, 0, sizeof(*tidy));
    tidy->level = level;
    square = region_square_at(doc, world_x, world_z, &x, &z);
    if( !square )
        return 0;
    tile = &square->tiles[Editor_TileIndex(x, z, level)];
    if( !tile->has_overlay || tile->shape >= EDITOR_REGION_SHAPES )
        return 0;
    ctx.doc = doc;
    ctx.level = level;
    ctx.overlay_id = tile->overlay_id;
    tidy->overlay_id = tile->overlay_id;

    xs = ToriMem_Alloc(ToriMem_Editor(), sizeof(*xs) * (size_t)room);
    zs = ToriMem_Alloc(ToriMem_Editor(), sizeof(*zs) * (size_t)room);
    assert(xs);
    assert(zs);
    run = Editor_RegionFlood(Editor_RegionDocTile, &ctx, world_x, world_z, xs, zs, room, &clipped);
    tidy->run = run;
    min_x = max_x = world_x;
    min_z = max_z = world_z;
    for( int i = 0; i < run; i++ )
    {
        min_x = xs[i] < min_x ? xs[i] : min_x;
        max_x = xs[i] > max_x ? xs[i] : max_x;
        min_z = zs[i] < min_z ? zs[i] : min_z;
        max_z = zs[i] > max_z ? zs[i] : max_z;
    }
    /* Room round the run for the blur to round a corner outward. */
    margin = (smooth_halves + 1) / 2 + 1;
    ok = !clipped
         && Editor_RegionGridInit(&tidy->grid, min_x - margin, min_z - margin, max_x - min_x + 1 + 2 * margin,
             max_z - min_z + 1 + 2 * margin);
    if( ok )
    {
        tidy->member = ToriMem_Calloc(ToriMem_Editor(), (size_t)tidy->grid.w * (size_t)tidy->grid.h, 1);
        assert(tidy->member);
        for( int i = 0; i < run; i++ )
        {
            size_t const at = (size_t)(zs[i] - tidy->grid.z0) * tidy->grid.w + (xs[i] - tidy->grid.x0);
            int shape;
            int rotation;
            (void)Editor_RegionDocTile(&ctx, xs[i], zs[i], &shape, &rotation);
            tidy->member[at] = 1;
            tidy->grid.tiles[at] = Editor_RegionShapeMask(shape, rotation);
        }
        Editor_RegionSmooth(&tidy->grid, smooth_halves * EDITOR_REGION_MASK / 2, hardness);
    }
    ToriMem_Free(ToriMem_Editor(), xs);
    ToriMem_Free(ToriMem_Editor(), zs);
    return ok ? 1 : -1;
}

void
Editor_RegionTidyFree(struct Editor_RegionTidy* tidy)
{
    if( !tidy )
        return;
    Editor_RegionGridFree(&tidy->grid);
    if( tidy->member )
        ToriMem_Free(ToriMem_Editor(), tidy->member);
    tidy->member = NULL;
}

int
Editor_RegionTidyPlan(
    struct Editor_Doc* doc,
    const struct Editor_RegionTidy* tidy,
    enum Editor_RegionSet set,
    int hardness,
    struct Editor_RegionTile* out,
    int max,
    struct Editor_RegionStats* stats)
{
    struct Editor_RegionParams params;
    struct RegionTidyTiles ctx;

    assert(doc);
    assert(tidy);
    assert(tidy->member);
    assert(out);
    params.set = set;
    params.op = EDITOR_REGION_REPLACE;
    params.hardness = hardness;
    params.misfit_cap = EDITOR_REGION_POOR_FIT;
    ctx.doc.doc = doc;
    ctx.doc.level = tidy->level;
    ctx.doc.overlay_id = tidy->overlay_id;
    ctx.tidy = tidy;
    return region_plan(&params, &tidy->grid, region_tidy_tile, &ctx, out, max, stats);
}

int
Editor_RegionTidyCommands(
    struct Editor_Doc* doc,
    const struct Editor_RegionTidy* tidy,
    enum Editor_RegionSet set,
    int hardness,
    struct Editor_Cmd* out,
    int max,
    struct Editor_RegionStats* stats)
{
    struct Editor_RegionParams params;
    struct RegionTidyTiles ctx;

    assert(doc);
    assert(tidy);
    assert(tidy->member);
    assert(out);
    params.set = set;
    params.op = EDITOR_REGION_REPLACE;
    params.hardness = hardness;
    params.misfit_cap = EDITOR_REGION_POOR_FIT;
    ctx.doc.doc = doc;
    ctx.doc.level = tidy->level;
    ctx.doc.overlay_id = tidy->overlay_id;
    ctx.tidy = tidy;
    return region_commands(doc, tidy->level, &params, tidy->overlay_id, &tidy->grid, region_tidy_tile, &ctx, out,
        max, stats);
}
