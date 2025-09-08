#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cmath>

extern "C" {
#include "shared_tables.h"
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
#include "screen.h"
}

struct SceneModel* g_scene_model = NULL;
struct IterRenderModel g_iter_model;
static int g_sort_order[1024] = { 0 };  // Stores face indices for sorting
static int g_sort_order_count = 0;
static unsigned int g_element_indices[1024 * 3] = { 0 };  // Stores vertex indices for OpenGL
static float g_vertices[10000] = { 0 };


// Vertex shader source
const char* vertexShaderSource = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform float uRotationX;
uniform float uRotationY;
uniform float uDistance;

out vec3 vColor;

mat4 createProjectionMatrix(float fov, float aspect, float near, float far) {
    float f = 1.0 / tan(fov * 0.5);
    float rangeInv = 1.0 / (far - near);
    
    return mat4(
        f / aspect, 0.0, 0.0, 0.0,
        0.0, f, 0.0, 0.0,
        0.0, 0.0, (far + near) * rangeInv, 1.0,
        0.0, 0.0, -2.0 * near * far * rangeInv, 0.0
    );
}

mat4 createModelMatrix(float angleX, float angleY, float dist) {
    float cx = cos(angleX);
    float sx = sin(angleX);
    float cy = cos(angleY);
    float sy = sin(angleY);
    
    return mat4(
        cy, sx*sy, -cx*sy, 0.0,
        0.0, -cx, -sx, 0.0,
        -sy, sx*cy, cx*cy, 0.0,
        0.0, 0.0, dist, 1.0
    );
}

void main() {
    mat4 model = createModelMatrix(uRotationX, uRotationY, uDistance);
    mat4 projection = createProjectionMatrix(radians(45.0), 800.0/600.0, 1.0, 10000.0);
    
    vec4 worldPos = model * vec4(aPos, 1.0);
    
    // Pass through the color
    vColor = aColor;
    
    gl_Position = projection * worldPos;
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
struct Triangle {
    unsigned int indices[3];  // Original indices
    float depth;             // Average Z depth
};

// Global variables
GLuint shaderProgram;
GLuint VAO;
GLuint VBO, colorVBO, EBO;
float rotationX = 0.0f;
float rotationY = 0.0f;
float distance = 2000.0f;
bool isDragging = false;
float lastX = 0.0f;
float lastY = 0.0f;
std::vector<unsigned int> sortedIndices;

// Cube vertices with smoothed normals at corners
std::vector<float> vertices = {
    // Positions and averaged normals for each vertex
    // Front-bottom-left
    -0.5f, -0.5f,  0.5f,  -0.577f, -0.577f,  0.577f,  // v0
    // Front-bottom-right
     0.5f, -0.5f,  0.5f,   0.577f, -0.577f,  0.577f,  // v1
    // Front-top-right
     0.5f,  0.5f,  0.5f,   0.577f,  0.577f,  0.577f,  // v2
    // Front-top-left
    -0.5f,  0.5f,  0.5f,  -0.577f,  0.577f,  0.577f,  // v3
    // Back-bottom-left
    -0.5f, -0.5f, -0.5f,  -0.577f, -0.577f, -0.577f,  // v4
    // Back-bottom-right
     0.5f, -0.5f, -0.5f,   0.577f, -0.577f, -0.577f,  // v5
    // Back-top-right
     0.5f,  0.5f, -0.5f,   0.577f,  0.577f, -0.577f,  // v6
    // Back-top-left
    -0.5f,  0.5f, -0.5f,  -0.577f,  0.577f, -0.577f   // v7
};

// Indices for the cube using shared vertices
std::vector<unsigned int> indices = {
    // Front face
    0, 1, 2,    0, 2, 3,    // v0-v1-v2, v0-v2-v3
    // Back face
    5, 4, 7,    5, 7, 6,    // v5-v4-v7, v5-v7-v6
    // Top face
    3, 2, 6,    3, 6, 7,    // v3-v2-v6, v3-v6-v7
    // Bottom face
    4, 5, 1,    4, 1, 0,    // v4-v5-v1, v4-v1-v0
    // Right face
    1, 5, 6,    1, 6, 2,    // v1-v5-v6, v1-v6-v2
    // Left face
    4, 0, 3,    4, 3, 7     // v4-v0-v3, v4-v3-v7
};




static void sort_model_faces()
{
    
    iter_render_model_init(
        &g_iter_model,
        g_scene_model,
        // TODO: For wall decor, this should probably be set on the model->yaw rather than
        // on the op.
        0,
        0,
        distance,
        0,
        0,
        0,
        0,
        512,
        800,
        600,
        50);

    int index = 0;
    while( iter_render_model_next(&g_iter_model) )
    {
        int face = g_iter_model.value_face;
        if (face < 0 || face >= g_scene_model->model->face_count) {
            printf("Warning: Face %d is out of bounds\n", face);
            continue;
        }
        
        g_sort_order[index] = face;
        index++;
    }

    g_sort_order_count = index;
}

// Function to transform a vertex by the model matrix
void transformVertex(const float* vertex, float* result, float angleX, float angleY, float dist) {
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
float calculateTriangleDepth(const std::vector<float>& vertices, const unsigned int* indices, 
                           float angleX, float angleY, float dist) {
    float depth = 0.0f;
    float transformedVertex[3];
    
    // Calculate average Z of transformed vertices
    for (int i = 0; i < 3; i++) {
        const float* vertex = &vertices[indices[i] * 6]; // Skip normal data
        transformVertex(vertex, transformedVertex, angleX, angleY, dist);
        depth += transformedVertex[2];
    }
    
    return depth / 3.0f;
}

// Function to update triangle depths and sort
void updateTriangleOrder() {
    // Calculate depths for all triangles
    sort_model_faces();

    
    // Convert face indices to vertex indices
    // for (int i = 0; i < g_sort_order_count; i++) {
    //     int face_idx = g_sort_order[i];
        
    //     // Each face maps to 3 vertex indices
    //     unsigned int v1 = g_scene_model->model->face_indices_a[face_idx];
    //     unsigned int v2 = g_scene_model->model->face_indices_b[face_idx];
    //     unsigned int v3 = g_scene_model->model->face_indices_c[face_idx];
        
    //     g_element_indices[i * 3] = v1;
    //     g_element_indices[i * 3 + 1] = v2;
    //     g_element_indices[i * 3 + 2] = v3;
    // }

    for (int i = 0; i < g_scene_model->model->face_count; i++) {
        int face_idx = i;
        
        unsigned int v1 = g_scene_model->model->face_indices_a[face_idx];
        unsigned int v2 = g_scene_model->model->face_indices_b[face_idx];
        unsigned int v3 = g_scene_model->model->face_indices_c[face_idx];
        
        g_element_indices[i * 3] = v1;
        g_element_indices[i * 3 + 1] = v2;
        g_element_indices[i * 3 + 2] = v3;
    }

    g_sort_order_count = g_scene_model->model->face_count;

    // Ensure VAO is bound before updating EBO
    glBindVertexArray(VAO);
    
    // Update GPU buffer with vertex indices
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, g_sort_order_count * 3 * sizeof(unsigned int),
                g_element_indices, GL_DYNAMIC_DRAW);
                
    // Check for errors after buffer update
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        printf("OpenGL error in updateTriangleOrder: 0x%x\n", err);
    }
}

GLuint createShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        printf("Shader compilation error: %s\n", infoLog);
        return 0;
    }
    return shader;
}




void initGL() {
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
    if (!success) {
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

    // Prepare vertex data
    for (int i = 0; i < g_scene_model->model->vertex_count; i++) {
        g_vertices[i * 3] = (float)g_scene_model->model->vertices_x[i];
        g_vertices[i * 3 + 1] = (float)g_scene_model->model->vertices_y[i];
        g_vertices[i * 3 + 2] = (float)g_scene_model->model->vertices_z[i];
    }

    printf("Vertices: %d\n", g_scene_model->model->vertex_count);
    
    // Setup vertex buffer
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, g_scene_model->model->vertex_count * 3 * sizeof(float), g_vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Create and setup color buffer
    std::vector<float> colors(g_scene_model->model->vertex_count * 3);
    for (int i = 0; i < g_sort_order_count; i++) {
        int face_idx = g_sort_order[i];
        
        // Get the HSL colors for each vertex of the face
        int hsl_a = g_scene_model->lighting->face_colors_hsl_a[face_idx];
        int hsl_b = g_scene_model->lighting->face_colors_hsl_b[face_idx];
        int hsl_c = g_scene_model->lighting->face_colors_hsl_c[face_idx];
        
        // Get vertex indices
        unsigned int vertex_a = g_scene_model->model->face_indices_a[face_idx];
        unsigned int vertex_b = g_scene_model->model->face_indices_b[face_idx];
        unsigned int vertex_c = g_scene_model->model->face_indices_c[face_idx];
        
        // Convert HSL to RGB and store for each vertex
        int rgb_a = g_hsl16_to_rgb_table[hsl_a];
        int rgb_b = g_hsl16_to_rgb_table[hsl_b];
        int rgb_c = g_hsl16_to_rgb_table[hsl_c];
        
        // Vertex A
        colors[vertex_a * 3] = ((rgb_a >> 16) & 0xFF) / 255.0f;
        colors[vertex_a * 3 + 1] = ((rgb_a >> 8) & 0xFF) / 255.0f;
        colors[vertex_a * 3 + 2] = (rgb_a & 0xFF) / 255.0f;
        
        // Vertex B
        colors[vertex_b * 3] = ((rgb_b >> 16) & 0xFF) / 255.0f;
        colors[vertex_b * 3 + 1] = ((rgb_b >> 8) & 0xFF) / 255.0f;
        colors[vertex_b * 3 + 2] = (rgb_b & 0xFF) / 255.0f;
        
        // Vertex C
        colors[vertex_c * 3] = ((rgb_c >> 16) & 0xFF) / 255.0f;
        colors[vertex_c * 3 + 1] = ((rgb_c >> 8) & 0xFF) / 255.0f;
        colors[vertex_c * 3 + 2] = (rgb_c & 0xFF) / 255.0f;
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
    // glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    // glEnableVertexAttribArray(1);
    
    // Enable depth testing only for now
    glEnable(GL_DEPTH_TEST);
    // Disable face culling for debugging
    // glEnable(GL_CULL_FACE);
    // glFrontFace(GL_CCW);
    // glCullFace(GL_BACK);
}

EM_BOOL mouse_callback(int eventType, const EmscriptenMouseEvent* e, void* userData) {
    if (eventType == EMSCRIPTEN_EVENT_MOUSEDOWN) {
        isDragging = true;
        lastX = e->clientX;
        lastY = e->clientY;
    } 
    else if (eventType == EMSCRIPTEN_EVENT_MOUSEUP) {
        isDragging = false;
    }
    else if (eventType == EMSCRIPTEN_EVENT_MOUSEMOVE && isDragging) {
        float deltaX = e->clientX - lastX;
        float deltaY = e->clientY - lastY;
        
        // Update rotation angles (scale down the movement)
        rotationY += deltaX * 0.01f;
        rotationX += deltaY * 0.01f;
        
        // Limit vertical rotation to avoid gimbal lock
        rotationX = fmax(fmin(rotationX, 1.5f), -1.5f);
        
        lastX = e->clientX;
        lastY = e->clientY;
    }
    return EM_TRUE;
}

void render() {
    static int frame_count = 0;
    frame_count++;
    if (frame_count % 60 == 0) {  // Print every 60 frames
        printf("Render frame %d\n", frame_count);
    }
    
    // Clear any existing errors
    while (glGetError() != GL_NO_ERROR);

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Ensure proper state
    glUseProgram(shaderProgram);
    glBindVertexArray(VAO);
    
    // Get and validate uniform locations
    GLint rotationXLoc = glGetUniformLocation(shaderProgram, "uRotationX");
    GLint rotationYLoc = glGetUniformLocation(shaderProgram, "uRotationY");
    GLint distanceLoc = glGetUniformLocation(shaderProgram, "uDistance");
    
    if (rotationXLoc == -1 || rotationYLoc == -1 || distanceLoc == -1) {
        printf("Failed to get uniform locations\n");
        return;
    }
    
    // Update uniforms
    glUniform1f(rotationXLoc, rotationX);
    glUniform1f(rotationYLoc, rotationY);
    glUniform1f(distanceLoc, distance);
    
    // Sort triangles based on current transformation
    updateTriangleOrder();
    
    // Verify we have data to draw
    if (g_sort_order_count > 0) {
        glDrawElements(GL_TRIANGLES, g_sort_order_count * 3, GL_UNSIGNED_INT, 0);
        
        // Check for errors after draw
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            printf("OpenGL error during draw: 0x%x\n", err);
        }
    } else {
        printf("No triangles to draw\n");
    }
    
    // Check for OpenGL errors after draw
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        printf("OpenGL error after draw: 0x%x\n", err);
    }
}

EM_BOOL loop(double time, void* userData) {
    render();
    return EM_TRUE;
}



static const int TZTOK_JAD_MODEL_ID = 9319;
static const int TZTOK_JAD_NPCTYPE_ID = 3127;

static int load_model() 
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

    for (int i = 0; i < cache_model->vertex_count; i++) {
        printf("Vertex %d: %d %d %d\n", i, cache_model->vertices_x[i], cache_model->vertices_y[i], cache_model->vertices_z[i]);
    }


    struct ModelNormals* normals =
        (struct ModelNormals*)malloc(sizeof(struct ModelNormals));
    memset(normals, 0, sizeof(struct ModelNormals));

    normals->lighting_vertex_normals = (struct LightingNormal*)malloc(
        sizeof(struct LightingNormal) * cache_model->vertex_count);
    memset(
        normals->lighting_vertex_normals,
        0,
        sizeof(struct LightingNormal) * cache_model->vertex_count);
    normals->lighting_face_normals = (struct LightingNormal*)malloc(
        sizeof(struct LightingNormal) * cache_model->face_count);
    memset(
        normals->lighting_face_normals,
        0,
        sizeof(struct LightingNormal) * cache_model->face_count);

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

    struct ModelLighting* lighting =
        (struct ModelLighting*)malloc(sizeof(struct ModelLighting));
    memset(lighting, 0, sizeof(struct ModelLighting));
    lighting->face_colors_hsl_a =
        (int*)malloc(sizeof(int) * scene_model->model->face_count);
    memset(lighting->face_colors_hsl_a, 0, sizeof(int) * scene_model->model->face_count);
    lighting->face_colors_hsl_b =
        (int*)malloc(sizeof(int) * scene_model->model->face_count);
    memset(lighting->face_colors_hsl_b, 0, sizeof(int) * scene_model->model->face_count);
    lighting->face_colors_hsl_c =
        (int*)malloc(sizeof(int) * scene_model->model->face_count);
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

    int light_magnitude = (int)sqrt(
        lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
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

int main() {
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

    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = emscripten_webgl_create_context("#canvas", &attrs);
    emscripten_webgl_make_context_current(context);
    
    initGL();
    
    // Register mouse event handlers
    emscripten_set_mousedown_callback("#canvas", nullptr, true, mouse_callback);
    emscripten_set_mouseup_callback("#canvas", nullptr, true, mouse_callback);
    emscripten_set_mousemove_callback("#canvas", nullptr, true, mouse_callback);
    
    emscripten_request_animation_frame_loop(loop, nullptr);
    
    return 0;
}
