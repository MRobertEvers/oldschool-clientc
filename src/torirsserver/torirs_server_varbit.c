/*
 * Varbits: named bit ranges inside player variables.
 *
 * The ranges come from the cache (config group 14) and nowhere else. That is
 * deliberate — the *client* reads the same records to unpack what the server
 * packs, so authoring them again in a config would be a second source of truth
 * with nothing keeping the two in step.
 *
 * What content owns is the *names*: content/pack/varbit.pack maps
 * `weapon_category` to 357 and `combat_level` to 13027, imported from
 * OpenRune's gameval table like every other id.
 *
 * Same one-pass decode as ToriRSServer_ObjInfo: the group is 13,876 records at rev
 * 230 and decodes in a few milliseconds, so it is read once at startup rather
 * than lazily per id.
 */
#include "torirs_server.h"
#include <assert.h>

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct VarbitRange
{
    int basevar;
    uint8_t startbit;
    uint8_t endbit;
};

static struct VarbitRange* g_varbits;
static int g_varbit_count;

/*
 * How many varbits are based on each varp — the *carrier* set, built once here
 * because it is the same table read backwards.
 *
 * Nothing needed it until the port did. `docs/LOSTCITY_PORT_TRIAGE.md` §7.5:
 * a 2004 varp is very often a rev-230 varbit range, so a whole-varp write is a
 * write to somebody else's state, and it compiles, runs, transmits and corrupts.
 * sscompile refuses that write at compile time off `configs/all.varbit`; this is
 * the same fact at runtime, off the cache, for the writers a compiler cannot see.
 */
static uint16_t* g_carrier_bits;
static int g_carrier_count;

/** The highest varp any loaded varbit is based on; -1 until one is. */
static int g_max_basevar = -1;

/** Size of the varplayer config group the active client actually loads. */
static int g_client_varp_count;

/** Non-zero while ToriRSServer_VarbitSet is patching a base varp — a varbit write
 *  reaching ToriRSServer_WorldSetVarp is the correct path, not a violation. */
static int g_patching;

int
ToriRSServer_VarbitLoad(const char* cache_dir)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table;
    int highest = -1;
    int loaded = 0;

    ToriRSServer_VarbitFree();

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = TORIRSSERVER_CACHE_REVISION;

    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        char parent[512];

        snprintf(parent, sizeof(parent), "../%s", cache_dir);
        disk = RSCache_Dat2DiskNewFromDirectory(parent);
    }
    if( !disk )
    {
        fprintf(stderr, "torirsserver: no varbit table (cache '%s' not found)\n", cache_dir);
        return 0;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);

    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);

    /*
     * Measure the client's varp array from the active cache, separately from
     * the highest varbit carrier below.  Base cache.osrs239 has varp records
     * 0..5704 while a few varbits name carriers through 5724.  The official
     * client sizes its array from the former: transmitting carrier 5705 is an
     * immediate ArrayIndexOutOfBoundsException.  Overlay caches such as the
     * Ancient Curses lane add real records at 5705/5706, so a constant cannot
     * express the boundary correctly for both lanes.
     */
    {
        struct RSCache_Dat2DiskArchive* varps = RSCache_Dat2DiskArchiveNewLoad(
            disk, table, RSCACHE_DAT2_CONFIG_KIND_VARPLAYER);

        if( varps && RSCache_Dat2DiskArchiveInitMetadata(disk, varps) )
        {
            int max_id = -1;

            for( int i = 0; i < varps->file_count; i++ )
            {
                int id = varps->file_ids ? varps->file_ids[i] : i;

                if( id > max_id )
                    max_id = id;
            }
            g_client_varp_count = max_id + 1;
        }
        if( varps )
            RSCache_Dat2DiskArchiveFree(varps);
    }

    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_VARBIT);
    if( !archive || !RSCache_Dat2DiskArchiveInitMetadata(disk, archive) )
    {
        if( archive )
            RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        fprintf(stderr, "torirsserver: no varbit config group in '%s'\n", cache_dir);
        return 0;
    }

    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size,
                                          archive->file_count);
    if( !files || !archive->file_ids )
    {
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    for( int i = 0; i < archive->file_count; i++ )
    {
        if( archive->file_ids[i] > highest )
            highest = archive->file_ids[i];
    }
    if( highest < 0 )
    {
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        RSCache_Dat2DiskFree(disk);
        return 0;
    }

    g_varbit_count = highest + 1;
    g_varbits = calloc((size_t)g_varbit_count, sizeof(*g_varbits));
    assert(g_varbits);
    /* -1 is "absent". A zeroed slot would read as varp 0, which is a real varp,
     * so every unknown varbit would silently alias it. */
    for( int i = 0; i < g_varbit_count; i++ )
        g_varbits[i].basevar = -1;

    for( int i = 0; i < files->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2ConfigVarbit entry;

        if( id < 0 || id >= g_varbit_count || files->file_sizes[i] <= 0 )
            continue;
        memset(&entry, 0, sizeof(entry));
        RSCache_Dat2ConfigVarbitDecodeInplace(&entry, files->files[i], files->file_sizes[i]);
        g_varbits[id].basevar = entry.basevar;
        g_varbits[id].startbit = (uint8_t)entry.startbit;
        g_varbits[id].endbit = (uint8_t)entry.endbit;
        loaded++;
        RSCache_Dat2ConfigVarbitFreeInplace(&entry);
    }

    /* The reverse index. Sized off the varp table rather than the highest
     * basevar seen: a carrier query for a varp past the end must answer 0, not
     * read off the array. */
    g_carrier_count = TORIRSSERVER_VARP_COUNT;
    g_carrier_bits = calloc((size_t)g_carrier_count, sizeof(*g_carrier_bits));
    /* The ceiling is measured over every basevar, including any past the end of
     * the reverse index — that case is exactly the one worth hearing about. */
    for( int i = 0; i < g_varbit_count; i++ )
        if( g_varbits[i].basevar > g_max_basevar )
            g_max_basevar = g_varbits[i].basevar;

    if( g_carrier_bits )
    {
        int carriers = 0;
        int max_base = g_max_basevar;

        for( int i = 0; i < g_varbit_count; i++ )
        {
            int base = g_varbits[i].basevar;

            if( base < 0 || base >= g_carrier_count )
                continue;
            if( g_carrier_bits[base]++ == 0 )
                carriers++;
        }
        fprintf(stderr, "torirsserver: %d varp(s) carry varbits (highest basevar %d)\n", carriers,
                max_base);
        /*
         * The cache's varps and the tree's must not overlap, and this is the
         * only place that can tell.
         *
         * `tools/ss_allocate.py` hands out server varp ids from one past the
         * largest the *content tree* knows about, which it reads from
         * `configs/all.varp` — an export, and only as current as whenever it
         * was taken. TORIRSSERVER_VARP_CACHE_MAX is the engine's copy of the same
         * number. If the cache actually running carries varbits above that
         * line, then every one of them shares a slot with a server varp, and
         * the two corrupt each other in both directions: `~player_combat_stat`
         * writing `%com_slashattack` looks like a whole-varp write over
         * somebody's packed bits, and the packed bits' owner overwrites the
         * combat stat right back.
         *
         * It is not a fatal, because the collision only bites the varps that
         * actually overlap and the world is still worth booting -- but it is
         * the root cause of any whole-varp complaint about a varp at or above
         * the line, so say it once, here, rather than leaving each write to be
         * misdiagnosed as content writing the wrong thing.
         */
        if( max_base >= TORIRSSERVER_VARP_CACHE_MAX )
            fprintf(stderr,
                    "torirsserver: this cache carries varbits up to varp %d, past the %d the "
                    "engine assumes — server varps %d..%d collide with cache varbits; "
                    "re-export configs/all.varp, re-run tools/ss_allocate.py and raise "
                    "TORIRSSERVER_VARP_CACHE_MAX\n",
                    max_base, TORIRSSERVER_VARP_CACHE_MAX - 1, TORIRSSERVER_VARP_CACHE_MAX, max_base);
    }
    else
        g_carrier_count = 0;

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);
    fprintf(stderr, "torirsserver: varbit table loaded (%d records from %s)\n", loaded, cache_dir);
    fprintf(stderr, "torirsserver: client varp capacity %d from %s\n",
            g_client_varp_count, cache_dir);
    return loaded;
}

int
ToriRSServer_VarpClientCount(void)
{
    return g_client_varp_count;
}

void
ToriRSServer_VarbitFree(void)
{
    free(g_varbits);
    g_varbits = NULL;
    g_varbit_count = 0;
    g_client_varp_count = 0;
    free(g_carrier_bits);
    g_carrier_bits = NULL;
    g_carrier_count = 0;
    g_max_basevar = -1;
    g_patching = 0;
}

int
ToriRSServer_VarbitMaxBasevar(void)
{
    return g_max_basevar;
}

int
ToriRSServer_VarbitCarrierBits(int varp)
{
    if( !g_carrier_bits || varp < 0 || varp >= g_carrier_count )
        return 0;
    return g_carrier_bits[varp];
}

int
ToriRSServer_VarbitPatching(void)
{
    return g_patching;
}

static const struct VarbitRange*
varbit_range(int varbit_id)
{
    if( !g_varbits || varbit_id < 0 || varbit_id >= g_varbit_count )
        return NULL;
    if( g_varbits[varbit_id].basevar < 0 )
        return NULL;
    return &g_varbits[varbit_id];
}

int
ToriRSServer_VarbitGet(
    const struct ToriRSServerPlayer* player,
    int varbit_id)
{
    const struct VarbitRange* range = varbit_range(varbit_id);
    int width;
    uint32_t mask;

    if( !range || range->basevar >= TORIRSSERVER_VARP_COUNT )
        return 0;
    width = range->endbit - range->startbit + 1;
    if( width <= 0 || width > 32 )
        return 0;
    mask = width >= 32 ? 0xffffffffu : ((1u << width) - 1u);
    return (int)(((uint32_t)player->varps[range->basevar] >> range->startbit) & mask);
}

int
ToriRSServer_VarbitSet(
    struct ToriRSServer* srv,
    int varbit_id,
    int value)
{
    return ToriRSServer_VarbitSetOn(srv, srv->active_player, varbit_id, value);
}

/*
 * The same write, aimed at a NAMED player rather than at whoever's turn it is.
 *
 * `srv->active_player` is "whose turn is the server taking"; a script's active
 * player is whoever `huntnext` (or `p_finduid`) last selected, and inside an
 * `[ai_timer]` those are not the same thing — the npc phase has no player turn
 * at all. `SS_OP_PUSH_VARBIT` has always read from the SCRIPT's player and
 * `SS_OP_POP_VARBIT` wrote through the server's, so a script that walked a hunt
 * and wrote a varbit to each player read from one and wrote to another.
 *
 * That is a boss broadcasting anything to its room. The Theatre's health bar is
 * the case that found it: `~tob_hud_broadcast` hunts every player in the room
 * and sets three varbits on each, from the Maiden's own timer — and not one
 * VARP packet reached any client, because every write landed on whatever
 * `srv->active_player` happened to be (and on nobody at all when it was null).
 * The register said the bar had been pushed, the interface was mounted, the
 * client's `if_setonvartransmit` dispatch was in perfect order, and the wire
 * was empty.
 */
int
ToriRSServer_VarbitSetOn(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int varbit_id,
    int value)
{
    const struct VarbitRange* range = varbit_range(varbit_id);
    int width;
    uint32_t mask;
    uint32_t current;

    if( !range || range->basevar >= TORIRSSERVER_VARP_COUNT )
        return -1;
    width = range->endbit - range->startbit + 1;
    if( width <= 0 || width > 32 )
        return -1;
    mask = width >= 32 ? 0xffffffffu : ((1u << width) - 1u);

    if( !player )
        return -1;
    current = (uint32_t)player->varps[range->basevar];
    current &= ~(mask << range->startbit);
    current |= ((uint32_t)value & mask) << range->startbit;
    /* Through the ordinary varp write, so the transmit gate and the
     * small/large encoder choice apply exactly as they would to a plain varp —
     * a varbit is a *view* of a varp, not a second kind of variable.
     *
     * The flag is what tells the carrier backstop that this write is the
     * *correct* way to touch a shared container. Without it the one path that
     * must write a carrier would be the only thing the check ever saw. */
    g_patching++;
    ToriRSServer_WorldSetVarpOn(srv, player, range->basevar, (int)current);
    g_patching--;
    return range->basevar;
}
