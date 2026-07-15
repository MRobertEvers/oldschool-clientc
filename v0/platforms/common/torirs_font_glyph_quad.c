#include "platforms/common/torirs_font_glyph_quad.h"
#include "platforms/common/torirs_gpu_clipspace.h"

void
torirs_font_glyph_quad_clipspace(
    float sx0,
    float sy0,
    float sx1,
    float sy1,
    float u0,
    float v0,
    float u1,
    float v1,
    float cr,
    float cg,
    float cb,
    float ca,
    float fbw,
    float fbh,
    float out_vq[48])
{
    if( !out_vq )
        return;
    float cx0, cy0, cx1, cy1;
    torirs_logical_pixel_to_ndc(sx0, sy0, fbw, fbh, &cx0, &cy0);
    torirs_logical_pixel_to_ndc(sx1, sy1, fbw, fbh, &cx1, &cy1);

    out_vq[0] = cx0;
    out_vq[1] = cy0;
    out_vq[2] = u0;
    out_vq[3] = v0;
    out_vq[4] = cr;
    out_vq[5] = cg;
    out_vq[6] = cb;
    out_vq[7] = ca;
    out_vq[8] = cx1;
    out_vq[9] = cy0;
    out_vq[10] = u1;
    out_vq[11] = v0;
    out_vq[12] = cr;
    out_vq[13] = cg;
    out_vq[14] = cb;
    out_vq[15] = ca;
    out_vq[16] = cx1;
    out_vq[17] = cy1;
    out_vq[18] = u1;
    out_vq[19] = v1;
    out_vq[20] = cr;
    out_vq[21] = cg;
    out_vq[22] = cb;
    out_vq[23] = ca;
    out_vq[24] = cx0;
    out_vq[25] = cy0;
    out_vq[26] = u0;
    out_vq[27] = v0;
    out_vq[28] = cr;
    out_vq[29] = cg;
    out_vq[30] = cb;
    out_vq[31] = ca;
    out_vq[32] = cx1;
    out_vq[33] = cy1;
    out_vq[34] = u1;
    out_vq[35] = v1;
    out_vq[36] = cr;
    out_vq[37] = cg;
    out_vq[38] = cb;
    out_vq[39] = ca;
    out_vq[40] = cx0;
    out_vq[41] = cy1;
    out_vq[42] = u0;
    out_vq[43] = v1;
    out_vq[44] = cr;
    out_vq[45] = cg;
    out_vq[46] = cb;
    out_vq[47] = ca;
}
