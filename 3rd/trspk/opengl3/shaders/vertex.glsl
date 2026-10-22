#version 150 core

layout(std140) uniform TRSPK_UboWorld {
    mat4 u_modelViewMatrix;
    mat4 u_projectionMatrix;
    float uClock;
    float uAtlasDim;
    float uAtlasSlots;
    float uDepthBias;
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
    // The face's depth bias (rev 241, class174.field2408 * 2, baked into w):
    // its camera depth less the bias decides the depth test, its position
    // does not move. The depth projection has w = camera depth, so clip z is
    // rebuilt from the biased depth, clamped to [-w, w] so a bias never
    // clips or unclips a corner.
    if (ubo.uDepthBias > 0.5 && a_position.w != 0.0) {
        float depth = gl_Position.w;
        float biased = max(depth - a_position.w, 1.0);
        float ndc = ubo.u_projectionMatrix[2][2] + ubo.u_projectionMatrix[3][2] / biased;
        gl_Position.z = clamp(ndc, -1.0, 1.0) * depth;
    }
    v_color = a_color;
    v_texcoord = a_texcoord;
    v_tex_id = a_tex_id;
    v_uv_pack = a_uv_mode;
}
