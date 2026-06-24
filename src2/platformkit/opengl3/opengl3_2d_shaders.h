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
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec4 tex = texture(u_texture, v_texcoord);\n"
    "    vec4 c = (u_text_mode == 1)\n"
    "        ? vec4(v_color.rgb, tex.r * v_color.a)\n"
    "        : tex * v_color;\n"
    "    if (c.a < 0.004) discard;\n"
    "    fragColor = c;\n"
    "}\n";

#endif
