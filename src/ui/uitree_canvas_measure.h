#ifndef UITREE_CANVAS_MEASURE_H
#define UITREE_CANVAS_MEASURE_H
#include "uitree.h"
#include <assert.h>
#include <stdlib.h>
#include <limits.h>

void UITree_CanvasQuerySetCompact(int enabled);
int UITree_CanvasQueryCompactEnabled(void);

struct UITreeCanvasWidths { int strip, core; };

static inline int
uitree_canvas_hidden(struct UITree const* tree, int32_t idx)
{
    /* Preserve the existing canvas-query visibility contract and guard. */
    for( unsigned guard=0;idx>=0 && guard<256;guard++ ) {
        assert((uint32_t)idx<tree->component_count);
        struct UITreeComponent const* c=&tree->components[idx];
        if( c->freed || c->behavior.hide || c->frame_hidden ) return 1;
        idx=c->parent;
    }
    return 0;
}
static inline int
uitree_canvas_strip(struct UITree const* tree, uint32_t i, int width, int height, int best)
{
    struct UITreeComponent const* c=&tree->components[i];
    int w=c->position.abs_w;
    if( w<=0 || c->position.abs_x<=0 || c->position.width_mode!=0 ||
        c->position.height_mode!=1 || c->position.x_mode!=2 ||
        c->position.abs_h<height-2 || c->position.abs_x+w!=width || w<=best ) return best;
    return uitree_canvas_hidden(tree,(int32_t)i) ? best : w;
}
static inline int
uitree_canvas_core(struct UITree const* tree, uint32_t i, int width, int height, int strip, int best)
{
    struct UITreeComponent const* c=&tree->components[i];
    if( c->freed || c->position.width_mode!=0 || c->position.abs_w<=0 ||
        c->parent<0 || (uint32_t)c->parent>=tree->component_count ) return best;
    struct UITreeComponent const* p=&tree->components[c->parent];
    if( c->position.abs_w<=p->position.abs_w || p->position.width_mode!=1 ||
        p->position.height_mode!=1 || p->position.abs_x!=0 ||
        p->position.abs_w!=width-strip || p->position.abs_h<height-2 ||
        c->position.abs_w<=best ) return best;
    return uitree_canvas_hidden(tree,(int32_t)i) ? best : c->position.abs_w;
}
static inline struct UITreeCanvasWidths
UITree_CanvasMeasureReference(struct UITree const* tree, int width, int height)
{
    struct UITreeCanvasWidths result={0,0};
    for( uint32_t i=0;i<tree->component_count;i++ ) result.strip=uitree_canvas_strip(tree,i,width,height,result.strip);
    for( uint32_t i=0;i<tree->component_count;i++ ) result.core=uitree_canvas_core(tree,i,width,height,result.strip,result.core);
    return result;
}
static inline int
uitree_canvas_candidate_add(struct UITree* tree,uint32_t id)
{
    if( tree->canvas_candidate_count==tree->canvas_candidate_capacity ) {
        uint32_t cap=tree->canvas_candidate_capacity ? tree->canvas_candidate_capacity*2u : 16u;
        if( cap<tree->canvas_candidate_capacity || cap>UINT32_MAX/sizeof(uint32_t) ) return 0;
        uint32_t* p=realloc(tree->canvas_candidate_ids,(size_t)cap*sizeof(uint32_t));
        if( !p ) return 0;
        tree->canvas_candidate_ids=p;tree->canvas_candidate_capacity=cap;
    }
    tree->canvas_candidate_ids[tree->canvas_candidate_count++]=id;
    return 1;
}
static inline struct UITreeCanvasWidths
UITree_CanvasMeasureCompact(struct UITree const* source, int width, int height)
{
    struct UITree* tree=(struct UITree*)source;
    if( !tree->canvas_candidates_valid || tree->canvas_candidate_generation!=tree->generation ||
        tree->canvas_candidate_nodes!=tree->component_count ) {
        tree->canvas_candidates_valid=0;tree->canvas_candidate_count=0;
        if( tree->component_count>=0x80000000u ) return UITree_CanvasMeasureReference(tree,width,height);
        for( uint32_t i=0;i<tree->component_count;i++ ) {
            struct UITreeComponent const* c=&tree->components[i];
            if( c->freed || c->position.width_mode!=0 ) continue;
            if( c->position.height_mode==1 && c->position.x_mode==2 &&
                !uitree_canvas_candidate_add(tree,i|0x80000000u) )
                return UITree_CanvasMeasureReference(tree,width,height);
            if( c->parent>=0 && (uint32_t)c->parent<tree->component_count ) {
                struct UITreeComponent const* p=&tree->components[c->parent];
                if( p->position.width_mode==1 && p->position.height_mode==1 &&
                    !uitree_canvas_candidate_add(tree,i) )
                    return UITree_CanvasMeasureReference(tree,width,height);
            }
        }
        tree->canvas_candidate_generation=tree->generation;
        tree->canvas_candidate_nodes=tree->component_count;
        tree->canvas_candidates_valid=1;
    }
    struct UITreeCanvasWidths result={0,0};
    for( uint32_t n=0;n<tree->canvas_candidate_count;n++ ) {
        uint32_t id=tree->canvas_candidate_ids[n];
        if( id&0x80000000u ) result.strip=uitree_canvas_strip(tree,id&0x7fffffffu,width,height,result.strip);
    }
    for( uint32_t n=0;n<tree->canvas_candidate_count;n++ ) {
        uint32_t id=tree->canvas_candidate_ids[n];
        if( !(id&0x80000000u) ) result.core=uitree_canvas_core(tree,id,width,height,result.strip,result.core);
    }
    return result;
}
#endif
