#include "lighting.h"

#include <assert.h>
#include <math.h>

void
calculate_vertex_normals(
    struct VertexNormal* vertex_normals,
    int vertex_count,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int num_faces)
{
    for( int i = 0; i < num_faces; i++ )
    {
        int a = face_indices_a[i];
        int b = face_indices_b[i];
        int c = face_indices_c[i];
        assert(a < vertex_count);
        assert(b < vertex_count);
        assert(c < vertex_count);

        int xa = vertex_x[a];
        int xb = vertex_x[b];
        int xc = vertex_x[c];

        int ya = vertex_y[a];
        int yb = vertex_y[b];
        int yc = vertex_y[c];

        int za = vertex_z[a];
        int zb = vertex_z[b];
        int zc = vertex_z[c];

        int dx_ab = xb - xa;
        int dy_ab = yb - ya;
        int dz_ab = zb - za;

        int dx_ac = xc - xa;
        int dy_ac = yc - ya;
        int dz_ac = zc - za;

        // Cross Product of vectors AB and AC
        int normal_x = dy_ab * dz_ac - dy_ac * dz_ab;
        int normal_y = dz_ab * dx_ac - dz_ac * dx_ab;
        int normal_z = dx_ab * dy_ac - dx_ac * dy_ab;

        int magnitude = sqrt(normal_x * normal_x + normal_y * normal_y + normal_z * normal_z);
        if( magnitude <= 0 )
            magnitude = 1;
        normal_x <<= 8;
        normal_y <<= 8;
        normal_z <<= 8;
        normal_x /= magnitude;
        normal_y /= magnitude;
        normal_z /= magnitude;

        vertex_normals[a].x += normal_x;
        vertex_normals[a].y += normal_y;
        vertex_normals[a].z += normal_z;
        vertex_normals[a].face_count++;

        vertex_normals[b].x += normal_x;
        vertex_normals[b].y += normal_y;
        vertex_normals[b].z += normal_z;
        vertex_normals[b].face_count++;

        vertex_normals[c].x += normal_x;
        vertex_normals[c].y += normal_y;
        vertex_normals[c].z += normal_z;
        vertex_normals[c].face_count++;
    }
}

static int
lighting_multiply_hsl16(int hsl, int scalar)
{
    // bottom 7 bits are multiplied by scalar.
    // i.e. The lightness is multiplied by the scalar,
    // then divided by 128
    scalar = scalar * (hsl & 0x7f) >> 7;
    if( scalar < 2 )
        scalar = 2;
    else if( scalar > 126 )
        scalar = 126;

    // The scalar is added to the top 9 bits of the hsl value
    // The bottom 7 bits are the lightness.
    return (hsl & 0xff80) + scalar;
}

void
apply_lighting(
    int* face_colors_a_hsl16,
    int* face_colors_b_hsl16,
    int* face_colors_c_hsl16,
    struct VertexNormal* vertex_normals,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int num_faces,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int* face_colors_hsl16, // The flat color.
    int* face_infos,
    int light_ambient,
    int light_attenuation,
    int lightsrc_x,
    int lightsrc_y,
    int lightsrc_z)
{
    int lightness;
    for( int i = 0; i < num_faces; i++ )
    {
        // See modelData.light in rs-map-viewer.
        // Then, hsl = -2 means to skip drawing the face.
        // TODO: How to organize this?
        // For textured models, -2 is used as a "skip face" color.

        // TODO: How to organize this.
        // See here
        // /Users/matthewevers/Documents/git_repos/rs-map-viewer/src/rs/model/ModelData.ts
        // .light

        // and
        // /Users/matthewevers/Documents/git_repos/rs-map-viewer/src/mapviewer/webgl/buffer/SceneBuffer.ts
        // getModelFaces

        // and
        // /Users/matthewevers/Documents/git_repos/OS1/src/main/java/jagex3/dash3d/ModelUnlit.java
        // .draw:1018

        // and
        // OS1
        // 	public final ModelUnlit calculateNormals(int arg0, int arg1, int arg2, int arg3, int
        // arg4) {
        if( face_infos )
        {
            int face_info = face_infos[i];
            int face_type = face_info & 0x3;
            if( face_type == 2 )
            {
                face_colors_c_hsl16[i] = -2;
                continue;
            }
        }

        int color_flat_hsl16 = face_colors_hsl16[i];
        int a = face_indices_a[i];
        int b = face_indices_b[i];
        int c = face_indices_c[i];

        struct VertexNormal* n = &vertex_normals[a];

        // dot product of normal and light vector
        lightness = light_ambient + (lightsrc_x * n->x + lightsrc_y * n->y + lightsrc_z * n->z) /
                                        (light_attenuation * n->face_count);

        face_colors_a_hsl16[i] = lighting_multiply_hsl16(color_flat_hsl16, lightness);

        n = &vertex_normals[b];
        lightness = light_ambient + (lightsrc_x * n->x + lightsrc_y * n->y + lightsrc_z * n->z) /
                                        (light_attenuation * n->face_count);

        face_colors_b_hsl16[i] = lighting_multiply_hsl16(color_flat_hsl16, lightness);

        n = &vertex_normals[c];
        lightness = light_ambient + (lightsrc_x * n->x + lightsrc_y * n->y + lightsrc_z * n->z) /
                                        (light_attenuation * n->face_count);

        face_colors_c_hsl16[i] = lighting_multiply_hsl16(color_flat_hsl16, lightness);
    }
}