// ============================================================================
// PIX3DGL - OpenGL ES 2.0 / WebGL1 Implementation
// ============================================================================
// This is the OpenGL ES 2.0 / WebGL1 compatible version of pix3dgl.
// Compatible with:
//   - Emscripten / WebGL1
//   - OpenGL ES 2.0 (iOS, Android, embedded systems)
//   - Desktop OpenGL with ES2 profile
//
// Key differences from OpenGL 3.3 Core version:
//   - Uses GLSL #version 100 instead of #version 330 core
//   - Uses attribute/varying instead of in/out
//   - Uses texture2D() instead of texture()
//   - Requires OES extensions for VAO and 32-bit indices
// ============================================================================

#include "graphics/uv_pnm.h"
#include "pix3dgl.h"
#include "scene_cache.h"
#include "shared_tables.h"

#include <algorithm>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//

#if defined(__EMSCRIPTEN__)
// Emscripten with WebGL1 (OpenGL ES 2.0)
#include <GLES2/gl2.h>
#include <emscripten/html5.h>

#include <emscripten.h>
#define GLES2_WEBGL1

// Emscripten-specific optimizations
// These pragmas help the compiler optimize for WebGL1
#ifdef __EMSCRIPTEN__
// Enable WebGL-specific optimizations
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif
#elif defined(__APPLE__)
// iOS uses OpenGL ES 2.0
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#define GLES2_IOS
#elif defined(__ANDROID__)
// Android uses OpenGL ES 2.0
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#define GLES2_ANDROID
#else
// Desktop fallback - use GLES2 or regular OpenGL
#include <GLES2/gl2.h>
#define GLES2_DESKTOP
#endif

#include <unordered_map>

#include <vector>

// OpenGL ES 2.0 / WebGL1 Extension Requirements:
// -----------------------------------------------
// This implementation requires the following extensions for full functionality:
//
// 1. OES_vertex_array_object (VAO support)
//    - Required for VAO functionality (glGenVertexArrays, glBindVertexArray, etc.)
//    - Widely supported in WebGL1 and modern ES2 implementations
//    - Falls back to manual binding if not available (performance impact)
//
// 2. OES_element_index_uint (32-bit indices)
//    - Required for large models with >65535 vertices
//    - Supported in most WebGL1 and ES2 implementations
//    - Alternative: Use GL_UNSIGNED_SHORT and split large models
//
// 3. OES_texture_npot (Non-power-of-two textures)
//    - Required if using non-POT texture atlas
//    - Standard in WebGL1 with limitations (no mipmaps, clamp-to-edge only)
//
// Note: Most modern ES2/WebGL1 implementations support these extensions.

// VAO Extension Function Pointers
// ES2 doesn't have VAOs in core - must use OES_vertex_array_object extension
#ifdef __EMSCRIPTEN__
// Emscripten provides these functions if the extension is available
typedef void (*PFNGLGENVERTEXARRAYSOESPROC)(GLsizei n, GLuint* arrays);
typedef void (*PFNGLBINDVERTEXARRAYOESPROC)(GLuint array);
typedef void (*PFNGLDELETEVERTEXARRAYSOESPROC)(GLsizei n, const GLuint* arrays);

static PFNGLGENVERTEXARRAYSOESPROC glGenVertexArraysOES = nullptr;
static PFNGLBINDVERTEXARRAYOESPROC glBindVertexArrayOES = nullptr;
static PFNGLDELETEVERTEXARRAYSOESPROC glDeleteVertexArraysOES = nullptr;
#endif

// Global flag to track if VAO extension is available
static bool g_has_vao_extension = false;

// Helper macros for conditional VAO usage
#ifdef __EMSCRIPTEN__
#define GEN_VERTEX_ARRAYS(n, arrays)                                                               \
    do                                                                                             \
    {                                                                                              \
        if( g_has_vao_extension && glGenVertexArraysOES )                                          \
            glGenVertexArraysOES(n, arrays);                                                       \
    } while( 0 )
#define BIND_VERTEX_ARRAY(array)                                                                   \
    do                                                                                             \
    {                                                                                              \
        if( g_has_vao_extension && glBindVertexArrayOES )                                          \
            glBindVertexArrayOES(array);                                                           \
    } while( 0 )
#define DELETE_VERTEX_ARRAYS(n, arrays)                                                            \
    do                                                                                             \
    {                                                                                              \
        if( g_has_vao_extension && glDeleteVertexArraysOES )                                       \
            glDeleteVertexArraysOES(n, arrays);                                                    \
    } while( 0 )
#else
// On non-Emscripten platforms, assume VAO functions are available if extension is present
#define GEN_VERTEX_ARRAYS(n, arrays)                                                               \
    do                                                                                             \
    {                                                                                              \
        if( g_has_vao_extension )                                                                  \
            glGenVertexArrays(n, arrays);                                                          \
    } while( 0 )
#define BIND_VERTEX_ARRAY(array)                                                                   \
    do                                                                                             \
    {                                                                                              \
        if( g_has_vao_extension )                                                                  \
            glBindVertexArray(array);                                                              \
    } while( 0 )
#define DELETE_VERTEX_ARRAYS(n, arrays)                                                            \
    do                                                                                             \
    {                                                                                              \
        if( g_has_vao_extension )                                                                  \
            glDeleteVertexArrays(n, arrays);                                                       \
    } while( 0 )
#endif

// OpenGL ES 2.0 / WebGL1 vertex shader - ULTRA FAST, no branching!
// Atlas UVs are pre-calculated on CPU and stored in aTexCoord
const char* g_vertex_shader_es2 = R"(
#version 100
attribute vec3 aPos;
attribute vec4 aColor;
attribute vec2 aTexCoord;
attribute float aTextureId;

uniform mat4 uViewMatrix;       // Precomputed on CPU
uniform mat4 uProjectionMatrix; // Precomputed on CPU
uniform mat4 uModelMatrix;      // Per-model transformation

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vTexBlend;    // 1.0 if textured, 0.0 if not
varying float vAtlasSlot;   // Atlas slot ID for wrapping

void main() {
    // Apply transformations
    vec4 worldPos = uModelMatrix * vec4(aPos, 1.0);
    vec4 viewPos = uViewMatrix * worldPos;
    gl_Position = uProjectionMatrix * viewPos;
    
    // Pass through - all calculated on CPU!
    vColor = aColor;
    vTexCoord = aTexCoord;
    vTexBlend = aTextureId >= 0.0 ? 1.0 : 0.0;
    vAtlasSlot = aTextureId;
}
)";

// OpenGL ES 2.0 / WebGL1 fragment shader
// Handles texture wrapping: clamp U, tile V
// Note: Uses mediump precision for compatibility - sufficient for atlas calculations
const char* g_fragment_shader_es2 = R"(
#version 100
precision mediump float;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vTexBlend;
varying float vAtlasSlot;

uniform sampler2D uTextureAtlas;

void main() {
    vec2 finalTexCoord = vTexCoord;
    
    // Apply texture wrapping if this is a textured face
    if (vTexBlend > 0.5 && vAtlasSlot >= 0.0) {
        // Atlas configuration (must match CPU values)
        float atlasSize = 2048.0;
        float tileSize = 128.0;
        float tilesPerRow = 16.0;
        float tileSizeNorm = tileSize / atlasSize;  // Size of one tile in normalized coords
        
        // Calculate tile position in atlas
        float tileU = mod(vAtlasSlot, tilesPerRow);
        float tileV = floor(vAtlasSlot / tilesPerRow);
        vec2 tileOrigin = vec2(tileU, tileV) * tileSizeNorm;
        
        // Extract local UV within the tile (0 to 1 range within the tile)
        vec2 localUV = (vTexCoord - tileOrigin) / tileSizeNorm;
        
        // Apply wrapping: clamp U, tile V
        localUV.x = clamp(localUV.x, 0.008, 0.992);  // Clamp U coordinate
        localUV.y = clamp(fract(localUV.y), 0.008, 0.992);             // Tile V coordinate (wrap)
        
        // Transform back to atlas space
        finalTexCoord = tileOrigin + localUV * tileSizeNorm;
    }
    
    // Sample texture with wrapped coordinates
    vec4 texColor = texture2D(uTextureAtlas, finalTexCoord);
    
    // Blend between pure vertex color and textured color
    // vTexBlend = 1.0 for textured, 0.0 for untextured
    vec3 finalColor = mix(vColor.rgb, texColor.rgb * vColor.rgb, vTexBlend);
    
    // Handle transparency based on texture and face alpha
    float finalAlpha = vColor.a;
    
    if (vTexBlend > 0.5) {
        // This is a textured face - discard black pixels (for transparency mask)
        if (dot(texColor.rgb, vec3(0.299, 0.587, 0.114)) < 0.05) {
            discard;
        }
        // Use face alpha for transparency
        finalAlpha = vColor.a;
    }
    
    gl_FragColor = vec4(finalColor, finalAlpha);
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
    GLuint VAO; // Use VAO on desktop platforms for better performance
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
    GLuint VAO;
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

// Scene-level batch that includes model/VAO information
struct SceneDrawBatch
{
    int model_idx;
    GLuint vao;
    int texture_id;
    float model_matrix[16]; // Precomputed model matrix
    std::vector<GLint> face_starts;
    std::vector<GLsizei> face_counts;
};

// Structure to track where each face of a model is in the static scene buffer
struct FaceRange
{
    int start_vertex; // Starting vertex index for this face (always 3 vertices)
    int face_index;   // Original face index in the model (for reference)
};

// Structure to track where each model's faces are in the static scene buffer
struct ModelRange
{
    int scene_model_idx;          // Index of the model in the scene
    int start_vertex;             // Starting vertex index in the buffer
    int vertex_count;             // Number of vertices for this model
    std::vector<FaceRange> faces; // Position of each face in the buffer
};

// Static scene buffer for combining multiple models into a single buffer
struct StaticScene
{
    GLuint VAO;
    GLuint VBO;
    GLuint colorVBO;
    GLuint texCoordVBO;
    GLuint textureIdVBO; // New: stores texture ID per vertex
    GLuint dynamicEBO;   // Dynamic index buffer for painter's algorithm ordering

    int total_vertex_count;
    std::vector<float> vertices;
    std::vector<float> colors; // RGBA: 4 floats per vertex (RGB + Alpha)
    std::vector<float> texCoords;
    std::vector<float> textureIds; // New: texture ID per vertex

    // With texture atlas, we don't need separate batches anymore
    // but keep for now for gradual migration
    std::unordered_map<int, DrawBatch> texture_batches;

    // Track model face positions in the buffer
    std::unordered_map<int, ModelRange> model_ranges; // Key: scene_model_idx

    // Track tile face positions in the buffer
    std::unordered_map<int, ModelRange>
        tile_ranges; // Key: scene_tile_idx (reuse ModelRange structure)

    // Draw order (list of scene_model_idx values in the order they should be drawn)
    std::vector<int> draw_order;

    // Draw order for tiles (list of scene_tile_idx values)
    std::vector<int> tile_draw_order;

    // Unified draw order (models and tiles interleaved in correct depth order)
    struct DrawItem
    {
        bool is_tile;
        int index; // scene_model_idx or scene_tile_idx
    };
    std::vector<DrawItem> unified_draw_order;

    // Per-model face draw order (computed each frame based on camera)
    // Maps scene_model_idx -> vector of face indices in draw order
    std::unordered_map<int, std::vector<int>> model_face_order;

    // Per-tile face draw order (computed each frame based on camera)
    // Maps scene_tile_idx -> vector of face indices in draw order
    std::unordered_map<int, std::vector<int>> tile_face_order;

    // Dynamic index buffer data (rebuilt each frame based on face order)
    std::vector<GLuint> sorted_indices;

    // Track index ranges for each model in the sorted index buffer
    struct ModelIndexRange
    {
        int start_index; // Starting index in sorted_indices
        int count;       // Number of indices for this model
    };
    std::unordered_map<int, ModelIndexRange> model_index_ranges;

    bool is_finalized;
    bool indices_dirty;   // Flag to indicate indices need re-upload
    bool in_batch_update; // True when batching face order updates
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
    GLint uniform_texture_atlas; // New: texture atlas sampler
    GLint uniform_atlas_size;    // New: atlas size uniform
    GLint uniform_tile_size;     // New: tile size uniform
    GLint uniform_texture_matrix;

    // State tracking to avoid redundant GL calls
    GLuint currently_bound_texture;
    int current_texture_state; // 0 = texture disabled, 1 = texture enabled

    // Texture atlas for batching all textures into one
    GLuint texture_atlas;
    int atlas_size;                                     // e.g., 2048
    int atlas_tile_size;                                // e.g., 128
    int atlas_tiles_per_row;                            // e.g., 16
    std::unordered_map<int, int> texture_to_atlas_slot; // Maps texture_id -> atlas_slot

    // Batching system - accumulate faces by texture and draw together
    std::unordered_map<int, DrawBatch> draw_batches; // Key: texture_id (-1 for untextured)

    // Scene-level batching - accumulate all draws and render at end of frame
    std::vector<SceneDrawBatch> scene_batches;

    // Reusable batch structure to avoid allocations
    std::unordered_map<int, DrawBatch> reusable_batches;

    // Static scene buffer for efficient rendering of static geometry
    StaticScene* static_scene;
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

    // Create VAO if extension is available (optional in ES2)
    gl_model.VAO = 0;
    GEN_VERTEX_ARRAYS(1, &gl_model.VAO);
    BIND_VERTEX_ARRAY(gl_model.VAO);

    // Create buffers
    glGenBuffers(1, &gl_model.VBO);
    glGenBuffers(1, &gl_model.colorVBO);
    glGenBuffers(1, &gl_model.texCoordVBO);
    glGenBuffers(1, &gl_model.EBO);

    // Allocate arrays for vertex data
    std::vector<float> vertices(face_count * 9);  // 3 vertices * 3 coordinates per face
    std::vector<float> colors(face_count * 12);   // 3 vertices * 4 colors (RGBA) per face
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

        // Calculate face alpha (0xFF = fully opaque, 0x00 = fully transparent)
        // Convert from 0-255 to 0.0-1.0
        float face_alpha = 1.0f;
        if( face_alphas )
        {
            int alpha_byte = face_alphas[face];
            // For textured faces, use alpha directly
            if( face_textures && face_textures[face] != -1 )
            {
                face_alpha = (alpha_byte & 0xFF) / 255.0f;
            }
            else
            {
                // For untextured faces, invert as per render.c
                face_alpha = (0xFF - (alpha_byte & 0xFF)) / 255.0f;
            }
        }

        // Store colors with alpha (RGBA: 4 floats per vertex)
        colors[face * 12 + 0] = ((rgb_a >> 16) & 0xFF) / 255.0f;
        colors[face * 12 + 1] = ((rgb_a >> 8) & 0xFF) / 255.0f;
        colors[face * 12 + 2] = (rgb_a & 0xFF) / 255.0f;
        colors[face * 12 + 3] = face_alpha;

        colors[face * 12 + 4] = ((rgb_b >> 16) & 0xFF) / 255.0f;
        colors[face * 12 + 5] = ((rgb_b >> 8) & 0xFF) / 255.0f;
        colors[face * 12 + 6] = (rgb_b & 0xFF) / 255.0f;
        colors[face * 12 + 7] = face_alpha;

        colors[face * 12 + 8] = ((rgb_c >> 16) & 0xFF) / 255.0f;
        colors[face * 12 + 9] = ((rgb_c >> 8) & 0xFF) / 255.0f;
        colors[face * 12 + 10] = (rgb_c & 0xFF) / 255.0f;
        colors[face * 12 + 11] = face_alpha;

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
    // Set up vertex attributes (location 0 = aPos)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Upload color data to GPU
    glBindBuffer(GL_ARRAY_BUFFER, gl_model.colorVBO);
    glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(float), colors.data(), GL_STATIC_DRAW);
    // Set up color attributes (location 1 = aColor) - RGBA: 4 components
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    // Upload texture coordinate data to GPU
    glBindBuffer(GL_ARRAY_BUFFER, gl_model.texCoordVBO);
    glBufferData(
        GL_ARRAY_BUFFER, texCoords.size() * sizeof(float), texCoords.data(), GL_STATIC_DRAW);
    // Set up texture coordinate attributes (location 2 = aTexCoord)
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(2);

    // Unbind VAO to avoid accidental modifications
    BIND_VERTEX_ARRAY(0);

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

    // Create VAO if extension is available (optional in ES2)
    gl_model.VAO = 0;
    GEN_VERTEX_ARRAYS(1, &gl_model.VAO);
    BIND_VERTEX_ARRAY(gl_model.VAO);

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
    // Set up vertex attributes (location 0 = aPos)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Create and setup color buffer - one color per vertex per face
    std::vector<float> colors(face_count * 12); // 3 vertices * 4 colors (RGBA) per face

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

        // Calculate face alpha (0xFF = fully opaque, 0x00 = fully transparent)
        // Convert from 0-255 to 0.0-1.0
        float face_alpha = 1.0f;
        if( face_alphas )
        {
            int alpha_byte = face_alphas[face];
            // This function is for untextured models only, so always invert alpha as per render.c
            face_alpha = (0xFF - (alpha_byte & 0xFF)) / 255.0f;
        }

        // Store colors with alpha (RGBA: 4 floats per vertex)
        colors[face * 12 + 0] = ((rgb_a >> 16) & 0xFF) / 255.0f;
        colors[face * 12 + 1] = ((rgb_a >> 8) & 0xFF) / 255.0f;
        colors[face * 12 + 2] = (rgb_a & 0xFF) / 255.0f;
        colors[face * 12 + 3] = face_alpha;

        colors[face * 12 + 4] = ((rgb_b >> 16) & 0xFF) / 255.0f;
        colors[face * 12 + 5] = ((rgb_b >> 8) & 0xFF) / 255.0f;
        colors[face * 12 + 6] = (rgb_b & 0xFF) / 255.0f;
        colors[face * 12 + 7] = face_alpha;

        colors[face * 12 + 8] = ((rgb_c >> 16) & 0xFF) / 255.0f;
        colors[face * 12 + 9] = ((rgb_c >> 8) & 0xFF) / 255.0f;
        colors[face * 12 + 10] = (rgb_c & 0xFF) / 255.0f;
        colors[face * 12 + 11] = face_alpha;
    }

    glBindBuffer(GL_ARRAY_BUFFER, gl_model.colorVBO);
    glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(float), colors.data(), GL_STATIC_DRAW);
    // Set up color attributes (location 1 = aColor) - RGBA: 4 components
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    // Unbind VAO to avoid accidental modifications
    BIND_VERTEX_ARRAY(0);

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

    // Initialize static scene
    pix3dgl->static_scene = nullptr;

    pix3dgl->program_es2 = create_shader_program(g_vertex_shader_es2, g_fragment_shader_es2);

    if( !pix3dgl->program_es2 )
    {
        printf("Failed to create ES2/WebGL1 shader program\n");
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
    pix3dgl->uniform_texture_atlas = glGetUniformLocation(pix3dgl->program_es2, "uTextureAtlas");
    pix3dgl->uniform_atlas_size = glGetUniformLocation(pix3dgl->program_es2, "uAtlasSize");
    pix3dgl->uniform_tile_size = glGetUniformLocation(pix3dgl->program_es2, "uTileSize");
    pix3dgl->uniform_texture_matrix = glGetUniformLocation(pix3dgl->program_es2, "uTextureMatrix");

    printf(
        "Cached uniform locations - model_matrix:%d view_matrix:%d projection_matrix:%d "
        "use_texture:%d texture:%d atlas:%d\n",
        pix3dgl->uniform_model_matrix,
        pix3dgl->uniform_view_matrix,
        pix3dgl->uniform_projection_matrix,
        pix3dgl->uniform_use_texture,
        pix3dgl->uniform_texture,
        pix3dgl->uniform_texture_atlas);

    // Initialize texture atlas
    pix3dgl->atlas_tile_size = 128;
    pix3dgl->atlas_size = 2048; // 2048x2048 atlas can hold 16x16 = 256 textures of 128x128
    pix3dgl->atlas_tiles_per_row = pix3dgl->atlas_size / pix3dgl->atlas_tile_size;

    glGenTextures(1, &pix3dgl->texture_atlas);
    glBindTexture(GL_TEXTURE_2D, pix3dgl->texture_atlas);

    // Create empty atlas texture
    std::vector<unsigned char> empty_atlas(pix3dgl->atlas_size * pix3dgl->atlas_size * 4, 0);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        pix3dgl->atlas_size,
        pix3dgl->atlas_size,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        empty_atlas.data());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    printf(
        "Created texture atlas: %dx%d (tiles: %dx%d, capacity: %d textures)\n",
        pix3dgl->atlas_size,
        pix3dgl->atlas_size,
        pix3dgl->atlas_tiles_per_row,
        pix3dgl->atlas_tiles_per_row,
        pix3dgl->atlas_tiles_per_row * pix3dgl->atlas_tiles_per_row);

    // Enable alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Print OpenGL ES info to verify hardware acceleration
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* extensions = glGetString(GL_EXTENSIONS);
    printf("========================================\n");
    printf("OpenGL ES 2.0 / WebGL1 Renderer: %s\n", renderer);
    printf("OpenGL ES Version: %s\n", version);
    printf("OpenGL ES Vendor: %s\n", vendor);
    printf("========================================\n");

    // Check for required/recommended extensions
    const char* ext_str = (const char*)extensions;
    bool has_vao = strstr(ext_str, "OES_vertex_array_object") != nullptr;
    bool has_uint_indices = strstr(ext_str, "OES_element_index_uint") != nullptr;
    bool has_npot = strstr(ext_str, "OES_texture_npot") != nullptr ||
                    strstr(ext_str, "ARB_texture_non_power_of_two") != nullptr;

    // Set global VAO extension flag
    g_has_vao_extension = has_vao;

#ifdef __EMSCRIPTEN__
    // Get VAO extension function pointers for Emscripten
    if( has_vao )
    {
        glGenVertexArraysOES =
            (PFNGLGENVERTEXARRAYSOESPROC)emscripten_webgl_get_proc_address("glGenVertexArraysOES");
        glBindVertexArrayOES =
            (PFNGLBINDVERTEXARRAYOESPROC)emscripten_webgl_get_proc_address("glBindVertexArrayOES");
        glDeleteVertexArraysOES = (PFNGLDELETEVERTEXARRAYSOESPROC)emscripten_webgl_get_proc_address(
            "glDeleteVertexArraysOES");

        if( !glGenVertexArraysOES || !glBindVertexArrayOES || !glDeleteVertexArraysOES )
        {
            printf(
                "WARNING: VAO extension detected but function pointers failed - disabling VAOs\n");
            g_has_vao_extension = false;
        }
    }
#endif

    printf("Extension Support:\n");
    printf("  - VAO (OES_vertex_array_object): %s\n", has_vao ? "YES" : "NO");
    printf("  - 32-bit indices (OES_element_index_uint): %s\n", has_uint_indices ? "YES" : "NO");
    printf("  - NPOT textures: %s\n", has_npot ? "YES" : "NO");

    if( !has_vao )
    {
        printf("WARNING: VAO extension not found - will use manual buffer binding (slower)\n");
    }
    if( !has_uint_indices )
    {
        printf("WARNING: 32-bit index extension not found - large models may not render\n");
    }

    // Check if we're using software rendering (this would be a problem!)
    const char* renderer_str = (const char*)renderer;
    if( strstr(renderer_str, "Software") || strstr(renderer_str, "software") ||
        strstr(renderer_str, "llvmpipe") || strstr(renderer_str, "SwiftShader") )
    {
        printf("WARNING: Software rendering detected! Hardware acceleration may not be working!\n");
    }

    printf(
        "Pix3DGL ES2/WebGL1 initialized successfully with shader program ID: %d\n",
        pix3dgl->program_es2);
    return pix3dgl;
}

extern "C" void
pix3dgl_load_texture(
    struct Pix3DGL* pix3dgl,
    int texture_id,
    struct TexturesCache* textures_cache,
    struct Cache* cache)
{
    // Check if texture is already in atlas
    if( pix3dgl->texture_to_atlas_slot.find(texture_id) != pix3dgl->texture_to_atlas_slot.end() )
    {
        return; // Texture already loaded into atlas
    }

    // Assign next available atlas slot
    int atlas_slot = pix3dgl->texture_to_atlas_slot.size();
    int max_slots = pix3dgl->atlas_tiles_per_row * pix3dgl->atlas_tiles_per_row;

    if( atlas_slot >= max_slots )
    {
        printf(
            "ERROR: Texture atlas full! Cannot load texture %d (slot %d >= %d)\n",
            texture_id,
            atlas_slot,
            max_slots);
        return;
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

    // Convert texture data
    std::vector<unsigned char> texture_bytes(cache_tex->width * cache_tex->height * 4);
    for( int i = 0; i < cache_tex->width * cache_tex->height; i++ )
    {
        int pixel = cache_tex->texels[i];
        texture_bytes[i * 4 + 0] = (pixel >> 16) & 0xFF; // R
        texture_bytes[i * 4 + 1] = (pixel >> 8) & 0xFF;  // G
        texture_bytes[i * 4 + 2] = pixel & 0xFF;         // B
        texture_bytes[i * 4 + 3] = (pixel >> 24) & 0xFF; // A
    }

    // Calculate position in atlas
    int tile_x = atlas_slot % pix3dgl->atlas_tiles_per_row;
    int tile_y = atlas_slot / pix3dgl->atlas_tiles_per_row;
    int x_offset = tile_x * pix3dgl->atlas_tile_size;
    int y_offset = tile_y * pix3dgl->atlas_tile_size;

    // Upload texture to atlas
    glBindTexture(GL_TEXTURE_2D, pix3dgl->texture_atlas);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        x_offset,
        y_offset,
        cache_tex->width,
        cache_tex->height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        texture_bytes.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    // Store mapping from texture_id to atlas_slot
    pix3dgl->texture_to_atlas_slot[texture_id] = atlas_slot;

    // Also store in old map for compatibility (maps to atlas slot now)
    pix3dgl->texture_ids[texture_id] = atlas_slot;

    printf(
        "Loaded texture %d into atlas slot %d at (%d, %d)\n",
        texture_id,
        atlas_slot,
        x_offset,
        y_offset);

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

        // Desktop: Use VAO for better performance
        BIND_VERTEX_ARRAY(model.VAO);
        glDrawArrays(GL_TRIANGLES, 0, model.face_count * 3);
        BIND_VERTEX_ARRAY(0);
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
    pix3dgl->scene_batches.clear();

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

    // OPTIMIZATION: Use scene-level batching - accumulate draw calls instead of rendering
    // immediately Group faces by texture and store them for later rendering in pix3dgl_end_frame()

    // Clear reusable batches for this model
    pix3dgl->reusable_batches.clear();

    // Build batches per texture for this model
    for( int face = 0; face < model.face_count; face++ )
    {
        if( !model.face_visible[face] )
            continue;

        int face_texture_id = model.face_textures[face];

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

        // Add face to batch for this texture
        DrawBatch& batch = pix3dgl->reusable_batches[face_texture_id];
        batch.texture_id = face_texture_id;
        batch.face_starts.push_back(face * 3);
        batch.face_counts.push_back(3);
    }

    // Now create scene batches from the per-texture batches
    for( auto& [texture_id, batch] : pix3dgl->reusable_batches )
    {
        if( batch.face_starts.empty() )
            continue;

        SceneDrawBatch scene_batch;
        scene_batch.model_idx = model_idx;
        scene_batch.vao = model.VAO;
        scene_batch.texture_id = texture_id;
        memcpy(scene_batch.model_matrix, modelMatrix, sizeof(modelMatrix));
        scene_batch.face_starts = std::move(batch.face_starts);
        scene_batch.face_counts = std::move(batch.face_counts);

        pix3dgl->scene_batches.push_back(std::move(scene_batch));
    }

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

    // Desktop: Use VAO for better performance
    BIND_VERTEX_ARRAY(model.VAO);
    // Draw only the specified face (3 vertices starting at face_idx * 3)
    glDrawArrays(GL_TRIANGLES, face_idx * 3, 3);
    BIND_VERTEX_ARRAY(0);

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

    // Desktop: Bind VAO once
    BIND_VERTEX_ARRAY(model.VAO);
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
    }

    // Clear batches for next draw
    pix3dgl->draw_batches.clear();

    // Desktop: Unbind VAO
    BIND_VERTEX_ARRAY(0);

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

    GEN_VERTEX_ARRAYS(1, &gl_tile.VAO);
    BIND_VERTEX_ARRAY(gl_tile.VAO);

    // Create buffers
    glGenBuffers(1, &gl_tile.VBO);
    glGenBuffers(1, &gl_tile.colorVBO);
    glGenBuffers(1, &gl_tile.texCoordVBO);
    glGenBuffers(1, &gl_tile.EBO);

    // Build vertex, color, and texture coordinate data
    std::vector<float> vertices(face_count * 9);  // 3 vertices * 3 coordinates per face
    std::vector<float> colors(face_count * 12);   // 3 vertices * 4 colors (RGBA) per face
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

        // Store colors with alpha (RGBA: 4 floats per vertex)
        // Tiles don't have face alphas, so use 1.0 (fully opaque)
        colors[face * 12 + 0] = ((rgb_a >> 16) & 0xFF) / 255.0f;
        colors[face * 12 + 1] = ((rgb_a >> 8) & 0xFF) / 255.0f;
        colors[face * 12 + 2] = (rgb_a & 0xFF) / 255.0f;
        colors[face * 12 + 3] = 1.0f;

        colors[face * 12 + 4] = ((rgb_b >> 16) & 0xFF) / 255.0f;
        colors[face * 12 + 5] = ((rgb_b >> 8) & 0xFF) / 255.0f;
        colors[face * 12 + 6] = (rgb_b & 0xFF) / 255.0f;
        colors[face * 12 + 7] = 1.0f;

        colors[face * 12 + 8] = ((rgb_c >> 16) & 0xFF) / 255.0f;
        colors[face * 12 + 9] = ((rgb_c >> 8) & 0xFF) / 255.0f;
        colors[face * 12 + 10] = (rgb_c & 0xFF) / 255.0f;
        colors[face * 12 + 11] = 1.0f;

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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Upload color data
    glBindBuffer(GL_ARRAY_BUFFER, gl_tile.colorVBO);
    glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(float), colors.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    // Upload texture coordinate data
    glBindBuffer(GL_ARRAY_BUFFER, gl_tile.texCoordVBO);
    glBufferData(
        GL_ARRAY_BUFFER, texCoords.size() * sizeof(float), texCoords.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(2);

    BIND_VERTEX_ARRAY(0);

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

    // OPTIMIZATION: Use scene-level batching - accumulate draw calls like models
    // Tiles use identity matrix (no transformation)
    float modelMatrix[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                              0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

    // Clear reusable batches for this tile
    pix3dgl->reusable_batches.clear();

    // Build batches per texture for this tile
    for( int face = 0; face < tile.face_count; face++ )
    {
        if( !tile.face_visible[face] )
            continue;

        int texture_id = tile.face_textures[face];

        // Check if texture exists (if textured)
        if( texture_id != -1 )
        {
            auto tex_it = pix3dgl->texture_ids.find(texture_id);
            if( tex_it == pix3dgl->texture_ids.end() )
            {
                // Texture not loaded, treat as untextured
                texture_id = -1;
            }
        }

        // Add face to batch for this texture
        DrawBatch& batch = pix3dgl->reusable_batches[texture_id];
        batch.texture_id = texture_id;
        batch.face_starts.push_back(face * 3);
        batch.face_counts.push_back(3);
    }

    // Now create scene batches from the per-texture batches
    for( auto& [texture_id, batch] : pix3dgl->reusable_batches )
    {
        if( batch.face_starts.empty() )
            continue;

        SceneDrawBatch scene_batch;
        scene_batch.model_idx = tile_idx;
        scene_batch.vao = tile.VAO;
        scene_batch.texture_id = texture_id;
        memcpy(scene_batch.model_matrix, modelMatrix, sizeof(modelMatrix));
        scene_batch.face_starts = std::move(batch.face_starts);
        scene_batch.face_counts = std::move(batch.face_counts);

        pix3dgl->scene_batches.push_back(std::move(scene_batch));
    }
}

extern "C" void
pix3dgl_end_frame(struct Pix3DGL* pix3dgl)
{
    // OPTIMIZATION: Render all accumulated scene batches
    // This reduces state changes by sorting batches by texture

    if( pix3dgl->scene_batches.empty() )
        return;

    // Sort batches by texture ID to minimize texture binding
    std::sort(
        pix3dgl->scene_batches.begin(),
        pix3dgl->scene_batches.end(),
        [](const SceneDrawBatch& a, const SceneDrawBatch& b) {
            return a.texture_id < b.texture_id;
        });

    GLuint last_vao = 0;
    int last_texture_id = -999; // Invalid value to force first bind

    for( const auto& batch : pix3dgl->scene_batches )
    {
        if( batch.face_starts.empty() )
            continue;

        // Bind VAO only if it changed
        if( batch.vao != last_vao )
        {
            BIND_VERTEX_ARRAY(batch.vao);
            last_vao = batch.vao;
        }

        // Set model matrix
        if( pix3dgl->uniform_model_matrix >= 0 )
        {
            glUniformMatrix4fv(pix3dgl->uniform_model_matrix, 1, GL_FALSE, batch.model_matrix);
        }

        // Set texture state only if it changed
        if( batch.texture_id != last_texture_id )
        {
            if( batch.texture_id == -1 )
            {
                // Untextured - disable texturing
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
                // Textured - bind texture
                auto tex_it = pix3dgl->texture_ids.find(batch.texture_id);
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
            last_texture_id = batch.texture_id;
        }

        for( int face = 0; face < batch.face_starts.size(); face++ )
        {
            int start_index = batch.face_starts[face];
            int count = batch.face_counts[face];
            glDrawArrays(GL_TRIANGLES, start_index, count);
        }
    }

    // Unbind VAO
    if( last_vao != 0 )
    {
        BIND_VERTEX_ARRAY(0);
    }
}

// Static scene building functions - for efficiently rendering static geometry
extern "C" void
pix3dgl_scene_static_load_begin(struct Pix3DGL* pix3dgl)
{
    if( !pix3dgl )
    {
        printf("Pix3DGL not initialized\n");
        return;
    }

    // Clean up existing static scene if one exists
    if( pix3dgl->static_scene )
    {
        DELETE_VERTEX_ARRAYS(1, &pix3dgl->static_scene->VAO);
        glDeleteBuffers(1, &pix3dgl->static_scene->VBO);
        glDeleteBuffers(1, &pix3dgl->static_scene->colorVBO);
        glDeleteBuffers(1, &pix3dgl->static_scene->texCoordVBO);
        glDeleteBuffers(1, &pix3dgl->static_scene->textureIdVBO);
        if( pix3dgl->static_scene->dynamicEBO != 0 )
        {
            glDeleteBuffers(1, &pix3dgl->static_scene->dynamicEBO);
        }
        delete pix3dgl->static_scene;
    }

    // Create new static scene
    pix3dgl->static_scene = new StaticScene();
    pix3dgl->static_scene->VAO = 0;
    pix3dgl->static_scene->VBO = 0;
    pix3dgl->static_scene->colorVBO = 0;
    pix3dgl->static_scene->texCoordVBO = 0;
    pix3dgl->static_scene->textureIdVBO = 0;
    pix3dgl->static_scene->dynamicEBO = 0;
    pix3dgl->static_scene->total_vertex_count = 0;
    pix3dgl->static_scene->is_finalized = false;
    pix3dgl->static_scene->indices_dirty = false;
    pix3dgl->static_scene->in_batch_update = false;

    printf("Started building static scene\n");
}

extern "C" void
pix3dgl_scene_static_load_tile(
    struct Pix3DGL* pix3dgl,
    int scene_tile_idx,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int vertex_count,
    int* faces_a,
    int* faces_b,
    int* faces_c,
    int face_count,
    int* face_texture_ids,
    int* face_color_hsl_a,
    int* face_color_hsl_b,
    int* face_color_hsl_c)
{
    if( !pix3dgl || !pix3dgl->static_scene )
    {
        printf("Static scene not initialized\n");
        return;
    }

    if( pix3dgl->static_scene->is_finalized )
    {
        printf("Cannot add tiles to finalized static scene\n");
        return;
    }

    StaticScene* scene = pix3dgl->static_scene;
    int current_vertex_index = scene->total_vertex_count;
    int start_vertex_index = current_vertex_index;

    // Temporary storage for face positions (will be moved to ModelRange later)
    // Pre-allocate with size of face_count to maintain face index mapping
    std::vector<FaceRange> face_ranges(face_count);

    // Initialize all face ranges with invalid start_vertex (-1)
    for( int i = 0; i < face_count; i++ )
    {
        face_ranges[i].start_vertex = -1;
        face_ranges[i].face_index = i;
    }

    // Process each face
    for( int face = 0; face < face_count; face++ )
    {
        int face_start_index = current_vertex_index;

        // Track this face's position in the buffer
        face_ranges[face].start_vertex = face_start_index;
        face_ranges[face].face_index = face;

        // Get vertex indices
        int v_a = faces_a[face];
        int v_b = faces_b[face];
        int v_c = faces_c[face];

        // Add the 3 vertices for this face
        for( int v = 0; v < 3; v++ )
        {
            int vertex_idx;
            if( v == 0 )
                vertex_idx = v_a;
            else if( v == 1 )
                vertex_idx = v_b;
            else
                vertex_idx = v_c;

            // Store vertex position (tiles are already in world space)
            scene->vertices.push_back(vertex_x[vertex_idx]);
            scene->vertices.push_back(vertex_y[vertex_idx]);
            scene->vertices.push_back(vertex_z[vertex_idx]);
        }

        // Get and convert colors
        int hsl_a = face_color_hsl_a[face];
        int hsl_b = face_color_hsl_b[face];
        int hsl_c = face_color_hsl_c[face];

        int rgb_a, rgb_b, rgb_c;
        if( hsl_c == -1 )
        {
            // Flat shading
            rgb_a = rgb_b = rgb_c = g_hsl16_to_rgb_table[hsl_a];
        }
        else
        {
            // Gouraud shading
            rgb_a = g_hsl16_to_rgb_table[hsl_a];
            rgb_b = g_hsl16_to_rgb_table[hsl_b];
            rgb_c = g_hsl16_to_rgb_table[hsl_c];
        }

        // Store colors with alpha (RGBA: 4 floats per vertex)
        // Tiles don't have face alphas, so use 1.0 (fully opaque)
        scene->colors.push_back(((rgb_a >> 16) & 0xFF) / 255.0f);
        scene->colors.push_back(((rgb_a >> 8) & 0xFF) / 255.0f);
        scene->colors.push_back((rgb_a & 0xFF) / 255.0f);
        scene->colors.push_back(1.0f);

        scene->colors.push_back(((rgb_b >> 16) & 0xFF) / 255.0f);
        scene->colors.push_back(((rgb_b >> 8) & 0xFF) / 255.0f);
        scene->colors.push_back((rgb_b & 0xFF) / 255.0f);
        scene->colors.push_back(1.0f);

        scene->colors.push_back(((rgb_c >> 16) & 0xFF) / 255.0f);
        scene->colors.push_back(((rgb_c >> 8) & 0xFF) / 255.0f);
        scene->colors.push_back((rgb_c & 0xFF) / 255.0f);
        scene->colors.push_back(1.0f);

        // Declare texture variables at face loop scope
        int texture_id = -1;
        int atlas_slot = -1;

        // For tiles, we'll compute UVs if needed
        // For now, just store default UVs (tiles use more complex PNM mapping)
        if( face_texture_ids && face_texture_ids[face] != -1 )
        {
            // Compute UV coordinates using PNM method for tiles
            // P = vertex 0 (SW corner), M = vertex 1 (NW corner), N = vertex 3 (SE corner)
            int tp_vertex = 0;
            int tm_vertex = 1;
            int tn_vertex = 3;

            float p_x = vertex_x[tp_vertex];
            float p_y = vertex_y[tp_vertex];
            float p_z = vertex_z[tp_vertex];

            float m_x = vertex_x[tm_vertex];
            float m_y = vertex_y[tm_vertex];
            float m_z = vertex_z[tm_vertex];

            float n_x = vertex_x[tn_vertex];
            float n_y = vertex_y[tn_vertex];
            float n_z = vertex_z[tn_vertex];

            float a_x = vertex_x[v_a];
            float a_y = vertex_y[v_a];
            float a_z = vertex_z[v_a];

            float b_x = vertex_x[v_b];
            float b_y = vertex_y[v_b];
            float b_z = vertex_z[v_b];

            float c_x = vertex_x[v_c];
            float c_y = vertex_y[v_c];
            float c_z = vertex_z[v_c];

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

            // Get atlas slot for this texture
            texture_id = face_texture_ids ? face_texture_ids[face] : -1;

            if( texture_id != -1 )
            {
                auto tex_it = pix3dgl->texture_ids.find(texture_id);
                if( tex_it != pix3dgl->texture_ids.end() )
                {
                    atlas_slot = tex_it->second;
                }
                else
                {
                    texture_id = -1;
                }
            }

            // Calculate atlas UVs on CPU (once, not per-frame!)
            if( atlas_slot >= 0 )
            {
                float tiles_per_row = (float)pix3dgl->atlas_tiles_per_row;
                float tile_u = (float)(atlas_slot % pix3dgl->atlas_tiles_per_row);
                float tile_v = (float)(atlas_slot / pix3dgl->atlas_tiles_per_row);
                float tile_size = (float)pix3dgl->atlas_tile_size;
                float atlas_size = (float)pix3dgl->atlas_size;

                // Transform UVs to atlas space
                scene->texCoords.push_back((tile_u + uv_pnm.u1) * tile_size / atlas_size);
                scene->texCoords.push_back((tile_v + uv_pnm.v1) * tile_size / atlas_size);
                scene->texCoords.push_back((tile_u + uv_pnm.u2) * tile_size / atlas_size);
                scene->texCoords.push_back((tile_v + uv_pnm.v2) * tile_size / atlas_size);
                scene->texCoords.push_back((tile_u + uv_pnm.u3) * tile_size / atlas_size);
                scene->texCoords.push_back((tile_v + uv_pnm.v3) * tile_size / atlas_size);
            }
            else
            {
                // No valid atlas slot, store dummy UVs
                scene->texCoords.push_back(0.0f);
                scene->texCoords.push_back(0.0f);
                scene->texCoords.push_back(0.0f);
                scene->texCoords.push_back(0.0f);
                scene->texCoords.push_back(0.0f);
                scene->texCoords.push_back(0.0f);
            }

            // Store texture ID (atlas slot) for each vertex
            scene->textureIds.push_back(atlas_slot);
            scene->textureIds.push_back(atlas_slot);
            scene->textureIds.push_back(atlas_slot);

            texture_id = (atlas_slot >= 0) ? texture_id : -1;
        }
        else
        {
            // Untextured face
            texture_id = -1;
            atlas_slot = -1;

            scene->texCoords.push_back(0.0f);
            scene->texCoords.push_back(0.0f);
            scene->texCoords.push_back(0.0f);
            scene->texCoords.push_back(0.0f);
            scene->texCoords.push_back(0.0f);
            scene->texCoords.push_back(0.0f);

            scene->textureIds.push_back(atlas_slot);
            scene->textureIds.push_back(atlas_slot);
            scene->textureIds.push_back(atlas_slot);
        }

        DrawBatch& batch = scene->texture_batches[texture_id];
        batch.texture_id = texture_id;

        // Merge contiguous faces: if the last entry in this batch is immediately before this face,
        // just increase the count instead of adding a new entry
        if( !batch.face_starts.empty() &&
            batch.face_starts.back() + batch.face_counts.back() == face_start_index )
        {
            // Extend the last batch entry
            batch.face_counts.back() += 3;
        }
        else
        {
            // Start a new batch entry
            batch.face_starts.push_back(face_start_index);
            batch.face_counts.push_back(3);
        }

        current_vertex_index += 3;
    }

    scene->total_vertex_count = current_vertex_index;

    // Store tile range for later face ordering
    int vertex_count_total = current_vertex_index - start_vertex_index;
    if( vertex_count_total > 0 )
    {
        ModelRange range;                       // Reusing ModelRange structure for tiles
        range.scene_model_idx = scene_tile_idx; // Store tile index
        range.start_vertex = start_vertex_index;
        range.vertex_count = vertex_count_total;
        range.faces = std::move(face_ranges);
        scene->tile_ranges[scene_tile_idx] = range;

        printf(
            "Added tile %d to static scene: start=%d, count=%d, faces=%d (total %d vertices)\n",
            scene_tile_idx,
            start_vertex_index,
            vertex_count_total,
            face_count,
            scene->total_vertex_count);
    }
}

extern "C" void
pix3dgl_scene_static_load_model(
    struct Pix3DGL* pix3dgl,
    int scene_model_idx,
    int* vertices_x,
    int* vertices_y,
    int* vertices_z,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int face_count,
    int* face_textures_nullable,
    int* face_texture_coords_nullable,
    int* textured_p_coordinate_nullable,
    int* textured_m_coordinate_nullable,
    int* textured_n_coordinate_nullable,
    int* face_colors_hsl_a,
    int* face_colors_hsl_b,
    int* face_colors_hsl_c,
    int* face_infos_nullable,
    int* face_alphas_nullable,
    float position_x,
    float position_y,
    float position_z,
    float yaw)
{
    if( !pix3dgl || !pix3dgl->static_scene )
    {
        printf("Static scene not initialized\n");
        return;
    }

    if( pix3dgl->static_scene->is_finalized )
    {
        printf("Cannot add models to finalized static scene\n");
        return;
    }

    StaticScene* scene = pix3dgl->static_scene;

    // Create transformation matrix for this model instance
    float cos_yaw = cos(yaw);
    float sin_yaw = sin(yaw);

    int current_vertex_index = scene->total_vertex_count;
    int start_vertex_index = current_vertex_index;

    // Temporary storage for face positions (will be moved to ModelRange later)
    // Pre-allocate with size of face_count to maintain face index mapping
    std::vector<FaceRange> face_ranges(face_count);

    // Initialize all face ranges with invalid start_vertex (-1)
    for( int i = 0; i < face_count; i++ )
    {
        face_ranges[i].start_vertex = -1;
        face_ranges[i].face_index = i;
    }

    // Process each face
    for( int face = 0; face < face_count; face++ )
    {
        // Check if face should be drawn
        bool should_draw = true;
        if( face_infos_nullable && face_infos_nullable[face] == 2 )
        {
            should_draw = false;
        }
        if( face_colors_hsl_c && face_colors_hsl_c[face] == -2 )
        {
            should_draw = false;
        }

        if( !should_draw )
            continue;

        int face_start_index = current_vertex_index;

        // Get vertex indices
        int v_a = face_indices_a[face];
        int v_b = face_indices_b[face];
        int v_c = face_indices_c[face];

        // Transform and add vertices
        int vertex_indices[] = { v_a, v_b, v_c };
        for( int v = 0; v < 3; v++ )
        {
            int vertex_idx = vertex_indices[v];

            // Get original vertex position
            float x = vertices_x[vertex_idx];
            float y = vertices_y[vertex_idx];
            float z = vertices_z[vertex_idx];

            // Apply transformation (rotation + translation)
            float tx = cos_yaw * x + sin_yaw * z + position_x;
            float ty = y + position_y;
            float tz = -sin_yaw * x + cos_yaw * z + position_z;

            // Append transformed vertex
            scene->vertices.push_back(tx);
            scene->vertices.push_back(ty);
            scene->vertices.push_back(tz);
        }

        // Get and convert colors
        int hsl_a = face_colors_hsl_a[face];
        int hsl_b = face_colors_hsl_b[face];
        int hsl_c = face_colors_hsl_c[face];

        int rgb_a, rgb_b, rgb_c;
        if( hsl_c == -1 )
        {
            // Flat shading
            rgb_a = rgb_b = rgb_c = g_hsl16_to_rgb_table[hsl_a];
        }
        else
        {
            // Gouraud shading
            rgb_a = g_hsl16_to_rgb_table[hsl_a];
            rgb_b = g_hsl16_to_rgb_table[hsl_b];
            rgb_c = g_hsl16_to_rgb_table[hsl_c];
        }

        // Calculate face alpha (0xFF = fully opaque, 0x00 = fully transparent)
        // Convert from 0-255 to 0.0-1.0
        float face_alpha = 1.0f;
        if( face_alphas_nullable )
        {
            int alpha_byte = face_alphas_nullable[face];
            // For textured faces, use alpha directly
            if( face_textures_nullable && face_textures_nullable[face] != -1 )
            {
                face_alpha = (alpha_byte & 0xFF) / 255.0f;
            }
            else
            {
                // For untextured faces, invert as per render.c
                face_alpha = (0xFF - (alpha_byte & 0xFF)) / 255.0f;
            }
        }

        // Store colors with alpha (RGBA: 4 floats per vertex)
        scene->colors.push_back(((rgb_a >> 16) & 0xFF) / 255.0f);
        scene->colors.push_back(((rgb_a >> 8) & 0xFF) / 255.0f);
        scene->colors.push_back((rgb_a & 0xFF) / 255.0f);
        scene->colors.push_back(face_alpha);

        scene->colors.push_back(((rgb_b >> 16) & 0xFF) / 255.0f);
        scene->colors.push_back(((rgb_b >> 8) & 0xFF) / 255.0f);
        scene->colors.push_back((rgb_b & 0xFF) / 255.0f);
        scene->colors.push_back(face_alpha);

        scene->colors.push_back(((rgb_c >> 16) & 0xFF) / 255.0f);
        scene->colors.push_back(((rgb_c >> 8) & 0xFF) / 255.0f);
        scene->colors.push_back((rgb_c & 0xFF) / 255.0f);
        scene->colors.push_back(face_alpha);

        // Declare texture variables at face loop scope
        int texture_id = -1;
        int atlas_slot = -1;

        // Compute UV coordinates if textured
        if( face_textures_nullable && face_textures_nullable[face] != -1 )
        {
            // Determine texture space vertices (P, M, N)
            int tp_vertex = v_a;
            int tm_vertex = v_b;
            int tn_vertex = v_c;

            if( face_texture_coords_nullable && face_texture_coords_nullable[face] != -1 )
            {
                int texture_face = face_texture_coords_nullable[face];
                tp_vertex = textured_p_coordinate_nullable[texture_face];
                tm_vertex = textured_m_coordinate_nullable[texture_face];
                tn_vertex = textured_n_coordinate_nullable[texture_face];
            }

            // Get PNM vertices
            float p_x = vertices_x[tp_vertex];
            float p_y = vertices_y[tp_vertex];
            float p_z = vertices_z[tp_vertex];

            float m_x = vertices_x[tm_vertex];
            float m_y = vertices_y[tm_vertex];
            float m_z = vertices_z[tm_vertex];

            float n_x = vertices_x[tn_vertex];
            float n_y = vertices_y[tn_vertex];
            float n_z = vertices_z[tn_vertex];

            // Get face vertices
            float a_x = vertices_x[v_a];
            float a_y = vertices_y[v_a];
            float a_z = vertices_z[v_a];

            float b_x = vertices_x[v_b];
            float b_y = vertices_y[v_b];
            float b_z = vertices_z[v_b];

            float c_x = vertices_x[v_c];
            float c_y = vertices_y[v_c];
            float c_z = vertices_z[v_c];

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

            // Get atlas slot for this texture
            texture_id = face_textures_nullable ? face_textures_nullable[face] : -1;

            if( texture_id != -1 )
            {
                auto tex_it = pix3dgl->texture_ids.find(texture_id);
                if( tex_it != pix3dgl->texture_ids.end() )
                {
                    atlas_slot = tex_it->second;
                }
                else
                {
                    texture_id = -1;
                }
            }

            // Calculate atlas UVs on CPU (once, not per-frame!)
            if( atlas_slot >= 0 )
            {
                float tiles_per_row = (float)pix3dgl->atlas_tiles_per_row;
                float tile_u = (float)(atlas_slot % pix3dgl->atlas_tiles_per_row);
                float tile_v = (float)(atlas_slot / pix3dgl->atlas_tiles_per_row);
                float tile_size = (float)pix3dgl->atlas_tile_size;
                float atlas_size = (float)pix3dgl->atlas_size;

                // Transform UVs to atlas space
                scene->texCoords.push_back((tile_u + uv_pnm.u1) * tile_size / atlas_size);
                scene->texCoords.push_back((tile_v + uv_pnm.v1) * tile_size / atlas_size);
                scene->texCoords.push_back((tile_u + uv_pnm.u2) * tile_size / atlas_size);
                scene->texCoords.push_back((tile_v + uv_pnm.v2) * tile_size / atlas_size);
                scene->texCoords.push_back((tile_u + uv_pnm.u3) * tile_size / atlas_size);
                scene->texCoords.push_back((tile_v + uv_pnm.v3) * tile_size / atlas_size);
            }
            else
            {
                // No valid atlas slot, store dummy UVs
                scene->texCoords.push_back(0.0f);
                scene->texCoords.push_back(0.0f);
                scene->texCoords.push_back(0.0f);
                scene->texCoords.push_back(0.0f);
                scene->texCoords.push_back(0.0f);
                scene->texCoords.push_back(0.0f);
            }

            // Store texture ID (atlas slot) for each vertex
            scene->textureIds.push_back(atlas_slot);
            scene->textureIds.push_back(atlas_slot);
            scene->textureIds.push_back(atlas_slot);

            texture_id = (atlas_slot >= 0) ? texture_id : -1;
        }
        else
        {
            // Untextured face
            texture_id = -1;
            atlas_slot = -1;

            scene->texCoords.push_back(0.0f);
            scene->texCoords.push_back(0.0f);
            scene->texCoords.push_back(0.0f);
            scene->texCoords.push_back(0.0f);
            scene->texCoords.push_back(0.0f);
            scene->texCoords.push_back(0.0f);

            scene->textureIds.push_back(atlas_slot);
            scene->textureIds.push_back(atlas_slot);
            scene->textureIds.push_back(atlas_slot);
        }

        DrawBatch& batch = scene->texture_batches[texture_id];
        batch.texture_id = texture_id;

        // Merge contiguous faces
        if( !batch.face_starts.empty() &&
            batch.face_starts.back() + batch.face_counts.back() == face_start_index )
        {
            batch.face_counts.back() += 3;
        }
        else
        {
            batch.face_starts.push_back(face_start_index);
            batch.face_counts.push_back(3);
        }

        // Track this face's position in the buffer (use original face index)
        face_ranges[face].start_vertex = face_start_index;
        face_ranges[face].face_index = face;

        current_vertex_index += 3;
    }

    scene->total_vertex_count = current_vertex_index;

    // Track the model's position in the buffer
    int vertex_count = current_vertex_index - start_vertex_index;
    if( vertex_count > 0 )
    {
        ModelRange range;
        range.scene_model_idx = scene_model_idx;
        range.start_vertex = start_vertex_index;
        range.vertex_count = vertex_count;
        range.faces = std::move(face_ranges);
        scene->model_ranges[scene_model_idx] = range;

        printf(
            "Added model %d to static scene: start=%d, count=%d, faces=%d (total %d vertices)\n",
            scene_model_idx,
            start_vertex_index,
            vertex_count,
            (int)range.faces.size(),
            scene->total_vertex_count);
    }
}

extern "C" void
pix3dgl_scene_static_load_end(struct Pix3DGL* pix3dgl)
{
    if( !pix3dgl || !pix3dgl->static_scene )
    {
        printf("Static scene not initialized\n");
        return;
    }

    StaticScene* scene = pix3dgl->static_scene;

    if( scene->is_finalized )
    {
        printf("Static scene already finalized\n");
        return;
    }

    // Create and upload the combined buffers
    GEN_VERTEX_ARRAYS(1, &scene->VAO);
    BIND_VERTEX_ARRAY(scene->VAO);

    // Upload vertices
    glGenBuffers(1, &scene->VBO);
    glBindBuffer(GL_ARRAY_BUFFER, scene->VBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        scene->vertices.size() * sizeof(float),
        scene->vertices.data(),
        GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Upload colors
    glGenBuffers(1, &scene->colorVBO);
    glBindBuffer(GL_ARRAY_BUFFER, scene->colorVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        scene->colors.size() * sizeof(float),
        scene->colors.data(),
        GL_STATIC_DRAW);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    // Upload texture coordinates
    glGenBuffers(1, &scene->texCoordVBO);
    glBindBuffer(GL_ARRAY_BUFFER, scene->texCoordVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        scene->texCoords.size() * sizeof(float),
        scene->texCoords.data(),
        GL_STATIC_DRAW);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(2);

    // Upload texture IDs (atlas slots)
    glGenBuffers(1, &scene->textureIdVBO);
    glBindBuffer(GL_ARRAY_BUFFER, scene->textureIdVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        scene->textureIds.size() * sizeof(float),
        scene->textureIds.data(),
        GL_STATIC_DRAW);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glEnableVertexAttribArray(3);

    // Create dynamic Element Buffer Object for painter's algorithm ordering
    // Pre-allocate with maximum possible size (total_vertex_count indices)
    glGenBuffers(1, &scene->dynamicEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, scene->dynamicEBO);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        scene->total_vertex_count * sizeof(GLuint),
        nullptr,
        GL_DYNAMIC_DRAW); // Use DYNAMIC_DRAW for frequent updates

    BIND_VERTEX_ARRAY(0);

    scene->is_finalized = true;

    // Pre-allocate sorted_indices vector for later use
    scene->sorted_indices.reserve(scene->total_vertex_count);

    // Free CPU-side data to save memory (we only need GPU buffers now)
    scene->vertices.clear();
    scene->vertices.shrink_to_fit();
    scene->colors.clear();
    scene->colors.shrink_to_fit();
    scene->texCoords.clear();
    scene->texCoords.shrink_to_fit();
    scene->textureIds.clear();
    scene->textureIds.shrink_to_fit();

    printf("Finalized static scene with %d vertices\n", scene->total_vertex_count);
}

extern "C" void
pix3dgl_scene_static_set_draw_order(struct Pix3DGL* pix3dgl, int* scene_model_indices, int count)
{
    if( !pix3dgl || !pix3dgl->static_scene )
    {
        printf("Static scene not initialized\n");
        return;
    }

    StaticScene* scene = pix3dgl->static_scene;

    // Clear and set the draw order
    scene->draw_order.clear();
    scene->draw_order.reserve(count);

    for( int i = 0; i < count; i++ )
    {
        int scene_model_idx = scene_model_indices[i];

        // Only add models that actually have geometry in the buffer
        if( scene->model_ranges.find(scene_model_idx) != scene->model_ranges.end() )
        {
            scene->draw_order.push_back(scene_model_idx);
        }
    }

    printf(
        "Set draw order for %d models (out of %d requested)\n",
        (int)scene->draw_order.size(),
        count);
}

// Begin batching face order updates (delays dirty flag until end_batch)
extern "C" void
pix3dgl_scene_static_begin_face_order_batch(struct Pix3DGL* pix3dgl)
{
    if( !pix3dgl || !pix3dgl->static_scene )
    {
        return;
    }
    pix3dgl->static_scene->in_batch_update = true;
}

// End batching and mark indices as dirty (triggers rebuild on next draw)
extern "C" void
pix3dgl_scene_static_end_face_order_batch(struct Pix3DGL* pix3dgl)
{
    if( !pix3dgl || !pix3dgl->static_scene )
    {
        return;
    }
    pix3dgl->static_scene->in_batch_update = false;
    pix3dgl->static_scene->indices_dirty = true;
}

extern "C" void
pix3dgl_scene_static_set_model_face_order(
    struct Pix3DGL* pix3dgl, int scene_model_idx, int* face_indices, int face_count)
{
    if( !pix3dgl || !pix3dgl->static_scene )
    {
        printf("Static scene not initialized\n");
        return;
    }

    StaticScene* scene = pix3dgl->static_scene;

    // Check if the model exists
    auto it = scene->model_ranges.find(scene_model_idx);
    if( it == scene->model_ranges.end() )
    {
        printf("Model %d not found in static scene\n", scene_model_idx);
        return;
    }

    // Set the face order for this model
    std::vector<int>& face_order = scene->model_face_order[scene_model_idx];
    face_order.clear();
    face_order.reserve(face_count);

    for( int i = 0; i < face_count; i++ )
    {
        face_order.push_back(face_indices[i]);
    }

    // Only mark dirty if not in batch mode (batch mode marks dirty at end)
    if( !scene->in_batch_update )
    {
        scene->indices_dirty = true;
    }
}

extern "C" void
pix3dgl_scene_static_set_tile_face_order(
    struct Pix3DGL* pix3dgl, int scene_tile_idx, int* face_indices, int face_count)
{
    if( !pix3dgl || !pix3dgl->static_scene )
    {
        printf("Static scene not initialized\n");
        return;
    }

    StaticScene* scene = pix3dgl->static_scene;

    // Check if the tile exists
    auto it = scene->tile_ranges.find(scene_tile_idx);
    if( it == scene->tile_ranges.end() )
    {
        printf("Tile %d not found in static scene\n", scene_tile_idx);
        return;
    }

    // Set the face order for this tile
    std::vector<int>& face_order = scene->tile_face_order[scene_tile_idx];
    face_order.clear();
    face_order.reserve(face_count);

    for( int i = 0; i < face_count; i++ )
    {
        face_order.push_back(face_indices[i]);
    }

    // Only mark dirty if not in batch mode (batch mode marks dirty at end)
    if( !scene->in_batch_update )
    {
        scene->indices_dirty = true;
    }
}

extern "C" void
pix3dgl_scene_static_set_tile_draw_order(
    struct Pix3DGL* pix3dgl, int* scene_tile_indices, int count)
{
    if( !pix3dgl || !pix3dgl->static_scene )
    {
        printf("Static scene not initialized\n");
        return;
    }

    StaticScene* scene = pix3dgl->static_scene;

    // Update the tile draw order
    scene->tile_draw_order.clear();
    scene->tile_draw_order.reserve(count);

    for( int i = 0; i < count; i++ )
    {
        scene->tile_draw_order.push_back(scene_tile_indices[i]);
    }

    // Mark indices as dirty so they'll be rebuilt
    scene->indices_dirty = true;
}

extern "C" void
pix3dgl_scene_static_set_unified_draw_order(
    struct Pix3DGL* pix3dgl, bool* is_tile_array, int* index_array, int count, int stride)
{
    if( !pix3dgl || !pix3dgl->static_scene )
    {
        printf("Static scene not initialized\n");
        return;
    }

    StaticScene* scene = pix3dgl->static_scene;

    // Store unified draw order (models and tiles interleaved)
    scene->unified_draw_order.clear();
    scene->unified_draw_order.reserve(count);

    for( int i = 0; i < count; i++ )
    {
        bool* is_tile_ptr =
            reinterpret_cast<bool*>(reinterpret_cast<char*>(is_tile_array) + i * stride);
        int* index_ptr = reinterpret_cast<int*>(reinterpret_cast<char*>(index_array) + i * stride);

        scene->unified_draw_order.push_back({ *is_tile_ptr, *index_ptr });
    }

    // Mark indices as dirty so they'll be rebuilt with new order
    scene->indices_dirty = true;
}

extern "C" void
pix3dgl_scene_static_draw(struct Pix3DGL* pix3dgl)
{
    if( !pix3dgl || !pix3dgl->static_scene )
    {
        return;
    }

    StaticScene* scene = pix3dgl->static_scene;

    if( !scene->is_finalized )
    {
        printf("Static scene not finalized, call pix3dgl_scene_static_end() first\n");
        return;
    }

    if( scene->total_vertex_count == 0 )
    {
        return;
    }

    // Set identity model matrix (geometry is already transformed)
    float identity[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                           0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

    if( pix3dgl->uniform_model_matrix >= 0 )
    {
        glUniformMatrix4fv(pix3dgl->uniform_model_matrix, 1, GL_FALSE, identity);
    }

    // Bind texture atlas (UVs are already in atlas space, pre-calculated on CPU!)
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, pix3dgl->texture_atlas);
    pix3dgl->currently_bound_texture = pix3dgl->texture_atlas;

    if( pix3dgl->uniform_texture_atlas >= 0 )
    {
        glUniform1i(pix3dgl->uniform_texture_atlas, 0);
    }

    // Bind the static scene VAO (this also binds the dynamicEBO)
    BIND_VERTEX_ARRAY(scene->VAO);

    // Draw models in the specified order, or all models if no order is set
    if( !scene->draw_order.empty() || !scene->unified_draw_order.empty() )
    {
        // Rebuild index buffer if face order has changed (indices_dirty flag)
        if( scene->indices_dirty )
        {
            // OPTIMIZATION: Don't clear, just reset write position
            // This avoids deallocation and keeps capacity
            int write_idx = 0;
            scene->model_index_ranges.clear();

            // Use unified draw order if available (models and tiles interleaved)
            if( !scene->unified_draw_order.empty() )
            {
                // Iterate through unified order (tiles and models interleaved)
                for( const auto& draw_item : scene->unified_draw_order )
                {
                    if( draw_item.is_tile )
                    {
                        // Draw a tile
                        int scene_tile_idx = draw_item.index;
                        auto tile_it = scene->tile_ranges.find(scene_tile_idx);
                        if( tile_it == scene->tile_ranges.end() )
                            continue;

                        const ModelRange& range = tile_it->second;

                        // Check if we have a face order for this tile
                        auto face_order_it = scene->tile_face_order.find(scene_tile_idx);
                        if( face_order_it != scene->tile_face_order.end() &&
                            !face_order_it->second.empty() )
                        {
                            // Use sorted face order
                            const std::vector<int>& face_order = face_order_it->second;
                            for( int face_idx : face_order )
                            {
                                if( face_idx >= 0 && face_idx < (int)range.faces.size() )
                                {
                                    const FaceRange& face_range = range.faces[face_idx];
                                    if( face_range.start_vertex >= 0 )
                                    {
                                        GLuint base = face_range.start_vertex;
                                        if( write_idx + 3 > (int)scene->sorted_indices.size() )
                                        {
                                            scene->sorted_indices.resize(write_idx + 3);
                                        }
                                        scene->sorted_indices[write_idx++] = base;
                                        scene->sorted_indices[write_idx++] = base + 1;
                                        scene->sorted_indices[write_idx++] = base + 2;
                                    }
                                }
                            }
                        }
                        else
                        {
                            // No face order, add all faces in original order
                            for( size_t face_idx = 0; face_idx < range.faces.size(); face_idx++ )
                            {
                                const FaceRange& face_range = range.faces[face_idx];
                                if( face_range.start_vertex >= 0 )
                                {
                                    GLuint base = face_range.start_vertex;
                                    if( write_idx + 3 > (int)scene->sorted_indices.size() )
                                    {
                                        scene->sorted_indices.resize(write_idx + 3);
                                    }
                                    scene->sorted_indices[write_idx++] = base;
                                    scene->sorted_indices[write_idx++] = base + 1;
                                    scene->sorted_indices[write_idx++] = base + 2;
                                }
                            }
                        }
                    }
                    else
                    {
                        // Draw a model
                        int scene_model_idx = draw_item.index;
                        auto model_it = scene->model_ranges.find(scene_model_idx);
                        if( model_it == scene->model_ranges.end() )
                            continue;

                        const ModelRange& range = model_it->second;

                        // Check if we have a face order for this model
                        auto face_order_it = scene->model_face_order.find(scene_model_idx);
                        if( face_order_it != scene->model_face_order.end() &&
                            !face_order_it->second.empty() )
                        {
                            int start_index = write_idx;
                            const std::vector<int>& face_order = face_order_it->second;

                            for( int face_idx : face_order )
                            {
                                if( face_idx >= 0 && face_idx < (int)range.faces.size() )
                                {
                                    const FaceRange& face_range = range.faces[face_idx];
                                    if( face_range.start_vertex >= 0 )
                                    {
                                        GLuint base = face_range.start_vertex;
                                        if( write_idx + 3 > (int)scene->sorted_indices.size() )
                                        {
                                            scene->sorted_indices.resize(write_idx + 3);
                                        }
                                        scene->sorted_indices[write_idx++] = base;
                                        scene->sorted_indices[write_idx++] = base + 1;
                                        scene->sorted_indices[write_idx++] = base + 2;
                                    }
                                }
                            }

                            int count = write_idx - start_index;
                            if( count > 0 )
                            {
                                scene->model_index_ranges[scene_model_idx] = { start_index, count };
                            }
                        }
                        else
                        {
                            int start_index = write_idx;
                            for( size_t face_idx = 0; face_idx < range.faces.size(); face_idx++ )
                            {
                                const FaceRange& face_range = range.faces[face_idx];
                                if( face_range.start_vertex >= 0 )
                                {
                                    GLuint base = face_range.start_vertex;
                                    if( write_idx + 3 > (int)scene->sorted_indices.size() )
                                    {
                                        scene->sorted_indices.resize(write_idx + 3);
                                    }
                                    scene->sorted_indices[write_idx++] = base;
                                    scene->sorted_indices[write_idx++] = base + 1;
                                    scene->sorted_indices[write_idx++] = base + 2;
                                }
                            }

                            int count = write_idx - start_index;
                            if( count > 0 )
                            {
                                scene->model_index_ranges[scene_model_idx] = { start_index, count };
                            }
                        }
                    }
                }
            }
            else
            {
                // Legacy path: Build sorted index buffer by iterating through draw_order
                for( int scene_model_idx : scene->draw_order )
                {
                    auto model_it = scene->model_ranges.find(scene_model_idx);
                    if( model_it == scene->model_ranges.end() )
                        continue;

                    const ModelRange& range = model_it->second;

                    // Check if we have a face order for this model
                    auto face_order_it = scene->model_face_order.find(scene_model_idx);
                    if( face_order_it != scene->model_face_order.end() &&
                        !face_order_it->second.empty() )
                    {
                        // Record where this model's indices start
                        int start_index = write_idx;

                        const std::vector<int>& face_order = face_order_it->second;

                        // Add indices for each face in the sorted order
                        for( int face_idx : face_order )
                        {
                            if( face_idx >= 0 && face_idx < (int)range.faces.size() )
                            {
                                const FaceRange& face_range = range.faces[face_idx];
                                if( face_range.start_vertex >= 0 )
                                {
                                    // Add the 3 vertex indices for this triangle
                                    GLuint base = face_range.start_vertex;

                                    // Resize only if needed (rare after first frame)
                                    if( write_idx + 3 > (int)scene->sorted_indices.size() )
                                    {
                                        scene->sorted_indices.resize(write_idx + 3);
                                    }

                                    scene->sorted_indices[write_idx++] = base;
                                    scene->sorted_indices[write_idx++] = base + 1;
                                    scene->sorted_indices[write_idx++] = base + 2;
                                }
                            }
                        }

                        // Record the index range for this model
                        int count = write_idx - start_index;
                        if( count > 0 )
                        {
                            scene->model_index_ranges[scene_model_idx] = { start_index, count };
                        }
                    }
                    else
                    {
                        // No face order specified, add all faces in original order
                        int start_index = write_idx;

                        for( size_t face_idx = 0; face_idx < range.faces.size(); face_idx++ )
                        {
                            const FaceRange& face_range = range.faces[face_idx];
                            if( face_range.start_vertex >= 0 )
                            {
                                GLuint base = face_range.start_vertex;

                                if( write_idx + 3 > (int)scene->sorted_indices.size() )
                                {
                                    scene->sorted_indices.resize(write_idx + 3);
                                }

                                scene->sorted_indices[write_idx++] = base;
                                scene->sorted_indices[write_idx++] = base + 1;
                                scene->sorted_indices[write_idx++] = base + 2;
                            }
                        }

                        int count = write_idx - start_index;
                        if( count > 0 )
                        {
                            scene->model_index_ranges[scene_model_idx] = { start_index, count };
                        }
                    }
                }

                // Now add tiles in their draw order (legacy path)
                for( int scene_tile_idx : scene->tile_draw_order )
                {
                    auto tile_it = scene->tile_ranges.find(scene_tile_idx);
                    if( tile_it == scene->tile_ranges.end() )
                        continue;

                    const ModelRange& range = tile_it->second;

                    // Check if we have a face order for this tile
                    auto face_order_it = scene->tile_face_order.find(scene_tile_idx);
                    if( face_order_it != scene->tile_face_order.end() &&
                        !face_order_it->second.empty() )
                    {
                        // Add indices for each face in the sorted order
                        const std::vector<int>& face_order = face_order_it->second;

                        for( int face_idx : face_order )
                        {
                            if( face_idx >= 0 && face_idx < (int)range.faces.size() )
                            {
                                const FaceRange& face_range = range.faces[face_idx];
                                if( face_range.start_vertex >= 0 )
                                {
                                    // Add the 3 vertex indices for this triangle
                                    GLuint base = face_range.start_vertex;

                                    // Resize only if needed (rare after first frame)
                                    if( write_idx + 3 > (int)scene->sorted_indices.size() )
                                    {
                                        scene->sorted_indices.resize(write_idx + 3);
                                    }

                                    scene->sorted_indices[write_idx++] = base;
                                    scene->sorted_indices[write_idx++] = base + 1;
                                    scene->sorted_indices[write_idx++] = base + 2;
                                }
                            }
                        }
                    }
                    else
                    {
                        // No face order specified, add all faces in original order
                        for( size_t face_idx = 0; face_idx < range.faces.size(); face_idx++ )
                        {
                            const FaceRange& face_range = range.faces[face_idx];
                            if( face_range.start_vertex >= 0 )
                            {
                                GLuint base = face_range.start_vertex;

                                if( write_idx + 3 > (int)scene->sorted_indices.size() )
                                {
                                    scene->sorted_indices.resize(write_idx + 3);
                                }

                                scene->sorted_indices[write_idx++] = base;
                                scene->sorted_indices[write_idx++] = base + 1;
                                scene->sorted_indices[write_idx++] = base + 2;
                            }
                        }
                    }
                }
            }

            // Trim to actual size if we over-allocated
            if( write_idx < (int)scene->sorted_indices.size() )
            {
                scene->sorted_indices.resize(write_idx);
            }

            // Upload sorted indices to GPU using glBufferSubData
            if( !scene->sorted_indices.empty() )
            {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, scene->dynamicEBO);
                glBufferSubData(
                    GL_ELEMENT_ARRAY_BUFFER,
                    0,
                    scene->sorted_indices.size() * sizeof(GLuint),
                    scene->sorted_indices.data());
            }

            scene->indices_dirty = false;

            static bool printed_once = false;
            if( !printed_once )
            {
                if( !scene->unified_draw_order.empty() )
                {
                    printf(
                        "Built index buffer with %d indices for %d items (models + tiles "
                        "interleaved, painter's algorithm)\n",
                        (int)scene->sorted_indices.size(),
                        (int)scene->unified_draw_order.size());
                }
                else
                {
                    printf(
                        "Built index buffer with %d indices for %d models and %d tiles (painter's "
                        "algorithm)\n",
                        (int)scene->sorted_indices.size(),
                        (int)scene->draw_order.size(),
                        (int)scene->tile_draw_order.size());
                }
                printed_once = true;
            }
        }

        // Draw the entire scene in a single glDrawElements call!
        // This is MUCH faster than glMultiDrawArrays
        if( !scene->sorted_indices.empty() )
        {
            glDrawElements(
                GL_TRIANGLES,
                (GLsizei)scene->sorted_indices.size(),
                GL_UNSIGNED_INT,
                nullptr); // Offset 0 in the bound EBO

            static bool printed_once = false;
            if( !printed_once )
            {
                printf(
                    "Rendering %d models with dynamic index buffer (single glDrawElements call)!\n",
                    (int)scene->draw_order.size());
                printed_once = true;
            }
        }
    }
    else
    {
        // Fallback: draw entire scene in a single call (original behavior)
        glDrawArrays(GL_TRIANGLES, 0, scene->total_vertex_count);

        static bool printed_once = false;
        if( !printed_once )
        {
            printf(
                "Rendering %d vertices with texture atlas in SINGLE draw call (no order set)!\n",
                scene->total_vertex_count);
            printed_once = true;
        }
    }

    BIND_VERTEX_ARRAY(0);

    // Re-enable blending for other rendering
    glEnable(GL_BLEND);
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
            DELETE_VERTEX_ARRAYS(1, &model.VAO);
            glDeleteBuffers(1, &model.VBO);
            glDeleteBuffers(1, &model.colorVBO);
            glDeleteBuffers(1, &model.texCoordVBO);
            glDeleteBuffers(1, &model.EBO);
        }

        // Note: With atlas, individual textures are stored as slots in texture_ids map
        // but we don't delete them individually anymore - they're part of the atlas

        // Delete texture atlas
        if( pix3dgl->texture_atlas )
        {
            glDeleteTextures(1, &pix3dgl->texture_atlas);
        }

        // Clean up static scene if it exists
        if( pix3dgl->static_scene )
        {
            DELETE_VERTEX_ARRAYS(1, &pix3dgl->static_scene->VAO);
            glDeleteBuffers(1, &pix3dgl->static_scene->VBO);
            glDeleteBuffers(1, &pix3dgl->static_scene->colorVBO);
            glDeleteBuffers(1, &pix3dgl->static_scene->texCoordVBO);
            glDeleteBuffers(1, &pix3dgl->static_scene->textureIdVBO);
            if( pix3dgl->static_scene->dynamicEBO != 0 )
            {
                glDeleteBuffers(1, &pix3dgl->static_scene->dynamicEBO);
            }
            delete pix3dgl->static_scene;
        }

        // Clean up shader program
        if( pix3dgl->program_es2 )
        {
            glDeleteProgram(pix3dgl->program_es2);
        }

        delete pix3dgl;
    }
}