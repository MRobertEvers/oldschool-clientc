#version 300 es
precision highp float;

// Inputs from vertex shader
in vec2 v_texcoord;
in vec3 v_normal;
in vec3 v_color;
in float v_alpha;
in vec3 v_world_pos;

// Uniforms
uniform sampler2D u_texture;
uniform bool u_use_texture;
uniform vec3 u_light_direction;
uniform vec3 u_light_color;
uniform float u_ambient_strength;
uniform float u_light_intensity;

// Output
out vec4 frag_color;

// HSL to RGB conversion
vec3 hsl_to_rgb(vec3 hsl) {
    float h = hsl.x;
    float s = hsl.y;
    float l = hsl.z;
    
    float c = (1.0 - abs(2.0 * l - 1.0)) * s;
    float x = c * (1.0 - abs(mod(h * 6.0, 2.0) - 1.0));
    float m = l - c/2.0;
    
    vec3 rgb;
    if(h < 1.0/6.0) rgb = vec3(c, x, 0.0);
    else if(h < 2.0/6.0) rgb = vec3(x, c, 0.0);
    else if(h < 3.0/6.0) rgb = vec3(0.0, c, x);
    else if(h < 4.0/6.0) rgb = vec3(0.0, x, c);
    else if(h < 5.0/6.0) rgb = vec3(x, 0.0, c);
    else rgb = vec3(c, 0.0, x);
    
    return rgb + m;
}

void main() {
    // Start with base color (either white for textures or vertex color)
    vec4 base_color;
    if(u_use_texture) {
        base_color = vec4(1.0, 1.0, 1.0, v_alpha); // Draw textured surfaces as white
    } else {
        // Convert HSL to RGB if using vertex colors
        base_color = vec4(hsl_to_rgb(v_color), v_alpha);
    }
    
    // Calculate lighting
    vec3 normal = normalize(v_normal);
    float diffuse = max(dot(normal, normalize(-u_light_direction)), 0.0);
    
    // Combine ambient and diffuse lighting
    vec3 ambient = u_ambient_strength * u_light_color;
    vec3 diffuse_light = diffuse * u_light_color * u_light_intensity;
    vec3 lighting = ambient + diffuse_light;
    
    // Apply lighting to color
    vec3 final_color = base_color.rgb * lighting;
    
    // Output final color with alpha
    frag_color = vec4(final_color, base_color.a);
}
