#ifndef SRC_PLATFORM_PLATFORM_RENDERER_GLES2_SHADERS_H
#define SRC_PLATFORM_PLATFORM_RENDERER_GLES2_SHADERS_H

/*
 * GLSL ES 1.00, and deliberately as little of it as possible.
 *
 * ## The world
 *
 * One atlas, bound once per pass. A vertex carries its face's LOCAL texture
 * coordinate and a four-byte word saying which tile and how the texture
 * scrolls; the fragment shader wraps, clamps and maps that into the tile.
 * That is the GL3 renderer's sampling formula (its fragment.glsl), with two
 * differences that are about cost rather than result:
 *
 *   - the tile is resolved on the CPU at bake and arrives as two bytes, so
 *     there is no per-fragment slot division;
 *   - the scroll is added in the VERTEX shader. The offset is uniform across
 *     a face, so adding it per corner is exact, and the fragment shader only
 *     has to wrap the axes that scroll -- which it does with a mix() against
 *     a per-vertex flag rather than a branch.
 *
 * The clamp to [0.008, 0.992] and the unconditional fract() on v are the GL3
 * formula verbatim: that is the parity reference, and the tile margin is what
 * keeps a linear-interpolated coordinate from sampling the neighbouring tile.
 *
 * Two fragment variants, split on `discard`:
 *
 *   plain    no discard at all. Tile-based mobile GPUs keep early depth
 *            rejection only for draws whose shader cannot discard, so the
 *            bulk opaque world pass uses this one.
 *   cutout   discards fragments whose alpha is below 1/255 -- the alpha test
 *            D3D9 runs (ALPHAREF 1, GREATEREQUAL). Cutout textures, the
 *            blended pass, the painter path and every UI draw use it.
 *
 * ## The UI
 *
 * The 2D vertex carries (x, y, w): w is 1 for every ordinary quad and the
 * view depth for a widget-model triangle. Multiplying x and y back up by w
 * makes the hardware's perspective divide land the vertex at (x, y) while its
 * varyings interpolate with 1/w -- D3D9's XYZRHW vertex, and what keeps a
 * chathead's texture from swimming. UI textures are sampled at their final
 * coordinates; nothing in the UI scrolls.
 *
 * Attribute locations are fixed (GLES2_ATTRIB_*) and identical across the
 * programs, so switching program never means re-enabling arrays.
 */

/* clang-format off */

/* 128 / 2048: one tile's extent in atlas coordinates. Spelled once here and
 * checked against the atlas constants in the core. */
#define GLES2_SHADER_ATLAS_CELL "0.0625"

#define GLES2_FRAGMENT_PRECISION_PREAMBLE \
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n" \
    "precision highp float;\n" \
    "#else\n" \
    "precision mediump float;\n" \
    "#endif\n"

static const char* const gles2_world_vertex_shader =
    "attribute vec3 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "attribute vec4 a_color;\n"
    "attribute vec4 a_texinfo;\n"
    "uniform mat4 u_matrix;\n"
    "uniform float u_clock;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "varying vec2 v_tile;\n"
    "varying vec2 v_wrap;\n"
    "void main() {\n"
    "    gl_Position = u_matrix * vec4(a_position, 1.0);\n"
    "    v_color = a_color;\n"
    "    vec2 anim = (a_texinfo.zw - 128.0) * (1.0 / 128.0);\n"
    "    v_texcoord = a_texcoord + u_clock * anim;\n"
    "    v_wrap = step(0.001, abs(anim));\n"
    "    v_tile = a_texinfo.xy * " GLES2_SHADER_ATLAS_CELL ";\n"
    "}\n";

#define GLES2_WORLD_FRAGMENT_SAMPLE \
    "    vec2 local = mix(v_texcoord, fract(v_texcoord), v_wrap);\n" \
    "    local.x = clamp(local.x, 0.008, 0.992);\n" \
    "    local.y = clamp(fract(local.y), 0.008, 0.992);\n" \
    "    vec4 c = v_color * texture2D(s_texture, v_tile + local * " GLES2_SHADER_ATLAS_CELL ");\n"

static const char* const gles2_world_plain_fragment_shader =
    GLES2_FRAGMENT_PRECISION_PREAMBLE
    "uniform sampler2D s_texture;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "varying vec2 v_tile;\n"
    "varying vec2 v_wrap;\n"
    "void main() {\n"
    GLES2_WORLD_FRAGMENT_SAMPLE
    "    gl_FragColor = c;\n"
    "}\n";

static const char* const gles2_world_cutout_fragment_shader =
    GLES2_FRAGMENT_PRECISION_PREAMBLE
    "uniform sampler2D s_texture;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "varying vec2 v_tile;\n"
    "varying vec2 v_wrap;\n"
    "void main() {\n"
    GLES2_WORLD_FRAGMENT_SAMPLE
    "    if (c.a < 0.002) discard;\n"
    "    gl_FragColor = c;\n"
    "}\n";

/*
 * a_texinfo is the per-vertex sampler select (struct GLES2VertexUI.sel): 0 the
 * sprite atlas (s_texture, unit 0), 1 the batch's texture (s_mask, unit 1 --
 * the name is the linker's convention for "the unit-1 sampler", see
 * gles2_link_program), 2 flat colour. Both textures are sampled and mixed
 * rather than branched on: the select is constant across a quad, and two
 * fetches are cheaper on this class of GPU than a divergent branch would be
 * where it is not. It is what lets text, sprites and fills share one draw.
 */
static const char* const gles2_ui_vertex_shader =
    "attribute vec3 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "attribute vec4 a_color;\n"
    "attribute float a_texinfo;\n"
    "uniform mat4 u_matrix;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "varying float v_sel;\n"
    "void main() {\n"
    "    gl_Position = u_matrix * vec4(a_position.xy * a_position.z, 0.0, a_position.z);\n"
    "    v_color = a_color;\n"
    "    v_texcoord = a_texcoord;\n"
    "    v_sel = a_texinfo;\n"
    "}\n";

static const char* const gles2_ui_fragment_shader =
    GLES2_FRAGMENT_PRECISION_PREAMBLE
    "uniform sampler2D s_texture;\n"
    "uniform sampler2D s_mask;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "varying float v_sel;\n"
    "void main() {\n"
    "    vec4 t = mix(texture2D(s_texture, v_texcoord), texture2D(s_mask, v_texcoord),\n"
    "                 clamp(v_sel, 0.0, 1.0));\n"
    "    vec4 c = v_color * mix(t, vec4(1.0), step(1.5, v_sel));\n"
    "    if (c.a < 0.002) discard;\n"
    "    gl_FragColor = c;\n"
    "}\n";

/*
 * The rotated, masked chrome sprites (minimap, compass): the source is sampled
 * through the rotated quad and the mask axis-aligned over the destination box.
 * u_mask_invert selects which side of the mask is the window, the way D3D9's
 * D3DTA_COMPLEMENT did on its second texture stage.
 */
static const char* const gles2_rotmask_vertex_shader =
    "attribute vec3 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "attribute vec4 a_color;\n"
    "attribute vec2 a_mask_texcoord;\n"
    "uniform mat4 u_matrix;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "varying vec2 v_mask_texcoord;\n"
    "void main() {\n"
    "    gl_Position = u_matrix * vec4(a_position.xy, 0.0, 1.0);\n"
    "    v_color = a_color;\n"
    "    v_texcoord = a_texcoord;\n"
    "    v_mask_texcoord = a_mask_texcoord;\n"
    "}\n";

static const char* const gles2_rotmask_fragment_shader =
    GLES2_FRAGMENT_PRECISION_PREAMBLE
    "uniform sampler2D s_texture;\n"
    "uniform sampler2D s_mask;\n"
    "uniform float u_mask_invert;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "varying vec2 v_mask_texcoord;\n"
    "void main() {\n"
    "    vec4 c = v_color * texture2D(s_texture, v_texcoord);\n"
    "    float m = texture2D(s_mask, v_mask_texcoord).a;\n"
    "    c.a *= mix(m, 1.0 - m, u_mask_invert);\n"
    "    if (c.a < 0.002) discard;\n"
    "    gl_FragColor = c;\n"
    "}\n";

/* clang-format on */

#endif
