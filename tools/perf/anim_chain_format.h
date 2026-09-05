#ifndef TORIRS_ANIM_CHAIN_FORMAT_H
#define TORIRS_ANIM_CHAIN_FORMAT_H
#include "toridraw_types.h"
#define ANIM_CHAIN_MAGIC 0x414e4331u
#define ANIM_CHAIN_END 0x414e4345u
#define AC_ORIGINAL 1u
#define AC_ALPHA 2u
#define AC_ORIGINAL_ALPHA 4u
#define AC_SKELETAL 8u
#define AC_BASE_TYPES 16u
#define AC_BASE_LENGTHS 32u
#define AC_BASE_GROUPS 64u
struct AnimChainHeader {
    uint32_t magic,version,pass,ordinal,model_token,track_token,flags;
    int32_t element_id,vertices,faces,primary,request_frame,selected_frame;
    int32_t frame_count,secondary_count,secondary_frame,selected_secondary_frame,base_length,frame_length,secondary_length;
    int32_t walkmerge_count,skin_vertices,skin_bones;
    int32_t posed_primary,posed_frame,posed_frame2,track_matches,track2_matches;
    int32_t post_transform,post_resize,resize_x,resize_z,resize_y,orient,offset_x,offset_y,offset_z;
    uint32_t has_bounds;
    struct ToriDraw_BoundsCylinder bounds;
};
struct AnimChainOutput { uint32_t has_bounds; struct ToriDraw_BoundsCylinder bounds; };
#endif
