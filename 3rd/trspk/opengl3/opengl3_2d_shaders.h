#ifndef OPENGL3_2D_SHADERS_H
#define OPENGL3_2D_SHADERS_H

static char const* const trspk_opengl3_2d_vertex_shader =
    "#version 150 core\n"
    "in vec2 a_position;\n"
    "in vec2 a_texcoord;\n"
    "in vec4 a_color;\n"
    "uniform mat4 u_projection;\n"
    "out vec2 v_texcoord;\n"
    "out vec4 v_color;\n"
    "void main() {\n"
    "    v_texcoord = a_texcoord;\n"
    "    v_color = a_color;\n"
    "    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);\n"
    "}\n";

static char const* const trspk_opengl3_2d_fragment_shader =
    "#version 150 core\n"
    "in vec2 v_texcoord;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D u_texture;\n"
    "uniform int u_text_mode;\n"
    "uniform int u_uv_clamp;\n"
    "uniform vec4 u_uv_bounds;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    if (u_uv_clamp == 1 && (v_texcoord.x < u_uv_bounds.x || v_texcoord.x > u_uv_bounds.z ||\n"
    "        v_texcoord.y < u_uv_bounds.y || v_texcoord.y > u_uv_bounds.w))\n"
    "        discard;\n"
    "    vec4 tex = texture(u_texture, v_texcoord);\n"
    "    vec4 c = (u_text_mode == 1)\n"
    "        ? vec4(v_color.rgb, step(0.001, tex.r) * v_color.a)\n"
    "        : tex * v_color;\n"
    "    if (c.a < 0.004) discard;\n"
    "    fragColor = c;\n"
    "}\n";

/*
 * The interface layer composite: one full-output-rect quad sampling the
 * layout-sized premultiplied interface picture. u_filter 1 is linear (the
 * texture's own GL_LINEAR), 2 is Catmull-Rom bicubic over 4x4 texel centres
 * (the texture is GL_NEAREST then). Catmull-Rom overshoots, so the result is
 * clamped back into premultiplied range: alpha in [0,1], colour <= alpha.
 */
static char const* const trspk_opengl3_ui_composite_vertex_shader =
    "#version 150 core\n"
    "in vec2 a_position;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = a_position * 0.5 + 0.5;\n"
    "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "}\n";

static char const* const trspk_opengl3_ui_composite_fragment_shader =
    "#version 150 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_layer;\n"
    "uniform vec2 u_size;\n"
    "uniform int u_filter;\n"
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
    "    if (u_filter == 2) {\n"
    "        vec2 p = v_uv * u_size - 0.5;\n"
    "        vec2 base = floor(p);\n"
    "        vec2 f = p - base;\n"
    "        vec4 wx = cr_weights(f.x);\n"
    "        vec4 wy = cr_weights(f.y);\n"
    "        c = vec4(0.0);\n"
    "        for (int j = 0; j < 4; j++) {\n"
    "            vec4 row = vec4(0.0);\n"
    "            for (int i = 0; i < 4; i++) {\n"
    "                vec2 texel = clamp(base + vec2(float(i - 1), float(j - 1)), vec2(0.0), u_size - 1.0);\n"
    "                row += texture(u_layer, (texel + 0.5) / u_size) * wx[i];\n"
    "            }\n"
    "            c += row * wy[j];\n"
    "        }\n"
    "        c.a = clamp(c.a, 0.0, 1.0);\n"
    "        c.rgb = clamp(c.rgb, vec3(0.0), vec3(c.a));\n"
    "    } else {\n"
    "        c = texture(u_layer, v_uv);\n"
    "    }\n"
    "    if (c.a < 0.002) discard;\n"
    "    fragColor = c;\n"
    "}\n";

#endif
