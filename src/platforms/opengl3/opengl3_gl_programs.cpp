#include "platforms/opengl3/opengl3_ctx.h"
#include "platforms/opengl3/opengl3_renderer_core.h"

#    include <cstdio>
#    include <cstring>

static GLuint
compile_shader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    if( s == 0u )
        return 0u;
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if( !ok )
    {
        char buf[512];
        glGetShaderInfoLog(s, (GLsizei)sizeof(buf), nullptr, buf);
        fprintf(stderr, "OpenGL3 shader compile failed: %s\n", buf);
        glDeleteShader(s);
        return 0u;
    }
    return s;
}

/* GLSL 1.50 for OpenGL 3.2 core (macOS SDL default). */
static const char kWorldVs[] = R"(#version 150 core
in vec4 a_position;
in vec4 a_color;
in vec2 a_texcoord;
in float a_tex_id;
in float a_uv_mode;
uniform mat4 u_modelViewMatrix;
uniform mat4 u_projectionMatrix;
out vec4 v_color;
out vec2 v_texcoord;
out float v_tex_id;
out float v_uv_mode;
void main() {
  vec4 wp = vec4(a_position.xyz, 1.0);
  gl_Position = u_projectionMatrix * u_modelViewMatrix * wp;
  v_color = a_color;
  v_texcoord = a_texcoord;
  v_tex_id = a_tex_id;
  v_uv_mode = a_uv_mode;
}
)";

static const char kWorldFs[] = R"(#version 150 core
in vec4 v_color;
in vec2 v_texcoord;
in float v_tex_id;
in float v_uv_mode;
uniform float u_clock;
uniform sampler2D s_atlas;
uniform vec4 u_tileA[256];
uniform vec4 u_tileB[256];
out vec4 fragColor;
void main() {
  int tid = int(floor(v_tex_id + 0.5));
  if (tid < 0 || tid >= 256) {
    fragColor = vec4(v_color.rgb, v_color.a);
    return;
  }
  vec4 ta = vec4(0.0);
  vec4 tb = vec4(0.0);
  for (int i = 0; i < 256; ++i) {
    if (i == tid) {
      ta = u_tileA[i];
      tb = u_tileB[i];
      break;
    }
  }
  if (ta.z <= 0.0 || ta.w <= 0.0) {
    fragColor = vec4(v_color.rgb, v_color.a);
    return;
  }
  vec2 local = v_texcoord;
  float clk = u_clock;
  if (v_uv_mode < 0.5) {
    if (tb.x > 0.0) local.x += clk * tb.x;
    if (tb.y > 0.0) local.y -= clk * tb.y;
    local.x = clamp(local.x, 0.008, 0.992);
    local.y = clamp(fract(local.y), 0.008, 0.992);
  } else {
    if (tb.x > 0.0) local.x += clk * tb.x;
    if (tb.y > 0.0) local.y += clk * tb.y;
    local.x = clamp(fract(local.x), 0.008, 0.992);
    local.y = clamp(fract(local.y), 0.008, 0.992);
  }
  vec2 uv_atlas = ta.xy + local * ta.zw;
  vec4 texColor = texture(s_atlas, uv_atlas);
  vec3 finalColor = mix(v_color.rgb, texColor.rgb * v_color.rgb, 1.0);
  float finalAlpha = v_color.a;
  if (tb.z < 0.5) {
    if (texColor.a < 0.5) discard;
  }
  fragColor = vec4(finalColor, finalAlpha);
}
)";

bool
opengl3_gl_create_programs(Platform2_SDL2_Renderer_OpenGL3* r)
{
    if( !r )
        return false;

    GLuint wvs = compile_shader(GL_VERTEX_SHADER, kWorldVs);
    GLuint wfs = compile_shader(GL_FRAGMENT_SHADER, kWorldFs);
    if( !wvs || !wfs )
        return false;

    GLuint wp = glCreateProgram();
    glBindAttribLocation(wp, 0, "a_position");
    glBindAttribLocation(wp, 1, "a_color");
    glBindAttribLocation(wp, 2, "a_texcoord");
    glBindAttribLocation(wp, 3, "a_tex_id");
    glBindAttribLocation(wp, 4, "a_uv_mode");
    glAttachShader(wp, wvs);
    glAttachShader(wp, wfs);
    glLinkProgram(wp);
    glDeleteShader(wvs);
    glDeleteShader(wfs);
    GLint wok = 0;
    glGetProgramiv(wp, GL_LINK_STATUS, &wok);
    if( !wok )
    {
        char buf[512];
        glGetProgramInfoLog(wp, (GLsizei)sizeof(buf), nullptr, buf);
        fprintf(stderr, "OpenGL3 world program link failed: %s\n", buf);
        glDeleteProgram(wp);
        return false;
    }
    r->prog_world3d = wp;

    OpenGL3WorldShaderLocs& L = r->world_locs;
    L.a_position = glGetAttribLocation(wp, "a_position");
    L.a_color = glGetAttribLocation(wp, "a_color");
    L.a_texcoord = glGetAttribLocation(wp, "a_texcoord");
    L.a_tex_id = glGetAttribLocation(wp, "a_tex_id");
    L.a_uv_mode = glGetAttribLocation(wp, "a_uv_mode");
    L.u_modelViewMatrix = glGetUniformLocation(wp, "u_modelViewMatrix");
    L.u_projectionMatrix = glGetUniformLocation(wp, "u_projectionMatrix");
    L.u_clock = glGetUniformLocation(wp, "u_clock");
    L.s_atlas = glGetUniformLocation(wp, "s_atlas");
    L.u_tileA = glGetUniformLocation(wp, "u_tileA");
    L.u_tileB = glGetUniformLocation(wp, "u_tileB");

    opengl3_gl_bind_default_world_gl_state();
    return true;
}

void
opengl3_gl_bind_default_world_gl_state(void)
{
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void
opengl3_gl_destroy_programs(Platform2_SDL2_Renderer_OpenGL3* r)
{
    if( !r )
        return;
    if( r->prog_world3d )
    {
        glDeleteProgram(r->prog_world3d);
        r->prog_world3d = 0;
    }
    r->world_locs = OpenGL3WorldShaderLocs{};
}
