#include "render/torirs_frame.h"
#include "render/torirs_silhouette_frame.h"
#include "toridraw_raster_kernel.h"
#include "world/world.h"
#include "painters/painters.h"
#include "ui/uitree_emit.h"
#include "toridraw.h"
#include "toridraw_scene.h"
#include "toridraw_model.h"
#include "toridraw_model_transform.h"
#include "toridraw_lighting.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Only world element commands are under test; these unrelated lookup doors
 * close the frame translator's link boundary without inventing game state. */
struct UITreeModelRenderCache;
struct ToriDraw_ModelHandle UITreeAnim_ModelForDraw(struct ToriDraw_Scene* scene,
    struct UITreeModelRenderCache* cache,int model,int sequence,int frame)
{(void)scene;(void)cache;(void)model;(void)sequence;(void)frame;abort();}
int World_TerrainElementAt(struct World* world,int x,int z,int level)
{(void)world;(void)x;(void)z;(void)level;return -1;}
struct WorldEntity_NPC* World_NpcGetByElementId(struct World* world,int id,int* index)
{(void)world;(void)id;(void)index;return NULL;}
struct WorldEntity_Scenery* World_SceneryGetByElementId(struct World* world,int id)
{(void)world;(void)id;return NULL;}

static int checks,failures;
#define CHECK(c,m) do {++checks;if(!(c)){++failures;fprintf(stderr,"FAIL %d: %s\n",__LINE__,m);}}while(0)

static int mask_at(const struct ToriRS_Silhouette* mask,int x,int y)
{return mask->alpha[(size_t)y*mask->width+x];}

static void test_normalized_faces(void)
{
    struct ToriRS_Silhouette mask;
    int sx[3]={-5000,10,10},sy[3]={-20,10,-10},sz[3]={25,100,100};
    int ox[3]={-20,20,20},oy[3]={-20,20,-20},oz[3]={25,100,100};
    vertexint_t px[3]={-20,20,20},py[3]={-20,20,-20},pz[3]={25,100,100};
    int const texels[4]={-1,-1,-1,-1};
    struct ToriDraw_RasterTarget target={
        .width=100,.height=100,.stride=100,.projection_center_x=50,.projection_center_y=50,
        .near_plane_z=50,.camera_cot16=50*128,.parallel_zoom16=65536,
        .near_clip_available=true,.vertex_count=3,
        .screen_vertices_x=sx,.screen_vertices_y=sy,.screen_vertices_z=sz,
        .orthographic_vertices_x=ox,.orthographic_vertices_y=oy,.orthographic_vertices_z=oz,
        .posed_vertices_x=px,.posed_vertices_y=py,.posed_vertices_z=pz};
    struct ToriDraw_RasterFaceSD face={
        .face_class=TORIDRAW_RASTER_FACE_SD_TEXTURED,.vertex={0,1,2},.opacity=255,
        .near_clipped=true,.texture={.texels=texels,.width=2,.height=2,
            .gate=TORIDRAW_RASTER_TEXTURE_OPAQUE,.frame={0,1,2}}};
    ToriRS_SilhouetteInit(&mask,0,0,100,100);
    ToriRS_SilhouetteDrawFace(&mask,&target,&face,true,1,false);
    ToriRS_SilhouetteStyle(&mask,70,1);
    CHECK(mask_at(&mask,53,45)==70,"whole near-clipped triangle retains its visible area");
    CHECK(mask_at(&mask,25,25)==0,"clipped-away region remains empty");
    ToriRS_SilhouetteFree(&mask);
    face.vertex[1]=2;face.vertex[2]=1;
    ToriRS_SilhouetteInit(&mask,0,0,100,100);
    ToriRS_SilhouetteDrawFace(&mask,&target,&face,true,1,false);
    ToriRS_SilhouetteStyle(&mask,70,1);
    CHECK(mask_at(&mask,53,45)==0,"near clipping preserves backface rejection");
    ToriRS_SilhouetteFree(&mask);
    face.vertex[1]=1;face.vertex[2]=2;
    target.parallel_projection=true;sx[1]=20;sx[2]=20;sy[1]=20;sy[2]=-20;
    ToriRS_SilhouetteInit(&mask,0,0,100,100);
    ToriRS_SilhouetteDrawFace(&mask,&target,&face,true,1,false);
    ToriRS_SilhouetteStyle(&mask,70,1);
    CHECK(mask_at(&mask,55,45)==70,"parallel near clipping uses the declared parallel scale");
    CHECK(mask_at(&mask,71,45)==0,"parallel clipped face does not use perspective scale");
    ToriRS_SilhouetteFree(&mask);
}

static struct ToriDraw_Model*
mesh(int quads)
{
    struct ToriDraw_Model* m=ToriDraw_ModelNew(quads*4,quads*2,0);
#define ARRAY(field,count) do {m->field=calloc((size_t)(count),sizeof(*m->field));assert(m->field);}while(0)
    ARRAY(vertices_x,quads*4);ARRAY(vertices_y,quads*4);ARRAY(vertices_z,quads*4);
    ARRAY(face_indices_a,quads*2);ARRAY(face_indices_b,quads*2);ARRAY(face_indices_c,quads*2);
    ARRAY(face_colors_a,quads*2);ARRAY(face_colors_b,quads*2);ARRAY(face_colors_c,quads*2);
    ARRAY(face_alphas,quads*2);
#undef ARRAY
    return m;
}

static void
set_quad(struct ToriDraw_Model* m,int q,int x0,int y0,int x1,int y1,int distance)
{
    int const x[4]={x0,x1,x1,x0},y[4]={y0,y0,y1,y1};
    for( int i=0;i<4;++i )
    {
        m->vertices_x[q*4+i]=(vertexint_t)lround((x[i]-32)*distance/100.0);
        m->vertices_y[q*4+i]=(vertexint_t)lround((y[i]-32)*distance/100.0);
    }
    /* The renderer's front-facing winding is clockwise in screen space. */
    m->face_indices_a[q*2]=q*4;m->face_indices_b[q*2]=q*4+2;m->face_indices_c[q*2]=q*4+1;
    m->face_indices_a[q*2+1]=q*4;m->face_indices_b[q*2+1]=q*4+3;m->face_indices_c[q*2+1]=q*4+2;
    for( int i=q*2;i<q*2+2;++i )
        m->face_colors_a[i]=m->face_colors_b[i]=m->face_colors_c[i]=1000;
}

static unsigned char pixels[64*64];
static uint32_t colors[64*64];
static void
run_frame(struct ToriRS_Frame* frame)
{
    struct ToriRS_RenderCommand command;
    int steps=0,tail=0;
    memset(pixels,0,sizeof(pixels));
    memset(colors,0,sizeof(colors));
    ToriRS_FrameBegin(frame);
    while( ToriRS_FrameNextCommand(frame,&command) )
    {
        CHECK(++steps<10000,"deferred span cursor terminates");
        if( steps>=10000 ) break;
        if( command.kind!=TORIRSRC_FILL_RECT ) continue;
        struct ToriRS_RenderCommand_FillRect const* r=&command.u.fill_rect;
        if( (r->argb&0xffffff)==0xff00ff ) {++tail;continue;}
        CHECK(r->x>=0 && r->y>=0 && r->x+r->w<=64 && r->y+r->h<=64,
              "every emitted mask span stays inside the world viewport");
        for( int y=r->y;y<r->y+r->h;++y )
            for( int x=r->x;x<r->x+r->w;++x )
            {pixels[y*64+x]=(unsigned)r->argb>>24;colors[y*64+x]=(unsigned)r->argb&0xffffff;}
    }
    CHECK(tail==1,"next overlay follows the complete silhouette exactly once");
    ToriRS_FrameEnd(frame);
}

int main(void)
{
    ToriDraw_Init();
    test_normalized_faces();
    struct ToriDraw_Scene* scene=ToriDraw_SceneNew(0,TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(scene);
    struct World* world=calloc(1,sizeof(*world));assert(world);
    struct ToriDraw_Model* subject=mesh(3);
    set_quad(subject,0,8,8,56,24,100);
    set_quad(subject,1,8,24,24,56,100);
    set_quad(subject,2,40,24,56,56,100);
    struct ToriDraw_Model* foreground=mesh(1);
    set_quad(foreground,0,0,30,30,48,75);
    ToriDraw_ModelSetBoundsCylinder(subject);
    ToriDraw_ModelSetBoundsCylinder(foreground);
    int const selected=ToriDraw_SceneElementAddPool(scene,TORIDRAW_SCENE_POOL_STATIC);
    int const occluder=ToriDraw_SceneElementAddPool(scene,TORIDRAW_SCENE_POOL_STATIC);
    ToriDraw_SceneElementSetModel(scene,selected,ToriDraw_ModelHandleOwned(subject));
    ToriDraw_SceneElementSetPosition(scene,selected,0,0,100,0);
    ToriDraw_SceneElementSetModel(scene,occluder,ToriDraw_ModelHandleOwned(foreground));
    ToriDraw_SceneElementSetPosition(scene,occluder,0,0,75,0);
    struct PaintersElementCommand commands[2]={0};
    commands[0]._bf_kind=PNTR_CMD_ELEMENT;commands[0]._entity._bf_entity=selected;
    commands[1]._bf_kind=PNTR_CMD_ELEMENT;commands[1]._entity._bf_entity=occluder;
    commands[0]._element_id=selected;commands[1]._element_id=occluder;
    struct PaintersBuffer painters={.commands=commands,.command_count=2};
    struct UITreeEntityOverlay items[2]={
        {.kind=UITREE_ENTITY_OVERLAY_SILHOUETTE,.color=0xff00ffff,.trans=185,.line_width=2,
         .silhouette_element_id=selected,.silhouette_always_on_top=true},
        {.kind=UITREE_ENTITY_OVERLAY_RECT,.color=0xffff00ff,.x=1,.y=1,.w=1,.h=1}};
    struct UITreeEmitDesc desc[2]={0};
    for( int i=0;i<2;++i ) {desc[i].w=64;desc[i].h=64;desc[i].clip.w=64;desc[i].clip.h=64;}
    desc[0].kind=UITREE_EMIT_WORLD;
    desc[1].kind=UITREE_EMIT_ENTITY_OVERLAY;desc[1].entity_overlays=items;desc[1].entity_overlay_count=2;
    struct ToriDraw_Camera camera={.projection_mode=TORIDRAW_PROJECTION_MODE_SCALE,
        .projection_scale=100,.near_plane_z=50};
    struct ToriRS_Frame frame;
    ToriRS_FrameInit(&frame);ToriRS_FrameSetScene(&frame,scene);
    ToriRS_FrameSetCanvas(&frame,64,64);ToriRS_FrameSetEmit(&frame,desc,2);
    ToriRS_FrameSetWorld(&frame,world,&painters,&camera,0,0,0);
    run_frame(&frame);
    CHECK(pixels[40*64+16]==70,"actual posed mesh leg receives the fill");
    CHECK(pixels[40*64+32]==0,"actual face topology leaves the U notch empty");
    CHECK(pixels[40*64+7]==255,"always-on-top retains the covered contour");
    items[0].silhouette_always_on_top=false;
    run_frame(&frame);
    CHECK(pixels[40*64+16]==0,"actual foreground model hides normal mesh fill");
    CHECK(pixels[40*64+7]==0,"actual foreground model hides normal mesh contour");
    CHECK(pixels[29*64+16]==70,"foreground cut does not introduce a highlighted seam");
    CHECK(pixels[40*64+48]==70,"uncovered posed geometry remains marked");
    frame.world_depth_test=true;
    struct PaintersElementCommand swap=commands[0];commands[0]=commands[1];commands[1]=swap;
    run_frame(&frame);
    CHECK(pixels[40*64+16]==0,"depth visibility works when nearer model is submitted first");
    frame.world_depth_test=false;
    run_frame(&frame);
    CHECK(pixels[40*64+16]==70,"painter visibility follows the selected model's later draw");
    items[0].silhouette_always_on_top=true;
    for( int i=0;i<subject->face_count;++i ) subject->face_alphas[i]=255;
    run_frame(&frame);
    CHECK(pixels[40*64+16]==0 && pixels[40*64+7]==0,"normalized face alpha removes invisible geometry");
    for( int i=0;i<subject->face_count;++i ) {subject->face_alphas[i]=0;subject->face_colors_c[i]=TORIDRAWHSL16_HIDDEN;}
    run_frame(&frame);
    CHECK(pixels[40*64+16]==0,"hidden face sentinel produces no mask");
    for( int i=0;i<subject->face_count;++i ) subject->face_colors_c[i]=1000;
    for( int i=0;i<subject->vertex_count;++i ) subject->vertices_x[i]+=8;
    ToriDraw_ModelSetBoundsCylinder(subject);
    run_frame(&frame);
    CHECK(pixels[40*64+24]==70 && pixels[40*64+10]==0,"the next frame uses the live changed pose");
    items[0]=(struct UITreeEntityOverlay){.kind=UITREE_ENTITY_OVERLAY_WORLD_SURFACE,
        .color=0xff00ffff,.surface_fill_color=0xff112233,.trans=0,.line_width=2,
        .silhouette_always_on_top=true,.surface_x={-24,-24,24,24},
        .surface_y={-24,24,24,-24},.surface_z={100,100,100,100}};
    run_frame(&frame);
    CHECK(pixels[40*64+16]==255 && colors[40*64+16]==0x112233,
          "world surface retains its independent opaque fill under always-on-top");
    int outlines=0;
    for( int i=0;i<64*64;++i ) if( pixels[i] && colors[i]==0x00ffff ) ++outlines;
    CHECK(outlines>0,"equal-alpha fill and outline spans preserve their distinct colors");
    items[0].silhouette_always_on_top=false;
    run_frame(&frame);
    CHECK(pixels[40*64+16]==0,"world surface respects foreground depth in a painter renderer");
    CHECK(pixels[20*64+48]==255,"coplanar scene geometry does not erase a surface marker");
    items[0].line_width=0;items[0].silhouette_always_on_top=true;
    run_frame(&frame);outlines=0;
    for( int i=0;i<64*64;++i ) if( pixels[i] && colors[i]==0x00ffff ) ++outlines;
    CHECK(outlines==0 && pixels[40*64+16]==255,"native fill-only surface has no border");
    items[0].surface_z[0]=25;
    run_frame(&frame);
    CHECK(pixels[45*64+48]>0,"whole world surface clips across the near plane without dropping its face");
    /* The scene stores terrain about its centre; the surface request carries
     * world corners. Camera rotation must not turn integer-origin rounding
     * into a false foreground test against that same ground. */
    struct ToriDraw_Model* ground=mesh(1);
    int const gx[4]={-64,64,64,-64},gz[4]={-64,-64,64,64};
    for( int i=0;i<4;++i ) {ground->vertices_x[i]=gx[i];ground->vertices_z[i]=gz[i];}
    ground->face_indices_a[0]=0;ground->face_indices_b[0]=1;ground->face_indices_c[0]=2;
    ground->face_indices_a[1]=0;ground->face_indices_b[1]=2;ground->face_indices_c[1]=3;
    ToriDraw_ModelSetBoundsCylinder(ground);
    int const ground_id=ToriDraw_SceneElementAddPool(scene,TORIDRAW_SCENE_POOL_STATIC);
    ToriDraw_SceneElementSetModel(scene,ground_id,ToriDraw_ModelHandleOwned(ground));
    ToriDraw_SceneElementSetPosition(scene,ground_id,0,0,64,0);
    commands[0]=(struct PaintersElementCommand){0};commands[0]._bf_kind=PNTR_CMD_ELEMENT;
    commands[0]._entity._bf_entity=ground_id;commands[0]._element_id=ground_id;painters.command_count=1;
    camera.pitch=128;camera.yaw=64;
    ToriRS_FrameSetWorld(&frame,world,&painters,&camera,0,-100,-200);
    items[0]=(struct UITreeEntityOverlay){.kind=UITREE_ENTITY_OVERLAY_WORLD_SURFACE,
        .color=0xff00ffff,.surface_fill_color=0xff112233,.trans=100,.line_width=0,
        .silhouette_always_on_top=true,.surface_x={-64,64,64,-64},
        .surface_y={0,0,0,0},.surface_z={0,0,128,128}};
    run_frame(&frame);
    unsigned char unoccluded[64*64];memcpy(unoccluded,pixels,sizeof(pixels));
    int area=0;for( int i=0;i<64*64;++i ) area+=pixels[i]!=0;
    CHECK(area>=100,"rotated camera sees substantial test terrain coverage");
    items[0].silhouette_always_on_top=false;run_frame(&frame);
    CHECK(memcmp(unoccluded,pixels,sizeof(pixels))==0,
          "the same terrain cannot occlude its own coplanar marker after camera rotation");
    ToriDraw_SceneFree(scene);free(world);
    printf("frame silhouette: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
