/*
 * Entity-overlay sprite translation, kept separate from the UITree model
 * tests: this is the seam where a plugin draw-image destination box becomes
 * the renderer's IF3/scaled-blit flag.
 *
 * Run: make -C src test-frame-entity-overlay-sprite
 */

#include "render/torirs_frame.h"
#include "ui/uitree_emit.h"

#include "toridraw_scene.h"

#include <stdio.h>
#include <string.h>

/* torirs_frame.c's world branch is deliberately not exercised here. These
 * close that branch's link boundary without pulling the complete World into a
 * test of one 2D descriptor. */
struct World;
struct WorldEntity_NPC;
struct WorldEntity_Scenery;

int
World_TerrainElementAt(struct World* world, int x, int z, int level)
{
    (void)world;
    (void)x;
    (void)z;
    (void)level;
    return -1;
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

#define CHECK(condition, message)                                                        \
    do                                                                                   \
    {                                                                                    \
        if( !(condition) )                                                               \
        {                                                                                \
            fprintf(stderr, "FAIL: %s\n", (message));                                  \
            failures++;                                                                  \
        }                                                                                \
    } while( 0 )

static int
next_sprite(struct ToriRS_Frame* frame, struct ToriRS_RenderCommand* out)
{
    while( ToriRS_FrameNextCommand(frame, out) )
        if( out->kind == TORIRSRC_SPRITE )
            return 1;
    return 0;
}

int
main(void)
{
    struct ToriDraw_Scene* scene;
    struct ToriRS_Frame frame;
    struct ToriRS_RenderCommand cmd;
    struct UITreeEntityOverlay items[3];
    struct UITreeEmitDesc desc;

    scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    if( !scene )
    {
        fprintf(stderr, "FAIL: could not create scene\n");
        return 2;
    }

    memset(items, 0, sizeof(items));
    items[0].kind = UITREE_ENTITY_OVERLAY_SPRITE;
    items[0].scene_id = 101;
    items[0].x = 12;
    items[0].y = 18;
    items[0].w = 80;
    items[0].h = 48;

    /* Built-in hitsplats/headicons deliberately leave both dimensions zero:
     * they retain the historical native-size sprite path. */
    items[1].kind = UITREE_ENTITY_OVERLAY_SPRITE;
    items[1].scene_id = 102;
    items[1].x = 30;
    items[1].y = 40;

    /* An incomplete box is not a scalable destination. */
    items[2].kind = UITREE_ENTITY_OVERLAY_SPRITE;
    items[2].scene_id = 103;
    items[2].w = 80;
    items[2].h = 0;

    memset(&desc, 0, sizeof(desc));
    desc.kind = UITREE_EMIT_ENTITY_OVERLAY;
    desc.entity_overlays = items;
    desc.entity_overlay_count = 3;
    desc.clip.x = 5;
    desc.clip.y = 7;
    desc.clip.w = 200;
    desc.clip.h = 120;

    ToriRS_FrameInit(&frame);
    ToriRS_FrameSetScene(&frame, scene);
    ToriRS_FrameSetCanvas(&frame, 320, 200);
    ToriRS_FrameSetEmit(&frame, &desc, 1);
    ToriRS_FrameBegin(&frame);

    CHECK(next_sprite(&frame, &cmd), "explicit-size overlay emitted a sprite");
    CHECK(cmd.u.sprite.scene_id == 101, "explicit-size overlay kept its scene id");
    CHECK(cmd.u.sprite.w == 80 && cmd.u.sprite.h == 48, "destination box was forwarded");
    CHECK(cmd.u.sprite.if3 == 1, "positive destination width and height select scaling");

    CHECK(next_sprite(&frame, &cmd), "native-size overlay emitted a sprite");
    CHECK(cmd.u.sprite.scene_id == 102, "native-size overlay kept its scene id");
    CHECK(cmd.u.sprite.if3 == 0, "zero-size overlay remains a native blit");

    CHECK(next_sprite(&frame, &cmd), "incomplete-size overlay emitted a sprite");
    CHECK(cmd.u.sprite.scene_id == 103, "incomplete-size overlay kept its scene id");
    CHECK(cmd.u.sprite.if3 == 0, "both destination dimensions are required for scaling");

    ToriRS_FrameEnd(&frame);
    ToriDraw_SceneFree(scene);

    if( failures )
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("entity-overlay sprite translation passed");
    return 0;
}
