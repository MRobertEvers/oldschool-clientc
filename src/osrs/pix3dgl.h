#ifndef OSRS_PIX3D_H
#define OSRS_PIX3D_H

#ifdef __cplusplus
extern "C" {
#endif

struct Pix3DGL;
struct TexturesCache;
struct Cache;
struct Pix3DGLCoreBuffers;

struct Pix3DGL* pix3dgl_new();
void pix3dgl_load_texture(
    struct Pix3DGL* pix3dgl,
    int texture_id,
    struct TexturesCache* textures_cache,
    struct Cache* cache);

void pix3dgl_render_with_camera(
    struct Pix3DGL* pix3dgl,
    float camera_x,
    float camera_y,
    float camera_z,
    float camera_pitch,
    float camera_yaw,
    float screen_width,
    float screen_height);

void pix3dgl_begin_frame(
    struct Pix3DGL* pix3dgl,
    float camera_x,
    float camera_y,
    float camera_z,
    float camera_pitch,
    float camera_yaw,
    float screen_width,
    float screen_height);
void pix3dgl_end_frame(struct Pix3DGL* pix3dgl);

// Static scene batching - for efficiently rendering static geometry
void pix3dgl_scene_static_load_begin(struct Pix3DGL* pix3dgl);
void pix3dgl_scene_static_load_tile(
    struct Pix3DGL* pix3dgl,
    int scene_tile_idx,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int vertex_count,
    int* faces_a,
    int* faces_b,
    int* faces_c,
    int face_count,
    int* face_texture_ids,
    int* face_color_hsl_a,
    int* face_color_hsl_b,
    int* face_color_hsl_c);
void pix3dgl_scene_static_load_model(
    struct Pix3DGL* pix3dgl,
    int scene_model_idx,
    int* vertices_x,
    int* vertices_y,
    int* vertices_z,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int face_count,
    int* face_textures_nullable,
    int* face_texture_coords_nullable,
    int* textured_p_coordinate_nullable,
    int* textured_m_coordinate_nullable,
    int* textured_n_coordinate_nullable,
    int* face_colors_hsl_a,
    int* face_colors_hsl_b,
    int* face_colors_hsl_c,
    int* face_infos_nullable,
    int* face_alphas_nullable,
    float position_x,
    float position_y,
    float position_z,
    float yaw);
void pix3dgl_scene_static_load_end(struct Pix3DGL* pix3dgl);

// Animation support - load models with multiple keyframes
void pix3dgl_scene_static_load_animated_model_begin(
    struct Pix3DGL* pix3dgl, int scene_model_idx, int frame_count);
void pix3dgl_scene_static_load_animated_model_keyframe(
    struct Pix3DGL* pix3dgl,
    int scene_model_idx,
    int frame_idx,
    int* vertices_x,
    int* vertices_y,
    int* vertices_z,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int face_count,
    int* face_textures_nullable,
    int* face_texture_coords_nullable,
    int* textured_p_coordinate_nullable,
    int* textured_m_coordinate_nullable,
    int* textured_n_coordinate_nullable,
    int* face_colors_hsl_a,
    int* face_colors_hsl_b,
    int* face_colors_hsl_c,
    int* face_infos_nullable,
    int* face_alphas_nullable,
    float position_x,
    float position_y,
    float position_z,
    float yaw);
void pix3dgl_scene_static_load_animated_model_end(
    struct Pix3DGL* pix3dgl, int scene_model_idx, int* frame_lengths, int frame_count);

void
pix3dgl_scene_static_set_draw_order(struct Pix3DGL* pix3dgl, int* scene_model_indices, int count);

// Batch face order updates (MUCH faster than individual updates)
void pix3dgl_scene_static_begin_face_order_batch(struct Pix3DGL* pix3dgl);
void pix3dgl_scene_static_end_face_order_batch(struct Pix3DGL* pix3dgl);

void pix3dgl_scene_static_set_model_face_order(
    struct Pix3DGL* pix3dgl, int scene_model_idx, int* face_indices, int face_count);
void pix3dgl_scene_static_set_tile_face_order(
    struct Pix3DGL* pix3dgl, int scene_tile_idx, int* face_indices, int face_count);
void pix3dgl_scene_static_set_tile_draw_order(
    struct Pix3DGL* pix3dgl, int* scene_tile_indices, int count);
void pix3dgl_scene_static_set_unified_draw_order(
    struct Pix3DGL* pix3dgl, bool* is_tile_array, int* index_array, int count, int stride);
void pix3dgl_scene_static_draw(struct Pix3DGL* pix3dgl);

// Get current animation frame for a model (returns -1 if not animated)
int pix3dgl_scene_static_get_model_animation_frame(struct Pix3DGL* pix3dgl, int scene_model_idx);

void pix3dgl_set_animation_clock(struct Pix3DGL* pix3dgl, float clock_value);

void pix3dgl_cleanup(struct Pix3DGL* pix3dgl);

#ifdef __cplusplus
}
#endif

#endif // OSRS_PIX3D_H