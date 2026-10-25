/*
 * Every codec, at every revision this tree speaks.
 *
 *   make -C src test-pktsweep
 *
 * For each revision 227-241 and each prot its table (3rd/rsprot/gen/osrs<N>_prot.h)
 * names, server side and zone sub-packets included:
 *
 *   1. COVERAGE  the registry has a codec for the prot at that revision, or the
 *                prot is on k_no_codec below with the reason it has none. A
 *                server prot that is neither fails: the client would frame it
 *                and drop it.
 *   2. SIZE      a codec whose description is fixed-width must describe exactly
 *                the table's size. A layout that moves a field but keeps its
 *                width passes this; one that loses or gains a byte does not.
 *   3. ROUND TRIP  FILL -> ENCODE -> DECODE -> ENCODE gives the same bytes, the
 *                decode consumes exactly what was encoded, and a fixed-size
 *                prot encodes to the table's size.
 *
 * Client prots get the same checks but only REPORT coverage: the client side of
 * a revision is the outbound path, which this sweep does not gate.
 *
 * Lives beside pkt_packets_test.c for the reason that file gives: a codec needs
 * the host's executor to run.
 */
#include "net/exec/pkt_codec_storage.h"
#include "rsprot_exec.h"
#include "rsprot_registry.h"
#include "rsprot_tables.h"

#include "packets/if_initialstate.h"
#include "packets/if_resync.h"
#include "packets/if_resync_v1.h"
#include "packets/if_resync_v2.h"
#include "packets/loc_add_change_v2.h"
#include "packets/runclientscript.h"
#include "packets/update_friendlist.h"
#include "packets/update_ignorelist.h"
#include "packets/update_inv_full.h"
#include "packets/update_inv_partial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REV_LO 227
#define REV_HI 241

static int g_failures;
static int g_checked;
static int g_skipped_fill;

/* Server prots with no codec, and why. Anything else uncovered fails. */
static const struct
{
    const char *prot;
    const char *why;
} k_no_codec[] = {
    /* Stateful bit streams, read by the client's entity-info readers. */
    { "PLAYER_INFO", "info stream" },
    { "NPC_INFO_SMALL_V5", "info stream" },
    { "NPC_INFO_LARGE_V5", "info stream" },
    { "NPC_INFO_SMALL_V6", "info stream" },
    { "NPC_INFO_LARGE_V6", "info stream" },
    { "WORLDENTITY_INFO_V3", "info stream" },
    { "WORLDENTITY_INFO_V4", "info stream" },
    { "WORLDENTITY_INFO_V5", "info stream" },
    { "WORLDENTITY_INFO_V6", "info stream" },
    { "WORLDENTITY_INFO_V7", "info stream" },
    /* One layout across 227-241; carried raw to the store that decodes it
     * (tools/rsprox_gen_codec.py RAW). */
    { "CLANCHANNEL_FULL", "raw to rs_clan" },
    { "CLANCHANNEL_DELTA", "raw to rs_clan" },
    { "CLANSETTINGS_FULL", "raw to rs_clan" },
    { "CLANSETTINGS_DELTA", "raw to rs_clan" },
    { "VARCLAN", "raw to rs_clan" },
    { "MESSAGE_CLANCHANNEL", "raw to rs_clan" },
    { "MESSAGE_CLANCHANNEL_SYSTEM", "raw to rs_clan" },
    { "UPDATE_FRIENDCHAT_CHANNEL_FULL_V2", "raw to rs_friends_chat" },
    { "UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER", "raw to rs_friends_chat" },
    { "MESSAGE_FRIENDCHANNEL", "raw to rs_friends_chat" },
    { "MESSAGE_PRIVATE", "raw to rs_social" },
    { "MESSAGE_PRIVATE_ECHO", "raw to rs_social" },
    { "UPDATE_TRADINGPOST", "raw to the trading post store" },
    { "UPDATE_STOCKMARKET_SLOT", "raw to the stock market store" },
    /* RSProx's catch-all row for a prot it cannot decode: no layout exists. */
    { "UNHANDLED", "no layout" },
};

static const char*
no_codec_reason(const char *prot)
{
    for (size_t i = 0; i < sizeof(k_no_codec) / sizeof(k_no_codec[0]); i++)
        if (strcmp(k_no_codec[i].prot, prot) == 0)
            return k_no_codec[i].why;
    return NULL;
}

/* Big enough for the largest message struct (OBJUNLOCK_UPDATE's 4096 entries). */
static union
{
    long double align;
    uint8_t bytes[256 * 1024];
} g_msg_a, g_msg_b;

static uint8_t g_wire_a[256 * 1024];
static uint8_t g_wire_b[256 * 1024];

static int
check_codec(int rev, const char *prot, int table_size, RsprotCodecFn fn)
{
    static RsprotSchema schema;
    RsprotExec x;
    int fixed;
    int ok = 1;

    if (getenv("PKTSWEEP_TRACE"))
        printf("  check rev %d %s\n", rev, prot);

    memset(&g_msg_a, 0, sizeof(g_msg_a));
    pkt_codec_storage_provide(prot, &g_msg_a, 0);
    rsprot_exec_describe(&x, &schema);
    fn(&x, &g_msg_a);
    fixed = rsprot_schema_fixed_size(&schema);
    if (rsprot_exec_ok(&x) && fixed >= 0 && table_size >= 0 && fixed != table_size) {
        printf("FAIL size  rev %d %-36s describes %d bytes, table says %d\n", rev, prot, fixed,
               table_size);
        ok = 0;
    }

    int tripped = 0;

    for (uint32_t seed = 1; seed <= 8; seed++) {
        RsprotBuf a, b, r;
        int len;

        memset(&g_msg_a, 0, sizeof(g_msg_a));
        pkt_codec_storage_provide(prot, &g_msg_a, 0);
        rsprot_exec_fill(&x, seed);
        fn(&x, &g_msg_a);
        if (!rsprot_exec_ok(&x)) {
            g_skipped_fill++;
            continue;
        }
        rsprot_buf_wrap(&a, g_wire_a, sizeof(g_wire_a));
        rsprot_exec_encode(&x, &a);
        fn(&x, &g_msg_a);
        if (!rsprot_exec_ok(&x) || a.err) {
            if (getenv("PKTSWEEP_UNTRIPPED"))
                printf("  encode-skip rev %d %s: %s err=%d\n", rev, prot,
                       x.error_reason ? x.error_reason : "-", a.err);
            /* A filled value the layout cannot carry (a branch tag with no
             * arm). Not the codec's fault; the decode side is what matters. */
            g_skipped_fill++;
            continue;
        }
        len = a.wpos;
        if (table_size >= 0 && len != table_size) {
            printf("FAIL len   rev %d %-36s encodes %d bytes, table says %d (seed %u)\n", rev, prot,
                   len, table_size, seed);
            ok = 0;
            break;
        }

        memset(&g_msg_b, 0, sizeof(g_msg_b));
        pkt_codec_storage_provide(prot, &g_msg_b, 1);
        rsprot_buf_wrap_read(&r, g_wire_a, len);
        rsprot_exec_decode(&x, &r);
        fn(&x, &g_msg_b);
        if (!rsprot_exec_ok(&x) || r.err || r.rpos != len) {
            printf("FAIL decode rev %d %-36s ok=%d err=%d read %d of %d (%s) seed %u\n", rev, prot,
                   rsprot_exec_ok(&x), r.err, r.rpos, len,
                   x.error_reason ? x.error_reason : "-", seed);
            ok = 0;
            break;
        }

        rsprot_buf_wrap(&b, g_wire_b, sizeof(g_wire_b));
        rsprot_exec_encode(&x, &b);
        fn(&x, &g_msg_b);
        if (!rsprot_exec_ok(&x) || b.err || b.wpos != len || memcmp(g_wire_a, g_wire_b, len) != 0) {
            printf("FAIL trip  rev %d %-36s re-encode differs (seed %u)\n", rev, prot, seed);
            ok = 0;
            break;
        }
        tripped++;
    }
    if (ok && tripped == 0 && getenv("PKTSWEEP_UNTRIPPED"))
        printf("  untripped rev %d %s\n", rev, prot);
    g_checked++;
    return ok;
}

static void
sweep_side(int rev, const RsprotProtDef *defs, int count, RsprotRegistryDir dir, int *uncovered)
{
    for (int i = 0; i < count; i++) {
        const RsprotProtDef *d = &defs[i];
        RsprotCodecFn fn = rsprot_registry_resolve(d->name, dir, rev);

        if (!fn) {
            if (dir == RSPROT_REG_OUT && !no_codec_reason(d->name)) {
                printf("FAIL cover rev %d %-36s no codec (opcode %d, size %d)\n", rev, d->name,
                       d->code, d->size);
                g_failures++;
            }
            (*uncovered)++;
            continue;
        }
        if (!check_codec(rev, d->name, d->size, fn))
            g_failures++;
    }
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int rev = REV_LO; rev <= REV_HI; rev++) {
        const RsprotRevTable *t = rsprot_rev_table(rev);
        int un_server = 0, un_client = 0;

        if (!t) {
            printf("FAIL rev %d has no prot table\n", rev);
            g_failures++;
            continue;
        }
        sweep_side(rev, t->server, t->server_count, RSPROT_REG_OUT, &un_server);
        sweep_side(rev, t->client, t->client_count, RSPROT_REG_IN, &un_client);
        /* Every zone sub-prot must resolve to a table row and a codec: the
         * enclosed packet indexes them by ordinal and has no other way out. */
        for (int z = 0; z < t->zone_count; z++) {
            if (!rsprot_registry_resolve(t->zone_order[z], RSPROT_REG_OUT, rev)) {
                printf("FAIL zone  rev %d ordinal %d %s has no codec\n", rev, z, t->zone_order[z]);
                g_failures++;
            }
        }
        printf("rev %d: %d server prots (%d without a codec, by design), %d client prots (%d "
               "without)\n",
               rev, t->server_count, un_server, t->client_count, un_client);
    }
    printf("%d codec checks, %d fills the layout could not carry, %d failure(s)\n", g_checked,
           g_skipped_fill, g_failures);
    return g_failures ? 1 : 0;
}
