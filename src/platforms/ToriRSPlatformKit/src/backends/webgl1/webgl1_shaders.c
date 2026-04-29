#include "trspk_webgl1.h"
#include "webgl1_vertex.h"

#include <math.h>
#include <stdio.h>

#ifdef __EMSCRIPTEN__
static GLuint
trspk_webgl1_compile(
    GLenum type,
    const char* src)
{
    GLuint shader = glCreateShader(type);
    if( shader == 0u )
        return 0u;
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if( !ok )
    {
        char buf[512];
        glGetShaderInfoLog(shader, (GLsizei)sizeof(buf), NULL, buf);
        fprintf(stderr, "TRSPK WebGL1 shader compile failed: %s\n", buf);
        glDeleteShader(shader);
        return 0u;
    }
    return shader;
}
#endif

GLuint
trspk_webgl1_create_world_program(TRSPK_WebGL1WorldShaderLocs* locs)
{
#ifdef __EMSCRIPTEN__
    /* Must stay in sync with kWorldVs / kWorldFs in src/platforms/webgl1/gl.cpp */
    const char* vs = "attribute vec4 a_position;\n"
                     "attribute vec4 a_color;\n"
                     "attribute vec2 a_texcoord;\n"
                     "attribute float a_tex_id;\n"
                     "attribute float a_uv_mode;\n"
                     "uniform mat4 u_modelViewMatrix;\n"
                     "uniform mat4 u_projectionMatrix;\n"
                     "varying vec4 v_color;\n"
                     "varying vec2 v_texcoord;\n"
                     "varying float v_tex_id;\n"
                     "varying float v_uv_pack;\n"
                     "void main() {\n"
                     "  vec4 wp = vec4(a_position.xyz, 1.0);\n"
                     "  gl_Position = u_projectionMatrix * u_modelViewMatrix * wp;\n"
                     "  v_color = a_color;\n"
                     "  v_texcoord = a_texcoord;\n"
                     "  v_tex_id = a_tex_id;\n"
                     "  v_uv_pack = a_uv_mode;\n"
                     "}\n";
    const char* fs =
        "precision mediump float;\n"
        "varying vec4 v_color;\n"
        "varying vec2 v_texcoord;\n"
        "varying float v_tex_id;\n"
        "varying float v_uv_pack;\n"
        "uniform float u_clock;\n"
        "uniform sampler2D s_atlas;\n"
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
        "    gl_FragColor = vec4(v_color.rgb, v_color.a);\n"
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
        "  float clk = u_clock;\n"
        "  if (anim_u > 0.0) local.x += clk * anim_u;\n"
        "  if (anim_v > 0.0) local.y -= clk * anim_v;\n"
        "  local.x = clamp(local.x, 0.008, 0.992);\n"
        "  local.y = clamp(fract(local.y), 0.008, 0.992);\n"
        "  vec2 uv_atlas = ta.xy + local * ta.zw;\n"
        "  vec4 texColor = texture2D(s_atlas, uv_atlas);\n"
        "  vec3 finalColor = mix(v_color.rgb, texColor.rgb * v_color.rgb, 1.0);\n"
        "  float finalAlpha = v_color.a;\n"
        "  if (cutout) {\n"
        "    if (texColor.a < 0.5) discard;\n"
        "  }\n"
        "  gl_FragColor = vec4(finalColor, finalAlpha);\n"
        "}\n";
    GLuint p = glCreateProgram();
    GLuint v = trspk_webgl1_compile(GL_VERTEX_SHADER, vs);
    GLuint f = trspk_webgl1_compile(GL_FRAGMENT_SHADER, fs);
    if( p == 0u || v == 0u || f == 0u )
    {
        if( v )
            glDeleteShader(v);
        if( f )
            glDeleteShader(f);
        if( p )
            glDeleteProgram(p);
        return 0u;
    }
    glAttachShader(p, v);
    glAttachShader(p, f);
    glBindAttribLocation(p, 0, "a_position");
    glBindAttribLocation(p, 1, "a_color");
    glBindAttribLocation(p, 2, "a_texcoord");
    glBindAttribLocation(p, 3, "a_tex_id");
    glBindAttribLocation(p, 4, "a_uv_mode");
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if( !ok )
    {
        char buf[512];
        glGetProgramInfoLog(p, (GLsizei)sizeof(buf), NULL, buf);
        fprintf(stderr, "TRSPK WebGL1 world program link failed: %s\n", buf);
        glDeleteProgram(p);
        return 0u;
    }
    locs->a_position = glGetAttribLocation(p, "a_position");
    locs->a_color = glGetAttribLocation(p, "a_color");
    locs->a_texcoord = glGetAttribLocation(p, "a_texcoord");
    locs->a_tex_id = glGetAttribLocation(p, "a_tex_id");
    locs->a_uv_mode = glGetAttribLocation(p, "a_uv_mode");
    locs->u_modelViewMatrix = glGetUniformLocation(p, "u_modelViewMatrix");
    locs->u_projectionMatrix = glGetUniformLocation(p, "u_projectionMatrix");
    locs->u_clock = glGetUniformLocation(p, "u_clock");
    locs->s_atlas = glGetUniformLocation(p, "s_atlas");
    return p;
#else
    (void)locs;
    return 0u;
#endif
}

void
trspk_webgl1_bind_world_attribs(TRSPK_WebGL1Renderer* r)
{
#ifdef __EMSCRIPTEN__
    const GLsizei stride = (GLsizei)sizeof(TRSPK_VertexWebGL1);
    if( r->world_locs.a_position >= 0 )
    {
        glEnableVertexAttribArray((GLuint)r->world_locs.a_position);
        glVertexAttribPointer(
            (GLuint)r->world_locs.a_position, 4, GL_FLOAT, GL_FALSE, stride, (void*)0);
    }
    if( r->world_locs.a_color >= 0 )
    {
        glEnableVertexAttribArray((GLuint)r->world_locs.a_color);
        glVertexAttribPointer(
            (GLuint)r->world_locs.a_color, 4, GL_FLOAT, GL_FALSE, stride, (void*)16);
    }
    if( r->world_locs.a_texcoord >= 0 )
    {
        glEnableVertexAttribArray((GLuint)r->world_locs.a_texcoord);
        glVertexAttribPointer(
            (GLuint)r->world_locs.a_texcoord, 2, GL_FLOAT, GL_FALSE, stride, (void*)32);
    }
    if( r->world_locs.a_tex_id >= 0 )
    {
        glEnableVertexAttribArray((GLuint)r->world_locs.a_tex_id);
        glVertexAttribPointer(
            (GLuint)r->world_locs.a_tex_id, 1, GL_FLOAT, GL_FALSE, stride, (void*)40);
    }
    if( r->world_locs.a_uv_mode >= 0 )
    {
        glEnableVertexAttribArray((GLuint)r->world_locs.a_uv_mode);
        glVertexAttribPointer(
            (GLuint)r->world_locs.a_uv_mode, 1, GL_FLOAT, GL_FALSE, stride, (void*)44);
    }
#else
    (void)r;
#endif
}

void
trspk_webgl1_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw)
{
    float cosPitch = cosf(-pitch);
    float sinPitch = sinf(-pitch);
    float cosYaw = cosf(-yaw);
    float sinYaw = sinf(-yaw);

    out_matrix[0] = cosYaw;
    out_matrix[1] = sinYaw * sinPitch;
    out_matrix[2] = sinYaw * cosPitch;
    out_matrix[3] = 0.0f;
    out_matrix[4] = 0.0f;
    out_matrix[5] = cosPitch;
    out_matrix[6] = -sinPitch;
    out_matrix[7] = 0.0f;
    out_matrix[8] = -sinYaw;
    out_matrix[9] = cosYaw * sinPitch;
    out_matrix[10] = cosYaw * cosPitch;
    out_matrix[11] = 0.0f;
    out_matrix[12] = -camera_x * cosYaw + camera_z * sinYaw;
    out_matrix[13] =
        -camera_x * sinYaw * sinPitch - camera_y * cosPitch - camera_z * cosYaw * sinPitch;
    out_matrix[14] =
        -camera_x * sinYaw * cosPitch + camera_y * sinPitch - camera_z * cosYaw * cosPitch;
    out_matrix[15] = 1.0f;
}

void
trspk_webgl1_compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height)
{
    float y = 1.0f / tanf(fov * 0.5f);
    float x = y;
    out_matrix[0] = x * 512.0f / (screen_width / 2.0f);
    out_matrix[1] = 0.0f;
    out_matrix[2] = 0.0f;
    out_matrix[3] = 0.0f;
    out_matrix[4] = 0.0f;
    out_matrix[5] = -y * 512.0f / (screen_height / 2.0f);
    out_matrix[6] = 0.0f;
    out_matrix[7] = 0.0f;
    out_matrix[8] = 0.0f;
    out_matrix[9] = 0.0f;
    out_matrix[10] = 0.0f;
    out_matrix[11] = 1.0f;
    out_matrix[12] = 0.0f;
    out_matrix[13] = 0.0f;
    out_matrix[14] = -1.0f;
    out_matrix[15] = 0.0f;
}
