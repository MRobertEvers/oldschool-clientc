#pragma once

#include "pass_3d_builder2.h"
#include "platforms/common/gpu_3d.h"

#ifdef __EMSCRIPTEN__

#    include <GLES2/gl2.h>
#    include <cstddef>
#    include <cstdint>

struct Platform2_SDL2_Renderer_WebGL1;

/** Uniform / attribute locations for world 3D program (stride 48 mesh, stride 32 instance). */
struct WebGL1WorldShaderLocs
{
    GLint a_position = -1;
    GLint a_color = -1;
    GLint a_texcoord = -1;
    GLint a_tex_id = -1;
    GLint a_uv_mode = -1;
    GLint a_inst0 = -1; ///< vec4: cos_yaw, sin_yaw, x, y
    GLint a_inst1 = -1; ///< vec4: z, angle_encoding, 0, 0
    GLint u_modelViewMatrix = -1;
    GLint u_projectionMatrix = -1;
    GLint u_clock = -1;
    GLint s_atlas = -1;
    /** `glUniform4fv(loc, 256, ...)` packed `WebGL1AtlasTile`: u0,v0,du,dv per slot. */
    GLint u_tileA = -1;
    /** anim_u, anim_v, opaque, pad per slot. */
    GLint u_tileB = -1;
};

void
Pass3DBuilder2SubmitWebGL1(
    Pass3DBuilder2<GPU3DTransformUniformMetal>& builder,
    const GPU3DCache2<GPU3DMeshVertexWebGL1>& cache,
    struct Platform2_SDL2_Renderer_WebGL1* webgl_renderer,
    GLuint program,
    const WebGL1WorldShaderLocs& locs,
    GLuint dynamic_instance_buffer,
    GLuint dynamic_index_buffer,
    size_t instance_base_bytes,
    size_t index_base_bytes,
    GLuint fragment_atlas_texture,
    const float modelViewMatrix[16],
    const float projectionMatrix[16],
    float u_clock);

#endif
