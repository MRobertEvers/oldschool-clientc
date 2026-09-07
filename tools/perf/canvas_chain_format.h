#ifndef CANVAS_CHAIN_FORMAT_H
#define CANVAS_CHAIN_FORMAT_H
#include <stdint.h>
#define CANVAS_CHAIN_MAGIC 0x31435751u
struct CanvasChainHeader {
    uint32_t magic,nodes,generation,width,height,dirty,layout,invalid;
    int32_t strip,core;
};
struct CanvasChainNode {
    int32_t parent,freed,hide,frame_hidden,replacement_hidden;
    int32_t width_mode,height_mode,x_mode,x,w,h;
};
#endif
