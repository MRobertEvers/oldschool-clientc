/*
 * replay-ledger-check: the world ledger on real RSProx recordings.
 *
 *   make -C src replay-ledger-check RECORDINGS="a.bin b.bin.gz"
 *
 * For each recording: build the ledger, then check at sampled ticks that the
 * stabbing query equals a scan of the intervals and the live ops rebuild the
 * world the full history builds (ReplayLedgerVerify_Zones), that a key
 * advanced to a tick equals one fold from tick 0 (ReplayLedgerVerify_Entities),
 * that the session's player table (net/proto/proto_info.c) is byte-identical
 * before and after, and that the ledger's private reader agrees with the
 * session's own (rev->player_info_read over g_pi, saved and put back) at every
 * key. Then a size report and keyframe timings.
 *
 * Not a suite test: it needs recordings, which are not in the tree.
 */
#include "replay/replay_ledger.h"
#include "replay/replay_ledger_verify.h"

#include "net/rev/gameproto_revisions.h"
#include "net/rev/revpacket.h"
#include "net/rsprox_recording.h"
#include "streams/rsprot_info.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double
now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* The session's table as the client would hold it through the whole
 * recording -- login index, the login REBUILD's seed (its parse), each
 * RECONNECT's re-seed, every PLAYER_INFO through rev->player_info_read -- is
 * compared with the ledger's key tables. g_pi is saved first and put back. */
static int
session_reader_agrees(
    struct ReplayLedger const* ledger,
    struct RsproxRecording const* rec,
    struct GameProtoRevTable const* rev,
    int* compared)
{
    static RsprotInfoOp ops[16384];
    static uint8_t bytes[1 << 18];
    RsprotInfoList list;
    size_t size = rev->player_info_state_size();
    uint8_t* saved = malloc(size);
    uint8_t* session = malloc(size);
    struct ReplayEntityState* keyed = ReplayLedger_EntityNew(ledger);
    struct ReplayWorldKeyframe kf;
    int key = 1;
    int ok = 1;

    assert(saved);
    assert(session);
    *compared = 0;
    rev->player_info_state_save(saved);
    rev->player_info_set_local(rec->local_player_index);
    rsprot_info_list_init(&list, ops, (int)(sizeof(ops) / sizeof(ops[0])), bytes, sizeof(bytes));
    for( int f = 0; f < rec->frame_count && ok; f++ )
    {
        struct RsproxRecordingFrame const* fr = &rec->frames[f];
        int name;

        /* A key sits in front of its tick's first frame. */
        while( key < ReplayLedger_KeyCount(ledger) &&
               ReplayLedger_FirstFrame(ledger, key * REPLAY_LEDGER_KEY_TICKS) == (uint32_t)f )
        {
            memset(&kf, 0, sizeof(kf));
            ReplayLedger_EntityLoadKey(ledger, key, keyed);
            rev->player_info_state_save(session);
            if( !ReplayLedger_WorldAt(ledger, key * REPLAY_LEDGER_KEY_TICKS, &kf) ||
                kf.player_table_size != size || memcmp(kf.player_table, session, size) != 0 )
            {
                printf("  the session's table and the ledger's key %d differ\n", key);
                ok = 0;
            }
            ReplayWorldKeyframe_Free(&kf);
            (*compared)++;
            key++;
        }
        if( fr->opcode == RSPROX_RECORDING_RECONNECT )
        {
            rev->player_info_set_local(fr->reconnect_index);
            rev->player_info_init(rec->data + fr->offset, (int)fr->length);
            continue;
        }
        if( fr->opcode == RSPROX_RECORDING_PACKET_GROUP_END )
            continue;
        name = rev->packetin_code(fr->opcode);
        if( name == PKT_NAME_REBUILD_NORMAL )
        {
            struct RevPacket pkt;

            memset(&pkt, 0, sizeof(pkt));
            rev->parse_wire(rev, fr->opcode, rec->data + fr->offset, (int)fr->length, &pkt);
            gameproto_free(&pkt);
        }
        else if( name == PKT_NAME_PLAYER_INFO )
            rev->player_info_read(rec->data + fr->offset, (int)fr->length, &list);
    }
    rev->player_info_state_load(saved);
    ReplayEntityState_Free(keyed);
    free(saved);
    free(session);
    return ok;
}

static int
check(char const* path)
{
    struct RsproxRecording rec;
    struct GameProtoRevTable const* rev;
    struct ReplayLedger* ledger;
    struct ReplayLedgerStats st;
    char err[512];
    uint8_t* before;
    uint8_t* after;
    size_t size;
    int failures = 0;
    int ticks;
    enum
    {
        SAMPLES = 40
    };
    int sample_ticks[SAMPLES + 8];
    int n = 0;

    memset(&rec, 0, sizeof(rec));
    if( RsproxRecording_OpenFile(&rec, path, err, sizeof(err)) != 0 )
    {
        printf("FAIL %s: %s\n", path, err);
        return 1;
    }
    rev = RsproxRecording_Rev(&rec);
    if( !rev )
    {
        printf("SKIP %s: revision %d (client type %d) has no executor\n", path, rec.revision,
               rec.client_type);
        RsproxRecording_Free(&rec);
        return 0;
    }
    if( RsproxRecording_Index(&rec, rev, err, sizeof(err)) != 0 )
    {
        printf("FAIL %s: %s\n", path, err);
        RsproxRecording_Free(&rec);
        return 1;
    }
    size = rev->player_info_state_size();
    before = malloc(size);
    after = malloc(size);
    assert(before);
    assert(after);
    /* The resolve trials leave g_pi as their last one left it (its contract);
     * what the ledger must not touch is whatever is there when it starts. */
    rsprox_recording_resolve_reconnects(rev, &rec);
    rev->player_info_state_save(before);
    {
        int reconnects = 0;

        for( int f = 0; f < rec.frame_count; f++ )
            reconnects += rec.frames[f].opcode == RSPROX_RECORDING_RECONNECT;
        printf("%s: %s, %d server packets, %d ticks, %d RECONNECTs\n", path, rev->name,
               rec.frame_count, rec.tick_count, reconnects);
    }

    ledger = ReplayLedger_Build(&rec, rev);
    ReplayLedger_Stats(ledger, &st);
    ticks = ReplayLedger_TickCount(ledger);

    {
        char why[512];

        if( !ReplayLedgerVerify_Intervals(ledger, why, sizeof(why)) )
        {
            printf("  FAIL intervals: %s\n", why);
            failures++;
        }
        else
            printf("  intervals: ok (%d ops)\n", st.zone_ops);
    }

    /* Sampled ticks: evenly over the recording, and the edges of a key. */
    for( int i = 0; i < SAMPLES; i++ )
        sample_ticks[n++] = (int)((long long)ticks * i / SAMPLES);
    sample_ticks[n++] = REPLAY_LEDGER_KEY_TICKS - 1 < ticks ? REPLAY_LEDGER_KEY_TICKS - 1 : 0;
    sample_ticks[n++] = REPLAY_LEDGER_KEY_TICKS < ticks ? REPLAY_LEDGER_KEY_TICKS : 0;
    sample_ticks[n++] = REPLAY_LEDGER_KEY_TICKS + 1 < ticks ? REPLAY_LEDGER_KEY_TICKS + 1 : 0;
    sample_ticks[n++] = ticks;
    for( int i = 1; i < n; i++ )
        for( int j = i; j > 0 && sample_ticks[j - 1] > sample_ticks[j]; j-- )
        {
            int t = sample_ticks[j];

            sample_ticks[j] = sample_ticks[j - 1];
            sample_ticks[j - 1] = t;
        }

    {
        char why[512];
        double t0 = now_ms();

        /* Zones are cheap to check: every 8th tick boundary. */
        int dense_count = ticks / 8 + 1;
        uint32_t* dense = malloc((size_t)dense_count * sizeof(*dense));

        assert(dense);
        for( int i = 0; i < dense_count; i++ )
            dense[i] = ReplayLedger_FirstFrame(ledger, i * 8);
        if( !ReplayLedgerVerify_Zones(ledger, dense, dense_count, why, sizeof(why)) )
        {
            printf("  FAIL zones: %s\n", why);
            failures++;
        }
        else
            printf("  zones: live set == history at %d tick boundaries, every 8th (%.0f ms)\n",
                   dense_count, now_ms() - t0);
        free(dense);
        t0 = now_ms();
        if( !ReplayLedgerVerify_Entities(ledger, sample_ticks, n, why, sizeof(why)) )
        {
            printf("  FAIL entities: %s\n", why);
            failures++;
        }
        else
            printf("  entities: key advanced == fold from 0 at %d sampled ticks (%.0f ms)\n", n,
                   now_ms() - t0);
    }

    {
        int compared = 0;

        if( !session_reader_agrees(ledger, &rec, rev, &compared) )
        {
            printf("  FAIL the session's reader and the ledger's disagree\n");
            failures++;
        }
        else
            printf("  private table == the session reader's at all %d keys after tick 0\n",
                   compared);
    }

    {
        double worst = 0;
        double total = 0;
        int samples = 0;
        struct ReplayWorldKeyframe kf;

        for( int i = 0; i < n; i++ )
        {
            double t0 = now_ms();
            int ok = ReplayLedger_WorldAt(ledger, sample_ticks[i], &kf);
            double ms = now_ms() - t0;

            if( !ok )
            {
                printf("  FAIL WorldAt(%d): a sticky block or zone op did not resolve\n",
                       sample_ticks[i]);
                failures++;
            }
            if( i == n / 2 )
                printf("  WorldAt(%d): scene %d (base %d,%d), %d zone ops, %d players, %d npcs, "
                       "%d sticky ops, world entity %d\n",
                       kf.tick, kf.scene, kf.scene_row.base_x, kf.scene_row.base_z,
                       kf.zone_op_count, kf.player_count, kf.npc_count, kf.op_count,
                       kf.world_entity);
            ReplayWorldKeyframe_Free(&kf);
            total += ms;
            worst = ms > worst ? ms : worst;
            samples++;
        }
        printf("  WorldAt: %.2f ms mean, %.2f ms worst over %d ticks\n", total / samples, worst,
               samples);
    }

    rev->player_info_state_save(after);
    if( memcmp(before, after, size) != 0 )
    {
        printf("  FAIL the session's player table changed\n");
        failures++;
    }
    else
        printf("  session player table: byte-identical before and after (%zu bytes)\n", size);

    printf("  size: %d scenes, %d zone ops (%d live at most, %d zone clears, %d checkpoints), "
           "%d keys x %zu bytes (table %zu), %d player sticky copies (appearance: %d referenced in "
           "the recording, %d copied), total %.2f MB\n",
           st.scenes, st.zone_ops, st.zone_ops_live_max, st.zone_clears, st.checkpoints, st.keys,
           st.bytes_per_key, st.table_bytes, st.player_sticky_copies, st.appearance_refs,
           st.appearance_copies,
           (double)st.total_bytes / (1024.0 * 1024.0));
    printf("  decode: %d PLAYER_INFO (%d incomplete), %d NPC_INFO (%d incomplete); "
           "world-entity ticks %d, npc infos skipped %d, zone packets skipped %d; zone ops "
           "outside the scene %d, before a scene %d\n",
           st.player_info_frames, st.player_info_incomplete, st.npc_info_frames,
           st.npc_info_incomplete, st.world_entity_ticks, st.npc_info_world_entity,
           st.zone_world_entity, st.zone_outside_scene, st.zone_no_scene);
    {
        static char const* const names[] = { "LOC_ADD_CHANGE", "LOC_DEL", "OBJ_ADD", "OBJ_DEL",
                                             "OBJ_COUNT", "OBJ_REVEAL", "OBJ_CUSTOMISE",
                                             "OBJ_UNCUSTOMISE" };
        int const arms[] = { PKT_NAME_LOC_ADD_CHANGE, PKT_NAME_LOC_DEL, PKT_NAME_OBJ_ADD,
                             PKT_NAME_OBJ_DEL, PKT_NAME_OBJ_COUNT, PKT_NAME_OBJ_REVEAL,
                             PKT_NAME_OBJ_CUSTOMISE, PKT_NAME_OBJ_UNCUSTOMISE };
        int counts[8] = { 0 };
        int enclosed = 0;

        for( int i = 0; i < st.zone_ops; i++ )
        {
            struct ReplayZoneOp const* op = ReplayLedger_ZoneOp(ledger, i);

            for( int k = 0; k < 8; k++ )
                counts[k] += op->name == arms[k];
            enclosed += rev->packetin_code(rec.frames[op->born].opcode) ==
                        PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED;
        }
        printf("  zone ops by arm:");
        for( int k = 0; k < 8; k++ )
            printf(" %s %d", names[k], counts[k]);
        printf("; %d from UPDATE_ZONE_PARTIAL_ENCLOSED\n", enclosed);
    }
    printf("  build: %.0f ms\n", st.build_ms);
    printf("%s %s\n", failures ? "FAIL" : "PASS", path);

    ReplayLedger_Free(ledger);
    free(before);
    free(after);
    RsproxRecording_Free(&rec);
    return failures ? 1 : 0;
}

int
main(
    int argc,
    char** argv)
{
    int failures = 0;

    if( argc < 2 )
    {
        printf("usage: replay_ledger_check <recording.bin[.gz]> ...\n");
        return 2;
    }
    for( int i = 1; i < argc; i++ )
        failures += check(argv[i]);
    return failures ? 1 : 0;
}
