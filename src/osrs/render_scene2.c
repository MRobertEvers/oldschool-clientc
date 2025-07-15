#include "render_scene2.h"

#include "gouraud.h"
#include "projection.h"
#include "tables/maps.h"
#include "tables/textures.h"

char*
element2_step_str(enum Element2Step step)
{
    switch( step )
    {
    case E_STEP_VERIFY_FURTHER_TILES_DONE_UNLESS_SPANNED:
        return "VERIFY_FURTHER_TILES_DONE_UNLESS_SPANNED";
    case E_STEP_GROUND:
        return "GROUND";
    case E_STEP_WAIT_ADJACENT_GROUND:
        return "WAIT_ADJACENT_GROUND";
    }

    return NULL;
}

struct IntQueue
{
    int* data;
    int length;
    int capacity;

    int head;
    int tail;
};

static void
int_queue_init(struct IntQueue* queue, int capacity)
{
    queue->data = (int*)malloc(capacity * sizeof(int));
    queue->length = 0;
    queue->capacity = capacity;
}

static void
int_queue_push_wrap(struct IntQueue* queue, int value)
{
    int next_tail = (queue->tail + 1) % queue->capacity;
    assert(next_tail != queue->head);

    queue->data[queue->tail] = value;
    queue->tail = next_tail;
    queue->length++;
}

static int
int_queue_pop(struct IntQueue* queue)
{
    assert((queue->head) != queue->tail);

    int value = queue->data[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->length--;

    return value;
}

static void
int_queue_free(struct IntQueue* queue)
{
    free(queue->data);
}

struct Scene2Op*
render_scene2_compute_ops(
    struct Scene2* scene2, int width, int height, int scene_x, int scene_y, int scene_z)
{
    struct Scene2Op* ops = NULL;
    int op_count = 0;

    struct IntQueue queue = { 0 };
    int_queue_init(&queue, 1024);

    int x_width = scene2->world->x_width;
    int z_width = scene2->world->z_width;

    int scene_x_tile = scene_x / 128;
    int scene_y_tile = scene_y / 128;

    int coord_corner_x[4] = { 0 };
    int coord_corner_z[4] = { 0 };

    int radius = 25;

    coord_corner_x[0] = scene_x_tile - radius;
    coord_corner_z[0] = scene_y_tile - radius;
    coord_corner_x[1] = scene_x_tile - radius;
    coord_corner_z[1] = scene_y_tile + radius;
    coord_corner_x[2] = scene_x_tile + radius;
    coord_corner_z[2] = scene_y_tile - radius;
    coord_corner_x[3] = scene_x_tile + radius;
    coord_corner_z[3] = scene_y_tile + radius;

    if( coord_corner_x[0] < 0 )
        coord_corner_x[0] = 0;
    if( coord_corner_z[0] < 0 )
        coord_corner_z[0] = 0;
    if( coord_corner_x[1] >= x_width )
        coord_corner_x[1] = x_width - 1;
    if( coord_corner_z[1] >= z_width )
        coord_corner_z[1] = z_width - 1;
    if( coord_corner_x[2] < 0 )
        coord_corner_x[2] = 0;
    if( coord_corner_z[2] >= z_width )
        coord_corner_z[2] = z_width - 1;
    if( coord_corner_x[3] >= x_width )
        coord_corner_x[3] = x_width - 1;

    int op_capacity = x_width * z_width * 11;
    ops = (struct Scene2Op*)malloc(op_capacity * sizeof(struct Scene2Op));
    memset(ops, 0, op_capacity * sizeof(struct Scene2Op));

    for( int i = 0; i < 4; i++ )
    {
        int corner_tile_x = coord_corner_x[i];
        int corner_tile_z = coord_corner_z[i];

        int_queue_push_wrap(&queue, MAP_TILE_COORD(corner_tile_x, corner_tile_z, 0));

        while( queue.length > 0 )
        {
            int tile_idx = int_queue_pop(&queue);
            struct WorldTile* tile = &scene2->world->tiles[tile_idx];

            int tile_x = tile->x;
            int tile_z = tile->z;

            if( tile->floor != -1 )
            {
                ops[op_count++] = (struct Scene2Op){
                    .op = SCENE2_OP_TYPE_DRAW_GROUND,
                    .x = tile_x,
                    .z = tile_z,
                    .level = 0,
                };
            }

            if( tile_x > scene_x_tile && tile_x > 0 )
            {
                int_queue_push_wrap(&queue, MAP_TILE_COORD(tile_x - 1, tile_z, 0));
            }

            if( tile_z > scene_y_tile && tile_z > 0 )
            {
                int_queue_push_wrap(&queue, MAP_TILE_COORD(tile_x, tile_z - 1, 0));
            }

            if( tile_x < scene_x_tile && tile_x < MAP_TERRAIN_X )
            {
                int_queue_push_wrap(&queue, MAP_TILE_COORD(tile_x + 1, tile_z, 0));
            }

            if( tile_z < scene_y_tile && tile_z < MAP_TERRAIN_Y )
            {
                int_queue_push_wrap(&queue, MAP_TILE_COORD(tile_x, tile_z + 1, 0));
            }
        }
    }

    return ops;
}

#define TILE_SIZE 128

/**
 * Terrain is treated as a single, so the origin test does not apply.
 */
static void
project_vertices_terrain(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int num_vertices,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int model_pitch,
    int model_yaw,
    int model_roll,
    int camera_x,
    int camera_y,
    int camera_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll,
    int camera_fov,
    int near_plane_z,
    int screen_width,
    int screen_height)
{
    struct ProjectedTriangle projected_triangle;

    assert(camera_pitch >= 0 && camera_pitch < 2048);
    assert(camera_yaw >= 0 && camera_yaw < 2048);
    assert(camera_roll >= 0 && camera_roll < 2048);

    for( int i = 0; i < num_vertices; i++ )
    {
        projected_triangle = project(
            vertex_x[i],
            vertex_y[i],
            vertex_z[i],
            model_pitch,
            model_yaw,
            model_roll,
            camera_x,
            camera_z,
            camera_y,
            camera_pitch,
            camera_yaw,
            camera_roll,
            camera_fov,
            near_plane_z,
            screen_width,
            screen_height);

        if( projected_triangle.clipped )
        {
            // Since terrain vertexes are calculated as a single mesh rather,
            // than around some origin, assume that if any vertex is clipped,
            // then the entire terrain is clipped.
            goto clipped;
        }
        else
        {
            screen_vertices_x[i] = projected_triangle.x;
            screen_vertices_y[i] = projected_triangle.y;
            screen_vertices_z[i] = projected_triangle.z;
        }
    }

    return;
clipped:
    for( int i = 0; i < num_vertices; i++ )
    {
        screen_vertices_x[i] = -5000;
        screen_vertices_y[i] = -5000;
        screen_vertices_z[i] = -5000;
    }

    return;
}

static void
raster_osrs_single_gouraud(
    struct Pixel* pixel_buffer,
    int face,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int* colors_a,
    int* colors_b,
    int* colors_c,
    int offset_x,
    int offset_y,
    int screen_width,
    int screen_height)
{
    int index = face;

    int x1 = vertex_x[face_indices_a[index]] + offset_x;
    int y1 = vertex_y[face_indices_a[index]] + offset_y;
    int z1 = vertex_z[face_indices_a[index]];
    int x2 = vertex_x[face_indices_b[index]] + offset_x;
    int y2 = vertex_y[face_indices_b[index]] + offset_y;
    int z2 = vertex_z[face_indices_b[index]];
    int x3 = vertex_x[face_indices_c[index]] + offset_x;
    int y3 = vertex_y[face_indices_c[index]] + offset_y;
    int z3 = vertex_z[face_indices_c[index]];

    // Skip triangle if any vertex was clipped
    // TODO: Perhaps use a separate buffer to track this.
    if( x1 == -5000 || x2 == -5000 || x3 == -5000 )
        return;

    int color_a = colors_a[index];
    int color_b = colors_b[index];
    int color_c = colors_c[index];

    assert(color_a >= 0 && color_a < 65536);
    assert(color_b >= 0 && color_b < 65536);
    assert(color_c >= 0 && color_c < 65536);

    // drawGouraudTriangle(pixel_buffer, y1, y2, y3, x1, x2, x3, color_a, color_b, color_c);

    raster_gouraud(
        pixel_buffer,
        // z_buffer,
        screen_width,
        screen_height,
        x1,
        x2,
        x3,
        y1,
        y2,
        y3,
        // z1,
        // z2,
        // z3,
        color_a,
        color_b,
        color_c);
}

static void
render_scene_tile(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* ortho_vertices_x,
    int* ortho_vertices_y,
    int* ortho_vertices_z,
    int* pixel_buffer,
    int width,
    int height,
    int near_plane_z,
    int grid_x,
    int grid_y,
    int grid_z,
    int camera_x,
    int camera_y,
    int camera_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll,
    int fov,
    struct SceneTile* tile,
    struct SceneTextures* textures_nullable,
    int* color_override_hsl16_nullable)
{
    if( tile->vertex_count == 0 || tile->face_color_hsl_a == NULL )
        return;

    // TODO: A faster culling
    // if the tile is far enough away, skip it.
    // Calculate distance from camera to tile center
    int tile_center_x = grid_x * TILE_SIZE + TILE_SIZE / 2;
    int tile_center_y = grid_y * TILE_SIZE + TILE_SIZE / 2;
    int tile_center_z = grid_z * 240;

    // int dx = tile_center_x + scene_x;
    // int dy = tile_center_y + scene_y;
    // int dz = tile_center_z + scene_z;

    // Simple squared distance - avoid sqrt for performance
    // int dist_sq = dx * dx;
    // if( dist_sq > TILE_SIZE * TILE_SIZE * 10000 )
    //     return;

    // dist_sq = dy * dy;
    // if( dist_sq > TILE_SIZE * TILE_SIZE * 10000 )
    //     return;

    for( int face = 0; face < tile->face_count; face++ )
    {
        if( tile->valid_faces[face] == 0 )
            continue;

        int texture_id = tile->face_texture_ids ? tile->face_texture_ids[face] : -1;

        if( texture_id == -1 || textures_nullable == NULL )
        {
            project_vertices_terrain(
                screen_vertices_x,
                screen_vertices_y,
                screen_vertices_z,
                tile->vertex_count,
                tile->vertex_x,
                tile->vertex_y,
                tile->vertex_z,
                0,
                0,
                0,
                camera_x,
                camera_y,
                camera_z,
                camera_pitch,
                camera_yaw,
                camera_roll,
                fov,
                near_plane_z,
                width,
                height);

            int* colors_a = color_override_hsl16_nullable ? color_override_hsl16_nullable
                                                          : tile->face_color_hsl_a;
            int* colors_b = color_override_hsl16_nullable ? color_override_hsl16_nullable
                                                          : tile->face_color_hsl_b;
            int* colors_c = color_override_hsl16_nullable ? color_override_hsl16_nullable
                                                          : tile->face_color_hsl_c;

            raster_osrs_single_gouraud(
                pixel_buffer,
                face,
                tile->faces_a,
                tile->faces_b,
                tile->faces_c,
                screen_vertices_x,
                screen_vertices_y,
                screen_vertices_z,
                // TODO: Remove legacy face_color_hsl.
                colors_a,
                colors_b,
                colors_c,
                width / 2,
                height / 2,
                width,
                height);
        }
        else
        {
            // Tile vertexes are wrapped ccw.

            // finish bathroom and floor in basement
            // two tables against the wall put in garage.
            // add 0404
            // project_vertices_terrain_textured(
            //     screen_vertices_x,
            //     screen_vertices_y,
            //     screen_vertices_z,
            //     ortho_vertices_x,
            //     ortho_vertices_y,
            //     ortho_vertices_z,
            //     tile->vertex_count,
            //     tile->vertex_x,
            //     tile->vertex_y,
            //     tile->vertex_z,
            //     0,
            //     0,
            //     0,
            //     camera_x,
            //     camera_y,
            //     camera_z,
            //     camera_pitch,
            //     camera_yaw,
            //     camera_roll,
            //     fov,
            //     near_plane_z,
            //     width,
            //     height);

            // bool success = raster_osrs_single_texture(
            //     pixel_buffer,
            //     width,
            //     height,
            //     face,
            //     tile->faces_a,
            //     tile->faces_b,
            //     tile->faces_c,
            //     screen_vertices_x,
            //     screen_vertices_y,
            //     screen_vertices_z,
            //     ortho_vertices_x,
            //     ortho_vertices_y,
            //     ortho_vertices_z,
            //     tile->face_texture_ids,
            //     tile->face_texture_u_a,
            //     tile->face_texture_v_a,
            //     tile->face_texture_u_b,
            //     tile->face_texture_v_b,
            //     tile->face_texture_u_c,
            //     tile->face_texture_v_c,
            //     textures_nullable,
            //     width / 2,
            //     height / 2);
        }
    }
}

static int g_screen_vertices_x[20];
static int g_screen_vertices_y[20];
static int g_screen_vertices_z[20];
static int g_ortho_vertices_x[20];
static int g_ortho_vertices_y[20];
static int g_ortho_vertices_z[20];

void
render_scene2_ops(
    struct Scene2Op* ops,
    int op_count,
    int offset,
    int number_to_render,
    int* pixel_buffer,
    int width,
    int height,
    int near_plane_z,
    int scene_x,
    int scene_y,
    int scene_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll,
    int fov,
    struct Scene2* scene2)
{
    int* screen_vertices_x = g_screen_vertices_x;
    int* screen_vertices_y = g_screen_vertices_y;
    int* screen_vertices_z = g_screen_vertices_z;
    int* ortho_vertices_x = g_ortho_vertices_x;
    int* ortho_vertices_y = g_ortho_vertices_y;
    int* ortho_vertices_z = g_ortho_vertices_z;

    for( int i = offset; i < offset + number_to_render; i++ )
    {
        struct Scene2Op* op = &ops[i];

        struct WorldTile* tile = NULL;
        tile = &scene2->world->tiles[MAP_TILE_COORD(op->x, op->z, op->level)];

        switch( op->op )
        {
        case SCENE2_OP_TYPE_DRAW_GROUND:
        {
            struct Scene2Floor* floor = &scene2->terrain[MAP_TILE_COORD(op->x, op->z, op->level)];

            int tile_x = tile->x;
            int tile_y = tile->z;
            int tile_z = tile->level;

            int* color_override_hsl16_nullable = NULL;
            if( op->_ground.override_color )
            {
                color_override_hsl16_nullable = (int*)malloc(sizeof(int) * floor->face_count);
                for( int j = 0; j < floor->face_count; j++ )
                {
                    color_override_hsl16_nullable[j] = op->_ground.color_hsl16;
                }
            }

            render_scene_tile(
                screen_vertices_x,
                screen_vertices_y,
                screen_vertices_z,
                ortho_vertices_x,
                ortho_vertices_y,
                ortho_vertices_z,
                pixel_buffer,
                width,
                height,
                near_plane_z,
                tile_x,
                tile_y,
                tile_z,
                scene_x,
                scene_y,
                scene_z,
                camera_pitch,
                camera_yaw,
                camera_roll,
                fov,
                tile,
                NULL,
                color_override_hsl16_nullable);
        }
        }
    }
}