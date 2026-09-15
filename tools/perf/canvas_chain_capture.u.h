#include "canvas_chain_format.h"
static void canvas_chain_capture(struct UITree const* tree,int w,int h)
{
    extern long g_torirs_frame_no;
    static long last=-1;
    static unsigned count;
    static FILE* file;
    if( !tree || g_torirs_frame_no<600 || last==g_torirs_frame_no || count>=24 ) return;
    const char* path=getenv("TORIRS_CANVAS_CAPTURE");
    if( !path ) return;
    if( !file ) {file=fopen(path,"wb");if( !file ) abort();}
    last=g_torirs_frame_no;
    struct UITreeCanvasWidths expected=UITree_CanvasMeasureReference(tree,w,h);
    struct UITreeCanvasWidths actual=UITree_CanvasMeasureCompact(tree,w,h);
    if( expected.strip!=actual.strip || expected.core!=actual.core ) abort();
    struct CanvasChainHeader header={CANVAS_CHAIN_MAGIC,tree->component_count,tree->generation,
        (uint32_t)w,(uint32_t)h,tree->dirty_gen,tree->layout_resolve_seq,1,expected.strip,expected.core};
    if( fwrite(&header,sizeof(header),1,file)!=1 ) abort();
    for( uint32_t i=0;i<tree->component_count;i++ ) {
        struct UITreeComponent const* c=&tree->components[i];
        struct CanvasChainNode n={c->parent,c->freed,c->behavior.hide,c->frame_hidden,c->replacement_hidden,
            c->position.width_mode,c->position.height_mode,c->position.x_mode,
            c->position.abs_x,c->position.abs_w,c->position.abs_h};
        if( fwrite(&n,sizeof(n),1,file)!=1 ) abort();
    }
    if( ++count==24 ) {if(fclose(file))abort();file=NULL;TORIRS_ERR("canvas-chain-complete,%u\n",count);}
}
