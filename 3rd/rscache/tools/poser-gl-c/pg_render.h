#ifndef POSER_GL_C_PG_RENDER_H
#define POSER_GL_C_PG_RENDER_H

/*
 * The 3D half of the editor: entity, grid, skeleton and joint markers.
 *
 * poser-gl renders the scene into an FBO and shows it as a LEGUI ImageView so
 * the retained-mode layout can place it. There is no retained-mode layout here,
 * so the scene draws straight into a viewport rectangle of the window and the
 * UI draws around it — same picture, one less texture round trip.
 */

#include "pg_anim.h"
#include "pg_gl.h"
#include "pg_math.h"
#include "pg_model.h"

enum PG_PolygonMode
{
    PG_POLYGON_FILL = 0,
    PG_POLYGON_LINE,
    PG_POLYGON_POINT
};

enum PG_ShadingType
{
    PG_SHADING_SMOOTH = 0,
    PG_SHADING_FLAT,
    PG_SHADING_NONE
};

struct PG_GlModel
{
    PG_GLuint vao;
    PG_GLuint vbo[2];
    int vbo_count;
    int vertex_count;
};

/**
 * Face adjacency by vertex *position*, built once per entity.
 *
 * The reference recomputes it inside the per-frame normal pass, comparing
 * animated positions — an O(vertices x faces) scan every frame, and one whose
 * result can change mid-animation when two coincident vertices separate. Fixing
 * the grouping at the rest pose makes the smoothing stable and the per-frame
 * cost linear in faces.
 */
struct PG_NormalAdjacency
{
    int vertex_count;
    int group_count;
    int* vertex_group; /* vertex -> position group */
    int** group_faces; /* position group -> face indices, once per matching corner */
    int* group_face_counts;
};

struct PG_NormalAdjacency*
pg_normals_build(const struct PG_ModelDef* def);
void
pg_normals_free(struct PG_NormalAdjacency* adj);

/* ---- shaders -------------------------------------------------------------- */

struct PG_Shader
{
    PG_GLuint program;
    PG_GLuint vertex;
    PG_GLuint fragment;
};

/** Compile and link. Returns 0 on success; the GL log goes to stderr. */
int
pg_shader_init(
    struct PG_Shader* shader,
    const char* vertex_source,
    const char* fragment_source,
    const char* const* attributes,
    int attribute_count);
void
pg_shader_free(struct PG_Shader* shader);

/* ---- renderer -------------------------------------------------------------- */

struct PG_Renderer
{
    struct PG_Shader entity_shader;
    struct PG_Shader line_shader;
    struct PG_Shader node_shader;
    struct PG_Shader gizmo_shader;
    struct PG_Shader gui_shader;

    struct PG_GlModel grid;
    struct PG_GlModel quad;
    struct PG_GlModel line;
    struct PG_GlModel translation_gizmo;
    struct PG_GlModel scale_gizmo;
    struct PG_GlModel rotation_gizmo;

    struct PG_Mat4 projection;
    int viewport[4];
};

int
pg_renderer_init(struct PG_Renderer* r);
void
pg_renderer_free(struct PG_Renderer* r);

/** ThinMatrix-style perspective, ported including its aspect quirk. */
struct PG_Mat4
pg_create_projection_matrix(int width, int height);
struct PG_Mat4
pg_create_transformation_matrix(struct PG_Vec3 translation, struct PG_Vec3 rotation, float scale);

/** Upload an entity model. Frees and rebuilds `out` in place. */
void
pg_upload_entity_model(
    struct PG_GlModel* out,
    const struct PG_ModelDef* def,
    const struct PG_NormalAdjacency* adjacency,
    bool flat_shading);

void
pg_gl_model_free(struct PG_GlModel* model);
/** A VAO of `size`-component float positions on attribute 0. */
void
pg_gl_model_from_floats(struct PG_GlModel* out, const float* data, int count, int size);

void
pg_renderer_set_grid(struct PG_Renderer* r, int entity_size);

void
pg_render_entity(
    struct PG_Renderer* r,
    const struct PG_GlModel* model,
    struct PG_Mat4 view,
    struct PG_Vec3 position,
    struct PG_Vec3 rotation,
    float scale,
    int shading_type);

void
pg_render_grid(struct PG_Renderer* r, struct PG_Mat4 view, float scale);

/** One yellow bone between two joint positions. */
void
pg_render_bone(
    struct PG_Renderer* r,
    struct PG_Mat4 view,
    struct PG_Vec3 from,
    struct PG_Vec3 to,
    float scale);

/** A camera-facing quad at `position`, sized in world units. */
void
pg_render_node(
    struct PG_Renderer* r,
    struct PG_Mat4 view,
    struct PG_Vec3 position,
    float scale,
    bool highlighted,
    bool is_root);

/* ---- camera ---------------------------------------------------------------- */

struct PG_Camera
{
    float pitch;
    float yaw;
    float roll;
    float distance;
    float angle;
    struct PG_Vec3 position;
    struct PG_Vec3 centre;
    bool zooming;
    float wheel;
};

void
pg_camera_init(struct PG_Camera* camera);
void
pg_camera_tick(
    struct PG_Camera* camera,
    float zoom_multiplier,
    float camera_multiplier,
    struct PG_Vec2 orbit_delta,
    bool orbiting);
void
pg_camera_scroll(struct PG_Camera* camera, float delta);
void
pg_camera_pan(
    struct PG_Camera* camera,
    struct PG_Mat4 view,
    struct PG_Vec2 delta,
    float camera_multiplier);
struct PG_Mat4
pg_camera_view_matrix(const struct PG_Camera* camera);

#endif
