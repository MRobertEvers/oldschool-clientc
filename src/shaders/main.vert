#version 300 es

// Vertex attributes
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_texcoord;
layout(location = 2) in vec3 a_normal;
layout(location = 3) in vec3 a_color;
layout(location = 4) in float a_alpha;

// Uniforms for transformations
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform mat4 u_normal_matrix; // For normal transformation

// Uniforms for lighting
uniform vec3 u_light_direction;
uniform vec3 u_light_color;
uniform float u_ambient_strength;
uniform float u_light_intensity;

// Output to fragment shader
out vec2 v_texcoord;
out vec3 v_normal;
out vec3 v_color;
out float v_alpha;
out vec3 v_world_pos;

void main() {
    // Transform position
    vec4 world_pos = u_model * vec4(a_position, 1.0);
    v_world_pos = world_pos.xyz;
    gl_Position = u_projection * u_view * world_pos;
    
    // Transform normal
    v_normal = normalize(mat3(u_normal_matrix) * a_normal);
    
    // Pass through texture coordinates and color
    v_texcoord = a_texcoord;
    v_color = a_color;
    v_alpha = a_alpha;
}

