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

/*
 * A file list for one config group, plus the archive it borrows its buffers
 * from. Both have to stay alive until the caller is done decoding, which is why
 * this returns the pair rather than just the list.
 */
struct ConfigGroup
{
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
};

static int
config_group_open(
    struct RSCache_Dat2Disk* disk,
    int kind,
    struct ConfigGroup* out)
{
    int table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);

    out->archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, kind);
    out->files = NULL;
    if( !out->archive || !RSCache_Dat2DiskArchiveInitMetadata(disk, out->archive) ||
        out->archive->file_count <= 0 )
    {
        if( out->archive )
            RSCache_Dat2DiskArchiveFree(out->archive);
        out->archive = NULL;
        return 0;
    }
    out->files = RSCache_FileListNewFromDecode(
        out->archive->data, out->archive->data_size, out->archive->file_count);
    if( !out->files )
    {
        RSCache_Dat2DiskArchiveFree(out->archive);
        out->archive = NULL;
        return 0;
    }
    return 1;
}

static void
config_group_close(struct ConfigGroup* group)
{
    if( group->files )
        RSCache_FileListFree(group->files);
    if( group->archive )
        RSCache_Dat2DiskArchiveFree(group->archive);
    group->files = NULL;
    group->archive = NULL;
}

/** file_ids are sparse, so a table has to be sized from the largest one. */
static int
config_group_max_id(const struct ConfigGroup* group)
{
    int max_id = 0;

    for( int i = 0; i < group->files->file_count; i++ )
    {
        int file_id = (group->archive->file_ids && i < group->archive->file_count)
                          ? group->archive->file_ids[i]
                          : i;
        if( file_id + 1 > max_id )
            max_id = file_id + 1;
    }
    return max_id;
}

static void
load_inv_sizes(struct RSCache_Dat2Disk* disk)
{
    struct ConfigGroup group;

    if( !config_group_open(disk, RSCACHE_DAT2_CONFIG_KIND_INV, &group) )
        return;

    g_inv_count = config_group_max_id(&group);
    g_inv_sizes = calloc((size_t)(g_inv_count > 0 ? g_inv_count : 1), sizeof(*g_inv_sizes));
    assert(g_inv_sizes);

    for( int i = 0; i < group.files->file_count; i++ )
    {
        int file_id = (group.archive->file_ids && i < group.archive->file_count)
                          ? group.archive->file_ids[i]
                          : i;
        struct RSCache_Dat2ConfigInv inv;
        struct RSCache_Buffer buffer;

        if( group.files->file_sizes[i] <= 0 || file_id < 0 || file_id >= g_inv_count )
            continue;
        memset(&inv, 0, sizeof(inv));
        RSCache_BufferInit(&buffer, (uint8_t*)group.files->files[i], group.files->file_sizes[i]);
        RSCache_Dat2ConfigInvDecode(&inv, &buffer);
        g_inv_sizes[file_id] = inv.size;
    }
    config_group_close(&group);
}

static int
load_varbits(struct RSCache_Dat2Disk* disk)
{
    struct ConfigGroup group;
    int loaded = 0;

    if( !config_group_open(disk, RSCACHE_DAT2_CONFIG_KIND_VARBIT, &group) )
        return 0;

    g_varbit_count = config_group_max_id(&group);
    g_varbits = calloc((size_t)(g_varbit_count > 0 ? g_varbit_count : 1), sizeof(*g_varbits));
    assert(g_varbits);
    for( int i = 0; i < g_varbit_count; i++ )
        g_varbits[i].basevar = -1;

    for( int i = 0; i < group.files->file_count; i++ )
    {
        int file_id = (group.archive->file_ids && i < group.archive->file_count)
                          ? group.archive->file_ids[i]
                          : i;
        struct RSCache_Dat2ConfigVarbit varbit;
        struct RSCache_Buffer buffer;

        if( group.files->file_sizes[i] <= 0 || file_id < 0 || file_id >= g_varbit_count )
            continue;
        memset(&varbit, 0, sizeof(varbit));
        RSCache_BufferInit(&buffer, (uint8_t*)group.files->files[i], group.files->file_sizes[i]);
        RSCache_Dat2ConfigVarbitDecode(&varbit, &buffer);
        if( varbit.basevar < 0 || varbit.startbit < 0 || varbit.endbit > 31 ||
            varbit.endbit < varbit.startbit )
            continue;
        g_varbits[file_id].basevar = (int16_t)varbit.basevar;
        g_varbits[file_id].lsb = (int8_t)varbit.startbit;
        g_varbits[file_id].msb = (int8_t)varbit.endbit;
        loaded++;
    }
    config_group_close(&group);
    return loaded;
}

int
ToriRSServer_BankLoad(const char* cache_dir)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    int loaded;

    ToriRSServer_BankFree();

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = TORIRSSERVER_CACHE_REVISION;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        /* Same ../ fallback as ToriRSServer_ObjInfoLoad: the binary is run both
         * from the repo root and from src/. */
        char parent[512];
        snprintf(parent, sizeof(parent), "../%s", cache_dir);
        disk = RSCache_Dat2DiskNewFromDirectory(parent);
    }
    if( !disk )
    {
        fprintf(stderr,
                "torirsserver: no cache at %s — bank varbits unavailable "
                "(settings cannot be pushed to the interface)\n",
                cache_dir);
        return 0;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);

    load_inv_sizes(disk);
    loaded = load_varbits(disk);
    RSCache_Dat2DiskFree(disk);

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