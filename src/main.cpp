#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include <GLES3/gl3.h>
#include <emscripten/html5.h>

#include <SDL.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <emscripten.h>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Global SDL variables
SDL_Window* g_window = nullptr;
SDL_GLContext g_gl_context = nullptr;

extern "C" {
#include "osrs/cache.h"
#include "osrs/filelist.h"
#include "osrs/frustrum_cullmap.h"
#include "osrs/render.h"
#include "osrs/scene.h"
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
uniform float uRotationX;  // pitch
uniform float uRotationY;  // yaw
// uniform float uDistance;   // zoom
uniform vec3 uCameraPos;   // camera position
uniform float uAspect;     // aspect ratio

out vec3 vColor;

mat4 createProjectionMatrix(float fov, float aspect, float near, float far) {
    float y = 1.0 / tan(fov * 0.5);
    float x = y / aspect;
    float z = far / (far - near);
    
    return mat4(
        x, 0.0, 0.0, 0.0,
        0.0, -y, 0.0, 0.0,  // Negate y to flip screen space coordinates
        0.0, 0.0, z, 1.0,
        0.0, 0.0, -z * near, 0.0
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
    return yawMatrix * pitchMatrix * translateMatrix;
}

void main() {
    // Create view matrix with camera transformations
    mat4 viewMatrix = createViewMatrix(uCameraPos, uRotationX, uRotationY);
    
    // Create projection matrix with 90 degree FOV (same as Metal version)
    mat4 projection = createProjectionMatrix(radians(90.0), uAspect, 0.1, 10000.0);
    
    // Transform vertex position
    vec4 viewPos = viewMatrix * vec4(aPos, 1.0);
    
    // Apply zoom by moving camera back
    // viewPos.z += uDistance;
    
    // Pass through the color
    vColor = aColor;
    
    gl_Position = projection * viewPos;
}
)";

// Fragment shader source
const char* fragmentShaderSource = R"(#version 300 es
precision mediump float;

in vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
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
GLuint VBO, colorVBO, EBO;
float rotationX = 0.1f; // Initial pitch (look down slightly)
float rotationY = 0.0f; // Initial yaw
// float distance = 1000.0f;  // Initial zoom level
float cameraX = 0.0f;
float cameraY = 50.0f;
float cameraZ = -300.0f;
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

// Cube vertices with smoothed normals at corners
std::vector<float> vertices = {
    // Positions and averaged normals for each vertex
    // Front-bottom-left
    -0.5f,
    -0.5f,
    0.5f,
    -0.577f,
    -0.577f,
    0.577f, // v0
            // Front-bottom-right
    0.5f,
    -0.5f,
    0.5f,
    0.577f,
    -0.577f,
    0.577f, // v1
            // Front-top-right
    0.5f,
    0.5f,
    0.5f,
    0.577f,
    0.577f,
    0.577f, // v2
    // Front-top-left
    -0.5f,
    0.5f,
    0.5f,
    -0.577f,
    0.577f,
    0.577f, // v3
    // Back-bottom-left
    -0.5f,
    -0.5f,
    -0.5f,
    -0.577f,
    -0.577f,
    -0.577f, // v4
             // Back-bottom-right
    0.5f,
    -0.5f,
    -0.5f,
    0.577f,
    -0.577f,
    -0.577f, // v5
             // Back-top-right
    0.5f,
    0.5f,
    -0.5f,
    0.577f,
    0.577f,
    -0.577f, // v6
    // Back-top-left
    -0.5f,
    0.5f,
    -0.5f,
    -0.577f,
    0.577f,
    -0.577f // v7
};

// Indices for the cube using shared vertices
std::vector<unsigned int> indices = {
    // Front face
    0,
    1,
    2,
    0,
    2,
    3, // v0-v1-v2, v0-v2-v3
    // Back face
    5,
    4,
    7,
    5,
    7,
    6, // v5-v4-v7, v5-v7-v6
    // Top face
    3,
    2,
    6,
    3,
    6,
    7, // v3-v2-v6, v3-v6-v7
    // Bottom face
    4,
    5,
    1,
    4,
    1,
    0, // v4-v5-v1, v4-v1-v0
    // Right face
    1,
    5,
    6,
    1,
    6,
    2, // v1-v5-v6, v1-v6-v2
    // Left face
    4,
    0,
    3,
    4,
    3,
    7 // v4-v0-v3, v4-v3-v7
};

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
        800,
        600,
        50);

    int index = 0;
    memset(g_pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));
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

        model_draw_face(
            g_pixel_buffer,
            face,
            g_scene_model->model->face_infos,
            g_scene_model->model->face_indices_a,
            g_scene_model->model->face_indices_b,
            g_scene_model->model->face_indices_c,
            g_scene_model->model->face_count,
            g_iter_model.screen_vertices_x,
            g_iter_model.screen_vertices_y,
            g_iter_model.screen_vertices_z,
            g_iter_model.ortho_vertices_x,
            g_iter_model.ortho_vertices_y,
            g_iter_model.ortho_vertices_z,
            g_scene_model->model->vertex_count,
            g_scene_model->model->face_textures,
            g_scene_model->model->face_texture_coords,
            g_scene_model->model->textured_face_count,
            g_scene_model->model->textured_p_coordinate,
            g_scene_model->model->textured_m_coordinate,
            g_scene_model->model->textured_n_coordinate,
            g_scene_model->model->textured_face_count,
            g_scene_model->lighting->face_colors_hsl_a,
            g_scene_model->lighting->face_colors_hsl_b,
            g_scene_model->lighting->face_colors_hsl_c,
            g_scene_model->model->face_alphas,
            SCREEN_WIDTH / 2,
            SCREEN_HEIGHT / 2,
            SCREEN_WIDTH,
            SCREEN_HEIGHT,
            NULL);
    }

    g_sort_order_count = index;
}

// Function to transform a vertex by the model matrix
void
transformVertex(const float* vertex, float* result, float angleX, float angleY, float dist)
{
    // Create rotation matrices
    float cx = cos(angleX);
    float sx = sin(angleX);
    float cy = cos(angleY);
    float sy = sin(angleY);

    // Apply rotations and translation
    float tempX = vertex[0];
    float tempY = vertex[1];
    float tempZ = vertex[2];

    // Rotate around X
    float y2 = tempY * cx - tempZ * sx;
    float z2 = tempY * sx + tempZ * cx;

    // Rotate around Y
    result[0] = tempX * cy - z2 * sy;
    result[1] = y2;
    result[2] = tempX * sy + z2 * cy + dist;
}

// Function to calculate triangle depth
float
calculateTriangleDepth(
    const std::vector<float>& vertices,
    const unsigned int* indices,
    float angleX,
    float angleY,
    float dist)
{
    float depth = 0.0f;
    float transformedVertex[3];

    // Calculate average Z of transformed vertices
    for( int i = 0; i < 3; i++ )
    {
        const float* vertex = &vertices[indices[i] * 6]; // Skip normal data
        transformVertex(vertex, transformedVertex, angleX, angleY, dist);
        depth += transformedVertex[2];
    }

    return depth / 3.0f;
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

    // Ensure VAO is bound before updating EBO
    glBindVertexArray(VAO);

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
    GLuint vertexShader = createShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = createShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

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

    // Initialize triangles array
    sort_model_faces();

    // Create and bind VAO first
    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    // Create buffers
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &colorVBO);
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

    // Bind EBO and initialize indices
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

    // Initialize sorted indices after buffer setup
    updateTriangleOrder();

    // Normal attribute
    // glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 *
    // sizeof(float))); glEnableVertexAttribArray(1);

    // Enable depth testing only for now
    glEnable(GL_DEPTH_TEST);
    // Disable face culling for debugging
    // glEnable(GL_CULL_FACE);
    // glFrontFace(GL_CW);
    // glCullFace(GL_FRONT);
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

EM_BOOL
mouse_callback(int eventType, const EmscriptenMouseEvent* e, void* userData)
{
    if( eventType == EMSCRIPTEN_EVENT_MOUSEDOWN )
    {
        isDragging = true;
        lastX = e->clientX;
        lastY = e->clientY;
    }
    else if( eventType == EMSCRIPTEN_EVENT_MOUSEUP )
    {
        isDragging = false;
    }
    else if( eventType == EMSCRIPTEN_EVENT_MOUSEMOVE && isDragging )
    {
        float deltaX = e->clientX - lastX;
        float deltaY = e->clientY - lastY;

        // Update rotation angles (scale down the movement)
        rotationY -= deltaX * ROTATE_SPEED;
        rotationX += deltaY * ROTATE_SPEED;

        // Clamp pitch to prevent over-rotation (same as Metal version)
        rotationX = fmax(fmin(rotationX, M_PI / 2), -M_PI / 2);

        lastX = e->clientX;
        lastY = e->clientY;
    }
    return EM_TRUE;
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
                // Create container for the canvases
                let container = document.createElement('div');
                container.style.display = 'flex';
                container.style.justifyContent = 'center';
                container.style.alignItems = 'center';
                // container.style.gap = '20px';
                // container.style.padding = '20px';
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
                mainCanvas.style.aspectRatio = $0 + '/' + $1;

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
                    pixelCanvas.style.aspectRatio = $0 + '/' + $1;
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
    static int frame_count = 0;
    frame_count++;
    if( frame_count % 60 == 0 )
    { // Print every 60 frames
        printf("Render frame %d\n", frame_count);
    }

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

    ImGui::End();

    // Update camera position based on keyboard state
    updateCameraPosition(
        wKeyPressed, sKeyPressed, aKeyPressed, dKeyPressed, rKeyPressed, fKeyPressed);

    // Clear any existing errors
    while( glGetError() != GL_NO_ERROR )
        ;

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Ensure proper state
    glUseProgram(shaderProgram);

    // Get and validate uniform locations
    GLint rotationXLoc = glGetUniformLocation(shaderProgram, "uRotationX");
    GLint rotationYLoc = glGetUniformLocation(shaderProgram, "uRotationY");
    // GLint distanceLoc = glGetUniformLocation(shaderProgram, "uDistance");
    GLint cameraPosLoc = glGetUniformLocation(shaderProgram, "uCameraPos");
    GLint aspectLoc = glGetUniformLocation(shaderProgram, "uAspect");

    if( rotationXLoc == -1 || rotationYLoc == -1 || cameraPosLoc == -1 || aspectLoc == -1 )
    {
        printf("Failed to get uniform locations\n");
        return;
    }

    // Update uniforms
    glUniform1f(rotationXLoc, rotationX);
    glUniform1f(rotationYLoc, rotationY);
    // glUniform1f(distanceLoc, distance);
    glUniform3f(cameraPosLoc, cameraX, cameraY, cameraZ);
    glUniform1f(aspectLoc, 800.0f / 600.0f); // TODO: Get actual window dimensions

    // Sort triangles based on current transformation
    updateTriangleOrder();

    // Verify we have data to draw
    if( g_sort_order_count > 0 )
    {
        glDrawElements(GL_TRIANGLES, g_sort_order_count * 3, GL_UNSIGNED_INT, 0);

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
    drawPixelBufferToCanvas();

    // Render ImGui
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

EM_BOOL
loop(double time, void* userData)
{
    render();
    return EM_TRUE;
}

static const int TZTOK_JAD_MODEL_ID = 9319;
static const int TZTOK_JAD_NPCTYPE_ID = 3127;

static int
load_model()
{
    printf("Loading model...\n");

    struct Cache* cache = cache_new_from_directory("../cache");
    if( !cache )
    {
        printf("Failed to load cache from directory: ../cache\n");
        return 0;
    }
    printf("Loaded cache\n");

    struct CacheModel* cache_model = model_new_from_cache(cache, TZTOK_JAD_MODEL_ID);
    if( !cache_model )
    {
        printf("Failed to load model\n");
        return 0;
    }
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

    return EM_TRUE;
}

void
cleanup()
{
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
        }
    }
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

    // Initialize SDL
    if( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0 )
    {
        printf("Error: %s\n", SDL_GetError());
        return 0;
    }

    // GL ES 3.0 + GLSL 300 es
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
        return 0;
    }

    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.enableExtensionsByDefault = 1;
    attrs.alpha = 1;
    attrs.depth = 1;
    attrs.stencil = 1;
    attrs.antialias = 1;
    attrs.premultipliedAlpha = 0;
    attrs.preserveDrawingBuffer = 0;
    attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_DEFAULT;
    attrs.failIfMajorPerformanceCaveat = 0;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = emscripten_webgl_create_context("#canvas", &attrs);
    if( context < 0 )
    {
        printf("Failed to create WebGL2 context!\n");
        return 0;
    }
    emscripten_webgl_make_context_current(context);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.IniFilename = nullptr; // Don't save settings

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

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(g_window, nullptr);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    // Initialize OpenGL
    initGL();

    // Register mouse event handlers
    emscripten_set_mousedown_callback("#canvas", nullptr, true, mouse_callback);
    emscripten_set_mouseup_callback("#canvas", nullptr, true, mouse_callback);
    emscripten_set_mousemove_callback("#canvas", nullptr, true, mouse_callback);

    // Register keyboard event handlers
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, key_callback);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, key_callback);

    emscripten_request_animation_frame_loop(loop, nullptr);

    return 0;
}
