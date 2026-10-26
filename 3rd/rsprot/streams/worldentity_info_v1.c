/* WORLDENTITY_INFO_V1 / _V2, revisions 223-226. See worldentity_info_v1.h. */
#include "worldentity_info_v1.h"

#include "rsprot_info_internal.h"

#include <assert.h>
#include <stddef.h>

int
worldentity_info_v1(const uint8_t *data, int32_t len, RsprotInfoList *out)
{
    RsprotBuf b;
    int count;

    assert(out);
    rsprot_info_list_begin(out);
    rsprot_buf_wrap_read(&b, data, len);

    count = rsprot_g1(&b);
    rsprot_info_emit_value(out, RSPROT_OP_WE_COUNT, (uint64_t)count);
    for (int i = 0; i < count && !b.err; i++) {
        RsprotInfoOp *op;

        if (rsprot_g1(&b) != 1) {
            rsprot_info_emit(out, RSPROT_OP_WE_REMOVE)->who = i;
            continue;
        }
        op = rsprot_info_emit(out, RSPROT_OP_WE_MOVE);
        op->who = i;
        /* Whole tiles, carried in fine units like the later versions'. */
        op->we_move.dx = rsprot_g1(&b) << 7;
        op->we_move.dz = rsprot_g1(&b) << 7;
        op->we_move.dangle = rsprot_g2(&b);
        op->we_move.move_speed = rsprot_g1(&b);
    }
    while (!b.err && rsprot_buf_readable(&b) >= 10) {
        int before = out->count;
        RsprotInfoOp *op = rsprot_info_emit(out, RSPROT_OP_WE_ADD);

        op->who = rsprot_g2(&b);
        op->we_add.id = -1;
        op->we_add.priority = -1;
        op->we_add.size_x = rsprot_g1(&b) * 8;
        op->we_add.size_z = rsprot_g1(&b) * 8;
        op->we_add.x = rsprot_g1(&b) << 7;
        op->we_add.z = rsprot_g1(&b) << 7;
        op->we_add.angle = rsprot_g2(&b);
        op->we_add.unknown = rsprot_g2(&b); /* RSProx: unknownProperty */
        if (b.err)
            out->count = before;
    }
    return rsprot_info_finish(&b, out, len);
}

#define WORLDENTITY_INFO_V1_ENTRY(V, N)                                                            \
    int worldentity_info_##V##_osrs##N(const uint8_t *data, int32_t len, RsprotInfoList *out)      \
    {                                                                                              \
        return worldentity_info_v1(data, len, out);                                                \
    }

WORLDENTITY_INFO_V1_ENTRY(v1, 223)
WORLDENTITY_INFO_V1_ENTRY(v1, 224)
WORLDENTITY_INFO_V1_ENTRY(v1, 225)
WORLDENTITY_INFO_V1_ENTRY(v1, 226)
WORLDENTITY_INFO_V1_ENTRY(v2, 225)
WORLDENTITY_INFO_V1_ENTRY(v2, 226)

RsprotWorldEntityInfoReadFn
worldentity_info_v1_osrs(int revision)
{
    switch (revision) {
    case 223: return worldentity_info_v1_osrs223;
    case 224: return worldentity_info_v1_osrs224;
    case 225: return worldentity_info_v1_osrs225;
    case 226: return worldentity_info_v1_osrs226;
    default: return NULL;
    }
}

RsprotWorldEntityInfoReadFn
worldentity_info_v2_osrs(int revision)
{
    switch (revision) {
    case 225: return worldentity_info_v2_osrs225;
    case 226: return worldentity_info_v2_osrs226;
    default: return NULL;
    }
}
