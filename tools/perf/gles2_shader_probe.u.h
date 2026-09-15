/* Replay the actual uploaded painter draw list into equal offscreen targets.
 * Pixel parity and hardware SP counters; no CPU/GL timer queries. */
#include "kgsl_counters.h"
#include <inttypes.h>
static void shader_probe_clear(struct ToriRS_GLES2* r,bool scissor,const struct GLES2Rect* rect)
{
    gles2_set_scissor(r,NULL);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    if(scissor)gles2_set_scissor(r,rect);
}
static void gles2_shader_probe(struct ToriRS_GLES2* r,uint32_t index_base)
{
    static bool done=false,probed=false,enabled=false,aa=false,parity_only=false;
    static unsigned checked=0;
    if(!probed){probed=true;enabled=getenv("TORIRS_SHADER_PROBE")!=NULL;aa=getenv("TORIRS_SHADER_AA")!=NULL;parity_only=getenv("TORIRS_SHADER_PARITY_ONLY")!=NULL;}
    if(!enabled||done||r->zbuffer||r->frame_clock<300+checked*60)return;
    int width=r->drawable_width,height=r->drawable_height;
    if(width<=0||height<=0||width>4096||height>4096)abort();
    size_t bytes=(size_t)width*height*4;uint8_t* pixels[2]={malloc(bytes),malloc(bytes)};
    if(!pixels[0]||!pixels[1])abort();
    GLint previous_fbo;GLfloat clear[4];glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous_fbo);glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);
    bool scissor=r->scissor_on,original_fast=r->world_fast_shader;struct GLES2Rect rect=r->scissor_rect;
    GLuint texture,fbo;glGenTextures(1,&texture);gles2_bind_texture0(r,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)abort();
    for(int arm=0;arm<2;arm++){
        r->world_fast_shader=arm&&!aa;shader_probe_clear(r,scissor,&rect);gles2_sequence_issue(r,index_base);
        GLboolean writes;glGetBooleanv(GL_DEPTH_WRITEMASK,&writes);
        if(glIsEnabled(GL_DEPTH_TEST)||writes){fprintf(stderr,"shader probe: depth state violated\n");abort();}
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels[arm]);
    }
    size_t different=0;for(size_t p=0;p<bytes;p+=4)different+=memcmp(pixels[0]+p,pixels[1]+p,4)!=0;
    fprintf(stderr,"shader-parity,%d,%d,%zu,%u,%u\n",width,height,different,r->draw_item_count,(unsigned)r->frame_clock);
    if(different){fprintf(stderr,"shader probe: pixel mismatch, candidate rejected\n");abort();}
    if(parity_only)goto restore;
    int fd=krait_kgsl_open();if(fd<0){fprintf(stderr,"shader probe: hardware counters unavailable\n");abort();}
    for(int sample=0;sample<12;sample++){
        int arm=sample%4==1||sample%4==2;r->world_fast_shader=arm&&!aa;
        for(int i=0;i<2;i++){shader_probe_clear(r,scissor,&rect);gles2_sequence_issue(r,index_base);}glFinish();
        uint64_t begin_cycles,begin_alu,end_cycles,end_alu;
        if(krait_kgsl_read(fd,&begin_cycles,&begin_alu))abort();
        for(int i=0;i<8;i++){shader_probe_clear(r,scissor,&rect);gles2_sequence_issue(r,index_base);}glFinish();
        if(krait_kgsl_read(fd,&end_cycles,&end_alu)||end_cycles<begin_cycles||end_alu<begin_alu)abort();
        fprintf(stderr,"shader-gpu,%d,%d,8,%" PRIu64 ",%" PRIu64 "\n",sample,arm,end_cycles-begin_cycles,end_alu-begin_alu);
    }
    close(fd);
restore:
    checked++;done=!parity_only || checked>=4;
    glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)previous_fbo);r->world_fast_shader=original_fast;
    glClearColor(clear[0],clear[1],clear[2],clear[3]);
    glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);free(pixels[0]);free(pixels[1]);
    if(done)fprintf(stderr,"shader probe complete: %s\n",aa?"A/A":"A/B");
}
