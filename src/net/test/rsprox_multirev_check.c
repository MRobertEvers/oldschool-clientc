/*
 * Decode RSProx recordings with the data-driven revision tables, packet by
 * packet, and say what does not decode.
 *
 *   make -C src rsprox-multirev-check RECORDINGS="a.bin.gz b.bin.gz ..."
 *
 * Every recording is framed with its header's revision (GameProtoRev_Rsprox):
 * a frame that does not end at the end of the file fails the recording outright,
 * which is the strongest check there is on the prot tables -- one wrong size
 * shifts every packet after it. Then each server packet is decoded the way the
 * client would:
 *
 *   PLAYER_INFO               through the entity-info hook, against the
 *                             session's own state (seeded by the login REBUILD)
 *   NPC_INFO_*                through its prot's reader at this revision -- also
 *                             the pair 240 does not bind to a canonical arm
 *   WORLDENTITY_INFO_V*       through its reader; a stream that reads whole but
 *                             does not fit the canonical arm (over 15 entities)
 *                             is tallied as `arm` rather than failed
 *   a prot with a codec       through rsprox_parse_validate, which also asks
 *                             that the codec consume the payload EXACTLY
 *   everything               through the revision's parse_wire
 *
 * and tallied per prot. A prot with no canonical arm parses as dropped and is
 * reported as such rather than as a failure; one that has a codec still has to
 * decode.
 *
 * Exit status 1 when any packet with a codec or a stream reader failed.
 * MULTIREV_VERBOSE=1 prints each prot's first failure and its bytes; MULTIREV_TRACE=1 every
 * frame before it is decoded (the last line names a frame that crashes).
 */
#include "net/rev/gameproto_parse.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/packets/pkt_npc_info.h"
#include "net/rev/packets/pkt_player_info.h"
#include "net/rev/revpacket.h"
#include "net/rev/rsprox/rsprox_rev.h"
#include "net/rsprox_recording.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PROTS 256

struct Tally
{
    char const* prot;
    int count;
    int decoded;  /* codec or stream said yes */
    int failed;   /* codec or stream said no */
    int dropped;  /* no canonical arm */
    int arm;      /* read whole, but past the canonical arm's bounds */
    int parsed;   /* parse_wire said yes */
    int first_fail_frame;
    int first_fail_tick;
};

static struct Tally g_tally[MAX_PROTS];
static int g_tally_count;

static struct Tally*
tally(char const* prot)
{
    for( int i = 0; i < g_tally_count; i++ )
        if( strcmp(g_tally[i].prot, prot) == 0 )
            return &g_tally[i];
    if( g_tally_count == MAX_PROTS )
        return &g_tally[MAX_PROTS - 1];
    g_tally[g_tally_count].prot = prot;
    g_tally[g_tally_count].first_fail_frame = -1;
    return &g_tally[g_tally_count++];
}

/* Every npc stream prot's revision -> reader rows. */
static struct
{
    char const* prot;
    RsprotNpcInfoRange const* rows;
    int const* count;
} const k_npc_streams[] = {
    { "NPC_INFO_SMALL_V5", rsprot_npc_info_small_v5_read, &rsprot_npc_info_small_v5_read_count },
    { "NPC_INFO_LARGE_V5", rsprot_npc_info_large_v5_read, &rsprot_npc_info_large_v5_read_count },
    { "NPC_INFO_SMALL_V6", rsprot_npc_info_small_v6_read, &rsprot_npc_info_small_v6_read_count },
    { "NPC_INFO_LARGE_V6", rsprot_npc_info_large_v6_read, &rsprot_npc_info_large_v6_read_count },
};

/* `prot`'s reader at `revision`, or NULL when `prot` is not an npc stream. */
static RsprotNpcInfoReadFn
npc_reader(char const* prot, int revision)
{
    for( size_t i = 0; i < sizeof(k_npc_streams) / sizeof(k_npc_streams[0]); i++ )
    {
        if( strcmp(k_npc_streams[i].prot, prot) != 0 )
            continue;
        for( int r = 0; r < *k_npc_streams[i].count; r++ )
            if( revision >= k_npc_streams[i].rows[r].rev_lo &&
                revision <= k_npc_streams[i].rows[r].rev_hi )
                return k_npc_streams[i].rows[r].fn;
    }
    return NULL;
}

static RsprotInfoOp g_ops[16384];
static struct PktPlayerInfoOp g_pops[8192];
static struct PktNpcInfoOp g_nops[8192];

static int
check_one(char const* path, int verbose)
{
    struct RsproxRecording rec;
    struct GameProtoRevTable const* rev;
    char err[256];
    int failures = 0;

    memset(&rec, 0, sizeof(rec));
    if( RsproxRecording_OpenFile(&rec, path, err, sizeof(err)) != 0 )
    {
        printf("FAIL %s: %s\n", path, err);
        return 1;
    }
    rev = GameProtoRev_Rsprox(rec.revision);
    if( !rev )
    {
        printf("SKIP %s: revision %d is outside 227-241\n", path, rec.revision);
        RsproxRecording_Free(&rec);
        return 0;
    }
    if( RsproxRecording_Index(&rec, rev, err, sizeof(err)) != 0 )
    {
        printf("FAIL %s (rev %d): %s\n", path, rec.revision, err);
        RsproxRecording_Free(&rec);
        return 1;
    }
    printf("%s: rev %d, %d server packets, %d client, %d ticks\n", path, rec.revision,
           rec.frame_count, rec.client_packet_count, rec.tick_count);

    rev->player_info_set_local(rec.local_player_index);
    for( int f = 0; f < rec.frame_count; f++ )
    {
        struct RsproxRecordingFrame const* fr = &rec.frames[f];
        uint8_t const* data = rec.data + fr->offset;
        int len = (int)fr->length;
        RsprotProtDef const* d = rsprox_server_prot(rsprox_rev(rev), fr->opcode);
        struct Tally* t;
        struct RevPacket packet;
        RsprotNpcInfoReadFn npc_read;
        int ok = -1;
        int parsed;

        if( !d )
        {
            printf("  frame %d: opcode %d has no prot\n", f, fr->opcode);
            failures++;
            continue;
        }
        if( getenv("MULTIREV_TRACE") )
            printf("  frame %d tick %d: %s, %d bytes\n", f, fr->tick, d->name, len);
        t = tally(d->name);
        t->count++;
        memset(&packet, 0, sizeof(packet));
        parsed = rev->parse_wire(rev, fr->opcode, data, len, &packet);
        if( parsed > 0 )
            t->parsed++;

        if( parsed > 0 && packet.packet_type == PKT_NAME_PLAYER_INFO )
        {
            int n = rev->player_info_read(packet._player_info.data, packet._player_info.length,
                                          g_pops, (int)(sizeof(g_pops) / sizeof(g_pops[0])));

            ok = rsprox_entity_info_last_ok();
            pkt_player_info_ops_free(g_pops, n);
        }
        else if( strncmp(d->name, "WORLDENTITY_INFO_V", 18) == 0 )
        {
            RsprotWorldEntityInfoReadFn read =
                rsprox_rev(rev)->worldentity_read[d->name[strlen(d->name) - 1] - '0'];
            int complete;

            read(data, len, g_ops, (int)(sizeof(g_ops) / sizeof(g_ops[0])), &complete);
            ok = complete;
            if( complete && parsed <= 0 )
                t->arm++;
        }
        else if( (npc_read = npc_reader(d->name, rec.revision)) != NULL )
        {
            int n = rsprox_npc_info_convert(npc_read, data, len, g_nops,
                                            (int)(sizeof(g_nops) / sizeof(g_nops[0])));

            ok = rsprox_entity_info_last_ok();
            pkt_npc_info_ops_free(g_nops, n);
        }
        else
        {
            ok = rsprox_parse_validate(rev, d->name, data, len);
            if( ok < 0 && packet.packet_type != PKT_NAME_NONE )
                ok = parsed > 0; /* no codec: the special parse is the check */
        }
        if( packet.packet_type == PKT_NAME_NONE )
            t->dropped++;
        if( ok > 0 )
            t->decoded++;
        else if( ok == 0 )
        {
            t->failed++;
            failures++;
            if( t->first_fail_frame < 0 )
            {
                t->first_fail_frame = f;
                t->first_fail_tick = fr->tick;
                if( verbose )
                {
                    printf("  first %s failure: frame %d tick %d, %d bytes:", d->name, f,
                           fr->tick, len);
                    for( int k = 0; k < len && k < 96; k++ )
                        printf(" %02x", data[k]);
                    printf("%s\n", len > 96 ? " ..." : "");
                }
            }
        }
        if( parsed > 0 )
            gameproto_free(&packet);
    }
    RsproxRecording_Free(&rec);
    return failures != 0;
}

int
main(int argc, char** argv)
{
    int failed = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    for( int i = 1; i < argc; i++ )
        failed |= check_one(argv[i], getenv("MULTIREV_VERBOSE") != NULL);

    printf("\n%-40s %7s %7s %7s %7s %7s %7s\n", "prot", "count", "decoded", "FAILED", "parsed",
           "dropped", "arm");
    for( int i = 0; i < g_tally_count; i++ )
    {
        struct Tally const* t = &g_tally[i];

        printf("%-40s %7d %7d %7d %7d %7d %7d%s\n", t->prot, t->count, t->decoded, t->failed,
               t->parsed, t->dropped, t->arm, t->failed ? "   <--" : "");
    }
    return failed ? 1 : 0;
}
