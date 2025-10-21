
#include "pix3dgl.h"

#include "graphics/uv_pnm.h"
#include "scene_cache.h"
#include "shared_tables.h"

#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//
#ifdef __EMSCRIPTEN__
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#elif defined(__ANDROID__)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#elif defined(__APPLE__)
// Use regular OpenGL headers on macOS/iOS for development
#include <OpenGL/gl.h>
#include <OpenGL/gl3.h>
#define GL_GLEXT_PROTOTYPES
#else
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

#include <unordered_map>

#include <vector>

// Desktop OpenGL 3.0 Core vertex shader
const char* g_vertex_shader_core = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uViewMatrix;       // Precomputed on CPU
uniform mat4 uProjectionMatrix; // Precomputed on CPU
uniform mat4 uTextureMatrix;    // Optional texture coordinate transform
uniform mat4 uModelMatrix;      // Per-model transformation

out vec3 vColor;
out vec2 vTexCoord;

void main() {
    // Apply model transformation first, then view transformation
    vec4 worldPos = uModelMatrix * vec4(aPos, 1.0);
    
    // Transform vertex position using precomputed matrices
    vec4 viewPos = uViewMatrix * worldPos;
    gl_Position = uProjectionMatrix * viewPos;
    
    // Pass through the color
    vColor = aColor;
    
    // Transform texture coordinates if needed
    vec4 texCoord = vec4(aTexCoord, 0.0, 1.0);
    vec4 transformedTexCoord = uTextureMatrix * texCoord;
    vTexCoord = transformedTexCoord.xy / transformedTexCoord.w;
}
)";

// Desktop OpenGL 3.0 Core fragment shader
const char* g_fragment_shader_core = R"(
#version 330 core
in vec3 vColor;
in vec2 vTexCoord;
uniform sampler2D uTexture;
uniform bool uUseTexture;
out vec4 FragColor;

void main() {
    if (uUseTexture) {
        vec4 texColor = texture(uTexture, vTexCoord);
        // Make black texels transparent, preserve existing alpha
        float epsilon = 0.001; // Small threshold to account for floating point precision
        float luminance = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        if (luminance < epsilon) {
            // Discard completely transparent fragments so they don't write to depth buffer
            discard;
        }
        FragColor = vec4(texColor.rgb * vColor, texColor.a);
    } else {
        FragColor = vec4(vColor, 1.0);
    }
}
)";

enum FaceShadingType
{
    SHADING_TEXTURED = 0, // Face uses texture mapping
    SHADING_GOURAUD = 1,  // Face uses Gouraud shading (interpolated vertex colors)
    SHADING_FLAT = 2      // Face uses flat shading (single color)
};

struct GLModel
{
    int idx;
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    GLuint VAO; // Use VAO on desktop platforms for better performance
#endif
    GLuint VBO;
    GLuint colorVBO;
    GLuint texCoordVBO;
    GLuint EBO;

    int face_count;
    bool has_textures;
    int first_texture_id;           // First texture ID used by this model (for simple rendering)
    std::vector<int> face_textures; // Texture ID per face (-1 = no texture)
    std::vector<bool> face_visible; // Whether each face should be drawn
    std::vector<FaceShadingType> face_shading; // Shading type per face
};

struct GLTile
{
    int idx;
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    GLuint VAO;
#endif
    GLuint VBO;
    GLuint colorVBO;
    GLuint texCoordVBO;
    GLuint EBO;

    int face_count;
    std::vector<int> face_textures;            // Texture ID per face
    std::vector<bool> face_visible;            // Whether each face should be drawn
    std::vector<FaceShadingType> face_shading; // Shading type per face
};

// Batch drawing for models - accumulate faces and draw in batches
struct DrawBatch
{
    int texture_id;                   // -1 for untextured
    std::vector<GLint> face_starts;   // Starting vertex index for each face
    std::vector<GLsizei> face_counts; // Vertex count for each face (always 3)
};

struct Pix3DGL
{
    GLuint vertex_shader_es2;
    GLuint fragment_shader_es2;
    GLuint program_es2;

    std::unordered_map<int, GLuint> texture_ids;
    std::unordered_map<int, GLModel> models;
    std::unordered_map<int, GLTile> tiles;

    // For fast face drawing - stores the currently active model
    GLModel* current_model;
    int current_model_idx;

    // Cached uniform locations for performance (to avoid expensive glGetUniformLocation calls)
    GLint uniform_model_matrix;
    GLint uniform_view_matrix;
    GLint uniform_projection_matrix;
    GLint uniform_use_texture;
    GLint uniform_texture;
    GLint uniform_texture_matrix;

    // State tracking to avoid redundant GL calls
    GLuint currently_bound_texture;
    int current_texture_state; // 0 = texture disabled, 1 = texture enabled

    // Batching system - accumulate faces by texture and draw together
    std::unordered_map<int, DrawBatch> draw_batches; // Key: texture_id (-1 for untextured)
};

// CPU-side matrix computation functions for performance
static void
compute_view_matrix(
    float* out_matrix, float camera_x, float camera_y, float camera_z, float pitch, float yaw)
{
    float cosPitch = cos(-pitch);
    float sinPitch = sin(-pitch);
    float cosYaw = cos(-yaw);
    float sinYaw = sin(-yaw);

    // Combined rotation * translation matrix (column-major for OpenGL)
    // First apply translation, then yaw, then pitch
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

static void
compute_projection_matrix(float* out_matrix, float fov, float screen_width, float screen_height)
{
    float y = 1.0f / tan(fov * 0.5f);
    float x = y;

    // Column-major for OpenGL
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

extern "C" void
pix3dgl_model_load_textured_pnm(
    struct Pix3DGL* pix3dgl,
    int idx,
    int* vertices_x,
    int* vertices_y,
    int* vertices_z,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int face_count,
    int* face_infos,
    int* face_alphas,
    int* face_textures,
    int* face_texture_faces_nullable,
    int* face_p_coordinate_nullable,
    int* face_m_coordinate_nullable,
    int* face_n_coordinate_nullable,
    int* face_colors_a,
    int* face_colors_b,
    int* face_colors_c)
{
    GLModel gl_model;
    gl_model.idx = idx;
    gl_model.face_count = face_count;
    gl_model.has_textures = true;
    gl_model.first_texture_id = -1;

    // Store face texture IDs, shading types, and visibility
    gl_model.face_textures.resize(face_count);
    gl_model.face_visible.resize(face_count);
    gl_model.face_shading.resize(face_count);
    for( int face = 0; face < face_count; face++ )
    {
        gl_model.face_textures[face] = face_textures ? face_textures[face] : -1;

        // Check if face should be drawn
        // Don't draw if face_infos == 2 or face_colors_c == -2
        bool should_draw = true;
        if( face_infos && face_infos[face] == 2 )
        {
            should_draw = false;
        }
        if( face_colors_c && face_colors_c[face] == -2 )
        {
            should_draw = false;
        }
        gl_model.face_visible[face] = should_draw;

        // Determine shading type based on texture and color data
        if( face_textures && face_textures[face] != -1 )
        {
            gl_model.face_shading[face] = SHADING_TEXTURED;
        }
        else if( face_colors_c && face_colors_c[face] == -1 )
        {
            // face_colors_c == -1 means flat shading (all vertices same color)
            gl_model.face_shading[face] = SHADING_FLAT;
        }
        else
        {
            // Gouraud shading (interpolated vertex colors)
            gl_model.face_shading[face] = SHADING_GOURAUD;
        }

        // Find the first valid texture ID
        if( face_textures && face_textures[face] != -1 && gl_model.first_texture_id == -1 )
        {
            gl_model.first_texture_id = face_textures[face];
        }
    }

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // Create VAO on desktop platforms for better performance
    glGenVertexArrays(1, &gl_model.VAO);
    glBindVertexArray(gl_model.VAO);
#endif

    // Create buffers
    glGenBuffers(1, &gl_model.VBO);
    glGenBuffers(1, &gl_model.colorVBO);
    glGenBuffers(1, &gl_model.texCoordVBO);
    glGenBuffers(1, &gl_model.EBO);

    // Allocate arrays for vertex data
    std::vector<float> vertices(face_count * 9);  // 3 vertices * 3 coordinates per face
    std::vector<float> colors(face_count * 9);    // 3 vertices * 3 colors per face
    std::vector<float> texCoords(face_count * 6); // 3 vertices * 2 UV coords per face

    // Track min/max for debugging
    float min_x = 1e6, max_x = -1e6, min_y = 1e6, max_y = -1e6, min_z = 1e6, max_z = -1e6;

    // Build vertex, color, and texture coordinate data
    for( int face = 0; face < face_count; face++ )
    {
        // Get the vertex indices for this face
        int v_a = face_indices_a[face];
        int v_b = face_indices_b[face];
        int v_c = face_indices_c[face];

        // Get vertex positions
        vertices[face * 9 + 0] = vertices_x[v_a];
        vertices[face * 9 + 1] = vertices_y[v_a];
        vertices[face * 9 + 2] = vertices_z[v_a];

        vertices[face * 9 + 3] = vertices_x[v_b];
        vertices[face * 9 + 4] = vertices_y[v_b];
        vertices[face * 9 + 5] = vertices_z[v_b];

        vertices[face * 9 + 6] = vertices_x[v_c];
        vertices[face * 9 + 7] = vertices_y[v_c];
        vertices[face * 9 + 8] = vertices_z[v_c];

        // Track bounds
        for( int i = 0; i < 3; i++ )
        {
            float x = vertices[face * 9 + i * 3];
            float y = vertices[face * 9 + i * 3 + 1];
            float z = vertices[face * 9 + i * 3 + 2];
            if( x < min_x )
                min_x = x;
            if( x > max_x )
                max_x = x;
            if( y < min_y )
                min_y = y;
            if( y > max_y )
                max_y = y;
            if( z < min_z )
                min_z = z;
            if( z > max_z )
                max_z = z;
        }

        // Get vertex colors (HSL to RGB conversion)
        int hsl_a = face_colors_a[face];
        int hsl_b = face_colors_b[face];
        int hsl_c = face_colors_c[face];

        // Check if this is flat shading (face_colors_c == -1)
        // If so, use face_colors_a for all three vertices
        int rgb_a, rgb_b, rgb_c;
        if( hsl_c == -1 )
        {
            // Flat shading - all vertices use the same color
            rgb_a = rgb_b = rgb_c = g_hsl16_to_rgb_table[hsl_a];
        }
        else
        {
            // Gouraud shading - each vertex has its own color
            rgb_a = g_hsl16_to_rgb_table[hsl_a];
            rgb_b = g_hsl16_to_rgb_table[hsl_b];
            rgb_c = g_hsl16_to_rgb_table[hsl_c];
        }

        // Store colors for this face's vertices
        colors[face * 9 + 0] = ((rgb_a >> 16) & 0xFF) / 255.0f;
        colors[face * 9 + 1] = ((rgb_a >> 8) & 0xFF) / 255.0f;
        colors[face * 9 + 2] = (rgb_a & 0xFF) / 255.0f;

        colors[face * 9 + 3] = ((rgb_b >> 16) & 0xFF) / 255.0f;
        colors[face * 9 + 4] = ((rgb_b >> 8) & 0xFF) / 255.0f;
        colors[face * 9 + 5] = (rgb_b & 0xFF) / 255.0f;

        colors[face * 9 + 6] = ((rgb_c >> 16) & 0xFF) / 255.0f;
        colors[face * 9 + 7] = ((rgb_c >> 8) & 0xFF) / 255.0f;
        colors[face * 9 + 8] = (rgb_c & 0xFF) / 255.0f;

        // Compute UV coordinates using PNM method
        // Determine which vertices define the texture space (P, M, N)
        int texture_face = -1;
        int tp_vertex = -1;
        int tm_vertex = -1;
        int tn_vertex = -1;

        if( face_textures[face] != -1 )
        {
            if( face_texture_faces_nullable && face_texture_faces_nullable[face] != -1 )
            {
                // Use custom texture space definition
                texture_face = face_texture_faces_nullable[face];
                tp_vertex = face_p_coordinate_nullable[texture_face];
                tm_vertex = face_m_coordinate_nullable[texture_face];
                tn_vertex = face_n_coordinate_nullable[texture_face];
            }
            else
            {
                // Default: use the face's own vertices as texture space
                tp_vertex = v_a;
                tm_vertex = v_b;
                tn_vertex = v_c;
            }

            // Get the PNM triangle vertices that define texture space
            float p_x = vertices_x[tp_vertex];
            float p_y = vertices_y[tp_vertex];
            float p_z = vertices_z[tp_vertex];

            float m_x = vertices_x[tm_vertex];
            float m_y = vertices_y[tm_vertex];
            float m_z = vertices_z[tm_vertex];

            float n_x = vertices_x[tn_vertex];
            float n_y = vertices_y[tn_vertex];
            float n_z = vertices_z[tn_vertex];

            // Get the actual face vertices (A, B, C) for UV computation
            float a_x = vertices_x[v_a];
            float a_y = vertices_y[v_a];
            float a_z = vertices_z[v_a];

            float b_x = vertices_x[v_b];
            float b_y = vertices_y[v_b];
            float b_z = vertices_z[v_b];

            float c_x = vertices_x[v_c];
            float c_y = vertices_y[v_c];
            float c_z = vertices_z[v_c];

            // Compute UV coordinates
            struct UVFaceCoords uv_pnm;
            uv_pnm_compute(
                &uv_pnm,
                p_x,
                p_y,
                p_z,
                m_x,
                m_y,
                m_z,
                n_x,
                n_y,
                n_z,
                a_x,
                a_y,
                a_z,
                b_x,
                b_y,
                b_z,
                c_x,
                c_y,
                c_z);

            // Store UV coordinates
            texCoords[face * 6 + 0] = uv_pnm.u1;
            texCoords[face * 6 + 1] = uv_pnm.v1;
            texCoords[face * 6 + 2] = uv_pnm.u2;
            texCoords[face * 6 + 3] = uv_pnm.v2;
            texCoords[face * 6 + 4] = uv_pnm.u3;
            texCoords[face * 6 + 5] = uv_pnm.v3;
        }
    }

    printf(
        "Textured Model %d bounds: X[%.1f, %.1f] Y[%.1f, %.1f] Z[%.1f, %.1f]\n",
        idx,
        min_x,
        max_x,
        min_y,
        max_y,
        min_z,
        max_z);

    // Upload vertex data to GPU
    glBindBuffer(GL_ARRAY_BUFFER, gl_model.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // Set up vertex attributes when using VAO on desktop
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
#endif

    // Upload color data to GPU
    glBindBuffer(GL_ARRAY_BUFFER, gl_model.colorVBO);
    glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(float), colors.data(), GL_STATIC_DRAW);
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // Set up color attributes when using VAO on desktop
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
#endif

    // Upload texture coordinate data to GPU
    glBindBuffer(GL_ARRAY_BUFFER, gl_model.texCoordVBO);
    glBufferData(
        GL_ARRAY_BUFFER, texCoords.size() * sizeof(float), texCoords.data(), GL_STATIC_DRAW);
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // Set up texture coordinate attributes when using VAO on desktop
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(2);

    // Unbind VAO to avoid accidental modifications
    glBindVertexArray(0);
#endif

    // Store the model in the map
    pix3dgl->models[idx] = gl_model;
}

extern "C" void
pix3dgl_model_load(
    struct Pix3DGL* pix3dgl,
    int idx,
    int* vertices_x,
    int* vertices_y,
    int* vertices_z,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int face_count,
    int* face_alphas,
    int* face_colors_a,
    int* face_colors_b,
    int* face_colors_c)
{
    GLModel gl_model;
    gl_model.idx = idx;
    gl_model.face_count = face_count;
    gl_model.has_textures = false;
    gl_model.first_texture_id = -1;

    // No textures in this model
    gl_model.face_textures.resize(face_count, -1);

    // Check which faces should be visible and determine shading type
    gl_model.face_visible.resize(face_count);
    gl_model.face_shading.resize(face_count);
    for( int face = 0; face < face_count; face++ )
    {
        gl_model.face_visible[face] = !(face_colors_c && face_colors_c[face] == -2);

        // Determine shading type based on color data
        if( face_colors_c && face_colors_c[face] == -1 )
        {
            // face_colors_c == -1 means flat shading (all vertices same color)
            gl_model.face_shading[face] = SHADING_FLAT;
        }
        else
        {
            // Gouraud shading (interpolated vertex colors)
            gl_model.face_shading[face] = SHADING_GOURAUD;
        }
    }

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // Create VAO on desktop platforms for better performance
    glGenVertexArrays(1, &gl_model.VAO);
    glBindVertexArray(gl_model.VAO);
#endif

    // Create buffers
    glGenBuffers(1, &gl_model.VBO);
    glGenBuffers(1, &gl_model.colorVBO);
    glGenBuffers(1, &gl_model.texCoordVBO);
    glGenBuffers(1, &gl_model.EBO);

    std::vector<float> vertices(face_count * 9); // 3 vertices * 3 coordinates per face

    // Track min/max for debugging
    float min_x = 1e6, max_x = -1e6, min_y = 1e6, max_y = -1e6, min_z = 1e6, max_z = -1e6;

    for( int face = 0; face < face_count; face++ )
    {
        int v_a = face_indices_a[face];
        int v_b = face_indices_b[face];
        int v_c = face_indices_c[face];

        vertices[face * 9] = vertices_x[v_a];
        vertices[face * 9 + 1] = vertices_y[v_a];
        vertices[face * 9 + 2] = vertices_z[v_a];

        vertices[face * 9 + 3] = vertices_x[v_b];
        vertices[face * 9 + 4] = vertices_y[v_b];
        vertices[face * 9 + 5] = vertices_z[v_b];

        vertices[face * 9 + 6] = vertices_x[v_c];
        vertices[face * 9 + 7] = vertices_y[v_c];
        vertices[face * 9 + 8] = vertices_z[v_c];

        // Track bounds
        for( int i = 0; i < 3; i++ )
        {
            float x = vertices[face * 9 + i * 3];
            float y = vertices[face * 9 + i * 3 + 1];
            float z = vertices[face * 9 + i * 3 + 2];
            if( x < min_x )
                min_x = x;
            if( x > max_x )
                max_x = x;
            if( y < min_y )
                min_y = y;
            if( y > max_y )
                max_y = y;
            if( z < min_z )
                min_z = z;
            if( z > max_z )
                max_z = z;
        }
    }

    printf(
        "Model %d bounds: X[%.1f, %.1f] Y[%.1f, %.1f] Z[%.1f, %.1f]\n",
        idx,
        min_x,
        max_x,
        min_y,
        max_y,
        min_z,
        max_z);

    glBindBuffer(GL_ARRAY_BUFFER, gl_model.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // Set up vertex attributes when using VAO on desktop
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
#endif

    // Create and setup color buffer - one color per vertex per face
    std::vector<float> colors(face_count * 9); // 3 vertices * 3 colors per face

    for( int face = 0; face < face_count; face++ )
    {
        int a_x = face_indices_a[face];
        int a_y = face_indices_a[face];
        int a_z = face_indices_a[face];

        int b_x = face_indices_b[face];
        int b_y = face_indices_b[face];
        int b_z = face_indices_b[face];

        int c_x = face_indices_c[face];
        int c_y = face_indices_c[face];
        int c_z = face_indices_c[face];

        // Get the HSL colors for each vertex of the face
        int hsl_a = face_colors_a[face];
        int hsl_b = face_colors_b[face];
        int hsl_c = face_colors_c[face];

        // Check if this is flat shading (face_colors_c == -1)
        // If so, use face_colors_a for all three vertices
        int rgb_a, rgb_b, rgb_c;
        if( hsl_c == -1 )
        {
            // Flat shading - all vertices use the same color
            rgb_a = rgb_b = rgb_c = g_hsl16_to_rgb_table[hsl_a];
        }
        else
        {
            // Gouraud shading - each vertex has its own color
            rgb_a = g_hsl16_to_rgb_table[hsl_a];
            rgb_b = g_hsl16_to_rgb_table[hsl_b];
            rgb_c = g_hsl16_to_rgb_table[hsl_c];
        }

        // Store colors for this face's vertices
        // First vertex color
        colors[face * 9] = ((rgb_a >> 16) & 0xFF) / 255.0f;
        colors[face * 9 + 1] = ((rgb_a >> 8) & 0xFF) / 255.0f;
        colors[face * 9 + 2] = (rgb_a & 0xFF) / 255.0f;

        // Second vertex color
        colors[face * 9 + 3] = ((rgb_b >> 16) & 0xFF) / 255.0f;
        colors[face * 9 + 4] = ((rgb_b >> 8) & 0xFF) / 255.0f;
        colors[face * 9 + 5] = (rgb_b & 0xFF) / 255.0f;

        // Third vertex color
        colors[face * 9 + 6] = ((rgb_c >> 16) & 0xFF) / 255.0f;
        colors[face * 9 + 7] = ((rgb_c >> 8) & 0xFF) / 255.0f;
        colors[face * 9 + 8] = (rgb_c & 0xFF) / 255.0f;
    }

    glBindBuffer(GL_ARRAY_BUFFER, gl_model.colorVBO);
    glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(float), colors.data(), GL_STATIC_DRAW);
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // Set up color attributes when using VAO on desktop
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    // Unbind VAO to avoid accidental modifications
    glBindVertexArray(0);
#endif

    // Store the model in the map
    pix3dgl->models[idx] = gl_model;
}

// Helper function to compile shaders
static GLuint
compile_shader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if( !success )
    {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        printf("Shader compilation error: %s\n", infoLog);
        return 0;
    }
    return shader;
}

// Helper function to create shader program
static GLuint
create_shader_program(const char* vertex_source, const char* fragment_source)
{
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);

    if( !vertex_shader || !fragment_shader )
    {
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if( !success )
    {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        printf("Shader program linking error: %s\n", infoLog);
        return 0;
    }

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    return program;
}

extern "C" struct Pix3DGL*
pix3dgl_new()
{
    struct Pix3DGL* pix3dgl = new Pix3DGL();

    // Initialize current model tracking
    pix3dgl->current_model = nullptr;
    pix3dgl->current_model_idx = -1;

    // Initialize state tracking
    pix3dgl->currently_bound_texture = 0;
    pix3dgl->current_texture_state = -1; // -1 = unknown

    // Initialize OpenGL shaders - Use appropriate shaders for platform
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    // For WebGL and Android, use ES2 shaders
    pix3dgl->program_es2 = create_shader_program(g_vertex_shader_es2, g_fragment_shader_es2);
#else
    // For desktop platforms, use Core Profile shaders (but still ES2-compatible rendering)
    pix3dgl->program_es2 = create_shader_program(g_vertex_shader_core, g_fragment_shader_core);
#endif

    if( !pix3dgl->program_es2 )
    {
        printf("Failed to create shader program\n");
        delete pix3dgl;
        return nullptr;
    }

    // Cache ALL uniform locations for performance (avoids expensive string lookups per-frame)
    pix3dgl->uniform_model_matrix = glGetUniformLocation(pix3dgl->program_es2, "uModelMatrix");
    pix3dgl->uniform_view_matrix = glGetUniformLocation(pix3dgl->program_es2, "uViewMatrix");
    pix3dgl->uniform_projection_matrix =
        glGetUniformLocation(pix3dgl->program_es2, "uProjectionMatrix");
    pix3dgl->uniform_use_texture = glGetUniformLocation(pix3dgl->program_es2, "uUseTexture");
    pix3dgl->uniform_texture = glGetUniformLocation(pix3dgl->program_es2, "uTexture");
    pix3dgl->uniform_texture_matrix = glGetUniformLocation(pix3dgl->program_es2, "uTextureMatrix");

    printf(
        "Cached uniform locations - model_matrix:%d view_matrix:%d projection_matrix:%d "
        "use_texture:%d texture:%d\n",
        pix3dgl->uniform_model_matrix,
        pix3dgl->uniform_view_matrix,
        pix3dgl->uniform_projection_matrix,
        pix3dgl->uniform_use_texture,
        pix3dgl->uniform_texture);

    // Enable alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Print OpenGL info to verify hardware acceleration
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* vendor = glGetString(GL_VENDOR);
    printf("========================================\n");
    printf("OpenGL Renderer: %s\n", renderer);
    printf("OpenGL Version: %s\n", version);
    printf("OpenGL Vendor: %s\n", vendor);
    printf("========================================\n");

    // Check if we're using software rendering (this would be a problem!)
    const char* renderer_str = (const char*)renderer;
    if( strstr(renderer_str, "Software") || strstr(renderer_str, "software") ||
        strstr(renderer_str, "llvmpipe") || strstr(renderer_str, "SwiftShader") )
    {
        printf("WARNING: Software rendering detected! Hardware acceleration may not be working!\n");
    }

    printf("Pix3DGL initialized successfully with shader program ID: %d\n", pix3dgl->program_es2);
    return pix3dgl;
}

extern "C" void
pix3dgl_load_texture(
    struct Pix3DGL* pix3dgl,
    int texture_id,
    struct TexturesCache* textures_cache,
    struct Cache* cache)
{
    // Check if texture is already loaded
    if( pix3dgl->texture_ids.find(texture_id) != pix3dgl->texture_ids.end() )
    {
        return; // Texture already loaded
    }

    // Load texture from cache
    struct Texture* cache_tex = textures_cache_checkout(
        textures_cache,
        cache,
        texture_id,
        128, // Size
        1.0  // Default gamma
    );

    if( !cache_tex )
    {
        printf("Failed to load texture %d from cache\n", texture_id);
        return;
    }

    // Create OpenGL texture
    GLuint gl_texture_id;
    glGenTextures(1, &gl_texture_id);
    glBindTexture(GL_TEXTURE_2D, gl_texture_id);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // Upload texture data - convert from RGBA int array to byte array
    std::vector<unsigned char> texture_bytes(cache_tex->width * cache_tex->height * 4);
    for( int i = 0; i < cache_tex->width * cache_tex->height; i++ )
    {
        int pixel = cache_tex->texels[i];
        texture_bytes[i * 4 + 0] = (pixel >> 16) & 0xFF; // R
        texture_bytes[i * 4 + 1] = (pixel >> 8) & 0xFF;  // G
        texture_bytes[i * 4 + 2] = pixel & 0xFF;         // B
        texture_bytes[i * 4 + 3] = (pixel >> 24) & 0xFF; // A
    }

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        cache_tex->width,
        cache_tex->height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        texture_bytes.data());

    // Store texture ID in map
    pix3dgl->texture_ids[texture_id] = gl_texture_id;

    printf(
        "Loaded texture %d as OpenGL texture %u (%dx%d)\n",
        texture_id,
        gl_texture_id,
        cache_tex->width,
        cache_tex->height);

    // Clean up cache texture
    textures_cache_checkin(textures_cache, cache_tex);

    // Check for OpenGL errors
    GLenum error = glGetError();
    if( error != GL_NO_ERROR )
    {
        printf("OpenGL error in pix3dgl_load_texture: 0x%x\n", error);
    }
}

extern "C" void
pix3dgl_render_with_camera(
    struct Pix3DGL* pix3dgl,
    float camera_x,
    float camera_y,
    float camera_z,
    float camera_pitch,
    float camera_yaw,
    float screen_width,
    float screen_height)
{
    if( !pix3dgl || !pix3dgl->program_es2 )
    {
        printf("Pix3DGL not properly initialized\n");
        return;
    }

    // Use our shader program
    glUseProgram(pix3dgl->program_es2);

    // Set viewport (this might be missing!)
    glViewport(0, 0, (GLsizei)screen_width, (GLsizei)screen_height);

    // Convert camera angles from game units to radians
    // Game uses 2048 units per full rotation (2π radians)
    float pitch_radians = (camera_pitch * 2.0f * M_PI) / 2048.0f;
    float yaw_radians = (camera_yaw * 2.0f * M_PI) / 2048.0f;

    // Set up uniforms with actual camera parameters
    GLint rotationXLoc = glGetUniformLocation(pix3dgl->program_es2, "uRotationX");
    GLint rotationYLoc = glGetUniformLocation(pix3dgl->program_es2, "uRotationY");
    GLint screenWidthLoc = glGetUniformLocation(pix3dgl->program_es2, "uScreenWidth");
    GLint screenHeightLoc = glGetUniformLocation(pix3dgl->program_es2, "uScreenHeight");
    GLint cameraPosLoc = glGetUniformLocation(pix3dgl->program_es2, "uCameraPos");

    if( rotationXLoc >= 0 )
        glUniform1f(rotationXLoc, pitch_radians);
    if( rotationYLoc >= 0 )
        glUniform1f(rotationYLoc, yaw_radians);
    if( screenWidthLoc >= 0 )
        glUniform1f(screenWidthLoc, screen_width);
    if( screenHeightLoc >= 0 )
        glUniform1f(screenHeightLoc, screen_height);
    if( cameraPosLoc >= 0 )
        glUniform3f(cameraPosLoc, camera_x, camera_y, camera_z);

    // Set identity matrix for texture coordinates
    GLint textureMatrixLoc = glGetUniformLocation(pix3dgl->program_es2, "uTextureMatrix");
    if( textureMatrixLoc >= 0 )
    {
        float identity[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
        glUniformMatrix4fv(textureMatrixLoc, 1, GL_FALSE, identity);
    }
    // Check if we have any models to render
    if( pix3dgl->models.empty() )
    {
        printf("No models loaded to render!\n");
        return;
    }

    // Render all models
    for( auto& pair : pix3dgl->models )
    {
        GLModel& model = pair.second;

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
        // Desktop: Use VAO for better performance
        glBindVertexArray(model.VAO);
        glDrawArrays(GL_TRIANGLES, 0, model.face_count * 3);
        glBindVertexArray(0);
#else
        // Mobile/WebGL: Manually set up vertex attributes each time
        // Set up position attribute (location 0)
        glBindBuffer(GL_ARRAY_BUFFER, model.VBO);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        // Set up color attribute (location 1)
        glBindBuffer(GL_ARRAY_BUFFER, model.colorVBO);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);

        // Draw the model (render all triangles)
        glDrawArrays(GL_TRIANGLES, 0, model.face_count * 3);

        // Disable vertex attributes
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
#endif
    }

    // Check for OpenGL errors
    GLenum error = glGetError();
    if( error != GL_NO_ERROR )
    {
        printf("OpenGL error in pix3dgl_render_with_camera: 0x%x\n", error);
    }
}

extern "C" void
pix3dgl_render(struct Pix3DGL* pix3dgl)
{
    // Default camera parameters for backward compatibility
    pix3dgl_render_with_camera(pix3dgl, 0.0f, -120.0f, -1100.0f, 0.1f, 0.0f, 800.0f, 600.0f);
}

extern "C" void
pix3dgl_begin_frame(
    struct Pix3DGL* pix3dgl,
    float camera_x,
    float camera_y,
    float camera_z,
    float camera_pitch,
    float camera_yaw,
    float screen_width,
    float screen_height)
{
    if( !pix3dgl || !pix3dgl->program_es2 )
    {
        printf("Pix3DGL not properly initialized\n");
        return;
    }

    // Use our shader program
    glUseProgram(pix3dgl->program_es2);

    // Set viewport
    glViewport(0, 0, (GLsizei)screen_width, (GLsizei)screen_height);

    // Enable depth testing and alpha blending
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Convert camera angles from game units to radians
    // Game uses 2048 units per full rotation (2π radians)
    float pitch_radians = (camera_pitch * 2.0f * M_PI) / 2048.0f;
    float yaw_radians = (camera_yaw * 2.0f * M_PI) / 2048.0f;

    // OPTIMIZATION: Compute view and projection matrices on CPU once per frame
    float view_matrix[16];
    float projection_matrix[16];

    compute_view_matrix(view_matrix, camera_x, camera_y, camera_z, pitch_radians, yaw_radians);
    compute_projection_matrix(
        projection_matrix, (90.0f * M_PI) / 180.0f, screen_width, screen_height);

    // Set matrices as uniforms
    if( pix3dgl->uniform_view_matrix >= 0 )
    {
        glUniformMatrix4fv(pix3dgl->uniform_view_matrix, 1, GL_FALSE, view_matrix);
    }
    if( pix3dgl->uniform_projection_matrix >= 0 )
    {
        glUniformMatrix4fv(pix3dgl->uniform_projection_matrix, 1, GL_FALSE, projection_matrix);
    }

    // Set identity matrix for texture coordinates
    if( pix3dgl->uniform_texture_matrix >= 0 )
    {
        float identity[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
        glUniformMatrix4fv(pix3dgl->uniform_texture_matrix, 1, GL_FALSE, identity);
    }

    // Reset state tracking for new frame
    pix3dgl->currently_bound_texture = 0;
    pix3dgl->current_texture_state = -1;

    // Clear batching data for new frame
    pix3dgl->draw_batches.clear();

    // Disable texture usage by default (will be enabled for textured models later)
    if( pix3dgl->uniform_use_texture >= 0 )
    {
        glUniform1i(pix3dgl->uniform_use_texture, 0);
        pix3dgl->current_texture_state = 0;
    }
}

extern "C" void
pix3dgl_model_draw(
    struct Pix3DGL* pix3dgl,
    int model_idx,
    float position_x,
    float position_y,
    float position_z,
    float yaw)
{
    if( !pix3dgl || !pix3dgl->program_es2 )
    {
        printf("Pix3DGL not properly initialized\n");
        return;
    }

    // Find the model
    auto it = pix3dgl->models.find(model_idx);
    if( it == pix3dgl->models.end() )
    {
        printf("Model %d not loaded\n", model_idx);
        return;
    }

    GLModel& model = it->second;

    // Create model transformation matrix (rotation around Y axis + translation)
    float cos_yaw = cos(yaw);
    float sin_yaw = sin(yaw);

    // Model matrix: first rotate around Y axis, then translate
    float modelMatrix[16] = { cos_yaw,    0.0f,       sin_yaw,    0.0f, 0.0f,    1.0f,
                              0.0f,       0.0f,       -sin_yaw,   0.0f, cos_yaw, 0.0f,
                              position_x, position_y, position_z, 1.0f };

    // Set model matrix uniform (using cached location)
    if( pix3dgl->uniform_model_matrix >= 0 )
    {
        glUniformMatrix4fv(pix3dgl->uniform_model_matrix, 1, GL_FALSE, modelMatrix);
    }

    // Enable/disable texture based on model (using cached locations and state tracking)
    if( model.has_textures && model.first_texture_id != -1 )
    {
        // Find the OpenGL texture ID for this texture
        auto tex_it = pix3dgl->texture_ids.find(model.first_texture_id);
        if( tex_it != pix3dgl->texture_ids.end() )
        {
            GLuint gl_texture_id = tex_it->second;

            // Only bind texture if it's different from currently bound
            if( pix3dgl->currently_bound_texture != gl_texture_id )
            {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, gl_texture_id);
                pix3dgl->currently_bound_texture = gl_texture_id;
            }

            // Enable texture usage
            if( pix3dgl->current_texture_state != 1 )
            {
                if( pix3dgl->uniform_use_texture >= 0 )
                {
                    glUniform1i(pix3dgl->uniform_use_texture, 1);
                    pix3dgl->current_texture_state = 1;
                }
            }
        }
        else
        {
            // Texture not loaded, disable texturing
            if( pix3dgl->current_texture_state != 0 )
            {
                if( pix3dgl->uniform_use_texture >= 0 )
                {
                    glUniform1i(pix3dgl->uniform_use_texture, 0);
                    pix3dgl->current_texture_state = 0;
                }
            }
        }
    }
    else
    {
        // No textures, disable texturing
        if( pix3dgl->current_texture_state != 0 )
        {
            if( pix3dgl->uniform_use_texture >= 0 )
            {
                glUniform1i(pix3dgl->uniform_use_texture, 0);
                pix3dgl->current_texture_state = 0;
            }
        }
    }

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // Desktop: Use VAO for better performance
    glBindVertexArray(model.VAO);
    glDrawArrays(GL_TRIANGLES, 0, model.face_count * 3);
    glBindVertexArray(0);
#else
    // Mobile/WebGL: Manually set up vertex attributes each time
    // Set up position attribute (location 0)
    glBindBuffer(GL_ARRAY_BUFFER, model.VBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Set up color attribute (location 1)
    glBindBuffer(GL_ARRAY_BUFFER, model.colorVBO);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    // Set up texture coordinate attribute (location 2) if model has textures
    if( model.has_textures )
    {
        glBindBuffer(GL_ARRAY_BUFFER, model.texCoordVBO);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(2);
    }

    // Draw the model (render all triangles)
    glDrawArrays(GL_TRIANGLES, 0, model.face_count * 3);

    // Disable vertex attributes
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    if( model.has_textures )
    {
        glDisableVertexAttribArray(2);
    }
#endif

    // Check for OpenGL errors
    // GLenum error = glGetError();
    // if( error != GL_NO_ERROR )
    // {
    //     printf("OpenGL error in pix3dgl_model_draw: 0x%x\n", error);
    // }
}

extern "C" void
pix3dgl_model_draw_face(
    struct Pix3DGL* pix3dgl,
    int model_idx,
    int face_idx,
    float position_x,
    float position_y,
    float position_z,
    float yaw)
{
    if( !pix3dgl || !pix3dgl->program_es2 )
    {
        printf("Pix3DGL not properly initialized\n");
        return;
    }

    // Find the model
    auto it = pix3dgl->models.find(model_idx);
    if( it == pix3dgl->models.end() )
    {
        printf("Model %d not loaded\n", model_idx);
        return;
    }

    GLModel& model = it->second;

    // Validate face index
    if( face_idx < 0 || face_idx >= model.face_count )
    {
        printf(
            "Invalid face index %d for model %d (face_count=%d)\n",
            face_idx,
            model_idx,
            model.face_count);
        return;
    }

    // Create model transformation matrix (rotation around Y axis + translation)
    float cos_yaw = cos(yaw);
    float sin_yaw = sin(yaw);

    // Model matrix: first rotate around Y axis, then translate
    float modelMatrix[16] = { cos_yaw,    0.0f,       sin_yaw,    0.0f, 0.0f,    1.0f,
                              0.0f,       0.0f,       -sin_yaw,   0.0f, cos_yaw, 0.0f,
                              position_x, position_y, position_z, 1.0f };

    // Set model matrix uniform (using cached location)
    if( pix3dgl->uniform_model_matrix >= 0 )
    {
        glUniformMatrix4fv(pix3dgl->uniform_model_matrix, 1, GL_FALSE, modelMatrix);
    }

    // Enable/disable texture based on model (using cached locations and state tracking)
    if( model.has_textures && model.first_texture_id != -1 )
    {
        // Find the OpenGL texture ID for this texture
        auto tex_it = pix3dgl->texture_ids.find(model.first_texture_id);
        if( tex_it != pix3dgl->texture_ids.end() )
        {
            GLuint gl_texture_id = tex_it->second;

            // Only bind texture if it's different from currently bound
            if( pix3dgl->currently_bound_texture != gl_texture_id )
            {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, gl_texture_id);
                pix3dgl->currently_bound_texture = gl_texture_id;
            }

            // Enable texture usage
            if( pix3dgl->current_texture_state != 1 )
            {
                if( pix3dgl->uniform_use_texture >= 0 )
                {
                    glUniform1i(pix3dgl->uniform_use_texture, 1);
                    pix3dgl->current_texture_state = 1;
                }
            }
        }
        else
        {
            // Texture not loaded, disable texturing
            if( pix3dgl->current_texture_state != 0 )
            {
                if( pix3dgl->uniform_use_texture >= 0 )
                {
                    glUniform1i(pix3dgl->uniform_use_texture, 0);
                    pix3dgl->current_texture_state = 0;
                }
            }
        }
    }
    else
    {
        // No textures, disable texturing
        if( pix3dgl->current_texture_state != 0 )
        {
            if( pix3dgl->uniform_use_texture >= 0 )
            {
                glUniform1i(pix3dgl->uniform_use_texture, 0);
                pix3dgl->current_texture_state = 0;
            }
        }
    }

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // Desktop: Use VAO for better performance
    glBindVertexArray(model.VAO);
    // Draw only the specified face (3 vertices starting at face_idx * 3)
    glDrawArrays(GL_TRIANGLES, face_idx * 3, 3);
    glBindVertexArray(0);
#else
    // Mobile/WebGL: Manually set up vertex attributes each time
    // Set up position attribute (location 0)
    glBindBuffer(GL_ARRAY_BUFFER, model.VBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Set up color attribute (location 1)
    glBindBuffer(GL_ARRAY_BUFFER, model.colorVBO);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    // Set up texture coordinate attribute (location 2) if model has textures
    if( model.has_textures )
    {
        glBindBuffer(GL_ARRAY_BUFFER, model.texCoordVBO);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(2);
    }

    // Draw only the specified face (3 vertices starting at face_idx * 3)
    glDrawArrays(GL_TRIANGLES, face_idx * 3, 3);

    // Disable vertex attributes
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    if( model.has_textures )
    {
        glDisableVertexAttribArray(2);
    }
#endif

    // Check for OpenGL errors
    // GLenum error = glGetError();
    // if( error != GL_NO_ERROR )
    // {
    //     printf("OpenGL error in pix3dgl_model_draw_face: 0x%x\n", error);
    // }
}

extern "C" void
pix3dgl_model_begin_draw(
    struct Pix3DGL* pix3dgl,
    int model_idx,
    float position_x,
    float position_y,
    float position_z,
    float yaw)
{
    if( !pix3dgl || !pix3dgl->program_es2 )
    {
        printf("Pix3DGL not properly initialized\n");
        return;
    }

    // Find the model
    auto it = pix3dgl->models.find(model_idx);
    if( it == pix3dgl->models.end() )
    {
        printf("Model %d not loaded\n", model_idx);
        return;
    }

    GLModel& model = it->second;
    pix3dgl->current_model = &model;
    pix3dgl->current_model_idx = model_idx;

    // Create model transformation matrix (rotation around Y axis + translation)
    float cos_yaw = cos(yaw);
    float sin_yaw = sin(yaw);

    // Model matrix: first rotate around Y axis, then translate
    float modelMatrix[16] = { cos_yaw,    0.0f,       sin_yaw,    0.0f, 0.0f,    1.0f,
                              0.0f,       0.0f,       -sin_yaw,   0.0f, cos_yaw, 0.0f,
                              position_x, position_y, position_z, 1.0f };

    // Set model matrix uniform (using cached location)
    if( pix3dgl->uniform_model_matrix >= 0 )
    {
        glUniformMatrix4fv(pix3dgl->uniform_model_matrix, 1, GL_FALSE, modelMatrix);
    }

    // Note: Texture binding is now handled per-face in pix3dgl_model_draw_face_fast
    // to support mixed textured/Gouraud faces within the same model

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // Desktop: Bind VAO once
    glBindVertexArray(model.VAO);
#else
    // Mobile/WebGL: Set up vertex attributes once
    // Set up position attribute (location 0)
    glBindBuffer(GL_ARRAY_BUFFER, model.VBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Set up color attribute (location 1)
    glBindBuffer(GL_ARRAY_BUFFER, model.colorVBO);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    // Set up texture coordinate attribute (location 2) if model has textures
    if( model.has_textures )
    {
        glBindBuffer(GL_ARRAY_BUFFER, model.texCoordVBO);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(2);
    }
#endif
}

extern "C" void
pix3dgl_model_draw_face_fast(struct Pix3DGL* pix3dgl, int face_idx)
{
    if( !pix3dgl || !pix3dgl->current_model )
    {
        printf("No model currently active for fast drawing\n");
        return;
    }

    GLModel& model = *pix3dgl->current_model;

    // Validate face index
    if( face_idx < 0 || face_idx >= model.face_count )
    {
        printf(
            "Invalid face index %d for model %d (face_count=%d)\n",
            face_idx,
            pix3dgl->current_model_idx,
            model.face_count);
        return;
    }

    // Check if this face should be drawn (skip if face_infos == 2 or face_colors_c == -2)
    if( !model.face_visible[face_idx] )
    {
        return; // Don't draw this face
    }

    // OPTIMIZATION: Instead of drawing immediately, batch faces by texture
    // This dramatically reduces draw calls
    int face_texture_id = model.face_textures[face_idx];

    // Check if texture exists (if textured)
    if( face_texture_id != -1 )
    {
        auto tex_it = pix3dgl->texture_ids.find(face_texture_id);
        if( tex_it == pix3dgl->texture_ids.end() )
        {
            // Texture not loaded, treat as untextured
            face_texture_id = -1;
        }
    }

    // Add face to appropriate batch
    DrawBatch& batch = pix3dgl->draw_batches[face_texture_id];
    batch.texture_id = face_texture_id;
    batch.face_starts.push_back(face_idx * 3);
    batch.face_counts.push_back(3);
}

extern "C" void
pix3dgl_model_end_draw(struct Pix3DGL* pix3dgl)
{
    if( !pix3dgl || !pix3dgl->current_model )
    {
        return;
    }

    GLModel& model = *pix3dgl->current_model;

    // OPTIMIZATION: Flush all batches using glMultiDrawArrays
    // This replaces thousands of individual glDrawArrays calls with a few batch draws

    for( auto& [texture_id, batch] : pix3dgl->draw_batches )
    {
        if( batch.face_starts.empty() )
            continue;

        // Set texture state for this batch
        if( texture_id == -1 )
        {
            // Untextured batch - disable texturing
            if( pix3dgl->current_texture_state != 0 )
            {
                if( pix3dgl->uniform_use_texture >= 0 )
                {
                    glUniform1i(pix3dgl->uniform_use_texture, 0);
                    pix3dgl->current_texture_state = 0;
                }
            }
        }
        else
        {
            // Textured batch - bind texture
            auto tex_it = pix3dgl->texture_ids.find(texture_id);
            if( tex_it != pix3dgl->texture_ids.end() )
            {
                GLuint gl_texture_id = tex_it->second;

                if( pix3dgl->currently_bound_texture != gl_texture_id )
                {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, gl_texture_id);
                    pix3dgl->currently_bound_texture = gl_texture_id;
                }

                if( pix3dgl->current_texture_state != 1 )
                {
                    if( pix3dgl->uniform_use_texture >= 0 )
                    {
                        glUniform1i(pix3dgl->uniform_use_texture, 1);
                        pix3dgl->current_texture_state = 1;
                    }
                }
            }
        }

        // Use glMultiDrawArrays for efficient batch rendering
        // This issues ONE driver call instead of N glDrawArrays calls
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
        // macOS supports glMultiDrawArrays in OpenGL 3.2+
        glMultiDrawArrays(
            GL_TRIANGLES,
            batch.face_starts.data(),
            batch.face_counts.data(),
            batch.face_starts.size());
#else
        // Fallback for platforms without glMultiDrawArrays
        // Still better than drawing each face individually since we batch by texture
        for( size_t i = 0; i < batch.face_starts.size(); i++ )
        {
            glDrawArrays(GL_TRIANGLES, batch.face_starts[i], batch.face_counts[i]);
        }
#endif
    }

    // Clear batches for next draw
    pix3dgl->draw_batches.clear();

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    // Desktop: Unbind VAO
    glBindVertexArray(0);
#else
    // Mobile/WebGL: Disable vertex attributes
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    if( model.has_textures )
    {
        glDisableVertexAttribArray(2);
    }
#endif

    // Check for OpenGL errors
    GLenum error = glGetError();
    if( error != GL_NO_ERROR )
    {
        printf("OpenGL error in pix3dgl_model_end_draw: 0x%x\n", error);
    }

    pix3dgl->current_model = nullptr;
    pix3dgl->current_model_idx = -1;
}

extern "C" void
pix3dgl_tile_load(
    struct Pix3DGL* pix3dgl,
    int idx,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int vertex_count,
    int* faces_a,
    int* faces_b,
    int* faces_c,
    int face_count,
    int* valid_faces,
    int* face_texture_ids,
    int* face_color_hsl_a,
    int* face_color_hsl_b,
    int* face_color_hsl_c)
{
    GLTile gl_tile;
    gl_tile.idx = idx;
    gl_tile.face_count = face_count;

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    glGenVertexArrays(1, &gl_tile.VAO);
    glBindVertexArray(gl_tile.VAO);
#endif

    // Create buffers
    glGenBuffers(1, &gl_tile.VBO);
    glGenBuffers(1, &gl_tile.colorVBO);
    glGenBuffers(1, &gl_tile.texCoordVBO);
    glGenBuffers(1, &gl_tile.EBO);

    // Build vertex, color, and texture coordinate data
    std::vector<float> vertices(face_count * 9);  // 3 vertices * 3 coordinates per face
    std::vector<float> colors(face_count * 9);    // 3 vertices * 3 colors per face
    std::vector<float> texCoords(face_count * 6); // 3 vertices * 2 UV coords per face

    // Store face visibility, texture info, and shading type
    gl_tile.face_textures.resize(face_count);
    gl_tile.face_visible.resize(face_count);
    gl_tile.face_shading.resize(face_count);

    for( int face = 0; face < face_count; face++ )
    {
        // Check if face is valid
        gl_tile.face_visible[face] = valid_faces ? (valid_faces[face] != 0) : true;
        gl_tile.face_textures[face] = face_texture_ids ? face_texture_ids[face] : -1;

        // Determine shading type
        if( face_texture_ids && face_texture_ids[face] != -1 )
        {
            gl_tile.face_shading[face] = SHADING_TEXTURED;
        }
        else if( face_color_hsl_c && face_color_hsl_c[face] == -1 )
        {
            gl_tile.face_shading[face] = SHADING_FLAT;
        }
        else
        {
            gl_tile.face_shading[face] = SHADING_GOURAUD;
        }

        if( !gl_tile.face_visible[face] )
            continue;

        // Get vertex indices
        int v_a = faces_a[face];
        int v_b = faces_b[face];
        int v_c = faces_c[face];

        // Store vertex positions
        vertices[face * 9 + 0] = vertex_x[v_a];
        vertices[face * 9 + 1] = vertex_y[v_a];
        vertices[face * 9 + 2] = vertex_z[v_a];

        vertices[face * 9 + 3] = vertex_x[v_b];
        vertices[face * 9 + 4] = vertex_y[v_b];
        vertices[face * 9 + 5] = vertex_z[v_b];

        vertices[face * 9 + 6] = vertex_x[v_c];
        vertices[face * 9 + 7] = vertex_y[v_c];
        vertices[face * 9 + 8] = vertex_z[v_c];

        // Get vertex colors (HSL to RGB conversion)
        int hsl_a = face_color_hsl_a[face];
        int hsl_b = face_color_hsl_b[face];
        int hsl_c = face_color_hsl_c[face];

        // Check for flat shading
        int rgb_a, rgb_b, rgb_c;
        if( hsl_c == -1 )
        {
            rgb_a = rgb_b = rgb_c = g_hsl16_to_rgb_table[hsl_a];
        }
        else
        {
            rgb_a = g_hsl16_to_rgb_table[hsl_a];
            rgb_b = g_hsl16_to_rgb_table[hsl_b];
            rgb_c = g_hsl16_to_rgb_table[hsl_c];
        }

        // Store colors
        colors[face * 9 + 0] = ((rgb_a >> 16) & 0xFF) / 255.0f;
        colors[face * 9 + 1] = ((rgb_a >> 8) & 0xFF) / 255.0f;
        colors[face * 9 + 2] = (rgb_a & 0xFF) / 255.0f;

        colors[face * 9 + 3] = ((rgb_b >> 16) & 0xFF) / 255.0f;
        colors[face * 9 + 4] = ((rgb_b >> 8) & 0xFF) / 255.0f;
        colors[face * 9 + 5] = (rgb_b & 0xFF) / 255.0f;

        colors[face * 9 + 6] = ((rgb_c >> 16) & 0xFF) / 255.0f;
        colors[face * 9 + 7] = ((rgb_c >> 8) & 0xFF) / 255.0f;
        colors[face * 9 + 8] = (rgb_c & 0xFF) / 255.0f;

        // Compute UV coordinates using PNM method (like models)
        if( face_texture_ids && face_texture_ids[face] != -1 )
        {
            // For tiles, PNM vertices are fixed at specific corners:
            // P (tp_vertex) = 0 (SW corner)
            // M (tm_vertex) = 1 (NW corner)
            // N (tn_vertex) = 3 (SE corner)
            int tp_vertex = 0;
            int tm_vertex = 1;
            int tn_vertex = 3;

            // Get the PNM triangle vertices that define texture space
            float p_x = vertex_x[tp_vertex];
            float p_y = vertex_y[tp_vertex];
            float p_z = vertex_z[tp_vertex];

            float m_x = vertex_x[tm_vertex];
            float m_y = vertex_y[tm_vertex];
            float m_z = vertex_z[tm_vertex];

            float n_x = vertex_x[tn_vertex];
            float n_y = vertex_y[tn_vertex];
            float n_z = vertex_z[tn_vertex];

            // Get the actual face vertices (A, B, C) for UV computation
            float a_x = vertex_x[v_a];
            float a_y = vertex_y[v_a];
            float a_z = vertex_z[v_a];

            float b_x = vertex_x[v_b];
            float b_y = vertex_y[v_b];
            float b_z = vertex_z[v_b];

            float c_x = vertex_x[v_c];
            float c_y = vertex_y[v_c];
            float c_z = vertex_z[v_c];

            // Compute UV coordinates
            struct UVFaceCoords uv_pnm;
            uv_pnm_compute(
                &uv_pnm,
                p_x,
                p_y,
                p_z,
                m_x,
                m_y,
                m_z,
                n_x,
                n_y,
                n_z,
                a_x,
                a_y,
                a_z,
                b_x,
                b_y,
                b_z,
                c_x,
                c_y,
                c_z);

            // Store UV coordinates
            texCoords[face * 6 + 0] = uv_pnm.u1;
            texCoords[face * 6 + 1] = uv_pnm.v1;
            texCoords[face * 6 + 2] = uv_pnm.u2;
            texCoords[face * 6 + 3] = uv_pnm.v2;
            texCoords[face * 6 + 4] = uv_pnm.u3;
            texCoords[face * 6 + 5] = uv_pnm.v3;
        }
        else
        {
            // Default UVs for non-textured faces
            texCoords[face * 6 + 0] = 0.0f;
            texCoords[face * 6 + 1] = 0.0f;
            texCoords[face * 6 + 2] = 1.0f;
            texCoords[face * 6 + 3] = 0.0f;
            texCoords[face * 6 + 4] = 0.0f;
            texCoords[face * 6 + 5] = 1.0f;
        }
    }

    // Upload vertex data
    glBindBuffer(GL_ARRAY_BUFFER, gl_tile.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
#endif

    // Upload color data
    glBindBuffer(GL_ARRAY_BUFFER, gl_tile.colorVBO);
    glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(float), colors.data(), GL_STATIC_DRAW);
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
#endif

    // Upload texture coordinate data
    glBindBuffer(GL_ARRAY_BUFFER, gl_tile.texCoordVBO);
    glBufferData(
        GL_ARRAY_BUFFER, texCoords.size() * sizeof(float), texCoords.data(), GL_STATIC_DRAW);
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(2);
#endif

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    glBindVertexArray(0);
#endif

    pix3dgl->tiles[idx] = gl_tile;
    printf("Loaded tile %d with %d faces\n", idx, face_count);
}

extern "C" void
pix3dgl_tile_draw(struct Pix3DGL* pix3dgl, int tile_idx)
{
    if( !pix3dgl || !pix3dgl->program_es2 )
    {
        printf("Pix3DGL not properly initialized\n");
        return;
    }

    auto it = pix3dgl->tiles.find(tile_idx);
    if( it == pix3dgl->tiles.end() )
    {
        printf("Tile %d not loaded\n", tile_idx);
        return;
    }

    GLTile& tile = it->second;

    // Tiles are rendered at world origin (no transformation)
    float modelMatrix[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                              0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

    // Set model matrix using cached uniform location
    if( pix3dgl->uniform_model_matrix >= 0 )
    {
        glUniformMatrix4fv(pix3dgl->uniform_model_matrix, 1, GL_FALSE, modelMatrix);
    }

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    glBindVertexArray(tile.VAO);
#else
    glBindBuffer(GL_ARRAY_BUFFER, tile.VBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, tile.colorVBO);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, tile.texCoordVBO);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(2);
#endif

    // Draw each face (optimized with state tracking)
    for( int face = 0; face < tile.face_count; face++ )
    {
        if( !tile.face_visible[face] )
            continue;

        int texture_id = tile.face_textures[face];

        if( texture_id == -1 )
        {
            // No texture - use Gouraud shading
            // Only update if state changed (avoid redundant uniform updates)
            if( pix3dgl->current_texture_state != 0 )
            {
                if( pix3dgl->uniform_use_texture >= 0 )
                {
                    glUniform1i(pix3dgl->uniform_use_texture, 0);
                    pix3dgl->current_texture_state = 0;
                }
            }
        }
        else
        {
            // Has texture
            auto tex_it = pix3dgl->texture_ids.find(texture_id);
            if( tex_it != pix3dgl->texture_ids.end() )
            {
                GLuint gl_texture_id = tex_it->second;

                // Only bind texture if it's different from currently bound (avoid redundant state
                // changes)
                if( pix3dgl->currently_bound_texture != gl_texture_id )
                {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, gl_texture_id);
                    pix3dgl->currently_bound_texture = gl_texture_id;
                }

                // Only enable texture if not already enabled
                if( pix3dgl->current_texture_state != 1 )
                {
                    if( pix3dgl->uniform_use_texture >= 0 )
                    {
                        glUniform1i(pix3dgl->uniform_use_texture, 1);
                        pix3dgl->current_texture_state = 1;
                    }
                }
            }
            else
            {
                // Texture not loaded, fall back to Gouraud shading
                if( pix3dgl->current_texture_state != 0 )
                {
                    if( pix3dgl->uniform_use_texture >= 0 )
                    {
                        glUniform1i(pix3dgl->uniform_use_texture, 0);
                        pix3dgl->current_texture_state = 0;
                    }
                }
            }
        }

        glDrawArrays(GL_TRIANGLES, face * 3, 3);
    }

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    glBindVertexArray(0);
#else
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
#endif
}

extern "C" void
pix3dgl_end_frame(struct Pix3DGL* pix3dgl)
{
    // Placeholder for any end-of-frame cleanup
    // Currently nothing to do here
}

extern "C" void
pix3dgl_cleanup(struct Pix3DGL* pix3dgl)
{
    if( pix3dgl )
    {
        // Clean up GL resources
        for( auto& pair : pix3dgl->models )
        {
            GLModel& model = pair.second;
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
            glDeleteVertexArrays(1, &model.VAO);
#endif
            glDeleteBuffers(1, &model.VBO);
            glDeleteBuffers(1, &model.colorVBO);
            glDeleteBuffers(1, &model.texCoordVBO);
            glDeleteBuffers(1, &model.EBO);
        }

        for( auto& pair : pix3dgl->texture_ids )
        {
            glDeleteTextures(1, &pair.second);
        }

        // Clean up shader program
        if( pix3dgl->program_es2 )
        {
            glDeleteProgram(pix3dgl->program_es2);
        }

        delete pix3dgl;
    }
}