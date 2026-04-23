#include "platforms/webgl1/webgl1_internal.h"

#include "graphics/uv_pnm.h"

bool
wg1_fill_model_face_vertices_model_local(
    const struct DashModel* model,
    int f,
    int raw_tex,
    WgVertex out[3])
{
    const int* face_infos = dashmodel_face_infos_const(model);
    if( face_infos && face_infos[f] == 2 )
        return false;
    const hsl16_t* hsl_c_arr = dashmodel_face_colors_c_const(model);
    if( hsl_c_arr && hsl_c_arr[f] == DASHHSL16_HIDDEN )
        return false;

    const faceint_t* face_ia = dashmodel_face_indices_a_const(model);
    const faceint_t* face_ib = dashmodel_face_indices_b_const(model);
    const faceint_t* face_ic = dashmodel_face_indices_c_const(model);
    const int ia = face_ia[f];
    const int ib = face_ib[f];
    const int ic = face_ic[f];
    const int vcount = dashmodel_vertex_count(model);
    if( ia < 0 || ia >= vcount || ib < 0 || ib >= vcount || ic < 0 || ic >= vcount )
        return false;

    const hsl16_t* hsl_a_arr = dashmodel_face_colors_a_const(model);
    const hsl16_t* hsl_b_arr = dashmodel_face_colors_b_const(model);
    if( !hsl_a_arr || !hsl_b_arr || !hsl_c_arr )
        return false;

    int hsl_a = (int)hsl_a_arr[f];
    int hsl_b = (int)hsl_b_arr[f];
    int hsl_c = (int)hsl_c_arr[f];
    int rgb_a, rgb_b, rgb_c;
    if( hsl_c == DASHHSL16_FLAT )
        rgb_a = rgb_b = rgb_c = g_hsl16_to_rgb_table[hsl_a & 65535];
    else
    {
        rgb_a = g_hsl16_to_rgb_table[hsl_a & 65535];
        rgb_b = g_hsl16_to_rgb_table[hsl_b & 65535];
        rgb_c = g_hsl16_to_rgb_table[hsl_c & 65535];
    }

    float face_alpha = 1.0f;
    const alphaint_t* face_alphas = dashmodel_face_alphas_const(model);
    if( face_alphas )
        face_alpha = (float)(0xFF - face_alphas[f]) / 255.0f;

    float u_corner[3] = { 0.0f, 0.0f, 0.0f };
    float v_corner[3] = { 0.0f, 0.0f, 0.0f };

    if( raw_tex >= 0 )
    {
        int texture_face_idx = f;
        int tp = 0, tm = 0, tn = 0;
        const faceint_t* ftc = dashmodel_face_texture_coords_const(model);
        const faceint_t* tcp = dashmodel_textured_p_coordinate_const(model);
        const faceint_t* tcm = dashmodel_textured_m_coordinate_const(model);
        const faceint_t* tcn = dashmodel_textured_n_coordinate_const(model);
        if( dashmodel__is_ground_va(model) )
        {
            tp = (int)face_ia[0];
            tm = (int)face_ib[0];
            tn = (int)face_ic[0];
            if( tp < 0 || tp >= vcount || tm < 0 || tm >= vcount || tn < 0 || tn >= vcount )
                return false;
        }
        else if( ftc && ftc[f] != -1 && tcp && tcm && tcn )
        {
            texture_face_idx = ftc[f];
            tp = tcp[texture_face_idx];
            tm = tcm[texture_face_idx];
            tn = tcn[texture_face_idx];
        }
        else
        {
            tp = face_ia[f];
            tm = face_ib[f];
            tn = face_ic[f];
        }

        const vertexint_t* vtx = dashmodel_vertices_x_const(model);
        const vertexint_t* vty = dashmodel_vertices_y_const(model);
        const vertexint_t* vtz = dashmodel_vertices_z_const(model);
        struct UVFaceCoords uv;
        uv_pnm_compute(
            &uv,
            (float)vtx[tp],
            (float)vty[tp],
            (float)vtz[tp],
            (float)vtx[tm],
            (float)vty[tm],
            (float)vtz[tm],
            (float)vtx[tn],
            (float)vty[tn],
            (float)vtz[tn],
            (float)vtx[ia],
            (float)vty[ia],
            (float)vtz[ia],
            (float)vtx[ib],
            (float)vty[ib],
            (float)vtz[ib],
            (float)vtx[ic],
            (float)vty[ic],
            (float)vtz[ic]);
        u_corner[0] = uv.u1;
        u_corner[1] = uv.u2;
        u_corner[2] = uv.u3;
        v_corner[0] = uv.v1;
        v_corner[1] = uv.v2;
        v_corner[2] = uv.v3;
    }

    const uint16_t tex_slot =
        (raw_tex >= 0 && raw_tex < 256) ? (uint16_t)raw_tex : (uint16_t)0xFFFFu;
    const int verts[3] = { ia, ib, ic };
    const int rgbs[3] = { rgb_a, rgb_b, rgb_c };
    const vertexint_t* vx = dashmodel_vertices_x_const(model);
    const vertexint_t* vy = dashmodel_vertices_y_const(model);
    const vertexint_t* vz = dashmodel_vertices_z_const(model);
    for( int vi = 0; vi < 3; ++vi )
    {
        const int vi_idx = verts[vi];
        WgVertex mv;
        mv.position[0] = (float)vx[vi_idx];
        mv.position[1] = (float)vy[vi_idx];
        mv.position[2] = (float)vz[vi_idx];
        mv.position[3] = 1.0f;
        int rgb = rgbs[vi];
        mv.color[0] = ((rgb >> 16) & 0xFF) / 255.0f;
        mv.color[1] = ((rgb >> 8) & 0xFF) / 255.0f;
        mv.color[2] = (rgb & 0xFF) / 255.0f;
        mv.color[3] = face_alpha;
        mv.texcoord[0] = u_corner[vi];
        mv.texcoord[1] = v_corner[vi];
        mv.tex_id = tex_slot;
        mv.uv_mode = kWgVertexUvMode_Standard;
        out[vi] = mv;
    }
    return true;
}

void
wg1_vertex_fill_invisible(WgVertex* v)
{
    memset(v, 0, sizeof(WgVertex));
    v->position[3] = 1.0f;
    v->color[3] = 0.0f;
    v->tex_id = 0xFFFFu;
}

bool
wg1_fill_face_corner_vertices_from_fa(
    const struct DashVertexArray* va,
    const struct DashFaceArray* fa,
    int f,
    int raw_tex,
    const int* pnm_ref_face_per_face,
    WgVertex out[3])
{
    if( !va || !fa || f < 0 || f >= fa->count )
        return false;

    const faceint_t* face_ia = fa->indices_a;
    const faceint_t* face_ib = fa->indices_b;
    const faceint_t* face_ic = fa->indices_c;
    const int ia = (int)face_ia[f];
    const int ib = (int)face_ib[f];
    const int ic = (int)face_ic[f];
    const int vcount = va->vertex_count;
    if( ia < 0 || ia >= vcount || ib < 0 || ib >= vcount || ic < 0 || ic >= vcount )
        return false;

    const hsl16_t* hsl_a_arr = fa->colors_a;
    const hsl16_t* hsl_b_arr = fa->colors_b;
    const hsl16_t* hsl_c_arr = fa->colors_c;
    if( !hsl_a_arr || !hsl_b_arr || !hsl_c_arr )
        return false;
    if( hsl_c_arr[f] == DASHHSL16_HIDDEN )
        return false;

    int hsl_a = (int)hsl_a_arr[f];
    int hsl_b = (int)hsl_b_arr[f];
    int hsl_c = (int)hsl_c_arr[f];
    int rgb_a, rgb_b, rgb_c;
    if( hsl_c == DASHHSL16_FLAT )
        rgb_a = rgb_b = rgb_c = g_hsl16_to_rgb_table[hsl_a & 65535];
    else
    {
        rgb_a = g_hsl16_to_rgb_table[hsl_a & 65535];
        rgb_b = g_hsl16_to_rgb_table[hsl_b & 65535];
        rgb_c = g_hsl16_to_rgb_table[hsl_c & 65535];
    }

    const float face_alpha = 1.0f;

    float u_corner[3] = { 0.0f, 0.0f, 0.0f };
    float v_corner[3] = { 0.0f, 0.0f, 0.0f };

    if( raw_tex >= 0 && fa->count > 0 )
    {
        int pnm_f = 0;
        if( pnm_ref_face_per_face && f >= 0 && f < fa->count )
        {
            pnm_f = pnm_ref_face_per_face[f];
            if( pnm_f < 0 || pnm_f >= fa->count )
                pnm_f = 0;
        }
        const int tp = (int)fa->indices_a[pnm_f];
        const int tm = (int)fa->indices_b[pnm_f];
        const int tn = (int)fa->indices_c[pnm_f];
        if( tp < 0 || tp >= vcount || tm < 0 || tm >= vcount || tn < 0 || tn >= vcount )
            return false;

        const vertexint_t* vx = va->vertices_x;
        const vertexint_t* vy = va->vertices_y;
        const vertexint_t* vz = va->vertices_z;
        struct UVFaceCoords uv;
        uv_pnm_compute(
            &uv,
            (float)vx[tp],
            (float)vy[tp],
            (float)vz[tp],
            (float)vx[tm],
            (float)vy[tm],
            (float)vz[tm],
            (float)vx[tn],
            (float)vy[tn],
            (float)vz[tn],
            (float)vx[ia],
            (float)vy[ia],
            (float)vz[ia],
            (float)vx[ib],
            (float)vy[ib],
            (float)vz[ib],
            (float)vx[ic],
            (float)vy[ic],
            (float)vz[ic]);
        u_corner[0] = uv.u1;
        u_corner[1] = uv.u2;
        u_corner[2] = uv.u3;
        v_corner[0] = uv.v1;
        v_corner[1] = uv.v2;
        v_corner[2] = uv.v3;
    }

    const uint16_t tex_slot =
        (raw_tex >= 0 && raw_tex < 256) ? (uint16_t)raw_tex : (uint16_t)0xFFFFu;
    const int verts[3] = { ia, ib, ic };
    const int rgbs[3] = { rgb_a, rgb_b, rgb_c };
    const vertexint_t* vx = va->vertices_x;
    const vertexint_t* vy = va->vertices_y;
    const vertexint_t* vz = va->vertices_z;
    for( int vi = 0; vi < 3; ++vi )
    {
        const int vi_idx = verts[vi];
        WgVertex mv;
        mv.position[0] = (float)vx[vi_idx];
        mv.position[1] = (float)vy[vi_idx];
        mv.position[2] = (float)vz[vi_idx];
        mv.position[3] = 1.0f;
        int rgb = rgbs[vi];
        mv.color[0] = ((rgb >> 16) & 0xFF) / 255.0f;
        mv.color[1] = ((rgb >> 8) & 0xFF) / 255.0f;
        mv.color[2] = (rgb & 0xFF) / 255.0f;
        mv.color[3] = face_alpha;
        mv.texcoord[0] = u_corner[vi];
        mv.texcoord[1] = v_corner[vi];
        mv.tex_id = tex_slot;
        mv.uv_mode = kWgVertexUvMode_VaFractTile;
        out[vi] = mv;
    }
    return true;
}
