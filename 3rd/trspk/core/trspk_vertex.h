#ifndef TRSPK_VERTEX_H
#define TRSPK_VERTEX_H

enum TRSPK_VertexFormat
{
    TRSPK_VERTEX_FORMAT_TRSPK,
    TRSPK_VERTEX_FORMAT_WEBGL1,
    TRSPK_VERTEX_FORMAT_OPENGL3,
    TRSPK_VERTEX_FORMAT_D3D9,
    TRSPK_VERTEX_FORMAT_GLES2,
    /** The WebGL2 (OSRS) renderer's: GLES2 plus the face depth bias
     *  (es3/es3_osrs_vertex.h). */
    TRSPK_VERTEX_FORMAT_ES3_OSRS,
};

#endif