/*
 * TORIRS_SCENE_EXPORT=<path>: write the loaded scene's geometry for the web
 * device bench (src/web/devicebench.html), which draws a real scene -- the
 * cache's models as this client lit them -- in plain JavaScript to time a
 * software frame on a browser that cannot run the client (no WebAssembly).
 *
 * Every live element with a full model, as the software renderer reads it:
 * placement, vertices, faces, the lit HSL16 colours (HIDDEN / FLAT sentinels
 * kept), textures, alphas. Then the HSL16 -> RGB palette the raster looks
 * colours up in. Little-endian:
 *
 *   "TSCN" u32 version(1) i32 base_tile_x i32 base_tile_z u32 element_count
 *   per element:
 *     i32 x y z yaw   u32 vertex_count face_count
 *     i16 x[vc] y[vc] z[vc]
 *     u16 a[fc] b[fc] c[fc]          (vertex indices)
 *     u16 col_a[fc] col_b[fc] col_c[fc]
 *     i16 texture[fc]                (-1: none)
 *     u8  alpha[fc]                  (0 opaque)
 *     u16 p[fc] m[fc] n[fc]          (a textured face's mapping triangle, as
 *                                     vertex indices: textured_p/m/n through
 *                                     face_texture_coords, else the face's own
 *                                     corners; 0 for an untextured face)
 *   u32 palette[65536]               (0x00RRGGBB)
 *   u32 texture_count, then per id:
 *     u16 average_hsl16  u16 size (0: not loaded)  u8 opaque
 *     u32 texels[size * size]        (0x00RRGGBB)
 *
 * A textured face's own colours are its light alone; the reference fills a
 * GROUND tile's texture with the average (Scene.method4560, under
 * Scene.lowDetailTextures) and maps a model's always.
 *
 * A debug tool, not a game path: run once offline (TORIRS_WORLD_MAP), and the
 * frame loop calls this after the load has settled.
 */
#include "app/app_internal.h"

#include "graphics/shared_tables.h"
#include "toridraw_model.h"
#include "toridraw_scene.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void
export_u32(
    FILE* f,
    uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    fwrite(b, 1, 4, f);
}

static void
export_u16(
    FILE* f,
    uint16_t v)
{
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    fwrite(b, 1, 2, f);
}

int
App_SceneExport(
    struct App* app,
    char const* path)
{
    struct ToriDraw_Scene* scene;
    FILE* f;
    int slots;
    int written = 0;
    long count_at;

    assert(app);
    assert(path);
    scene = app->scene;
    assert(scene);
    f = fopen(path, "wb");
    if( !f )
    {
        TORIRS_ERR("scene export: cannot write %s\n", path);
        return 0;
    }
    fwrite("TSCN", 1, 4, f);
    export_u32(f, 3);
    export_u32(f, (uint32_t)(app->world ? app->world->_base_tile_x : 0));
    export_u32(f, (uint32_t)(app->world ? app->world->_base_tile_z : 0));
    count_at = ftell(f);
    export_u32(f, 0);

    slots = ToriDraw_SceneElementSlotCount(scene);
    for( int id = 0; id < slots; id++ ) /* tree-walk-exempt: offline debug export */
    {
        struct ToriDraw_SceneElement* el;
        const struct ToriDraw_Model* m;

        if( !ToriDraw_SceneElementIsLive(scene, id) )
            continue;
        el = ToriDraw_SceneElementGet(scene, id);
        if( !el || !ToriDraw_ModelKindIsFull(el->model.kind) )
            continue;
        m = ToriDraw_ModelRead(el->model);
        if( !m || m->vertex_count <= 0 || m->face_count <= 0 || !m->face_colors_a ||
            !m->face_colors_b || !m->face_colors_c || m->vertex_count > 65535 )
            continue;
        export_u32(f, (uint32_t)el->world_position.x);
        export_u32(f, (uint32_t)el->world_position.y);
        export_u32(f, (uint32_t)el->world_position.z);
        export_u32(f, (uint32_t)el->world_position.yaw);
        export_u32(f, (uint32_t)m->vertex_count);
        export_u32(f, (uint32_t)m->face_count);
        for( int i = 0; i < m->vertex_count; i++ )
            export_u16(f, (uint16_t)m->vertices_x[i]);
        for( int i = 0; i < m->vertex_count; i++ )
            export_u16(f, (uint16_t)m->vertices_y[i]);
        for( int i = 0; i < m->vertex_count; i++ )
            export_u16(f, (uint16_t)m->vertices_z[i]);
        for( int i = 0; i < m->face_count; i++ )
            export_u16(f, (uint16_t)m->face_indices_a[i]);
        for( int i = 0; i < m->face_count; i++ )
            export_u16(f, (uint16_t)m->face_indices_b[i]);
        for( int i = 0; i < m->face_count; i++ )
            export_u16(f, (uint16_t)m->face_indices_c[i]);
        for( int i = 0; i < m->face_count; i++ )
            export_u16(f, m->face_colors_a[i]);
        for( int i = 0; i < m->face_count; i++ )
            export_u16(f, m->face_colors_b[i]);
        for( int i = 0; i < m->face_count; i++ )
            export_u16(f, m->face_colors_c[i]);
        for( int i = 0; i < m->face_count; i++ )
            export_u16(f, (uint16_t)(m->face_textures ? m->face_textures[i] : -1));
        for( int i = 0; i < m->face_count; i++ )
        {
            uint8_t a = m->face_alphas ? m->face_alphas[i] : 0;
            fwrite(&a, 1, 1, f);
        }
        for( int corner = 0; corner < 3; corner++ )
        {
            for( int i = 0; i < m->face_count; i++ )
            {
                int v = 0;
                if( m->face_textures && m->face_textures[i] != -1 )
                {
                    int const coord = m->face_texture_coords ? m->face_texture_coords[i] : -1;
                    faceint_t const* own = corner == 0   ? m->face_indices_a
                                           : corner == 1 ? m->face_indices_b
                                                         : m->face_indices_c;
                    faceint_t const* mapped = corner == 0   ? m->textured_p_coordinate
                                              : corner == 1 ? m->textured_m_coordinate
                                                            : m->textured_n_coordinate;
                    v = (coord >= 0 && coord < m->textured_face_count && mapped) ? mapped[coord]
                                                                                 : own[i];
                }
                export_u16(f, (uint16_t)v);
            }
        }
        written++;
    }
    for( int i = 0; i < 65536; i++ )
        export_u32(f, (uint32_t)g_hsl16_to_pixel_table[i] & 0x00ffffffu);
    {
        struct ToriDraw_TextureMap* map = ToriDraw_SceneTextureMapOrNull(scene);
        int const count = map ? map->count : 0;

        export_u32(f, (uint32_t)count);
        for( int i = 0; i < count; i++ )
        {
            struct ToriDraw_Texture* texture = ToriDraw_TextureMapGet(map, i);
            int const size = texture && texture->texels && texture->width == texture->height
                                 ? texture->width
                                 : 0;
            uint8_t const opaque = texture && texture->opaque ? 1 : 0;

            export_u16(f, (uint16_t)(texture ? ToriDraw_TextureAverageHsl16(texture) : 0));
            export_u16(f, (uint16_t)size);
            fwrite(&opaque, 1, 1, f);
            for( int t = 0; t < size * size; t++ )
                export_u32(f, (uint32_t)texture->texels[t] & 0x00ffffffu);
        }
    }
    fseek(f, count_at, SEEK_SET);
    export_u32(f, (uint32_t)written);
    fclose(f);
    TORIRS_REPORT("scene export: %d element(s) to %s\n", written, path);
    return written;
}
