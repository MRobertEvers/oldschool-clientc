#ifndef WORLD_CONTOUR_GROUND_U_C
#define WORLD_CONTOUR_GROUND_U_C

#include "contour_ground.h"
#include "gamecache/gamecache.h"
#include "heightmap.h"
#include "toridraw/toridraw_model_transform.h"
#include "world_builder.h"

#include <stdbool.h>
#include <stdint.h>

void
world_contour_ground(struct WorldBuilder* builder)
{
    struct World* world = builder->world;
    int n = builder->contour_ground_queue_count;
    if( n <= 0 || !builder->contour_ground_queue )
        return;

    struct Heightmap* hm = world->heightmap;

    for( int ri = 0; ri < n; ri++ )
    {
        struct ContourGroundQueueEntry* r = &builder->contour_ground_queue[ri];
        if( r->element_id < 0 )
            continue;

        struct ToriDraw_GCElement* el = toridraw_gc_element_get(builder->context, r->element_id);
        if( !el || el->model.kind != TORIDRAWMK_MODEL || !el->model.u.model.model )
            continue;

        struct GameCache_Location* config_loc =
            gamecache_location_get(builder->gamecache, r->loc_id);
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

        toridraw_model_set_bounds_cylinder(dm);
    }

    builder->contour_ground_queue_count = 0;
}

#endif