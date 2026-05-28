#ifndef OPENGL3_VERTEX_H
#define OPENGL3_VERTEX_H

struct TRSPK_VertexOpenGl3
{
    float position[4];
    float color[4];
    float texcoord[2];
    float tex_id;
    float uv_mode;
};

#endif