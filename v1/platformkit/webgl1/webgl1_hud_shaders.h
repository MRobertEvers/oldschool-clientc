#ifndef WEBGL1_HUD_SHADERS_H
#define WEBGL1_HUD_SHADERS_H

static char const* const trspk_webgl1_hud_vertex_shader =
    "attribute vec2 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "attribute vec4 a_color;\n"
    "uniform mat4 u_projection;\n"
    "varying vec2 v_texcoord;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "    v_texcoord = a_texcoord;\n"
    "    v_color = a_color;\n"
    "    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);\n"
    "}\n";

static char const* const trspk_webgl1_hud_fragment_shader =
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "varying vec4 v_color;\n"
    "uniform sampler2D u_texture;\n"
    "void main() {\n"
    "    vec4 tex = texture2D(u_texture, v_texcoord);\n"
    "    vec4 c = tex * v_color;\n"
    "    if (c.a < 0.004) discard;\n"
    "    gl_FragColor = c;\n"
    "}\n";

#endif
