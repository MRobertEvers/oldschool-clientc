#include "opengl3_internal.h"
#include "trspk_opengl3.h"

#include <stdio.h>
#include <string.h>

#if defined(__APPLE__)
#define TRSPK_OGL3_SHADER_VERSION "#version 150 core\n"
#else
#define TRSPK_OGL3_SHADER_VERSION "#version 330 core\n"
#endif

static TRSPK_GLuint
trspk_opengl3_compile_shader(
    TRSPK_GLenum type,
    const char* src)
{
    TRSPK_GLuint shader = trspk_glCreateShader(type);
    if( shader == 0u )
        return 0u;
    trspk_glShaderSource(shader, 1, &src, NULL);
    trspk_glCompileShader(shader);
    TRSPK_GLint ok = 0;
    trspk_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if( !ok )
    {
        char buf[512];
        trspk_glGetShaderInfoLog(shader, (TRSPK_GLsizei)sizeof(buf), NULL, (TRSPK_GLchar*)buf);
        fprintf(stderr, "TRSPK OpenGL3 shader compile failed: %s\n", buf);
        trspk_glDeleteShader(shader);
        return 0u;
    }
    return shader;
}

uint32_t
trspk_opengl3_create_world_program(TRSPK_OpenGL3WorldShaderLocs* locs)
{
    if( !locs )
        return 0u;
    memset(locs, -1, sizeof(*locs));

    const char* vs = TRSPK_OGL3_SHADER_VERSION
                     "layout(std140) uniform TRSPK_UboWorld {\n"
                     "  mat4 u_modelViewMatrix;\n"
                     "  mat4 u_projectionMatrix;\n"
                     "  vec4 u_clock_pad;\n"
                     "} ubo;\n"
                     "in vec4 a_position;\n"
                     "in vec4 a_color;\n"
                     "in vec2 a_texcoord;\n"
                     "in float a_tex_id;\n"
                     "in float a_uv_mode;\n"
                     "out vec4 v_color;\n"
                     "out vec2 v_texcoord;\n"
                     "out float v_tex_id;\n"
                     "out float v_uv_pack;\n"
                     "void main() {\n"
                     "  vec4 wp = vec4(a_position.xyz, 1.0);\n"
                     "  gl_Position = ubo.u_projectionMatrix * ubo.u_modelViewMatrix * wp;\n"
                     "  v_color = a_color;\n"
                     "  v_texcoord = a_texcoord;\n"
                     "  v_tex_id = a_tex_id;\n"
                     "  v_uv_pack = a_uv_mode;\n"
                     "}\n";

    const char* fs = TRSPK_OGL3_SHADER_VERSION
                     "layout(std140) uniform TRSPK_UboWorld {\n"
                     "  mat4 u_modelViewMatrix;\n"
                     "  mat4 u_projectionMatrix;\n"
                     "  vec4 u_clock_pad;\n"
                     "} ubo;\n"
                     "in vec4 v_color;\n"
                     "in vec2 v_texcoord;\n"
                     "in float v_tex_id;\n"
                     "in float v_uv_pack;\n"
                     "uniform sampler2D s_atlas;\n"
                     "out vec4 frag_color;\n"
                     "void main() {\n"
                     "  int raw = int(floor(v_tex_id + 0.5));\n"
                     "  int atlas_id;\n"
                     "  bool cutout;\n"
                     "  if (raw >= 256) {\n"
                     "    atlas_id = raw - 256;\n"
                     "    cutout = true;\n"
                     "  } else {\n"
                     "    atlas_id = raw;\n"
                     "    cutout = false;\n"
                     "  }\n"
                     "  if (atlas_id < 0 || atlas_id >= 256) {\n"
                     "    frag_color = vec4(v_color.rgb, v_color.a);\n"
                     "    return;\n"
                     "  }\n"
                     "  int enc = int(floor(v_uv_pack * 0.5 + 0.5));\n"
                     "  float anim_u = 0.0;\n"
                     "  float anim_v = 0.0;\n"
                     "  if (enc > 0) {\n"
                     "    if (enc < 257) {\n"
                     "      int mag_u = enc - 1;\n"
                     "      anim_u = float(mag_u) / 256.0;\n"
                     "    } else {\n"
                     "      int mag_v = enc - 257;\n"
                     "      anim_v = float(mag_v) / 256.0;\n"
                     "    }\n"
                     "  }\n"
                     "  const float TEX_DIM = 128.0;\n"
                     "  const float ATLAS_DIM = 2048.0;\n"
                     "  const float cols = 16.0;\n"
                     "  float du = TEX_DIM / ATLAS_DIM;\n"
                     "  float dv = du;\n"
                     "  float col = mod(float(atlas_id), cols);\n"
                     "  float row = floor(float(atlas_id) / cols);\n"
                     "  vec4 ta = vec4(col * du, row * dv, du, dv);\n"
                     "  vec2 local = v_texcoord;\n"
                     "  float clk = ubo.u_clock_pad.x;\n"
                     "  if (anim_u > 0.0) local.x += clk * anim_u;\n"
                     "  if (anim_v > 0.0) local.y -= clk * anim_v;\n"
                     "  local.x = clamp(local.x, 0.008, 0.992);\n"
                     "  local.y = clamp(fract(local.y), 0.008, 0.992);\n"
                     "  vec2 uv_atlas = ta.xy + local * ta.zw;\n"
                     "  vec4 texColor = texture(s_atlas, uv_atlas);\n"
                     "  vec3 finalColor = mix(v_color.rgb, texColor.rgb * v_color.rgb, 1.0);\n"
                     "  float finalAlpha = v_color.a;\n"
                     "  if (cutout) {\n"
                     "    if (texColor.a < 0.5) discard;\n"
                     "  }\n"
                     "  frag_color = vec4(finalColor, finalAlpha);\n"
                     "}\n";

    TRSPK_GLuint p = trspk_glCreateProgram();
    TRSPK_GLuint v = trspk_opengl3_compile_shader(GL_VERTEX_SHADER, vs);
    TRSPK_GLuint f = trspk_opengl3_compile_shader(GL_FRAGMENT_SHADER, fs);
    if( p == 0u || v == 0u || f == 0u )
    {
        if( v )
            trspk_glDeleteShader(v);
        if( f )
            trspk_glDeleteShader(f);
        if( p )
            trspk_glDeleteProgram(p);
        return 0u;
    }
    trspk_glAttachShader(p, v);
    trspk_glAttachShader(p, f);
    trspk_glBindAttribLocation(p, 0u, "a_position");
    trspk_glBindAttribLocation(p, 1u, "a_color");
    trspk_glBindAttribLocation(p, 2u, "a_texcoord");
    trspk_glBindAttribLocation(p, 3u, "a_tex_id");
    trspk_glBindAttribLocation(p, 4u, "a_uv_mode");
    trspk_glLinkProgram(p);
    trspk_glDeleteShader(v);
    trspk_glDeleteShader(f);
    TRSPK_GLint ok = 0;
    trspk_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if( !ok )
    {
        char buf[512];
        trspk_glGetProgramInfoLog(p, (TRSPK_GLsizei)sizeof(buf), NULL, (TRSPK_GLchar*)buf);
        fprintf(stderr, "TRSPK OpenGL3 world program link failed: %s\n", buf);
        trspk_glDeleteProgram(p);
        return 0u;
    }
    locs->a_position = trspk_glGetAttribLocation(p, "a_position");
    locs->a_color = trspk_glGetAttribLocation(p, "a_color");
    locs->a_texcoord = trspk_glGetAttribLocation(p, "a_texcoord");
    locs->a_tex_id = trspk_glGetAttribLocation(p, "a_tex_id");
    locs->a_uv_mode = trspk_glGetAttribLocation(p, "a_uv_mode");
    locs->s_atlas = trspk_glGetUniformLocation(p, "s_atlas");
    {
        const TRSPK_GLuint block_ix = trspk_glGetUniformBlockIndex(p, "TRSPK_UboWorld");
        if( block_ix != GL_INVALID_INDEX )
            trspk_glUniformBlockBinding(p, block_ix, 0u);
    }
    return (uint32_t)p;
}
