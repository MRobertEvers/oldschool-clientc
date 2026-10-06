#include "cp_fields.h"

#include "checksum.h"
#include "rsbuffer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
cp_register_wire_bytes(enum RSCache_RegisterWire wire)
{
    if( wire == RSCACHE_REGISTER_WIRE_U1 )
        return 1;
    if( wire == RSCACHE_REGISTER_WIRE_U2 )
        return 2;
    if( wire == RSCACHE_REGISTER_WIRE_U4 )
        return 4;
    return 0;
}

/* ---- the archive -------------------------------------------------------- */

/* The framing is rscache_serverpack.c's, which the game server reads through;
 * these keep cachepack's spelling of it and its refuse-not-assert contract. */

uint32_t
cp_server_archive_build_payload(
    int kind,
    const uint8_t* payload,
    uint32_t payload_size,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(payload);
    assert(out);
    if( payload_size == 0 || out_capacity < payload_size + CP_SERVER_PACK_HEADER )
        return 0;
    return RSCache_ServerPackFrame((enum RSCache_ServerPackKind)kind, payload, payload_size,
                                   out, out_capacity);
}

uint32_t
cp_server_archive_build(
    const uint8_t* band,
    uint32_t band_size,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(band);
    /* An encoded band always holds its terminator, so an empty one is a caller
     * that skipped the encoder, not a record that states nothing. */
    assert(band_size > 0);
    return cp_server_archive_build_payload(CP_SERVER_PAYLOAD_BAND, band, band_size, out,
                                           out_capacity);
}

int
cp_server_archive_open(
    const uint8_t* data,
    int size,
    int* out_version,
    int* out_kind,
    const uint8_t** out_payload,
    int* out_payload_size)
{
    const uint8_t* payload;
    uint32_t payload_size;

    assert(data);
    if( size < CP_SERVER_PACK_HEADER )
        return 0;
    if( !RSCache_ServerPackUnframe(data, (uint32_t)size, (enum RSCache_ServerPackKind)data[3],
                                   &payload, &payload_size) )
        return 0;
    if( out_version )
        *out_version = data[2];
    if( out_kind )
        *out_kind = data[3];
    if( out_payload )
        *out_payload = payload;
    if( out_payload_size )
        *out_payload_size = (int)payload_size;
    return 1;
}

/* ---- server-only namespaces ---------------------------------------------- */

/*
 * One group per server-only namespace, from 128 up. See cp_fields.h for why the
 * base is 128 and why the space is one byte wide.
 *
 * Assigned here and nowhere else. The numbers are ours, so nothing outside this
 * project breaks if they move — but they must move *together* with whatever reads
 * the pack, which is why they are in one table rather than spelled at each use.
 */
static const struct CP_ServerGroup k_server_groups[] = {
    { "stat", CP_SERVER_GROUP_BASE + 0 },
    { "category", CP_SERVER_GROUP_BASE + 1 },
};

#define SERVER_GROUP_COUNT ((int)(sizeof(k_server_groups) / sizeof(k_server_groups[0])))

const struct CP_ServerGroup*
cp_server_groups(int* out_count)
{
    if( out_count )
        *out_count = SERVER_GROUP_COUNT;
    return k_server_groups;
}

int
cp_server_group_for(const char* ns)
{
    if( !ns )
        return -1;
    for( int i = 0; i < SERVER_GROUP_COUNT; i++ )
    {
        if( strcmp(k_server_groups[i].name, ns) == 0 )
            return k_server_groups[i].group;
    }
    return -1;
}

uint32_t
cp_server_names_encode(
    const int* ids,
    const char* const* names,
    int count,
    uint8_t* out,
    uint32_t out_capacity)
{
    return RSCache_ServerPackNamesEncode(ids, names, count, out, out_capacity);
}

int
cp_server_names_decode(
    const uint8_t* data,
    int size,
    int* out_ids,
    const char** out_names,
    int max)
{
    assert(size >= 0);
    return RSCache_ServerPackNamesDecode(data, (uint32_t)size, out_ids, out_names, max);
}
