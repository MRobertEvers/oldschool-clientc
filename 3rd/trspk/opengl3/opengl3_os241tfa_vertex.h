#ifndef OPENGL3_OS241TFA_VERTEX_H
#define OPENGL3_OS241TFA_VERTEX_H

#include "opengl3_os1_vertex.h"

#include <stddef.h>

/*
 * The OpenGL (Post-TFA OSRS) renderer's vertex (TRSPK_VERTEX_FORMAT_OPENGL3_OS241TFA):
 * the os1 vertex, then the face's texture projector in world space -- P less
 * this corner's position, and the edges U = M - P, V = N - P
 * (trspk_toridraw_face_projector) -- for the client's eye-ray texel lookup
 * (shaders/vertex_os241tfa.glsl). All zero on a face with none. Made only by
 * the Post-TFA renderer's handle; every other GL3 renderer keeps the os1 vertex.
 */
struct TRSPK_VertexOpenGl3Os241Tfa
{
    float position[4];
    float color[4];
    float texcoord[2];
    float tex_id;
    float uv_mode;
    float projector_p[3];
    float projector_u[3];
    float projector_v[3];
};

_Static_assert(
    offsetof(struct TRSPK_VertexOpenGl3Os241Tfa, uv_mode) ==
        offsetof(struct TRSPK_VertexOpenGl3Os1, uv_mode),
    "the os241tfa GL3 vertex starts with the os1 one");

#endif
