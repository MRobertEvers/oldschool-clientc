#ifndef SRC_PLATFORM_PLATFORM_RENDERER_WEBGL2_SHADERS_H
#define SRC_PLATFORM_PLATFORM_RENDERER_WEBGL2_SHADERS_H

/*
 * GLSL ES 3.00 -- the version a WebGL2 context compiles.
 *
 * Every program here is the GLES2 renderer's program
 * (platform_renderer_gles2_shaders.h) expressed in ES 3.00 and using the
 * three things ES 1.00 does not have:
 *
 *   layout(location = N) in    the attribute index is declared in the
 *                              shader, so no glBindAttribLocation before
 *                              the link and no chance of two programs
 *                              disagreeing about slot 3.
 *   uniform WorldBlock         the world pass's matrix and clock in one
 *                              std140 block bound to binding point 0. On
 *                              ES2 a uniform belongs to a PROGRAM object,
 *                              so the matrix has to be re-sent to each of
 *                              the four world programs; here the pass
 *                              uploads it once and all four read it.
 *   uvec4 texinfo              the tile/scroll word arrives as integers
 *                              (glVertexAttribIPointer) instead of four
 *                              normalised floats the shader has to scale
 *                              back up by 255.
 *
 * The sampling formula, the two fragment variants and the perspective
 * trick below are unchanged from the ES2 renderer, deliberately: they are
 * the parity reference, and a picture that differs between the two lanes is
 * a bug in one of them.
 *
 * ## The world
 *
 * One atlas, bound once per pass. A vertex carries its face's LOCAL texture
 * coordinate and a four-byte word saying which tile and how the texture
 * scrolls; the fragment shader wraps, clamps and maps that into the tile.
 * The clamp to [0.008, 0.992] and the unconditional fract() on v are the
 * GL3 formula verbatim, and the tile margin is what keeps a
 * linear-interpolated coordinate from sampling the neighbouring tile.
 *
 * Two fragment variants, split on `discard`:
 *
 *   plain    no discard at all, so the hardware keeps early depth rejection
 *            for the bulk opaque world pass.
 *   cutout   discards fragments whose alpha is below 1/255 -- the alpha test
 *            D3D9 runs (ALPHAREF 1, GREATEREQUAL). Cutout textures, the
 *            blended pass, the painter path and every UI draw use it.
 *
 * ## The UI
 *
 * The 2D vertex carries (x, y, w): w is 1 for every ordinary quad and the
 * view depth for a widget-model triangle. Multiplying x and y back up by w
 * makes the hardware's perspective divide land the vertex at (x, y) while
 * its varyings interpolate with 1/w -- D3D9's XYZRHW vertex, and what keeps
 * a chathead's texture from swimming.
 */

/* clang-format off */

/* 128 / 2048: one tile's extent in atlas coordinates. Spelled once here and
 * checked against the atlas constants in the core. */
#define WEBGL2_SHADER_ATLAS_CELL "0.0625"

/* The binding point the world block is attached to, spelled in both the
 * shader text and webgl2_link_program. */
#define WEBGL2_WORLD_BLOCK_BINDING 0

#define WEBGL2_VERTEX_PREAMBLE "#version 300 es\n"

#define WEBGL2_FRAGMENT_PREAMBLE \
    "#version 300 es\n" \
    "precision highp float;\n" \
    "precision highp sampler2D;\n"

/*
 * The world uniform block, std140, shared by all four world programs.
 * `u_clock` is the texture animation clock in ticks; std140 pads the block
 * to a vec4 boundary, and the padding is named so both halves of the ABI
 * are visible (webgl2_world_block_upload writes exactly this).
 */
#define WEBGL2_WORLD_BLOCK \
    "layout(std140) uniform WorldBlock {\n" \
    "    mat4 u_matrix;\n" \
    "    vec4 u_clock_pad;\n" \
    "};\n" \
    "#define u_clock (u_clock_pad.x)\n"

static const char* const webgl2_world_vertex_shader =
    WEBGL2_VERTEX_PREAMBLE
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec2 a_texcoord;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "layout(location = 3) in uvec4 a_texinfo;\n"
    WEBGL2_WORLD_BLOCK
    "out vec4 v_color;\n"
    "out vec2 v_texcoord;\n"
    "out vec2 v_tile;\n"
    "out vec2 v_wrap;\n"
    "void main() {\n"
    "    gl_Position = u_matrix * vec4(a_position, 1.0);\n"
    "    v_color = a_color;\n"
    "    vec2 anim = (vec2(a_texinfo.zw) - 128.0) * (1.0 / 128.0);\n"
    "    v_texcoord = a_texcoord + u_clock * anim;\n"
    "    v_wrap = step(0.001, abs(anim));\n"
    "    v_tile = vec2(a_texinfo.xy) * " WEBGL2_SHADER_ATLAS_CELL ";\n"
    "}\n";

#define WEBGL2_WORLD_FRAGMENT_IN \
    "uniform sampler2D s_texture;\n" \
    "in vec4 v_color;\n" \
    "in vec2 v_texcoord;\n" \
    "in vec2 v_tile;\n" \
    "in vec2 v_wrap;\n" \
    "out vec4 fragColor;\n"

#define WEBGL2_WORLD_FRAGMENT_SAMPLE \
    "    vec2 local = mix(v_texcoord, fract(v_texcoord), v_wrap);\n" \
    "    local.x = clamp(local.x, 0.008, 0.992);\n" \
    "    local.y = clamp(fract(local.y), 0.008, 0.992);\n" \
    "    vec4 c = v_color * texture(s_texture, v_tile + local * " WEBGL2_SHADER_ATLAS_CELL ");\n"

static const char* const webgl2_world_plain_fragment_shader =
    WEBGL2_FRAGMENT_PREAMBLE
    WEBGL2_WORLD_FRAGMENT_IN
    "void main() {\n"
    WEBGL2_WORLD_FRAGMENT_SAMPLE
    "    fragColor = c;\n"
    "}\n";

static const char* const webgl2_world_cutout_fragment_shader =
    WEBGL2_FRAGMENT_PREAMBLE
    WEBGL2_WORLD_FRAGMENT_IN
    "void main() {\n"
    WEBGL2_WORLD_FRAGMENT_SAMPLE
    "    if (c.a < 0.002) discard;\n"
    "    fragColor = c;\n"
    "}\n";

/* Slot zero is an opaque white tile. All three vertices of a face carry
 * the same tile, so untextured faces can return their interpolated colour
 * without a texture fetch at all. */
#define WEBGL2_WORLD_FRAGMENT_FAST_SAMPLE \
    "    vec4 c;\n" \
    "    if (v_tile.x == 0.0 && v_tile.y == 0.0) { c = v_color; }\n" \
    "    else {\n" \
    "        vec2 local = mix(v_texcoord, fract(v_texcoord), v_wrap);\n" \
    "        local.x = clamp(local.x, 0.008, 0.992);\n" \
    "        local.y = clamp(fract(local.y), 0.008, 0.992);\n" \
    "        c = v_color * texture(s_texture, v_tile + local * " WEBGL2_SHADER_ATLAS_CELL ");\n" \
    "    }\n"

static const char* const webgl2_world_fast_plain_fragment_shader =
    WEBGL2_FRAGMENT_PREAMBLE
    WEBGL2_WORLD_FRAGMENT_IN
    "void main() {\n"
    WEBGL2_WORLD_FRAGMENT_FAST_SAMPLE
    "    fragColor = c;\n"
    "}\n";

static const char* const webgl2_world_fast_cutout_fragment_shader =
    WEBGL2_FRAGMENT_PREAMBLE
    WEBGL2_WORLD_FRAGMENT_IN
    "void main() {\n"
    WEBGL2_WORLD_FRAGMENT_FAST_SAMPLE
    "    if (c.a < 0.002) discard;\n"
    "    fragColor = c;\n"
    "}\n";

/*
 * a_texinfo is the per-vertex sampler select (struct WebGL2VertexUI.sel): 0
 * the sprite atlas (s_texture, unit 0), 1 the batch's texture (s_mask, unit
 * 1 -- the name is the linker's convention for "the unit-1 sampler"), 2 flat
 * colour. Both textures are sampled and mixed rather than branched on: the
 * select is constant across a quad, and two fetches are cheaper than a
 * divergent branch where it is not. It is what lets text, sprites and fills
 * share one draw.
 *
 * This layout reads slot 3 as a FLOAT, not as the world's uvec4, which is
 * why the two layouts sit in separate vertex array objects here.
 */
static const char* const webgl2_ui_vertex_shader =
    WEBGL2_VERTEX_PREAMBLE
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec2 a_texcoord;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "layout(location = 3) in float a_texinfo;\n"
    "uniform mat4 u_matrix;\n"
    "out vec4 v_color;\n"
    "out vec2 v_texcoord;\n"
    "out float v_sel;\n"
    "void main() {\n"
    "    gl_Position = u_matrix * vec4(a_position.xy * a_position.z, 0.0, a_position.z);\n"
    "    v_color = a_color;\n"
    "    v_texcoord = a_texcoord;\n"
    "    v_sel = a_texinfo;\n"
    "}\n";

static const char* const webgl2_ui_fragment_shader =
    WEBGL2_FRAGMENT_PREAMBLE
    "uniform sampler2D s_texture;\n"
    "uniform sampler2D s_mask;\n"
    "in vec4 v_color;\n"
    "in vec2 v_texcoord;\n"
    "in float v_sel;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec4 t = mix(texture(s_texture, v_texcoord), texture(s_mask, v_texcoord),\n"
    "                 clamp(v_sel, 0.0, 1.0));\n"
    "    vec4 c = v_color * mix(t, vec4(1.0), step(1.5, v_sel));\n"
    "    if (c.a < 0.002) discard;\n"
    "    fragColor = c;\n"
    "}\n";

/*
 * The rotated, masked chrome sprites (minimap, compass): the source is
 * sampled through the rotated quad and the mask axis-aligned over the
 * destination box. u_mask_invert selects which side of the mask is the
 * window, the way D3D9's D3DTA_COMPLEMENT did on its second texture stage.
 *
 * The mask coordinate is slot 3 again, as a vec2 -- a third reading of that
 * slot, and a third vertex array object.
 */
static const char* const webgl2_rotmask_vertex_shader =
    WEBGL2_VERTEX_PREAMBLE
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec2 a_texcoord;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "layout(location = 3) in vec2 a_mask_texcoord;\n"
    "uniform mat4 u_matrix;\n"
    "out vec4 v_color;\n"
    "out vec2 v_texcoord;\n"
    "out vec2 v_mask_texcoord;\n"
    "void main() {\n"
    "    gl_Position = u_matrix * vec4(a_position.xy, 0.0, 1.0);\n"
    "    v_color = a_color;\n"
    "    v_texcoord = a_texcoord;\n"
    "    v_mask_texcoord = a_mask_texcoord;\n"
    "}\n";

/*
 * The mask texture is single-channel GL_R8 here, not GL_LUMINANCE_ALPHA:
 * ES3 dropped the luminance formats and gives sized ones instead, so the
 * coverage the ES2 shader reads out of `.a` is read out of `.r`.
 */
static const char* const webgl2_rotmask_fragment_shader =
    WEBGL2_FRAGMENT_PREAMBLE
    "uniform sampler2D s_texture;\n"
    "uniform sampler2D s_mask;\n"
    "uniform float u_mask_invert;\n"
    "in vec4 v_color;\n"
    "in vec2 v_texcoord;\n"
    "in vec2 v_mask_texcoord;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec4 c = v_color * texture(s_texture, v_texcoord);\n"
    "    float m = texture(s_mask, v_mask_texcoord).r;\n"
    "    c.a *= mix(m, 1.0 - m, u_mask_invert);\n"
    "    if (c.a < 0.002) discard;\n"
    "    fragColor = c;\n"
    "}\n";

/*
 * Client scaling's present: the offscreen frame sampled onto the output
 * rect. Positions are clip space; alpha is forced opaque because the direct
 * path shows the colour whatever the frame's alpha channel holds.
 */
static const char* const webgl2_present_vertex_shader =
    WEBGL2_VERTEX_PREAMBLE
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec2 a_texcoord;\n"
    "out vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_position.xy, 0.0, 1.0);\n"
    "    v_texcoord = a_texcoord;\n"
    "}\n";

static const char* const webgl2_present_fragment_shader =
    WEBGL2_FRAGMENT_PREAMBLE
    "uniform sampler2D s_texture;\n"
    "in vec2 v_texcoord;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = vec4(texture(s_texture, v_texcoord).rgb, 1.0);\n"
    "}\n";

/*
 * The interface layer composite (webgl2_ui_layer_composite): one quad over
 * the output rect sampling the layout-sized, premultiplied interface
 * picture. The vertex shader is the present's. u_filter 1 is linear (the
 * texture's own GL_LINEAR); 2 is Catmull-Rom bicubic over the 4x4 texel
 * centres around the sample, with the texture GL_NEAREST. Catmull-Rom
 * overshoots, so the result is clamped back into premultiplied range: alpha
 * in [0,1], colour <= alpha.
 *
 * ES 3.00 lets the taps be texelFetch with an integer coordinate, which is
 * what the loop actually wants: no division by the size per tap, no
 * half-texel bias to get right, and no filtering state to depend on.
 */
static const char* const webgl2_ui_composite_fragment_shader =
    WEBGL2_FRAGMENT_PREAMBLE
    "uniform sampler2D s_texture;\n"
    "uniform vec2 u_size;\n"
    "uniform float u_filter;\n"
    "in vec2 v_texcoord;\n"
    "out vec4 fragColor;\n"
    "vec4 cr_weights(float t) {\n"
    "    float t2 = t * t;\n"
    "    float t3 = t2 * t;\n"
    "    return vec4(-0.5 * t3 + t2 - 0.5 * t,\n"
    "                1.5 * t3 - 2.5 * t2 + 1.0,\n"
    "                -1.5 * t3 + 2.0 * t2 + 0.5 * t,\n"
    "                0.5 * t3 - 0.5 * t2);\n"
    "}\n"
    "void main() {\n"
    "    vec4 c;\n"
    "    if (u_filter > 1.5) {\n"
    "        vec2 p = v_texcoord * u_size - 0.5;\n"
    "        vec2 base = floor(p);\n"
    "        vec2 f = p - base;\n"
    "        vec4 wx = cr_weights(f.x);\n"
    "        vec4 wy = cr_weights(f.y);\n"
    "        ivec2 last = ivec2(u_size) - 1;\n"
    "        ivec2 origin = ivec2(base);\n"
    "        c = vec4(0.0);\n"
    "        for (int j = 0; j < 4; j++) {\n"
    "            vec4 row = vec4(0.0);\n"
    "            for (int i = 0; i < 4; i++) {\n"
    "                ivec2 texel = clamp(origin + ivec2(i - 1, j - 1), ivec2(0), last);\n"
    "                row += texelFetch(s_texture, texel, 0) * wx[i];\n"
    "            }\n"
    "            c += row * wy[j];\n"
    "        }\n"
    "        c.a = clamp(c.a, 0.0, 1.0);\n"
    "        c.rgb = clamp(c.rgb, vec3(0.0), vec3(c.a));\n"
    "    } else {\n"
    "        c = texture(s_texture, v_texcoord);\n"
    "    }\n"
    "    if (c.a < 0.002) discard;\n"
    "    fragColor = c;\n"
    "}\n";

/* clang-format on */

#endif
