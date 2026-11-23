#ifndef OPENGL3_OS1_VERTEX_H
#define OPENGL3_OS1_VERTEX_H

#define TRSPK_VERTEX_OPENGL3_TEXID_INVALID (-1.0f)

/* The desktop GL3 renderer's retained world vertex (TRSPK_VERTEX_FORMAT_OPENGL3_OS1):
 * its painter, depth and Sailing OSRS modes. position.w carries the face's
 * depth bias. */
struct TRSPK_VertexOpenGl3Os1
{
    float position[4];
    float color[4];
    float texcoord[2];
    float tex_id;
    float uv_mode;
};

#endif
