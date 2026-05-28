#ifndef WEBGL1_VERTEX_H
#define WEBGL1_VERTEX_H

#define TRSPK_VERTEX_WEBGL1_TEXID_INVALID (-1.0f)

struct TRSPK_VertexWebGL1
{
    float position[4];
    float color[4];
    float texcoord[2];
    float tex_id;
    float uv_mode;
};

#endif