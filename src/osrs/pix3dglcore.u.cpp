#ifndef PIX3DGLCORE_U_CPP
#define PIX3DGLCORE_U_CPP

#include "graphics/uv_pnm.h"
#include <unordered_map>

#include <algorithm>
#include <math.h>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Declare g_hsl16_to_rgb_table (it's defined in shared_tables.c)
// Note: This should match the declaration in shared_tables.h
#ifdef __cplusplus
extern "C" {
#endif
extern int g_hsl16_to_rgb_table[65536];
#ifdef __cplusplus
}
#endif

// CPU-side matrix computation functions for performance
// These are shared between OpenGL 3.x and OpenGL ES2
static void
compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw)
{
    float cosPitch = cos(-pitch);
    float sinPitch = sin(-pitch);
    float cosYaw = cos(-yaw);
    float sinYaw = sin(-yaw);

    // Combined rotation * translation matrix (column-major for OpenGL)
    // First apply translation, then yaw, then pitch
    out_matrix[0] = cosYaw;
    out_matrix[1] = sinYaw * sinPitch;
    out_matrix[2] = sinYaw * cosPitch;
    out_matrix[3] = 0.0f;

    out_matrix[4] = 0.0f;
    out_matrix[5] = cosPitch;
    out_matrix[6] = -sinPitch;
    out_matrix[7] = 0.0f;

    out_matrix[8] = -sinYaw;
    out_matrix[9] = cosYaw * sinPitch;
    out_matrix[10] = cosYaw * cosPitch;
    out_matrix[11] = 0.0f;

    out_matrix[12] = -camera_x * cosYaw + camera_z * sinYaw;
    out_matrix[13] =
        -camera_x * sinYaw * sinPitch - camera_y * cosPitch - camera_z * cosYaw * sinPitch;
    out_matrix[14] =
        -camera_x * sinYaw * cosPitch + camera_y * sinPitch - camera_z * cosYaw * cosPitch;
    out_matrix[15] = 1.0f;
}

static void
compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height)
{
    float y = 1.0f / tan(fov * 0.5f);
    float x = y;

    // Column-major for OpenGL
    // The 512.0f / (screen_width/2.0f) and 512.0f / (screen_height/2.0f) scaling
    // already accounts for aspect ratio correctly for this renderer's coordinate system
    out_matrix[0] = x * 512.0f / (screen_width / 2.0f);
    out_matrix[1] = 0.0f;
    out_matrix[2] = 0.0f;
    out_matrix[3] = 0.0f;

    out_matrix[4] = 0.0f;
    out_matrix[5] = -y * 512.0f / (screen_height / 2.0f);
    out_matrix[6] = 0.0f;
    out_matrix[7] = 0.0f;

    out_matrix[8] = 0.0f;
    out_matrix[9] = 0.0f;
    out_matrix[10] = 0.0f;
    out_matrix[11] = 1.0f;

    out_matrix[12] = 0.0f;
    out_matrix[13] = 0.0f;
    out_matrix[14] = -1.0f;
    out_matrix[15] = 0.0f;
}

struct Pix3DGLCoreFaceRange
{
    int start_vertex;
    int face_index;
};

struct Pix3DGLCoreDrawBatch
{
    int texture_id;
    std::vector<int> face_starts;
    std::vector<int> face_counts;
};

struct Pix3DGLCoreModelRange
{
    int scene_model_idx;
    int start_vertex;
    int vertex_count;
    std::vector<Pix3DGLCoreFaceRange> faces;
};

struct Pix3DGLCoreBuffers
{
    std::vector<float> vertices;
    std::vector<float> colors;
    std::vector<float> texCoords;
    std::vector<float> textureIds;
    std::vector<float> textureOpaques;
    std::vector<float> textureAnimSpeeds;
    std::unordered_map<int, int> texture_to_atlas_slot;
    std::unordered_map<int, bool> texture_to_opaque;
    std::unordered_map<int, float> texture_to_animation_speed;
    std::unordered_map<int, Pix3DGLCoreModelRange> model_ranges;

    std::unordered_map<int, Pix3DGLCoreDrawBatch> texture_batches;

    int total_vertex_count;
    int atlas_tiles_per_row;
    int atlas_tile_size;
    int atlas_size;
};

#endif