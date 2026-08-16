#ifndef TRSPK_WEBGL1_H
#define TRSPK_WEBGL1_H

/*
 * WebGL1 backend — GLES2 with no extensions at all.
 *
 * "No extensions" is the constraint the whole backend is shaped around, so it
 * is worth naming what it costs. Everything below is unavailable, and the
 * renderer is written not to want it:
 *
 *   OES_vertex_array_object   no VAOs; attribute state is set per draw
 *   OES_element_index_uint    16-bit indices only, hence webgl1_index16.h
 *   ANGLE_instanced_arrays    no instancing; nothing here instances
 *   EXT_frag_depth            no gl_FragDepth; depth comes from the vertex stage
 *   OES_standard_derivatives  no dFdx/dFdy in the fragment shaders
 *   EXT_shader_texture_lod    no explicit-LOD sampling
 *   WEBGL_depth_texture       depth is a renderbuffer, never sampled
 *   OES_texture_float         all textures are 8-bit unorm
 *
 * And the parts of core GLES2 that differ from desktop GL3, which the renderer
 * must respect rather than work around:
 *
 *   - Texture internal format must equal the format: no GL_RGBA8, no GL_R8.
 *     Single-channel data uploads as GL_LUMINANCE (the shader still reads .r).
 *   - No GL_BGRA anywhere, including glReadPixels.
 *   - No uniform blocks: the world matrices are plain uniforms.
 *   - Attribute locations must be bound before linking; GLSL ES 1.00 has no
 *     layout(location).
 *   - Non-power-of-two textures need CLAMP_TO_EDGE and no mipmaps. Every
 *     texture here is either POT or already clamped and unmipped.
 */

// clang-format off
#include "../core/trspk_core.h"
// clang-format on

#if defined(__EMSCRIPTEN__)
#include <GLES2/gl2.h>
#else
/* Native GLES2 (desktop debugging against an ES2 driver or an emulator). */
#include <GLES2/gl2.h>
#endif

#include "webgl1_index16.h"
#include "webgl1_shaders.h"
#include "webgl1_vertex.h"

#include <stdint.h>

#endif
