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
#include <stdlib.h>
#include <string.h>

/* torirs_frame.c's world branch is deliberately not exercised here. These
 * close that branch's link boundary without pulling the complete World into a
 * test of one 2D descriptor. */
struct World;
struct WorldEntity_NPC;
struct WorldEntity_Scenery;
struct UITreeModelRenderCache;

struct ToriDraw_ModelHandle UITreeAnim_ModelForDraw(struct ToriDraw_Scene* scene,
    struct UITreeModelRenderCache* cache,int model,int sequence,int frame)
{
    (void)scene;(void)cache;(void)model;(void)sequence;(void)frame;
    /* This fixture contains only 2D descriptors. Fail if it enters model drawing. */
    abort();
}

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

static void test_minimap_mark_translation(void)
{
    struct ToriDraw_Scene* scene=ToriDraw_SceneNew(0,TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(scene);
    struct ToriDraw_Sprite** sprites=calloc(1,sizeof(*sprites));assert(sprites);
    sprites[0]=calloc(1,sizeof(**sprites));assert(sprites[0]);
    sprites[0]->pixels_argb=calloc(16,sizeof(uint32_t));assert(sprites[0]->pixels_argb);
    sprites[0]->width=4;sprites[0]->height=4;sprites[0]->crop_x=8;sprites[0]->crop_y=8;
    for( int y=0;y<4;++y ) for( int x=0;x<2;++x ) sprites[0]->pixels_argb[y*4+x]=0xffffffffu;
    ToriDraw_SceneSpriteAdd(scene,901,sprites,1);
    struct UITreeMinimapDot dots[2]={0};
    CHECK(ToriRS_MinimapTileProject(&dots[0],-2,18,0,65536),"nearby world tile projects onto minimap");
    CHECK(dots[0].tile_x[0]==-2 && dots[0].tile_y[0]==-18 && dots[0].tile_x[2]==2 && dots[0].tile_y[2]==-22,
        "north-up map uses a four-pixel tile footprint relative to local player");
    CHECK(ToriRS_MinimapTileProject(&dots[0],-2,18,65536,0),"rotated tile still projects independently of world camera visibility");
    CHECK(dots[0].tile_x[0]==18 && dots[0].tile_y[0]==-2 && dots[0].tile_x[2]==22 && dots[0].tile_y[2]==2,
        "camera rotation matches normal minimap axes");
    CHECK(!ToriRS_MinimapTileProject(&dots[0],1000,1000,0,65536),"distant tile obeys native minimap cull ring");
    dots[0].kind=1;dots[0].tile_fill=0x00abcd;dots[0].tile_alpha=50;
    int const xx[4]={-2,2,2,-2},yy[4]={-2,-2,2,2};
    memcpy(dots[0].tile_x,xx,sizeof(xx));memcpy(dots[0].tile_y,yy,sizeof(yy));
    dots[1].w=1;dots[1].h=1;dots[1].color=0xffffffffu;
    struct UITreeEmitDesc desc={0};desc.kind=UITREE_EMIT_MINIMAP;
    desc.x=5;desc.y=7;desc.w=20;desc.h=20;desc.mask_scene_id=901;
    desc.clip=(struct UITreeEmitClip){5,7,20,20};desc.minimap_dots=dots;desc.minimap_dot_count=2;
    struct ToriRS_Frame frame;ToriRS_FrameInit(&frame);ToriRS_FrameSetScene(&frame,scene);
    ToriRS_FrameSetCanvas(&frame,64,64);ToriRS_FrameSetEmit(&frame,&desc,1);
    for( int pass=0;pass<2;++pass )
    {
        desc.mask_keep_opaque=pass;
        ToriRS_FrameBegin(&frame);
        struct ToriRS_RenderCommand cmd;
        int pixels=0,white=0,last_white=0;
        while( ToriRS_FrameNextCommand(&frame,&cmd) )
        {
            if( cmd.kind!=TORIRSRC_FILL_RECT ) continue;
            if( (uint32_t)cmd.u.fill_rect.argb==0xffffffffu ) { ++white;last_white=1;continue; }
            last_white=0;pixels+=cmd.u.fill_rect.w*cmd.u.fill_rect.h;
            CHECK((uint32_t)cmd.u.fill_rect.argb==0x3200abcdu,"minimap fill preserves native opacity without inventing a border");
            CHECK(cmd.u.fill_rect.y>=15 && cmd.u.fill_rect.y<19,"minimap mark uses normal box-centre projection");
            CHECK(pass ? cmd.u.fill_rect.x<15 : cmd.u.fill_rect.x>=15,"minimap mark honors native mask polarity and crop");
        }
        CHECK(pixels==8 && white==1 && last_white,"masked tile precedes native local-player indicator and survives the next frame");
        ToriRS_FrameEnd(&frame);
    }
    /* Inward border widths are literal pixels on the tiny map tile. */
    struct ToriRS_MinimapMarkScan scan;
    dots[0].tile_alpha=0;dots[0].tile_outline_width=1;dots[0].color=0xff0000;
    ToriRS_MinimapMarkBegin(&scan,&dots[0],10,10,0,0,20,20,0,0,NULL,0);
    int x,y,width,count=0;uint32_t color;
    while( ToriRS_MinimapMarkNext(&scan,&dots[0],&x,&y,&width,&color) ) count+=width;
    CHECK(count==12,"one-pixel outline leaves the2x2interior unpainted");
    dots[0].tile_outline_width=2;
    ToriRS_MinimapMarkBegin(&scan,&dots[0],10,10,0,0,20,20,0,0,NULL,0);count=0;
    while( ToriRS_MinimapMarkNext(&scan,&dots[0],&x,&y,&width,&color) ) count+=width;
    CHECK(count==16,"two-pixel outline differs from one-pixel on a four-pixel tile");
    ToriDraw_SceneFree(scene);
}

int
main(void)
{
    struct ToriDraw_Scene* scene;
    struct ToriRS_Frame frame;
    struct ToriRS_RenderCommand cmd;
    struct UITreeEntityOverlay items[4];
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
    items[3].kind = UITREE_ENTITY_OVERLAY_RECT;
    items[3].x=10;items[3].y=10;items[3].w=40;items[3].h=12;
    items[3].color=0xffff00ffu;items[3].trans=127;

    memset(&desc, 0, sizeof(desc));
    desc.kind = UITREE_EMIT_ENTITY_OVERLAY;
    desc.entity_overlays = items;
    desc.entity_overlay_count = 4;
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
    int rect_found=0;
    while( ToriRS_FrameNextCommand(&frame,&cmd) )
        if( cmd.kind==TORIRSRC_FILL_RECT ) { rect_found=1;break; }
    CHECK(rect_found,"overlay rectangle reaches the renderer");
    CHECK((uint32_t)cmd.u.fill_rect.argb==0x80ff00ffu,
        "overlay rectangle opacity reaches the renderer");

    ToriRS_FrameEnd(&frame);
    ToriDraw_SceneFree(scene);

    test_minimap_mark_translation();
    if( failures )
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("entity-overlay sprite translation passed");
    return 0;
}
