/* Included only by a diagnostic toridraw build. Entire call input/output is
 * serialized; no performance inference comes from capture or wall time. */
#include "anim_chain_format.h"
#include <pthread.h>
static struct { FILE* file; uint32_t pass,first,passes,count; bool probed,done; const char* path; } anim_capture;
static pthread_mutex_t anim_capture_lock=PTHREAD_MUTEX_INITIALIZER;
static void ac_write(const void* p,size_t n)
{ if( n && fwrite(p,1,n,anim_capture.file)!=n ) { perror("animation capture"); abort(); } }
static void ac_bones(const struct ToriDraw_Bones* b)
{
    int32_t n=b?b->bones_count:-1; ac_write(&n,4);
    if( n<0 ) return;
    uint32_t mask=(b->bones?1u:0u)|(b->bones_sizes?2u:0u);ac_write(&mask,4);
    if( b->bones_sizes ) ac_write(b->bones_sizes,(size_t)n*2);
    if( b->bones && b->bones_sizes ) for( int i=0;i<n;i++ ) ac_write(b->bones[i],(size_t)b->bones_sizes[i]*2);
}
static void ac_frame(const struct ToriDraw_AnimFrame* f)
{
    if( !f || f->length<=0 ) return;
    ac_write(f->groups,(size_t)f->length*2); ac_write(f->x,(size_t)f->length*2);
    ac_write(f->y,(size_t)f->length*2); ac_write(f->z,(size_t)f->length*2);
}
void ToriDraw_AnimCaptureBeginPass(void)
{
    pthread_mutex_lock(&anim_capture_lock); anim_capture.pass++;
    pthread_mutex_unlock(&anim_capture_lock);
}
void ToriDraw_AnimCaptureEndPass(void)
{
    pthread_mutex_lock(&anim_capture_lock);
    if( anim_capture.file && anim_capture.pass>=anim_capture.first+anim_capture.passes-1 )
    {
        uint32_t end[]={ANIM_CHAIN_END,anim_capture.count};ac_write(end,sizeof(end));
        if( fclose(anim_capture.file) ) abort();
        anim_capture.file=NULL;anim_capture.done=true;
        fprintf(stderr,"animation capture complete: %u real pose calls\n",anim_capture.count);
    }
    pthread_mutex_unlock(&anim_capture_lock);
}
static bool anim_chain_before(struct ToriDraw_SceneElement* e,int id,bool primary,int frame)
{
    pthread_mutex_lock(&anim_capture_lock);
    if( !anim_capture.probed )
    {
        anim_capture.probed=true;anim_capture.path=getenv("TORIRS_ANIM_CHAIN_CAPTURE");
        const char* v=getenv("TORIRS_ANIM_CHAIN_FIRST_PASS");anim_capture.first=v?(unsigned)atoi(v):120;
        v=getenv("TORIRS_ANIM_CHAIN_PASSES");anim_capture.passes=v?(unsigned)atoi(v):16;
    }
    if( !anim_capture.path || anim_capture.done || anim_capture.pass<anim_capture.first ||
        anim_capture.pass>=anim_capture.first+anim_capture.passes || e->model.kind!=TORIDRAWMK_MODEL || !e->model.u.model.model ) goto skip;
    struct ToriDraw_Model* m=e->model.u.model.model;
    const struct ToriDraw_Animation* a=primary?e->animation:e->secondary_animation;
    const struct ToriDraw_Animation* second=NULL;
    const struct ToriDraw_AnimFrame *f=NULL,*f2=NULL;
    const struct ToriDraw_SkeletalAnim* sk=e->is_skeletal?e->skeletal_animation:NULL;
    int selected=frame,selected2=0;
    const void *track=NULL,*track2=NULL;
    if( e->is_skeletal )
    {
        if( !sk || sk->frame_count<=0 || m->animaya_vertex_count<=0 || !m->animaya_group_counts || !m->animaya_groups || !m->animaya_scales ) goto skip;
        if( selected<0 || selected>=sk->frame_count ) selected=0;
        track=sk;
    }
    else
    {
        if( !a || !a->base || !a->frames || a->frame_count<=0 ) goto skip;
        if( selected<0 || selected>=a->frame_count ) selected=0;
        f=&a->frames[selected];track=a;
        if( primary && a->walkmerge && e->secondary_animation && e->secondary_animation->base &&
            e->secondary_animation->frames && e->secondary_animation->frame_count>0 )
        {
            second=e->secondary_animation; selected2=e->anim2_frame;
            if( selected2<0 || selected2>=second->frame_count ) selected2=0;
            f2=&second->frames[selected2];
            if( f2->length>0 ) track2=second;
        }
    }
    if( !anim_capture.file )
    {
        anim_capture.file=fopen(anim_capture.path,"wb");
        if( !anim_capture.file ) { perror("animation capture open");abort(); }
    }
    int merge_count=0;
    if( !sk && a->walkmerge )
        do { if( ++merge_count>65536 ) abort(); } while(a->walkmerge[merge_count-1]!=9999999);
    struct AnimChainHeader h={0};
    h.magic=ANIM_CHAIN_MAGIC;h.version=1;h.pass=anim_capture.pass;h.ordinal=anim_capture.count++;
    h.model_token=(uint32_t)(uintptr_t)m;h.track_token=(uint32_t)(uintptr_t)track;
    h.flags=(m->original_vertices_x?AC_ORIGINAL:0)|(m->face_alphas?AC_ALPHA:0)|
        (m->original_face_alphas?AC_ORIGINAL_ALPHA:0)|(sk?AC_SKELETAL:0);
    if( !sk ) h.flags |= (a->base->types?AC_BASE_TYPES:0)|(a->base->bone_group_lengths?AC_BASE_LENGTHS:0)|(a->base->bone_groups?AC_BASE_GROUPS:0);
    h.element_id=id;h.vertices=m->vertex_count;h.faces=m->face_count;
    h.primary=primary;h.request_frame=frame;h.selected_frame=selected;
    h.frame_count=sk?sk->frame_count:a->frame_count;
    h.secondary_count=second?second->frame_count:0;h.secondary_frame=e->anim2_frame;h.selected_secondary_frame=selected2;
    h.base_length=sk?0:a->base->length;h.frame_length=f?f->length:0;h.secondary_length=f2?f2->length:0;
    h.walkmerge_count=merge_count;h.skin_vertices=sk?m->animaya_vertex_count:0;h.skin_bones=sk?sk->bone_count:0;
    h.posed_primary=e->posed_primary;h.posed_frame=e->posed_frame;h.posed_frame2=e->posed_frame2;
    h.track_matches=e->posed_track==track;h.track2_matches=e->posed_track2==track2;
    h.post_transform=m->post_transform;h.post_resize=m->post_resize;
    h.resize_x=m->post_resize_x;h.resize_z=m->post_resize_z;h.resize_y=m->post_resize_height;
    h.orient=m->post_orient;h.offset_x=m->post_offset_x;h.offset_y=m->post_offset_y;h.offset_z=m->post_offset_z;
    h.has_bounds=m->has_bounds_cylinder;h.bounds=m->bounds_cylinder;
    ac_write(&h,sizeof(h));
    ac_write(m->vertices_x,(size_t)h.vertices*2);ac_write(m->vertices_y,(size_t)h.vertices*2);ac_write(m->vertices_z,(size_t)h.vertices*2);
    if(h.flags&AC_ORIGINAL) { ac_write(m->original_vertices_x,(size_t)h.vertices*2);ac_write(m->original_vertices_y,(size_t)h.vertices*2);ac_write(m->original_vertices_z,(size_t)h.vertices*2); }
    if(h.flags&AC_ALPHA) ac_write(m->face_alphas,(size_t)h.faces);
    if(h.flags&AC_ORIGINAL_ALPHA) ac_write(m->original_face_alphas,(size_t)h.faces);
    ac_bones(m->vertex_bones);ac_bones(m->face_bones);
    if( sk )
    {
        ac_write(m->animaya_group_counts,(size_t)h.skin_vertices);
        for(int i=0;i<h.skin_vertices;i++) { ac_write(m->animaya_groups[i],m->animaya_group_counts[i]);ac_write(m->animaya_scales[i],m->animaya_group_counts[i]); }
        ac_write(sk->matrices+(size_t)selected*h.skin_bones*16,(size_t)h.skin_bones*16*4);
    }
    else
    {
        if(h.flags&AC_BASE_TYPES) ac_write(a->base->types,(size_t)h.base_length);
        if(h.flags&AC_BASE_LENGTHS) ac_write(a->base->bone_group_lengths,(size_t)h.base_length*2);
        if((h.flags&AC_BASE_GROUPS) && (h.flags&AC_BASE_LENGTHS))
            for(int i=0;i<h.base_length;i++) ac_write(a->base->bone_groups[i],a->base->bone_group_lengths[i]);
        ac_frame(f);ac_frame(f2);ac_write(a->walkmerge,(size_t)merge_count*4);
    }
    return true; /* record lock remains held through the actual pose */
skip:
    pthread_mutex_unlock(&anim_capture_lock);return false;
}
static void anim_chain_after(struct ToriDraw_SceneElement* e)
{
    const struct ToriDraw_Model* m=e->model.u.model.model;
    struct AnimChainOutput out={m->has_bounds_cylinder,m->bounds_cylinder};ac_write(&out,sizeof(out));
    ac_write(m->vertices_x,(size_t)m->vertex_count*2);ac_write(m->vertices_y,(size_t)m->vertex_count*2);ac_write(m->vertices_z,(size_t)m->vertex_count*2);
    if(m->face_alphas) ac_write(m->face_alphas,(size_t)m->face_count);
    pthread_mutex_unlock(&anim_capture_lock);
}
