#ifndef OSRS_PIX3D_H
#define OSRS_PIX3D_H

#ifdef __cplusplus
extern "C" {
#endif

struct Pix3DGL;
struct TexturesCache;
struct Cache;

struct Pix3DGL* pix3dgl_new();
void pix3dgl_load_texture(
    struct Pix3DGL* pix3dgl,
    int texture_id,
    struct TexturesCache* textures_cache,
    struct Cache* cache);
void pix3dgl_model_load_textured_pnm(
    struct Pix3DGL* pix3dgl,
    int idx,
    int* vertices_x,
    int* vertices_y,
    int* vertices_z,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int face_count,
    int* face_infos,
    int* face_alphas,
    int* face_textures,
    int* face_texture_faces_nullable,
    int* face_p_coordinate_nullable,
    int* face_m_coordinate_nullable,
    int* face_n_coordinate_nullable,
    int* face_colors_a,
    int* face_colors_b,
    int* face_colors_c);
void pix3dgl_model_load(
    struct Pix3DGL* pix3dgl,
    int idx,
    int* vertices_x,
    int* vertices_y,
    int* vertices_z,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int face_count,
    int* face_alphas,
    int* face_colors_a,
    int* face_colors_b,
    int* face_colors_c);
void pix3dgl_render(struct Pix3DGL* pix3dgl);
void pix3dgl_render_with_camera(
    struct Pix3DGL* pix3dgl,
    float camera_x,
    float camera_y,
    float camera_z,
    float camera_pitch,
    float camera_yaw,
    float screen_width,
    float screen_height);
void pix3dgl_model_draw(
    struct Pix3DGL* pix3dgl,
    int model_idx,
    float position_x,
    float position_y,
    float position_z,
    float yaw);
void pix3dgl_model_draw_face(
    struct Pix3DGL* pix3dgl,
    int model_idx,
    int face_idx,
    float position_x,
    float position_y,
    float position_z,
    float yaw);
void pix3dgl_model_begin_draw(
    struct Pix3DGL* pix3dgl,
    int model_idx,
    float position_x,
    float position_y,
    float position_z,
    float yaw);
void pix3dgl_model_draw_face_fast(struct Pix3DGL* pix3dgl, int face_idx);
void pix3dgl_model_end_draw(struct Pix3DGL* pix3dgl);
void pix3dgl_tile_load(
    struct Pix3DGL* pix3dgl,
    int idx,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int vertex_count,
    int* faces_a,
    int* faces_b,
    int* faces_c,
    int face_count,
    int* valid_faces,
    int* face_texture_ids,
    int* face_texture_u_a,
    int* face_texture_v_a,
    int* face_texture_u_b,
    int* face_texture_v_b,
    int* face_texture_u_c,
    int* face_texture_v_c,
    int* face_color_hsl_a,
    int* face_color_hsl_b,
    int* face_color_hsl_c);
void pix3dgl_tile_draw(struct Pix3DGL* pix3dgl, int tile_idx);
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
void pix3dgl_cleanup(struct Pix3DGL* pix3dgl);

#ifdef __cplusplus
}
#endif

#endif // OSRS_PIX3D_H