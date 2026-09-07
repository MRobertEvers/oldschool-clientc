#define _GNU_SOURCE
#include "bake_chain_format.h"
#include "toridraw.h"
#include "toridraw_model.h"
#include <linux/perf_event.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct Record {struct BakeChainHeader h;struct ToriDraw_Model* m;int32_t* order;struct BakeChainFace* expected;};
static volatile uint64_t sink;
static int encode_mode;
static int words_mode;
static struct TRSPK_VertexGLES2* encoded_output;
static void pack_expected(const struct BakeChainFace* face,struct TRSPK_VertexGLES2* out)
{
    for(unsigned k=0;k<3;k++) {
        uint32_t argb=face->argb[k];
        out[k]=(struct TRSPK_VertexGLES2){
            {face->xyz[k*3],face->xyz[k*3+1],face->xyz[k*3+2]},
            (argb&0xff00ff00u)|((argb>>16)&0xffu)|((argb&0xffu)<<16),
            {0.5f,0.5f},0,0,128,128};
    }
}
static void fail(const char* s){fprintf(stderr,"bake replay: %s\n",s);exit(1);}
static void exact(FILE* f,void* p,size_t n){if(n&&fread(p,1,n,f)!=n)fail("truncated corpus");}
static void* arr(FILE* f,size_t n){void* p=malloc(n?n:1);if(!p)fail("allocation");exact(f,p,n);return p;}
static struct Record load_record(FILE* f,struct BakeChainHeader h)
{
    if(h.version!=1||h.vertices<0||h.vertices>32768||h.faces<0||h.faces>32768||h.textures<0||h.textures>32768||h.ordered<0||h.ordered>h.faces||(h.flags&~15u))fail("invalid header");
    struct Record r={.h=h};r.m=ToriDraw_ModelNew(h.vertices,h.faces,0);struct ToriDraw_Model* m=r.m;
    m->vertices_x=arr(f,(size_t)h.vertices*2);m->vertices_y=arr(f,(size_t)h.vertices*2);m->vertices_z=arr(f,(size_t)h.vertices*2);
    m->face_indices_a=arr(f,(size_t)h.faces*2);m->face_indices_b=arr(f,(size_t)h.faces*2);m->face_indices_c=arr(f,(size_t)h.faces*2);
    m->face_colors_a=arr(f,(size_t)h.faces*2);m->face_colors_b=arr(f,(size_t)h.faces*2);m->face_colors_c=arr(f,(size_t)h.faces*2);
    if(h.flags&BC_ALPHA)m->face_alphas=arr(f,h.faces);
    if(h.flags&BC_TEXTURES)m->face_textures=arr(f,(size_t)h.faces*2);
    if(h.flags&BC_TEX_COORDS)m->face_texture_coords=arr(f,(size_t)h.faces*2);
    m->textured_face_count=h.textures;
    if(h.flags&BC_PNM){m->textured_p_coordinate=arr(f,(size_t)h.textures*2);m->textured_m_coordinate=arr(f,(size_t)h.textures*2);m->textured_n_coordinate=arr(f,(size_t)h.textures*2);}
    for(int i=0;i<h.faces;i++) {
        if((unsigned)m->face_indices_a[i]>=(unsigned)h.vertices||(unsigned)m->face_indices_b[i]>=(unsigned)h.vertices||(unsigned)m->face_indices_c[i]>=(unsigned)h.vertices)fail("face index");
        if(m->face_texture_coords&&m->face_texture_coords[i]!=-1&&(!(h.flags&BC_PNM)||(unsigned)m->face_texture_coords[i]>=(unsigned)h.textures))fail("texture index");
    }
    for(int i=0;i<h.textures&&(h.flags&BC_PNM);i++)if((unsigned)m->textured_p_coordinate[i]>=(unsigned)h.vertices||(unsigned)m->textured_m_coordinate[i]>=(unsigned)h.vertices||(unsigned)m->textured_n_coordinate[i]>=(unsigned)h.vertices)fail("texture vertex");
    r.order=arr(f,(size_t)h.ordered*4);for(int i=0;i<h.ordered;i++)if((unsigned)r.order[i]>=(unsigned)h.faces)fail("order");
    r.expected=arr(f,(size_t)h.ordered*sizeof(*r.expected));return r;
}
static void run(struct Record* records,size_t n,float* xyz,int arm,int verify)
{
    uint64_t total=0;
    for(size_t r=0;r<n;r++){
        struct Record* record=&records[r];struct TRSPK_WorldPlacement placement;
        trspk_toridraw_placement_init(&placement,&record->h.position);
        int use=(arm||encode_mode)&&record->h.ordered*3>record->h.vertices;
        if(use)trspk_toridraw_world_vertices(record->m,&placement,xyz);
        if(encode_mode && use && !record->m->face_textures) {
            if(words_mode && arm)trspk_toridraw_gles2_untextured_words(record->m,record->order,record->h.ordered,xyz,encoded_output);
            else if(arm || words_mode)trspk_toridraw_gles2_untextured(record->m,record->order,record->h.ordered,xyz,encoded_output);
            else for(int i=0;i<record->h.ordered;i++) {
                struct TRSPK_ToriDrawBakeFaceVerts face;
                trspk_toridraw_bake_face_cached(record->m,record->order[i],&placement,NULL,true,TRSPK_BAKE_COLOR_ARGB,xyz,&face);
                struct BakeChainFace packed=bake_chain_face(&face);pack_expected(&packed,encoded_output+i*3);
            }
            for(int i=0;i<record->h.ordered;i++) {
                if(verify) {
                    struct TRSPK_VertexGLES2 expected[3];pack_expected(&record->expected[i],expected);
                    if(memcmp(expected,encoded_output+i*3,sizeof(expected)))fail("direct encoded bytes mismatch");
                }
                total+=encoded_output[i*3].rgba;
            }
            continue;
        }
        for(int i=0;i<record->h.ordered;i++){
            struct TRSPK_ToriDrawBakeFaceVerts f;
            if(use)trspk_toridraw_bake_face_cached(record->m,record->order[i],&placement,NULL,true,TRSPK_BAKE_COLOR_ARGB,xyz,&f);
            else trspk_toridraw_bake_face(record->m,record->order[i],&placement,NULL,true,TRSPK_BAKE_COLOR_ARGB,&f);
            if(verify){struct BakeChainFace got=bake_chain_face(&f);if(memcmp(&got,&record->expected[i],sizeof(got))){fprintf(stderr,"model %zu face %d mismatch arm %d\n",r,i,arm);exit(1);}}
            total+=f.argb_a;
        }
    }
    sink=total;
}
int main(int argc,char** argv)
{
    if(argc!=3 && argc!=4)fail("usage: bake_chain_replay FILE EVENT|verify [encode|words]");
    if(argc==4 && strcmp(argv[3],"encode") && strcmp(argv[3],"words"))fail("unknown encoder mode");
    words_mode=argc==4 && !strcmp(argv[3],"words");
    encode_mode=words_mode || (argc==4 && !strcmp(argv[3],"encode"));
    cpu_set_t mask;CPU_ZERO(&mask);CPU_SET(0,&mask);if(sched_setaffinity(0,sizeof(mask),&mask))fail("affinity");ToriDraw_Init();
    FILE* f=fopen(argv[1],"rb");if(!f)fail("open");struct Record* records=NULL;size_t count=0;int maxv=0;uint64_t faces=0,textured=0;
    for(;;){struct BakeChainHeader h={0};exact(f,&h.magic,4);if(h.magic==BAKE_CHAIN_END){uint32_t n;exact(f,&n,4);if(n!=count||fgetc(f)!=EOF)fail("footer");break;}
        if(h.magic!=BAKE_CHAIN_MAGIC)fail("magic");exact(f,(char*)&h+4,sizeof(h)-4);if(h.ordinal!=count||count>=100000)fail("ordinal");
        void* grown=realloc(records,(count+1)*sizeof(*records));if(!grown)fail("records");records=grown;records[count++]=load_record(f,h);
        if(h.vertices>maxv)maxv=h.vertices;faces+=h.ordered;for(int i=0;i<h.ordered;i++)textured+=records[count-1].expected[i].texture>=0;
    }
    fclose(f);if(!count)fail("empty");float* xyz=malloc((size_t)(maxv?maxv:1)*12);if(!xyz)fail("scratch");
    encoded_output=malloc(32768u*3u*sizeof(*encoded_output));if(!encoded_output)fail("encode scratch");
    run(records,count,xyz,0,1);run(records,count,xyz,1,1);
    printf("verified: %zu real model bakes, %" PRIu64 " ordered faces, %" PRIu64 " textured, both arms\n",count,faces,textured);fflush(stdout);
    if(!strcmp(argv[2],"verify"))return 0;
    struct perf_event_attr a={0};a.size=sizeof(a);a.type=PERF_TYPE_HARDWARE;a.disabled=1;a.pinned=1;a.exclude_kernel=1;a.exclude_hv=1;a.read_format=PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING;
    if(!strcmp(argv[2],"cpu-cycles"))a.config=PERF_COUNT_HW_CPU_CYCLES;else if(!strcmp(argv[2],"instructions"))a.config=PERF_COUNT_HW_INSTRUCTIONS;else if(!strcmp(argv[2],"L1-dcache-load-misses")){a.type=PERF_TYPE_HW_CACHE;a.config=PERF_COUNT_HW_CACHE_RESULT_MISS<<16;}else fail("event");
    int fd=(int)syscall(__NR_perf_event_open,&a,0,-1,-1,0);if(fd<0){perror("PMU unavailable; no fallback");return 1;}
    unsigned reps=(unsigned)((300000+faces-1)/faces);
    for(int sample=0;sample<12;sample++){int arm=sample%4==1||sample%4==2;for(unsigned i=0;i<reps;i++)run(records,count,xyz,arm,0);
        if(ioctl(fd,PERF_EVENT_IOC_RESET,0)||ioctl(fd,PERF_EVENT_IOC_ENABLE,0))fail("enable");for(unsigned i=0;i<reps;i++)run(records,count,xyz,arm,0);
        struct{uint64_t value,enabled,running;}v;if(ioctl(fd,PERF_EVENT_IOC_DISABLE,0)||read(fd,&v,sizeof(v))!=sizeof(v)||!v.running||v.running!=v.enabled)fail("counter unavailable/multiplexed");
        printf("pmu,%s,bake,%d,%zu,%" PRIu64 ",%.3f,arm=%d\n",argv[2],sample+1,count*reps,v.value,(double)v.value/(count*reps),arm);
    }close(fd);return 0;
}
