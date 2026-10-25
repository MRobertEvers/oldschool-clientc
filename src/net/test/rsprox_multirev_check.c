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
 * MULTIREV_VERBOSE=1 prints each prot's first failure and its bytes;
 * MULTIREV_HEX=<PROT> prints every <PROT> payload;
 * MULTIREV_EXPLAIN=N the first N PLAYER_INFO failures with the ops the library
 * reader wrote before it stopped; MULTIREV_TRACE=1 every
 * frame before it is decoded (the last line names a frame that crashes).
 */
#include "net/rev/gameproto_parse.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/rsprot_entity_state.h"
#include "net/rev/revpacket.h"
#include "net/rev/rsprox/rsprox_rev.h"
#include "net/rsprox_recording.h"
#include "info_op_dump.h"

#include <assert.h>
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

static int g_explain;
/* MULTIREV_DUMP_FRAME=f: explain frame f whether it fails or not, every op. */
static int g_dump_frame = -1;
static int g_explain_ops = 12;
/* MULTIREV_DUMP_WHO=i: every op of player slot i, in every PLAYER_INFO. */
static int g_dump_who = -1;
static int g_frame;
static uint8_t const* g_explain_data;
static int g_explain_len;

/* Every npc stream prot's revision -> entry point lookup. */
static struct
{
    char const* prot;
    RsprotNpcInfoReadFn (*osrs)(int revision);
} const k_npc_streams[] = {
    { "NPC_INFO_SMALL_V5", npc_info_small_v5_osrs },
    { "NPC_INFO_LARGE_V5", npc_info_large_v5_osrs },
    { "NPC_INFO_SMALL_V6", npc_info_small_v6_osrs },
    { "NPC_INFO_LARGE_V6", npc_info_large_v6_osrs },
};

/* `prot`'s entry point at `revision`, or NULL when `prot` is not an npc
 * stream. */
static RsprotNpcInfoReadFn
npc_reader(char const* prot, int revision)
{
    for( size_t i = 0; i < sizeof(k_npc_streams) / sizeof(k_npc_streams[0]); i++ )
    {
        RsprotNpcInfoReadFn fn;

        if( strcmp(k_npc_streams[i].prot, prot) != 0 )
            continue;
        fn = k_npc_streams[i].osrs(revision);
        assert(fn);
        return fn;
    }
    return NULL;
}

/* The table as it stood before the PLAYER_INFO being decoded. */
static uint8_t g_player_state[1 << 16];
static RsprotInfoOp g_ops[16384];
static uint8_t g_bytes[1 << 20];
static RsprotInfoList g_list;

/*
 * MULTIREV_EXPLAIN=N: for the first N PLAYER_INFO failures, read the packet
 * again from the saved table, and print the ops it wrote up to where it
 * stopped.
 */
static void
explain_player_info(struct GameProtoRevTable const* rev)
{
    uint8_t now[sizeof(g_player_state)];
    int n;

    /* The table is the session's: park it, read from the saved one, put it
     * back. */
    rev->player_info_state_save(now);
    rev->player_info_state_load(g_player_state);
    n = rev->player_info_read(g_explain_data, g_explain_len, &g_list);
    rev->player_info_state_load(now);
    if( g_dump_who < 0 )
        printf("    %d ops, complete %d; the last:\n", n, rsprot_info_list_complete(&g_list));
    for( int i = n > g_explain_ops ? n - g_explain_ops : 0; i < n; i++ )
    {
        RsprotInfoOp const* o = &g_ops[i];

        if( g_dump_who >= 0 && o->who != g_dump_who && o->kind != RSPROT_OP_STOP )
            continue;
        if( g_dump_who >= 0 )
            printf("  frame %d:", g_frame);
        if( o->kind == RSPROT_OP_STOP )
            printf("      [%d] STOP who %d reason %d at byte %d of %d, flags 0x%x, block kind %d "
                   "(0x%x)\n",
                   i, o->who, o->stop.reason, o->stop.rpos, g_explain_len, o->stop.flags,
                   o->stop.kind, o->stop.block_flag);
        else
        {
            printf("      [%d] ", i);
            dump_info_ops(stdout, o, 1, 0);
        }
    }
}

/* MULTIREV_DUMP_OPS=<file>: every PLAYER_INFO / NPC_INFO op list the
 * executor would read, as text (info_op_dump.h). */
static FILE* g_dump;

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

    rsprox_recording_resolve_reconnects(rev, &rec);
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

        /* RSProx's reconnect: the table restarts from the GPI init block, as
         * ReconnectDecoder's gpiInit does. */
        if( fr->opcode == RSPROX_RECORDING_RECONNECT )
        {
            rev->player_info_set_local(fr->reconnect_index);
            rev->player_info_init(data, len);
            continue;
        }
        if( fr->opcode == RSPROX_RECORDING_PACKET_GROUP_END )
            continue;
        if( !d )
        {
            printf("  frame %d: opcode %d has no prot (payload at byte %u, %u bytes)\n", f,
                   fr->opcode, fr->offset, fr->length);
            failures++;
            continue;
        }
        if( getenv("MULTIREV_TRACE") )
            printf("  frame %d tick %d: %s, %d bytes\n", f, fr->tick, d->name, len);
        if( getenv("MULTIREV_HEX") && strcmp(getenv("MULTIREV_HEX"), d->name) == 0 )
        {
            printf("  frame %d tick %d %s:", f, fr->tick, d->name);
            for( int k = 0; k < len && k < 48; k++ )
                printf(" %02x", data[k]);
            printf("\n");
        }
        t = tally(d->name);
        t->count++;
        memset(&packet, 0, sizeof(packet));
        parsed = rev->parse_wire(rev, fr->opcode, data, len, &packet);
        if( parsed > 0 )
            t->parsed++;

        if( parsed > 0 && packet.packet_type == PKT_NAME_PLAYER_INFO )
        {
            assert(rev->player_info_state_size() <= sizeof(g_player_state));
            rev->player_info_state_save(g_player_state);
            int n = rev->player_info_read(packet._player_info.data, packet._player_info.length,
                                          &g_list);

            ok = rsprot_info_list_complete(&g_list);
            if( g_dump )
            {
                fprintf(g_dump, "P %d %d\n", f, ok);
                dump_info_ops(g_dump, g_ops, n, 0);
            }
            if( g_dump_who >= 0 )
            {
                g_explain_data = data;
                g_explain_len = len;
                g_explain_ops = 100000;
                g_frame = f;
                explain_player_info(rev);
            }
            if( f == g_dump_frame )
            {
                g_explain_data = data;
                g_explain_len = len;
                g_explain_ops = 100000;
                printf("  frame %d (%d bytes), ok %d:\n", f, len, ok);
                explain_player_info(rev);
                g_explain_ops = 12;
            }
        }
        else if( strncmp(d->name, "WORLDENTITY_INFO_V", 18) == 0 )
        {
            int v = d->name[strlen(d->name) - 1] - '0';

            rsprox_rev(rev)->worldentity_read[v](data, len, &g_list);
            ok = rsprot_info_list_complete(&g_list);
            if( ok && parsed <= 0 )
                t->arm++;
        }
        else if( (npc_read = npc_reader(d->name, rec.revision)) != NULL )
        {
            int n = rsprot_entity_npc_read(npc_read, data, len, &g_list);

            ok = rsprot_info_list_complete(&g_list);
            if( g_dump )
            {
                fprintf(g_dump, "N %d %d\n", f, ok);
                dump_info_ops(g_dump, g_ops, n, 1);
            }
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
            if( packet.packet_type == PKT_NAME_PLAYER_INFO && t->failed <= g_explain )
            {
                printf("  %s failure %d: frame %d tick %d, %d bytes:", d->name, t->failed, f,
                       fr->tick, len);
                for( int k = 0; k < len && k < 160; k++ )
                    printf(" %02x", data[k]);
                printf("%s\n", len > 160 ? " ..." : "");
                g_explain_data = data;
                g_explain_len = len;
                explain_player_info(rev);
            }
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
    rsprot_info_list_init(&g_list, g_ops, (int)(sizeof(g_ops) / sizeof(g_ops[0])), g_bytes,
                          sizeof(g_bytes));
    g_explain = getenv("MULTIREV_EXPLAIN") ? atoi(getenv("MULTIREV_EXPLAIN")) : 0;
    g_dump_frame = getenv("MULTIREV_DUMP_FRAME") ? atoi(getenv("MULTIREV_DUMP_FRAME")) : -1;
    g_dump_who = getenv("MULTIREV_DUMP_WHO") ? atoi(getenv("MULTIREV_DUMP_WHO")) : -1;
    if( getenv("MULTIREV_DUMP_OPS") )
    {
        g_dump = fopen(getenv("MULTIREV_DUMP_OPS"), "w");
        assert(g_dump);
    }
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
    if( g_dump )
        fclose(g_dump);
    return failed ? 1 : 0;
}
