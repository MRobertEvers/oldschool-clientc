#include "pg_render.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PG_FOV 70.0f
#define PG_NEAR_PLANE 1.0f
#define PG_FAR_PLANE 10000.0f
#define PG_MIN_ZOOM 100.0f
#define PG_MAX_ZOOM 1600.0f

/* ---- shader sources -------------------------------------------------------- */

/*
 * The HSL-to-RGB unpack happens in the vertex shader, exactly as the reference
 * does it. A cache face colour is a 16-bit HSL word, not RGB, and converting on
 * the CPU would mean redoing it for every vertex of every rebuilt frame.
 */
static const char* ENTITY_VS =
    "#version 330 core\n"
    "layout (location = 0) in ivec4 position;\n"
    "layout (location = 1) in ivec3 normal;\n"
    "out vec4 faceColour;\n"
    "out vec3 vertexNormal;\n"
    "out vec3 toLightVector;\n"
    "uniform mat4 transformationMatrix;\n"
    "uniform mat4 projectionMatrix;\n"
    "uniform mat4 viewMatrix;\n"
    "uniform vec3 lightPosition;\n"
    "void main(void) {\n"
    "    vec4 worldPosition = transformationMatrix * vec4(-float(position.x), float(position.y), "
    "float(position.z), 1.0);\n"
    "    gl_Position = projectionMatrix * viewMatrix * worldPosition;\n"
    "    gl_PointSize = 3.0;\n"
    "    int ahsl = position.w;\n"
    "    int hsl = ahsl & 0xFFFF;\n"
    "    float a = float((ahsl >> 16) & 0xFF) / 255.0;\n"
    "    int var5 = hsl / 128;\n"
    "    float var6 = float(var5 >> 3) / 64.0 + 0.0078125;\n"
    "    float var8 = float(var5 & 7) / 8.0 + 0.0625;\n"
    "    int var10 = hsl % 128;\n"
    "    float var11 = float(var10) / 128.0;\n"
    "    float r = var11; float g = var11; float b = var11;\n"
    "    if (var8 != 0.0) {\n"
    "        float var19;\n"
    "        if (var11 < 0.5) { var19 = var11 * (1.0 + var8); }\n"
    "        else { var19 = var11 + var8 - var11 * var8; }\n"
    "        float var21 = 2.0 * var11 - var19;\n"
    "        float var23 = var6 + 0.3333333333333333;\n"
    "        if (var23 > 1.0) { var23 -= 1.0; }\n"
    "        float var27 = var6 - 0.3333333333333333;\n"
    "        if (var27 < 0.0) { var27 += 1.0; }\n"
    "        if (6.0 * var23 < 1.0) { r = var21 + (var19 - var21) * 6.0 * var23; }\n"
    "        else if (2.0 * var23 < 1.0) { r = var19; }\n"
    "        else if (3.0 * var23 < 2.0) { r = var21 + (var19 - var21) * (0.6666666666666666 - "
    "var23) * 6.0; }\n"
    "        else { r = var21; }\n"
    "        if (6.0 * var6 < 1.0) { g = var21 + (var19 - var21) * 6.0 * var6; }\n"
    "        else if (2.0 * var6 < 1.0) { g = var19; }\n"
    "        else if (3.0 * var6 < 2.0) { g = var21 + (var19 - var21) * (0.6666666666666666 - "
    "var6) * 6.0; }\n"
    "        else { g = var21; }\n"
    "        if (6.0 * var27 < 1.0) { b = var21 + (var19 - var21) * 6.0 * var27; }\n"
    "        else if (2.0 * var27 < 1.0) { b = var19; }\n"
    "        else if (3.0 * var27 < 2.0) { b = var21 + (var19 - var21) * (0.6666666666666666 - "
    "var27) * 6.0; }\n"
    "        else { b = var21; }\n"
    "    }\n"
    "    faceColour = vec4(r, g, b, 1.0 - a);\n"
    "    vertexNormal = (transformationMatrix * vec4(vec3(normal), 1.0)).xyz;\n"
    "    toLightVector = lightPosition - worldPosition.xyz;\n"
    "}\n";

static const char* ENTITY_FS =
    "#version 330 core\n"
    "in vec4 faceColour;\n"
    "in vec3 vertexNormal;\n"
    "in vec3 toLightVector;\n"
    "out vec4 outColour;\n"
    "uniform vec3 lightColour;\n"
    "uniform float useShading;\n"
    "void main() {\n"
    "    if (useShading == 1.0) {\n"
    "        vec3 unitNormal = normalize(vertexNormal);\n"
    "        vec3 unitLightVector = normalize(toLightVector);\n"
    "        float product = dot(unitNormal, unitLightVector);\n"
    "        float brightness = max(product, 0.65);\n"
    "        vec3 diffuse = brightness * lightColour;\n"
    "        outColour = vec4(diffuse, 1.0) * faceColour;\n"
    "    } else {\n"
    "        outColour = faceColour;\n"
    "    }\n"
    "}\n";

static const char* LINE_VS =
    "#version 330 core\n"
    "layout (location = 0) in vec3 position;\n"
    "out vec4 passColour;\n"
    "uniform float isGrid;\n"
    "uniform mat4 transformationMatrix;\n"
    "uniform mat4 projectionMatrix;\n"
    "uniform mat4 viewMatrix;\n"
    "void main(void) {\n"
    "    vec4 worldPosition = transformationMatrix * vec4(position, 1.0);\n"
    "    gl_Position = projectionMatrix * viewMatrix * worldPosition;\n"
    "    if (isGrid == 1.0) {\n"
    "        if (position.x == 0.0) { passColour = vec4(14.0/255.0, 44.0/255.0, 220.0/255.0, 1.0); "
    "}\n"
    "        else if (position.z == 0.0) { passColour = vec4(120.0/255.0, 10.0/255.0, 10.0/255.0, "
    "1.0); }\n"
    "        else { passColour = vec4(81.0/255.0, 81.0/255.0, 81.0/255.0, 1.0); }\n"
    "    } else {\n"
    "        passColour = vec4(221.0/255.0, 215.0/255.0, 0.0, 1.0);\n"
    "    }\n"
    "}\n";

static const char* SOLID_FS =
    "#version 330 core\n"
    "in vec4 passColour;\n"
    "out vec4 outColour;\n"
    "void main() { outColour = passColour; }\n";

static const char* NODE_VS =
    "#version 330 core\n"
    "layout (location = 0) in vec2 position;\n"
    "uniform mat4 projectionMatrix;\n"
    "uniform mat4 modelViewMatrix;\n"
    "void main(void) {\n"
    "    gl_Position = projectionMatrix * modelViewMatrix * vec4(-position.x, position.y, 0.0, "
    "1.0);\n"
    "}\n";

static const char* NODE_FS =
    "#version 330 core\n"
    "out vec4 outColour;\n"
    "uniform float isHighlighted;\n"
    "uniform float isRoot;\n"
    "void main(void) {\n"
    "    if (isHighlighted == 1.0) { outColour = vec4(221.0/255.0, 0.0, 215.0/255.0, 1.0); }\n"
    "    else if (isRoot == 1.0) { outColour = vec4(1.0, 1.0, 1.0, 1.0); }\n"
    "    else { outColour = vec4(221.0/255.0, 215.0/255.0, 0.0, 1.0); }\n"
    "}\n";

static const char* GIZMO_VS =
    "#version 330 core\n"
    "layout (location = 0) in vec3 position;\n"
    "out vec4 passColour;\n"
    "uniform mat4 transformationMatrix;\n"
    "uniform mat4 projectionMatrix;\n"
    "uniform mat4 viewMatrix;\n"
    "uniform vec4 colour;\n"
    "void main(void) {\n"
    "    vec4 worldPosition = transformationMatrix * vec4(position, 1.0);\n"
    "    gl_Position = projectionMatrix * viewMatrix * worldPosition;\n"
    "    passColour = colour;\n"
    "}\n";

/* The UI layer: screen-space quads, optionally sampling the font atlas. */
static const char* GUI_VS =
    "#version 330 core\n"
    "layout (location = 0) in vec2 position;\n"
    "layout (location = 1) in vec2 uv;\n"
    "layout (location = 2) in vec4 colour;\n"
    "out vec2 passUv;\n"
    "out vec4 passColour;\n"
    "uniform vec2 screen;\n"
    "void main(void) {\n"
    "    passUv = uv;\n"
    "    passColour = colour;\n"
    "    gl_Position = vec4(position.x / screen.x * 2.0 - 1.0, 1.0 - position.y / screen.y * 2.0, "
    "0.0, 1.0);\n"
    "}\n";

static const char* GUI_FS =
    "#version 330 core\n"
    "in vec2 passUv;\n"
    "in vec4 passColour;\n"
    "out vec4 outColour;\n"
    "uniform sampler2D atlas;\n"
    "void main(void) {\n"
    "    if (passUv.x < 0.0) { outColour = passColour; }\n"
    "    else {\n"
    "        float a = texture(atlas, passUv).r;\n"
    "        if (a < 0.5) discard;\n"
    "        outColour = passColour;\n"
    "    }\n"
    "}\n";

/* ---- shader helpers --------------------------------------------------------- */

static PG_GLuint
compile(const char* source, PG_GLenum type)
{
    PG_GLuint id = pg_glCreateShader(type);
    PG_GLint status = 0;
    pg_glShaderSource(id, 1, &source, NULL);
    pg_glCompileShader(id);
    pg_glGetShaderiv(id, GL_COMPILE_STATUS, &status);
    if( status == GL_FALSE )
    {
        char log[2048];
        PG_GLsizei written = 0;
        pg_glGetShaderInfoLog(id, (PG_GLsizei)sizeof(log) - 1, &written, log);
        log[written < (PG_GLsizei)sizeof(log) ? written : (PG_GLsizei)sizeof(log) - 1] = '\0';
        fprintf(stderr, "poser-gl: shader compile failed\n%s\n", log);
        pg_glDeleteShader(id);
        return 0;
    }
    return id;
}

int
pg_shader_init(
    struct PG_Shader* shader,
    const char* vertex_source,
    const char* fragment_source,
    const char* const* attributes,
    int attribute_count)
{
    PG_GLint status = 0;

    memset(shader, 0, sizeof(*shader));
    shader->vertex = compile(vertex_source, GL_VERTEX_SHADER);
    shader->fragment = compile(fragment_source, GL_FRAGMENT_SHADER);
    if( !shader->vertex || !shader->fragment )
        return -1;

    shader->program = pg_glCreateProgram();
    pg_glAttachShader(shader->program, shader->vertex);
    pg_glAttachShader(shader->program, shader->fragment);
    for( int i = 0; i < attribute_count; i++ )
        pg_glBindAttribLocation(shader->program, (PG_GLuint)i, attributes[i]);
    pg_glLinkProgram(shader->program);
    pg_glGetProgramiv(shader->program, GL_LINK_STATUS, &status);
    if( status == GL_FALSE )
    {
        char log[2048];
        PG_GLsizei written = 0;
        pg_glGetProgramInfoLog(shader->program, (PG_GLsizei)sizeof(log) - 1, &written, log);
        log[written < (PG_GLsizei)sizeof(log) ? written : (PG_GLsizei)sizeof(log) - 1] = '\0';
        fprintf(stderr, "poser-gl: shader link failed\n%s\n", log);
        return -1;
    }
    return 0;
}

void
pg_shader_free(struct PG_Shader* shader)
{
    if( !shader || !shader->program )
        return;
    pg_glUseProgram(0);
    pg_glDetachShader(shader->program, shader->vertex);
    pg_glDetachShader(shader->program, shader->fragment);
    pg_glDeleteShader(shader->vertex);
    pg_glDeleteShader(shader->fragment);
    pg_glDeleteProgram(shader->program);
    memset(shader, 0, sizeof(*shader));
}

static void
set_matrix(PG_GLuint program, const char* name, struct PG_Mat4 m)
{
    pg_glUniformMatrix4fv(pg_glGetUniformLocation(program, name), 1, GL_FALSE, m.m);
}

static void
set_float(PG_GLuint program, const char* name, float v)
{
    pg_glUniform1f(pg_glGetUniformLocation(program, name), v);
}

/* ---- matrices ---------------------------------------------------------------- */

struct PG_Mat4
pg_create_projection_matrix(int width, int height)
{
    /*
     * Ported verbatim, aspect quirk included: yScale multiplies by the aspect
     * ratio where the textbook form divides. The editor's framing, zoom limits
     * and node scale were all tuned against this, so "fixing" it changes what
     * every animation looks like.
     */
    struct PG_Mat4 m;
    float aspect = height != 0 ? (float)width / (float)height : 1.0f;
    float y_scale = (float)((1.0 / tan(pg_radians(PG_FOV / 2.0f))) * aspect);
    float x_scale = y_scale / aspect;
    float frustum = PG_FAR_PLANE - PG_NEAR_PLANE;

    memset(m.m, 0, sizeof(m.m));
    m.m[0] = x_scale;
    m.m[5] = y_scale;
    m.m[10] = -((PG_FAR_PLANE + PG_NEAR_PLANE) / frustum);
    m.m[11] = -1.0f;
    m.m[14] = -((2.0f * PG_NEAR_PLANE * PG_FAR_PLANE) / frustum);
    m.m[15] = 0.0f;
    return m;
}

struct PG_Mat4
pg_create_transformation_matrix(struct PG_Vec3 translation, struct PG_Vec3 rotation, float scale)
{
    struct PG_Mat4 m = pg_mat4_identity();
    pg_mat4_translate(&m, translation);
    pg_mat4_rotate_xyz(
        &m, pg_radians(rotation.x), pg_radians(rotation.y), pg_radians(rotation.z));
    pg_mat4_scale(&m, scale);
    return m;
}

/* ---- vertex buffers ------------------------------------------------------------ */

void
pg_gl_model_free(struct PG_GlModel* model)
{
    if( !model )
        return;
    if( model->vbo_count > 0 )
        pg_glDeleteBuffers(model->vbo_count, model->vbo);
    if( model->vao )
        pg_glDeleteVertexArrays(1, &model->vao);
    memset(model, 0, sizeof(*model));
}

void
pg_gl_model_from_floats(struct PG_GlModel* out, const float* data, int count, int size)
{
    pg_gl_model_free(out);
    pg_glGenVertexArrays(1, &out->vao);
    pg_glBindVertexArray(out->vao);
    pg_glGenBuffers(1, &out->vbo[0]);
    out->vbo_count = 1;
    pg_glBindBuffer(GL_ARRAY_BUFFER, out->vbo[0]);
    pg_glBufferData(
        GL_ARRAY_BUFFER, (PG_GLsizeiptr)((size_t)count * sizeof(float)), data, GL_STATIC_DRAW);
    pg_glVertexAttribPointer(0, size, GL_FLOAT, GL_FALSE, 0, NULL);
    pg_glEnableVertexAttribArray(0);
    pg_glBindBuffer(GL_ARRAY_BUFFER, 0);
    pg_glBindVertexArray(0);
    out->vertex_count = count / size;
}

/* ---- normals ------------------------------------------------------------------- */

struct PosKey
{
    int x, y, z;
    int group;
};

static int
cmp_poskey(const void* a, const void* b)
{
    const struct PosKey* p = a;
    const struct PosKey* q = b;
    if( p->x != q->x )
        return p->x - q->x;
    if( p->y != q->y )
        return p->y - q->y;
    return p->z - q->z;
}

struct PG_NormalAdjacency*
pg_normals_build(const struct PG_ModelDef* def)
{
    struct PG_NormalAdjacency* adj = calloc(1, sizeof(*adj));
    struct PosKey* keys;
    int* counts;
    int groups = 0;

    if( !adj )
        return NULL;
    adj->vertex_count = def->vertex_count;
    if( def->vertex_count <= 0 )
        return adj;

    adj->vertex_group = malloc((size_t)def->vertex_count * sizeof(int));
    keys = malloc((size_t)def->vertex_count * sizeof(*keys));
    if( !adj->vertex_group || !keys )
    {
        free(keys);
        pg_normals_free(adj);
        return NULL;
    }
    for( int i = 0; i < def->vertex_count; i++ )
    {
        keys[i].x = def->vertices_x[i];
        keys[i].y = def->vertices_y[i];
        keys[i].z = def->vertices_z[i];
        keys[i].group = i; /* carries the vertex index through the sort */
    }
    qsort(keys, (size_t)def->vertex_count, sizeof(*keys), cmp_poskey);
    for( int i = 0; i < def->vertex_count; i++ )
    {
        if( i > 0 && cmp_poskey(&keys[i], &keys[i - 1]) != 0 )
            groups++;
        adj->vertex_group[keys[i].group] = groups;
    }
    groups++;
    adj->group_count = groups;
    free(keys);

    counts = calloc((size_t)groups, sizeof(int));
    adj->group_faces = calloc((size_t)groups, sizeof(int*));
    adj->group_face_counts = calloc((size_t)groups, sizeof(int));
    if( !counts || !adj->group_faces || !adj->group_face_counts )
    {
        free(counts);
        pg_normals_free(adj);
        return NULL;
    }

    /* Once per matching corner, not once per face: the reference accumulates a
     * face normal for every corner whose position matches, and a face with two
     * corners on the same position therefore contributes twice. */
    for( int f = 0; f < def->face_count; f++ )
    {
        counts[adj->vertex_group[def->face_a[f]]]++;
        counts[adj->vertex_group[def->face_b[f]]]++;
        counts[adj->vertex_group[def->face_c[f]]]++;
    }
    for( int g = 0; g < groups; g++ )
    {
        adj->group_faces[g] = counts[g] > 0 ? malloc((size_t)counts[g] * sizeof(int)) : NULL;
        adj->group_face_counts[g] = 0;
    }
    for( int f = 0; f < def->face_count; f++ )
    {
        const int corners[3] = { def->face_a[f], def->face_b[f], def->face_c[f] };
        for( int k = 0; k < 3; k++ )
        {
            int g = adj->vertex_group[corners[k]];
            if( adj->group_faces[g] )
                adj->group_faces[g][adj->group_face_counts[g]++] = f;
        }
    }
    free(counts);
    return adj;
}

void
pg_normals_free(struct PG_NormalAdjacency* adj)
{
    if( !adj )
        return;
    if( adj->group_faces )
    {
        for( int i = 0; i < adj->group_count; i++ )
            free(adj->group_faces[i]);
        free(adj->group_faces);
    }
    free(adj->group_face_counts);
    free(adj->vertex_group);
    free(adj);
}

static void
face_normal(const struct PG_ModelDef* def, int face, float out[3])
{
    float p1[3] = { (float)def->vertices_x[def->face_a[face]],
                    (float)def->vertices_y[def->face_a[face]],
                    (float)def->vertices_z[def->face_a[face]] };
    float p2[3] = { (float)def->vertices_x[def->face_b[face]],
                    (float)def->vertices_y[def->face_b[face]],
                    (float)def->vertices_z[def->face_b[face]] };
    float p3[3] = { (float)def->vertices_x[def->face_c[face]],
                    (float)def->vertices_y[def->face_c[face]],
                    (float)def->vertices_z[def->face_c[face]] };
    float u[3] = { p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2] };
    float v[3] = { p3[0] - p1[0], p3[1] - p1[1], p3[2] - p1[2] };
    /* Left unnormalised; the fragment shader normalises. */
    out[0] = u[1] * v[2] - u[2] * v[1];
    out[1] = u[2] * v[0] - u[0] * v[2];
    out[2] = u[0] * v[1] - u[1] * v[0];
}

void
pg_upload_entity_model(
    struct PG_GlModel* out,
    const struct PG_ModelDef* def,
    const struct PG_NormalAdjacency* adjacency,
    bool flat_shading)
{
    int* positions;
    int* normals;
    float* group_normals = NULL;
    int vertex_index = 0;
    int normal_index = 0;
    /* Reuse the buffers when the topology is unchanged. This runs every frame an
     * animation plays — the vertices move but the face list does not — and
     * recreating a VAO and two VBOs sixty times a second is churn the driver
     * charges for. */
    bool reuse = out->vao != 0 && out->vbo_count == 2 &&
                 out->vertex_count == def->face_count * 3;

    if( !reuse )
        pg_gl_model_free(out);
    if( !def || def->face_count <= 0 )
    {
        pg_gl_model_free(out);
        return;
    }

    positions = malloc((size_t)def->face_count * 3 * 4 * sizeof(int));
    normals = malloc((size_t)def->face_count * 3 * 3 * sizeof(int));
    if( !positions || !normals )
    {
        free(positions);
        free(normals);
        return;
    }

    if( !flat_shading && adjacency && adjacency->group_count > 0 )
    {
        group_normals = calloc((size_t)adjacency->group_count * 3, sizeof(float));
        if( group_normals )
        {
            for( int g = 0; g < adjacency->group_count; g++ )
            {
                for( int i = 0; i < adjacency->group_face_counts[g]; i++ )
                {
                    float n[3];
                    face_normal(def, adjacency->group_faces[g][i], n);
                    group_normals[g * 3 + 0] += n[0];
                    group_normals[g * 3 + 1] += n[1];
                    group_normals[g * 3 + 2] += n[2];
                }
            }
        }
    }

    for( int f = 0; f < def->face_count; f++ )
    {
        int alpha = 0;
        int colour;
        int points[3];
        float flat[3];

        if( def->face_alphas )
        {
            /* The reference sign-extends the alpha byte then masks 16 bits, so a
             * byte of 0xFF becomes 0xFFFF and reads back as fully transparent. */
            alpha = ((int)(signed char)def->face_alphas[f] & 0xFFFF) << 16;
        }
        colour = def->face_colors ? (int)def->face_colors[f] & 0xFFFF : 0;
        points[0] = def->face_a[f];
        points[1] = def->face_b[f];
        points[2] = def->face_c[f];

        if( flat_shading || !group_normals )
            face_normal(def, f, flat);

        for( int k = 0; k < 3; k++ )
        {
            int point = points[k];
            positions[vertex_index++] = def->vertices_x[point];
            positions[vertex_index++] = def->vertices_y[point];
            positions[vertex_index++] = def->vertices_z[point];
            positions[vertex_index++] = alpha | colour;

            if( flat_shading || !group_normals )
            {
                normals[normal_index++] = (int)flat[0];
                normals[normal_index++] = (int)flat[1];
                normals[normal_index++] = (int)flat[2];
            }
            else
            {
                int g = adjacency->vertex_group[point];
                normals[normal_index++] = (int)group_normals[g * 3 + 0];
                normals[normal_index++] = (int)group_normals[g * 3 + 1];
                normals[normal_index++] = (int)group_normals[g * 3 + 2];
            }
        }
    }

    if( !reuse )
    {
        pg_glGenVertexArrays(1, &out->vao);
        pg_glBindVertexArray(out->vao);
        pg_glGenBuffers(2, out->vbo);
        out->vbo_count = 2;
    }
    else
    {
        pg_glBindVertexArray(out->vao);
    }

    pg_glBindBuffer(GL_ARRAY_BUFFER, out->vbo[0]);
    pg_glBufferData(
        GL_ARRAY_BUFFER, (PG_GLsizeiptr)((size_t)vertex_index * sizeof(int)), positions,
        GL_STATIC_DRAW);
    pg_glVertexAttribIPointer(0, 4, GL_INT, 0, NULL);
    pg_glEnableVertexAttribArray(0);

    pg_glBindBuffer(GL_ARRAY_BUFFER, out->vbo[1]);
    pg_glBufferData(
        GL_ARRAY_BUFFER, (PG_GLsizeiptr)((size_t)normal_index * sizeof(int)), normals,
        GL_STATIC_DRAW);
    pg_glVertexAttribIPointer(1, 3, GL_INT, 0, NULL);
    pg_glEnableVertexAttribArray(1);

    pg_glBindBuffer(GL_ARRAY_BUFFER, 0);
    pg_glBindVertexArray(0);
    out->vertex_count = vertex_index / 4;

    free(positions);
    free(normals);
    free(group_normals);
}

/* ---- gizmo geometry ------------------------------------------------------------- */

/*
 * poser-gl loads three .obj files from its resources. Generating the same shapes
 * keeps this a single binary with no asset path to resolve, and the shapes are
 * simple enough to state exactly: an arrow along +X of unit length, and a ring of
 * unit radius in the XZ plane. Both dimensions are load-bearing — the pick
 * volumes and the rotation gizmo's circle projection are expressed in multiples
 * of the gizmo scale and assume a unit model.
 */
static void
build_arrow(struct PG_GlModel* out, bool cube_head)
{
    const int segments = 12;
    const float shaft_radius = 0.012f;
    const float head_radius = 0.05f;
    const float head_start = 0.75f;
    float* v;
    int n = 0;
    int capacity = segments * 6 * 3 * 3 + 1024;

    v = malloc((size_t)capacity * sizeof(float));
    if( !v )
        return;

#define PUSH(px, py, pz)                                                                          \
    do                                                                                            \
    {                                                                                             \
        v[n++] = (px);                                                                            \
        v[n++] = (py);                                                                            \
        v[n++] = (pz);                                                                            \
    } while( 0 )

    for( int i = 0; i < segments; i++ )
    {
        float a0 = (float)(2.0 * M_PI * i / segments);
        float a1 = (float)(2.0 * M_PI * (i + 1) / segments);
        float y0 = cosf(a0) * shaft_radius, z0 = sinf(a0) * shaft_radius;
        float y1 = cosf(a1) * shaft_radius, z1 = sinf(a1) * shaft_radius;

        PUSH(0.0f, y0, z0);
        PUSH(head_start, y0, z0);
        PUSH(head_start, y1, z1);
        PUSH(0.0f, y0, z0);
        PUSH(head_start, y1, z1);
        PUSH(0.0f, y1, z1);
    }

    if( cube_head )
    {
        /* A box, which is what tells the scale gizmo apart from the translation
         * one at a glance. */
        const float h = head_radius;
        const float corners[8][3] = {
            { head_start, -h, -h }, { 1.0f, -h, -h }, { 1.0f, h, -h },   { head_start, h, -h },
            { head_start, -h, h },  { 1.0f, -h, h },  { 1.0f, h, h },    { head_start, h, h },
        };
        const int faces[12][3] = { { 0, 1, 2 }, { 0, 2, 3 }, { 4, 6, 5 }, { 4, 7, 6 },
                                   { 0, 4, 5 }, { 0, 5, 1 }, { 3, 2, 6 }, { 3, 6, 7 },
                                   { 0, 3, 7 }, { 0, 7, 4 }, { 1, 5, 6 }, { 1, 6, 2 } };
        for( int f = 0; f < 12; f++ )
            for( int k = 0; k < 3; k++ )
                PUSH(
                    corners[faces[f][k]][0], corners[faces[f][k]][1], corners[faces[f][k]][2]);
    }
    else
    {
        for( int i = 0; i < segments; i++ )
        {
            float a0 = (float)(2.0 * M_PI * i / segments);
            float a1 = (float)(2.0 * M_PI * (i + 1) / segments);
            float y0 = cosf(a0) * head_radius, z0 = sinf(a0) * head_radius;
            float y1 = cosf(a1) * head_radius, z1 = sinf(a1) * head_radius;
            PUSH(head_start, y0, z0);
            PUSH(1.0f, 0.0f, 0.0f);
            PUSH(head_start, y1, z1);
            PUSH(head_start, y0, z0);
            PUSH(head_start, y1, z1);
            PUSH(head_start, 0.0f, 0.0f);
        }
    }
#undef PUSH

    pg_gl_model_from_floats(out, v, n, 3);
    free(v);
}

static void
build_torus(struct PG_GlModel* out)
{
    const int major = 48;
    const int minor = 6;
    const float ring = 1.0f;
    const float tube = 0.012f;
    float* v = malloc((size_t)major * minor * 6 * 3 * sizeof(float));
    int n = 0;

    if( !v )
        return;
    for( int i = 0; i < major; i++ )
    {
        float u0 = (float)(2.0 * M_PI * i / major);
        float u1 = (float)(2.0 * M_PI * (i + 1) / major);
        for( int j = 0; j < minor; j++ )
        {
            float w0 = (float)(2.0 * M_PI * j / minor);
            float w1 = (float)(2.0 * M_PI * (j + 1) / minor);
            float p[4][3];
            const float us[2] = { u0, u1 };
            const float ws[2] = { w0, w1 };
            int k = 0;
            for( int a = 0; a < 2; a++ )
            {
                for( int b = 0; b < 2; b++ )
                {
                    float r = ring + tube * cosf(ws[b]);
                    /* Ring in the XZ plane, normal +Y — the orientation the Y
                     * axis uses unrotated. */
                    p[k][0] = r * cosf(us[a]);
                    p[k][1] = tube * sinf(ws[b]);
                    p[k][2] = r * sinf(us[a]);
                    k++;
                }
            }
            /* p[0]=u0w0 p[1]=u0w1 p[2]=u1w0 p[3]=u1w1 */
            const int tri[6] = { 0, 2, 3, 0, 3, 1 };
            for( int t = 0; t < 6; t++ )
            {
                v[n++] = p[tri[t]][0];
                v[n++] = p[tri[t]][1];
                v[n++] = p[tri[t]][2];
            }
        }
    }
    pg_gl_model_from_floats(out, v, n, 3);
    free(v);
}

/* ---- renderer --------------------------------------------------------------------- */

int
pg_renderer_init(struct PG_Renderer* r)
{
    static const char* entity_attrs[] = { "position", "normal" };
    static const char* one_attr[] = { "position" };
    static const char* gui_attrs[] = { "position", "uv", "colour" };
    static const float quad[] = { -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, -0.5f };

    memset(r, 0, sizeof(*r));
    if( pg_shader_init(&r->entity_shader, ENTITY_VS, ENTITY_FS, entity_attrs, 2) )
        return -1;
    if( pg_shader_init(&r->line_shader, LINE_VS, SOLID_FS, one_attr, 1) )
        return -1;
    if( pg_shader_init(&r->node_shader, NODE_VS, NODE_FS, one_attr, 1) )
        return -1;
    if( pg_shader_init(&r->gizmo_shader, GIZMO_VS, SOLID_FS, one_attr, 1) )
        return -1;
    if( pg_shader_init(&r->gui_shader, GUI_VS, GUI_FS, gui_attrs, 3) )
        return -1;

    pg_gl_model_from_floats(&r->quad, quad, 8, 2);
    build_arrow(&r->translation_gizmo, false);
    build_arrow(&r->scale_gizmo, true);
    build_torus(&r->rotation_gizmo);
    pg_renderer_set_grid(r, 1);

    pg_glUseProgram(r->entity_shader.program);
    pg_glUniform3f(
        pg_glGetUniformLocation(r->entity_shader.program, "lightPosition"), 0.0f, -500.0f,
        -1000.0f);
    pg_glUniform3f(
        pg_glGetUniformLocation(r->entity_shader.program, "lightColour"), 1.0f, 1.0f, 1.0f);
    pg_glUseProgram(0);
    return 0;
}

void
pg_renderer_free(struct PG_Renderer* r)
{
    pg_gl_model_free(&r->grid);
    pg_gl_model_free(&r->quad);
    pg_gl_model_free(&r->line);
    pg_gl_model_free(&r->translation_gizmo);
    pg_gl_model_free(&r->scale_gizmo);
    pg_gl_model_free(&r->rotation_gizmo);
    pg_shader_free(&r->entity_shader);
    pg_shader_free(&r->line_shader);
    pg_shader_free(&r->node_shader);
    pg_shader_free(&r->gizmo_shader);
    pg_shader_free(&r->gui_shader);
}

void
pg_renderer_set_grid(struct PG_Renderer* r, int entity_size)
{
    float base = entity_size > 1 ? 10.0f : 9.0f;
    float dimension = ceilf((base + (float)entity_size) / 2.0f) * 2.0f; /* round up to even */
    float offset = dimension / 4.0f;
    int steps = (int)lrintf(dimension * 2.0f);
    float* v = malloc((size_t)(steps / 2 + 2) * 12 * sizeof(float));
    int n = 0;

    if( !v )
        return;
    for( int i = 0; i <= steps; i += 2 )
    {
        float x = ((float)i - dimension) / 4.0f;
        v[n++] = x;
        v[n++] = 0.0f;
        v[n++] = offset;
        v[n++] = x;
        v[n++] = 0.0f;
        v[n++] = -offset;
        v[n++] = offset;
        v[n++] = 0.0f;
        v[n++] = x;
        v[n++] = -offset;
        v[n++] = 0.0f;
        v[n++] = x;
    }
    pg_gl_model_from_floats(&r->grid, v, n, 3);
    free(v);
}

void
pg_render_entity(
    struct PG_Renderer* r,
    const struct PG_GlModel* model,
    struct PG_Mat4 view,
    struct PG_Vec3 position,
    struct PG_Vec3 rotation,
    float scale,
    int shading_type)
{
    if( !model || !model->vao || model->vertex_count <= 0 )
        return;
    pg_glUseProgram(r->entity_shader.program);
    pg_glBindVertexArray(model->vao);
    set_float(r->entity_shader.program, "useShading", shading_type != PG_SHADING_NONE ? 1.0f : 0.0f);
    set_matrix(r->entity_shader.program, "viewMatrix", view);
    set_matrix(
        r->entity_shader.program,
        "transformationMatrix",
        pg_create_transformation_matrix(position, rotation, scale));
    set_matrix(r->entity_shader.program, "projectionMatrix", r->projection);
    pg_glDrawArrays(GL_TRIANGLES, 0, model->vertex_count);
    pg_glBindVertexArray(0);
    pg_glUseProgram(0);
}

static void
line_prepare(struct PG_Renderer* r, const struct PG_GlModel* model, bool is_grid, float scale)
{
    pg_glUseProgram(r->line_shader.program);
    set_float(r->line_shader.program, "isGrid", is_grid ? 1.0f : 0.0f);
    pg_glBindVertexArray(model->vao);
    pg_glEnable(GL_LINE_SMOOTH);
    pg_glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    set_matrix(
        r->line_shader.program,
        "transformationMatrix",
        pg_create_transformation_matrix(pg_vec3(0, 0, 0), pg_vec3(0, 0, 0), scale));
    set_matrix(r->line_shader.program, "projectionMatrix", r->projection);
}

void
pg_render_grid(struct PG_Renderer* r, struct PG_Mat4 view, float scale)
{
    if( !r->grid.vao )
        return;
    line_prepare(r, &r->grid, true, scale);
    set_matrix(r->line_shader.program, "viewMatrix", view);
    pg_glDrawArrays(GL_LINES, 0, r->grid.vertex_count);
    pg_glBindVertexArray(0);
    pg_glUseProgram(0);
}

void
pg_render_bone(
    struct PG_Renderer* r,
    struct PG_Mat4 view,
    struct PG_Vec3 from,
    struct PG_Vec3 to,
    float scale)
{
    float v[6] = { from.x, from.y, from.z, to.x, to.y, to.z };
    pg_gl_model_from_floats(&r->line, v, 6, 3);
    line_prepare(r, &r->line, false, scale);
    set_matrix(r->line_shader.program, "viewMatrix", view);
    pg_glDisable(GL_DEPTH_TEST);
    pg_glDrawArrays(GL_LINES, 0, r->line.vertex_count);
    pg_glEnable(GL_DEPTH_TEST);
    pg_glBindVertexArray(0);
    pg_glUseProgram(0);
}

void
pg_render_node(
    struct PG_Renderer* r,
    struct PG_Mat4 view,
    struct PG_Vec3 position,
    float scale,
    bool highlighted,
    bool is_root)
{
    struct PG_Mat4 model = pg_mat4_identity();

    pg_glUseProgram(r->node_shader.program);
    pg_glBindVertexArray(r->quad.vao);
    pg_glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    pg_glDisable(GL_DEPTH_TEST);

    pg_mat4_translate(&model, position);
    /* Billboard: the model's upper 3x3 becomes the transpose of the view's, so
     * the quad always faces the camera whatever the orbit is doing. */
    model.m[0] = view.m[0];
    model.m[1] = view.m[4];
    model.m[2] = view.m[8];
    model.m[4] = view.m[1];
    model.m[5] = view.m[5];
    model.m[6] = view.m[9];
    model.m[8] = view.m[2];
    model.m[9] = view.m[6];
    model.m[10] = view.m[10];
    pg_mat4_scale(&model, scale);

    set_float(r->node_shader.program, "isHighlighted", highlighted ? 1.0f : 0.0f);
    set_float(r->node_shader.program, "isRoot", is_root ? 1.0f : 0.0f);
    set_matrix(r->node_shader.program, "modelViewMatrix", pg_mat4_mul(view, model));
    set_matrix(r->node_shader.program, "projectionMatrix", r->projection);
    pg_glDrawArrays(GL_TRIANGLE_STRIP, 0, r->quad.vertex_count);

    pg_glEnable(GL_DEPTH_TEST);
    pg_glBindVertexArray(0);
    pg_glUseProgram(0);
}

/* ---- camera ------------------------------------------------------------------------- */

void
pg_camera_init(struct PG_Camera* camera)
{
    memset(camera, 0, sizeof(*camera));
    camera->pitch = -23.0f;
    camera->distance = 500.0f;
}

void
pg_camera_scroll(struct PG_Camera* camera, float delta)
{
    camera->zooming = true;
    camera->wheel = delta;
}

void
pg_camera_tick(
    struct PG_Camera* camera,
    float zoom_multiplier,
    float camera_multiplier,
    struct PG_Vec2 orbit_delta,
    bool orbiting)
{
    float h, v;
    double theta;

    if( camera->zooming )
    {
        float level = camera->wheel * zoom_multiplier;
        camera->distance = pg_clampf(camera->distance - level, PG_MIN_ZOOM, PG_MAX_ZOOM);
        camera->zooming = false;
    }
    if( orbiting )
    {
        camera->pitch -= orbit_delta.y * camera_multiplier;
        camera->angle -= orbit_delta.x * camera_multiplier;
    }

    h = camera->distance * (float)cos(pg_radians(camera->pitch));
    v = camera->distance * (float)sin(pg_radians(camera->pitch));
    theta = pg_radians(camera->angle);

    /* The entity sits at the origin with no rotation, so the orbit is entirely
     * the camera's own angle. */
    camera->position = pg_vec3_sub(
        camera->centre,
        pg_vec3(h * (float)sin(theta), -v + 60.0f, h * (float)cos(theta)));
    camera->yaw = 180.0f - camera->angle;
}

void
pg_camera_pan(
    struct PG_Camera* camera,
    struct PG_Mat4 view,
    struct PG_Vec2 delta,
    float camera_multiplier)
{
    /* The view matrix's first two *rows* — its right and up axes expressed in
     * world space, which is what a screen-space drag has to move along. Reading
     * the columns instead pans along the world axes and feels sheared. */
    struct PG_Vec3 right = pg_vec3_mul(pg_vec3(view.m[0], view.m[4], view.m[8]), delta.x);
    struct PG_Vec3 up = pg_vec3_mul(pg_vec3(view.m[1], view.m[5], view.m[9]), delta.y);
    struct PG_Vec3 move = pg_vec3_mul(pg_vec3_add(right, up), camera_multiplier * 2.0f);
    camera->centre = pg_vec3_sub(camera->centre, move);
}

struct PG_Mat4
pg_camera_view_matrix(const struct PG_Camera* camera)
{
    struct PG_Mat4 m = pg_mat4_identity();
    pg_mat4_rotate_xyz(
        &m, pg_radians(camera->pitch), pg_radians(camera->yaw), pg_radians(camera->roll));
    pg_mat4_translate(&m, pg_vec3(-camera->position.x, -camera->position.y, -camera->position.z));
    return m;
}
