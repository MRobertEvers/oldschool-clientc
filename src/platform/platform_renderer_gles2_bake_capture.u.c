#include "../../tools/perf/bake_chain_format.h"
static struct { FILE* f; unsigned count,limit,first; bool initialized,active,done; const char* path; } bake_capture;
static void bc_write(const void* p,size_t n)
{ if(n && fwrite(p,1,n,bake_capture.f)!=n) {perror("bake capture");abort();} }
static void gles2_bake_capture_begin(struct ToriRS_GLES2* r,struct ToriDraw_ModelHandle handle,
    const struct ToriDraw_Position* pos,const int* order,int count)
{
    bake_capture.active=false;
    if(!bake_capture.initialized) {
        bake_capture.initialized=true;bake_capture.path=getenv("TORIRS_BAKE_CHAIN_CAPTURE");
        const char* v=getenv("TORIRS_BAKE_CHAIN_MODELS");bake_capture.limit=v?(unsigned)atoi(v):128;
        v=getenv("TORIRS_BAKE_CHAIN_FIRST");bake_capture.first=v?(unsigned)atoi(v):300;
    }
    if(!bake_capture.path || bake_capture.done || !order || count<=0 || r->zbuffer ||
        r->frame_clock<bake_capture.first || !ToriDraw_ModelKindIsFull(handle.kind))return;
    const struct ToriDraw_Model* m=ToriDraw_ModelRead(handle);
    if(!bake_capture.f) {bake_capture.f=fopen(bake_capture.path,"wb");if(!bake_capture.f)abort();}
    struct BakeChainHeader h={BAKE_CHAIN_MAGIC,1,bake_capture.count,(uint32_t)(uintptr_t)m,
        (m->face_alphas?BC_ALPHA:0)|(m->face_textures?BC_TEXTURES:0)|(m->face_texture_coords?BC_TEX_COORDS:0)|
        (m->textured_p_coordinate?BC_PNM:0),m->vertex_count,m->face_count,m->textured_face_count,count,*pos};
    bc_write(&h,sizeof(h));
    bc_write(m->vertices_x,(size_t)h.vertices*2);bc_write(m->vertices_y,(size_t)h.vertices*2);bc_write(m->vertices_z,(size_t)h.vertices*2);
    bc_write(m->face_indices_a,(size_t)h.faces*2);bc_write(m->face_indices_b,(size_t)h.faces*2);bc_write(m->face_indices_c,(size_t)h.faces*2);
    bc_write(m->face_colors_a,(size_t)h.faces*2);bc_write(m->face_colors_b,(size_t)h.faces*2);bc_write(m->face_colors_c,(size_t)h.faces*2);
    if(h.flags&BC_ALPHA)bc_write(m->face_alphas,h.faces);
    if(h.flags&BC_TEXTURES)bc_write(m->face_textures,(size_t)h.faces*2);
    if(h.flags&BC_TEX_COORDS)bc_write(m->face_texture_coords,(size_t)h.faces*2);
    if(h.flags&BC_PNM){bc_write(m->textured_p_coordinate,(size_t)h.textures*2);bc_write(m->textured_m_coordinate,(size_t)h.textures*2);bc_write(m->textured_n_coordinate,(size_t)h.textures*2);}
    bc_write(order,(size_t)count*4);bake_capture.active=true;
}
static void gles2_bake_capture_face(const struct TRSPK_ToriDrawBakeFaceVerts* f)
{ if(bake_capture.active){struct BakeChainFace face=bake_chain_face(f);bc_write(&face,sizeof(face));} }
static void gles2_bake_capture_end(void)
{
    if(!bake_capture.active)return;bake_capture.active=false;
    if(++bake_capture.count>=bake_capture.limit){uint32_t end[]={BAKE_CHAIN_END,bake_capture.count};bc_write(end,sizeof(end));if(fclose(bake_capture.f))abort();bake_capture.f=NULL;bake_capture.done=true;fprintf(stderr,"bake capture complete: %u real model bakes\n",bake_capture.count);}
}
