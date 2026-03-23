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

#include "graphics/shared_tables.h"
#include "graphics/uv_pnm.h"
#include "pix3dgl.h"

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
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
// iOS uses OpenGL ES 2.0
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#define GLES2_IOS
#else
// macOS builds use desktop OpenGL headers for this ES2-style implementation.
#include <OpenGL/gl3.h>
#define GLES2_DESKTOP
#endif
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
typedef void (*PFNGLGENVERTEXARRAYSOESPROC)(
    GLsizei n,
    GLuint* arrays);
typedef void (*PFNGLBINDVERTEXARRAYOESPROC)(GLuint array);
typedef void (*PFNGLDELETEVERTEXARRAYSOESPROC)(
    GLsizei n,
    const GLuint* arrays);

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
precision highp float;

attribute vec3 aPos;
attribute vec4 aColor;
attribute vec2 aTexCoord;
attribute float aTextureId;
attribute float aTextureOpaque;
attribute float aTextureAnimSpeed;

uniform mat4 uViewMatrix;       // Precomputed on CPU
uniform mat4 uProjectionMatrix; // Precomputed on CPU
uniform mat4 uModelMatrix;      // Per-model transformation
uniform mediump float uClock;   // Animation clock

varying vec4 vColor;
varying highp vec2 vTexCoord;
varying float vTexBlend;       // 1.0 if textured, 0.0 if not
varying float vAtlasSlot;      // Atlas slot ID for wrapping
varying float vTextureOpaque;  // 1.0 if opaque, 0.0 if transparent
varying float vTextureAnimSpeed; // Animation speed

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
    vTextureOpaque = aTextureOpaque;
    vTextureAnimSpeed = aTextureAnimSpeed;
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
varying float vTextureOpaque;
varying float vTextureAnimSpeed;

uniform sampler2D uTextureAtlas;
uniform highp float uClock;

void main() {
    highp vec2 finalTexCoord = vTexCoord;
    
    // Apply texture wrapping if this is a textured face
    if (vTexBlend > 0.5 && vAtlasSlot >= 0.0) {
        // Atlas configuration (must match CPU values)
        highp float atlasSize = 2048.0;
        highp float tileSize = 128.0;
        highp float tilesPerRow = 16.0;
        highp float tileSizeNorm = tileSize / atlasSize;  // Size of one tile in normalized coords
        
        // Calculate tile position in atlas
        highp float tileU = mod(vAtlasSlot, tilesPerRow);
        highp float tileV = floor(vAtlasSlot / tilesPerRow);
        highp vec2 tileOrigin = vec2(tileU, tileV) * tileSizeNorm;
        
        // Extract local UV within the tile (0 to 1 range within the tile)
        highp vec2 localUV = (vTexCoord - tileOrigin) / tileSizeNorm;
        
        // Apply texture animation based on direction encoded in speed sign
        // Positive speed = U direction, Negative speed = V direction
        if (vTextureAnimSpeed > 0.0) {
            // U direction animation (horizontal)
            localUV.x = localUV.x + (uClock * vTextureAnimSpeed);
        } else if (vTextureAnimSpeed < 0.0) {
            // V direction animation (vertical)
            localUV.y = localUV.y - (uClock * -vTextureAnimSpeed);
        }
        
        // Match software rasterizer behavior:
        // - U is clamped (no horizontal tiling)
        // - V is tiled/repeated (vertical wrapping)
        // Inset by 0.008/0.992 to prevent bilinear sampling from bleeding into zero-alpha
        // empty margins between atlas tile slots.
        localUV.x = clamp(localUV.x, 0.008, 0.992);          // Clamp U
        localUV.y = clamp(fract(localUV.y), 0.008, 0.992);   // Tile V
        
        // Transform back to atlas space
        finalTexCoord = tileOrigin + localUV * tileSizeNorm;
    }
    
    // Sample texture with wrapped coordinates
    vec4 texColor = texture2D(uTextureAtlas, finalTexCoord);
    
    // Blend between pure vertex color and textured color
    // vTexBlend = 1.0 for textured, 0.0 for untextured
    vec3 finalColor = mix(vColor.rgb, texColor.rgb * vColor.rgb, vTexBlend);
    
    // Handle transparency based on texture opaque flag and face alpha
    float finalAlpha = vColor.a;
    
    if (vTexBlend > 0.5) {
        // This is a textured face.
        // vTextureOpaque == 0.0  →  texture has transparent regions (black = transparent).
        // vTextureOpaque == 1.0  →  texture is fully opaque (no transparent pixels).
        if (vTextureOpaque < 0.5) {
            // Transparent texture: discard pixels whose alpha was set to 0 at upload time.
            // Using the alpha channel directly is exact; luminance thresholds can discard
            // valid dark-but-non-transparent pixels.
            if (texColor.a < 0.5) {
                discard;
            }
            finalAlpha = vColor.a;
        } else {
            // Opaque texture: every pixel is solid.  texColor.a == 1.0 here by construction.
            finalAlpha = vColor.a;
        }
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
    GLuint textureIdVBO;
    GLuint textureOpaqueVBO;
    GLuint textureAnimSpeedVBO;

    int face_count;
    bool has_textures;
    int first_texture_id;           // First texture ID used by this model (for simple rendering)
    std::vector<int> face_textures; // Texture ID per face (-1 = no texture)
    std::vector<bool> face_visible; // Whether each face should be drawn
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
    GLuint textureIdVBO;        // New: stores texture ID per vertex
    GLuint textureOpaqueVBO;    // New: stores texture opaque flag per vertex
    GLuint textureAnimSpeedVBO; // New: stores texture animation speed per vertex
    GLuint dynamicEBO;          // Dynamic index buffer for painter's algorithm ordering

    int total_vertex_count;
    std::vector<float> vertices;
    std::vector<float> colors; // RGBA: 4 floats per vertex (RGB + Alpha)
    std::vector<float> texCoords;
    std::vector<float> textureIds;        // New: texture ID per vertex
    std::vector<float> textureOpaques;    // New: texture opaque flag per vertex
    std::vector<float> textureAnimSpeeds; // New: texture animation speed per vertex

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

    // Animation support - track keyframes for animated models
    struct AnimatedModelData
    {
        int scene_model_idx;
        int frame_count;
        int current_frame_step;
        int frame_tick_count;
        std::vector<int> frame_lengths;          // Duration of each frame in ticks
        std::vector<ModelRange> keyframe_ranges; // One ModelRange per keyframe
    };
    std::unordered_map<int, AnimatedModelData> animated_models; // Key: scene_model_idx

    // Current animated model being built (used during loading)
    AnimatedModelData* current_animated_model;
};

struct Pix3DGL
{
    GLuint vertex_shader_es2;
    GLuint fragment_shader_es2;
    GLuint program_es2;

    std::unordered_map<int, GLuint> texture_ids;
    std::unordered_map<int, bool> texture_to_opaque;           // Maps texture_id -> opaque flag
    std::unordered_map<int, float> texture_to_animation_speed; // Maps texture_id -> animation speed
    std::unordered_map<int, GLModel> models;
    std::unordered_map<int, GLModel> tiles;

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
    GLint uniform_clock; // New: animation clock uniform

    // State tracking to avoid redundant GL calls
    GLuint currently_bound_texture;
    int current_texture_state; // 0 = texture disabled, 1 = texture enabled

    // Clock for texture animation
    float animation_clock;

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
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw)
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
compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height)
{
    float y = 1.0f / tan(fov * 0.5f);
    float x = y;
    // Software renderer clips against z >= 50 and has no practical far-plane clip.
    // Use a large far value while keeping proper perspective depth so GL depth
    // ordering matches the software math/path.
    const float near_plane_z = 50.0f;
    const float far_plane_z = 65536.0f;
    const float depth_a = (far_plane_z + near_plane_z) / (far_plane_z - near_plane_z);
    const float depth_b = (-2.0f * far_plane_z * near_plane_z) / (far_plane_z - near_plane_z);

    // Column-major for OpenGL
    // The 512.0f / (screen_width/2.0f) and 512.0f / (screen_height/2.0f) scaling
    // already accounts for aspect ratio correctly for this renderer's coordinate system
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
    out_matrix[10] = depth_a;
    out_matrix[11] = 1.0f;

    out_matrix[12] = 0.0f;
    out_matrix[13] = 0.0f;
    out_matrix[14] = depth_b;
    out_matrix[15] = 0.0f;
}

// Helper function to compile shaders
static GLuint
compile_shader(
    GLenum type,
    const char* source)
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
create_shader_program(
    const char* vertex_source,
    const char* fragment_source)
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

    // CRITICAL: Bind attribute locations BEFORE linking
    // ES2/WebGL1 doesn't support layout(location=N) in shaders!
    glBindAttribLocation(program, 0, "aPos");
    glBindAttribLocation(program, 1, "aColor");
    glBindAttribLocation(program, 2, "aTexCoord");
    glBindAttribLocation(program, 3, "aTextureId");
    glBindAttribLocation(program, 4, "aTextureOpaque");
    glBindAttribLocation(program, 5, "aTextureAnimSpeed");

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

    // Initialize animation clock
    pix3dgl->animation_clock = 0.0f;

    pix3dgl->program_es2 = create_shader_program(g_vertex_shader_es2, g_fragment_shader_es2);

    if( !pix3dgl->program_es2 )
    {
        printf("Failed to create ES2/WebGL1 shader program\n");
        delete pix3dgl;
        return nullptr;
    }

    printf("Shader program created successfully: %u\n", pix3dgl->program_es2);

    // Verify attribute locations
    GLint aPos = glGetAttribLocation(pix3dgl->program_es2, "aPos");
    GLint aColor = glGetAttribLocation(pix3dgl->program_es2, "aColor");
    GLint aTexCoord = glGetAttribLocation(pix3dgl->program_es2, "aTexCoord");
    GLint aTextureId = glGetAttribLocation(pix3dgl->program_es2, "aTextureId");
    GLint aTextureOpaque = glGetAttribLocation(pix3dgl->program_es2, "aTextureOpaque");

    printf("Attribute locations:\n");
    printf("  aPos: %d (expected 0)\n", aPos);
    printf("  aColor: %d (expected 1)\n", aColor);
    printf("  aTexCoord: %d (expected 2)\n", aTexCoord);
    printf("  aTextureId: %d (expected 3)\n", aTextureId);
    printf("  aTextureOpaque: %d (expected 4)\n", aTextureOpaque);

    if( aPos != 0 || aColor != 1 || aTexCoord != 2 || aTextureId != 3 || aTextureOpaque != 4 )
    {
        printf("WARNING: Attribute locations don't match expected values!\n");
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
    pix3dgl->uniform_clock = glGetUniformLocation(pix3dgl->program_es2, "uClock");

    printf(
        "Cached uniform locations - model_matrix:%d view_matrix:%d projection_matrix:%d "
        "use_texture:%d texture:%d atlas:%d clock:%d\n",
        pix3dgl->uniform_model_matrix,
        pix3dgl->uniform_view_matrix,
        pix3dgl->uniform_projection_matrix,
        pix3dgl->uniform_use_texture,
        pix3dgl->uniform_texture,
        pix3dgl->uniform_texture_atlas,
        pix3dgl->uniform_clock);

    // Initialize texture atlas
    pix3dgl->atlas_tile_size = 128;
    pix3dgl->atlas_size = 2048; // 2048x2048 atlas can hold 16x16 = 256 textures of 128x128
    pix3dgl->atlas_tiles_per_row = pix3dgl->atlas_size / pix3dgl->atlas_tile_size;

    glGenTextures(1, &pix3dgl->texture_atlas);
    glBindTexture(GL_TEXTURE_2D, pix3dgl->texture_atlas);

    // Create empty atlas texture.
    // Use GL_RGBA8 (sized internal format) — required on Core Profile / Metal-backed GL;
    // GL_RGBA (unsized) causes "unloadable texture" warnings and falls back to zero-alpha.
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
    printf("========================================\n");
    printf("OpenGL ES 2.0 / WebGL1 Renderer: %s\n", renderer ? (const char*)renderer : "(null)");
    printf("OpenGL ES Version: %s\n", version ? (const char*)version : "(null)");
    printf("OpenGL ES Vendor: %s\n", vendor ? (const char*)vendor : "(null)");
    printf("========================================\n");

    // glGetString(GL_EXTENSIONS) is illegal in a Core Profile context (OpenGL 3.1+) and
    // generates GL_INVALID_ENUM, poisoning the error state for subsequent checks.
    // On non-Emscripten / non-iOS platforms assume Core Profile capabilities are present.
    bool has_vao = false;
    bool has_uint_indices = false;
    bool has_npot = false;
#if defined(__EMSCRIPTEN__)
    {
        const GLubyte* extensions = glGetString(GL_EXTENSIONS);
        const char* ext_str = extensions ? (const char*)extensions : "";
        has_vao = strstr(ext_str, "OES_vertex_array_object") != nullptr;
        has_uint_indices = strstr(ext_str, "OES_element_index_uint") != nullptr;
        has_npot = strstr(ext_str, "OES_texture_npot") != nullptr ||
                   strstr(ext_str, "ARB_texture_non_power_of_two") != nullptr;
    }
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    {
        const GLubyte* extensions = glGetString(GL_EXTENSIONS);
        const char* ext_str = extensions ? (const char*)extensions : "";
        has_vao = strstr(ext_str, "OES_vertex_array_object") != nullptr;
        has_uint_indices = strstr(ext_str, "OES_element_index_uint") != nullptr;
        has_npot = strstr(ext_str, "OES_texture_npot") != nullptr;
    }
#else
    // Desktop Core Profile: VAO and uint indices are core features; don't call GL_EXTENSIONS.
    has_vao = true;
    has_uint_indices = true;
    has_npot = true;
#endif

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
        printf("========================================\n");
        printf("CRITICAL ERROR: OES_element_index_uint NOT SUPPORTED!\n");
        printf("This device cannot render with GL_UNSIGNED_INT indices.\n");
        printf("Scenes with >65535 vertices will NOT render!\n");
        printf("========================================\n");
    }

    // Check if we're using software rendering (this would be a problem!)
    const char* renderer_str = renderer ? (const char*)renderer : "";
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
    const int* texels_argb,
    int width,
    int height,
    int animation_direction,
    int animation_speed,
    bool opaque)
{
    if( !pix3dgl || !texels_argb || width <= 0 || height <= 0 )
        return;

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

    // Store the opaque flag for this texture
    pix3dgl->texture_to_opaque[texture_id] = opaque;

    // Store the animation speed for this texture
    // Encode direction in the sign: positive for U direction, negative for V direction
    float anim_speed = 0.0f;
    if( animation_direction != 0 )
    {
        // Use actual animation speed from texture definition (increase multiplier for faster
        // animation) 128 is the size of the texture.
        float speed = ((float)animation_speed) / (128.0f / 50.0f);

        // Encode direction: U directions use positive, V directions use negative
        if( animation_direction == 2 || animation_direction == 4 )
        {
            anim_speed = speed; // Positive for U direction
        }
        else // V_DOWN or V_UP
        {
            anim_speed = -speed; // Negative for V direction
        }
    }
    pix3dgl->texture_to_animation_speed[texture_id] = anim_speed;

    // Convert texture data
    std::vector<unsigned char> texture_bytes((size_t)width * (size_t)height * 4u);
    for( int i = 0; i < width * height; i++ )
    {
        int pixel = texels_argb[i];
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
        width,
        height,
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

    // Set animation clock uniform for texture animations
    if( pix3dgl->uniform_clock >= 0 )
    {
        glUniform1f(pix3dgl->uniform_clock, pix3dgl->animation_clock);
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

    // Enable depth testing so geometry is correctly occluded regardless of draw order.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    // Enable alpha blending for transparent faces.
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

    // Set animation clock uniform for texture animations
    if( pix3dgl->uniform_clock >= 0 )
    {
        glUniform1f(pix3dgl->uniform_clock, pix3dgl->animation_clock);
    }

    // Bind the texture atlas once for the whole frame.
    // texture_ids maps texture_id -> atlas_slot (an integer index, NOT a GL handle).
    // The atlas GL object is the only texture we ever bind; the shader uses the per-vertex
    // aTextureId attribute to select the correct tile inside the atlas.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, pix3dgl->texture_atlas);
    if( pix3dgl->uniform_texture_atlas >= 0 )
    {
        glUniform1i(pix3dgl->uniform_texture_atlas, 0);
    }
    pix3dgl->currently_bound_texture = pix3dgl->texture_atlas;

    // Reset state tracking for new frame
    pix3dgl->current_texture_state = -1;

    // Clear batching data for new frame
    pix3dgl->draw_batches.clear();
    pix3dgl->scene_batches.clear();
}

extern "C" void
pix3dgl_model_load(
    struct Pix3DGL* pix3dgl,
    int model_idx,
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
    int* face_alphas_nullable)
{
    if( !pix3dgl || face_count <= 0 || !vertices_x || !vertices_y || !vertices_z ||
        !face_indices_a || !face_indices_b || !face_indices_c || !face_colors_hsl_a ||
        !face_colors_hsl_b || !face_colors_hsl_c )
        return;

    if( pix3dgl->models.find(model_idx) != pix3dgl->models.end() )
        return;

    GLModel model = {};
    model.idx = model_idx;
    model.face_count = face_count;
    model.has_textures = false;
    model.first_texture_id = -1;
    model.face_textures.resize((size_t)face_count, -1);
    model.face_visible.resize((size_t)face_count, true);
    model.face_shading.resize((size_t)face_count, SHADING_GOURAUD);

    std::vector<float> verts;
    std::vector<float> colors;
    std::vector<float> uvs;
    std::vector<float> texture_ids;
    std::vector<float> texture_opaques;
    std::vector<float> texture_anim_speeds;
    verts.reserve((size_t)face_count * 9u);
    colors.reserve((size_t)face_count * 12u);
    uvs.reserve((size_t)face_count * 6u);
    texture_ids.reserve((size_t)face_count * 3u);
    texture_opaques.reserve((size_t)face_count * 3u);
    texture_anim_speeds.reserve((size_t)face_count * 3u);

    for( int face = 0; face < face_count; ++face )
    {
        bool visible = true;
        if( face_infos_nullable && face_infos_nullable[face] == 2 )
            visible = false;
        if( face_colors_hsl_c[face] == -2 )
            visible = false;
        model.face_visible[(size_t)face] = visible;

        int ia = face_indices_a[face];
        int ib = face_indices_b[face];
        int ic = face_indices_c[face];
        int idxs[3] = { ia, ib, ic };
        for( int i = 0; i < 3; ++i )
        {
            int vi = idxs[i];
            verts.push_back((float)vertices_x[vi]);
            verts.push_back((float)vertices_y[vi]);
            verts.push_back((float)vertices_z[vi]);
        }

        int hsl_a = face_colors_hsl_a[face];
        int hsl_b = face_colors_hsl_b[face];
        int hsl_c = face_colors_hsl_c[face];
        int rgb_a, rgb_b, rgb_c;
        if( hsl_c == -1 )
        {
            rgb_a = rgb_b = rgb_c = g_hsl16_to_rgb_table[hsl_a];
            model.face_shading[(size_t)face] = SHADING_FLAT;
        }
        else
        {
            rgb_a = g_hsl16_to_rgb_table[hsl_a];
            rgb_b = g_hsl16_to_rgb_table[hsl_b];
            rgb_c = g_hsl16_to_rgb_table[hsl_c];
            model.face_shading[(size_t)face] = SHADING_GOURAUD;
        }

        float face_alpha = 1.0f;
        if( face_alphas_nullable )
        {
            int alpha_byte = face_alphas_nullable[face] & 0xFF;
            if( face_textures_nullable && face_textures_nullable[face] != -1 )
                face_alpha = alpha_byte / 255.0f;
            else
                face_alpha = (0xFF - alpha_byte) / 255.0f;
        }

        int rgbs[3] = { rgb_a, rgb_b, rgb_c };
        for( int i = 0; i < 3; ++i )
        {
            colors.push_back(((rgbs[i] >> 16) & 0xFF) / 255.0f);
            colors.push_back(((rgbs[i] >> 8) & 0xFF) / 255.0f);
            colors.push_back((rgbs[i] & 0xFF) / 255.0f);
            colors.push_back(face_alpha);
        }

        int texture_id = (face_textures_nullable ? face_textures_nullable[face] : -1);
        int atlas_slot = -1;
        if( texture_id != -1 )
        {
            auto tex_it = pix3dgl->texture_ids.find(texture_id);
            if( tex_it != pix3dgl->texture_ids.end() )
            {
                atlas_slot = tex_it->second;
                model.has_textures = true;
                if( model.first_texture_id == -1 )
                    model.first_texture_id = texture_id;
            }
            else
            {
                texture_id = -1;
            }
        }
        model.face_textures[(size_t)face] = texture_id;

        if( texture_id != -1 && face_texture_coords_nullable && textured_p_coordinate_nullable &&
            textured_m_coordinate_nullable && textured_n_coordinate_nullable )
        {
            int texture_face_idx = face_texture_coords_nullable[face];
            if( texture_face_idx == -1 )
                texture_face_idx = face;

            int tp = textured_p_coordinate_nullable[texture_face_idx];
            int tm = textured_m_coordinate_nullable[texture_face_idx];
            int tn = textured_n_coordinate_nullable[texture_face_idx];

            struct UVFaceCoords uv;
            uv_pnm_compute(
                &uv,
                (float)vertices_x[tp],
                (float)vertices_y[tp],
                (float)vertices_z[tp],
                (float)vertices_x[tm],
                (float)vertices_y[tm],
                (float)vertices_z[tm],
                (float)vertices_x[tn],
                (float)vertices_y[tn],
                (float)vertices_z[tn],
                (float)vertices_x[ia],
                (float)vertices_y[ia],
                (float)vertices_z[ia],
                (float)vertices_x[ib],
                (float)vertices_y[ib],
                (float)vertices_z[ib],
                (float)vertices_x[ic],
                (float)vertices_y[ic],
                (float)vertices_z[ic]);

            float tile_u = (float)(atlas_slot % pix3dgl->atlas_tiles_per_row);
            float tile_v = (float)(atlas_slot / pix3dgl->atlas_tiles_per_row);
            float tile_size = (float)pix3dgl->atlas_tile_size;
            float atlas_size = (float)pix3dgl->atlas_size;
            uvs.push_back((tile_u + uv.u1) * tile_size / atlas_size);
            uvs.push_back((tile_v + uv.v1) * tile_size / atlas_size);
            uvs.push_back((tile_u + uv.u2) * tile_size / atlas_size);
            uvs.push_back((tile_v + uv.v2) * tile_size / atlas_size);
            uvs.push_back((tile_u + uv.u3) * tile_size / atlas_size);
            uvs.push_back((tile_v + uv.v3) * tile_size / atlas_size);
        }
        else
        {
            for( int i = 0; i < 6; ++i )
                uvs.push_back(0.0f);
            atlas_slot = -1;
        }

        float is_opaque = 1.0f;
        if( texture_id != -1 )
        {
            auto op_it = pix3dgl->texture_to_opaque.find(texture_id);
            if( op_it != pix3dgl->texture_to_opaque.end() )
                is_opaque = op_it->second ? 1.0f : 0.0f;
        }
        float anim_speed = 0.0f;
        if( texture_id != -1 )
        {
            auto an_it = pix3dgl->texture_to_animation_speed.find(texture_id);
            if( an_it != pix3dgl->texture_to_animation_speed.end() )
                anim_speed = an_it->second;
        }
        for( int i = 0; i < 3; ++i )
        {
            texture_ids.push_back((float)atlas_slot);
            texture_opaques.push_back(is_opaque);
            texture_anim_speeds.push_back(anim_speed);
        }
    }

    GEN_VERTEX_ARRAYS(1, &model.VAO);
    BIND_VERTEX_ARRAY(model.VAO);

    glGenBuffers(1, &model.VBO);
    glBindBuffer(GL_ARRAY_BUFFER, model.VBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glGenBuffers(1, &model.colorVBO);
    glBindBuffer(GL_ARRAY_BUFFER, model.colorVBO);
    glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(float), colors.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    glGenBuffers(1, &model.texCoordVBO);
    glBindBuffer(GL_ARRAY_BUFFER, model.texCoordVBO);
    glBufferData(GL_ARRAY_BUFFER, uvs.size() * sizeof(float), uvs.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(2);

    glGenBuffers(1, &model.textureIdVBO);
    glBindBuffer(GL_ARRAY_BUFFER, model.textureIdVBO);
    glBufferData(
        GL_ARRAY_BUFFER, texture_ids.size() * sizeof(float), texture_ids.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glEnableVertexAttribArray(3);

    glGenBuffers(1, &model.textureOpaqueVBO);
    glBindBuffer(GL_ARRAY_BUFFER, model.textureOpaqueVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        texture_opaques.size() * sizeof(float),
        texture_opaques.data(),
        GL_STATIC_DRAW);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glEnableVertexAttribArray(4);

    glGenBuffers(1, &model.textureAnimSpeedVBO);
    glBindBuffer(GL_ARRAY_BUFFER, model.textureAnimSpeedVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        texture_anim_speeds.size() * sizeof(float),
        texture_anim_speeds.data(),
        GL_STATIC_DRAW);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glEnableVertexAttribArray(5);

    BIND_VERTEX_ARRAY(0);
    pix3dgl->models[model_idx] = std::move(model);
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
        int start = face * 3;
        // Merge adjacent faces into a single draw range to cut draw calls.
        if( !batch.face_starts.empty() &&
            batch.face_starts.back() + batch.face_counts.back() == start )
        {
            batch.face_counts.back() += 3;
        }
        else
        {
            batch.face_starts.push_back(start);
            batch.face_counts.push_back(3);
        }
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

    // The texture atlas is already bound to unit 0 for the whole frame (set in begin_frame).
    // The shader determines texturing per-vertex via aTextureId/vAtlasSlot—no CPU switch needed.

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
pix3dgl_model_draw_face_fast(
    struct Pix3DGL* pix3dgl,
    int face_idx)
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

    // The atlas is bound for the whole frame. Flush all per-face batches accumulated by
    // pix3dgl_model_draw_face_fast, drawing each range in insertion order.
    for( auto& [texture_id, batch] : pix3dgl->draw_batches )
    {
        for( size_t i = 0; i < batch.face_starts.size(); i++ )
        {
            glDrawArrays(GL_TRIANGLES, batch.face_starts[i], batch.face_counts[i]);
        }
    }

    pix3dgl->draw_batches.clear();

    BIND_VERTEX_ARRAY(0);

    pix3dgl->current_model = nullptr;
    pix3dgl->current_model_idx = -1;
}

extern "C" void
pix3dgl_tile_draw(
    struct Pix3DGL* pix3dgl,
    int tile_idx)
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

    GLModel& tile = it->second;

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

    // The texture atlas is bound once per frame in pix3dgl_begin_frame.
    // All textures live inside it; the per-vertex aTextureId/vAtlasSlot attributes
    // tell the fragment shader which tile to sample—no CPU-side texture switching needed.
    // Preserve insertion order (painter's order from the game engine) — do NOT sort by
    // texture_id, which would destroy back-to-front ordering.

    GLuint last_vao = 0;

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

        for( size_t face = 0; face < batch.face_starts.size(); face++ )
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
            glDeleteBuffers(1, &model.textureIdVBO);
            glDeleteBuffers(1, &model.textureOpaqueVBO);
            glDeleteBuffers(1, &model.textureAnimSpeedVBO);
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
#if 0
// Get current animation frame for a model (returns -1 if not animated)
extern "C" int
pix3dgl_scene_static_get_model_animation_frame(
    struct Pix3DGL* pix3dgl,
    int scene_model_idx)
{
    if( !pix3dgl || !pix3dgl->static_scene )
    {
        return -1;
    }

    StaticScene* scene = pix3dgl->static_scene;

    // Check if this model is animated
    auto anim_it = scene->animated_models.find(scene_model_idx);
    if( anim_it != scene->animated_models.end() )
    {
        return anim_it->second.current_frame_step;
    }

    // Not an animated model
    return -1;
}
#endif

extern "C" void
pix3dgl_set_animation_clock(
    struct Pix3DGL* pix3dgl,
    float clock_value)
{
    if( !pix3dgl )
        return;

    pix3dgl->animation_clock = clock_value;
}
