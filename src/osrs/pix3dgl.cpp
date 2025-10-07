
#include "pix3dgl.h"

#include "graphics/uv_pnm.h"
#include "shared_tables.h"
//
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <unordered_map>

#include <vector>

// WebGL1 vertex shader
const char* g_vertex_shader_es2 = R"(
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
const char* g_fragment_shader_es2 = R"(
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

struct GLModel
{
    int idx;
    GLuint VBO;
    GLuint colorVBO;
    GLuint texCoordVBO;
    GLuint EBO;

    int face_count;
};

struct Pix3DGL
{
    GLuint vertex_shader_es2;
    GLuint fragment_shader_es2;
    GLuint program_es2;

    std::unordered_map<int, GLuint> texture_ids;
    std::unordered_map<int, GLModel> models;
};

extern "C" void
pix3dgl_model_load_textured_pnm(
    struct Pix3DGL* pix3dgl,
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
    // Get vertex positions for UV coordinate computation
    int texture_face = -1;
    int tp_vertex = -1;
    int tm_vertex = -1;
    int tn_vertex = -1;

    for( int face = 0; face < face_count; face++ )
    {
        if( face_texture_faces_nullable && face_texture_faces_nullable[face] != -1 )
        {
            texture_face = face_texture_faces_nullable[face];

            tp_vertex = face_p_coordinate_nullable[texture_face];
            tm_vertex = face_m_coordinate_nullable[texture_face];
            tn_vertex = face_n_coordinate_nullable[texture_face];
        }
        else
        {
            texture_face = face;

            tp_vertex = face_indices_a[texture_face];
            tm_vertex = face_indices_b[texture_face];
            tn_vertex = face_indices_c[texture_face];
        }

        int tp_vertex = face_indices_a[face];
        int tm_vertex = face_indices_b[face];
        int tn_vertex = face_indices_c[face];

        float p_x = vertices_x[tp_vertex];
        float p_y = vertices_y[tp_vertex];
        float p_z = vertices_z[tp_vertex];

        float m_x = vertices_x[tm_vertex];
        float m_y = vertices_y[tm_vertex];
        float m_z = vertices_z[tm_vertex];

        float n_x = vertices_x[tn_vertex];
        float n_y = vertices_y[tn_vertex];
        float n_z = vertices_z[tn_vertex];

        int a_x = face_indices_a[face];
        int a_y = face_indices_a[face];
        int a_z = face_indices_a[face];

        int b_x = face_indices_b[face];
        int b_y = face_indices_b[face];
        int b_z = face_indices_b[face];

        int c_x = face_indices_c[face];
        int c_y = face_indices_c[face];
        int c_z = face_indices_c[face];

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
    }
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

    // Create buffers
    glGenBuffers(1, &gl_model.VBO);
    glGenBuffers(1, &gl_model.colorVBO);
    glGenBuffers(1, &gl_model.texCoordVBO);
    glGenBuffers(1, &gl_model.EBO);

    std::vector<float> vertices(face_count * 9); // 3 vertices * 3 coordinates per face
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
    }

    glBindBuffer(GL_ARRAY_BUFFER, gl_model.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

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

        // Convert HSL to RGB
        int rgb_a = g_hsl16_to_rgb_table[hsl_a];
        int rgb_b = g_hsl16_to_rgb_table[hsl_b];
        int rgb_c = g_hsl16_to_rgb_table[hsl_c];

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
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    // Store the model in the map
    pix3dgl->models[idx] = gl_model;
}

extern "C" struct Pix3DGL*
pix3dgl_new()
{
    struct Pix3DGL* pix3dgl = new Pix3DGL();
    return pix3dgl;
}

extern "C" void
pix3dgl_render(struct Pix3DGL* pix3dgl)
{
    // Basic render implementation - would need proper scene setup
    // This is a stub for now

    for( auto& pair : pix3dgl->models )
    {
        GLModel& model = pair.second;

        glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, (void*)(0));
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
            glDeleteBuffers(1, &model.VBO);
            glDeleteBuffers(1, &model.colorVBO);
            glDeleteBuffers(1, &model.texCoordVBO);
            glDeleteBuffers(1, &model.EBO);
        }

        for( auto& pair : pix3dgl->texture_ids )
        {
            glDeleteTextures(1, &pair.second);
        }

        delete pix3dgl;
    }
}