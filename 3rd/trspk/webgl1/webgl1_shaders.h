#ifndef TRSPK_WEBGL1_SHADERS_H
#define TRSPK_WEBGL1_SHADERS_H

/*
 * GLSL ES 1.00 ports of the OpenGL 3 shaders (opengl3_shaders.h,
 * opengl3_2d_shaders.h). Same names, same maths, same results — the language is
 * what changed:
 *
 *   in / out            -> attribute / varying
 *   out vec4 frag_color -> gl_FragColor
 *   texture()           -> texture2D()
 *   layout(std140) UBO  -> plain uniforms (uniform blocks are GL3-only)
 *
 * No extension is used or required. Nothing here asks for a #version
 * directive: absent one, the compiler assumes 100, which is exactly what a
 * WebGL1 context accepts.
 *
 * Precision is not a detail to skip. A fragment shader has no default float
 * precision in ES, so one must be declared, and mediump is only guaranteed to
 * carry integers exactly to 2^10 — while a texture id here runs to twice the
 * atlas slot count (the cutout flag is folded into the high half). highp is
 * requested where the implementation has it, which every WebGL1 fragment stage
 * that reports GL_FRAGMENT_PRECISION_HIGH does; mediump is the fallback and is
 * only reached on hardware that cannot do better.
 */

// clang-format off

#define TRSPK_WEBGL1_FRAG_PRECISION                                                                \
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"                                                          \
    "precision highp float;\n"                                                                     \
    "#else\n"                                                                                      \
    "precision mediump float;\n"                                                                   \
    "#endif\n"

static char const* const trspk_webgl1_vertex_shader =
    "uniform mat4 u_modelViewMatrix;\n"
    "uniform mat4 u_projectionMatrix;\n"
    "\n"
    "attribute vec4 a_position;\n"
    "attribute vec4 a_color;\n"
    "attribute vec2 a_texcoord;\n"
    "attribute float a_tex_id;\n"
    "attribute float a_uv_mode;\n"
    "\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "varying float v_tex_id;\n"
    "varying float v_uv_pack;\n"
    "\n"
    "void main() {\n"
    "    vec4 wp = vec4(a_position.xyz, 1.0);\n"
    "    gl_Position = u_projectionMatrix * u_modelViewMatrix * wp;\n"
    "    v_color = a_color;\n"
    "    v_texcoord = a_texcoord;\n"
    "    v_tex_id = a_tex_id;\n"
    "    v_uv_pack = a_uv_mode;\n"
    "}\n";

static char const* const trspk_webgl1_fragment_shader =
    TRSPK_WEBGL1_FRAG_PRECISION
    "\n"
    "uniform float uClock;\n"
    "uniform float uAtlasDim;\n"
    "uniform float uAtlasSlots;\n"
    "\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "varying float v_tex_id;\n"
    "varying float v_uv_pack;\n"
    "\n"
    "uniform sampler2D s_atlas;\n"
    "\n"
    "void main() {\n"
    "    int slots = int(floor(uAtlasSlots + 0.5));\n"
    "    int raw = int(floor(v_tex_id + 0.5));\n"
    "    int atlas_id;\n"
    "    bool cutout;\n"
    "    if (raw >= slots) {\n"
    "        atlas_id = raw - slots;\n"
    "        cutout = true;\n"
    "    } else {\n"
    "        atlas_id = raw;\n"
    "        cutout = false;\n"
    "    }\n"
    "    if (atlas_id < 0 || atlas_id >= slots) {\n"
    "        gl_FragColor = vec4(v_color.rgb, v_color.a);\n"
    "        return;\n"
    "    }\n"
    "    int enc = int(floor(v_uv_pack * 0.5 + 0.5));\n"
    "    float anim_u = 0.0;\n"
    "    float anim_v = 0.0;\n"
    "    if (enc > 0) {\n"
    "        if (enc < 257) {\n"
    "            int mag_u = enc - 1;\n"
    "            anim_u = float(mag_u) / 256.0;\n"
    "        } else {\n"
    "            int mag_v = enc - 257;\n"
    "            anim_v = float(mag_v) / 256.0;\n"
    "        }\n"
    "    }\n"
    "    const float TEX_DIM = 128.0;\n"
    "    float atlas_dim = uAtlasDim;\n"
    "    float cols = atlas_dim / TEX_DIM;\n"
    "    float du = TEX_DIM / atlas_dim;\n"
    "    float dv = du;\n"
    "    float col = mod(float(atlas_id), cols);\n"
    "    float row = floor(float(atlas_id) / cols);\n"
    "    vec4 ta = vec4(col * du, row * dv, du, dv);\n"
    "    vec2 local = v_texcoord;\n"
    "    float clk = uClock;\n"
    "    if (anim_u > 0.0) local.x += clk * anim_u;\n"
    "    if (anim_v > 0.0) local.y -= clk * anim_v;\n"
    "    local.x = clamp(local.x, 0.008, 0.992);\n"
    "    local.y = clamp(fract(local.y), 0.008, 0.992);\n"
    "    vec2 uv_atlas = ta.xy + local * ta.zw;\n"
    "    vec4 texColor = texture2D(s_atlas, uv_atlas);\n"
    "    vec3 finalColor = mix(v_color.rgb, texColor.rgb * v_color.rgb, 1.0);\n"
    "    float finalAlpha = v_color.a;\n"
    "    if (cutout) {\n"
    "        if (texColor.a < 0.5) discard;\n"
    "    }\n"
    "    gl_FragColor = vec4(finalColor, finalAlpha);\n"
    "}\n";

/*
 * The atlas window a quad may sample travels in the VERTEX, not in a uniform.
 *
 * It used to be `uniform vec4 u_uv_bounds`, which made it batch state: every
 * sprite has its own tile, so every sprite ended the batch and the UI went out
 * as one draw per sprite -- 53 flushes and ~850 GL calls a frame on the phone.
 * D3D9's UI batcher has no per-sprite state at all and draws the whole
 * interface in a handful of calls; this is how the same batching is reached
 * while keeping the clamp WebGL1 needs because it filters. A quad that wants
 * no clamp passes a window wider than any texture coordinate.
 */
static char const* const trspk_webgl1_2d_vertex_shader =
    "attribute vec2 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "attribute vec4 a_color;\n"
    "attribute vec4 a_uv_bounds;\n"
    "uniform mat4 u_projection;\n"
    "varying vec2 v_texcoord;\n"
    "varying vec4 v_color;\n"
    "varying vec4 v_uv_bounds;\n"
    "void main() {\n"
    "    v_texcoord = a_texcoord;\n"
    "    v_color = a_color;\n"
    "    v_uv_bounds = a_uv_bounds;\n"
    "    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);\n"
    "}\n";

static char const* const trspk_webgl1_2d_fragment_shader =
    TRSPK_WEBGL1_FRAG_PRECISION
    "varying vec2 v_texcoord;\n"
    "varying vec4 v_color;\n"
    "varying vec4 v_uv_bounds;\n"
    "uniform sampler2D u_texture;\n"
    "uniform int u_text_mode;\n"
    "void main() {\n"
    "    if (v_texcoord.x < v_uv_bounds.x || v_texcoord.x > v_uv_bounds.z ||\n"
    "        v_texcoord.y < v_uv_bounds.y || v_texcoord.y > v_uv_bounds.w)\n"
    "        discard;\n"
    "    vec4 tex = texture2D(u_texture, v_texcoord);\n"
    /* The font atlas is single-channel. GLES2 has no GL_RED/GL_R8, so it is
     * uploaded as GL_LUMINANCE, which replicates the one channel into rgb —
     * .r still reads the coverage, exactly as it did on the GL3 path. */
    "    vec4 c = (u_text_mode == 1)\n"
    "        ? vec4(v_color.rgb, step(0.001, tex.r) * v_color.a)\n"
    "        : tex * v_color;\n"
    "    if (c.a < 0.004) discard;\n"
    "    gl_FragColor = c;\n"
    "}\n";

// clang-format on
#endif
