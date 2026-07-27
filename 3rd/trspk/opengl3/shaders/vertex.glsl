#version 150 core

layout(std140) uniform TRSPK_UboWorld {
    mat4 u_modelViewMatrix;
    mat4 u_projectionMatrix;
    float uClock;
    float uAtlasDim;
    float uAtlasSlots;
    float _pad;
} ubo;

in vec4 a_position;
in vec4 a_color;
in vec2 a_texcoord;
in float a_tex_id;
in float a_uv_mode;

out vec4 v_color;
out vec2 v_texcoord;
out float v_tex_id;
out float v_uv_pack;

void main() {
    vec4 wp = vec4(a_position.xyz, 1.0);
    gl_Position = ubo.u_projectionMatrix * ubo.u_modelViewMatrix * wp;
    v_color = a_color;
    v_texcoord = a_texcoord;
    v_tex_id = a_tex_id;
    v_uv_pack = a_uv_mode;
}
