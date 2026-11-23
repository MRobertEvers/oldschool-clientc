#ifndef TRSPK_VERTEX_H
#define TRSPK_VERTEX_H

enum TRSPK_VertexFormat
{
    TRSPK_VERTEX_FORMAT_TRSPK,
    TRSPK_VERTEX_FORMAT_WEBGL1,
    TRSPK_VERTEX_FORMAT_OPENGL3_OS1,
    TRSPK_VERTEX_FORMAT_D3D9,
    TRSPK_VERTEX_FORMAT_GLES2_OS1,
    /** The WebGL 2 (Sailing OSRS) renderer's: GLES2 plus the face depth bias
     *  (es3/es3_os235sailing_vertex.h). */
    TRSPK_VERTEX_FORMAT_ES3_OS235SAILING,
    /** The WebGL 2 (Post-TFA OSRS) renderer's: Sailing plus the texture
     *  projector (es3/es3_os241tfa_vertex.h). */
    TRSPK_VERTEX_FORMAT_ES3_OS241TFA,
    /** The OpenGL (Post-TFA OSRS) renderer's: OPENGL3 plus the texture
     *  projector (opengl3/opengl3_os241tfa_vertex.h). OpenGL (Sailing OSRS) uses
     *  OPENGL3, whose position.w already holds the depth bias. */
    TRSPK_VERTEX_FORMAT_OPENGL3_OS241TFA,
};

#endif