#include "contour_ground.h"

#include <assert.h>

/** Scene units per heightmap tile axis (subpixels). */
enum
{
    CONTOUR_SCENE_SUBPIXELS_PER_TILE = 128,
    CONTOUR_SCENE_TILE_SHIFT = 7,
};

/** Fixed-point shift for type-2 Y ratio (`vy` vs `_min_y`). */
enum
{
    CONTOUR_Y_RATIO_FP_SHIFT = 16,
};

/** Fixed-point shift for type-5 blend along vertex Y span. */
enum
{
    CONTOUR_LEVEL_BLEND_FP_SHIFT = 8,
};

/** Async height-fetch pipeline step stored in `_substep`. */
enum ContourGroundSubstep
{
    CG_SUBSTEP_BEFORE_PRIMARY_FETCH = 0,
    CG_SUBSTEP_AFTER_PRIMARY_FETCH = 1,
    CG_SUBSTEP_AFTER_SECONDARY_FETCH = 2,
};

static bool
type_needs_main_heightmap_bounds(enum ContourGroundType t)
{
    return t == CONTOUR_GROUND_SCENE_FOLLOW || t == CONTOUR_GROUND_THRESHOLD_BLEND ||
           t == CONTOUR_GROUND_MODEL_Y || t == CONTOUR_GROUND_DUAL_LEVEL_BLEND;
}

static bool
type_needs_above_heightmap(enum ContourGroundType t)
{
    return t == CONTOUR_GROUND_ABOVE_OFFSET || t == CONTOUR_GROUND_DUAL_LEVEL_BLEND;
}

static bool
vertex_interp_in_bounds(
    int vx,
    int vz,
    int hm_size_x,
    int hm_size_z)
{
    int tx = vx >> CONTOUR_SCENE_TILE_SHIFT;
    int tz = vz >> CONTOUR_SCENE_TILE_SHIFT;
    return tx >= 0 && tx < hm_size_x - 1 && tz >= 0 && tz < hm_size_z - 1;
}

static bool
bbox_fits_heightmap(
    int start_x,
    int end_x,
    int start_z,
    int end_z,
    int hm_size_x,
    int hm_size_z)
{
    if( start_x < 0 || start_z < 0 )
    {
        return false;
    }
    if( ((end_x + CONTOUR_SCENE_SUBPIXELS_PER_TILE) >> CONTOUR_SCENE_TILE_SHIFT) >= hm_size_x ||
        ((end_z + CONTOUR_SCENE_SUBPIXELS_PER_TILE) >> CONTOUR_SCENE_TILE_SHIFT) >= hm_size_z )
    {
        return false;
    }
    return true;
}

static bool
contour_ground_next_type_scene_follow(
    struct ContourGround* cg,
    int i,
    int vx,
    int vz,
    int vy,
    struct ContourGroundCommand* cmd)
{
    if( cg->_substep == CG_SUBSTEP_BEFORE_PRIMARY_FETCH )
    {
        if( i >= cg->used_vertex_count &&
            !vertex_interp_in_bounds(vx, vz, cg->_hm_size_x, cg->_hm_size_z) )
        {
            cmd->kind = CONTOUR_CMD_SET_Y;
            cmd->vertex_index = i;
            cmd->contour_y = vy;
            cg->_i++;
            cg->_substep = CG_SUBSTEP_BEFORE_PRIMARY_FETCH;
            return true;
        }
        cmd->kind = CONTOUR_CMD_FETCH_HEIGHT;
        cmd->draw_x = vx;
        cmd->draw_z = vz;
        cmd->slevel = cg->slevel;
        return true;
    }
    cmd->kind = CONTOUR_CMD_SET_Y;
    cmd->vertex_index = i;
    cmd->contour_y = vy + cg->_height - cg->scene_height;
    cg->_i++;
    cg->_substep = CG_SUBSTEP_BEFORE_PRIMARY_FETCH;
    return true;
}

static bool
contour_ground_next_type_threshold_blend(
    struct ContourGround* cg,
    int i,
    int vx,
    int vz,
    int vy,
    struct ContourGroundCommand* cmd)
{
    if( cg->_min_y == 0 )
    {
        cmd->kind = CONTOUR_CMD_SET_Y;
        cmd->vertex_index = i;
        cmd->contour_y = vy;
        cg->_i++;
        cg->_substep = CG_SUBSTEP_BEFORE_PRIMARY_FETCH;
        return true;
    }

    int y_ratio = (vy << CONTOUR_Y_RATIO_FP_SHIFT) / cg->_min_y;
    if( cg->_substep == CG_SUBSTEP_BEFORE_PRIMARY_FETCH )
    {
        if( y_ratio >= cg->param )
        {
            cmd->kind = CONTOUR_CMD_SET_Y;
            cmd->vertex_index = i;
            cmd->contour_y = vy;
            cg->_i++;
            cg->_substep = CG_SUBSTEP_BEFORE_PRIMARY_FETCH;
            return true;
        }
        if( i >= cg->used_vertex_count &&
            !vertex_interp_in_bounds(vx, vz, cg->_hm_size_x, cg->_hm_size_z) )
        {
            cmd->kind = CONTOUR_CMD_SET_Y;
            cmd->vertex_index = i;
            cmd->contour_y = vy;
            cg->_i++;
            cg->_substep = CG_SUBSTEP_BEFORE_PRIMARY_FETCH;
            return true;
        }
        cmd->kind = CONTOUR_CMD_FETCH_HEIGHT;
        cmd->draw_x = vx;
        cmd->draw_z = vz;
        cmd->slevel = cg->slevel;
        return true;
    }
    cmd->kind = CONTOUR_CMD_SET_Y;
    cmd->vertex_index = i;
    cmd->contour_y = vy + ((cg->_height - cg->scene_height) * (cg->param - y_ratio)) / cg->param;
    cg->_i++;
    cg->_substep = CG_SUBSTEP_BEFORE_PRIMARY_FETCH;
    return true;
}

static bool
contour_ground_next_type_model_y(
    struct ContourGround* cg,
    int i,
    int vy,
    struct ContourGroundCommand* cmd)
{
    cmd->kind = CONTOUR_CMD_SET_Y;
    cmd->vertex_index = i;
    cmd->contour_y = vy;
    cg->_i++;
    return true;
}

static bool
contour_ground_next_type_above_offset(
    struct ContourGround* cg,
    int i,
    int vx,
    int vz,
    int vy,
    struct ContourGroundCommand* cmd)
{
    if( cg->_substep == CG_SUBSTEP_BEFORE_PRIMARY_FETCH )
    {
        cmd->kind = CONTOUR_CMD_FETCH_HEIGHT_ABOVE;
        cmd->draw_x = vx;
        cmd->draw_z = vz;
        cmd->slevel = cg->slevel;
        return true;
    }
    cmd->kind = CONTOUR_CMD_SET_Y;
    cmd->vertex_index = i;
    cmd->contour_y = vy + cg->_height_above - cg->scene_height + cg->_delta_y;
    cg->_i++;
    cg->_substep = CG_SUBSTEP_BEFORE_PRIMARY_FETCH;
    return true;
}

static bool
contour_ground_next_type_dual_level_blend(
    struct ContourGround* cg,
    int i,
    int vx,
    int vz,
    int vy,
    struct ContourGroundCommand* cmd)
{
    if( cg->_delta_y == 0 )
    {
        cmd->kind = CONTOUR_CMD_SET_Y;
        cmd->vertex_index = i;
        cmd->contour_y = vy;
        cg->_i++;
        cg->_substep = CG_SUBSTEP_BEFORE_PRIMARY_FETCH;
        return true;
    }
    if( cg->_substep == CG_SUBSTEP_BEFORE_PRIMARY_FETCH )
    {
        cmd->kind = CONTOUR_CMD_FETCH_HEIGHT;
        cmd->draw_x = vx;
        cmd->draw_z = vz;
        cmd->slevel = cg->slevel;
        return true;
    }
    if( cg->_substep == CG_SUBSTEP_AFTER_PRIMARY_FETCH )
    {
        cmd->kind = CONTOUR_CMD_FETCH_HEIGHT_ABOVE;
        cmd->draw_x = vx;
        cmd->draw_z = vz;
        cmd->slevel = cg->slevel;
        return true;
    }
    {
        int delta_height = cg->_height - cg->_height_above;
        int t = (vy << CONTOUR_LEVEL_BLEND_FP_SHIFT) / cg->_delta_y;
        cmd->kind = CONTOUR_CMD_SET_Y;
        cmd->vertex_index = i;
        cmd->contour_y =
            (t * delta_height >> CONTOUR_LEVEL_BLEND_FP_SHIFT) - (cg->scene_height - cg->_height);
        cg->_i++;
        cg->_substep = CG_SUBSTEP_BEFORE_PRIMARY_FETCH;
        return true;
    }
}

static void
contour_ground_provide_type_scene_follow_or_threshold(
    struct ContourGround* cg,
    int height)
{
    cg->_height = height;
    cg->_substep = CG_SUBSTEP_AFTER_PRIMARY_FETCH;
}

static void
contour_ground_provide_type_above_offset(
    struct ContourGround* cg,
    int height)
{
    cg->_height_above = height;
    cg->_substep = CG_SUBSTEP_AFTER_PRIMARY_FETCH;
}

static void
contour_ground_provide_type_dual_level_blend(
    struct ContourGround* cg,
    int height)
{
    if( cg->_substep == CG_SUBSTEP_BEFORE_PRIMARY_FETCH )
    {
        cg->_height = height;
        cg->_substep = CG_SUBSTEP_AFTER_PRIMARY_FETCH;
    }
    else
    {
        cg->_height_above = height;
        cg->_substep = CG_SUBSTEP_AFTER_SECONDARY_FETCH;
    }
}

bool
contour_ground_init(
    struct ContourGround* cg,
    int type,
    int param,
    int hm_size_x,
    int hm_size_z,
    int hm_above_size_x,
    int hm_above_size_z,
    const void* vertices_x,
    const void* vertices_y,
    const void* vertices_z,
    int vertex_count,
    int used_vertex_count,
    enum ContourVertexType vertex_type,
    int scene_x,
    int scene_z,
    int scene_height,
    int slevel)
{
    assert(cg);
    cg->_active = false;

    if( used_vertex_count <= 0 || vertex_count <= 0 || used_vertex_count > vertex_count ||
        !vertices_x || !vertices_y || !vertices_z )
    {
        return false;
    }

    if( type < (int)CONTOUR_GROUND_SCENE_FOLLOW || type > (int)CONTOUR_GROUND_DUAL_LEVEL_BLEND )
    {
        return false;
    }

    enum ContourGroundType ctype = (enum ContourGroundType)type;

    int min_x = contour_vertex_get(vertices_x, vertex_type, 0);
    int max_x = min_x;
    int min_z = contour_vertex_get(vertices_z, vertex_type, 0);
    int max_z = min_z;
    int min_y = contour_vertex_get(vertices_y, vertex_type, 0);
    int max_y = min_y;

    for( int i = 1; i < used_vertex_count; i++ )
    {
        int vx = contour_vertex_get(vertices_x, vertex_type, i);
        int vz = contour_vertex_get(vertices_z, vertex_type, i);
        int vy = contour_vertex_get(vertices_y, vertex_type, i);
        if( vx < min_x )
        {
            min_x = vx;
        }
        if( vx > max_x )
        {
            max_x = vx;
        }
        if( vz < min_z )
        {
            min_z = vz;
        }
        if( vz > max_z )
        {
            max_z = vz;
        }
        if( vy < min_y )
        {
            min_y = vy;
        }
        if( vy > max_y )
        {
            max_y = vy;
        }
    }

    int start_x = scene_x + min_x;
    int end_x = scene_x + max_x;
    int start_z = scene_z + min_z;
    int end_z = scene_z + max_z;

    if( type_needs_main_heightmap_bounds(ctype) )
    {
        if( !bbox_fits_heightmap(start_x, end_x, start_z, end_z, hm_size_x, hm_size_z) )
        {
            return false;
        }
    }

    if( type_needs_above_heightmap(ctype) )
    {
        if( hm_above_size_x <= 0 || hm_above_size_z <= 0 ||
            !bbox_fits_heightmap(start_x, end_x, start_z, end_z, hm_above_size_x, hm_above_size_z) )
        {
            return false;
        }
    }

    cg->type = ctype;
    cg->param = param;
    cg->scene_x = scene_x;
    cg->scene_z = scene_z;
    cg->scene_height = scene_height;
    cg->slevel = slevel;
    cg->vertices_x = vertices_x;
    cg->vertices_y = vertices_y;
    cg->vertices_z = vertices_z;
    cg->vertex_count = vertex_count;
    cg->used_vertex_count = used_vertex_count;
    cg->vertex_type = vertex_type;
    cg->_min_y = min_y;
    cg->_delta_y = max_y - min_y;
    cg->_hm_size_x = hm_size_x;
    cg->_hm_size_z = hm_size_z;
    cg->_i = 0;
    cg->_substep = CG_SUBSTEP_BEFORE_PRIMARY_FETCH;

    if( ctype == CONTOUR_GROUND_SCENE_FOLLOW || ctype == CONTOUR_GROUND_THRESHOLD_BLEND )
    {
        cg->_limit = vertex_count;
    }
    else
    {
        cg->_limit = used_vertex_count;
    }

    cg->_active = true;
    return true;
}

bool
contour_ground_next(
    struct ContourGround* cg,
    struct ContourGroundCommand* cmd)
{
    assert(cg && cmd);

    if( !cg->_active || cg->_i >= cg->_limit )
    {
        return false;
    }

    int i = cg->_i;
    int vx = contour_vertex_get(cg->vertices_x, cg->vertex_type, i) + cg->scene_x;
    int vz = contour_vertex_get(cg->vertices_z, cg->vertex_type, i) + cg->scene_z;
    int vy = contour_vertex_get(cg->vertices_y, cg->vertex_type, i);

    switch( cg->type )
    {
    case CONTOUR_GROUND_SCENE_FOLLOW:
        return contour_ground_next_type_scene_follow(cg, i, vx, vz, vy, cmd);
    case CONTOUR_GROUND_THRESHOLD_BLEND:
        return contour_ground_next_type_threshold_blend(cg, i, vx, vz, vy, cmd);
    case CONTOUR_GROUND_MODEL_Y:
        return contour_ground_next_type_model_y(cg, i, vy, cmd);
    case CONTOUR_GROUND_ABOVE_OFFSET:
        return contour_ground_next_type_above_offset(cg, i, vx, vz, vy, cmd);
    case CONTOUR_GROUND_DUAL_LEVEL_BLEND:
        return contour_ground_next_type_dual_level_blend(cg, i, vx, vz, vy, cmd);
    default:
        return false;
    }
}

void
contour_ground_provide(
    struct ContourGround* cg,
    int height)
{
    assert(cg && cg->_active);

    switch( cg->type )
    {
    case CONTOUR_GROUND_SCENE_FOLLOW:
    case CONTOUR_GROUND_THRESHOLD_BLEND:
        contour_ground_provide_type_scene_follow_or_threshold(cg, height);
        break;
    case CONTOUR_GROUND_ABOVE_OFFSET:
        contour_ground_provide_type_above_offset(cg, height);
        break;
    case CONTOUR_GROUND_DUAL_LEVEL_BLEND:
        contour_ground_provide_type_dual_level_blend(cg, height);
        break;
    default:
        break;
    }
}
