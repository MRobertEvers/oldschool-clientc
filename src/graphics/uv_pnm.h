#ifndef OSRS_UV_PNM_H
#define OSRS_UV_PNM_H

struct UVFaceCoords
{
    float u1;
    float v1;
    float u2;
    float v2;
    float u3;
    float v3;
};

static inline void
uv_pnm_compute(
    struct UVFaceCoords* uv_pnm,
    float p_x,
    float p_y,
    float p_z,
    float m_x,
    float m_y,
    float m_z,
    float n_x,
    float n_y,
    float n_z,
    float a_x,
    float a_y,
    float a_z,
    float b_x,
    float b_y,
    float b_z,
    float c_x,
    float c_y,
    float c_z)
{
    float u1 = 0.0f;
    float v1 = 0.0f;
    float u2 = 0.0f;
    float v2 = 0.0f;
    float u3 = 0.0f;
    float v3 = 0.0f;

    // Get vertex positions for UV coordinate computation
    // float p_x = g_scene_model->model->vertices_x[tp_vertex];
    // float p_y = g_scene_model->model->vertices_y[tp_vertex];
    // float p_z = g_scene_model->model->vertices_z[tp_vertex];

    // float m_x = g_scene_model->model->vertices_x[tm_vertex];
    // float m_y = g_scene_model->model->vertices_y[tm_vertex];
    // float m_z = g_scene_model->model->vertices_z[tm_vertex];

    // float n_x = g_scene_model->model->vertices_x[tn_vertex];
    // float n_y = g_scene_model->model->vertices_y[tn_vertex];
    // float n_z = g_scene_model->model->vertices_z[tn_vertex];

    // Compute vectors for texture space
    float vU_x = m_x - p_x; // M vector (U direction)
    float vU_y = m_y - p_y;
    float vU_z = m_z - p_z;

    float vV_x = n_x - p_x; // N vector (V direction)
    float vV_y = n_y - p_y;
    float vV_z = n_z - p_z;

    int vUVPlane_normal_xhat = vU_y * vV_z - vU_z * vV_y;
    int vUVPlane_normal_yhat = vU_z * vV_x - vU_x * vV_z;
    int vUVPlane_normal_zhat = vU_x * vV_y - vU_y * vV_x;

    // Compute the positions of A, B, C relative to P.
    int dxa = a_x - p_x;
    int dya = a_y - p_y;
    int dza = a_z - p_z;
    int dxb = b_x - p_x;
    int dyb = b_y - p_y;
    int dzb = b_z - p_z;
    int dxc = c_x - p_x;
    int dyc = c_y - p_y;
    int dzc = c_z - p_z;

    // The derivation is the same, this is the coefficient on U given by cramer's rule
    float U_xhat = vV_y * vUVPlane_normal_zhat - vV_z * vUVPlane_normal_yhat;
    float U_yhat = vV_z * vUVPlane_normal_xhat - vV_x * vUVPlane_normal_zhat;
    float U_zhat = vV_x * vUVPlane_normal_yhat - vV_y * vUVPlane_normal_xhat;
    float U_inv_w = 1.0 / (U_xhat * vU_x + U_yhat * vU_y + U_zhat * vU_z);

    // dot product of A and U
    u1 = (U_xhat * dxa + U_yhat * dya + U_zhat * dza) * U_inv_w;
    u2 = (U_xhat * dxb + U_yhat * dyb + U_zhat * dzb) * U_inv_w;
    u3 = (U_xhat * dxc + U_yhat * dyc + U_zhat * dzc) * U_inv_w;

    // The derivation is the same, this is the coefficient on V given by cramer's rule
    float V_xhat = vU_y * vUVPlane_normal_zhat - vU_z * vUVPlane_normal_yhat;
    float V_yhat = vU_z * vUVPlane_normal_xhat - vU_x * vUVPlane_normal_zhat;
    float V_zhat = vU_x * vUVPlane_normal_yhat - vU_y * vUVPlane_normal_xhat;
    float V_inv_w = 1.0 / (V_xhat * vV_x + V_yhat * vV_y + V_zhat * vV_z);

    v1 = (V_xhat * dxa + V_yhat * dya + V_zhat * dza) * V_inv_w;
    v2 = (V_xhat * dxb + V_yhat * dyb + V_zhat * dzb) * V_inv_w;
    v3 = (V_xhat * dxc + V_yhat * dyc + V_zhat * dzc) * V_inv_w;

    uv_pnm->u1 = u1;
    uv_pnm->v1 = v1;
    uv_pnm->u2 = u2;
    uv_pnm->v2 = v2;
    uv_pnm->u3 = u3;
    uv_pnm->v3 = v3;
}

#endif