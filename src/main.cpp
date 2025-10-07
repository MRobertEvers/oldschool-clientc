#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <emscripten/html5.h>

extern "C" {
#include "osrs/scene_cache.h"
}

// Define timer query extension types if not available
#ifndef GL_TIME_ELAPSED_EXT
#define GL_TIME_ELAPSED_EXT 0x88BF
#endif

#include <SDL.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <emscripten.h>
#include <set>
#include <vector>

extern "C" {
#include "datastruct/ht.h"
#include "graphics/uv_pnm.h"
#include "osrs/cache.h"
#include "osrs/scene_cache.h"
#include "osrs/tables/textures.h"
}

struct TextureGL
{
    GLuint id;
    int width;
    int height;
    bool opaque;
};

TextureGL load_texture(int texture_id, int size);

// Define TexItem struct to match the one in scene_cache.c
struct TexItem
{
    int id;
    int ref_count;
    struct Texture* texture;
    struct TexItem* next;
    struct TexItem* prev;
};

// Define TexturesCache struct to match the one in scene_cache.c
struct TexturesCache
{
    struct Cache* cache;
    struct HashTable table;
    struct TexItem root;
};

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Performance measurement variables
static double g_software_render_time = 0.0; // Time in milliseconds
static double g_gpu_render_time = 0.0;      // Time in milliseconds
static int g_frame_count = 0;
static double g_avg_software_time = 0.0;
static double g_avg_gpu_time = 0.0;

// Helper function to get current time in milliseconds
static double
get_time_ms()
{
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration<double, std::milli>(duration).count();
}

// Global SDL variables
SDL_Window* g_window = nullptr;
SDL_GLContext g_gl_context = nullptr;

extern "C" {
#include "graphics/render.h"
#include "osrs/cache.h"
#include "osrs/filelist.h"
#include "osrs/frustrum_cullmap.h"
#include "osrs/scene.h"
#include "osrs/scene_cache.h"
#include "osrs/scene_tile.h"
#include "osrs/tables/config_idk.h"
#include "osrs/tables/config_locs.h"
#include "osrs/tables/config_object.h"
#include "osrs/tables/config_sequence.h"
#include "osrs/tables/sprites.h"
#include "osrs/tables/textures.h"
#include "osrs/world.h"
#include "osrs/xtea_config.h"
#include "shared_tables.h"
// #include "screen.h"
}

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

static int g_pixel_buffer[SCREEN_WIDTH * SCREEN_HEIGHT] = { 0 };

struct SceneModel* g_scene_model = NULL;
struct IterRenderModel g_iter_model;
static int g_sort_order[1024] = { 0 }; // Stores face indices for sorting
static int g_sort_order_count = 0;
static unsigned int g_element_indices[1024 * 9] = { 0 }; // Stores vertex indices for OpenGL
static float g_vertices[10000] = { 0 };

// Vertex shader source
const char* vertexShaderSource = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec2 aTexCoord;  // UV coordinates

uniform float uRotationX;  // pitch
uniform float uRotationY;  // yaw
uniform float uScreenHeight;
uniform float uScreenWidth;
uniform vec3 uCameraPos;   // camera position
uniform mat4 uTextureMatrix;  // Optional texture coordinate transform

out vec3 vColor;
out vec2 vTexCoord;  // Pass UV coordinates to fragment shader

mat4 createProjectionMatrix(float fov, float screenWidth, float screenHeight) {
    float y = 1.0 / tan(fov * 0.5);
    float x = y;

    // Example: Screen 800x600
    // Multiply by 512 which is what the software renderer scales by.
    // Then we want a pixel at +400 to be normalized to 1.0 and -400 to be normalized to -1.0.
    // So we divide by half the screen width for x and half the screen height for y.
    return mat4(
        x * 512.0 / (screenWidth / 2.0), 0.0, 0.0, 0.0,
        0.0, -y * 512.0 / (screenHeight / 2.0), 0.0, 0.0, 
        0.0, 0.0, 1.0, 1.0,
        0.0, 0.0, -50.0, 0.0
    );
}

mat4 createViewMatrix(vec3 cameraPos, float pitch, float yaw) {
    float cosPitch = cos(-pitch);
    float sinPitch = sin(-pitch);
    float cosYaw = cos(-yaw);
    float sinYaw = sin(-yaw);

    mat4 pitchMatrix = mat4(
        1.0, 0.0, 0.0, 0.0,
        0.0, cosPitch, -sinPitch, 0.0,
        0.0, sinPitch, cosPitch, 0.0,
        0.0, 0.0, 0.0, 1.0
    );
    
    mat4 yawMatrix = mat4(
        cosYaw, 0.0, sinYaw, 0.0,
        0.0, 1.0, 0.0, 0.0,
        -sinYaw, 0.0, cosYaw, 0.0,
        0.0, 0.0, 0.0, 1.0
    );
    
    mat4 translateMatrix = mat4(
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        -cameraPos.x, -cameraPos.y, -cameraPos.z, 1.0
    );
    
    return pitchMatrix * yawMatrix * translateMatrix;
}

void main() {
    mat4 viewMatrix = createViewMatrix(uCameraPos, uRotationX, uRotationY);
    mat4 projection = createProjectionMatrix(radians(90.0), uScreenWidth, uScreenHeight);
    vec4 viewPos = viewMatrix * vec4(aPos, 1.0);
    
    vColor = aColor;
    
    // Transform texture coordinates if needed
    vec4 texCoord = vec4(aTexCoord, 0.0, 1.0);
    vec4 transformedTexCoord = uTextureMatrix * texCoord;
    vTexCoord = transformedTexCoord.xy / transformedTexCoord.w;
    
    gl_Position = projection * viewPos;
}
)";

// Fragment shader source
const char* fragmentShaderSource = R"(#version 300 es
precision mediump float;

in vec3 vColor;
in vec2 vTexCoord;
uniform sampler2D uTexture;
uniform bool uUseTexture;
out vec4 FragColor;

void main() {
    if (uUseTexture) {
        vec4 texColor = texture(uTexture, vTexCoord);
        FragColor = texColor * vec4(vColor, 1.0);
    } else {
        FragColor = vec4(vColor, 1.0);
    }
}
)";

// WebGL1 vertex shader
const char* vertexShaderSourceWebGL1 = R"(
attribute vec3 aPos;
attribute vec3 aColor;
attribute vec2 aTexCoord;  // UV coordinates

uniform float uRotationX;  // pitch
uniform float uRotationY;  // yaw
uniform vec3 uCameraPos;   // camera position
uniform float uScreenWidth;
uniform float uScreenHeight;
uniform mat4 uTextureMatrix;  // Optional texture coordinate transform

varying vec3 vColor;
varying vec2 vTexCoord;  // Pass UV coordinates to fragment shader

mat4 createProjectionMatrix(float fov, float screenWidth, float screenHeight) {
    float y = 1.0 / tan(fov * 0.5);
    float x = y;

    return mat4(
        x * 512.0 / (screenWidth / 2.0), 0.0, 0.0, 0.0,
        0.0, -y * 512.0 / (screenHeight / 2.0), 0.0, 0.0, 
        0.0, 0.0, 0.0, 1.0,
        0.0, 0.0, -1.0, 0.0
    );
}

mat4 createViewMatrix(vec3 cameraPos, float pitch, float yaw) {
    float cosPitch = cos(-pitch);
    float sinPitch = sin(-pitch);
    float cosYaw = cos(-yaw);
    float sinYaw = sin(-yaw);

    // Create rotation matrices
    mat4 pitchMatrix = mat4(
        1.0, 0.0, 0.0, 0.0,
        0.0, cosPitch, -sinPitch, 0.0,
        0.0, sinPitch, cosPitch, 0.0,
        0.0, 0.0, 0.0, 1.0
    );
    
    mat4 yawMatrix = mat4(
        cosYaw, 0.0, sinYaw, 0.0,
        0.0, 1.0, 0.0, 0.0,
        -sinYaw, 0.0, cosYaw, 0.0,
        0.0, 0.0, 0.0, 1.0
    );
    
    // Create translation matrix, move relative to camera position
    mat4 translateMatrix = mat4(
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        -cameraPos.x, -cameraPos.y, -cameraPos.z, 1.0
    );
    
    // Combine matrices: rotation * translation
    return pitchMatrix * yawMatrix * translateMatrix;
}

void main() {
    // Create view matrix with camera transformations
    mat4 viewMatrix = createViewMatrix(uCameraPos, uRotationX, uRotationY);
    
    // Create projection matrix with 90 degree FOV (same as Metal version)
    mat4 projection = createProjectionMatrix(radians(90.0), uScreenWidth, uScreenHeight);
    
    // Transform vertex position
    vec4 viewPos = viewMatrix * vec4(aPos, 1.0);
    
    // Pass through the color
    vColor = aColor;
    
    // Transform texture coordinates if needed
    vec4 texCoord = vec4(aTexCoord, 0.0, 1.0);
    vec4 transformedTexCoord = uTextureMatrix * texCoord;
    vTexCoord = transformedTexCoord.xy / transformedTexCoord.w;
    
    gl_Position = projection * viewPos;
}
)";

// WebGL1 fragment shader
const char* fragmentShaderSourceWebGL1 = R"(
precision mediump float;

varying vec3 vColor;
varying vec2 vTexCoord;
uniform sampler2D uTexture;
uniform bool uUseTexture;

void main() {
    if (uUseTexture) {
        vec4 texColor = texture2D(uTexture, vTexCoord);
        gl_FragColor = texColor * vec4(vColor, 1.0);
    } else {
        gl_FragColor = vec4(vColor, 1.0);
    }
}
)";

// Structure to hold triangle data for sorting
struct Triangle
{
    unsigned int indices[3]; // Original indices
    float depth;             // Average Z depth
};

// Global variables
GLuint shaderProgram;
GLuint VAO;
GLuint VBO, colorVBO, texCoordVBO, EBO; // Added texCoordVBO for texture coordinates

// Texture management

std::vector<TextureGL> g_textures; // Store loaded textures
GLuint g_texture_matrix_loc;       // Uniform location for texture matrix
GLuint g_use_texture_loc;          // Uniform location for texture toggle

// WebGL version tracking
static bool g_using_webgl2 = false;

// Timer query objects and state
GLuint g_timer_queries[2];            // Double-buffered timer queries
int g_current_query = 0;              // Index of current query
GLuint g_gpu_time_ns = 0;             // Last GPU time in nanoseconds
bool g_timer_query_supported = false; // Whether timer queries are supported

// Helper function to check for timer query extension support
static bool
check_timer_query_support()
{
    printf("Checking timer query support %d\n", g_using_webgl2);
    if( !g_using_webgl2 )
    {
        return false;
    }

    const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
    if( !extensions )
        return false;

    // Check for either EXT_disjoint_timer_query or EXT_timer_query
    return (strstr(extensions, "EXT_disjoint_timer_query") != NULL) ||
           (strstr(extensions, "EXT_timer_query") != NULL) ||
           (strstr(extensions, "EXT_disjoint_timer_query_webgl2") != NULL);
}
float rotationX = 0.1f; // Initial pitch (look down slightly)
float rotationY = 0.0f; // Initial yaw
// float distance = 1000.0f;  // Initial zoom level
float cameraX = 0.0f;
float cameraY = -120.0f;
float cameraZ = -1100.0f;
bool isDragging = false;
float lastX = 0.0f;
float lastY = 0.0f;
std::vector<unsigned int> sortedIndices;

// Keyboard state
bool wKeyPressed = false;
bool aKeyPressed = false;
bool sKeyPressed = false;
bool dKeyPressed = false;
bool rKeyPressed = false;
bool fKeyPressed = false;

static int
get_rotation_r2pi2048(float rotation)
{
    int rotation_r2pi2048 = (int)(rotation * 2048 / (2 * M_PI));
    while( rotation_r2pi2048 < 0 )
    {
        rotation_r2pi2048 += 2048;
    }
    rotation_r2pi2048 %= 2048;
    return rotation_r2pi2048;
}

static void
sort_model_faces()
{
    int rotation_pitch_r2pi2048 = get_rotation_r2pi2048(rotationX);
    int rotation_yaw_r2pi2048 = get_rotation_r2pi2048(rotationY);

    iter_render_model_init(
        &g_iter_model,
        g_scene_model,
        // TODO: For wall decor, this should probably be set on the model->yaw rather than
        // on the op.
        0,
        cameraX,
        cameraY,
        cameraZ,
        rotation_pitch_r2pi2048,
        rotation_yaw_r2pi2048,
        0,
        512,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        50);

    int index = 0;
    memset(g_pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));
    g_software_render_time = 0.0; // Reset software render time for this frame
    double start_time = get_time_ms();

    while( iter_render_model_next(&g_iter_model) )
    {
        int face = g_iter_model.value_face;
        if( face < 0 || face >= g_scene_model->model->face_count )
        {
            printf("Warning: Face %d is out of bounds\n", face);
            continue;
        }

        g_sort_order[index] = face;
        index++;

        // Time only the actual rasterization

        // model_draw_face(
        //     g_pixel_buffer,
        //     face,
        //     g_scene_model->model->face_infos,
        //     g_scene_model->model->face_indices_a,
        //     g_scene_model->model->face_indices_b,
        //     g_scene_model->model->face_indices_c,
        //     g_scene_model->model->face_count,
        //     g_iter_model.screen_vertices_x,
        //     g_iter_model.screen_vertices_y,
        //     g_iter_model.screen_vertices_z,
        //     g_iter_model.ortho_vertices_x,
        //     g_iter_model.ortho_vertices_y,
        //     g_iter_model.ortho_vertices_z,
        //     g_scene_model->model->vertex_count,
        //     g_scene_model->model->face_textures,
        //     g_scene_model->model->face_texture_coords,
        //     g_scene_model->model->textured_face_count,
        //     g_scene_model->model->textured_p_coordinate,
        //     g_scene_model->model->textured_m_coordinate,
        //     g_scene_model->model->textured_n_coordinate,
        //     g_scene_model->model->textured_face_count,
        //     g_scene_model->lighting->face_colors_hsl_a,
        //     g_scene_model->lighting->face_colors_hsl_b,
        //     g_scene_model->lighting->face_colors_hsl_c,
        //     g_scene_model->model->face_alphas,
        //     SCREEN_WIDTH / 2,
        //     SCREEN_HEIGHT / 2,
        //     50,
        //     SCREEN_WIDTH,
        //     SCREEN_HEIGHT,
        //     512,
        //     NULL);
    }

    g_software_render_time += get_time_ms() - start_time;
    g_sort_order_count = index;

    // Update average render time
    g_avg_software_time =
        (g_avg_software_time * g_frame_count + g_software_render_time) / (g_frame_count + 1);
}

// Function to update triangle depths and sort
void
updateTriangleOrder()
{
    // Calculate depths for all triangles
    sort_model_faces();

    // Convert face indices to vertex indices
    // for (int i = 0; i < g_scene_model->model->face_count; i++) {
    // int face_idx = i;
    for( int i = 0; i < g_sort_order_count; i++ )
    {
        int face_idx = g_sort_order[i];

        // For each face, we need to reference the correct vertices and their corresponding colors
        // Each face has 3 vertices, and each vertex has its own color in the face
        g_element_indices[i * 3] = face_idx * 3;         // First vertex of the face
        g_element_indices[i * 3 + 1] = face_idx * 3 + 1; // Second vertex of the face
        g_element_indices[i * 3 + 2] = face_idx * 3 + 2; // Third vertex of the face
    }

    // g_sort_order_count = g_scene_model->model->face_count;

    // For WebGL2, bind VAO before updating EBO
    if( g_using_webgl2 )
    {
        glBindVertexArray(VAO);
    }
    else
    {
        // WebGL1 fallback: manually bind vertex attributes
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, colorVBO);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
    }

    // Update GPU buffer with vertex indices
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        g_sort_order_count * 3 * sizeof(unsigned int),
        g_element_indices,
        GL_DYNAMIC_DRAW);

    // Check for errors after buffer update
    GLenum err;
    while( (err = glGetError()) != GL_NO_ERROR )
    {
        printf("OpenGL error in updateTriangleOrder: 0x%x\n", err);
    }
}

GLuint
createShader(GLenum type, const char* source)
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

void
initImGui()
{
    // Initialize SDL
    if( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0 )
    {
        printf("Error: %s\n", SDL_GetError());
        return;
    }

    // GL ES 3.0 + GLSL 300 es
    const char* glsl_version = "#version 300 es";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(0, &current);
    g_window = SDL_CreateWindow(
        "3D Raster",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if( !g_window )
    {
        printf("Failed to create window: %s\n", SDL_GetError());
        return;
    }
    g_gl_context = SDL_GL_CreateContext(g_window);
    if( !g_gl_context )
    {
        printf("Failed to create GL context: %s\n", SDL_GetError());
        return;
    }
    SDL_GL_MakeCurrent(g_window, g_gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.IniFilename = nullptr; // Don't save settings

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(g_window, g_gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    // Colors
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.94f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.25f, 0.25f, 0.25f, 0.54f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.31f, 0.31f, 0.31f, 0.40f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.39f, 0.39f, 0.39f, 0.67f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.44f, 0.44f, 0.44f, 0.40f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.46f, 0.47f, 0.48f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.42f, 0.42f, 0.42f, 1.00f);

    // Style
    style.WindowRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(5.0f, 3.0f);
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize = 16.0f;
    style.GrabMinSize = 8.0f;
}

void
initGL()
{
    // Create and compile shaders
    const char* vsSource = g_using_webgl2 ? vertexShaderSource : vertexShaderSourceWebGL1;
    const char* fsSource = g_using_webgl2 ? fragmentShaderSource : fragmentShaderSourceWebGL1;
    GLuint vertexShader = createShader(GL_VERTEX_SHADER, vsSource);
    GLuint fragmentShader = createShader(GL_FRAGMENT_SHADER, fsSource);

    // Create shader program
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    // Check for linking errors
    GLint success;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if( !success )
    {
        char infoLog[512];
        glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
        printf("Shader program linking error: %s\n", infoLog);
    }

    // Clean up shaders
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Get texture uniform locations
    g_texture_matrix_loc = glGetUniformLocation(shaderProgram, "uTextureMatrix");
    g_use_texture_loc = glGetUniformLocation(shaderProgram, "uUseTexture");

    // Load textures for the model
    g_textures.clear();
    if( g_scene_model->model->face_textures )
    {
        std::set<int> unique_textures;
        for( int i = 0; i < g_scene_model->model->face_count; i++ )
        {
            int texture_id = g_scene_model->model->face_textures[i];
            if( texture_id >= 0 )
            {
                unique_textures.insert(texture_id);
            }
        }

        for( int texture_id : unique_textures )
        {
            TextureGL tex = load_texture(texture_id, 128);
            if( tex.id != 0 )
            {
                g_textures.push_back(tex);
            }
        }
    }

    // Initialize triangles array
    sort_model_faces();

    // Create and bind VAO if available (WebGL2)
    if( g_using_webgl2 )
    {
        glGenVertexArrays(1, &VAO);
        glBindVertexArray(VAO);
    }

    // Create buffers
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &colorVBO);
    glGenBuffers(1, &texCoordVBO); // New buffer for texture coordinates
    glGenBuffers(1, &EBO);

    // Prepare vertex data - one set of vertices per face
    std::vector<float> vertices(
        g_scene_model->model->face_count * 9); // 3 vertices * 3 coordinates per face
    for( int i = 0; i < g_scene_model->model->face_count; i++ )
    {
        // Get vertex indices for this face
        unsigned int v1 = g_scene_model->model->face_indices_a[i];
        unsigned int v2 = g_scene_model->model->face_indices_b[i];
        unsigned int v3 = g_scene_model->model->face_indices_c[i];

        // First vertex
        vertices[i * 9] = (float)g_scene_model->model->vertices_x[v1];
        vertices[i * 9 + 1] = (float)g_scene_model->model->vertices_y[v1];
        vertices[i * 9 + 2] = (float)g_scene_model->model->vertices_z[v1];

        // Second vertex
        vertices[i * 9 + 3] = (float)g_scene_model->model->vertices_x[v2];
        vertices[i * 9 + 4] = (float)g_scene_model->model->vertices_y[v2];
        vertices[i * 9 + 5] = (float)g_scene_model->model->vertices_z[v2];

        // Third vertex
        vertices[i * 9 + 6] = (float)g_scene_model->model->vertices_x[v3];
        vertices[i * 9 + 7] = (float)g_scene_model->model->vertices_y[v3];
        vertices[i * 9 + 8] = (float)g_scene_model->model->vertices_z[v3];
    }

    printf("Faces: %d\n", g_scene_model->model->face_count);

    // Setup vertex buffer
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Create and setup color buffer - one color per vertex per face
    std::vector<float> colors(
        g_scene_model->model->face_count * 9); // 3 vertices * 3 colors per face
    for( int i = 0; i < g_scene_model->model->face_count; i++ )
    {
        int face_idx = i;

        // Get the HSL colors for each vertex of the face
        int hsl_a = g_scene_model->lighting->face_colors_hsl_a[face_idx];
        int hsl_b = g_scene_model->lighting->face_colors_hsl_b[face_idx];
        int hsl_c = g_scene_model->lighting->face_colors_hsl_c[face_idx];

        // Convert HSL to RGB
        int rgb_a = g_hsl16_to_rgb_table[hsl_a];
        int rgb_b = g_hsl16_to_rgb_table[hsl_b];
        int rgb_c = g_hsl16_to_rgb_table[hsl_c];

        // Store colors for this face's vertices
        // First vertex color
        colors[i * 9] = ((rgb_a >> 16) & 0xFF) / 255.0f;
        colors[i * 9 + 1] = ((rgb_a >> 8) & 0xFF) / 255.0f;
        colors[i * 9 + 2] = (rgb_a & 0xFF) / 255.0f;

        // Second vertex color
        colors[i * 9 + 3] = ((rgb_b >> 16) & 0xFF) / 255.0f;
        colors[i * 9 + 4] = ((rgb_b >> 8) & 0xFF) / 255.0f;
        colors[i * 9 + 5] = (rgb_b & 0xFF) / 255.0f;

        // Third vertex color
        colors[i * 9 + 6] = ((rgb_c >> 16) & 0xFF) / 255.0f;
        colors[i * 9 + 7] = ((rgb_c >> 8) & 0xFF) / 255.0f;
        colors[i * 9 + 8] = (rgb_c & 0xFF) / 255.0f;
    }

    glBindBuffer(GL_ARRAY_BUFFER, colorVBO);
    glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(float), colors.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    // Create and setup texture coordinate buffer - one UV pair per vertex per face
    std::vector<float> texCoords(
        g_scene_model->model->face_count * 6); // 3 vertices * 2 UV coordinates per face

    int* face_texture_coords = g_scene_model->model->face_texture_coords;
    int* face_indices_a = g_scene_model->model->face_indices_a;
    int* face_indices_b = g_scene_model->model->face_indices_b;
    int* face_indices_c = g_scene_model->model->face_indices_c;
    int* face_p_coordinate_nullable = g_scene_model->model->textured_p_coordinate;
    int* face_m_coordinate_nullable = g_scene_model->model->textured_m_coordinate;
    int* face_n_coordinate_nullable = g_scene_model->model->textured_n_coordinate;
    int texture_face = -1;
    int tp_vertex = -1;
    int tm_vertex = -1;
    int tn_vertex = -1;

    for( int i = 0; i < g_scene_model->model->face_count; i++ )
    {
        int face = i;
        // Get texture coordinates for this face
        float u1 = 0.0f, v1 = 0.0f;
        float u2 = 0.0f, v2 = 0.0f;
        float u3 = 0.0f, v3 = 0.0f;

// #define ADJUST_X(x) (x - (SCREEN_WIDTH / 2))
// #define ADJUST_Y(y) (y - (SCREEN_HEIGHT / 2))
#define ADJUST_X(x) (x)
#define ADJUST_Y(y) (y)

        int xa = ADJUST_X(g_scene_model->model->vertices_x[face_indices_a[face]]);
        int ya = ADJUST_Y(g_scene_model->model->vertices_y[face_indices_a[face]]);
        int za = g_scene_model->model->vertices_z[face_indices_a[face]];

        int xb = ADJUST_X(g_scene_model->model->vertices_x[face_indices_b[face]]);
        int yb = ADJUST_Y(g_scene_model->model->vertices_y[face_indices_b[face]]);
        int zb = g_scene_model->model->vertices_z[face_indices_b[face]];

        int xc = ADJUST_X(g_scene_model->model->vertices_x[face_indices_c[face]]);
        int yc = ADJUST_Y(g_scene_model->model->vertices_y[face_indices_c[face]]);
        int zc = g_scene_model->model->vertices_z[face_indices_c[face]];

        if( face_texture_coords && face_texture_coords[face] != -1 )
        {
            printf("face %d: has texture coords\n", i);
            texture_face = face_texture_coords[face];

            tp_vertex = face_p_coordinate_nullable[texture_face];
            tm_vertex = face_m_coordinate_nullable[texture_face];
            tn_vertex = face_n_coordinate_nullable[texture_face];
        }
        else
        {
            printf("face %d: no texture coords\n", i);
            texture_face = face;
            tp_vertex = face_indices_a[texture_face];
            tm_vertex = face_indices_b[texture_face];
            tn_vertex = face_indices_c[texture_face];
        }

        // Compute UV coordinates based on vertex positions
        if( g_scene_model->model->face_textures && g_scene_model->model->face_texture_coords )
        {
            int texture_idx = g_scene_model->model->face_textures[i];
            if( texture_idx >= 0 )
            {
                // Get vertex positions for UV coordinate computation
                float p_x = g_scene_model->model->vertices_x[tp_vertex];
                float p_y = g_scene_model->model->vertices_y[tp_vertex];
                float p_z = g_scene_model->model->vertices_z[tp_vertex];

                float m_x = g_scene_model->model->vertices_x[tm_vertex];
                float m_y = g_scene_model->model->vertices_y[tm_vertex];
                float m_z = g_scene_model->model->vertices_z[tm_vertex];

                float n_x = g_scene_model->model->vertices_x[tn_vertex];
                float n_y = g_scene_model->model->vertices_y[tn_vertex];
                float n_z = g_scene_model->model->vertices_z[tn_vertex];

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
                    xa,
                    ya,
                    za,
                    xb,
                    yb,
                    zb,
                    xc,
                    yc,
                    zc);

                u1 = uv_pnm.u1;
                v1 = uv_pnm.v1;

                u2 = uv_pnm.u2;
                v2 = uv_pnm.v2;

                u3 = uv_pnm.u3;
                v3 = uv_pnm.v3;

                printf("u1: %f, v1: %f, u2: %f, v2: %f, u3: %f, v3: %f\n", u1, v1, u2, v2, u3, v3);

                // // Compute vectors for texture space
                // float vU_x = m_x - p_x; // M vector (U direction)
                // float vU_y = m_y - p_y;
                // float vU_z = m_z - p_z;

                // float vV_x = n_x - p_x; // N vector (V direction)
                // float vV_y = n_y - p_y;
                // float vV_z = n_z - p_z;

                // int vUVPlane_normal_xhat = vU_y * vV_z - vU_z * vV_y;
                // int vUVPlane_normal_yhat = vU_z * vV_x - vU_x * vV_z;
                // int vUVPlane_normal_zhat = vU_x * vV_y - vU_y * vV_x;

                // // Compute the positions of A, B, C relative to P.
                // int dxa = xa - p_x;
                // int dya = ya - p_y;
                // int dza = za - p_z;
                // int dxb = xb - p_x;
                // int dyb = yb - p_y;
                // int dzb = zb - p_z;
                // int dxc = xc - p_x;
                // int dyc = yc - p_y;
                // int dzc = zc - p_z;

                // // The derivation is the same, this is the coefficient on U given by cramer's
                // rule float U_xhat = vV_y * vUVPlane_normal_zhat - vV_z * vUVPlane_normal_yhat;
                // float U_yhat = vV_z * vUVPlane_normal_xhat - vV_x * vUVPlane_normal_zhat;
                // float U_zhat = vV_x * vUVPlane_normal_yhat - vV_y * vUVPlane_normal_xhat;
                // float U_inv_w = 1.0 / (U_xhat * vU_x + U_yhat * vU_y + U_zhat * vU_z);

                // // dot product of A and U
                // u1 = (U_xhat * dxa + U_yhat * dya + U_zhat * dza) * U_inv_w;
                // u2 = (U_xhat * dxb + U_yhat * dyb + U_zhat * dzb) * U_inv_w;
                // u3 = (U_xhat * dxc + U_yhat * dyc + U_zhat * dzc) * U_inv_w;

                // // The derivation is the same, this is the coefficient on V given by cramer's
                // rule float V_xhat = vU_y * vUVPlane_normal_zhat - vU_z * vUVPlane_normal_yhat;
                // float V_yhat = vU_z * vUVPlane_normal_xhat - vU_x * vUVPlane_normal_zhat;
                // float V_zhat = vU_x * vUVPlane_normal_yhat - vU_y * vUVPlane_normal_xhat;
                // float V_inv_w = 1.0 / (V_xhat * vV_x + V_yhat * vV_y + V_zhat * vV_z);

                // v1 = (V_xhat * dxa + V_yhat * dya + V_zhat * dza) * V_inv_w;
                // v2 = (V_xhat * dxb + V_yhat * dyb + V_zhat * dzb) * V_inv_w;
                // v3 = (V_xhat * dxc + V_yhat * dyc + V_zhat * dzc) * V_inv_w;
            }
        }

        // Store texture coordinates for this face's vertices
        texCoords[i * 6] = u1;     // First vertex U (tp - origin)
        texCoords[i * 6 + 1] = v1; // First vertex V
        texCoords[i * 6 + 2] = u2; // Second vertex U (tm - U endpoint)
        texCoords[i * 6 + 3] = v2; // Second vertex V
        texCoords[i * 6 + 4] = u3; // Third vertex U (tn - V endpoint)
        texCoords[i * 6 + 5] = v3; // Third vertex V
    }

    glBindBuffer(GL_ARRAY_BUFFER, texCoordVBO);
    glBufferData(
        GL_ARRAY_BUFFER, texCoords.size() * sizeof(float), texCoords.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(2);

    // Bind EBO and initialize indices
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

    // Initialize sorted indices after buffer setup
    updateTriangleOrder();

    // If using WebGL1, set up attributes here since we don't have VAOs
    if( !g_using_webgl2 )
    {
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, colorVBO);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
    }

    // Enable depth testing only for now
    glEnable(GL_DEPTH_TEST);
    // Disable face culling for debugging
    // glEnable(GL_CULL_FACE);
    // glFrontFace(GL_CW);
    // glCullFace(GL_FRONT);

    // Check and initialize timer queries if supported
    g_timer_query_supported = check_timer_query_support();
    if( g_timer_query_supported )
    {
        glGenQueries(2, g_timer_queries);
    }
}

// Camera movement speed
const float MOVE_SPEED = 50.0f; // Increased for larger scale
const float ROTATE_SPEED = 0.01f;

void
updateCameraPosition(bool forward, bool backward, bool left, bool right, bool up, bool down)
{
    float rotation_y = -rotationY;
    if( forward )
    {
        cameraX += sin(rotation_y) * MOVE_SPEED;
        cameraZ += cos(rotation_y) * MOVE_SPEED;
    }
    if( backward )
    {
        cameraX -= sin(rotation_y) * MOVE_SPEED;
        cameraZ -= cos(rotation_y) * MOVE_SPEED;
    }
    if( left )
    {
        cameraX -= cos(rotation_y) * MOVE_SPEED;
        cameraZ += sin(rotation_y) * MOVE_SPEED;
    }
    if( right )
    {
        cameraX += cos(rotation_y) * MOVE_SPEED;
        cameraZ -= sin(rotation_y) * MOVE_SPEED;
    }
    if( up )
    {
        cameraY -= MOVE_SPEED;
    }
    if( down )
    {
        cameraY += MOVE_SPEED;
    }
}

void
drawPixelBufferToCanvas()
{
    static bool canvas_initialized = false;

    if( !canvas_initialized )
    {
        // Initialize the canvas only once
        EM_ASM_(
            {
                // Create container for the canvases and status
                let container = document.createElement('div');
                container.style.display = 'flex';
                container.style.flexDirection = 'column';
                container.style.justifyContent = 'center';
                container.style.alignItems = 'center';
                container.style.backgroundColor = '#1a1a1a';
                container.style.position = 'fixed';
                container.style.top = '0';
                container.style.left = '0';
                container.style.width = '100%';
                container.style.height = '100%';
                document.body.style.margin = '0';
                document.body.style.overflow = 'hidden';

                // Style the main WebGL canvas
                let mainCanvas = document.getElementById('canvas');
                mainCanvas.style.border = '1px solid white';
                mainCanvas.style.flex = '1';
                mainCanvas.style.maxWidth = 'calc(50% - 10px)';
                mainCanvas.style.height = 'auto';
                // mainCanvas.style.aspectRatio = $0 + '/' + $1;
                mainCanvas.style.position = 'relative';
                mainCanvas.style.zIndex = '1';

                // Create and style the software rasterizer canvas
                let pixelCanvas = document.getElementById('pixel-canvas');
                if( !pixelCanvas )
                {
                    pixelCanvas = document.createElement('canvas');
                    pixelCanvas.id = 'pixel-canvas';
                    pixelCanvas.width = $0;
                    pixelCanvas.height = $1;
                    pixelCanvas.style.border = '1px solid white';
                    pixelCanvas.style.flex = '1';
                    pixelCanvas.style.maxWidth = 'calc(50% - 10px)';
                    pixelCanvas.style.height = 'auto';
                    // pixelCanvas.style.aspectRatio = $0 + '/' + $1;
                }

                // Move elements to container
                document.body.appendChild(container);
                container.appendChild(mainCanvas);
                container.appendChild(pixelCanvas);
            },
            SCREEN_WIDTH,
            SCREEN_HEIGHT);
        canvas_initialized = true;
    }

    // Get the canvas and update it
    int* pixels = g_pixel_buffer;
    int width = SCREEN_WIDTH;
    int height = SCREEN_HEIGHT;

    EM_ASM_(
        {
            const pixelCanvas = document.getElementById('pixel-canvas');
            if( !pixelCanvas )
                return;

            const ctx = pixelCanvas.getContext('2d');
            const imageData = ctx.createImageData($0, $1);
            const size = $0 * $1;
            const pixels = new Int32Array(Module.HEAPU32.buffer, $2, size);

            for( let i = 0; i < size; i++ )
            {
                const pixel = pixels[i];

                if( pixel == 0 )
                    continue;

                const r = (pixel >> 16) & 0xFF;
                const g = (pixel >> 8) & 0xFF;
                const b = pixel & 0xFF;

                const idx = i * 4;
                imageData.data[idx] = r;
                imageData.data[idx + 1] = g;
                imageData.data[idx + 2] = b;
                imageData.data[idx + 3] = 255;
            }

            ctx.putImageData(imageData, 0, 0);
        },
        width,
        height,
        pixels);
}

void
render()
{
    g_frame_count++;
    if( g_frame_count % 60 == 0 )
    { // Print every 60 frames
      // printf(
      //     "Render frame %d, camera: %f, %f, %f (pitch: %f, yaw: %f)\n",
      //     g_frame_count,
      //     cameraX,
      //     cameraY,
      //     cameraZ,
      //     rotationX,
      //     rotationY);
    }

    // Reset software render time for this frame
    g_software_render_time = 0.0;

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Create ImGui window
    ImGui::Begin("Controls");

    // Camera controls
    ImGui::Text("Camera Position");
    ImGui::SliderFloat("X##Pos", &cameraX, -1000.0f, 1000.0f);
    ImGui::SliderFloat("Y##Pos", &cameraY, -1000.0f, 1000.0f);
    ImGui::SliderFloat("Z##Pos", &cameraZ, -1000.0f, 1000.0f);

    ImGui::Text("Camera Rotation");
    ImGui::SliderFloat("Pitch", &rotationX, -M_PI / 2, M_PI / 2);
    ImGui::SliderFloat("Yaw", &rotationY, -M_PI, M_PI);

    ImGui::Separator();

    ImGui::Text("Performance Metrics (Rasterization Only)");
    ImGui::Text("WebGL Version: %s", g_using_webgl2 ? "WebGL2" : "WebGL1");
    ImGui::Text("Software Rasterizer (CPU, model_draw_face):");
    ImGui::Text("  Current: %.2f ms", g_software_render_time);
    ImGui::Text("  Average: %.2f ms", g_avg_software_time);
    ImGui::Text(
        "GPU Rasterizer (%s, glDrawElements):",
        g_timer_query_supported ? "GL Timer Query" : "CPU Timer");
    ImGui::Text("  Current: %.2f ms", g_gpu_render_time);
    ImGui::Text("  Average: %.2f ms", g_avg_gpu_time);
    ImGui::Text("Frame Count: %d", g_frame_count);

    if( g_frame_count <= 1 && g_timer_query_supported )
    {
        ImGui::TextColored(
            ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Note: GPU timing data will appear after first frame");
    }
    else if( !g_timer_query_supported )
    {
        ImGui::TextColored(
            ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
            "Note: Using CPU timing fallback (GL_TIME_ELAPSED_EXT not supported)");
    }

    ImGui::End();

    // Update camera position based on keyboard state
    updateCameraPosition(
        wKeyPressed, sKeyPressed, aKeyPressed, dKeyPressed, rKeyPressed, fKeyPressed);

    // Clear any existing errors
    while( glGetError() != GL_NO_ERROR )
        ;

    double frame_start_time = 0.0;

    // Check result from previous frame's timer query
    if( g_timer_query_supported && g_frame_count > 1 )
    { // Skip first frame
        GLuint timer_ready = 0;
        glGetQueryObjectuiv(
            g_timer_queries[1 - g_current_query], GL_QUERY_RESULT_AVAILABLE, &timer_ready);
        if( timer_ready )
        {
            GLuint time_ns = 0;
            glGetQueryObjectuiv(g_timer_queries[1 - g_current_query], GL_QUERY_RESULT, &time_ns);
            g_gpu_render_time = time_ns / 1000000.0; // Convert to milliseconds
            g_avg_gpu_time =
                (g_avg_gpu_time * (g_frame_count - 1) + g_gpu_render_time) / g_frame_count;
        }
    }

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Ensure proper state
    glUseProgram(shaderProgram);

    // Get and validate uniform locations
    GLint rotationXLoc = glGetUniformLocation(shaderProgram, "uRotationX");
    GLint rotationYLoc = glGetUniformLocation(shaderProgram, "uRotationY");
    GLint screenHeightLoc = glGetUniformLocation(shaderProgram, "uScreenHeight");
    GLint screenWidthLoc = glGetUniformLocation(shaderProgram, "uScreenWidth");
    GLint cameraPosLoc = glGetUniformLocation(shaderProgram, "uCameraPos");

    if( rotationXLoc == -1 || rotationYLoc == -1 || cameraPosLoc == -1 || screenHeightLoc == -1 ||
        screenWidthLoc == -1 )
    {
        printf("Failed to get uniform locations\n");
        return;
    }

    // Update uniforms
    glUniform1f(rotationXLoc, rotationX);
    glUniform1f(rotationYLoc, rotationY);
    glUniform1f(screenHeightLoc, (float)SCREEN_HEIGHT);
    glUniform1f(screenWidthLoc, (float)SCREEN_WIDTH);
    glUniform3f(cameraPosLoc, cameraX, cameraY, cameraZ);

    // Sort triangles based on current transformation
    updateTriangleOrder();

    // Verify we have data to draw
    if( g_sort_order_count > 0 )
    {
        // Start timing just before the draw call
        if( g_timer_query_supported )
        {
            glBeginQuery(GL_TIME_ELAPSED_EXT, g_timer_queries[g_current_query]);
        }
        else
        {
            frame_start_time = get_time_ms();
        }

        // Set identity matrix for texture coordinates since they're already correctly computed
        float texture_matrix[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                     0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
        glUniformMatrix4fv(g_texture_matrix_loc, 1, GL_FALSE, texture_matrix);

        // Draw faces with textures
        if( !g_textures.empty() && g_scene_model->model->face_textures )
        {
            glUniform1i(g_use_texture_loc, 1); // Enable texturing

            for( int i = 0; i < g_sort_order_count; i++ )
            {
                int face_idx = g_sort_order[i];
                int texture_id = g_scene_model->model->face_textures[face_idx];

                if( texture_id >= 0 )
                {
                    // Find and bind the texture
                    for( const auto& tex : g_textures )
                    {
                        if( tex.id != 0 )
                        {
                            glActiveTexture(GL_TEXTURE0);
                            glBindTexture(GL_TEXTURE_2D, tex.id);
                            break;
                        }
                    }

                    // Draw the textured face
                    glDrawElements(
                        GL_TRIANGLES, 3, GL_UNSIGNED_INT, (void*)(i * 3 * sizeof(unsigned int)));
                }
            }

            // Reset texture state
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // Draw untextured faces
        glUniform1i(g_use_texture_loc, 0); // Disable texturing
        glDrawElements(GL_TRIANGLES, g_sort_order_count * 3, GL_UNSIGNED_INT, 0);

        // End timing right after the draw call
        if( g_timer_query_supported )
        {
            glEndQuery(GL_TIME_ELAPSED_EXT);
            g_current_query = 1 - g_current_query; // Swap query buffers
        }
        else
        {
            // Hacky, but force a flush and sync to get the actual time
            glFinish();
            g_gpu_render_time = get_time_ms() - frame_start_time;
            g_avg_gpu_time =
                (g_avg_gpu_time * (g_frame_count - 1) + g_gpu_render_time) / g_frame_count;
        }

        // Check for errors after draw
        GLenum err = glGetError();
        if( err != GL_NO_ERROR )
        {
            printf("OpenGL error during draw: 0x%x\n", err);
        }
    }
    else
    {
        printf("No triangles to draw\n");
    }

    // Check for OpenGL errors after draw
    GLenum err;
    while( (err = glGetError()) != GL_NO_ERROR )
    {
        printf("OpenGL error after draw: 0x%x\n", err);
    }

    // Draw the software rasterized buffer to a separate canvas
    // drawPixelBufferToCanvas();

    // Render ImGui
    ImGui::Render();
    // ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

static const int TZTOK_JAD_MODEL_ID = 9319;
static const int TZTOK_JAD_NPCTYPE_ID = 3127;

static const int BRICK_WALL_MODEL_ID = 638;

struct TexturesCache* g_textures_cache = NULL; // Defined in scene_cache.c

// Function to load a texture from cache and create OpenGL texture
TextureGL
load_texture(int texture_id, int size)
{
    TextureGL tex = { 0 };

    // Load texture from cache
    struct Texture* cache_tex = textures_cache_checkout(
        g_textures_cache,
        g_textures_cache->cache,
        texture_id,
        size,
        1.0 // Default gamma
    );

    if( !cache_tex )
    {
        printf("Failed to load texture %d from cache\n", texture_id);
        return tex;
    }

    // Create OpenGL texture
    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // Upload texture data
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, cache_tex->texels);

    // Store texture info
    tex.width = cache_tex->width;
    tex.height = cache_tex->height;
    tex.opaque = cache_tex->opaque;

    // Clean up cache texture
    textures_cache_checkin(g_textures_cache, cache_tex);

    return tex;
}

static int
load_model()
{
    printf("Loading model...\n");
    EM_ASM({
        //
        if( window.updateLoadingStatus )
        {
            updateLoadingStatus('Loading model...');
        }
    });

    struct Cache* cache = cache_new_from_directory("../cache");
    if( !cache )
    {
        printf("Failed to load cache from directory: ../cache\n");
        EM_ASM({
            //
            if( window.updateLoadingStatus )
            {
                updateLoadingStatus('Failed to load cache');
            }
        });
        return 0;
    }
    printf("Loaded cache\n");

    g_textures_cache = textures_cache_new(cache);

    // struct CacheModel* cache_model = model_new_from_cache(cache, TZTOK_JAD_MODEL_ID);
    struct CacheModel* cache_model = model_new_from_cache(cache, BRICK_WALL_MODEL_ID);
    if( !cache_model )
    {
        printf("Failed to load model\n");
        EM_ASM({
            //
            if( window.updateLoadingStatus )
            {
                updateLoadingStatus('Failed to load model');
            }
        });
        return 0;
    }
    EM_ASM({
        //
        if( window.updateLoadingStatus )
        {
            updateLoadingStatus('Ready');
        }
    });
    printf("Loaded model: vertex count: %d\n", cache_model->vertex_count);

    struct SceneModel* scene_model = (struct SceneModel*)malloc(sizeof(struct SceneModel));
    memset(scene_model, 0, sizeof(struct SceneModel));
    scene_model->model = cache_model;

    for( int i = 0; i < cache_model->vertex_count; i++ )
    {
        printf(
            "Vertex %d: %d %d %d\n",
            i,
            cache_model->vertices_x[i],
            cache_model->vertices_y[i],
            cache_model->vertices_z[i]);
    }

    struct ModelNormals* normals = (struct ModelNormals*)malloc(sizeof(struct ModelNormals));
    memset(normals, 0, sizeof(struct ModelNormals));

    normals->lighting_vertex_normals =
        (struct LightingNormal*)malloc(sizeof(struct LightingNormal) * cache_model->vertex_count);
    memset(
        normals->lighting_vertex_normals,
        0,
        sizeof(struct LightingNormal) * cache_model->vertex_count);
    normals->lighting_face_normals =
        (struct LightingNormal*)malloc(sizeof(struct LightingNormal) * cache_model->face_count);
    memset(
        normals->lighting_face_normals, 0, sizeof(struct LightingNormal) * cache_model->face_count);

    normals->lighting_vertex_normals_count = cache_model->vertex_count;
    normals->lighting_face_normals_count = cache_model->face_count;

    calculate_vertex_normals(
        normals->lighting_vertex_normals,
        normals->lighting_face_normals,
        cache_model->vertex_count,
        cache_model->face_indices_a,
        cache_model->face_indices_b,
        cache_model->face_indices_c,
        cache_model->vertices_x,
        cache_model->vertices_y,
        cache_model->vertices_z,
        cache_model->face_count);

    scene_model->normals = normals;

    struct ModelLighting* lighting = (struct ModelLighting*)malloc(sizeof(struct ModelLighting));
    memset(lighting, 0, sizeof(struct ModelLighting));
    lighting->face_colors_hsl_a = (int*)malloc(sizeof(int) * scene_model->model->face_count);
    memset(lighting->face_colors_hsl_a, 0, sizeof(int) * scene_model->model->face_count);
    lighting->face_colors_hsl_b = (int*)malloc(sizeof(int) * scene_model->model->face_count);
    memset(lighting->face_colors_hsl_b, 0, sizeof(int) * scene_model->model->face_count);
    lighting->face_colors_hsl_c = (int*)malloc(sizeof(int) * scene_model->model->face_count);
    memset(lighting->face_colors_hsl_c, 0, sizeof(int) * scene_model->model->face_count);

    scene_model->lighting = lighting;

    int light_ambient = 64;
    int light_attenuation = 768;
    int lightsrc_x = -50;
    int lightsrc_y = -10;
    int lightsrc_z = -50;

    light_ambient += scene_model->light_ambient;
    // 2004Scape multiplies contrast by 5.
    // Later versions do not.
    light_attenuation += scene_model->light_contrast;

    int light_magnitude =
        (int)sqrt(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
    int attenuation = (light_attenuation * light_magnitude) >> 8;

    apply_lighting(
        lighting->face_colors_hsl_a,
        lighting->face_colors_hsl_b,
        lighting->face_colors_hsl_c,
        scene_model->aliased_lighting_normals
            ? scene_model->aliased_lighting_normals->lighting_vertex_normals
            : scene_model->normals->lighting_vertex_normals,
        scene_model->normals->lighting_face_normals,
        scene_model->model->face_indices_a,
        scene_model->model->face_indices_b,
        scene_model->model->face_indices_c,
        scene_model->model->face_count,
        scene_model->model->face_colors,
        scene_model->model->face_alphas,
        scene_model->model->face_textures,
        scene_model->model->face_infos,
        light_ambient,
        attenuation,
        lightsrc_x,
        lightsrc_y,
        lightsrc_z);

    scene_model->lighting = lighting;

    g_scene_model = scene_model;

    return 1;
}

// Keyboard event handlers
EM_BOOL
key_callback(int eventType, const EmscriptenKeyboardEvent* e, void* userData)
{
    ImGuiIO& io = ImGui::GetIO();

    // Update ImGui key state
    if( eventType == EMSCRIPTEN_EVENT_KEYDOWN || eventType == EMSCRIPTEN_EVENT_KEYUP )
    {
        io.KeyCtrl = e->ctrlKey;
        io.KeyShift = e->shiftKey;
        io.KeyAlt = e->altKey;
        io.KeySuper = e->metaKey;

        // Map common keys
        if( e->keyCode == 9 )
            io.AddKeyEvent(ImGuiKey_Tab, eventType == EMSCRIPTEN_EVENT_KEYDOWN);
        if( e->keyCode == 37 )
            io.AddKeyEvent(ImGuiKey_LeftArrow, eventType == EMSCRIPTEN_EVENT_KEYDOWN);
        if( e->keyCode == 39 )
            io.AddKeyEvent(ImGuiKey_RightArrow, eventType == EMSCRIPTEN_EVENT_KEYDOWN);
        if( e->keyCode == 38 )
            io.AddKeyEvent(ImGuiKey_UpArrow, eventType == EMSCRIPTEN_EVENT_KEYDOWN);
        if( e->keyCode == 40 )
            io.AddKeyEvent(ImGuiKey_DownArrow, eventType == EMSCRIPTEN_EVENT_KEYDOWN);
        if( e->keyCode == 36 )
            io.AddKeyEvent(ImGuiKey_Home, eventType == EMSCRIPTEN_EVENT_KEYDOWN);
        if( e->keyCode == 35 )
            io.AddKeyEvent(ImGuiKey_End, eventType == EMSCRIPTEN_EVENT_KEYDOWN);
        if( e->keyCode == 33 )
            io.AddKeyEvent(ImGuiKey_PageUp, eventType == EMSCRIPTEN_EVENT_KEYDOWN);
        if( e->keyCode == 34 )
            io.AddKeyEvent(ImGuiKey_PageDown, eventType == EMSCRIPTEN_EVENT_KEYDOWN);
        if( e->keyCode == 13 )
            io.AddKeyEvent(ImGuiKey_Enter, eventType == EMSCRIPTEN_EVENT_KEYDOWN);
        if( e->keyCode == 27 )
            io.AddKeyEvent(ImGuiKey_Escape, eventType == EMSCRIPTEN_EVENT_KEYDOWN);
        if( e->keyCode == 8 )
            io.AddKeyEvent(ImGuiKey_Backspace, eventType == EMSCRIPTEN_EVENT_KEYDOWN);
        if( e->keyCode == 46 )
            io.AddKeyEvent(ImGuiKey_Delete, eventType == EMSCRIPTEN_EVENT_KEYDOWN);
        if( e->keyCode == 32 )
            io.AddKeyEvent(ImGuiKey_Space, eventType == EMSCRIPTEN_EVENT_KEYDOWN);
    }

    // Only handle game input if ImGui is not capturing keyboard
    if( !io.WantCaptureKeyboard )
    {
        bool* keyState = nullptr;

        // Map key codes to key states
        switch( e->keyCode )
        {
        case 87: // W key
            keyState = &wKeyPressed;
            break;
        case 65: // A key
            keyState = &aKeyPressed;
            break;
        case 83: // S key
            keyState = &sKeyPressed;
            break;
        case 68: // D key
            keyState = &dKeyPressed;
            break;
        case 82: // R key
            keyState = &rKeyPressed;
            break;
        case 70: // F key
            keyState = &fKeyPressed;
            break;
        }

        if( keyState )
        {
            if( eventType == EMSCRIPTEN_EVENT_KEYDOWN )
            {
                *keyState = true;
            }
            else if( eventType == EMSCRIPTEN_EVENT_KEYUP )
            {
                *keyState = false;
            }
        }
    }

    return EM_TRUE;
}

void
cleanup()
{
    // Cleanup textures
    for( const auto& tex : g_textures )
    {
        if( tex.id != 0 )
        {
            glDeleteTextures(1, &tex.id);
        }
    }
    g_textures.clear();

    // Cleanup texture cache
    if( g_textures_cache )
    {
        textures_cache_free(g_textures_cache);
        g_textures_cache = NULL;
    }

    // Cleanup ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    // Cleanup SDL
    SDL_GL_DeleteContext(g_gl_context);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
}

EM_BOOL
sdl_event_handler(int eventType, const void* keyEvent, void* userData)
{
    SDL_Event event;
    while( SDL_PollEvent(&event) )
    {
        ImGui_ImplSDL2_ProcessEvent(&event);

        ImGuiIO& io = ImGui::GetIO();

        // Handle SDL events
        switch( event.type )
        {
        case SDL_QUIT:
            cleanup();
            emscripten_cancel_main_loop();
            return EM_FALSE;

        case SDL_WINDOWEVENT:
            if( event.window.event == SDL_WINDOWEVENT_CLOSE )
            {
                cleanup();
                emscripten_cancel_main_loop();
                return EM_FALSE;
            }
            break;

        case SDL_MOUSEBUTTONDOWN:
            if( !io.WantCaptureMouse )
            {
                isDragging = true;
                lastX = event.button.x;
                lastY = event.button.y;
            }
            break;

        case SDL_MOUSEBUTTONUP:
            if( !io.WantCaptureMouse )
            {
                isDragging = false;
            }
            break;

        case SDL_MOUSEMOTION:
            if( !io.WantCaptureMouse && isDragging )
            {
                float deltaX = event.motion.x - lastX;
                float deltaY = event.motion.y - lastY;

                rotationY -= deltaX * ROTATE_SPEED;
                rotationX += deltaY * ROTATE_SPEED;
                rotationX = fmax(fmin(rotationX, M_PI / 2), -M_PI / 2);

                lastX = event.motion.x;
                lastY = event.motion.y;
            }
            break;

        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if( !io.WantCaptureKeyboard )
            {
                bool isDown = event.type == SDL_KEYDOWN;
                switch( event.key.keysym.sym )
                {
                case SDLK_w:
                    wKeyPressed = isDown;
                    break;
                case SDLK_a:
                    aKeyPressed = isDown;
                    break;
                case SDLK_s:
                    sKeyPressed = isDown;
                    break;
                case SDLK_d:
                    dKeyPressed = isDown;
                    break;
                case SDLK_r:
                    rKeyPressed = isDown;
                    break;
                case SDLK_f:
                    fKeyPressed = isDown;
                    break;
                }
            }
            break;
        }
    }
    return EM_TRUE;
}

EM_BOOL
loop(double time, void* userData)
{
    // Handle SDL events for ImGui
    sdl_event_handler(0, nullptr, nullptr);

    render();
    return EM_TRUE;
}

int
main()
{
    init_hsl16_to_rgb_table();
    init_sin_table();
    init_cos_table();
    init_tan_table();

    int model = load_model();
    if( !model )
    {
        printf("Failed to load model\n");
        return 0;
    }

    // Initialize SDL for input handling only
    if( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0 )
    {
        printf("Error: %s\n", SDL_GetError());
        return 0;
    }

    // Create a dummy SDL window for input handling
    g_window = SDL_CreateWindow(
        "3D Raster",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        SDL_WINDOW_HIDDEN); // Hidden window, we'll use the Emscripten canvas
    if( !g_window )
    {
        printf("Failed to create window: %s\n", SDL_GetError());
        return 0;
    }

    // Try WebGL2 first, then fall back to WebGL1
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.enableExtensionsByDefault = 1;
    attrs.alpha = 1;
    attrs.depth = 1;
    attrs.stencil = 0;
    attrs.antialias = 0;
    attrs.premultipliedAlpha = 0;
    attrs.preserveDrawingBuffer = 0;
    attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_DEFAULT;
    attrs.failIfMajorPerformanceCaveat = 0;

    // Check WebGL2 support first
    /**
     * Note: I found that even if requesting webgl2 and it's disabled, emscripten will not fail to
     * create a context for itself, so we need to check in javascript first.
     */
    bool webgl2_supported = EM_ASM_INT({
        try
        {
            const testCanvas = document.createElement('canvas');
            const gl = testCanvas.getContext('webgl2');
            if( !gl )
            {
                console.log("WebGL2 not supported or blocked");
                return 0;
            }
            return 1;
        }
        catch( e )
        {
            console.error("Error checking WebGL2 support:", e);
            return 0;
        }
    });

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context;
    if( webgl2_supported )
    {
        // Try WebGL2 first
        attrs.majorVersion = 2;
        attrs.minorVersion = 0;
        context = emscripten_webgl_create_context("#canvas", &attrs);

        if( context >= 0 )
        {
            g_using_webgl2 = true;
            printf("Using WebGL2\n");
        }
        else
        {
            printf(
                "WebGL2 support check passed but context creation failed, falling back to "
                "WebGL1\n");
            webgl2_supported = false;
        }
    }

    if( !webgl2_supported || context < 0 )
    {
        // Fall back to WebGL1
        attrs.majorVersion = 1;
        attrs.minorVersion = 0;
        context = emscripten_webgl_create_context("#canvas", &attrs);
        g_using_webgl2 = false;

        if( context < 0 )
        {
            printf("Failed to create WebGL context!\n");
            return 0;
        }
        printf("Using WebGL1\n");
    }

    // Make the context current before querying version
    if( emscripten_webgl_make_context_current(context) != EMSCRIPTEN_RESULT_SUCCESS )
    {
        printf("Failed to make WebGL context current!\n");
        return 0;
    }

    const char* version = NULL;

    if( !g_using_webgl2 )
    {
        // version = (const char*)EM_ASM_INT({
        //     const canvas = document.querySelector('#canvas');
        //     const gl = canvas.getContext('webgl');
        //     if( !gl )
        //     {
        //         console.log("WebGL1 not supported or blocked");
        //         return stringToNewUTF8("Failed");
        //     }

        //     const versionStr = gl.getParameter(gl.VERSION);
        //     return stringToNewUTF8(versionStr); // allocates in wasm heap
        // });

        version = "WebGL1";
    }
    else
    {
        //       glGetString is deprecated in webgl1 due to security reasons
        // Get WebGL version after context is made current
        version = (const char*)glGetString(GL_VERSION);
        if( !version )
        {
            printf("Failed to get WebGL version!\n");
            return 0;
        }

        // version = (const char*)EM_ASM_INT({
        //     const canvas = document.querySelector('#canvas');
        //     const gl = canvas.getContext('webgl2');
        //     if( !gl )
        //     {
        //         console.log("WebGL2 not supported or blocked");
        //         return stringToNewUTF8("Failed");
        //     }

        //     const versionStr = gl.getParameter(gl.VERSION);
        //     return stringToNewUTF8(versionStr); // allocates in wasm heap
        // });
    }

    if( strcmp(version, "Failed") == 0 )
    {
        printf("Failed to get WebGL version!\n");
        return 0;
    }

    printf("WebGL Version: %s\n", version);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;                               // Don't save settings
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;   // Enable keyboard controls
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // Don't change mouse cursor
    io.ConfigWindowsMoveFromTitleBarOnly = true;            // Only move windows from title bar
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;   // We can honor GetMouseCursor() values

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);

    context = emscripten_webgl_create_context("#canvas", &attrs);
    if( emscripten_webgl_make_context_current(context) != EMSCRIPTEN_RESULT_SUCCESS )
    {
        printf("Failed to make WebGL context current!\n");
        return 0;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(g_window, (void*)context);
    ImGui_ImplOpenGL3_Init(g_using_webgl2 ? "#version 300 es" : "#version 100");

    // Initialize OpenGL
    initGL();

    // Register SDL event handler for ImGui
    SDL_EventState(SDL_MOUSEMOTION, SDL_ENABLE);
    SDL_EventState(SDL_MOUSEBUTTONDOWN, SDL_ENABLE);
    SDL_EventState(SDL_MOUSEBUTTONUP, SDL_ENABLE);
    SDL_EventState(SDL_MOUSEWHEEL, SDL_ENABLE);
    SDL_EventState(SDL_KEYDOWN, SDL_ENABLE);
    SDL_EventState(SDL_KEYUP, SDL_ENABLE);
    SDL_EventState(SDL_TEXTINPUT, SDL_ENABLE);

    // Register SDL event handler for ImGui
    emscripten_set_main_loop_timing(EM_TIMING_RAF, 0);

    emscripten_request_animation_frame_loop(loop, nullptr);

    return 0;
}
