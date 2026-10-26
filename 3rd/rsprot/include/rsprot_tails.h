#ifndef RSPROT_TAILS_H
#define RSPROT_TAILS_H

/*
 * The parts of a payload RSProt writes outside the codec's Kotlin.
 *
 * A few prots are finished by RSProt's Netty layer or by a shared helper the
 * generators cannot transcribe -- a placeholder back-patched with a length, a
 * de-duplicated key list, a pre-encoded sub-packet stream, a bit grid. The
 * generated codec stops where the Kotlin stops, and every payload then fails
 * to decode whole. These are those parts, each written once; a packet's spec
 * calls them (spec/packets/<file>.ini), so the codecs stay generated and
 * decode the whole payload.
 */

#include "rsprot_exec.h"

/** The GPI init block that leads the login REBUILD_NORMAL: 30 bits of local
 *  coordinate, then 18 bits for each of the other 2046 slots, byte-rounded. */
#define RSPROT_GPI_INIT_BYTES ((30 + 2046 * 18 + 7) / 8)

/** Zones in a REBUILD_REGION grid: 4 levels x 13 x 13. */
#define RSPROT_REGION_ZONES (4 * 13 * 13)

/** XTEA keys a grid can need: one per distinct mapsquare, at most one a zone. */
#define RSPROT_REGION_KEYS_MAX RSPROT_REGION_ZONES

/*
 * The login form's GPI block, in front of REBUILD_NORMAL's own fields
 * (RSProx StaticRebuildDecoder: present when the payload has room for it, the
 * rebuild header and at least the smallest key list; RSProt RebuildLoginEncoder).
 * Decoding borrows the block and sets its length; encoding writes it when
 * `*length` is RSPROT_GPI_INIT_BYTES and nothing when it is 0.
 */
static inline void
rsprot_tail_gpi_init(RsprotExec *x, const uint8_t **block, int32_t *length, int32_t after)
{
    int32_t remaining = rsprot_remaining(x);

    if (remaining > 0)
        *length = remaining >= RSPROT_GPI_INIT_BYTES + after ? RSPROT_GPI_INIT_BYTES : 0;
    if (*length > 0)
        rsprot_xdata(x, block, RSPROT_GPI_INIT_BYTES, RSPROT_DATA_PLAIN, "gpi_init");
}

/*
 * A zone grid (RSProt map/util/RegionEncoder.encodeRegion; RSProx
 * RebuildRegion*Decoder, RebuildWorldEntity*Decoder): per zone, level-major
 * then x then z, a 1-bit "exists" and, when set, the 26-bit packed reference
 * zone. `zones` holds -1 for an absent zone.
 */
static inline void
rsprot_tail_zone_grid(RsprotExec *x, int32_t *zones, int32_t count)
{
    rsprot_xbits_begin(x);
    for (int32_t i = 0; i < count; i++) {
        int32_t exists = zones[i] != -1;

        rsprot_xbits(x, 1, &exists, "zone_exists");
        if (rsprot_branch(x, &exists))
            rsprot_xbits(x, 26, &zones[i], "zone");
        else
            zones[i] = -1;
    }
    rsprot_xbits_end(x);
}

/* `count` XTEA keys, four g4 each, after the grid. */
static inline void
rsprot_tail_xtea_keys(RsprotExec *x, int32_t *keys, int32_t count)
{
    for (int32_t i = 0; i < count * 4; i++)
        rsprot_x_u4_1234(x, &keys[i], "xtea_key");
}

#endif
