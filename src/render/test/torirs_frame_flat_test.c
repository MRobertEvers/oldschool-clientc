#include "render/torirs_frame.h"
#include "world/world.h"
#include "painters/painters.h"
#include "ui/uitree_emit.h"
#include "toridraw_scene.h"
#include "toridraw_model.h"
#include "render/torirs_frame_flat.u.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The test drives the real marker/emitter/model-copy path. Entity lookups are
 * irrelevant: this scene contains two loc models and one terrain element. */
static int ground_element;
int World_TerrainElementAt(struct World* w, int x, int z, int level)
{ (void)w; (void)x; (void)z; (void)level; return ground_element; }
struct WorldEntity_NPC* World_NpcGetByElementId(struct World* w, int id, int* idx)
{ (void)w; (void)id; (void)idx; return NULL; }
struct WorldEntity_Scenery* World_SceneryGetByElementId(struct World* w, int id)
{ (void)w; (void)id; return NULL; }

static struct ToriDraw_Model* model(void)
{
    struct ToriDraw_Model* m = ToriDraw_ModelNew(3, 1, 0);
    vertexint_t x[] = {0, 100, 0}, y[] = {100, 1000, -1000}, z[] = {0, 0, 100};
    faceint_t a[] = {0}, b[] = {1}, c[] = {2}, textures[] = {17};
    hsl16_t ca[] = {123}, cb[] = {456}, cc[] = {789};
#define COPY(field, data) m->field = ToriDraw_BufCopy(data, sizeof(data)/sizeof(*data), sizeof(*data))
    COPY(vertices_x,x); COPY(vertices_y,y); COPY(vertices_z,z);
    COPY(face_indices_a,a); COPY(face_indices_b,b); COPY(face_indices_c,c);
    COPY(face_colors_a,ca); COPY(face_colors_b,cb); COPY(face_colors_c,cc);
    COPY(face_textures,textures);
#undef COPY
    return m;
}

static int next_model(struct ToriRS_Frame* frame, struct ToriRS_RenderCommand* out)
{
    while( ToriRS_FrameNextCommand(frame, out) )
        if( out->kind == TORIRSRC_DRAW_MODEL ) return 1;
    return 0;
}

int main(void)
{
    ToriDraw_InitSinTable(); ToriDraw_InitCosTable();
    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(scene);
    struct ToriDraw_Model* original = model();
    int element = ToriDraw_SceneElementAddPool(scene, TORIDRAW_SCENE_POOL_STATIC_VIEW(1));
    ToriDraw_SceneElementSetModel(scene, element, ToriDraw_ModelHandleOwned(original));
    ToriDraw_SceneElementSetPosition(scene, element, 100, 1000, 300, 0);
    ground_element = element;
    struct World root = {0}, deck = {0};
    struct PaintersElementCommand commands[5] = {0};
    commands[0]._bf_kind = PNTR_CMD_BEGIN_WORLD; commands[0]._entity._bf_entity = 1;
    commands[1]._bf_kind = PNTR_CMD_ELEMENT; commands[1]._entity._bf_entity = element;
    commands[2]._bf_kind = PNTR_CMD_TERRAIN_PICK_ONLY;
    commands[3]._bf_kind = PNTR_CMD_END_WORLD; commands[3]._entity._bf_entity = 1;
    commands[4]._bf_kind = PNTR_CMD_ELEMENT; commands[4]._entity._bf_entity = element;
    struct PaintersBuffer painters = {0}; painters.commands = commands; painters.command_count = 5;
    struct UITreeEmitDesc desc = {0}; desc.kind = UITREE_EMIT_WORLD;
    desc.clip.w = 320; desc.clip.h = 200;
    struct ToriDraw_Camera camera = {0};
    struct ToriRS_Frame frame;
    ToriRS_FrameInit(&frame); ToriRS_FrameSetScene(&frame,scene);
    ToriRS_FrameSetCanvas(&frame,320,200); ToriRS_FrameSetEmit(&frame,&desc,1);
    ToriRS_FrameSetWorld(&frame,&root,&painters,&camera,0,0,0);
    ToriRS_FrameSetViewXform(&frame,1,&deck,-64,-64,1000,50,2000,512);
    frame.views[1].flatten_scale = 0.01f;
    frame.views[1].flatten_y_offset = -1200; frame.views[1].flat_hsl = 923;
    ToriRS_FrameBegin(&frame);
    struct ToriRS_Frame replay = frame;
    ToriRS_FrameBeginWorldOnly(&replay);
    struct ToriRS_RenderCommand flat, normal, again;
    assert(next_model(&frame,&flat));
    const struct ToriDraw_Model* copy = ToriDraw_ModelRead(flat.u.model.model);
    assert(copy != original);
    assert(copy->vertices_y[0] == 1 && copy->vertices_y[1] == 10 && copy->vertices_y[2] == -10);
    assert(copy->face_colors_a[0] == 923 && copy->face_colors_b[0] == 923 && copy->face_colors_c[0] == 923);
    assert(copy->face_textures[0] == -1);
    assert(flat.u.model.position.y == 48); /* 50 + .01*(1000-1200) */
    assert(abs(flat.u.model.position.x - 1236) <= 1 &&
           abs(flat.u.model.position.z - 1964) <= 1); /* Q16 quarter-turn truncation */
    assert(!flat.u.model.pickable && flat.u.model.animation == NULL && flat.u.model.dynamic);
    assert(next_model(&frame,&normal)); /* PICK_ONLY was swallowed, root is next. */
    assert(normal.u.model.pick_view == 0 && normal.u.model.pickable);
    assert(normal.u.model.model.u.model.model == original && normal.u.model.position.y == 1000);
    assert(original->vertices_y[1] == 1000 && original->face_colors_a[0] == 123);
    assert(next_model(&replay,&again));
    assert(again.u.model.model.u.model.model == copy); /* copied iterator retains live ownership */
    assert(!next_model(&frame,&again));
    ToriRS_FrameEnd(&frame);
    assert(!frame.flat_arena);
    /* Live changes must appear next frame rather than reusing a static bake. */
    original->vertices_y[1] = 2000;
    ToriRS_FrameBegin(&frame);
    assert(next_model(&frame,&flat));
    assert(ToriDraw_ModelRead(flat.u.model.model)->vertices_y[1] == 20);
    ToriRS_FrameEnd(&frame);
    /* Root +15 valid views, followed by an empty refused cycle. The painter
     * may emit that pair at capacity; the emitter must not push frame17. */
    struct PaintersElementCommand deep[33]={0};
    int n=0;
    for(int id=1;id<16;++id)
    {
        deep[n]._bf_kind=PNTR_CMD_BEGIN_WORLD;deep[n++]._entity._bf_entity=id;
        ToriRS_FrameSetViewXform(&frame,id,&deck,0,0,0,0,0,0);
    }
    deep[n]._bf_kind=PNTR_CMD_BEGIN_WORLD;deep[n++]._entity._bf_entity=1;
    deep[n]._bf_kind=PNTR_CMD_END_WORLD;deep[n++]._entity._bf_entity=1;
    deep[n]._bf_kind=PNTR_CMD_ELEMENT;deep[n++]._entity._bf_entity=element;
    for(int id=15;id>0;--id)
    {deep[n]._bf_kind=PNTR_CMD_END_WORLD;deep[n++]._entity._bf_entity=id;}
    assert(n==33);painters.commands=deep;painters.command_count=n;
    ToriRS_FrameBegin(&frame);
    assert(next_model(&frame,&normal) && normal.u.model.pick_view==15);
    assert(!next_model(&frame,&normal) && frame.view_depth==0);
    ToriRS_FrameEnd(&frame);
    /* The alternate terrain handle must flatten without borrowing mutable
     * arrays from the source ground mesh. */
    vertexint_t gx[]={0,100,0},gy[]={100,200,-100},gz[]={0,0,100};
    faceint_t ga[]={0},gb[]={1},gc[]={2};
    hsl16_t colours[]={111};
    struct ToriDraw_ModelGround ground={.vertex_count=3,.face_count=1,
        .vertices_x=gx,.vertices_y=gy,.vertices_z=gz,.face_indices_a=ga,
        .face_indices_b=gb,.face_indices_c=gc,.face_colors_a=colours,
        .face_colors_b=colours,.face_colors_c=colours};
    struct ToriDraw_ModelHandle gh={.kind=TORIDRAWMK_GROUND};gh.u.model.ground=&ground;
    struct ToriRS_FrameFlatArena* arena=calloc(1,sizeof(*arena));assert(arena);
    struct ToriDraw_ModelHandle flattened_ground=frame_flat_model(arena,gh,0.01f,923);
    const struct ToriDraw_Model* gm=ToriDraw_ModelRead(flattened_ground);
    assert(gm->vertices_y[0]==1 && gm->vertices_y[1]==2 && gm->vertices_y[2]==-1);
    assert(gm->face_colors_a[0]==923 && !gm->face_textures);
    assert(gy[1]==200 && colours[0]==111 && gm->face_indices_a[0]==0);
    frame_flat_free(arena);
    ToriDraw_SceneFree(scene);
    puts("frame flat live geometry, scale, colour, no-pick, replay lifetime and root identity PASS");
    return 0;
}
