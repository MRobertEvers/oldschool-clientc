#include "contour_ground.h"

#include <assert.h>

static bool
vertex_interp_in_bounds(
    int vx,
    int vz,
    int hm_size_x,
    int hm_size_z)
{
    int tx = vx >> 7;
    int tz = vz >> 7;
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
    if( ((end_x + 128) >> 7) >= hm_size_x || ((end_z + 128) >> 7) >= hm_size_z )
    {
        return false;
    }
    return true;
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

    if( type < 1 || type > 5 )
    {
        return false;
    }

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

    if( type == 1 || type == 2 || type == 3 || type == 5 )
    {
        if( !bbox_fits_heightmap(start_x, end_x, start_z, end_z, hm_size_x, hm_size_z) )
        {
            return false;
        }
    }

    if( type == 4 || type == 5 )
    {
        if( hm_above_size_x <= 0 || hm_above_size_z <= 0 ||
            !bbox_fits_heightmap(start_x, end_x, start_z, end_z, hm_above_size_x, hm_above_size_z) )
        {
            return false;
        }
    }

    cg->type = type;
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
    cg->_substep = 0;

    if( type == 1 || type == 2 )
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
    case 1:
        if( cg->_substep == 0 )
        {
            if( i >= cg->used_vertex_count &&
                !vertex_interp_in_bounds(vx, vz, cg->_hm_size_x, cg->_hm_size_z) )
            {
                cmd->kind = CONTOUR_CMD_SET_Y;
                cmd->vertex_index = i;
                cmd->contour_y = vy;
                cg->_i++;
                cg->_substep = 0;
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
        cg->_substep = 0;
        return true;

    case 2:
        if( cg->_min_y == 0 )
        {
            cmd->kind = CONTOUR_CMD_SET_Y;
            cmd->vertex_index = i;
            cmd->contour_y = vy;
            cg->_i++;
            cg->_substep = 0;
            return true;
        }
        {
            int y_ratio = ((vy << 16) / cg->_min_y) | 0;
            if( cg->_substep == 0 )
            {
                if( y_ratio >= cg->param )
                {
                    cmd->kind = CONTOUR_CMD_SET_Y;
                    cmd->vertex_index = i;
                    cmd->contour_y = vy;
                    cg->_i++;
                    cg->_substep = 0;
                    return true;
                }
                if( i >= cg->used_vertex_count &&
                    !vertex_interp_in_bounds(vx, vz, cg->_hm_size_x, cg->_hm_size_z) )
                {
                    cmd->kind = CONTOUR_CMD_SET_Y;
                    cmd->vertex_index = i;
                    cmd->contour_y = vy;
                    cg->_i++;
                    cg->_substep = 0;
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
            cmd->contour_y =
                vy + ((cg->_height - cg->scene_height) * (cg->param - y_ratio)) / cg->param;
            cg->_i++;
            cg->_substep = 0;
            return true;
        }

    case 3:
        cmd->kind = CONTOUR_CMD_SET_Y;
        cmd->vertex_index = i;
        cmd->contour_y = vy;
        cg->_i++;
        return true;

    case 4:
        if( cg->_substep == 0 )
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
        cg->_substep = 0;
        return true;

    case 5:
        if( cg->_delta_y == 0 )
        {
            cmd->kind = CONTOUR_CMD_SET_Y;
            cmd->vertex_index = i;
            cmd->contour_y = vy;
            cg->_i++;
            cg->_substep = 0;
            return true;
        }
        if( cg->_substep == 0 )
        {
            cmd->kind = CONTOUR_CMD_FETCH_HEIGHT;
            cmd->draw_x = vx;
            cmd->draw_z = vz;
            cmd->slevel = cg->slevel;
            return true;
        }
        if( cg->_substep == 1 )
        {
            cmd->kind = CONTOUR_CMD_FETCH_HEIGHT_ABOVE;
            cmd->draw_x = vx;
            cmd->draw_z = vz;
            cmd->slevel = cg->slevel;
            return true;
        }
        {
            int delta_height = cg->_height - cg->_height_above;
            cmd->kind = CONTOUR_CMD_SET_Y;
            cmd->vertex_index = i;
            cmd->contour_y = (((((vy << 8) / cg->_delta_y) | 0) * delta_height) >> 8) -
                             (cg->scene_height - cg->_height);
            cg->_i++;
            cg->_substep = 0;
            return true;
        }

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
    case 1:
    case 2:
        cg->_height = height;
        cg->_substep = 1;
        break;
    case 4:
        cg->_height_above = height;
        cg->_substep = 1;
        break;
    case 5:
        if( cg->_substep == 0 )
        {
            cg->_height = height;
            cg->_substep = 1;
        }
        else
        {
            cg->_height_above = height;
            cg->_substep = 2;
        }
        break;
    default:
        break;
    }
}
