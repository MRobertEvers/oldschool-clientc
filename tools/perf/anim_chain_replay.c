/* Actual captured element-pose calls. Input restoration is outside PMU windows. */
#define _GNU_SOURCE
#include "anim_chain_format.h"
#include "toridraw.h"
#include "toridraw_model.h"
#include "toridraw_scene.h"
#include "toridraw_animation.h"
#include <linux/perf_event.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct Record {
    struct AnimChainHeader h;
    struct ToriDraw_SceneElement e;
    struct ToriDraw_Animation a,b;
    struct ToriDraw_AnimBase base;
    vertexint_t *before[3],*expected[3];
    uint8_t *before_alpha,*expected_alpha;
    struct AnimChainOutput output;
};
struct SkinAsset { uint32_t token; struct ToriDraw_SkeletalAnim skin; uint8_t* seen; struct SkinAsset* next; };
static struct SkinAsset* skins;
static void fail(const char* text) { fprintf(stderr,"animation replay: %s\n",text); exit(1); }
static void exact(FILE* f,void* p,size_t n) { if(n && fread(p,1,n,f)!=n) fail("truncated corpus"); }
static void* alloc(size_t n) { void* p=calloc(1,n?n:1);if(!p)fail("allocation failed");return p; }
static void* array(FILE* f,size_t n) {void* p=alloc(n);exact(f,p,n);return p;}
static struct ToriDraw_Bones* bones(FILE* f,int limit)
{
    int32_t n;exact(f,&n,4);if(n==-1)return NULL;if(n<0 || n>65536)fail("bone count");
    uint32_t mask;exact(f,&mask,4);if(mask>3)fail("bone mask");
    struct ToriDraw_Bones* b=alloc(sizeof(*b));b->bones_count=n;
    if(mask&2)b->bones_sizes=array(f,(size_t)n*2);
    if(mask&1)b->bones=alloc((size_t)n*sizeof(*b->bones));
    if(mask==3)for(int i=0;i<n;i++) {
        b->bones[i]=array(f,(size_t)b->bones_sizes[i]*2);
        for(unsigned j=0;j<b->bones_sizes[i];j++)if(b->bones[i][j]>=limit)fail("bone index");
    }
    return b;
}
static void read_frame(FILE* f,struct ToriDraw_AnimFrame* frame,int length)
{
    frame->length=length;if(length<=0)return;
    frame->groups=array(f,(size_t)length*2);frame->x=array(f,(size_t)length*2);
    frame->y=array(f,(size_t)length*2);frame->z=array(f,(size_t)length*2);
}
static struct ToriDraw_SkeletalAnim* skin(FILE* f,const struct AnimChainHeader* h)
{
    size_t bytes=(size_t)h->skin_bones*16*4;
    float* values=array(f,bytes);
    struct SkinAsset* a;
    for(a=skins;a;a=a->next)if(a->token==h->track_token && a->skin.bone_count==h->skin_bones && a->skin.frame_count==h->frame_count) {
        float* slot=a->skin.matrices+(size_t)h->selected_frame*h->skin_bones*16;
        if(!a->seen[h->selected_frame] || !memcmp(slot,values,bytes))break;
    }
    if(!a) {
        a=alloc(sizeof(*a));a->token=h->track_token;a->skin.bone_count=h->skin_bones;a->skin.frame_count=h->frame_count;
        a->skin.matrices=alloc((size_t)h->frame_count*bytes);a->seen=alloc(h->frame_count);a->next=skins;skins=a;
    }
    memcpy(a->skin.matrices+(size_t)h->selected_frame*h->skin_bones*16,values,bytes);
    a->seen[h->selected_frame]=1;free(values);return &a->skin;
}
static struct Record* read_record(FILE* f,struct AnimChainHeader h)
{
    if(h.version!=1 || h.vertices<0 || h.vertices>32768 || h.faces<0 || h.faces>32768 || h.frame_count<1 || h.frame_count>32768 ||
        h.selected_frame<0 || h.selected_frame>=h.frame_count || h.secondary_count<0 || h.secondary_count>32768 ||
        h.base_length<0 || h.base_length>65536 || h.frame_length<0 || h.frame_length>65536 || h.secondary_length<0 || h.secondary_length>65536 ||
        h.walkmerge_count<0 || h.walkmerge_count>65536 || h.skin_vertices<0 || h.skin_vertices>65536 || h.skin_bones<0 || h.skin_bones>1024)fail("invalid header bounds");
    if(h.secondary_count && (h.selected_secondary_frame<0 || h.selected_secondary_frame>=h.secondary_count))fail("secondary frame");
    struct Record* r=alloc(sizeof(*r));r->h=h;
    struct ToriDraw_Model* m=ToriDraw_ModelNew(h.vertices,h.faces,0);if(!m)fail("model allocation");
    r->e.model.kind=TORIDRAWMK_MODEL;r->e.model.u.model.model=m;
    m->vertices_x=alloc((size_t)h.vertices*2);m->vertices_y=alloc((size_t)h.vertices*2);m->vertices_z=alloc((size_t)h.vertices*2);
    for(int i=0;i<3;i++)r->before[i]=array(f,(size_t)h.vertices*2);
    if(h.flags&AC_ORIGINAL) {m->original_vertices_x=array(f,(size_t)h.vertices*2);m->original_vertices_y=array(f,(size_t)h.vertices*2);m->original_vertices_z=array(f,(size_t)h.vertices*2);}
    if(h.flags&AC_ALPHA){r->before_alpha=array(f,h.faces);m->face_alphas=alloc(h.faces);}
    if(h.flags&AC_ORIGINAL_ALPHA)m->original_face_alphas=array(f,h.faces);
    m->vertex_bones=bones(f,h.vertices);m->face_bones=bones(f,h.faces);
    m->post_transform=h.post_transform;m->post_resize=h.post_resize;
    m->post_resize_x=h.resize_x;m->post_resize_z=h.resize_z;m->post_resize_height=h.resize_y;
    m->post_orient=h.orient;m->post_offset_x=h.offset_x;m->post_offset_y=h.offset_y;m->post_offset_z=h.offset_z;
    r->e.anim2_frame=h.secondary_frame;
    if(h.flags&AC_SKELETAL) {
        r->e.is_skeletal=true;m->animaya_vertex_count=h.skin_vertices;
        m->animaya_group_counts=array(f,h.skin_vertices);m->animaya_groups=alloc((size_t)h.skin_vertices*sizeof(void*));m->animaya_scales=alloc((size_t)h.skin_vertices*sizeof(void*));
        for(int i=0;i<h.skin_vertices;i++){m->animaya_groups[i]=array(f,m->animaya_group_counts[i]);m->animaya_scales[i]=array(f,m->animaya_group_counts[i]);}
        r->e.skeletal_animation=skin(f,&h);
    } else {
        r->base.length=h.base_length;
        if(h.flags&AC_BASE_TYPES)r->base.types=array(f,h.base_length);
        if(h.flags&AC_BASE_LENGTHS)r->base.bone_group_lengths=array(f,(size_t)h.base_length*2);
        if(h.flags&AC_BASE_GROUPS)r->base.bone_groups=alloc((size_t)h.base_length*sizeof(void*));
        if((h.flags&AC_BASE_GROUPS)&&(h.flags&AC_BASE_LENGTHS))for(int i=0;i<h.base_length;i++)r->base.bone_groups[i]=array(f,r->base.bone_group_lengths[i]);
        r->a.base=&r->base;r->a.frame_count=h.frame_count;r->a.frames=alloc((size_t)h.frame_count*sizeof(*r->a.frames));
        read_frame(f,&r->a.frames[h.selected_frame],h.frame_length);
        if(h.secondary_count){r->b.base=&r->base;r->b.frame_count=h.secondary_count;r->b.frames=alloc((size_t)h.secondary_count*sizeof(*r->b.frames));read_frame(f,&r->b.frames[h.selected_secondary_frame],h.secondary_length);}
        if(h.walkmerge_count){r->a.walkmerge=array(f,(size_t)h.walkmerge_count*4);if(r->a.walkmerge[h.walkmerge_count-1]!=9999999)fail("walkmerge terminator");}
        if(h.primary){r->e.animation=&r->a;r->e.secondary_animation=h.secondary_count?&r->b:NULL;}
        else r->e.secondary_animation=&r->a;
    }
    exact(f,&r->output,sizeof(r->output));
    for(int i=0;i<3;i++)r->expected[i]=array(f,(size_t)h.vertices*2);
    if(h.flags&AC_ALPHA)r->expected_alpha=array(f,h.faces);
    return r;
}
static void reset(struct Record* r)
{
    const struct AnimChainHeader* h=&r->h;struct ToriDraw_Model* m=r->e.model.u.model.model;
    memcpy(m->vertices_x,r->before[0],(size_t)h->vertices*2);memcpy(m->vertices_y,r->before[1],(size_t)h->vertices*2);memcpy(m->vertices_z,r->before[2],(size_t)h->vertices*2);
    if(h->flags&AC_ALPHA)memcpy(m->face_alphas,r->before_alpha,h->faces);
    m->has_bounds_cylinder=h->has_bounds;m->bounds_cylinder=h->bounds;
    r->e.posed_primary=h->posed_primary;r->e.posed_frame=h->posed_frame;r->e.posed_frame2=h->posed_frame2;
    const void* track=(h->flags&AC_SKELETAL)?(const void*)r->e.skeletal_animation:(const void*)&r->a;
    const void* second=h->primary && h->secondary_count && h->secondary_length>0?(const void*)&r->b:NULL;
    r->e.posed_track=h->track_matches?track:NULL;
    r->e.posed_track2=h->track2_matches?second:second?NULL:(const void*)m;
}
static void check(struct Record* r,size_t i,int arm)
{
    struct ToriDraw_Model* m=r->e.model.u.model.model;size_t n=(size_t)r->h.vertices*2;
    if(memcmp(m->vertices_x,r->expected[0],n)||memcmp(m->vertices_y,r->expected[1],n)||memcmp(m->vertices_z,r->expected[2],n)||
        ((r->h.flags&AC_ALPHA)&&memcmp(m->face_alphas,r->expected_alpha,r->h.faces))||
        m->has_bounds_cylinder!=r->output.has_bounds || memcmp(&m->bounds_cylinder,&r->output.bounds,sizeof(r->output.bounds))) {
        fprintf(stderr,"animation record %zu arm %d mismatch (element %d, frame %d)\n",i,arm,r->h.element_id,r->h.request_frame);exit(1);
    }
}
int main(int argc,char** argv)
{
    if(argc!=3)fail("usage: anim_chain_replay CORPUS EVENT|verify");
    cpu_set_t mask;CPU_ZERO(&mask);CPU_SET(0,&mask);if(sched_setaffinity(0,sizeof(mask),&mask))fail("affinity");
    ToriDraw_Init();FILE* f=fopen(argv[1],"rb");if(!f)fail("open corpus");
    struct Record** records=NULL;size_t count=0;unsigned skeletal=0,opportunities=0;
    for(;;) {
        struct AnimChainHeader h={0};exact(f,&h.magic,4);
        if(h.magic==ANIM_CHAIN_END){uint32_t n;exact(f,&n,4);if(n!=count || fgetc(f)!=EOF)fail("footer");break;}
        if(h.magic!=ANIM_CHAIN_MAGIC)fail("record magic");exact(f,(char*)&h+4,sizeof(h)-4);
        if(h.ordinal!=count || count>=100000)fail("record sequence");
        void* grown=realloc(records,(count+1)*sizeof(*records));if(!grown)fail("record allocation");records=grown;
        records[count++]=read_record(f,h);skeletal+=(h.flags&AC_SKELETAL)!=0;
        int second_frame=h.primary&&h.secondary_count&&h.secondary_length>0?h.selected_secondary_frame:0;
        opportunities+=h.posed_primary==(h.primary?1:0)&&h.posed_frame==h.selected_frame&&h.posed_frame2==second_frame&&h.track_matches&&h.track2_matches&&((h.flags&AC_SKELETAL)||h.frame_length>0);
    }
    fclose(f);if(!count)fail("empty corpus");
    for(int arm=0;arm<2;arm++)for(size_t i=0;i<count;i++){reset(records[i]);ToriDraw_SceneElementApplyAnimationResolved(&records[i]->e,records[i]->h.element_id,records[i]->h.primary,records[i]->h.request_frame,arm);check(records[i],i,arm);}
    printf("verified: %zu real pose calls, %u skeletal, %u matching prior poses, both arms\n",count,skeletal,opportunities);fflush(stdout);
    if(!strcmp(argv[2],"verify"))return 0;
    struct perf_event_attr a={0};a.size=sizeof(a);a.type=PERF_TYPE_HARDWARE;a.disabled=1;a.pinned=1;a.exclude_kernel=1;a.exclude_hv=1;a.read_format=PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING;
    if(!strcmp(argv[2],"cpu-cycles"))a.config=PERF_COUNT_HW_CPU_CYCLES;
    else if(!strcmp(argv[2],"instructions"))a.config=PERF_COUNT_HW_INSTRUCTIONS;
    else if(!strcmp(argv[2],"branch-misses"))a.config=PERF_COUNT_HW_BRANCH_MISSES;
    else if(!strcmp(argv[2],"L1-dcache-load-misses")){a.type=PERF_TYPE_HW_CACHE;a.config=PERF_COUNT_HW_CACHE_RESULT_MISS<<16;}
    else fail("unknown event");
    int fd=(int)syscall(__NR_perf_event_open,&a,0,-1,-1,0);if(fd<0){perror("PMU unavailable; no fallback");return 1;}
    for(int sample=0;sample<12;sample++) {
        int arm=sample%4==1||sample%4==2;
        for(size_t i=0;i<count;i++){reset(records[i]);ToriDraw_SceneElementApplyAnimationResolved(&records[i]->e,records[i]->h.element_id,records[i]->h.primary,records[i]->h.request_frame,arm);}
        for(size_t i=0;i<count;i++)reset(records[i]);
        if(ioctl(fd,PERF_EVENT_IOC_RESET,0)||ioctl(fd,PERF_EVENT_IOC_ENABLE,0))fail("PMU enable");
        for(size_t i=0;i<count;i++)ToriDraw_SceneElementApplyAnimationResolved(&records[i]->e,records[i]->h.element_id,records[i]->h.primary,records[i]->h.request_frame,arm);
        struct {uint64_t value,enabled,running;} v;
        if(ioctl(fd,PERF_EVENT_IOC_DISABLE,0)||read(fd,&v,sizeof(v))!=sizeof(v)||!v.running||v.running!=v.enabled)fail("PMU unavailable/multiplexed");
        for(size_t i=0;i<count;i++)check(records[i],i,arm);
        printf("pmu,%s,animation,%d,%zu,%" PRIu64 ",%.3f,arm=%d\n",argv[2],sample+1,count,v.value,(double)v.value/count,arm);
    }
    close(fd);return 0;
}
