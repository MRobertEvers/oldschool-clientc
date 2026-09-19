/*
 * In-scene tile markers: WHEN they leave the world walk.
 *
 * A tile marker is a wash on the ground, and the cache says for each of its
 * highlight groups whether that wash is composited over the finished scene or
 * drawn with the tile it marks -- flag bit 16, the settings row spelled
 * "- Always on top". The first is the entity-overlay list and always was; the
 * second is this, and the whole of it is an ORDER: the marker's primitives
 * are emitted immediately after the tile's own ground command, so that
 * everything the painter puts down afterwards -- the locs on the tile, the
 * npc, the player standing on it -- covers the wash.
 *
 * There is nothing to see in a screenshot of this on its own. What a picture
 * shows is the consequence: with the order wrong, a 50/255 wash of the
 * group's colour sits on the player's boots, and the current-tile group (which
 * does not set bit 16) was indistinguishable from the tile-marker group (which
 * does).
 *
 * Run: make -C src test-frame-tile-mark
 */

#include "render/torirs_frame.h"
#include "painters/painters.h"
#include "ui/uitree_emit.h"

#include "toridraw_model.h"
#include "toridraw_scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The world branch's link boundary, closed the way the sibling overlay test
 * closes it: this fixture has one scene element and no animation, no npc and
 * no loc pools. */
struct World;
struct WorldEntity_NPC;
struct WorldEntity_Scenery;
struct UITreeModelRenderCache;

static int g_ground_element = -1;

struct ToriDraw_ModelHandle
UITreeAnim_ModelForDraw(
    struct ToriDraw_Scene* scene,
    struct UITreeModelRenderCache* cache,
    int model,
    int sequence,
    int frame)
{
    (void)scene;
    (void)cache;
    (void)model;
    (void)sequence;
    (void)frame;
    /* Only 3D world commands here; a widget model draw means the fixture is
     * not the fixture this test describes. */
    abort();
}

int
World_TerrainElementAt(struct World* world, int x, int z, int level)
{
    (void)world;
    (void)x;
    (void)z;
    (void)level;
    return g_ground_element;
}

struct WorldEntity_NPC*
World_NpcGetByElementId(struct World* world, int element_id, int* out_index)
{
    (void)world;
    (void)element_id;
    (void)out_index;
    return NULL;
}

struct WorldEntity_Scenery*
World_SceneryGetByElementId(struct World* world, int element_id)
{
    (void)world;
    (void)element_id;
    return NULL;
}

static int failures;

#define CHECK(condition, message)                                                                  \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s\n", (message));                                              \
            failures++;                                                                            \
        }                                                                                          \
    } while( 0 )

static struct ToriDraw_Model*
one_triangle(void)
{
    struct ToriDraw_Model* m = ToriDraw_ModelNew(3, 1, 0);
    vertexint_t x[] = { 0, 100, 0 };
    vertexint_t y[] = { 100, 1000, -1000 };
    vertexint_t z[] = { 0, 0, 100 };
    faceint_t a[] = { 0 };
    faceint_t b[] = { 1 };
    faceint_t c[] = { 2 };
    hsl16_t colour[] = { 123 };

    if( !m )
        return NULL;
#define COPY(field, data)                                                                          \
    m->field = ToriDraw_BufCopy(data, sizeof(data) / sizeof(*data), sizeof(*data))
    COPY(vertices_x, x);
    COPY(vertices_y, y);
    COPY(vertices_z, z);
    COPY(face_indices_a, a);
    COPY(face_indices_b, b);
    COPY(face_indices_c, c);
    COPY(face_colors_a, colour);
    COPY(face_colors_b, colour);
    COPY(face_colors_c, colour);
#undef COPY
    return m;
}

int
main(void)
{
    struct ToriDraw_Scene* scene;
    struct ToriDraw_Model* model;
    /* The walk refuses a NULL world outright, and everything it would read
     * from one -- the terrain element of a tile -- is the stub above. A
     * one-byte stand-in is therefore the whole World this fixture needs, and
     * it keeps the test from pulling in the real struct to hold nothing. */
    static char world_storage[1];
    struct World* root = (struct World*)world_storage;
    struct PaintersElementCommand commands[3];
    struct PaintersBuffer painters;
    struct UITreeEmitDesc desc;
    struct ToriDraw_Camera camera;
    struct ToriRS_Frame frame;
    struct ToriRS_RenderCommand cmd;
    struct ToriRS_WorldTileMark marks[3];
    int element;
    int order[8];
    int count;

    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();
    scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    if( !scene )
    {
        fprintf(stderr, "FAIL: could not create scene\n");
        return 2;
    }
    model = one_triangle();
    if( !model )
    {
        fprintf(stderr, "FAIL: could not build the fixture model\n");
        return 2;
    }
    element = ToriDraw_SceneElementAddPool(scene, TORIDRAW_SCENE_POOL_STATIC_VIEW(1));
    ToriDraw_SceneElementSetModel(scene, element, ToriDraw_ModelHandleOwned(model));
    ToriDraw_SceneElementSetPosition(scene, element, 100, 1000, 300, 0);
    g_ground_element = element;

    /* An element, the marked tile's ground, then a second element. In a real
     * paint the second one is what stands on the tile: the bucket drains a
     * tile's ground before the scenery and dynamics that sit on it. */
    memset(commands, 0, sizeof(commands));
    commands[0]._bf_kind = PNTR_CMD_ELEMENT;
    commands[0]._entity._bf_entity = (uint32_t)element;
    commands[1]._bf_kind = PNTR_CMD_TERRAIN;
    commands[2]._bf_kind = PNTR_CMD_ELEMENT;
    commands[2]._entity._bf_entity = (uint32_t)element;
    memset(&painters, 0, sizeof(painters));
    painters.commands = commands;
    painters.command_count = 3;

    memset(&desc, 0, sizeof(desc));
    desc.kind = UITREE_EMIT_WORLD;
    desc.clip.w = 320;
    desc.clip.h = 200;
    memset(&camera, 0, sizeof(camera));

    /* One filled quad, spelled the way the overlay list spells one: a begin,
     * its points, an end. All three carry the same command index, because
     * they are one primitive. */
    memset(marks, 0, sizeof(marks));
    for( int i = 0; i < 3; i++ )
        marks[i].after_command = 1;
    marks[0].item.kind = UITREE_ENTITY_OVERLAY_POLY_BEGIN;
    marks[0].item.color = 0x9A9733;
    marks[0].item.trans = 205;
    marks[1].item.kind = UITREE_ENTITY_OVERLAY_POLY_POINT;
    marks[1].item.x = 40;
    marks[1].item.y = 50;
    marks[2].item.kind = UITREE_ENTITY_OVERLAY_POLY_END;

    ToriRS_FrameInit(&frame);
    ToriRS_FrameSetScene(&frame, scene);
    ToriRS_FrameSetCanvas(&frame, 320, 200);
    ToriRS_FrameSetEmit(&frame, &desc, 1);
    ToriRS_FrameSetWorld(&frame, root, &painters, &camera, 0, 0, 0);
    ToriRS_FrameSetWorldTileMarks(&frame, marks, 3);

    /*
     * MUTATION: drain the marks after the command loop in
     *   try_emit_world_draw_model instead of before it, or compare
     *   `after_command` with `<=` rather than `<`. Red on the two ordering
     *   lines below and nowhere else -- the wash is still drawn, in the
     *   colour asked for, one command too late, which is the whole defect.
     */
    ToriRS_FrameBegin(&frame);
    count = 0;
    while( ToriRS_FrameNextCommand(&frame, &cmd) && count < 8 )
        if( cmd.kind == TORIRSRC_DRAW_MODEL || cmd.kind == TORIRSRC_POLYGON_BEGIN ||
            cmd.kind == TORIRSRC_POLYGON_POINT || cmd.kind == TORIRSRC_POLYGON_END )
            order[count++] = (int)cmd.kind;
    ToriRS_FrameEnd(&frame);

    CHECK(count == 6, "three models and one three-part polygon reach the renderer");
    if( count == 6 )
    {
        CHECK(order[0] == TORIRSRC_DRAW_MODEL, "the element before the tile is drawn first");
        CHECK(order[1] == TORIRSRC_DRAW_MODEL, "then the marked tile's own ground");
        CHECK(order[2] == TORIRSRC_POLYGON_BEGIN, "then the wash begins");
        CHECK(order[3] == TORIRSRC_POLYGON_POINT, "with its points");
        CHECK(order[4] == TORIRSRC_POLYGON_END, "and its end");
        CHECK(
            order[5] == TORIRSRC_DRAW_MODEL,
            "and only THEN what stands on the tile -- which is what covers the wash");
    }

    /*
     * The same marks, and the second command of the buffer never reached.
     *
     * A mark is drained on the strength of the command index the app resolved
     * it to, and the walk runs forward: a marker whose ground the paint limit
     * cut off must not be emitted early, at the top of a frame, over nothing.
     */
    for( int i = 0; i < 3; i++ )
        marks[i].after_command = 2;
    ToriRS_FrameBegin(&frame);
    count = 0;
    while( ToriRS_FrameNextCommand(&frame, &cmd) && count < 8 )
        if( cmd.kind == TORIRSRC_DRAW_MODEL || cmd.kind == TORIRSRC_POLYGON_BEGIN )
            order[count++] = (int)cmd.kind;
    ToriRS_FrameEnd(&frame);
    CHECK(count == 4, "a mark placed against the last command still reaches the renderer");
    if( count == 4 )
        CHECK(
            order[0] == TORIRSRC_DRAW_MODEL && order[1] == TORIRSRC_DRAW_MODEL &&
                order[2] == TORIRSRC_DRAW_MODEL && order[3] == TORIRSRC_POLYGON_BEGIN,
            "after all three commands, not before any of them");

    /*
     * The world-only iterator carries MODEL commands and nothing else.
     *
     * It is the dual-core lane's worker stream -- a list of models to project
     * and sort ahead of the draw, not a stream that reaches a framebuffer --
     * so a 2D primitive in it is a command that lane has no job for.
     *
     * MUTATION: drop the `!frame->world_only` arm from the drain. Red here.
     */
    for( int i = 0; i < 3; i++ )
        marks[i].after_command = 1;
    ToriRS_FrameBegin(&frame);
    {
        struct ToriRS_Frame worker = frame;
        int models = 0;
        int polygons = 0;

        ToriRS_FrameBeginWorldOnly(&worker);
        while( ToriRS_FrameNextCommand(&worker, &cmd) )
        {
            if( cmd.kind == TORIRSRC_DRAW_MODEL )
                models++;
            if( cmd.kind == TORIRSRC_POLYGON_BEGIN || cmd.kind == TORIRSRC_POLYGON_POINT ||
                cmd.kind == TORIRSRC_POLYGON_END )
                polygons++;
        }
        CHECK(models == 3, "the world-only iterator yields the same three models");
        CHECK(polygons == 0, "and no tile-mark primitive at all");
    }
    ToriRS_FrameEnd(&frame);

    ToriDraw_SceneFree(scene);
    if( failures )
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("in-scene tile marks: order, placement and the world-only stream PASS");
    return 0;
}
