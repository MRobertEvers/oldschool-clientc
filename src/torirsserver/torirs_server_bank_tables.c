/*
 * The bank's CACHE facts: which inv holds how many slots, and where a varbit
 * sits inside its varp.
 *
 * Split out of `torirs_server_bank.c` so a binary can read them without linking a
 * bank. That is not a tidiness argument — it is what `ToriRSServer_Pack` needs.
 *
 * The validator loads the content tree and checks it against the cache, and one
 * of its checks is that an `.inv` block does not restate a size the cache
 * already states (`torirs_server_content.c`, "inv size is a cache fact (config group
 * 5)"). That check reads `ToriRSServer_BankInvSize`. With the whole of
 * `torirs_server_bank.c` behind that symbol the validator would have to link the
 * bank's *wire* half too — IF_OPENSUB, UPDATE_INV_FULL, the container flush,
 * and from there the encoder and the server — none of which it has or wants.
 *
 * Stubbing the symbol instead was the other option and is the worse one: a stub
 * returns 0 for every inv, the check never fires, and a `size=` line that
 * contradicts the cache validates clean forever. The check would still be in
 * the source, still be read as coverage, and mean nothing.
 *
 * Nothing here sends a packet, touches a player, or needs a server. Everything
 * that does stayed in `torirs_server_bank.c`.
 */
#include "torirs_server_servpack.h"
#include "torirs_server_bank.h"

#include "torirs_server.h"
#include "torirs_server_ids.h"

#include <rscache.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Cache-derived tables                                                */
/* ------------------------------------------------------------------ */

struct BankVarbit
{
    int16_t basevar;
    int8_t lsb;
    int8_t msb;
};

/** Indexed by varbit id; basevar -1 means "no such record". */
static struct BankVarbit* g_varbits;
static int g_varbit_count;

/** Indexed by inv id; 0 means "no such record". */
static int* g_inv_sizes;
static int g_inv_count;

static void
load_inv_sizes(const struct ToriRSServerKindRecords* records)
{
    g_inv_count = ToriRSServer_ServPackKindIdBound(records);
    g_inv_sizes = calloc((size_t)(g_inv_count > 0 ? g_inv_count : 1), sizeof(*g_inv_sizes));
    assert(g_inv_sizes);

    for( int i = 0; i < records->count; i++ )
    {
        struct RSCache_Dat2ConfigInv inv;

        memset(&inv, 0, sizeof(inv));
        RSCache_Dat2ConfigInvDecodeInplace(&inv, records->files[i], (int)records->sizes[i]);
        g_inv_sizes[records->ids[i]] = inv.size;
        RSCache_Dat2ConfigInvFreeInplace(&inv);
    }
}

static int
load_varbits(const struct ToriRSServerKindRecords* records)
{
    int loaded = 0;

    g_varbit_count = ToriRSServer_ServPackKindIdBound(records);
    g_varbits = calloc((size_t)(g_varbit_count > 0 ? g_varbit_count : 1), sizeof(*g_varbits));
    assert(g_varbits);
    for( int i = 0; i < g_varbit_count; i++ )
        g_varbits[i].basevar = -1;

    for( int i = 0; i < records->count; i++ )
    {
        int file_id = records->ids[i];
        struct RSCache_Dat2ConfigVarbit varbit;

        memset(&varbit, 0, sizeof(varbit));
        RSCache_Dat2ConfigVarbitDecodeInplace(&varbit, records->files[i], (int)records->sizes[i]);
        if( varbit.basevar < 0 || varbit.startbit < 0 || varbit.endbit > 31 ||
            varbit.endbit < varbit.startbit )
        {
            free(varbit.debugname);
            continue;
        }
        g_varbits[file_id].basevar = (int16_t)varbit.basevar;
        g_varbits[file_id].lsb = (int8_t)varbit.startbit;
        g_varbits[file_id].msb = (int8_t)varbit.endbit;
        free(varbit.debugname);
        loaded++;
    }
    return loaded;
}

/*
 * Inv sizes and varbit ranges, from the server pack's client records -- the
 * merged tree, so an authored inv's `size=` and an authored varbit reach the
 * bank exactly as they reach the client.
 */
int
ToriRSServer_BankLoad(struct RSCache_ServerPack* pack)
{
    struct ToriRSServerKindRecords invs;
    struct ToriRSServerKindRecords varbits;
    int loaded;

    assert(pack);
    ToriRSServer_BankFree();
    if( !ToriRSServer_ServPackKindLoad(pack, RSCACHE_DAT2_CONFIG_KIND_INV, &invs) )
        return -1;
    if( !ToriRSServer_ServPackKindLoad(pack, RSCACHE_DAT2_CONFIG_KIND_VARBIT, &varbits) )
    {
        ToriRSServer_ServPackKindFree(&invs);
        return -1;
    }
    load_inv_sizes(&invs);
    loaded = load_varbits(&varbits);
    ToriRSServer_ServPackKindFree(&invs);
    ToriRSServer_ServPackKindFree(&varbits);

    fprintf(stderr, "torirsserver: bank tables loaded (%d varbits, bank=%d slots)\n", loaded,
            ToriRSServer_BankInvSize(ToriRSServer_Ids()->inv_bank));
    return loaded;
}

void
ToriRSServer_BankFree(void)
{
    free(g_varbits);
    g_varbits = NULL;
    g_varbit_count = 0;
    free(g_inv_sizes);
    g_inv_sizes = NULL;
    g_inv_count = 0;
}

int
ToriRSServer_BankInvSize(int inv_id)
{
    if( !g_inv_sizes || inv_id < 0 || inv_id >= g_inv_count )
        return 0;
    return g_inv_sizes[inv_id];
}

int
ToriRSServer_BankVarbitResolve(
    int varbit_id,
    int* basevar,
    int* lsb,
    int* msb)
{
    if( !g_varbits || varbit_id < 0 || varbit_id >= g_varbit_count )
        return 0;
    if( g_varbits[varbit_id].basevar < 0 )
        return 0;
    *basevar = g_varbits[varbit_id].basevar;
    *lsb = g_varbits[varbit_id].lsb;
    *msb = g_varbits[varbit_id].msb;
    return 1;
}