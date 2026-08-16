attribute vec4 a_position;
attribute vec4 a_color;
attribute vec2 a_texcoord;
attribute float a_tex_id;
attribute float a_uv_mode;

uniform mat4 u_modelViewMatrix;
uniform mat4 u_projectionMatrix;

varying vec4 v_color;
varying vec2 v_texcoord;
varying float v_tex_id;
varying float v_uv_pack;

void main() {
    vec4 wp = vec4(a_position.xyz, 1.0);
    gl_Position = u_projectionMatrix * u_modelViewMatrix * wp;
    v_color = a_color;
    v_texcoord = a_texcoord;
    v_tex_id = a_tex_id;
    v_uv_pack = a_uv_mode;
}
