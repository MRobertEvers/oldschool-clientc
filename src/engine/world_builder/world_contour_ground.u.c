#ifndef WORLD_CONTOUR_GROUND_U_C
#define WORLD_CONTOUR_GROUND_U_C

#include "contour_ground.h"
#include "engine/cache_provider.h"
#include "heightmap.h"
#include "toridraw_model_transform.h"
#include "world_builder.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Contour-ground every queued placement in [begin, end) of the queue. Each
 * entry deforms its own element's model against the finished heightmap and
 * nothing else, so a range is the whole pass's work for those entries.
 */
static void
world_contour_ground_range(
    struct WorldBuilder* builder,
    int begin,
    int end)
{
    struct World* world = builder->world;
    struct Heightmap* hm = world->heightmap;

    for( int ri = begin; ri < end; ri++ )
    {
        struct ContourGroundQueueEntry* r = contour_ground_q_at(&builder->contour_ground_queue, ri);
        if( r->element_id < 0 )
            continue;

        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(builder->scene, r->element_id);
        if( !el || !ToriDraw_ModelKindIsFull(el->model.kind) || !el->model.u.model.model )
            continue;

        struct ToriRS_Location* config_loc = CacheProvider_LocationGet(builder->cache, r->loc_id);
        if( !config_loc || config_loc->contour_ground_type == 0 )
            continue;

        struct ToriDraw_Model* dm = el->model.u.model.model;
        int contour_type = config_loc->contour_ground_type;
        int contour_param = config_loc->contour_ground_param;
        int sl = r->level;

        int wx = el->world_position.x;
        int wz = el->world_position.z;
        int wy = el->world_position.y;

        if( (contour_type == CONTOUR_GROUND_ABOVE_OFFSET ||
             contour_type == CONTOUR_GROUND_DUAL_LEVEL_BLEND) &&
            sl + 1 >= hm->levels )
            continue;

        int hm_ax = hm->size_x;
        int hm_az = hm->size_z;
        int above_ax = (contour_type == CONTOUR_GROUND_ABOVE_OFFSET ||
                        contour_type == CONTOUR_GROUND_DUAL_LEVEL_BLEND)
                           ? hm_ax
                           : 0;
        int above_az = (contour_type == CONTOUR_GROUND_ABOVE_OFFSET ||
                        contour_type == CONTOUR_GROUND_DUAL_LEVEL_BLEND)
                           ? hm_az
                           : 0;

        int vc = dm->vertex_count;
        vertexint_t* vxs = dm->vertices_x;
        vertexint_t* vys = dm->vertices_y;
        vertexint_t* vzs = dm->vertices_z;
        if( vc <= 0 || !vxs || !vys || !vzs )
            continue;

        struct ContourGround cg;
        if( !contour_ground_init(
                &cg,
                contour_type,
                contour_param,
                hm_ax,
                hm_az,
                above_ax,
                above_az,
                vxs,
                vys,
                vzs,
                vc,
                vc,
                CONTOUR_VERTEX_INT16,
                wx,
                wz,
                wy,
                sl) )
            continue;

        struct ContourGroundCommand cmd;
        while( contour_ground_next(&cg, &cmd) )
        {
            switch( cmd.kind )
            {
            case CONTOUR_CMD_FETCH_HEIGHT:
                contour_ground_provide(
                    &cg, heightmap_get_interpolated(hm, cmd.draw_x, cmd.draw_z, cmd.slevel));
                break;
            case CONTOUR_CMD_FETCH_HEIGHT_ABOVE:
                contour_ground_provide(
                    &cg, heightmap_get_interpolated(hm, cmd.draw_x, cmd.draw_z, sl + 1));
                break;
            case CONTOUR_CMD_SET_Y:
                vys[cmd.vertex_index] = (vertexint_t)cmd.contour_y;
                break;
            }
        }

        ToriDraw_ModelSetBoundsCylinder(dm);
    }
}

/* Placements per unit of the stepped build's contour pass. */
#define WORLD_CONTOUR_PER_UNIT 384

static void
world_contour_ground_init(
    struct WorldBuilder* builder,
    struct WorldBuildContour* cur)
{
    assert(builder);
    assert(cur);
    cur->index = 0;
    cur->count = contour_ground_q_count(&builder->contour_ground_queue);
}

/* One unit of at most `per_unit` placements. Returns 1 once the queue is done
 * (and emptied). */
static int
world_contour_ground_step(
    struct WorldBuilder* builder,
    struct WorldBuildContour* cur,
    int per_unit)
{
    int end;

    assert(cur);
    assert(per_unit > 0);
    end = cur->index + per_unit;
    if( end > cur->count )
        end = cur->count;
    world_contour_ground_range(builder, cur->index, end);
    cur->index = end;
    if( cur->index < cur->count )
        return 0;
    contour_ground_q_clear(&builder->contour_ground_queue);
    return 1;
}

/* The whole queue at once: the blocking build's End. */
void
world_contour_ground(struct WorldBuilder* builder)
{
    struct WorldBuildContour cur;

    world_contour_ground_init(builder, &cur);
    if( cur.count <= 0 )
        return;
    world_contour_ground_step(builder, &cur, cur.count);
}

#endif