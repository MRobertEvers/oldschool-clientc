/*
 * Map-square export: floors, loc configs, and the `.jm2` text a LostCity build
 * consumes.
 *
 * Floors are *ported*, not matched. The plan called for a nearest-colour table
 * from OSRS floor ids onto rev-254 ones, but a `.flo` record is little more than
 * a colour, and LostCity reads floors by name out of `flo.pack` — so emitting
 * new entries carrying the source colours costs the same and loses nothing.
 * A textured floor also names its material, when one fits inside the
 * destination client's texture table; when it does not, the tile falls back to
 * the colour, which is what the reference does for an unloadable texture too.
 */

#include "lc_export.h"
#include "lc_manifest.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * A LostCity map tile writes its underlay as `id + 81` in one byte, so the
 * highest floor id a square can name is 174. Overlays get a whole byte, hence
 * 255. Exceeding either silently wraps into a different floor, so it is checked.
 */
#define LC_MAX_UNDERLAY_FLO 173
#define LC_MAX_OVERLAY_FLO 254

/* ---- floors ------------------------------------------------------------- */

static int
export_underlay(
    struct LC_Ctx* ctx,
    int src_id)
{
    int existing;
    if( lc_id_map_get(&ctx->underlay_map, src_id, &existing) )
        return existing;

    struct Tool_Bytes bytes;
    if( !lc_config_record(
            ctx,
            RSCACHE_TYPE_UNDERLAY,
            RSCACHE_DAT2_CONFIG_KIND_UNDERLAY,
            src_id,
            &bytes,
            NULL) )
    {
        lc_out_warn(ctx->out, "underlay %d: not in source cache", src_id);
        return -1;
    }

    struct RSCache_Dat2ConfigUnderlay under;
    memset(&under, 0, sizeof(under));
    RSCache_Dat2ConfigUnderlayDecodeInplaceFlags(
        &under,
        (char*)bytes.data,
        bytes.size,
        RSCache_Dat2ConfigFloFlags(&ctx->src->profile));
    tool_bytes_free(&bytes);

    char name[96];
    snprintf(name, sizeof(name), "flo_%s_u%d", ctx->out->prefix, src_id);
    int lc_id = lc_pack_alloc(&ctx->packs->packs[LC_PACK_FLO], name);
    if( lc_id < 0 )
        return -1;
    if( lc_id > LC_MAX_UNDERLAY_FLO )
        lc_out_warn(
            ctx->out,
            "underlay %s got flo id %d; a map tile can only name up to %d",
            name,
            lc_id,
            LC_MAX_UNDERLAY_FLO);

    lc_str_addf(&ctx->out->flo_cfg, "[%s]\n", name);
    lc_str_addf(&ctx->out->flo_cfg, "colour=0x%06X\n\n", under.rgb_color & 0xFFFFFF);

    lc_id_map_put(&ctx->underlay_map, src_id, lc_id);
    return lc_id;
}

static int
export_overlay(
    struct LC_Ctx* ctx,
    int src_id)
{
    int existing;
    if( lc_id_map_get(&ctx->overlay_map, src_id, &existing) )
        return existing;

    struct Tool_Bytes bytes;
    if( !lc_config_record(
            ctx,
            RSCACHE_TYPE_OVERLAY,
            RSCACHE_DAT2_CONFIG_KIND_OVERLAY,
            src_id,
            &bytes,
            NULL) )
    {
        lc_out_warn(ctx->out, "overlay %d: not in source cache", src_id);
        return -1;
    }

    struct RSCache_Dat2ConfigOverlay over;
    memset(&over, 0, sizeof(over));
    over.hide_underlay = true;
    RSCache_Dat2ConfigOverlayDecodeInplaceFlags(
        &over,
        (char*)bytes.data,
        bytes.size,
        RSCache_Dat2ConfigFloFlags(&ctx->src->profile));
    tool_bytes_free(&bytes);

    char name[96];
    snprintf(name, sizeof(name), "flo_%s_o%d", ctx->out->prefix, src_id);
    int lc_id = lc_pack_alloc(&ctx->packs->packs[LC_PACK_FLO], name);
    if( lc_id < 0 )
    {
        RSCache_Dat2ConfigOverlayFreeInplace(&over);
        return -1;
    }
    if( lc_id > LC_MAX_OVERLAY_FLO )
        lc_out_warn(
            ctx->out, "overlay %s got flo id %d; a map tile cannot name it", name, lc_id);

    /*
     * 0xFF00FF is the "I have no colour of my own" sentinel: the overlay is a
     * *shape* marker and the tile draws from its texture, or from the underlay
     * when there is no texture. The secondary colour is the blend edge, not a
     * fill — substituting it paints every such tile as one flat quad, which is
     * what turned the Inferno's lava into an orange grid.
     *
     * Rev 254 implements the same sentinel, in the same three-way order
     * (Client-TS ClientBuild.ts: texture, then `flo.rgb === Colour.MAGENTA`,
     * then colour), and its magenta branch sets the overlay colour to -2, which
     * getOCol turns into the 12345678 "skip this face" value. So the sentinel
     * has to be written through *verbatim*. Omitting `colour=` instead leaves
     * FloType.rgb at 0, which is not the sentinel — it is the colour black, and
     * the tile takes the third branch and paints a black quad over the hole the
     * reference leaves. That is why the Inferno's lava moat exported black.
     */
    int rgb = over.rgb_color & 0xFFFFFF;

    /* The texture wins over the colour in both clients' three-way resolve, so it
     * is emitted alongside rather than instead: a material that fails to port
     * leaves the tile on its colour rather than on nothing. */
    int texture_id = -1;
    if( over.texture >= 0 && ctx->max_textures > 0 )
        texture_id = lc_export_texture(ctx, over.texture, NULL);

    lc_str_addf(&ctx->out->flo_cfg, "[%s]\n", name);
    lc_str_addf(&ctx->out->flo_cfg, "overlay=yes\n");
    lc_str_addf(&ctx->out->flo_cfg, "colour=0x%06X\n", rgb);
    if( texture_id >= 0 )
        lc_str_addf(
            &ctx->out->flo_cfg,
            "texture=%s\n",
            ctx->packs->packs[LC_PACK_TEXTURE].names[texture_id]);
    if( !over.hide_underlay )
        lc_str_addf(&ctx->out->flo_cfg, "occlude=no\n");
    lc_str_addf(&ctx->out->flo_cfg, "\n");
    if( over.texture >= 0 && texture_id < 0 )
        lc_out_warn(
            ctx->out,
            "overlay %d: texture %d dropped, exported as its flat colour",
            src_id,
            over.texture);

    RSCache_Dat2ConfigOverlayFreeInplace(&over);
    lc_id_map_put(&ctx->overlay_map, src_id, lc_id);
    return lc_id;
}

/* ---- loc configs -------------------------------------------------------- */

static int
clamp_i(int v, int lo, int hi)
{
    if( v < lo )
        return lo;
    if( v > hi )
        return hi;
    return v;
}

int
lc_export_loc(
    struct LC_Ctx* ctx,
    int src_loc_id)
{
    assert(ctx);
    int existing;
    if( lc_id_map_get(&ctx->loc_map, src_loc_id, &existing) )
        return existing;

    struct Tool_Bytes bytes;
    int revision = 0;
    if( !lc_config_record(
            ctx,
            RSCACHE_TYPE_LOC,
            RSCACHE_DAT2_CONFIG_KIND_LOCS,
            src_loc_id,
            &bytes,
            &revision) )
    {
        lc_out_warn(ctx->out, "loc %d: not in source cache", src_loc_id);
        return -1;
    }

    struct RSCache local = ctx->src->profile;
    RSCache_ProfileSetGroupRevision(&local, RSCACHE_TYPE_LOC, revision);
    struct RSCache_Dat2ConfigLoc* loc =
        RSCache_Dat2ConfigLocNewDecodeProfile(&local, (char*)bytes.data, bytes.size);
    int consumed_ok = loc && loc->_consumed == bytes.size;
    int record_size = bytes.size;
    tool_bytes_free(&bytes);
    if( !loc )
    {
        lc_out_warn(ctx->out, "loc %d: decode failed", src_loc_id);
        return -1;
    }
    if( !consumed_ok )
    {
        lc_out_warn(
            ctx->out,
            "loc %d: record does not decode exactly (%d of %d bytes); skipped",
            src_loc_id,
            loc->_consumed,
            record_size);
        RSCache_Dat2ConfigLocFree(loc);
        return -1;
    }

    char name[96];
    snprintf(name, sizeof(name), "%s_loc_%d", ctx->out->prefix, src_loc_id);
    int lc_id = lc_pack_alloc(&ctx->packs->packs[LC_PACK_LOC], name);
    if( lc_id < 0 )
    {
        RSCache_Dat2ConfigLocFree(loc);
        return -1;
    }
    lc_id_map_put(&ctx->loc_map, src_loc_id, lc_id);

    struct LC_Str* cfg = &ctx->out->loc_cfg;
    lc_str_addf(cfg, "[%s]\n", name);
    if( loc->name && loc->name[0] )
        lc_str_addf(cfg, "name=%s\n", loc->name);

    /*
     * LostCity recovers a loc model's shape from the model *file name*, not from
     * the config, so each (shape, model) pair becomes its own `modelN` entry
     * whose file carries that shape's suffix. A source group listing several
     * models under one shape therefore expands into several entries.
     */
    /* LostCity's loc model keys are `model`, then `model2`..`model5` — five
     * slots, and the first is unnumbered. */
    int model_slot = 0;
    int truncated = 0;
    for( int g = 0; g < loc->shapes_and_model_count; g++ )
    {
        int shape = loc->shapes ? loc->shapes[g] : 10;
        for( int m = 0; m < loc->lengths[g]; m++ )
        {
            if( model_slot >= 5 )
            {
                truncated++;
                continue;
            }
            char base[160];
            if( lc_export_model(ctx, loc->models[g][m], shape, base, sizeof(base)) < 0 )
                continue;
            if( model_slot == 0 )
                lc_str_addf(cfg, "model=%s\n", base);
            else
                lc_str_addf(cfg, "model%d=%s\n", model_slot + 1, base);
            model_slot++;
        }
    }
    int emitted_models = model_slot;
    if( truncated > 0 )
        lc_out_warn(
            ctx->out,
            "loc %d (%s): %d model entries past the 5 LostCity allows were dropped",
            src_loc_id,
            name,
            truncated);
    if( loc->shapes_and_model_count > 0 && emitted_models == 0 )
    {
        /* The LostCity packer throws on a loc that names models and resolves
         * none, so a loc whose geometry all failed must not carry a model line
         * at all — it becomes an invisible marker instead of a build error. */
        lc_out_warn(
            ctx->out, "loc %d (%s): no model transcoded; exported invisible", src_loc_id, name);
    }

    if( loc->size_x != 1 )
        lc_str_addf(cfg, "width=%d\n", clamp_i(loc->size_x, 1, 255));
    if( loc->size_z != 1 )
        lc_str_addf(cfg, "length=%d\n", clamp_i(loc->size_z, 1, 255));
    if( loc->blocks_walk == 0 )
        lc_str_addf(cfg, "blockwalk=no\n");
    if( loc->blocks_projectiles == 0 )
        lc_str_addf(cfg, "blockrange=no\n");
    if( loc->is_interactive == 0 || loc->is_interactive == 1 )
        lc_str_addf(cfg, "active=%s\n", loc->is_interactive ? "yes" : "no");
    /* Opcode 21 is the hill-skew flag and sets contoured_ground to 0; opcode 81
     * is a graded contour LostCity has no key for. */
    if( loc->contoured_ground == 0 )
        lc_str_addf(cfg, "hillskew=yes\n");
    if( loc->sharelight )
        lc_str_addf(cfg, "sharelight=yes\n");
    if( loc->occlude )
        lc_str_addf(cfg, "occlude=yes\n");
    if( loc->seq_id > 0 )
    {
        int seq = lc_export_seq(ctx, loc->seq_id, NULL);
        if( seq >= 0 )
            lc_str_addf(cfg, "anim=%s\n", ctx->packs->packs[LC_PACK_SEQ].names[seq]);
    }
    if( loc->wall_width != 16 )
        lc_str_addf(cfg, "wallwidth=%d\n", clamp_i(loc->wall_width, 0, 255));
    if( loc->ambient != 0 )
        lc_str_addf(cfg, "ambient=%d\n", clamp_i(loc->ambient, -128, 127));
    if( loc->contrast != 0 )
    {
        /* The cache stores contrast pre-multiplied by 25 and adds it straight to
         * the light term; LostCity stores the raw byte and multiplies by 5. */
        lc_str_addf(cfg, "contrast=%d\n", clamp_i(loc->contrast / 5, -128, 127));
    }
    for( int i = 0; i < 5; i++ )
    {
        if( loc->actions[i] && loc->actions[i][0] )
            lc_str_addf(cfg, "op%d=%s\n", i + 1, loc->actions[i]);
    }
    if( loc->mirrored )
        lc_str_addf(cfg, "mirror=yes\n");
    if( !loc->shadowed )
        lc_str_addf(cfg, "shadow=no\n");
    if( loc->resize_x != 128 )
        lc_str_addf(cfg, "resizex=%d\n", clamp_i(loc->resize_x, 0, 65535));
    if( loc->resize_height != 128 )
        lc_str_addf(cfg, "resizey=%d\n", clamp_i(loc->resize_height, 0, 65535));
    if( loc->resize_z != 128 )
        lc_str_addf(cfg, "resizez=%d\n", clamp_i(loc->resize_z, 0, 65535));
    if( loc->offset_x != 0 )
        lc_str_addf(cfg, "offsetx=%d\n", loc->offset_x);
    if( loc->offset_y != 0 )
        lc_str_addf(cfg, "offsety=%d\n", loc->offset_y);
    if( loc->offset_z != 0 )
        lc_str_addf(cfg, "offsetz=%d\n", loc->offset_z);
    if( loc->obstructs_ground )
        lc_str_addf(cfg, "forcedecor=yes\n");
    if( loc->break_routefinding )
        lc_str_addf(cfg, "breakroutefinding=yes\n");
    if( loc->support_items == 0 || loc->support_items == 1 )
        lc_str_addf(cfg, "raiseobject=%s\n", loc->support_items ? "yes" : "no");

    for( int i = 0; i < loc->recolor_count && i < 6; i++ )
    {
        lc_str_addf(
            cfg, "recol%ds=%d\n", i + 1, lc_hsl16_to_rgb15(loc->recolors_from[i] & 0xFFFF));
        lc_str_addf(
            cfg, "recol%dd=%d\n", i + 1, lc_hsl16_to_rgb15(loc->recolors_to[i] & 0xFFFF));
    }
    lc_str_addf(cfg, "\n");

    if( loc->transform_count > 0 )
        lc_out_warn(
            ctx->out,
            "loc %d (%s): %d varbit transforms dropped",
            src_loc_id,
            name,
            loc->transform_count);

    RSCache_Dat2ConfigLocFree(loc);
    return lc_id;
}

/* ---- map square --------------------------------------------------------- */

int
lc_export_map(
    struct LC_Ctx* ctx,
    int map_x,
    int map_z,
    const struct LC_MapLocAdd* extra,
    int extra_count,
    const struct LC_MapFill* fills,
    int fill_count)
{
    assert(ctx);

    struct RSCache_MapTerrain* terrain =
        RSCache_MapTerrainNewFromCache(ctx->src->disk, map_x, map_z);
    if( !terrain )
    {
        lc_out_warn(ctx->out, "map %d_%d: terrain not in source cache", map_x, map_z);
        return 0;
    }

    struct RSCache_MapLocs* locs = RSCache_MapLocsNewFromCache(ctx->src->disk, map_x, map_z);
    if( !locs )
        lc_out_warn(
            ctx->out,
            "map %d_%d: loc archive missing or undecryptable; terrain only",
            map_x,
            map_z);

    struct LC_Str body;
    memset(&body, 0, sizeof(body));
    lc_str_addf(&body, "==== MAP ====\n");

    for( int level = 0; level < RSCACHE_MAP_TERRAIN_LEVELS; level++ )
    {
        for( int x = 0; x < RSCACHE_MAP_TERRAIN_X; x++ )
        {
            for( int z = 0; z < RSCACHE_MAP_TERRAIN_Z; z++ )
            {
                const struct RSCache_MapFloor* tile =
                    &terrain->tiles_xyz[RSCACHE_MAP_TILE_COORD(x, z, level)];

                char tokens[128];
                int n = 0;
                tokens[0] = '\0';

                if( tile->height_authored && tile->authored_height != 0 )
                    n += snprintf(tokens + n, sizeof(tokens) - (size_t)n, "h%d ", tile->authored_height);
                if( tile->attr_opcode != 0 )
                {
                    /* A tile stores its floor as `config id + 1`, zero meaning
                     * "none" — both the reference client and LostCity's do the
                     * -1 on lookup. Exporting the stored value verbatim loads
                     * the neighbouring record and paints the whole square in
                     * somebody else's colours. */
                    int flo = export_overlay(ctx, tile->overlay_id - 1);
                    if( flo >= 0 )
                        n += snprintf(
                            tokens + n,
                            sizeof(tokens) - (size_t)n,
                            "o%d;%d;%d ",
                            flo + 1,
                            tile->shape,
                            tile->rotation);
                }
                if( tile->settings != 0 )
                    n += snprintf(
                        tokens + n, sizeof(tokens) - (size_t)n, "f%d ", tile->settings);
                int underlay_id = tile->underlay_id;
                if( underlay_id == 0 && tile->attr_opcode != 0 &&
                    ctx->overlay_backing_underlay > 0 )
                    underlay_id = ctx->overlay_backing_underlay;
                /* Region fills: floor for void tiles inside an arena, so
                 * translucent model faces standing there have something behind
                 * them. Overlay tiles are already handled above. */
                if( underlay_id == 0 && tile->attr_opcode == 0 )
                    for( int fi = 0; fi < fill_count; fi++ )
                        if( fills[fi].map_x == map_x && fills[fi].map_z == map_z &&
                            fills[fi].level == level && x >= fills[fi].x1 &&
                            x <= fills[fi].x2 && z >= fills[fi].z1 && z <= fills[fi].z2 )
                        {
                            underlay_id = fills[fi].underlay;
                            break;
                        }
                if( underlay_id != 0 )
                {
                    int flo = export_underlay(ctx, underlay_id - 1);
                    if( flo >= 0 )
                        n += snprintf(
                            tokens + n, sizeof(tokens) - (size_t)n, "u%d ", flo + 1);
                }

                if( getenv("LC_MAP_DEBUG") && level == 0 && x >= 20 && x <= 40 && z >= 44 &&
                    z <= 60 )
                    fprintf(
                        stderr,
                        "MAPDBG %d,%d u=%d o=%d shape=%d rot=%d settings=%d h=%d wrote='%s'\n",
                        x,
                        z,
                        tile->underlay_id,
                        tile->overlay_id,
                        tile->shape,
                        tile->rotation,
                        tile->settings,
                        tile->authored_height,
                        tokens);
                if( n == 0 )
                    continue;
                tokens[n - 1] = '\0'; /* drop the trailing space */
                lc_str_addf(&body, "%d %d %d: %s\n", level, x, z, tokens);
            }
        }
    }

    int loc_count = 0;
    int loc_dropped = 0;
    int extra_here = 0;
    for( int i = 0; i < extra_count; i++ )
        if( extra[i].map_x == map_x && extra[i].map_z == map_z )
            extra_here++;
    if( (locs && locs->locs_count > 0) || extra_here > 0 )
    {
        lc_str_addf(&body, "==== LOC ====\n");
        for( int i = 0; locs && i < locs->locs_count; i++ )
        {
            const struct RSCache_MapLoc* loc = &locs->locs[i];
            int lc_id = lc_export_loc(ctx, loc->loc_id);
            if( lc_id < 0 )
            {
                loc_dropped++;
                continue;
            }
            lc_str_addf(
                &body,
                "%d %d %d: %d %d %d\n",
                loc->chunk_pos_level,
                loc->chunk_pos_x,
                loc->chunk_pos_z,
                lc_id,
                loc->shape_select,
                loc->orientation);
            loc_count++;
        }

        /* Placements the square does not carry. Same treatment as the source
         * ones: the loc is exported first, and a failure drops the placement
         * rather than writing an id that names nothing. */
        for( int i = 0; i < extra_count; i++ )
        {
            if( extra[i].map_x != map_x || extra[i].map_z != map_z )
                continue;
            int lc_id = lc_export_loc(ctx, extra[i].src_loc);
            if( lc_id < 0 )
            {
                loc_dropped++;
                continue;
            }
            lc_str_addf(
                &body,
                "%d %d %d: %d %d %d\n",
                extra[i].level,
                extra[i].x,
                extra[i].z,
                lc_id,
                extra[i].shape,
                extra[i].angle);
            loc_count++;
        }
    }

    char rel[256];
    snprintf(rel, sizeof(rel), "maps/m%d_%d.jm2", map_x, map_z);
    int ok = lc_out_write_file(ctx->out, rel, body.data, body.len);
    lc_str_free(&body);

    char pack_name[64];
    snprintf(pack_name, sizeof(pack_name), "m%d_%d", map_x, map_z);
    if( lc_pack_alloc(&ctx->packs->packs[LC_PACK_MAP], pack_name) < 0 )
        ok = 0;
    snprintf(pack_name, sizeof(pack_name), "l%d_%d", map_x, map_z);
    if( lc_pack_alloc(&ctx->packs->packs[LC_PACK_MAP], pack_name) < 0 )
        ok = 0;

    printf(
        "map %d_%d: %d locs placed, %d dropped\n", map_x, map_z, loc_count, loc_dropped);

    if( locs )
        RSCache_MapLocsFree(locs);
    RSCache_MapTerrainFree(terrain);
    return ok;
}
