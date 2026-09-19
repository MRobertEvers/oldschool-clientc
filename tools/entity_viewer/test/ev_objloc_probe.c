/*
 * Do obj and loc models come out of a cache, and do they come out RIGHT?
 *
 * Every failure this covers is silent — the viewer draws something either way,
 * and something is what a wrong answer looks like:
 *
 *   the shape a loc does not have. A loc carries one mesh per shape, and asking
 *   for the wrong one must produce nothing rather than another shape's mesh. A
 *   wall drawn in answer to "show me the corner" reads as a correct answer.
 *
 *   the loc transforms. `mirrored`, `resize` and `offset` are opcodes on the
 *   record that the scene builder applies and a naive build skips; a mirrored
 *   loc built without them is handed the wrong way round and looks fine alone.
 *   Checked as geometry — the mirrored build's vertices must be the unmirrored
 *   one's x negated — rather than by trusting the call to have happened.
 *
 *   the obj variants. The item model, the wear models and the chatheads are
 *   different ids in one record, and serving one where another was asked for is
 *   a picture of the right item and the wrong mesh. Checked by asserting the
 *   builds differ from each other where the record says the ids differ.
 *
 *   the index. Obj and loc rows are what the pickers list, so an index that
 *   silently returns none makes both modes look like a cache with no content.
 *
 * Every check is against a cache, so it runs where one is:
 *
 *   ev_objloc_probe <cache_dir> <rev>
 */
#include "ev_build.h"
#include "ev_caches.h"
#include "ev_config.h"

#include "asset_access.h"
#include "tool_profile.h"

/* The client's own two-step model conversion — the mirror check needs the
 * untransformed model the loc build starts from, and hand-rolling that
 * conversion is what ev_build.c's header comment warns about. */
#include "engine/toridraw_model_from_torirs.h"
#include "engine/torirs_model_from_rscache.h"

#include "toridraw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;

static void
check(const char* what, int ok)
{
    if( !ok )
        fails++;
    printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
}

/*
 * How many records any pass over a whole table looks at.
 *
 * Not a shortcut: `ev_loc_load` decodes the archive the record lives in, and on
 * an OldSchool cache every loc lives in ONE archive of sixty thousand files. A
 * pass calling it per id re-decodes that archive per id — the same trap the
 * npc walk hit at 537 seconds against 72 ms — so a full sweep here would be
 * hours, not thoroughness. Everything sampled is REPORTED, because a silent cap
 * reads as "covered everything".
 */
#define PROBE_SAMPLE 96

/** Spread `PROBE_SAMPLE` picks across `count` records rather than taking the
 *  first few: the low ids of every table are one content batch from one year. */
static int
sample_stride(int count)
{
    int stride = count / PROBE_SAMPLE;
    return stride > 1 ? stride : 1;
}

/**
 * A loc that carries more than one shape, and one that is mirrored.
 *
 * Found rather than hardcoded: loc ids mean different things in every cache, so
 * a probe pinned to "loc 1560 is a gate" only checks cache.osrs239. Both are
 * written as -1 when the sample holds none, and the checks that need them are
 * skipped rather than failed.
 */
static void
find_locs(struct Tool_Dat2Cache* c, const struct EV_Index* index, int* out_multi, int* out_mirror)
{
    *out_multi = -1;
    *out_mirror = -1;

    for( int i = 0; i < index->loc_count; i += sample_stride(index->loc_count) )
    {
        if( *out_multi >= 0 && *out_mirror >= 0 )
            return;

        struct RSCache_Dat2ConfigLoc* loc = ev_loc_load(c, index->locs[i].id);
        if( !loc )
            continue;

        if( *out_multi < 0 && loc->shapes && loc->shapes_and_model_count > 1 && loc->models )
            *out_multi = index->locs[i].id;
        if( *out_mirror < 0 && loc->mirrored && loc->models && loc->shapes_and_model_count > 0 )
            *out_mirror = index->locs[i].id;

        RSCache_Dat2ConfigLocFree(loc);
    }
}

/** The shape numbers a loc carries, and one it does not. */
static int
shape_absent_from(struct Tool_Dat2Cache* c, int loc_id)
{
    struct RSCache_Dat2ConfigLoc* loc = ev_loc_load(c, loc_id);
    int absent = -1;
    if( !loc )
        return -1;

    /* 0..22 is the whole shape space (RSCache_Dat2LocShape). A record carrying
     * every one of them would leave nothing to ask for; none do. */
    for( int s = 0; s <= 22 && absent < 0; s++ )
    {
        int carried = 0;
        for( int g = 0; g < loc->shapes_and_model_count && loc->shapes; g++ )
            if( loc->shapes[g] == s )
                carried = 1;
        if( !carried )
            absent = s;
    }
    RSCache_Dat2ConfigLocFree(loc);
    return absent;
}

/**
 * Is `a` `b` mirrored?
 *
 * ToriDraw_ModelMirror negates **z** and swaps each face's first and third
 * corner — the winding has to flip or every face faces inward. Both halves are
 * checked: a mirror that moved the vertices and left the winding would light
 * and cull the model inside out, which looks like a broken normal pass rather
 * than like a missing swap.
 *
 * The axis is worth stating because guessing it wrong is easy and silent: this
 * check was written against x first, and passed nothing.
 */
static int
is_mirror_of(const struct ToriDraw_Model* a, const struct ToriDraw_Model* b)
{
    if( a->vertex_count != b->vertex_count || a->face_count != b->face_count )
        return 0;
    for( int v = 0; v < a->vertex_count; v++ )
        if( a->vertices_z[v] != -b->vertices_z[v] || a->vertices_x[v] != b->vertices_x[v] ||
            a->vertices_y[v] != b->vertices_y[v] )
            return 0;
    for( int f = 0; f < a->face_count; f++ )
        if( a->face_indices_a[f] != b->face_indices_c[f] ||
            a->face_indices_c[f] != b->face_indices_a[f] ||
            a->face_indices_b[f] != b->face_indices_b[f] )
            return 0;
    return 1;
}

/** An obj whose item model and male wear model are different ids. */
static int
find_worn_obj(struct Tool_Dat2Cache* c, const struct EV_Index* index)
{
    for( int i = 0; i < index->obj_count; i += sample_stride(index->obj_count) )
    {
        struct RSCache_Dat2ConfigObj* obj = ev_obj_load(c, index->objs[i].id);
        if( !obj )
            continue;
        int id = index->objs[i].id;
        int usable = obj->inventory_model_id >= 0 && obj->male_model_0 >= 0 &&
                     obj->male_model_0 != obj->inventory_model_id;
        RSCache_Dat2ConfigObjFree(obj);
        if( usable )
            return id;
    }
    return -1;
}

int
main(int argc, char** argv)
{
    if( argc < 3 )
    {
        fprintf(stderr, "usage: ev_objloc_probe <cache_dir> <rev>\n");
        return 2;
    }
    const char* dir = argv[1];
    const char* rev = argv[2];

    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    if( !tool_resolve_profile(rev, NULL, NULL, NULL, NULL, &profile) )
    {
        fprintf(stderr, "unknown revision profile %s\n", rev);
        return 2;
    }
    if( !tool_dat2_open(dir, &profile, &cache) )
    {
        fprintf(stderr, "cannot open %s\n", dir);
        return 2;
    }
    ToriDraw_Init();

    struct EV_Index index;
    if( !ev_index_build(&cache, &profile, dir, rev, &index) )
    {
        fprintf(stderr, "cannot index %s\n", dir);
        return 2;
    }

    printf("%s (%s): %d npcs, %d objs, %d locs\n",
           dir, rev, index.npc_count, index.obj_count, index.loc_count);

    printf("\nthe index\n");
    check("the obj list is not empty", index.obj_count > 0);
    check("the loc list is not empty", index.loc_count > 0);
    /* Names are the whole reason obj and loc rows carry more than an id: a
     * picker over forty thousand bare numbers is not a picker. An index that
     * decoded every record and found no name in any of them is a decoder
     * reading at the wrong widths, and it looks like a cache of unnamed
     * records. */
    int named_objs = 0;
    for( int i = 0; i < index.obj_count; i++ )
        if( index.objs[i].name )
            named_objs++;
    int named_locs = 0;
    for( int i = 0; i < index.loc_count; i++ )
        if( index.locs[i].name )
            named_locs++;
    printf("  %d of %d objs named, %d of %d locs named\n",
           named_objs, index.obj_count, named_locs, index.loc_count);
    check("most objs have a name", named_objs * 2 > index.obj_count);
    /*
     * A quarter, not most: over half the loc table is genuinely nameless — the
     * invisible blockers, the shadows under farming patches, the multiloc
     * parents a varbit transforms away from. On cache.osrs239 it is 30,033
     * named of 62,194. What this catches is the decoder reading at the wrong
     * field widths, which leaves nearly nothing named at all.
     */
    check("a substantial share of locs have a name", named_locs * 4 > index.loc_count);

    int multi_loc = -1;
    int mirror_loc = -1;
    find_locs(&cache, &index, &multi_loc, &mirror_loc);

    printf("\nloc shapes (loc %d)\n", multi_loc);
    if( multi_loc < 0 )
        printf("  no multi-shape loc in this cache — skipped\n");
    else
    {
        struct RSCache_Dat2ConfigLoc* loc = ev_loc_load(&cache, multi_loc);
        int first_shape = loc->shapes[0];
        int other_shape = -1;
        for( int g = 1; g < loc->shapes_and_model_count; g++ )
            if( loc->shapes[g] != first_shape )
                other_shape = loc->shapes[g];
        RSCache_Dat2ConfigLocFree(loc);

        struct ToriDraw_Model* a = ev_build_loc_model(&cache, multi_loc, first_shape);
        check("the first shape builds", a != NULL);

        /* -1 means "whichever the record lists first", so it must agree with
         * asking for that shape by number. A default that quietly picked
         * something else would make every direct URL show a different model
         * from the picker. */
        struct ToriDraw_Model* d = ev_build_loc_model(&cache, multi_loc, -1);
        check("the default shape is the first one",
              a && d && a->face_count == d->face_count && a->vertex_count == d->vertex_count);

        if( other_shape >= 0 )
        {
            struct ToriDraw_Model* b = ev_build_loc_model(&cache, multi_loc, other_shape);
            check("a second shape builds", b != NULL);
            /* They may legitimately share a mesh, so this is reported rather
             * than checked — what matters is that asking got a different
             * answer, not that the answers differ. */
            if( a && b )
                printf("  shape %d: %d faces, shape %d: %d faces\n",
                       first_shape, a->face_count, other_shape, b->face_count);
            ToriDraw_ModelFree(b);
        }

        int absent = shape_absent_from(&cache, multi_loc);
        struct ToriDraw_Model* none = absent >= 0
                                          ? ev_build_loc_model(&cache, multi_loc, absent)
                                          : NULL;
        check("a shape the record does not carry builds NOTHING", absent < 0 || none == NULL);
        ToriDraw_ModelFree(none);
        ToriDraw_ModelFree(a);
        ToriDraw_ModelFree(d);
    }

    printf("\nloc transforms (loc %d)\n", mirror_loc);
    if( mirror_loc < 0 )
        printf("  no mirrored loc in this cache — skipped\n");
    else
    {
        /*
         * The mirror, proved as geometry.
         *
         * A build that skipped ToriDraw_ModelMirror produces a model that is
         * perfectly plausible and handed the wrong way, which no face count and
         * no render can catch. So this rebuilds the same models WITHOUT the
         * record's transforms — straight from the model ids — and asserts the
         * loc build's vertices are those with x negated.
         *
         * Only meaningful for a record that mirrors and does nothing else: a
         * resize or an offset also moves the vertices, and the comparison would
         * then fail for a reason that is not the mirror. Those records are
         * skipped rather than approximated.
         */
        struct RSCache_Dat2ConfigLoc* loc = ev_loc_load(&cache, mirror_loc);
        int plain = loc->resize_x == 128 && loc->resize_height == 128 && loc->resize_z == 128 &&
                    loc->offset_x == 0 && loc->offset_y == 0 && loc->offset_z == 0 &&
                    loc->recolor_count == 0 && loc->retexture_count == 0 && loc->lengths &&
                    loc->lengths[0] == 1 && loc->models;
        int model_id = plain ? loc->models[0][0] : -1;
        int shape = loc->shapes ? loc->shapes[0] : -1;
        RSCache_Dat2ConfigLocFree(loc);

        if( !plain )
            printf("  loc %d also resizes or recolours — mirror check skipped\n", mirror_loc);
        else
        {
            struct RSCache_Model* rs = tool_dat2_model_load(&cache, model_id);
            struct ToriRS_Model* mid = rs ? ToriRS_ModelFromRSCache(rs) : NULL;
            RSCache_ModelFree(rs);
            struct ToriDraw_Model* raw = mid ? ToriDraw_ModelFromToriRS(mid) : NULL;
            ToriRS_ModelFree(mid);

            struct ToriDraw_Model* built = ev_build_loc_model(&cache, mirror_loc, shape);
            check("a mirrored loc builds", built != NULL);
            check("it is the source model mirrored, winding and all",
                  raw && built && is_mirror_of(built, raw));
            ToriDraw_ModelFree(raw);
            ToriDraw_ModelFree(built);
        }
    }

    int worn = find_worn_obj(&cache, &index);
    printf("\nobj variants (obj %d)\n", worn);
    if( worn < 0 )
        printf("  no obj with a distinct wear model in this cache — skipped\n");
    else
    {
        struct ToriDraw_Model* item = ev_build_obj_model(&cache, worn, EV_OBJ_MODEL_ITEM);
        struct ToriDraw_Model* male = ev_build_obj_model(&cache, worn, EV_OBJ_MODEL_MALE);
        check("the item model builds", item != NULL);
        check("the male wear model builds", male != NULL);
        /* The record says these are different model ids. A variant parameter
         * that was ignored — the failure worth probing for — would make them
         * identical, and both would look like the right answer. */
        check("they are not the same mesh",
              item && male &&
                  (item->vertex_count != male->vertex_count ||
                   item->face_count != male->face_count ||
                   memcmp(item->vertices_x, male->vertices_x,
                          (size_t)item->vertex_count * sizeof(*item->vertices_x)) != 0));
        if( item && male )
            printf("  item: %d faces, male wear: %d faces\n", item->face_count, male->face_count);
        ToriDraw_ModelFree(item);
        ToriDraw_ModelFree(male);
    }

    /*
     * A sample of both tables, built.
     *
     * The checks above are about correctness on one record each; this is about
     * the build surviving records nobody would think to click. A NULL is not a
     * failure — a record can legitimately name no model — but a crash or an
     * assert is, and only running them finds that.
     */
    int obj_stride = sample_stride(index.obj_count);
    int loc_stride = sample_stride(index.loc_count);
    printf("\nsweep (every %dth obj, every %dth loc)\n", obj_stride, loc_stride);

    int obj_tried = 0;
    int obj_built = 0;
    for( int i = 0; i < index.obj_count; i += obj_stride )
    {
        struct ToriDraw_Model* m = ev_build_obj_model(&cache, index.objs[i].id, EV_OBJ_MODEL_ITEM);
        obj_tried++;
        if( m )
            obj_built++;
        ToriDraw_ModelFree(m);
    }
    int loc_tried = 0;
    int loc_built = 0;
    for( int i = 0; i < index.loc_count; i += loc_stride )
    {
        struct ToriDraw_Model* m = ev_build_loc_model(&cache, index.locs[i].id, -1);
        loc_tried++;
        if( m )
            loc_built++;
        ToriDraw_ModelFree(m);
    }
    printf("  %d of %d sampled objs and %d of %d sampled locs produced a model\n",
           obj_built, obj_tried, loc_built, loc_tried);
    /* Most, not all: a placeholder, a bank note and a loc that exists only to
     * transform into another all decode perfectly and name no model. */
    check("most sampled objs produce an item model", obj_built * 2 > obj_tried);
    check("most sampled locs produce a model", loc_built * 2 > loc_tried);

    ev_index_free(&index);
    tool_dat2_close(&cache);

    printf("\n%s\n", fails ? "FAILURES" : "all checks passed");
    return fails ? 1 : 0;
}
