#ifndef OSRS_PIX3D_H
#define OSRS_PIX3D_H

#ifdef __cplusplus
extern "C" {
#endif

struct Pix3DGL;

struct Pix3DGL* pix3dgl_new();
void pix3dgl_model_load_textured_pnm(
    struct Pix3DGL* pix3dgl,
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
void pix3dgl_cleanup(struct Pix3DGL* pix3dgl);

#ifdef __cplusplus
}
#endif

#endif // OSRS_PIX3D_H