#ifndef TORIRS_BAKE_CHAIN_FORMAT_H
#define TORIRS_BAKE_CHAIN_FORMAT_H
#include "render/trspk_toridraw.h"
#define BAKE_CHAIN_MAGIC 0x424b4331u
#define BAKE_CHAIN_END 0x424b4345u
#define BC_ALPHA 1u
#define BC_TEXTURES 2u
#define BC_TEX_COORDS 4u
#define BC_PNM 8u
struct BakeChainHeader {
    uint32_t magic,version,ordinal,model_token,flags;
    int32_t vertices,faces,textures,ordered;
    struct ToriDraw_Position position;
};
struct BakeChainFace { float xyz[9]; uint32_t argb[3]; struct UVFaceCoords uv; int32_t texture; };
static inline struct BakeChainFace bake_chain_face(const struct TRSPK_ToriDrawBakeFaceVerts* f)
{
    return (struct BakeChainFace){{f->wx_a,f->wy_a,f->wz_a,f->wx_b,f->wy_b,f->wz_b,f->wx_c,f->wy_c,f->wz_c},
        {f->argb_a,f->argb_b,f->argb_c},f->uv,f->tex_id};
}
#endif
