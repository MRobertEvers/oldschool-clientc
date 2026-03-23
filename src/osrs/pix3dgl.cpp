
#include "pix3dgl.h"

#include "graphics/shared_tables.h"
#include "graphics/uv_pnm.h"
#include "pix3dglcore.u.cpp"

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
};

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

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Use our shader program
    glUseProgram(pix3dgl->program_es2);

    // Set viewport
    glViewport(0, 0, (GLsizei)screen_width, (GLsizei)screen_height);

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