
#include "pix3dgl.h"

#include "graphics/dash.h"
#include "graphics/uv_pnm.h"
#include "pix3dglcore.u.cpp"

#include <algorithm>
#include <cmath>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#include <OpenGL/gl3.h>
#define GL_GLEXT_PROTOTYPES
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/glcorearb.h>
#endif

#include <unordered_map>

#include <vector>

// Desktop OpenGL 3.0 Core vertex shader - ULTRA FAST, no branching!
// Atlas UVs are pre-calculated on CPU and stored in aTexCoord
const char* g_vertex_shader_core = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in float aTextureId;
layout(location = 4) in float aTextureOpaque;
layout(location = 5) in float aTextureAnimSpeed;

uniform mat4 uViewMatrix;       // Precomputed on CPU
uniform mat4 uProjectionMatrix; // Precomputed on CPU
uniform mat4 uModelMatrix;      // Per-model transformation
uniform float uClock;           // Animation clock

out vec4 vColor;
out vec2 vTexCoord;
flat out float vTexBlend;       // 1.0 if textured, 0.0 if not
flat out float vAtlasSlot;      // Atlas slot ID for wrapping
flat out float vTextureOpaque;  // 1.0 if opaque, 0.0 if transparent
flat out float vTextureAnimSpeed; // Animation speed

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

// Desktop OpenGL 3.0 Core fragment shader
// Handles texture wrapping: clamp U, tile V
const char* g_fragment_shader_core = R"(
#version 330 core
in vec4 vColor;
in vec2 vTexCoord;
flat in float vTexBlend;
flat in float vAtlasSlot;
flat in float vTextureOpaque;
flat in float vTextureAnimSpeed;

uniform sampler2D uTextureAtlas;
uniform float uClock;

out vec4 FragColor;

void main() {
    vec2 finalTexCoord = vTexCoord;
    
    // Apply texture wrapping if this is a textured face
    if (vTexBlend > 0.5 && vAtlasSlot >= 0.0) {
        // Atlas configuration (must match CPU values)
        const float atlasSize = 2048.0;
        const float tileSize = 128.0;
        const float tilesPerRow = 16.0;
        const float tileSizeNorm = tileSize / atlasSize;  // Size of one tile in normalized coords
        
        // Calculate tile position in atlas
        float tileU = mod(vAtlasSlot, tilesPerRow);
        float tileV = floor(vAtlasSlot / tilesPerRow);
        vec2 tileOrigin = vec2(tileU, tileV) * tileSizeNorm;
        
        // Extract local UV within the tile (0 to 1 range within the tile)
        vec2 localUV = (vTexCoord - tileOrigin) / tileSizeNorm;
        
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
        localUV.x = clamp(localUV.x, 0.008, 0.992);
        localUV.y = clamp(fract(localUV.y), 0.008, 0.992);
        
        // Transform back to atlas space
        finalTexCoord = tileOrigin + localUV * tileSizeNorm;
    }
    
    // Sample texture with wrapped coordinates
    vec4 texColor = texture(uTextureAtlas, finalTexCoord);
    
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
            if (texColor.a < 0.5) {
                discard;
            }
            finalAlpha = vColor.a;
        } else {
            // Opaque texture: every pixel is solid.  texColor.a == 1.0 here by construction.
            finalAlpha = vColor.a;
        }
    }
    
    FragColor = vec4(finalColor, finalAlpha);
}
)";

static const char* g_ui_vertex_shader_gl3 = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
uniform mat4 uProjection;
out vec2 vUv;
void main()
{
    vUv = aUv;
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
)";

static const char* g_ui_fragment_shader_gl3 = R"(
#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
out vec4 FragColor;
void main()
{
    /* Ortho projection matches top-left screen space; sample UVs directly (no V flip). */
    vec4 c = texture(uTex, vUv);
    if( c.a < 0.01 )
        discard;
    FragColor = c;
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
    GLuint VAO;
    GLuint VBO;
    GLuint colorVBO;
    GLuint texCoordVBO;
    GLuint textureIdVBO;
    GLuint textureOpaqueVBO;
    GLuint textureAnimSpeedVBO;

    int face_count;
    bool has_textures;
    int first_texture_id;
    std::vector<int> face_textures;
    std::vector<bool> face_visible;
    std::vector<FaceShadingType> face_shading;
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

    // Reference to core buffers (owned by Pix3DGL)
    // All vertex data, colors, texCoords, etc. are stored here
    Pix3DGLCoreBuffers* core_buffers;

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

    // Texture atlas for batching all textures into one
    GLuint texture_atlas;
    int atlas_size;                                            // e.g., 2048
    int atlas_tile_size;                                       // e.g., 128
    int atlas_tiles_per_row;                                   // e.g., 16
    std::unordered_map<int, int> texture_to_atlas_slot;        // Maps texture_id -> atlas_slot
    std::unordered_map<int, bool> texture_to_opaque;           // Maps texture_id -> opaque flag
    std::unordered_map<int, float> texture_to_animation_speed; // Maps texture_id -> animation speed

    // Clock for texture animation
    float animation_clock;

    // Batching system - accumulate faces by texture and draw together
    std::unordered_map<int, DrawBatch> draw_batches; // Key: texture_id (-1 for untextured)

    // Scene-level batching - accumulate all draws and render at end of frame
    std::vector<SceneDrawBatch> scene_batches;

    // Reusable batch structure to avoid allocations
    std::unordered_map<int, DrawBatch> reusable_batches;

    // Static scene buffer for efficient rendering of static geometry
    StaticScene* static_scene;

    // Core buffers for the new unified loading system
    Pix3DGLCoreBuffers* core_buffers;

    // Runtime render state configuration.
    bool z_buffer_enabled;
    bool backface_cull_enabled;

    GLuint program_ui;
    GLint uniform_ui_tex;
    GLint uniform_ui_projection;
    GLuint ui_vao;
    GLuint ui_vbo;
    /* Per-sprite GPU texture cache.  Key = (uintptr_t)DashSprite*.
     * Replaces the old single ui_sprite_texture. */
    std::unordered_map<uintptr_t, GLuint> sprite_texture_cache;

    /* Pending 2D sprite draws accumulated during a frame.
     * Flushed in a single glBufferData call by pix3dgl_end_2dframe. */
    struct SpritePendingDraw
    {
        GLuint tex;
        float verts[6 * 4]; /* 6 vertices × (screen_x, screen_y, u, v) */
    };
    std::vector<SpritePendingDraw> pending_sprite_draws;
    int sprite_batch_fb_width;
    int sprite_batch_fb_height;
};

static void
create_ortho_matrix(
    float* out_matrix,
    float left,
    float right,
    float bottom,
    float top)
{
    /* Column-major order for OpenGL. Maps (left..right, bottom..top) to clip space.
     * For top-left origin, use top=0 and bottom=height. */
    out_matrix[0] = 2.0f / (right - left);
    out_matrix[1] = 0.0f;
    out_matrix[2] = 0.0f;
    out_matrix[3] = 0.0f;

    out_matrix[4] = 0.0f;
    out_matrix[5] = 2.0f / (top - bottom);
    out_matrix[6] = 0.0f;
    out_matrix[7] = 0.0f;

    out_matrix[8] = 0.0f;
    out_matrix[9] = 0.0f;
    out_matrix[10] = -1.0f;
    out_matrix[11] = 0.0f;

    out_matrix[12] = -(right + left) / (right - left);
    out_matrix[13] = -(top + bottom) / (top - bottom);
    out_matrix[14] = 0.0f;
    out_matrix[15] = 1.0f;
}

// Note: Matrix computation functions moved to pix3dglcore.u.cpp for code reuse

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

static bool
pix3dgl_init_ui_gl(struct Pix3DGL* pix3dgl)
{
    pix3dgl->program_ui = 0;
    pix3dgl->uniform_ui_tex = -1;
    pix3dgl->uniform_ui_projection = -1;
    pix3dgl->ui_vao = 0;
    pix3dgl->ui_vbo = 0;

    GLuint prog = create_shader_program(g_ui_vertex_shader_gl3, g_ui_fragment_shader_gl3);
    if( !prog )
    {
        printf("Pix3DGL: failed to create UI sprite shader program\n");
        return false;
    }
    pix3dgl->program_ui = prog;
    pix3dgl->uniform_ui_tex = glGetUniformLocation(prog, "uTex");
    pix3dgl->uniform_ui_projection = glGetUniformLocation(prog, "uProjection");

    glGenVertexArrays(1, &pix3dgl->ui_vao);
    glGenBuffers(1, &pix3dgl->ui_vbo);
    printf("Pix3DGL: UI sprite program %u (uTex loc %d)\n", prog, pix3dgl->uniform_ui_tex);
    return true;
}

extern "C" struct Pix3DGL*
pix3dgl_new(
    bool z_buffer_enabled,
    bool backface_cull_enabled)
{
    struct Pix3DGL* pix3dgl = new Pix3DGL();

    pix3dgl->program_ui = 0;
    pix3dgl->uniform_ui_tex = -1;
    pix3dgl->uniform_ui_projection = -1;
    pix3dgl->ui_vao = 0;
    pix3dgl->ui_vbo = 0;
    pix3dgl->sprite_batch_fb_width = 0;
    pix3dgl->sprite_batch_fb_height = 0;

    // Initialize current model tracking
    pix3dgl->current_model = nullptr;
    pix3dgl->current_model_idx = -1;

    // Initialize state tracking
    pix3dgl->currently_bound_texture = 0;
    pix3dgl->current_texture_state = -1; // -1 = unknown

    // Initialize static scene
    pix3dgl->static_scene = nullptr;

    // Initialize core buffers
    pix3dgl->core_buffers = new Pix3DGLCoreBuffers();
    pix3dgl->core_buffers->total_vertex_count = 0;
    pix3dgl->core_buffers->atlas_tiles_per_row = 0;
    pix3dgl->core_buffers->atlas_tile_size = 0;
    pix3dgl->core_buffers->atlas_size = 0;

    // Initialize animation clock
    pix3dgl->animation_clock = 0.0f;
    pix3dgl->z_buffer_enabled = z_buffer_enabled;
    pix3dgl->backface_cull_enabled = backface_cull_enabled;

    pix3dgl->program_es2 = create_shader_program(g_vertex_shader_core, g_fragment_shader_core);

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
    pix3dgl->uniform_texture_atlas = glGetUniformLocation(pix3dgl->program_es2, "uTextureAtlas");
    pix3dgl->uniform_atlas_size = glGetUniformLocation(pix3dgl->program_es2, "uAtlasSize");
    pix3dgl->uniform_tile_size = glGetUniformLocation(pix3dgl->program_es2, "uTileSize");
    pix3dgl->uniform_texture_matrix = glGetUniformLocation(pix3dgl->program_es2, "uTextureMatrix");
    pix3dgl->uniform_clock = glGetUniformLocation(pix3dgl->program_es2, "uClock");

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

    // Create empty atlas texture.
    // GL_RGBA8 (sized internal format) is required by Core Profile and for Metal-backed GL on
    // macOS — using the unsized GL_RGBA causes zero-alpha fallback on some drivers.
    std::vector<unsigned char> empty_atlas(pix3dgl->atlas_size * pix3dgl->atlas_size * 4, 0);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
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

    if( !pix3dgl_init_ui_gl(pix3dgl) )
    {
        printf("Pix3DGL: continuing without UI sprite program\n");
    }

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
        // animation)
        // 128 is the size of the texture.
        float speed = ((float)animation_speed) / 128.0f;

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
        "Loaded texture %d into atlas slot %d at (%d, %d) [opaque=%d, anim_dir=%d, "
        "anim_speed=%.3f]\n",
        texture_id,
        atlas_slot,
        x_offset,
        y_offset,
        opaque ? 1 : 0,
        animation_direction,
        anim_speed);

    // Clean up cache texture

    // Check for OpenGL errors
    GLenum error = glGetError();
    if( error != GL_NO_ERROR )
    {
        printf("OpenGL error in pix3dgl_load_texture: 0x%x\n", error);
    }
}

extern "C" void
pix3dgl_begin_3dframe(
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

    // Viewport is owned by the platform renderer (e.g. scaled world sub-rect on HiDPI).

    // Depth testing is optional and disabled by default to match painter's-order rendering.
    glEnable(GL_DEPTH_TEST);
    if( pix3dgl->z_buffer_enabled )
    {
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
    }
    else
    {
        glDepthFunc(GL_ALWAYS); // Always pass the depth test
        glDepthMask(GL_FALSE);  // Never write to the depth buffer
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

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

    // Set animation clock uniform
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
    pix3dgl_begin_3dframe(
        pix3dgl,
        camera_x,
        camera_y,
        camera_z,
        camera_pitch,
        camera_yaw,
        screen_width,
        screen_height);
}

extern "C" void
pix3dgl_begin_2dframe(struct Pix3DGL* pix3dgl)
{
    if( !pix3dgl || !pix3dgl->program_ui )
        return;
    pix3dgl->pending_sprite_draws.clear();
    pix3dgl->sprite_batch_fb_width = 0;
    pix3dgl->sprite_batch_fb_height = 0;
    glUseProgram(pix3dgl->program_ui);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);   // Ensure we aren't writing to depth
    glDisable(GL_CULL_FACE); // Sprites shouldn't be culled
}

extern "C" void
pix3dgl_end_2dframe(struct Pix3DGL* pix3dgl)
{
    if( !pix3dgl )
        return;

    const auto& draws = pix3dgl->pending_sprite_draws;
    if( !draws.empty() && pix3dgl->ui_vao && pix3dgl->ui_vbo )
    {
        const int n = (int)draws.size();

        /* Upload all sprite quads in one buffer upload. */
        std::vector<float> all_verts((size_t)n * 6 * 4);
        for( int i = 0; i < n; ++i )
            memcpy(&all_verts[(size_t)i * 6 * 4], draws[i].verts, 6 * 4 * sizeof(float));

        glUseProgram(pix3dgl->program_ui);
        glBindVertexArray(pix3dgl->ui_vao);
        glBindBuffer(GL_ARRAY_BUFFER, pix3dgl->ui_vbo);

        /* Orthographic projection in screen pixel space (top-left origin). */
        if( pix3dgl->uniform_ui_projection >= 0 && pix3dgl->sprite_batch_fb_width > 0 &&
            pix3dgl->sprite_batch_fb_height > 0 )
        {
            float ortho[16];
            const float left = 0.0f;
            const float right = (float)pix3dgl->sprite_batch_fb_width;
            /* Our authored sprite coordinates use a top-left origin (y grows downward).
             * OpenGL's ortho assumes y grows upward, so we invert by swapping bottom/top. */
            const float bottom = (float)pix3dgl->sprite_batch_fb_height;
            const float top = 0.0f;
            create_ortho_matrix(ortho, left, right, bottom, top);
            glUniformMatrix4fv(pix3dgl->uniform_ui_projection, 1, GL_FALSE, ortho);
        }

        glBufferData(
            GL_ARRAY_BUFFER,
            (GLsizeiptr)(all_verts.size() * sizeof(float)),
            all_verts.data(),
            GL_STREAM_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        if( pix3dgl->uniform_ui_tex >= 0 )
            glUniform1i(pix3dgl->uniform_ui_tex, 0);
        glActiveTexture(GL_TEXTURE0);

        /* Draw consecutive same-texture sprites as one glDrawArrays call. */
        int group_start = 0;
        while( group_start < n )
        {
            GLuint group_tex = draws[group_start].tex;
            int group_end = group_start + 1;
            while( group_end < n && draws[group_end].tex == group_tex )
                ++group_end;

            glBindTexture(GL_TEXTURE_2D, group_tex);
            glDrawArrays(GL_TRIANGLES, group_start * 6, (group_end - group_start) * 6);

            group_start = group_end;
        }
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

extern "C" void
pix3dgl_restore_gl_state_after_imgui(struct Pix3DGL* pix3dgl)
{
    if( !pix3dgl || !pix3dgl->program_es2 )
        return;

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);

    glUseProgram(pix3dgl->program_es2);

    glEnable(GL_DEPTH_TEST);
    if( pix3dgl->z_buffer_enabled )
    {
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
    }
    else
    {
        glDepthFunc(GL_ALWAYS);
        glDepthMask(GL_FALSE);
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, pix3dgl->texture_atlas);
    if( pix3dgl->uniform_texture_atlas >= 0 )
    {
        glUniform1i(pix3dgl->uniform_texture_atlas, 0);
    }
    pix3dgl->currently_bound_texture = pix3dgl->texture_atlas;
}

extern "C" void
pix3dgl_end_3dframe(struct Pix3DGL* pix3dgl)
{
    if( !pix3dgl || !pix3dgl->program_es2 )
        return;

    // OPTIMIZATION: Render all accumulated scene batches
    // This reduces state changes by sorting batches by texture

    if( pix3dgl->scene_batches.empty() )
        return;

    glUseProgram(pix3dgl->program_es2);

    // The texture atlas is bound once per frame in pix3dgl_begin_3dframe.
    // All textures live inside it; the per-vertex aTextureId/vAtlasSlot attributes
    // tell the fragment shader which tile to sample — no CPU-side texture switching needed.
    // Preserve insertion order (painter's order from the game engine) — do NOT sort by
    // texture_id, which would destroy back-to-front ordering.

    GLuint last_vao = 0;

    for( const auto& batch : pix3dgl->scene_batches )
    {
        if( batch.face_starts.empty() )
            continue;

        if( batch.vao != last_vao )
        {
            glBindVertexArray(batch.vao);
            last_vao = batch.vao;
        }

        if( pix3dgl->uniform_model_matrix >= 0 )
        {
            glUniformMatrix4fv(pix3dgl->uniform_model_matrix, 1, GL_FALSE, batch.model_matrix);
        }

        glMultiDrawArrays(
            GL_TRIANGLES,
            batch.face_starts.data(),
            batch.face_counts.data(),
            (GLsizei)batch.face_starts.size());
    }

    if( last_vao != 0 )
    {
        glBindVertexArray(0);
    }
}

extern "C" void
pix3dgl_end_frame(struct Pix3DGL* pix3dgl)
{
    pix3dgl_end_3dframe(pix3dgl);
}

extern "C" void
pix3dgl_set_animation_clock(
    struct Pix3DGL* pix3dgl,
    float clock_value)
{
    if( !pix3dgl )
        return;
    pix3dgl->animation_clock = clock_value;
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
    hsl16_t* face_colors_hsl_a,
    hsl16_t* face_colors_hsl_b,
    hsl16_t* face_colors_hsl_c,
    int* face_infos_nullable,
    int* face_alphas_nullable)
{
    if( !pix3dgl || face_count <= 0 || !vertices_x || !vertices_y || !vertices_z ||
        !face_indices_a || !face_indices_b || !face_indices_c || !face_colors_hsl_a ||
        !face_colors_hsl_b || !face_colors_hsl_c )
    {
        return;
    }

    if( pix3dgl->models.find(model_idx) != pix3dgl->models.end() )
    {
        return;
    }

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
        if( face_colors_hsl_c[face] == DASHHSL16_HIDDEN )
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
        if( hsl_c == DASHHSL16_FLAT )
        {
            rgb_a = rgb_b = rgb_c = g_hsl16_to_rgb_table[hsl_a];
            model.face_shading[(size_t)face] = SHADING_FLAT;
        }
        else if( hsl_c == DASHHSL16_HIDDEN )
        {
            rgb_a = rgb_b = rgb_c = 0;
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

        int texture_face_idx = face;
        int tp = 0;
        int tm = 0;
        int tn = 0;
        if( texture_id != -1 )
        {
            if( face_texture_coords_nullable && face_texture_coords_nullable[face] != -1 )
            {
                texture_face_idx = face_texture_coords_nullable[face];
                tp = textured_p_coordinate_nullable[texture_face_idx];
                tm = textured_m_coordinate_nullable[texture_face_idx];
                tn = textured_n_coordinate_nullable[texture_face_idx];
            }
            else
            {
                tp = face_indices_a[texture_face_idx];
                tm = face_indices_b[texture_face_idx];
                tn = face_indices_c[texture_face_idx];
            }

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
            float atlas_size_f = (float)pix3dgl->atlas_size;
            uvs.push_back((tile_u + uv.u1) * tile_size / atlas_size_f);
            uvs.push_back((tile_v + uv.v1) * tile_size / atlas_size_f);
            uvs.push_back((tile_u + uv.u2) * tile_size / atlas_size_f);
            uvs.push_back((tile_v + uv.v2) * tile_size / atlas_size_f);
            uvs.push_back((tile_u + uv.u3) * tile_size / atlas_size_f);
            uvs.push_back((tile_v + uv.v3) * tile_size / atlas_size_f);
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

    glGenVertexArrays(1, &model.VAO);
    glBindVertexArray(model.VAO);

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

    glBindVertexArray(0);
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
    pix3dgl_model_draw_ordered(
        pix3dgl, model_idx, position_x, position_y, position_z, yaw, NULL, 0);
}

extern "C" void
pix3dgl_model_draw_ordered(
    struct Pix3DGL* pix3dgl,
    int model_idx,
    float position_x,
    float position_y,
    float position_z,
    float yaw,
    const int* face_order,
    int face_order_count)
{
    if( !pix3dgl || !pix3dgl->program_es2 )
        return;

    auto it = pix3dgl->models.find(model_idx);
    if( it == pix3dgl->models.end() )
        return;

    GLModel& model = it->second;

    float cos_yaw = cos(yaw);
    float sin_yaw = sin(yaw);

    float modelMatrix[16] = { cos_yaw,    0.0f,       -sin_yaw,   0.0f, 0.0f,    1.0f,
                              0.0f,       0.0f,       sin_yaw,    0.0f, cos_yaw, 0.0f,
                              position_x, position_y, position_z, 1.0f };

    // Preserve strict face order so non-Z-buffer rendering matches software painter behavior.
    SceneDrawBatch scene_batch;
    scene_batch.model_idx = model_idx;
    scene_batch.vao = model.VAO;
    scene_batch.texture_id = -1;
    memcpy(scene_batch.model_matrix, modelMatrix, sizeof(modelMatrix));

    if( face_order && face_order_count > 0 )
    {
        for( int i = 0; i < face_order_count; i++ )
        {
            int face = face_order[i];
            if( face < 0 || face >= model.face_count || !model.face_visible[face] )
                continue;

            int start = face * 3;
            if( !scene_batch.face_starts.empty() &&
                scene_batch.face_starts.back() + scene_batch.face_counts.back() == start )
            {
                scene_batch.face_counts.back() += 3;
            }
            else
            {
                scene_batch.face_starts.push_back(start);
                scene_batch.face_counts.push_back(3);
            }
        }
    }
    else
    {
        for( int face = 0; face < model.face_count; face++ )
        {
            if( !model.face_visible[face] )
                continue;

            int start = face * 3;
            if( !scene_batch.face_starts.empty() &&
                scene_batch.face_starts.back() + scene_batch.face_counts.back() == start )
            {
                scene_batch.face_counts.back() += 3;
            }
            else
            {
                scene_batch.face_starts.push_back(start);
                scene_batch.face_counts.push_back(3);
            }
        }
    }

    if( !scene_batch.face_starts.empty() )
    {
        pix3dgl->scene_batches.push_back(std::move(scene_batch));
    }
}

extern "C" void
pix3dgl_sprite_load(
    struct Pix3DGL* pix3dgl,
    struct DashSprite* sprite)
{
    if( !pix3dgl )
        return;
    if( !sprite || !sprite->pixels_argb || sprite->width <= 0 || sprite->height <= 0 )
        return;

    uintptr_t key = (uintptr_t)sprite;
    if( pix3dgl->sprite_texture_cache.count(key) )
        return; /* Already uploaded — nothing to do. */

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if( !tex )
        return;

    /* Pixels are 0x00RRGGBB; 0 is the color-key. Synthesize alpha for the UI shader discard. */
    const int tw = sprite->width;
    const int th = sprite->height;
    std::vector<uint8_t> rgba_buf((size_t)tw * (size_t)th * 4u);
    for( int i = 0; i < tw * th; ++i )
    {
        uint32_t p = sprite->pixels_argb[i];
        rgba_buf[(size_t)i * 4u + 0u] = (uint8_t)((p >> 16) & 0xFFu);
        rgba_buf[(size_t)i * 4u + 1u] = (uint8_t)((p >> 8) & 0xFFu);
        rgba_buf[(size_t)i * 4u + 2u] = (uint8_t)(p & 0xFFu);
        rgba_buf[(size_t)i * 4u + 3u] = (p != 0u) ? 0xFFu : 0x00u;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_buf.data());

    pix3dgl->sprite_texture_cache[key] = tex;
}

extern "C" void
pix3dgl_sprite_unload(
    struct Pix3DGL* pix3dgl,
    struct DashSprite* sprite)
{
    if( !pix3dgl || !sprite )
        return;
    uintptr_t key = (uintptr_t)sprite;
    auto it = pix3dgl->sprite_texture_cache.find(key);
    if( it == pix3dgl->sprite_texture_cache.end() )
        return;
    glDeleteTextures(1, &it->second);
    pix3dgl->sprite_texture_cache.erase(it);
}

extern "C" void
pix3dgl_sprite_draw(
    struct Pix3DGL* pix3dgl,
    struct DashSprite* sprite,
    int dst_x,
    int dst_y,
    int framebuffer_width,
    int framebuffer_height,
    int rotation_r2pi2048,
    int src_x,
    int src_y,
    int src_w,
    int src_h)
{
    if( !pix3dgl || !pix3dgl->program_ui || !pix3dgl->ui_vao || !pix3dgl->ui_vbo )
        return;
    if( !sprite || !sprite->pixels_argb || framebuffer_width <= 0 || framebuffer_height <= 0 ||
        sprite->width <= 0 || sprite->height <= 0 )
        return;

    const int iw = src_w > 0 ? src_w : sprite->width;
    const int ih = src_h > 0 ? src_h : sprite->height;
    const int ix = src_x;
    const int iy = src_y;
    if( ix < 0 || iy < 0 || ix + iw > sprite->width || iy + ih > sprite->height )
        return;

    /* Lazy-upload fallback: if the sprite was never explicitly loaded (e.g. first frame
     * after a renderer restart), upload it now and cache it. */
    uintptr_t key = (uintptr_t)sprite;
    if( !pix3dgl->sprite_texture_cache.count(key) )
        pix3dgl_sprite_load(pix3dgl, sprite);

    auto it = pix3dgl->sprite_texture_cache.find(key);
    if( it == pix3dgl->sprite_texture_cache.end() )
        return; /* Upload failed. */
    GLuint tex = it->second;

    dst_x += sprite->crop_x;
    dst_y += sprite->crop_y;
    const float tw = (float)sprite->width;
    const float th = (float)sprite->height;
    const float w = (float)iw;
    const float h = (float)ih;
    const float x0 = (float)dst_x;
    const float y0 = (float)dst_y;
    const float x1 = x0 + w;
    const float y1 = y0 + h;
    const float cx = 0.5f * (x0 + x1);
    const float cy = 0.5f * (y0 + y1);
    const float angle = (float)(rotation_r2pi2048 * (2.0 * M_PI) / 2048.0);
    const float ca = cosf(angle);
    const float sa = sinf(angle);
    const float hw = 0.5f * w;
    const float hh = 0.5f * h;

    auto rot_local = [&](float lx, float ly, float* ox, float* oy) {
        *ox = cx + ca * lx - sa * ly;
        *oy = cy + sa * lx + ca * ly;
    };

    float px[4];
    float py[4];
    rot_local(-hw, -hh, &px[0], &py[0]);
    rot_local(hw, -hh, &px[1], &py[1]);
    rot_local(hw, hh, &px[2], &py[2]);
    rot_local(-hw, hh, &px[3], &py[3]);

    const float u0 = (float)ix / tw;
    const float v0 = (float)iy / th;
    const float u1 = (float)(ix + iw) / tw;
    const float v1 = (float)(iy + ih) / th;

    /* Capture framebuffer dimensions for projection (used in pix3dgl_end_2dframe). */
    if( pix3dgl->sprite_batch_fb_width <= 0 )
    {
        pix3dgl->sprite_batch_fb_width = framebuffer_width;
        pix3dgl->sprite_batch_fb_height = framebuffer_height;
    }

    /* Enqueue into the frame batch; pix3dgl_end_2dframe will issue the GL calls. */
    Pix3DGL::SpritePendingDraw draw;
    draw.tex = tex;
    float verts[6 * 4] = {
        px[0], py[0], u0, v0, px[1], py[1], u1, v0, px[2], py[2], u1, v1,
        px[0], py[0], u0, v0, px[2], py[2], u1, v1, px[3], py[3], u0, v1,
    };
    memcpy(draw.verts, verts, sizeof(verts));
    pix3dgl->pending_sprite_draws.push_back(draw);
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
            glDeleteVertexArrays(1, &model.VAO);
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
            glDeleteVertexArrays(1, &pix3dgl->static_scene->VAO);
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

        if( pix3dgl->ui_vao )
        {
            glDeleteVertexArrays(1, &pix3dgl->ui_vao);
        }
        if( pix3dgl->ui_vbo )
        {
            glDeleteBuffers(1, &pix3dgl->ui_vbo);
        }
        for( auto& pair : pix3dgl->sprite_texture_cache )
        {
            glDeleteTextures(1, &pair.second);
        }
        pix3dgl->sprite_texture_cache.clear();
        if( pix3dgl->program_ui )
        {
            glDeleteProgram(pix3dgl->program_ui);
        }

        // Clean up core buffers
        if( pix3dgl->core_buffers )
        {
            delete pix3dgl->core_buffers;
        }

        delete pix3dgl;
    }
}