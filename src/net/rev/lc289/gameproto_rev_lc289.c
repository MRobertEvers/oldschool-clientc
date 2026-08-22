#include "net/rev/gameproto_revisions.h"

#include "packetin.h"
#include "packetout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This revision's own PLAYER_INFO / NPC_INFO bit streams (lc289_entity_info.c).
 * Declared here rather than in a header of their own: the rev table is the only
 * thing that may reach them, which is the point of the hook. */
int
lc289_player_info_read(uint8_t const* data, int len, struct PktPlayerInfoOp* ops, int cap);

int
lc289_npc_info_read(uint8_t const* data, int len, struct PktNpcInfoOp* ops, int cap);

int
lc289_appearance_decode(uint8_t const* data, int len, struct PktPlayerAppearance* out);

static int
rev_packetin_size(int wire_opcode)
{
    return packetin_size_lc289(wire_opcode);
}

static int
rev_packetin_code(int wire_opcode)
{
    return packetin_code_lc289(wire_opcode);
}

static int
rev_packetin_wire(int pkt_name)
{
    return packetin_wire_lc289(pkt_name);
}

static int
rev_packetout_code(int pkt_out_name)
{
    return packetout_code_lc289(pkt_out_name);
}

/*
 * LostCity_Server Engine-TS branch 289 (January 17, 2005).
 *
 * The login block, the ISAAC keying and the classic bit codec are unchanged
 * from 254 -- every extension slot below stays at its zero default, which is
 * exactly what "classic lc254 behaviour" means here. What moved is the opcode
 * numbering, wholesale, and the client version: 289 does not fit in the
 * login block's one revision byte, so it goes out through the 255 escape
 * (net/loginproto.c).
 *
 * Jag-archive CRCs are server-INSTANCE specific -- the server computes them
 * from the cache it packed (engine/src/cache/CrcTable.ts) -- so there is no
 * right value to bake in here. A boot that reads its cache off the server
 * reads them off the same server too (`[cache:boot] source=ondemand`);
 * TORIRS_JAG_CRC below is the manual path, nine comma-separated decimal ints
 * from `curl -s http://<host>/crc`.
 */
static struct GameProtoRevTable k_rev_lc289 = {
    .revision = GAMEPROTO_REVISION_LC289,
    .name = "lc289",
    .client_version = 289,
    .jag_checksum = { 0 },
    .packetin_size = rev_packetin_size,
    .packetin_code = rev_packetin_code,
    .packetin_wire = rev_packetin_wire,
    .packetout_code = rev_packetout_code,
    /* 289 has its own entity-info readers rather than 254's. The wire
     * difference is one bit in the new-npc record, but a bit stream has no
     * partial compatibility: the layout is the unit, so the reader is too. */
    .player_info_read = lc289_player_info_read,
    .npc_info_read = lc289_npc_info_read,
    /* 289's appearance block is 254's plus a trailing two-byte skill level.
     * Length-prefixed, so the 254 decoder read it without complaint and simply
     * never saw the field. */
    .appearance_decode = lc289_appearance_decode,
};

struct GameProtoRevTable const*
GameProtoRev_LC289(void)
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
                k_rev_lc289.jag_checksum[i++] = (int32_t)strtol(tok, NULL, 10);
            if( i != 9 )
                fprintf(
                    stderr,
                    "lc289: TORIRS_JAG_CRC had %d of 9 values; login CRCs incomplete\n",
                    i);
        }
    }
    return &k_rev_lc289;
}
