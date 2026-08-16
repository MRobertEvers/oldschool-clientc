#include "net/rev/gameproto_revisions.h"
#include <assert.h>
#include "packetin.h"
#include "packetout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
rev_packetin_size(int wire_opcode)
{
    return packetin_size_lc254(wire_opcode);
}

static int
rev_packetin_code(int wire_opcode)
{
    return packetin_code_lc254(wire_opcode);
}

static int
rev_packetin_wire(int pkt_name)
{
    return packetin_wire_lc254(pkt_name);
}

static int
rev_packetout_code(int pkt_out_name)
{
    return packetout_code_lc254(pkt_out_name);
}

/*
 * Jag-archive CRCs are server-instance specific (LostCity_Server computes
 * them from its own cache: engine/src/cache/CrcTable.ts) and served over
 * HTTP:  curl -s http://<server-host>/crc | xxd   -> 9 big-endian int32s
 * (archive 0 is always 0). Supply them via TORIRS_JAG_CRC as 9
 * comma-separated decimal ints; without the env the login block carries
 * zeros and a CRC-checking server rejects the login.
 */
static struct GameProtoRevTable k_rev_lc254 = {
    .revision = GAMEPROTO_REVISION_LC254,
    .name = "lc254",
    .client_version = 254,
    .jag_checksum = { 0 },
    .packetin_size = rev_packetin_size,
    .packetin_code = rev_packetin_code,
    .packetin_wire = rev_packetin_wire,
    .packetout_code = rev_packetout_code,
};

struct GameProtoRevTable const*
GameProtoRev_LC254(void)
{
    static int crc_loaded = 0;
    if( !crc_loaded )
    {
        crc_loaded = 1;
        char const* env = getenv("TORIRS_JAG_CRC");
        if( env )
        {
            char buf[256];
            strncpy(buf, env, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            int i = 0;
            for( char* tok = strtok(buf, ","); tok && i < 9; tok = strtok(NULL, ",") )
                k_rev_lc254.jag_checksum[i++] = (int32_t)strtol(tok, NULL, 10);
            if( i != 9 )
                fprintf(
                    stderr,
                    "lc254: TORIRS_JAG_CRC had %d of 9 values; login CRCs incomplete\n",
                    i);
        }
    }
    return &k_rev_lc254;
}

struct GameProtoRevTable const*
GameProtoRev_ByName(char const* name)
{
    assert(name);
    if( strcmp(name, "lc254") == 0 )
        return GameProtoRev_LC254();
    if( strcmp(name, "lc245_2") == 0 )
        return GameProtoRev_LC245_2();
    if( strcmp(name, "xrsps233") == 0 )
        return GameProtoRev_XRSPS233();
    if( strcmp(name, "osrs230") == 0 )
        return GameProtoRev_OSRS230();
    if( strcmp(name, "osrs239") == 0 )
        return GameProtoRev_OSRS239();
    return NULL;
}
