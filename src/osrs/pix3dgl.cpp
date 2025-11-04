
#include "pix3dgl.h"

#include "graphics/uv_pnm.h"
#include "pix3dglcore.u.cpp"
#include "scene_cache.h"
#include "shared_tables.h"

#include <algorithm>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//

#if defined(__APPLE__)
// Use regular OpenGL headers on macOS/iOS for development
#include <OpenGL/gl.h>
#include <OpenGL/gl3.h>
#define GL_GLEXT_PROTOTYPES

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
        
        // Apply wrapping: wrap both coordinates for animation
        localUV.x = clamp(fract(localUV.x), 0.008, 0.992);  // Wrap U coordinate
        localUV.y = clamp(fract(localUV.y), 0.008, 0.992);  // Wrap V coordinate
        
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
        // This is a textured face
        if (vTextureOpaque < 0.5) {
            // Opaque texture: discard black pixels (for transparency mask), use face alpha only
            if (dot(texColor.rgb, vec3(0.299, 0.587, 0.114)) < 0.05) {
                discard;
            }
            finalAlpha = vColor.a;
        } else {
            // Transparent texture: use texture's alpha channel combined with face alpha
            finalAlpha = texColor.a * vColor.a;
        }
    }
    
    FragColor = vec4(finalColor, finalAlpha);
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
};

// Note: Matrix computation functions moved to pix3dglcore.u.cpp for code reuse

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

    // Initialize core buffers
    pix3dgl->core_buffers = new Pix3DGLCoreBuffers();
    pix3dgl->core_buffers->total_vertex_count = 0;
    pix3dgl->core_buffers->atlas_tiles_per_row = 0;
    pix3dgl->core_buffers->atlas_tile_size = 0;
    pix3dgl->core_buffers->atlas_size = 0;

    // Initialize animation clock
    pix3dgl->animation_clock = 0.0f;

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
        0.8  // Default gamma
    );

    if( !cache_tex )
    {
        printf("Failed to load texture %d from cache\n", texture_id);
        return;
    }

    // Store the opaque flag for this texture
    pix3dgl->texture_to_opaque[texture_id] = cache_tex->opaque;

    // Store the animation speed for this texture
    // Encode direction in the sign: positive for U direction, negative for V direction
    float anim_speed = 0.0f;
    if( cache_tex->animation_direction != TEXTURE_DIRECTION_NONE )
    {
        // Use actual animation speed from texture definition (increase multiplier for faster
        // animation)
        // 128 is the size of the texture.
        float speed = ((float)cache_tex->animation_speed) / (128.0f / 50.0f);

        // Encode direction: U directions use positive, V directions use negative
        if( cache_tex->animation_direction == TEXTURE_DIRECTION_U_DOWN ||
            cache_tex->animation_direction == TEXTURE_DIRECTION_U_UP )
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
        "Loaded texture %d into atlas slot %d at (%d, %d) [opaque=%d, anim_dir=%d, "
        "anim_speed=%.3f]\n",
        texture_id,
        atlas_slot,
        x_offset,
        y_offset,
        cache_tex->opaque,
        cache_tex->animation_direction,
        anim_speed);

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

    // Set animation clock uniform
    if( pix3dgl->uniform_clock >= 0 )
    {
        glUniform1f(pix3dgl->uniform_clock, pix3dgl->animation_clock);
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
            glBindVertexArray(batch.vao);
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

        // Render this batch with glMultiDrawArrays
        glMultiDrawArrays(
            GL_TRIANGLES,
            batch.face_starts.data(),
            batch.face_counts.data(),
            batch.face_starts.size());
    }

    // Unbind VAO
    if( last_vao != 0 )
    {
        glBindVertexArray(0);
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

    // Create new static scene
    pix3dgl->static_scene = new StaticScene();
    pix3dgl->static_scene->VAO = 0;
    pix3dgl->static_scene->VBO = 0;
    pix3dgl->static_scene->colorVBO = 0;
    pix3dgl->static_scene->texCoordVBO = 0;
    pix3dgl->static_scene->textureIdVBO = 0;
    pix3dgl->static_scene->dynamicEBO = 0;
    pix3dgl->static_scene->is_finalized = false;
    pix3dgl->static_scene->indices_dirty = false;
    pix3dgl->static_scene->in_batch_update = false;
    pix3dgl->static_scene->current_animated_model = nullptr;

    // Link to core buffers and reset them
    pix3dgl->static_scene->core_buffers = pix3dgl->core_buffers;
    pix3dgl->core_buffers->vertices.clear();
    pix3dgl->core_buffers->colors.clear();
    pix3dgl->core_buffers->texCoords.clear();
    pix3dgl->core_buffers->textureIds.clear();
    pix3dgl->core_buffers->textureOpaques.clear();
    pix3dgl->core_buffers->textureAnimSpeeds.clear();
    pix3dgl->core_buffers->texture_batches.clear();
    pix3dgl->core_buffers->model_ranges.clear();
    pix3dgl->core_buffers->total_vertex_count = 0;

    printf("Started building static scene\n");
}

// Wrapper function for tile loading
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
    if( !pix3dgl || !pix3dgl->static_scene || !pix3dgl->core_buffers )
    {
        printf("Static scene or core buffers not initialized\n");
        return;
    }

    if( pix3dgl->static_scene->is_finalized )
    {
        printf("Cannot add tiles to finalized static scene\n");
        return;
    }

    StaticScene* scene = pix3dgl->static_scene;
    Pix3DGLCoreBuffers* buffers = pix3dgl->core_buffers;
    int start_vertex_index = buffers->total_vertex_count;

    // Update atlas info in core buffers before calling core function
    buffers->texture_to_atlas_slot = pix3dgl->texture_to_atlas_slot;
    buffers->texture_to_opaque = pix3dgl->texture_to_opaque;
    buffers->texture_to_animation_speed = pix3dgl->texture_to_animation_speed;
    buffers->atlas_tiles_per_row = pix3dgl->atlas_tiles_per_row;
    buffers->atlas_tile_size = pix3dgl->atlas_tile_size;
    buffers->atlas_size = pix3dgl->atlas_size;

    // Storage for face ranges
    std::vector<Pix3DGLCoreFaceRange> core_face_ranges;

    // Call the core function
    pix3dgl_scene_static_load_tile_core(
        buffers,
        scene_tile_idx,
        vertex_x,
        vertex_y,
        vertex_z,
        vertex_count,
        faces_a,
        faces_b,
        faces_c,
        face_count,
        face_texture_ids,
        face_color_hsl_a,
        face_color_hsl_b,
        face_color_hsl_c,
        core_face_ranges);

    // Store tile range for later face ordering (convert from core to ModelRange)
    int vertex_count_total = buffers->total_vertex_count - start_vertex_index;
    if( vertex_count_total > 0 )
    {
        ModelRange range;
        range.scene_model_idx = scene_tile_idx;
        range.start_vertex = start_vertex_index;
        range.vertex_count = vertex_count_total;
        range.faces.reserve(core_face_ranges.size());
        for( const auto& core_face : core_face_ranges )
        {
            FaceRange face;
            face.start_vertex = core_face.start_vertex;
            face.face_index = core_face.face_index;
            range.faces.push_back(face);
        }
        scene->tile_ranges[scene_tile_idx] = range;

        printf(
            "Added tile %d to static scene: start=%d, count=%d, faces=%d (total %d vertices)\n",
            scene_tile_idx,
            start_vertex_index,
            vertex_count_total,
            face_count,
            buffers->total_vertex_count);
    }
}

// Wrapper function that adapts Pix3DGL to work with the core function
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
    if( !pix3dgl || !pix3dgl->static_scene || !pix3dgl->core_buffers )
    {
        printf("Static scene or core buffers not initialized\n");
        return;
    }

    if( pix3dgl->static_scene->is_finalized )
    {
        printf("Cannot add models to finalized static scene\n");
        return;
    }

    Pix3DGLCoreBuffers* buffers = pix3dgl->core_buffers;

    // Update atlas info from pix3dgl (only need to do this, not copy all vectors!)
    buffers->texture_to_atlas_slot = pix3dgl->texture_to_atlas_slot;
    buffers->texture_to_opaque = pix3dgl->texture_to_opaque;
    buffers->texture_to_animation_speed = pix3dgl->texture_to_animation_speed;
    buffers->atlas_tiles_per_row = pix3dgl->atlas_tiles_per_row;
    buffers->atlas_tile_size = pix3dgl->atlas_tile_size;
    buffers->atlas_size = pix3dgl->atlas_size;

    // Call the core function - it works directly on the buffers
    pix3dgl_scene_static_load_model_core(
        buffers,
        scene_model_idx,
        vertices_x,
        vertices_y,
        vertices_z,
        face_indices_a,
        face_indices_b,
        face_indices_c,
        face_count,
        face_textures_nullable,
        face_texture_coords_nullable,
        textured_p_coordinate_nullable,
        textured_m_coordinate_nullable,
        textured_n_coordinate_nullable,
        face_colors_hsl_a,
        face_colors_hsl_b,
        face_colors_hsl_c,
        face_infos_nullable,
        face_alphas_nullable,
        position_x,
        position_y,
        position_z,
        yaw);

    printf(
        "Added model %d to static scene: start=%d, count=%d, faces=%d (total %d vertices)\n",
        scene_model_idx,
        buffers->model_ranges[scene_model_idx].start_vertex,
        buffers->model_ranges[scene_model_idx].vertex_count,
        (int)buffers->model_ranges[scene_model_idx].faces.size(),
        buffers->total_vertex_count);
}

// Animation support functions
extern "C" void
pix3dgl_scene_static_load_animated_model_begin(
    struct Pix3DGL* pix3dgl, int scene_model_idx, int frame_count)
{
    if( !pix3dgl || !pix3dgl->static_scene )
    {
        printf("Static scene not initialized\n");
        return;
    }

    StaticScene* scene = pix3dgl->static_scene;

    if( scene->is_finalized )
    {
        printf("Cannot add animated models to finalized static scene\n");
        return;
    }

    // Create new animated model data
    StaticScene::AnimatedModelData anim_data;
    anim_data.scene_model_idx = scene_model_idx;
    anim_data.frame_count = frame_count;
    anim_data.current_frame_step = 0;
    anim_data.frame_tick_count = 0;
    anim_data.keyframe_ranges.reserve(frame_count);

    // Store in map and keep pointer for building
    scene->animated_models[scene_model_idx] = anim_data;
    scene->current_animated_model = &scene->animated_models[scene_model_idx];

    printf("Started loading animated model %d with %d keyframes\n", scene_model_idx, frame_count);
}

extern "C" void
pix3dgl_scene_static_load_animated_model_keyframe(
    struct Pix3DGL* pix3dgl,
    int scene_model_idx,
    int frame_idx,
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
    if( !pix3dgl || !pix3dgl->static_scene || !pix3dgl->static_scene->current_animated_model )
    {
        printf("Animated model not initialized\n");
        return;
    }

    StaticScene* scene = pix3dgl->static_scene;
    Pix3DGLCoreBuffers* buffers = pix3dgl->core_buffers;

    // Update atlas info from pix3dgl
    buffers->texture_to_atlas_slot = pix3dgl->texture_to_atlas_slot;
    buffers->texture_to_opaque = pix3dgl->texture_to_opaque;
    buffers->texture_to_animation_speed = pix3dgl->texture_to_animation_speed;
    buffers->atlas_tiles_per_row = pix3dgl->atlas_tiles_per_row;
    buffers->atlas_tile_size = pix3dgl->atlas_tile_size;
    buffers->atlas_size = pix3dgl->atlas_size;

    int start_vertex_index = buffers->total_vertex_count;

    // Call the core function to load this keyframe
    pix3dgl_scene_static_load_model_core(
        buffers,
        scene_model_idx,
        vertices_x,
        vertices_y,
        vertices_z,
        face_indices_a,
        face_indices_b,
        face_indices_c,
        face_count,
        face_textures_nullable,
        face_texture_coords_nullable,
        textured_p_coordinate_nullable,
        textured_m_coordinate_nullable,
        textured_n_coordinate_nullable,
        face_colors_hsl_a,
        face_colors_hsl_b,
        face_colors_hsl_c,
        face_infos_nullable,
        face_alphas_nullable,
        position_x,
        position_y,
        position_z,
        yaw);

    // Store this keyframe's range from the core buffers
    int vertex_count = buffers->total_vertex_count - start_vertex_index;
    if( vertex_count > 0 && buffers->model_ranges.count(scene_model_idx) > 0 )
    {
        // Convert core range to StaticScene ModelRange
        auto& core_range = buffers->model_ranges[scene_model_idx];
        ModelRange range;
        range.scene_model_idx = core_range.scene_model_idx;
        range.start_vertex = core_range.start_vertex;
        range.vertex_count = core_range.vertex_count;
        range.faces.reserve(core_range.faces.size());
        for( const auto& core_face : core_range.faces )
        {
            FaceRange face;
            face.start_vertex = core_face.start_vertex;
            face.face_index = core_face.face_index;
            range.faces.push_back(face);
        }

        scene->current_animated_model->keyframe_ranges.push_back(range);

        printf(
            "Added keyframe %d for animated model %d: start=%d, count=%d, faces=%d\n",
            frame_idx,
            scene_model_idx,
            start_vertex_index,
            vertex_count,
            face_count);
    }
}

extern "C" void
pix3dgl_scene_static_load_animated_model_end(
    struct Pix3DGL* pix3dgl, int scene_model_idx, int* frame_lengths, int frame_count)
{
    if( !pix3dgl || !pix3dgl->static_scene || !pix3dgl->static_scene->current_animated_model )
    {
        printf("Animated model not initialized\n");
        return;
    }

    StaticScene* scene = pix3dgl->static_scene;

    // Store frame lengths
    for( int i = 0; i < frame_count; i++ )
    {
        scene->current_animated_model->frame_lengths.push_back(frame_lengths[i]);
    }

    // IMPORTANT: Also add the first keyframe to model_ranges so the model can be found during
    // drawing This allows the model to participate in depth sorting with other models
    if( !scene->current_animated_model->keyframe_ranges.empty() )
    {
        // Use the first keyframe as the base range for depth sorting
        // Need to convert ModelRange to Pix3DGLCoreModelRange
        auto& first_range = scene->current_animated_model->keyframe_ranges[0];
        Pix3DGLCoreModelRange core_range;
        core_range.scene_model_idx = first_range.scene_model_idx;
        core_range.start_vertex = first_range.start_vertex;
        core_range.vertex_count = first_range.vertex_count;
        core_range.faces.reserve(first_range.faces.size());
        for( const auto& face : first_range.faces )
        {
            Pix3DGLCoreFaceRange core_face;
            core_face.start_vertex = face.start_vertex;
            core_face.face_index = face.face_index;
            core_range.faces.push_back(core_face);
        }
        scene->core_buffers->model_ranges[scene_model_idx] = core_range;
    }

    // Clear current animated model pointer
    scene->current_animated_model = nullptr;

    printf("Finished loading animated model %d with %d keyframes\n", scene_model_idx, frame_count);
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
    Pix3DGLCoreBuffers* buffers = scene->core_buffers;

    if( scene->is_finalized )
    {
        printf("Static scene already finalized\n");
        return;
    }

    // Create and upload the combined buffers
    glGenVertexArrays(1, &scene->VAO);
    glBindVertexArray(scene->VAO);

    // Upload vertices
    glGenBuffers(1, &scene->VBO);
    glBindBuffer(GL_ARRAY_BUFFER, scene->VBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        buffers->vertices.size() * sizeof(float),
        buffers->vertices.data(),
        GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Upload colors
    glGenBuffers(1, &scene->colorVBO);
    glBindBuffer(GL_ARRAY_BUFFER, scene->colorVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        buffers->colors.size() * sizeof(float),
        buffers->colors.data(),
        GL_STATIC_DRAW);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    // Upload texture coordinates
    glGenBuffers(1, &scene->texCoordVBO);
    glBindBuffer(GL_ARRAY_BUFFER, scene->texCoordVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        buffers->texCoords.size() * sizeof(float),
        buffers->texCoords.data(),
        GL_STATIC_DRAW);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(2);

    // Upload texture IDs (atlas slots)
    glGenBuffers(1, &scene->textureIdVBO);
    glBindBuffer(GL_ARRAY_BUFFER, scene->textureIdVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        buffers->textureIds.size() * sizeof(float),
        buffers->textureIds.data(),
        GL_STATIC_DRAW);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glEnableVertexAttribArray(3);

    // Texture Opaque VBO (attribute location 4)
    glGenBuffers(1, &scene->textureOpaqueVBO);
    glBindBuffer(GL_ARRAY_BUFFER, scene->textureOpaqueVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        buffers->textureOpaques.size() * sizeof(float),
        buffers->textureOpaques.data(),
        GL_STATIC_DRAW);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glEnableVertexAttribArray(4);

    // Texture Animation Speed VBO (attribute location 5)
    glGenBuffers(1, &scene->textureAnimSpeedVBO);
    glBindBuffer(GL_ARRAY_BUFFER, scene->textureAnimSpeedVBO);
    glBufferData(
        GL_ARRAY_BUFFER,
        buffers->textureAnimSpeeds.size() * sizeof(float),
        buffers->textureAnimSpeeds.data(),
        GL_STATIC_DRAW);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glEnableVertexAttribArray(5);

    // Create dynamic Element Buffer Object for painter's algorithm ordering
    // Pre-allocate with maximum possible size (total_vertex_count indices)
    glGenBuffers(1, &scene->dynamicEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, scene->dynamicEBO);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        buffers->total_vertex_count * sizeof(GLuint),
        nullptr,
        GL_DYNAMIC_DRAW); // Use DYNAMIC_DRAW for frequent updates

    glBindVertexArray(0);

    scene->is_finalized = true;

    // Pre-allocate sorted_indices vector for later use
    scene->sorted_indices.reserve(buffers->total_vertex_count);

    // Free CPU-side data to save memory (we only need GPU buffers now)
    buffers->vertices.clear();
    buffers->vertices.shrink_to_fit();
    buffers->colors.clear();
    buffers->colors.shrink_to_fit();
    buffers->texCoords.clear();
    buffers->texCoords.shrink_to_fit();
    buffers->textureIds.clear();
    buffers->textureIds.shrink_to_fit();

    printf("Finalized static scene with %d vertices\n", buffers->total_vertex_count);
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
        if( scene->core_buffers->model_ranges.find(scene_model_idx) !=
            scene->core_buffers->model_ranges.end() )
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
    auto it = scene->core_buffers->model_ranges.find(scene_model_idx);
    if( it == scene->core_buffers->model_ranges.end() )
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

    if( scene->core_buffers->total_vertex_count == 0 )
    {
        return;
    }

    // Update animation state for all animated models
    // Use the animation clock (set to 50fps game ticks) to advance animations
    static float last_animation_clock = 0.0f;
    float current_clock = pix3dgl->animation_clock;

    // Calculate ticks elapsed (animation clock is in seconds, each tick is 0.02 seconds at 50fps)
    int ticks_elapsed = (int)((current_clock - last_animation_clock) / 0.02f);

    if( ticks_elapsed > 0 )
    {
        last_animation_clock = current_clock;

        for( auto& anim_pair : scene->animated_models )
        {
            auto& anim_data = anim_pair.second;

            // Advance animation by elapsed ticks
            anim_data.frame_tick_count += ticks_elapsed;

            // Check if we need to advance to next frame(s)
            while( anim_data.frame_tick_count >=
                   anim_data.frame_lengths[anim_data.current_frame_step] )
            {
                anim_data.frame_tick_count -= anim_data.frame_lengths[anim_data.current_frame_step];
                anim_data.current_frame_step++;

                // Loop animation if we've reached the end
                if( anim_data.current_frame_step >= anim_data.frame_count )
                {
                    anim_data.current_frame_step = 0;
                }

                // Mark indices as dirty since we changed keyframe
                scene->indices_dirty = true;
            }
        }
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

    // Enable alpha blending for transparency support
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Bind the static scene VAO (this also binds the dynamicEBO)
    glBindVertexArray(scene->VAO);

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

                        // Check if this is an animated model
                        const ModelRange* range_ptr = nullptr;
                        ModelRange converted_range; // For converting core ranges
                        auto anim_it = scene->animated_models.find(scene_model_idx);
                        if( anim_it != scene->animated_models.end() )
                        {
                            // Use current keyframe for animated model
                            const auto& anim_data = anim_it->second;
                            if( anim_data.current_frame_step <
                                (int)anim_data.keyframe_ranges.size() )
                            {
                                range_ptr =
                                    &anim_data.keyframe_ranges[anim_data.current_frame_step];
                            }
                        }
                        else
                        {
                            // Use static model range (need to convert from core type)
                            auto model_it = scene->core_buffers->model_ranges.find(scene_model_idx);
                            if( model_it != scene->core_buffers->model_ranges.end() )
                            {
                                // Convert Pix3DGLCoreModelRange to ModelRange
                                const auto& core_range = model_it->second;
                                converted_range.scene_model_idx = core_range.scene_model_idx;
                                converted_range.start_vertex = core_range.start_vertex;
                                converted_range.vertex_count = core_range.vertex_count;
                                converted_range.faces.reserve(core_range.faces.size());
                                for( const auto& core_face : core_range.faces )
                                {
                                    FaceRange face;
                                    face.start_vertex = core_face.start_vertex;
                                    face.face_index = core_face.face_index;
                                    converted_range.faces.push_back(face);
                                }
                                range_ptr = &converted_range;
                            }
                        }

                        if( !range_ptr )
                            continue;

                        const ModelRange& range = *range_ptr;

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
                    // Check if this is an animated model
                    const ModelRange* range_ptr = nullptr;
                    ModelRange converted_range; // For converting core ranges
                    auto anim_it = scene->animated_models.find(scene_model_idx);
                    if( anim_it != scene->animated_models.end() )
                    {
                        // Use current keyframe for animated model
                        const auto& anim_data = anim_it->second;
                        if( anim_data.current_frame_step < (int)anim_data.keyframe_ranges.size() )
                        {
                            range_ptr = &anim_data.keyframe_ranges[anim_data.current_frame_step];
                        }
                    }
                    else
                    {
                        // Use static model range (need to convert from core type)
                        auto model_it = scene->core_buffers->model_ranges.find(scene_model_idx);
                        if( model_it != scene->core_buffers->model_ranges.end() )
                        {
                            // Convert Pix3DGLCoreModelRange to ModelRange
                            const auto& core_range = model_it->second;
                            converted_range.scene_model_idx = core_range.scene_model_idx;
                            converted_range.start_vertex = core_range.start_vertex;
                            converted_range.vertex_count = core_range.vertex_count;
                            converted_range.faces.reserve(core_range.faces.size());
                            for( const auto& core_face : core_range.faces )
                            {
                                FaceRange face;
                                face.start_vertex = core_face.start_vertex;
                                face.face_index = core_face.face_index;
                                converted_range.faces.push_back(face);
                            }
                            range_ptr = &converted_range;
                        }
                    }

                    if( !range_ptr )
                        continue;

                    const ModelRange& range = *range_ptr;

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
        }
    }
    else
    {
        // Fallback: draw entire scene in a single call (original behavior)
        glDrawArrays(GL_TRIANGLES, 0, scene->core_buffers->total_vertex_count);
    }

    glBindVertexArray(0);

    // Re-enable blending for other rendering
    glEnable(GL_BLEND);
}

extern "C" void
pix3dgl_set_animation_clock(struct Pix3DGL* pix3dgl, float clock_value)
{
    if( !pix3dgl )
        return;

    pix3dgl->animation_clock = clock_value;
}

extern "C" int
pix3dgl_scene_static_get_model_animation_frame(struct Pix3DGL* pix3dgl, int scene_model_idx)
{
    if( !pix3dgl || !pix3dgl->static_scene )
    {
        return -1;
    }

    StaticScene* scene = pix3dgl->static_scene;

    auto anim_it = scene->animated_models.find(scene_model_idx);
    if( anim_it != scene->animated_models.end() )
    {
        return anim_it->second.current_frame_step;
    }

    return -1; // Not an animated model
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

        // Clean up core buffers
        if( pix3dgl->core_buffers )
        {
            delete pix3dgl->core_buffers;
        }

        delete pix3dgl;
    }
}