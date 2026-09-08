#include "render/torirs_silhouette_frame.h"

#include "render/torirs_frame.h"
#include "toridraw.h"
#include "toridraw_model.h"
#include "toridraw_model_transform.h"
#include "toridraw_raster_kernel.h"
#include "toridraw_scene.h"
#include "ui/uitree_emit.h"
#include "graphics/winding.h"
#include "graphics/raster/texture/texmap_common.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct SilhouetteCameraVertex
{
    float x,y,z,u,v;
    float sx,sy;
};

static void
silhouette_face_uv(const struct ToriDraw_RasterTarget* target,
                   const struct ToriDraw_RasterFaceSD* face,
                   struct SilhouetteCameraVertex* vertices)
{
    int const p=face->texture.frame.p, m=face->texture.frame.m, n=face->texture.frame.n;
    const vertexint_t* x=target->posed_vertices_x;
    const vertexint_t* y=target->posed_vertices_y;
    const vertexint_t* z=target->posed_vertices_z;
    float u[3],v[3];
    assert(x);
    assert(y);
    assert(z);
    assert(p>=0);
    assert(p<target->vertex_count);
    assert(m>=0);
    assert(m<target->vertex_count);
    assert(n>=0);
    assert(n<target->vertex_count);
    struct ToriDraw_TexPlaneFrame const frame={
        x[p],y[p],z[p],x[m],y[m],z[m],x[n],y[n],z[n]};
    int const a=face->vertex[0],b=face->vertex[1],c=face->vertex[2];
    toridraw_texmap_project_plane(&frame,x[a],y[a],z[a],x[b],y[b],z[b],x[c],y[c],z[c],u,v);
    for( int i=0;i<3;++i ) {vertices[i].u=u[i];vertices[i].v=v[i];}

}

/* Preserve whole crossing faces: clip their camera-space polygon, then
 * project the new vertices with the same near plane and camera scale. */
static int
silhouette_face_vertices(const struct ToriDraw_RasterTarget* target,
                         const struct ToriDraw_RasterFaceSD* face,
                         struct ToriRS_SilhouetteVertex out[4])
{
    struct SilhouetteCameraVertex input[3], clipped[4];
    bool const textured=face->face_class==TORIDRAW_RASTER_FACE_SD_TEXTURED ||
                        face->face_class==TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT;
    int count=3;
    float const cx=(float)(target->clip_origin_x+target->projection_center_x);
    float const cy=(float)(target->clip_origin_y+target->projection_center_y);
    for( int i=0;i<3;++i )
    {
        int const v=face->vertex[i];
        assert(v>=0);
        assert(v<target->vertex_count);
        input[i]=(struct SilhouetteCameraVertex){
            .z=(float)(target->screen_vertices_z[v]+target->model_mid_z),
            .sx=target->screen_vertices_x[v]+cx,
            .sy=target->screen_vertices_y[v]+cy};
        if( face->near_clipped )
        {
            assert(target->near_clip_available);
            assert(target->orthographic_vertices_x);
            assert(target->orthographic_vertices_y);
            assert(target->orthographic_vertices_z);
            input[i].x=(float)target->orthographic_vertices_x[v];
            input[i].y=(float)target->orthographic_vertices_y[v];
            input[i].z=(float)target->orthographic_vertices_z[v];
        }
    }
    if( textured ) silhouette_face_uv(target,face,input);
    if( face->near_clipped )
    {
        float const near=(float)target->near_plane_z;
        float const scale=(float)(target->camera_cot16>>1)/64.0f;
        count=0;
        for( int i=0;i<3;++i )
        {
            struct SilhouetteCameraVertex const a=input[i], b=input[(i+1)%3];
            bool const ai=a.z>=near, bi=b.z>=near;
            if( ai ) clipped[count++]=a;
            if( ai!=bi )
            {
                float const t=(near-a.z)/(b.z-a.z);
                struct SilhouetteCameraVertex v={
                    .x=a.x+t*(b.x-a.x),.y=a.y+t*(b.y-a.y),.z=near,
                    .u=a.u+t*(b.u-a.u),.v=a.v+t*(b.v-a.v)};
                if( target->parallel_projection )
                {
                    v.sx=cx+v.x*target->parallel_zoom16/65536.0f;
                    v.sy=cy+v.y*target->parallel_zoom16/65536.0f;
                }
                else
                {
                    v.sx=cx+v.x*scale/near;
                    v.sy=cy+v.y*scale/near;
                }
                clipped[count++]=v;
            }
        }
    }
    else memcpy(clipped,input,sizeof(input));
    for( int i=0;i<count;++i )
    {
        struct SilhouetteCameraVertex const v=clipped[i];
        if( !target->parallel_projection && v.z<=0 ) return 0;
        out[i]=(struct ToriRS_SilhouetteVertex){
            .x=v.sx,.y=v.sy,.depth=target->parallel_projection?-v.z:1.0f/v.z,
            .u=v.u,.v=v.v,.q=target->parallel_projection?1.0f:1.0f/v.z};
    }
    if( count>=3 )
    {
        double const winding=((double)out[0].x-out[1].x)*((double)out[2].y-out[1].y)-
                             ((double)out[0].y-out[1].y)*((double)out[2].x-out[1].x);
        if( !toridraw_winding_front_facing(winding>0?1:winding<0?-1:0) ) return 0;
    }
    return count;
}

void
ToriRS_SilhouetteDrawFace(struct ToriRS_Silhouette* mask,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face,
    bool subject, int draw_order, bool depth_test)
{
    struct ToriRS_SilhouetteVertex polygon[4];
    struct ToriRS_SilhouetteTexture texture;
    int count;
    bool textured;
    assert(mask);
    assert(target);
    assert(face);
    count=silhouette_face_vertices(target,face,polygon);
    textured=face->face_class==TORIDRAW_RASTER_FACE_SD_TEXTURED ||
             face->face_class==TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT;
    if( textured )
        texture=(struct ToriRS_SilhouetteTexture){
            .pixels=(const uint32_t*)face->texture.texels,
            .width=face->texture.width,.height=face->texture.height,
            .color_key=face->texture.gate==TORIDRAW_RASTER_TEXTURE_COLOR_KEY,
            .texel_alpha=face->texture.gate==TORIDRAW_RASTER_TEXTURE_TEXEL_ALPHA};
    for( int i=1;i+1<count;++i )
    {
        struct ToriRS_SilhouetteVertex const triangle[3]={polygon[0],polygon[i],polygon[i+1]};
        ToriRS_SilhouetteTriangle(mask,triangle,face->opacity,textured,
            textured?&texture:NULL,subject,draw_order,depth_test);
    }
}

struct SilhouetteFramePass
{
    struct ToriRS_Silhouette* mask;
    bool bounds_only,subject,depth_test;
    int draw_order;
    double left,top,right,bottom;
};

static void
silhouette_frame_face(void* data,const struct ToriDraw_RasterTarget* target,
                      const struct ToriDraw_RasterFaceSD* face)
{
    struct SilhouetteFramePass* pass=data;
    assert(pass);
    assert(target);
    assert(face);
    if( pass->bounds_only )
    {
        struct ToriRS_SilhouetteVertex polygon[4];
        int const count=silhouette_face_vertices(target,face,polygon);
        for( int i=0;i<count;++i )
        {
            pass->left=fmin(pass->left,polygon[i].x);
            pass->top=fmin(pass->top,polygon[i].y);
            pass->right=fmax(pass->right,polygon[i].x);
            pass->bottom=fmax(pass->bottom,polygon[i].y);
        }
        return;
    }
    ToriRS_SilhouetteDrawFace(pass->mask,target,face,
        pass->subject,pass->draw_order,pass->depth_test);
}

static void
silhouette_raster_model(struct ToriDraw_Scene* scene,
    const struct ToriRS_RenderCommand_Model* model,
    struct ToriRS_RenderCommand_Begin3D* view,
    struct SilhouetteFramePass* pass,toripixel_t* sink)
{
    struct ToriDraw_RasterKernelSDVTable const vtable={
        {silhouette_frame_face,silhouette_frame_face,silhouette_frame_face,silhouette_frame_face}};
    struct ToriDraw_RasterKernelSD zkernel={
        .name="silhouette_z",.draw_model=ToriDraw_RasterWalkPerFace,.vtable=&vtable,
        .user_data=pass,.flags=TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING |
                               TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER};
    struct ToriDraw_RasterKernelSD kernel={
        .name="silhouette",.draw_model=ToriDraw_RasterWalkPerFace,.vtable=&vtable,
        .user_data=pass,.flags=TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
        .zbuffered_variant=&zkernel};
    struct ToriDraw_Kernel table=*ToriDraw_KernelGetSoftwarePainter();
    table.name="silhouette";
    table.raster=&kernel;
    table.raster_hd=NULL;
    if( model->animation && model->element_id>=0 )
        ToriDraw_SceneElementApplyAnimation(scene,model->element_id,
            model->anim_index==0,model->anim_frame);
    struct ToriDraw_Position position=model->position;
    if( ToriDraw_RenderModel1ProjectWithTable(model->model,scene,&position,
            &view->view_port,&view->camera,&table)!=TORIDRAW_CULL_VISIBLE ) return;
    if( ToriDraw_RenderModel2SortFacesWithTable(model->model,scene,&table)<=0 ) return;
    (void)ToriDraw_RenderModel3RasterWithTable(scene,&view->view_port,&view->camera,sink,&table);
}

/* Derive nested-view transforms and painter commands once per viewport. A
 * crowd of marked entities must not repeat the full world emitter for each
 * bound/coverage pass. Commands borrow model/pose storage until FrameEnd. */
struct ToriRS_SilhouetteWorld
{
    const struct UITreeEmitDesc* descriptor;
    struct ToriRS_RenderCommand_Begin3D view;
    struct ToriRS_RenderCommand_Model* models;
    int count,capacity;
    struct ToriRS_SilhouetteWorld* next;
};

static struct ToriRS_SilhouetteWorld*
silhouette_world(struct ToriRS_Frame* source,const struct UITreeEmitDesc* descriptor)
{
    struct ToriRS_SilhouetteWorld* world;
    for( world=source->silhouette_worlds;world;world=world->next )
        if( world->descriptor==descriptor ) return world;
    world=calloc(1,sizeof(*world));assert(world);
    world->descriptor=descriptor;
    struct ToriRS_Frame replay=*source;
    struct ToriRS_RenderCommand command;
    replay.emit_cmds=descriptor;replay.emit_count=1;
    ToriRS_FrameBeginWorldOnly(&replay);
    while( ToriRS_FrameNextCommand(&replay,&command) )
    {
        if( command.kind==TORIRSRC_BEGIN_3D ) world->view=command.u.begin_3d;
        if( command.kind!=TORIRSRC_DRAW_MODEL ) continue;
        if( world->count==world->capacity )
        {
            assert(world->capacity<=INT_MAX/2);
            int const capacity=world->capacity?world->capacity*2:256;
            assert(capacity>world->capacity);
            void* grown=realloc(world->models,(size_t)capacity*sizeof(*world->models));
            assert(grown);world->models=grown;world->capacity=capacity;
        }
        world->models[world->count++]=command.u.model;
    }
    world->next=source->silhouette_worlds;source->silhouette_worlds=world;
    return world;
}

void
ToriRS_SilhouetteForgetFrame(struct ToriRS_Frame* frame)
{
    assert(frame);
    while( frame->silhouette_worlds )
    {
        struct ToriRS_SilhouetteWorld* world=frame->silhouette_worlds;
        frame->silhouette_worlds=world->next;
        free(world->models);free(world);
    }
}

static void
silhouette_frame_walk(struct ToriRS_Frame* source,const struct UITreeEmitDesc* descriptor,
    int element_id,bool selected,struct SilhouetteFramePass* pass,toripixel_t* sink)
{
    struct ToriRS_SilhouetteWorld* world=silhouette_world(source,descriptor);
    struct ToriRS_RenderCommand_Begin3D view=world->view;
    for( int i=0;i<world->count;++i )
    {
        struct ToriRS_RenderCommand_Model const* model=&world->models[i];
        if( model->pick_only || !ToriDraw_ModelKindIsFull(model->model.kind) ||
            (model->element_id==element_id)!=selected ) continue;
        pass->subject=selected;pass->draw_order=i+1;
        silhouette_raster_model(source->scene,model,&view,pass,sink);
    }
}

struct ToriRS_Silhouette*
ToriRS_SilhouetteBuildFrame(struct ToriRS_Frame* frame,int element_id,
    int fill_alpha,int outline_width,bool always_on_top,
    int clip_x,int clip_y,int clip_w,int clip_h)
{
    const struct UITreeEmitDesc* world=NULL;
    struct SilhouetteFramePass pass={.bounds_only=true,.left=INFINITY,.top=INFINITY,
                                     .right=-INFINITY,.bottom=-INFINITY};
    struct ToriRS_Silhouette* mask;
    toripixel_t* sink;
    int left,top,right,bottom;
    assert(frame);
    assert(frame->scene);
    if( clip_w<=0 || clip_h<=0 || !frame->world || !frame->painters ) return NULL;
    for( int i=frame->emit_index-1;i>=0;--i )
        if( frame->emit_cmds[i].kind==UITREE_EMIT_WORLD ) {world=&frame->emit_cmds[i];break;}
    if( !world ) return NULL;
    assert(frame->canvas_w>0);
    assert(frame->canvas_h>0);
    /* The normalizing raster entry requires a valid full-canvas target even
     * though these callbacks write only the separate, bounded coverage mask. */
    sink=malloc((size_t)frame->canvas_w*frame->canvas_h*sizeof(*sink));
    assert(sink);
    silhouette_frame_walk(frame,world,element_id,true,&pass,sink);
    double const clip_right=(double)clip_x+clip_w,clip_bottom=(double)clip_y+clip_h;
    left=(int)fmin(clip_right,fmax(clip_x,floor(pass.left)-outline_width));
    top=(int)fmin(clip_bottom,fmax(clip_y,floor(pass.top)-outline_width));
    right=(int)fmax(clip_x,fmin(clip_right,ceil(pass.right)+outline_width));
    bottom=(int)fmax(clip_y,fmin(clip_bottom,ceil(pass.bottom)+outline_width));
    if( right<=left || bottom<=top ) {free(sink);return NULL;}
    mask=malloc(sizeof(*mask));
    assert(mask);
    ToriRS_SilhouetteInit(mask,left,top,right-left,bottom-top);
    pass.bounds_only=false;
    pass.mask=mask;
    pass.depth_test=frame->world_depth_test;
    silhouette_frame_walk(frame,world,element_id,true,&pass,sink);
    ToriRS_SilhouetteStyle(mask,fill_alpha,outline_width);
    if( !always_on_top ) silhouette_frame_walk(frame,world,element_id,false,&pass,sink);
    free(sink);
    return mask;
}


struct ToriRS_Silhouette*
ToriRS_SilhouetteBuildSurface(struct ToriRS_Frame* frame,
    const int x[4],const int y[4],const int z[4],int fill_alpha,int outline_width,
    bool always_on_top,int clip_x,int clip_y,int clip_w,int clip_h)
{
    const struct UITreeEmitDesc* world=NULL;
    struct SilhouetteFramePass pass={.bounds_only=true,.subject=true,.depth_test=true,
        .left=INFINITY,.top=INFINITY,.right=-INFINITY,.bottom=-INFINITY};
    struct ToriRS_RenderCommand_Begin3D view;
    struct ToriRS_RenderCommand command;
    struct ToriRS_Silhouette* mask;
    toripixel_t* sink;
    vertexint_t vx[4],vy[4],vz[4];
    faceint_t a[2]={0,0},b[2]={1,2},c[2]={2,3};
    hsl16_t color[2]={1,1};
    int left,top,right,bottom;
    assert(frame);
    assert(frame->scene);
    assert(x);
    assert(y);
    assert(z);
    if( clip_w<=0 || clip_h<=0 || !frame->world || !frame->painters ) return NULL;
    for( int i=frame->emit_index-1;i>=0;--i )
        if( frame->emit_cmds[i].kind==UITREE_EMIT_WORLD ) {world=&frame->emit_cmds[i];break;}
    if( !world ) return NULL;
    struct ToriRS_Frame replay=*frame;
    replay.emit_cmds=world;replay.emit_count=1;
    ToriRS_FrameBeginWorldOnly(&replay);
    if( !ToriRS_FrameNextCommand(&replay,&command) || command.kind!=TORIRSRC_BEGIN_3D ) return NULL;
    view=command.u.begin_3d;
    for( int i=0;i<4;++i ) {vx[i]=x[i]-x[0];vy[i]=y[i]-y[0];vz[i]=z[i]-z[0];}
    /* A valid plane basis for each triangle also supplies the standard
     * camera-space projection scratch used by the near clipper. No texture
     * ids are assigned: this surface contributes coverage, not material. */
    struct ToriDraw_Model quad={.vertex_count=4,.face_count=2,.textured_face_count=2,
        .vertices_x=vx,.vertices_y=vy,.vertices_z=vz,
        .face_indices_a=a,.face_indices_b=b,.face_indices_c=c,
        .face_colors_a=color,.face_colors_b=color,.face_colors_c=color,
        .textured_p_coordinate=a,.textured_m_coordinate=b,.textured_n_coordinate=c};
    ToriDraw_ModelSetBoundsCylinder(&quad);
    struct ToriRS_RenderCommand_Model surface={.element_id=-1,
        .model={.kind=TORIDRAWMK_MODEL,.u.model.model=&quad},
        .position={.x=x[0]-frame->cam_x,.y=y[0]-frame->cam_y,.z=z[0]-frame->cam_z}};
    sink=malloc((size_t)frame->canvas_w*frame->canvas_h*sizeof(*sink));
    assert(sink);
    silhouette_raster_model(frame->scene,&surface,&view,&pass,sink);
    double const clip_right=(double)clip_x+clip_w,clip_bottom=(double)clip_y+clip_h;
    left=(int)fmin(clip_right,fmax(clip_x,floor(pass.left)-outline_width));
    top=(int)fmin(clip_bottom,fmax(clip_y,floor(pass.top)-outline_width));
    right=(int)fmax(clip_x,fmin(clip_right,ceil(pass.right)+outline_width));
    bottom=(int)fmax(clip_y,fmin(clip_bottom,ceil(pass.bottom)+outline_width));
    if( right<=left || bottom<=top ) {free(sink);return NULL;}
    mask=malloc(sizeof(*mask));assert(mask);
    ToriRS_SilhouetteInit(mask,left,top,right-left,bottom-top);
    pass.bounds_only=false;pass.mask=mask;
    silhouette_raster_model(frame->scene,&surface,&view,&pass,sink);
    ToriRS_SilhouetteStyle(mask,fill_alpha,outline_width);
    /* A surface marker is attached to the terrain geometry, not to an
     * entity's painter submission. Compare scene depth in every renderer;
     * coplanar terrain survives, nearer actors and scenery attenuate it. */
    if( !always_on_top ) silhouette_frame_walk(frame,world,-1,false,&pass,sink);
    free(sink);
    return mask;
}
