/* REBUILD_WORLDENTITY_V1. Hand-written; see rebuild_worldentity_v1.h. */
#include "rebuild_worldentity_v1.h"

void
packet_rebuild_worldentity_v1_v1_out(RsprotExec *x, MsgRebuildWorldEntityV1 *m)
{
    RSPROT_U2_12(x, m->index);
    RSPROT_U2_12(x, m->base_x);
    RSPROT_U2_12(x, m->base_z);
    rsprot_xdata(x, &m->gpi_init, RSPROT_GPI_INIT_BYTES, RSPROT_DATA_PLAIN, "gpi_init");
    RSPROT_COUNT2(x, m->xtea_count, RSPROT_REGION_KEYS_MAX);
    rsprot_tail_zone_grid(x, m->zones, RSPROT_REGION_ZONES);
    rsprot_tail_xtea_keys(x, m->keys, m->xtea_count);
}

const RsprotVersionRange rsprot_rebuild_worldentity_v1_out[] = {
    { RSPROT_OSRS_ID(222), RSPROT_OSRS_ID(224), (RsprotCodecFn)packet_rebuild_worldentity_v1_v1_out },
};

const int rsprot_rebuild_worldentity_v1_out_count =
    (int)(sizeof(rsprot_rebuild_worldentity_v1_out) / sizeof(rsprot_rebuild_worldentity_v1_out[0]));
